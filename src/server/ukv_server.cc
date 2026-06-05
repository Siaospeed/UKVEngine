#include "ukv_server.h"

#include <arpa/inet.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <netinet/tcp.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <unistd.h>

#include <algorithm>
#include <cstring>
#include <iostream>
#include <thread>

#include "resp_builder.h"
#include "thread_pool.h"
#include "utils.h"

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
    for (const auto& [client_fd, x] : clients_) {
        close(client_fd);
    }

    clients_.clear();

    if (listen_fd_ != -1) {
        close(listen_fd_);
    }

    if (epoll_fd_ != -1) {
        close(epoll_fd_);
    }

    aof_flusher_.join();

    if (aof_file_.is_open()) {
        aof_file_.close();
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
                    std::string msg;
                    msg.append("Bad port \'").append(optarg).append("\'.");
                    LOG_ERROR(msg);
                    return 1;
                }

            default:
                std::string msg;
                msg.append("Usage: ").append(argv[0]).append(" [-p port]");
                LOG_ERROR(msg);
                return 1;
        }
    }

    return 0;
}

void UkvServer::Run() {
    if (!InitNetwork()) {
        return;
    }

    if (std::error_code ec; !utils::PathExistOrCreate(kAofBasePath, ec)) {
        LOG_WARN(ec.message());
        LOG_WARN("Failed to open AOF directory, persistence disabled");
    }

    ReplayAof();

    aof_file_.open(kAofFilePath, std::ios::app | std::ios::binary);
    if (!aof_file_.is_open()) {
        LOG_WARN("Failed to open AOF file, persistence disabled");
    }

    aof_flusher_ = std::thread([this] {
        while (g_running) {
            std::this_thread::sleep_for(std::chrono::seconds(1));

            {
                std::lock_guard<std::mutex> lock(aof_buffer_mutex_);
                if (active_aof_buffer_.empty()) { continue; }
                std::swap(active_aof_buffer_, backend_aof_buffer_);
            }

            if (aof_file_.is_open()) {
                aof_file_.write(backend_aof_buffer_.data(), backend_aof_buffer_.size());
                aof_file_.flush();
            }

            backend_aof_buffer_.clear();
        }

        std::lock_guard<std::mutex> lock(aof_buffer_mutex_);
        if (aof_file_.is_open() && !active_aof_buffer_.empty()) {
            aof_file_.write(active_aof_buffer_.data(), active_aof_buffer_.size());
            aof_file_.flush();
            active_aof_buffer_.clear();
        }
    });

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
            auto* base = static_cast<EpollContext*>(active_events_[i].data.ptr);

            if (base->type == EpollContextType::LISTENER) {
                HandleNewConnection();
            }
            else {
                auto* context = static_cast<ClientContext*>(base);
                HandleClientEvent(context, active_events_[i].events);
            }
        }
    }
}

bool UkvServer::InitNetwork() {
    listen_fd_ = socket(AF_INET6, SOCK_STREAM, 0);
    listen_context_.fd = listen_fd_;

    if (listen_fd_ == -1) {
        LOG_FATAL("Cannot create socket!");
    }

    int listen_fd_flag = fcntl(listen_fd_, F_GETFL, 0);
    fcntl(listen_fd_, F_SETFL, listen_fd_flag | O_NONBLOCK);

    // Disable 'IPv6-only' to support dual-stack socket.
    int opt = 0;
    if (setsockopt(listen_fd_, IPPROTO_IPV6, IPV6_V6ONLY, &opt, sizeof(opt)) == -1) {
        LOG_ERROR("Failed to enable dual-stack socket");
    }

    // Enable port reuse to avoid 'address already in use' errors during server restarts.
    int reuse = 1;
    setsockopt(listen_fd_, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));

    server_addr_.sin6_family = AF_INET6;
    server_addr_.sin6_addr = in6addr_any;
    server_addr_.sin6_port = htons(port_);

    if (bind(listen_fd_, reinterpret_cast<sockaddr*>(&server_addr_), sizeof(server_addr_)) == -1) {
        std::string port_str;
        utils::FastIntToString(port_, port_str);
        LOG_ERROR("Failed to bind port " + port_str + ". Is it already in use?");
        return false;
    }

    if (listen(listen_fd_, 16384) == -1) {
        LOG_FATAL("Cannot prepare to accept connections!");
    }

    epoll_fd_ = epoll_create1(0);
    epoll_event event{};
    event.events = EPOLLIN | EPOLLET;
    event.data.ptr = &listen_context_;
    epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, listen_fd_, &event);

    return true;
}

