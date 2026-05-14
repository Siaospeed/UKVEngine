#include "resp_parser.h"

#include <charconv>

void RespParser::AppendData(const char* data, size_t len) {
    internal_buffer_.append(data, len);
}

std::optional<std::vector<std::string>> RespParser::NextCommand() {
    std::string_view sv = internal_buffer_;
    t_.clear();

    while (parse_index_ < sv.size()) {
        char c = sv[parse_index_];

        switch (parse_state_) {
            case ParseState::READ_ARRAY_PREFIX:
                if (c != '*') {
                    return ErrorDataHandler();
                }
                parse_state_ = ParseState::READ_ARRAY_LEN;
                break;

            case ParseState::READ_ARRAY_LEN:
                if (c == '\r') {
                    parse_state_ = ParseState::EXPECT_ARRAY_LF;
                    auto [ptr, error_code] = std::from_chars(
                        t_.data(), t_.data() + t_.size(), array_len_);

                    if (error_code != std::errc()) {
                        return ErrorDataHandler();
                    }

                    t_.clear();
                }
                else if (c >= '0' && c <= '9') {
                    t_.push_back(c);
                }
                else {
                    return ErrorDataHandler();
                }
                break;

            case ParseState::EXPECT_ARRAY_LF:
                if (c != '\n') {
                    return ErrorDataHandler();
                }
                parse_state_ = ParseState::READ_STR_PREFIX;
                break;

            case ParseState::READ_STR_PREFIX:
                if (c != '$') {
                    return ErrorDataHandler();
                }
                parse_state_ = ParseState::READ_STR_LEN;
                break;

            case ParseState::READ_STR_LEN:
                if (c == '\r') {
                    parse_state_ = ParseState::EXPECT_STR_LF;
                    auto [ptr, error_code] = std::from_chars(
                        t_.data(), t_.data() + t_.size(), expected_len_);

                    if (error_code != std::errc()) {
                        return ErrorDataHandler();
                    }

                    t_.clear();
                }
                else if (c >= '0' && c <= '9') {
                    t_.push_back(c);
                }
                else {
                    return ErrorDataHandler();
                }
                break;

            case ParseState::EXPECT_STR_LF:
                if (c != '\n') {
                    return ErrorDataHandler();
                }
                parse_state_ = ParseState::READ_STR_DATA;
                break;

            case ParseState::READ_STR_DATA:
                if (sv.size() - parse_index_ >= expected_len_) {
                    parse_state_ = ParseState::EXPECT_DATA_CR;
                    command_.push_back(internal_buffer_.substr(parse_index_,
                                                               expected_len_));
                    parse_index_ += expected_len_;
                }
                else {
                    return std::nullopt;
                }
                continue;

            case ParseState::EXPECT_DATA_CR:
                if (c != '\r') {
                    return ErrorDataHandler();
                }
                parse_state_ = ParseState::EXPECT_DATA_LF;
                break;

            case ParseState::EXPECT_DATA_LF:
                if (c != '\n') {
                    return ErrorDataHandler();
                }

                if (command_.size() == array_len_) {
                    internal_buffer_.erase(0, parse_index_ + 1);
                    parse_index_ = 0;
                    parse_state_ = ParseState::READ_ARRAY_PREFIX;

                    auto result = std::move(command_);
                    command_.clear();
                    return result;
                }

                parse_state_ = ParseState::READ_STR_PREFIX;
                break;
        }

        parse_index_++;
    }

    return std::nullopt;
}

std::nullopt_t RespParser::ErrorDataHandler() {
    internal_buffer_.clear();
    parse_index_ = 0;
    parse_state_ = ParseState::READ_ARRAY_PREFIX;
    command_.clear();
    t_.clear();

    return std::nullopt;
}
