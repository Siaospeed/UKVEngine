#ifndef UKVENGINE_UKV_CLIENT_H_
#define UKVENGINE_UKV_CLIENT_H_

#include <arpa/inet.h>
#include <unistd.h>

#include <cstdint>
#include <string>
#include <vector>

class UkvClient {
public:
    UkvClient() = default;
    ~UkvClient();

    int ParseArgs(int argc, char* argv[]);
    std::vector<std::string> ParseCommandLine(const std::string& line);
    void Run();

    [[nodiscard]] const std::string& get_host() const {
        return host_;
    }

    [[nodiscard]] std::uint16_t get_port() const {
        return port_;
    }

    [[nodiscard]] std::string get_endpoint() const {
        if (host_.find(':') != std::string::npos) {
            return "[" + host_ + "]:" + std::to_string(port_);
        }
        return host_ + ":" + std::to_string(port_);
    }

private:
    std::string host_ = "127.0.0.1";
    uint16_t port_ = 8586;
    int client_fd_ = -1;
    sockaddr_in6 server_addr_{};

    bool InitNetwork();
};

#endif // !UKVENGINE_UKV_CLIENT_H_
