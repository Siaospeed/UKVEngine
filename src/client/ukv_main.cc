#include "ukv_main.h"

#include <unistd.h>

#include <atomic>
#include <csignal>
#include <iostream>
#include <string>

#include "ukv_client.h"

std::atomic<bool> g_running(true);

int main(int argc, char* argv[]) {
    signal(SIGTERM, SignalHandler);
    signal(SIGINT, SignalHandler);

    UkvClient client;
    if (client.ParseArgs(argc, argv)) {
        return 1;
    }

    std::cout << "UKVEngine Client connecting to "
              << client.get_endpoint() << "...\n";
    client.Run();

    return 0;
}
