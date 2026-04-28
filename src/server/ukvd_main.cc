#include "ukvd_main.h"

#include <atomic>
#include <csignal>
#include <iostream>

#include "sharded_lru_cache.h"
#include "ukv_server.h"

std::atomic<bool> g_running(true);

int main(int argc, char* argv[]) {
    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);

    ShardedLruCache cache(16384, 64);

    UkvServer server(&cache);
    if (server.ParseArgs(argc, argv)) {
        return 1;
    }

    std::cout << "UKVEngine Daemon starting at port " << server.get_port() << "...\n";
    server.Run();

    return 0;
}
