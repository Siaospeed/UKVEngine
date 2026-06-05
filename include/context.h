#ifndef UKVENGINE_CONTEXT_H
#define UKVENGINE_CONTEXT_H
#include <atomic>

#include "resp_parser.h"

enum class EpollContextType {
    LISTENER,
    CLIENT
};

struct EpollContext {
    EpollContextType type;
    int fd;
};

struct ClientContext : EpollContext {
    RespParser parser;

    std::string out_buffer;
    size_t write_pos = 0;

    std::atomic<bool> is_closed{false};

    explicit ClientContext(int client_fd)
        : EpollContext{EpollContextType::CLIENT, client_fd} {}
};

#endif // !UKVENGINE_CONTEXT_H
