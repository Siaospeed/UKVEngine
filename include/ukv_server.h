#ifndef UKVENGINE_UKV_SERVER_H_
#define UKVENGINE_UKV_SERVER_H_

#include <netinet/in.h>
#include <sys/epoll.h>

#include <filesystem>
#include <fstream>
#include <functional>
#include <memory>
#include <shared_mutex>
#include <unordered_map>
#include <vector>

#include "sharded_lru_cache.h"
#include "resp_parser.h"
#include "thread_pool.h"

extern std::atomic<bool> g_running;

class UkvServer {
public:
    UkvServer(ShardedLruCache* cache);
    ~UkvServer();

    int ParseArgs(int argc, char* argv[]);
    void Run();

    uint16_t get_port() const {
        return port_;
    }

private:
    using CommandHandler = std::function<void(std::vector<std::string>&&, std::string&)>;
    std::unordered_map<std::string, CommandHandler> command_handlers_;

    void DoGet(std::vector<std::string>&& args, std::string& out_buffer);
    void DoSet(std::vector<std::string>&& args, std::string& out_buffer);
    void DoDelete(std::vector<std::string>&& args, std::string& out_buffer);
    void DoConfig(std::vector<std::string>&& args, std::string& out_buffer);

private:
    uint16_t port_ = 8586;
    ShardedLruCache* cache_;

    int listen_fd_ = -1;
    int epoll_fd_ = -1;
    sockaddr_in6 server_addr_{};

    std::vector<epoll_event> active_events_;
    std::unique_ptr<ThreadPool> pool_;
    std::unordered_map<int, RespParser> client_parsers_;

    inline static const std::filesystem::path kAofBasePath = "/var/lib/ukvd";
    inline static const std::filesystem::path kAofFilePath = kAofBasePath / "ukv.aof";
    std::ofstream aof_file_;

    std::shared_mutex map_mutex_;
    std::mutex aof_mutex_;

    enum class ServerState {
        OK,
        START_SOCKET_FAILED
    };

    bool InitNetwork();
    void HandleNewConnection();
    void HandleClientData(int active_fd);

    void AppendAof(const std::vector<std::string>& args);
    void ReplayAof();
};

#endif // !UKVENGINE_UKV_SERVER_H_
