#include "ukvd_main.h"

#include <atomic>
#include <csignal>
#include <iostream>

#include "sharded_lru_cache.h"
#include "ukv_server.h"
#include "utils.h"

std::atomic<bool> g_running(true);

int main(int argc, char* argv[]) {
    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);

    ShardedLruCache cache(16384, 64);

    UkvServer server(&cache);
    if (server.ParseArgs(argc, argv)) {
        return 1;
    }

    std::string port_str;
    utils::FastIntToString(server.get_port(), port_str);
    LOG_INFO("UKVEngine Daemon starting at port " + port_str + "...");
    server.Run();

    return 0;
}