void UkvServer::HandleNewConnection() {
    for (;;) {
        sockaddr_in6 client_addr{};
        socklen_t client_addr_len = sizeof(client_addr);

        int client_fd = accept4(
            listen_fd_,
            reinterpret_cast<sockaddr*>(&client_addr),
            &client_addr_len,
            SOCK_NONBLOCK | SOCK_CLOEXEC
        );

        if (client_fd >= 0) {
            int flag = 1;
            if (setsockopt(client_fd, IPPROTO_TCP, TCP_NODELAY, &flag, sizeof(flag))) {
                LOG_ERROR("Failed to set TCP_NODELAY");
            }

            auto owned_client_context = std::make_unique<ClientContext>(client_fd);
            auto* client_context = owned_client_context.get();

            {
                std::unique_lock<std::shared_mutex> lock(clients_mutex_);
                clients_.emplace(client_fd, std::move(owned_client_context));
            }

            epoll_event event{};
            event.events = EPOLLIN | EPOLLRDHUP | EPOLLET | EPOLLONESHOT;
            event.data.ptr = client_context;

            if (epoll_ctl(epoll_fd_, EPOLL_CTL_ADD, client_fd, &event) < 0) {
                int saved_errno = errno;

                {
                    std::unique_lock<std::shared_mutex> lock(clients_mutex_);
                    clients_.erase(client_fd);
                }

                close(client_fd);

                std::string error_msg;
                error_msg.append("epoll_ctl ADD failed: ")
                         .append(std::to_string(saved_errno))
                         .append(": ")
                         .append(std::strerror(saved_errno));
                LOG_ERROR(error_msg);
            }

            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            break;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == ECONNABORTED) {
            LOG_ERROR("accept4 failed: ECONNABORTED");
            continue;
        }

        std::string error_msg;
        error_msg.append("accept4 failed: ")
                 .append(std::to_string(errno))
                 .append(": ")
                 .append(std::strerror(errno));
        LOG_ERROR(error_msg);

        break;
    }
}

void UkvServer::HandleClientEvent(ClientContext* client_context, uint32_t events) {
    pool_->EnQueue([this, client_context, events] {
        if (client_context == nullptr || client_context->is_closed.load()) {
            return;
        }

        if (events & (EPOLLERR | EPOLLHUP)) {
            CloseClient(client_context);
            return;
        }

        if (events & (EPOLLIN | EPOLLRDHUP)) {
            if (!ReadAll(client_context)) {
                return;
            }

            ProcessCommands(client_context);
        }

        if ((events & EPOLLOUT) || client_context->write_pos < client_context->out_buffer.size()) {
            if (!FlushOutput(client_context)) {
                return;
            }
        }

        RearmClient(client_context);
    });
}

