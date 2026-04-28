#ifndef UKVENGINE_UKVD_MAIN_H_
#define UKVENGINE_UKVD_MAIN_H_

#include <atomic>

extern std::atomic<bool> g_running;

inline void SignalHandler(int signum) {
    g_running = false;
}

#endif // !UKVENGINE_UKVD_MAIN_H_
