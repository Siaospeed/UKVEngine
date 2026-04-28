#ifndef UKVENGINE_RESP_BUILDER_H_
#define UKVENGINE_RESP_BUILDER_H_

#include <charconv>
#include <string>

class RespBuilder {
public:
    static void SimpleString(const std::string& str, std::string& out) {
        out.append("+").append(str).append("\r\n");
    }

    static void Error(const std::string& err, std::string& out) {
        out.append("-ERR ").append(err).append("\r\n");
    }

    static void Integer(int64_t num, std::string& out) {
        out.append(":");
        FastIntToString(num, out);
        out.append("\r\n");
    }

    static void NullBulkString(std::string& out) {
        out.append("$-1\r\n");
    }

    static void BulkString(const std::string& str, std::string& out) {
        out.append("$");
        FastIntToString(str.size(), out);
        out.append("\r\n").append(str).append("\r\n");
    }

    static void BulkStringArray(const std::vector<std::string>& args, std::string& out) {
        out.append("*");
        FastIntToString(args.size(), out);
        out.append("\r\n");

        for (const auto& arg : args) {
            out.append("$");
            FastIntToString(arg.size(), out);
            out.append("\r\n").append(arg).append("\r\n");
        }
    }

private:
    template<typename T>
    static void FastIntToString(T value, std::string& out) {
        char buf[32];
        auto [ptr, ec] = std::to_chars(buf, buf + sizeof(buf), value);
        if (ec == std::errc()) {
            out.append(buf, ptr - buf);
        }
    }
};

#endif // !UKVENGINE_RESP_BUILDER_H_
