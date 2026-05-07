#ifndef UKVENGINE_RESP_BUILDER_H_
#define UKVENGINE_RESP_BUILDER_H_

#include <string>

#include "utils.h"

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
        utils::FastIntToString(num, out);
        out.append("\r\n");
    }

    static void NullBulkString(std::string& out) {
        out.append("$-1\r\n");
    }

    static void BulkString(const std::string& str, std::string& out) {
        out.append("$");
        utils::FastIntToString(str.size(), out);
        out.append("\r\n").append(str).append("\r\n");
    }

    static void BulkStringArray(const std::vector<std::string>& args, std::string& out) {
        out.append("*");
        utils::FastIntToString(args.size(), out);
        out.append("\r\n");

        for (const auto& arg : args) {
            out.append("$");
            utils::FastIntToString(arg.size(), out);
            out.append("\r\n").append(arg).append("\r\n");
        }
    }

private:

};

#endif // !UKVENGINE_RESP_BUILDER_H_
