#ifndef UKVENGINE_RESP_PARSER_H_
#define UKVENGINE_RESP_PARSER_H_

#include <optional>
#include <string>
#include <string_view>
#include <vector>

class RespParser {
public:
    RespParser() = default;

    void AppendData(const char* data, size_t len);
    std::optional<std::vector<std::string>> NextCommand();

private:
    enum class ParseState {
        READ_ARRAY_PREFIX,
        READ_ARRAY_LEN,
        EXPECT_ARRAY_LF,

        READ_STR_PREFIX,
        READ_STR_LEN,
        EXPECT_STR_LF,

        READ_STR_DATA,
        EXPECT_DATA_CR,
        EXPECT_DATA_LF
    };

    std::string internal_buffer_;
    size_t parse_index_ = 0;
    ParseState parse_state_ = ParseState::READ_ARRAY_PREFIX;

    std::vector<std::string> command_;
    size_t array_len_ = 0;
    size_t expected_len_ = 0;
    std::string t_;

    std::nullopt_t ErrorDataHandler();
};

#endif // !UKVENGINE_RESP_PARSER_H_
