#pragma once
#include "core/error.hpp"
#include <cstdint>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace osw::core {
bool valid_online_id(std::string_view id) noexcept;
std::optional<std::uint64_t> parse_decimal_id(std::string_view text) noexcept;
std::uint64_t decimal_id(std::string_view text);
struct Token {
    std::uint64_t user;
    std::vector<std::uint8_t> bytes;
    std::string online_id;
};
std::string hex(const std::vector<std::uint8_t> &bytes);
std::optional<std::string> try_encode_token(const Token &token);
std::string encode_token(const Token &token);
Token parse_token(std::string_view token);
} // namespace osw::core