bool UkvServer::ReadAll(ClientContext* client_context) {
    char buffer[16384] = {};

    for (;;) {
        ssize_t bytes_read = read(client_context->fd, buffer, sizeof(buffer));

        if (bytes_read > 0) {
            client_context->parser.AppendData(buffer, bytes_read);
            continue;
        }

        if (bytes_read == 0) {
            CloseClient(client_context);
            return false;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        LOG_ERROR(std::string("Failed to read: ") +
                  std::to_string(errno) + ": " +
                  std::strerror(errno));
        CloseClient(client_context);
        return false;
    }
}

void UkvServer::ProcessCommands(ClientContext* client_context) {
    std::optional<std::vector<std::string>> opt_command;

    while ((opt_command = client_context->parser.NextCommand()) != std::nullopt) {
        auto command = std::move(opt_command.value());
        if (command.empty()) {
            continue;
        }

        std::string& action = command[0];
        std::transform(action.begin(), action.end(), action.begin(), ::toupper);

        auto iter = command_handlers_.find(action);
        if (iter != command_handlers_.end()) {
            iter->second(std::move(command), client_context->out_buffer);
        }
        else {
            RespBuilder::Error("unknown action", client_context->out_buffer);
        }
    }
}

bool UkvServer::FlushOutput(ClientContext* client_context) {
    while (client_context->write_pos < client_context->out_buffer.size()) {
        const char* data = client_context->out_buffer.data() + client_context->write_pos;
        size_t left_size = client_context->out_buffer.size() - client_context->write_pos;

        ssize_t bytes_write = write(client_context->fd, data, left_size);

        if (bytes_write > 0) {
            client_context->write_pos += bytes_write;
            continue;
        }

        if (errno == EINTR) {
            continue;
        }

        if (errno == EAGAIN || errno == EWOULDBLOCK) {
            return true;
        }

        LOG_ERROR(std::string("Failed to write: ") +
                  std::to_string(errno) + ": " +
                  std::strerror(errno));
        CloseClient(client_context);
        return false;
    }

    client_context->out_buffer.clear();
    client_context->write_pos = 0;
    return true;
}
void UkvServer::RearmClient(ClientContext* client_context) {
    if (client_context == nullptr || client_context->is_closed.load()) {
        return;
    }

    epoll_event re_event{};
    re_event.events = EPOLLIN | EPOLLRDHUP | EPOLLET | EPOLLONESHOT;

    if (client_context->write_pos < client_context->out_buffer.size()) {
        re_event.events |= EPOLLOUT;
    }

    re_event.data.ptr = client_context;

    if (epoll_ctl(epoll_fd_, EPOLL_CTL_MOD, client_context->fd, &re_event) < 0) {
        LOG_ERROR(std::string("epoll_ctl MOD failed: ") +
                  std::to_string(errno) + ": " +
                  std::strerror(errno));
        CloseClient(client_context);
    }
}

void UkvServer::CloseClient(ClientContext* client_context) {
    if (client_context == nullptr) {
        return;
    }

    if (client_context->is_closed.exchange(true)) {
        return;
    }

    int fd = client_context->fd;
    epoll_ctl(epoll_fd_, EPOLL_CTL_DEL, fd, nullptr);
    close(fd);

    {
        std::unique_lock<std::shared_mutex> lock(clients_mutex_);
        clients_.erase(fd);
    }
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

    AppendAof(args);

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

    AppendAof(args);

    for (size_t i = 1; i < args.size(); i++) {
        std::string& key = args[i];
        success_delete_count += cache_->Delete(key);
    }

    RespBuilder::Integer(success_delete_count, out_buffer);
}

void UkvServer::DoConfig(std::vector<std::string>&& args, std::string& out_buffer) {
    out_buffer.append("*0\r\n");
}

void UkvServer::AppendAof(const std::vector<std::string>& args) {
    if (!aof_file_.is_open()) {
        return;
    }

    std::string line;
    RespBuilder::BulkStringArray(args, line);

    std::lock_guard<std::mutex> lock(aof_buffer_mutex_);
    active_aof_buffer_.append(line);
}

void UkvServer::ReplayAof() {
    std::ifstream file(kAofFilePath, std::ios::binary);
    if (!file.is_open()) {
        return;
    }

    std::string content((std::istreambuf_iterator<char>(file)),
                         std::istreambuf_iterator<char>());
    if (content.empty()) return;

    RespParser parser;
    parser.AppendData(content.data(), content.size());

    std::optional<std::vector<std::string>> opt_command;
    std::string dummy_out;
    while ((opt_command = parser.NextCommand()) != std::nullopt) {
        auto command = std::move(opt_command.value());
        if (command.empty()) continue;

        std::string& action = command[0];
        std::transform(action.begin(), action.end(), action.begin(), ::toupper);

        auto iter = command_handlers_.find(action);
        if (iter != command_handlers_.end()) {
            iter->second(std::move(command), dummy_out);
            dummy_out.clear();
        }
    }

    LOG_INFO("AOF Replay complete");
}
