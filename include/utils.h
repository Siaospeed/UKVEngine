#ifndef UKVENGINE_UTILS_H_
#define UKVENGINE_UTILS_H_

#define ANSI_COLOR_RED     "\x1b[31m"
#define ANSI_COLOR_GREEN   "\x1b[32m"
#define ANSI_COLOR_YELLOW  "\x1b[33m"
#define ANSI_COLOR_RESET   "\x1b[0m"

#ifdef NDEBUG
#define LOG_DEBUG(msg) do {} while(0)
#else // NDEBUG
#define LOG_DEBUG(msg) std::cerr << "[DEBUG] " << msg << "\n"
#endif // !NDEBUG
#define LOG_INFO(msg)  std::cout << ANSI_COLOR_GREEN  << "[INFO]  " << ANSI_COLOR_RESET << msg << "\n"
#define LOG_WARN(msg)  std::cerr << ANSI_COLOR_YELLOW << "[WARN]  " << ANSI_COLOR_RESET << msg << "\n"
#define LOG_ERROR(msg) std::cerr << ANSI_COLOR_RED    << "[ERROR] " << ANSI_COLOR_RESET << msg << "\n"
#define LOG_FATAL(msg) \
    do { \
        std::cerr << ANSI_COLOR_RED << "[FATAL] " << msg \
                  << " (" << __FILE__ << ":" << __LINE__ << ")" \
                  << ANSI_COLOR_RESET << "\n"; \
        std::exit(EXIT_FAILURE); \
    } while(0)

#include <charconv>
#include <filesystem>

namespace utils {
template<typename T>
static void FastIntToString(T value, std::string& out) {
    char buf[32];
    auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
    if (ec == std::errc()) {
        out.append(buf, ptr - buf);
    }
}

inline bool PathExistOrCreate(const std::filesystem::path& path, std::error_code& ec) {
    if (!std::filesystem::exists(path)) {
        if (!std::filesystem::create_directories(path, ec)) {
            return false;
        }
    }
    return true;
}
} // namespace utils

#endif // !UKVENGINE_UTILS_H_
