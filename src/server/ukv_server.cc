#include "ukv_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <iostream>
#include <thread>

#include "resp_builder.h"
#include "thread_pool.h"

UkvServer::UkvServer(ShardedLruCache* cache)
        : cache_(cache) {
    pool_ = std::make_unique<ThreadPool>(std::thread::hardware_concurrency() * 2);
    active_events_.resize(16384);

    command_handlers_["SET"] = [this](
        std::vector<std::string>&& args, std::string& out_buffer) {
        DoSet(std::move(args), out_buffer);
    };
    command_handlers_["GET"] = [this](
        std::vector<std::string>&& args, std::string& out_buffer) {
        DoGet(std::move(args), out_buffer);
    };
    command_handlers_["DEL"] = [this](
        std::vector<std::string>&& args, std::string& out_buffer) {
        DoDelete(std::move(args), out_buffer);
    };
    command_handlers_["CONFIG"] = [this](
        std::vector<std::string>&& args, std::string& out_buffer) {
        DoConfig(std::move(args), out_buffer);
    };
}

UkvServer::~UkvServer() {
    for (const auto& [client_fd, x] : client_parsers_) {
        close(client_fd);
    }
    client_parsers_.clear();

    if (listen_fd_ != -1) {
        close(listen_fd_);
    }

    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }
}

int UkvServer::ParseArgs(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "h:p:")) != -1) {
        switch (opt) {
            case 'p':
                try {
                    port_ = std::stoi(optarg);
                    break;
                } catch (std::invalid_argument&) {
                    std::cerr << "[FATAL] Bad port \'" << optarg << "\'.\n";
                    return 1;
                }

            default:
                std::cerr << "Usage: " << argv[0] << " [-p port]\n";
                return 1;
        }
    }

    return 0;
}

void UkvServer::Run() {
    if (!InitNetwork()) {
        return;
    }

    while (g_running) {
        int ready_count = epoll_wait(epoll_fd_, active_events_.data(),
                                     active_events_.size(), -1);

        if (ready_count == -1) {
            if (errno == EINTR) {
                continue;
            }
            break;
        }

        for (int i = 0; i < ready_count; i++) {
            int active_fd = active_events_[i].data.fd;
            if (active_fd == listen_fd_) {
                HandleNewConnection();
            }
            else {
                HandleClientData(active_fd);
            }
        }
    }
}

bool UkvServer::InitNetwork() {
    listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    if (listen_fd_ == -1) {
        std::cerr << "[FATAL] Cannot create socket!\n";
        return false;
    }

    // Disable 'IPv6-only' to support dual-stack socket.
    int opt = 0;
    if (setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) == -1) {
        std::cerr << "[FATAL] ";
        return false;
    }

    // Enable port reuse to avoid 'address already in use' errors during server restarts.
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    server_addr_.sin6_family = AF_INET6;
    server_addr_.sin6_addr = in6addr_any;
    server_addr_.sin6_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_)) == -1) {
        std::cerr << "[FATAL] Failed to bind port " << port_
                  << ". Is it already in use?\n";
        return false;
    }

    if (listen(listen_fd_, 16384) == -1) {
        std::cerr << "[FATAL] Cannot prepare to accept connections!\n";
        return false;
    }

    epoll_fd_ = epoll_create1(0);
    epoll_event event;
    event.events = EPOLLIN;
    event.data.fd = listen_fd_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);

    return true;
}

void UkvServer::HandleNewConnection() {
    epoll_event event;
    sockaddr_in6 client_addr{};
    socklen_t client_addr_len = sizeof(client_addr);
    int client_fd = accept(listen_fd_, reinterpret_cast<sockaddr*>(&client_addr), &client_addr_len);

    {
        std::lock_guard<std::shared_mutex> lock(map_mutex_);
        client_parsers_[client_fd] = {};
    }

    event.events = EPOLLIN | EPOLLONESHOT;
    event.data.fd = client_fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event);
}

void UkvServer::HandleClientData(int active_fd) {
    pool_->EnQueue([active_fd, this] {
        char buffer[16384] = {};

        ssize_t bytes_read = read(active_fd, buffer, sizeof(buffer) - 1);
        if (bytes_read > 0) {
            RespParser* parser = nullptr;
            {
                std::lock_guard<std::shared_mutex> lock(map_mutex_);
                auto iter = client_parsers_.find(active_fd);
                if (iter != client_parsers_.end()) {
                    parser = &(iter->second);
                }
            }

            if (parser != nullptr) {
                parser->AppendData(buffer, bytes_read);
                std::optional<std::vector<std::string>> opt_command;
                thread_local std::string out_buffer;
                out_buffer.reserve(8192);
                out_buffer.clear();
                while ((opt_command = parser->NextCommand()) != std::nullopt) {
                    auto command = std::move(opt_command.value());
                    if (command.empty()) {
                        continue;
                    }

                    std::string& action = command[0];
                    auto iter = command_handlers_.find(action);
                    if (iter != command_handlers_.end()) {
                        iter->second(std::move(command), out_buffer);
                        write(active_fd, out_buffer.data(), out_buffer.size());
                    }
                    else {
                        RespBuilder::Error("unknown action", out_buffer);
                        write(active_fd, out_buffer.data(), out_buffer.size());
                    }
                }
            }

            epoll_event re_event;
            re_event.events = EPOLLIN | EPOLLONESHOT;
            re_event.data.fd = active_fd;
            epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, active_fd, &re_event);
        }
        else if (bytes_read == 0) {
            close(active_fd);
            std::lock_guard<std::shared_mutex> lock(map_mutex_);
            client_parsers_.erase(active_fd);
        }
    });
}

void UkvServer::DoGet(std::vector<std::string>&& args, std::string& out_buffer) {
    if (args.size() != 2) {
        RespBuilder::Error("wrong number of arguments for 'GET' command", out_buffer);
        return;
    }

    const std::string& key = args[1];
    std::string get_output;
    if (!cache_->Get(key, &get_output)) {
        RespBuilder::NullBulkString(out_buffer);
        return;
    }

    RespBuilder::BulkString(get_output, out_buffer);
}

void UkvServer::DoSet(std::vector<std::string>&& args, std::string& out_buffer) {
    if (args.size() != 3) {
        RespBuilder::Error("wrong number of arguments for 'SET' command", out_buffer);
        return;
    }

    std::string& key = args[1];
    std::string& value = args[2];

    cache_->Put(std::move(key), std::move(value));
    RespBuilder::SimpleString("OK", out_buffer);
}

void UkvServer::DoDelete(std::vector<std::string>&& args, std::string& out_buffer) {
    size_t success_delete_count = 0;

    if (args.size() <= 1) {
        RespBuilder::Error("wrong number of arguments for 'DEL' command", out_buffer);
        return;
    }

    for (size_t i = 1; i < args.size(); i++) {
        std::string& key = args[i];
        success_delete_count += cache_->Delete(key);
    }

    RespBuilder::Integer(success_delete_count, out_buffer);
}

void UkvServer::DoConfig(std::vector<std::string>&& args, std::string& out_buffer) {
    out_buffer.append("*0\r\n");
}
