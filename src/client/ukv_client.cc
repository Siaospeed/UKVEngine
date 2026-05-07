#include "ukv_client.h"

#include <netdb.h>

#include <iostream>
#include <sstream>
#include <vector>

#include "resp_builder.h"
#include "ukv_main.h"
#include "utils.h"

UkvClient::~UkvClient() {
    if (client_fd_ != -1) {
        close(client_fd_);
    }
}

int UkvClient::ParseArgs(int argc, char* argv[]) {
    int opt;
    while ((opt = getopt(argc, argv, "h:p:")) != -1) {
        switch (opt) {
            case 'h':
                host_ = optarg;
                break;
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
                msg.append("Usage: ").append(argv[0]).append(" [-h host] [-p port]");
                LOG_ERROR(msg);
                return 1;
        }
    }

    return 0;
}

std::vector<std::string> UkvClient::ParseCommandLine(const std::string& line) {
    std::vector<std::string> args;
    std::string current_arg;
    bool in_quotes = false;
    bool escape_next = false;

    for (char c : line) {
        if (escape_next) {
            if (c == 'n') current_arg += '\n';
            else if (c == 't') current_arg += '\t';
            else if (c == 'r') current_arg += '\r';
            else current_arg += c;
            escape_next = false;
        } else if (c == '\\') {
            escape_next = true;
        } else if (c == '"') {
            in_quotes = !in_quotes;
        } else if (c == ' ' && !in_quotes) {
            if (!current_arg.empty()) {
                args.push_back(current_arg);
                current_arg.clear();
            }
        } else {
            current_arg += c;
        }
    }

    if (!current_arg.empty()) {
        args.push_back(current_arg);
    }

    return args;
}

bool UkvClient::InitNetwork() {
    addrinfo hints{};
    addrinfo *result, *rp;

    hints.ai_family = AF_UNSPEC;
    hints.ai_socktype = SOCK_STREAM;

    std::string port_str = std::to_string(port_);

    int s = getaddrinfo(host_.c_str(), port_str.c_str(), &hints, &result);
    if (s != 0) {
        std::string msg;
        msg.append("DNS resolution failed: ").append(gai_strerror(s));
        LOG_ERROR(msg);
        return false;
    }

    for (rp = result; rp != nullptr; rp = rp->ai_next) {
        client_fd_ = socket(rp->ai_family, rp->ai_socktype, rp->ai_protocol);
        if (client_fd_ == -1) {
            continue;
        }

        if (connect(client_fd_, rp->ai_addr, rp->ai_addrlen) != -1) {
            break;
        }

        close(client_fd_);
        client_fd_ = -1;
    }

    freeaddrinfo(result);

    if (rp == nullptr) {
        LOG_ERROR("Connection failed! Is ukvd running on " + get_endpoint() + "?");
        return false;
    }

    LOG_INFO("UKVEngine Client successfully connected to " + get_endpoint() + "...");
    return true;
}

void UkvClient::Run() {
    if (!InitNetwork()) {
        return;
    }

    while (g_running) {
        std::cout << "ukv> ";
        std::string line;
        if (!std::getline(std::cin, line)) {
            break;
        }
        if (line == "exit") {
            break;
        }
        if (line.empty()) {
            continue;
        }

        auto args = ParseCommandLine(line);

        std::string request_resp_str;
        RespBuilder::BulkStringArray(args, request_resp_str);
        send(client_fd_, request_resp_str.c_str(), request_resp_str.length(), 0);

        char buffer[4096];
        ssize_t bytes_recv = recv(client_fd_, buffer, sizeof(buffer) - 1, 0);
        if (bytes_recv <= 0) {
            return;
        }
        buffer[bytes_recv] = '\0';

        std::string output(buffer);
        switch (output[0]) {
            case '+':
                std::cout << output.substr(1, output.length() - 3) << std::endl;
                break;
            case ':':
                std::cout << "(integer) "
                        << output.substr(1, output.length() - 3)
                        << std::endl;
                break;
            case '-':
                std::cout << "(error) "
                        << output.substr(5, output.length() - 7)
                        << std::endl;
                break;
            case '$':
                if (output.substr(1, 2) == "-1") {
                    std::cout << "(nil)" << std::endl;
                } else {
                    std::string bulk_string_len_str;
                    size_t bulk_string_len = 0;
                    for (size_t i = 1; i + 1 < output.length(); i++) {
                        if (output[i] == '\r' && output[i + 1] == '\n') {
                            bulk_string_len = std::stoull(bulk_string_len_str);
                            break;
                        }
                        bulk_string_len_str.push_back(output[i]);
                    }

                    std::cout << '\"'
                            << output.substr(bulk_string_len_str.length() + 3,
                                             bulk_string_len)
                            << '\"'
                            << std::endl;
                    break;
                }
            default:;
        }
    }
}
