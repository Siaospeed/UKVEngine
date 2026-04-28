#ifndef UKVENGINE_UKV_MAIN_H_
#define UKVENGINE_UKV_MAIN_H_

#include <unistd.h>

#include <atomic>

extern std::atomic<bool> g_running;

inline void SignalHandler(int signum) {
    close(STDIN_FILENO);
    g_running = false;
}

#endif // !UKVENGINE_UKV_MAIN_H_
