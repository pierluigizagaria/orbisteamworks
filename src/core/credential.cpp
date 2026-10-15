#include "core/credential.hpp"
#include <array>
#include <charconv>

namespace osw::core {
namespace {
constexpr std::string_view alphabet =
    "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789-_";
size_t utf8_length(std::string_view text, size_t i) noexcept {
    const auto first = static_cast<unsigned char>(text[i]);
    const size_t length = first < 0x80                    ? 1
                          : first >= 0xc2 && first <= 0xdf ? 2
                          : first >= 0xe0 && first <= 0xef ? 3
                          : first >= 0xf0 && first <= 0xf4 ? 4
                                                           : 0;
    if (!length || length > text.size() - i)
        return 0;
    std::uint32_t codepoint = first & (length == 1 ? 0x7f : length == 2 ? 0x1f : length == 3 ? 0x0f : 0x07);
    for (size_t j = 1; j < length; ++j) {
        const auto next = static_cast<unsigned char>(text[i + j]);
        if ((next & 0xc0) != 0x80)
            return 0;
        codepoint = (codepoint << 6) | (next & 0x3f);
    }
    if ((length == 2 && codepoint < 0x80) || (length == 3 && codepoint < 0x800) ||
        (length == 4 && codepoint < 0x10000) || (codepoint >= 0xd800 && codepoint <= 0xdfff) ||
        codepoint > 0x10ffff || codepoint < 0x20 || codepoint == 0x7f)
        return 0;
    return length;
}
const char *online_id_error(std::string_view id) noexcept {
    if (id.empty() || id.size() > 16)
        return "Online ID needs 1-16 bytes";
    if (id.front() == ' ' || id.back() == ' ')
        return "Online ID has surrounding spaces";
    for (size_t i = 0; i < id.size();) {
        const auto length = utf8_length(id, i);
        if (!length)
            return "Online ID contains invalid UTF-8 or control characters";
        i += length;
    }
    return nullptr;
}
void check_online_id(std::string_view id) {
    if (const auto error = online_id_error(id))
        throw Error(ErrorCode::token, error);
}
std::string base64url(std::string_view bytes) {
    std::string out;
    std::uint32_t bits = 0;
    int count = 0;
    for (unsigned char c : bytes) {
        bits = (bits << 8) | c;
        count += 8;
        while (count >= 6) {
            count -= 6;
            out += alphabet[(bits >> count) & 63];
        }
    }
    if (count)
        out += alphabet[(bits << (6 - count)) & 63];
    return out;
}
std::string decode64(std::string_view text) {
    if (text.size() > 22)
        throw Error(ErrorCode::token, "Online ID encoding too long");
    std::string out;
    std::uint32_t bits = 0;
    int count = 0;
    for (char c : text) {
        auto value = alphabet.find(c);
        if (value == std::string_view::npos)
            throw Error(ErrorCode::token, "Invalid base64url online ID");
        bits = (bits << 6) | static_cast<std::uint32_t>(value);
        count += 6;
        if (count >= 8) {
            count -= 8;
            out += static_cast<char>((bits >> count) & 255);
        }
    }
    if (base64url(out) != text)
        throw Error(ErrorCode::token, "Noncanonical base64url online ID");
    check_online_id(out);
    return out;
}
int nibble(char c) {
    if (c >= '0' && c <= '9')
        return c - '0';
    if (c >= 'a' && c <= 'f')
        return c - 'a' + 10;
    // Accept equivalent uppercase input; replay mitigation must hash bytes.
    if (c >= 'A' && c <= 'F')
        return c - 'A' + 10;
    throw Error(ErrorCode::token, "Ticket is not hex encoded");
}
} // namespace

bool valid_online_id(std::string_view id) noexcept { return !online_id_error(id); }
std::string clean_text(std::string_view text, size_t limit) {
    std::string result;
    for (size_t i = 0; i < text.size();) {
        const auto length = utf8_length(text, i);
        if (!length) {
            ++i;
            continue;
        }
        if (result.empty() && text[i] == ' ') {
            ++i;
            continue;
        }
        if (result.size() + length > limit)
            break;
        result.append(text.substr(i, length));
        i += length;
    }
    while (!result.empty() && result.back() == ' ')
        result.pop_back();
    return result;
}
std::string online_id_from_persona(std::string_view persona, std::uint64_t account) {
    auto name = clean_text(persona, 16);
    return name.empty() ? "Player" + std::to_string(account % 100000) : name;
}
std::optional<std::uint64_t> parse_decimal_id(std::string_view text) noexcept {
    if (text.empty() || text.front() == '0')
        return {};
    std::uint64_t id = 0;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), id);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || !id)
        return {};
    return id;
}
std::uint64_t decimal_id(std::string_view text) {
    const auto id = parse_decimal_id(text);
    if (!id)
        throw Error(ErrorCode::token, "SteamID must be a nonzero canonical uint64 decimal");
    return *id;
}
std::string hex(const std::vector<std::uint8_t> &bytes) {
    constexpr char digits[] = "0123456789abcdef";
    std::string out;
    out.reserve(bytes.size() * 2);
    for (auto byte : bytes) {
        out += digits[byte >> 4];
        out += digits[byte & 15];
    }
    return out;
}
std::optional<std::string> try_encode_token(const Token &token) {
    if (!valid_online_id(token.online_id) || !token.user || token.bytes.empty())
        return {};
    const auto user = std::to_string(token.user), name = base64url(token.online_id);
    const auto overhead = 7 + user.size() + name.size();
    if (token.bytes.size() > (4096 - overhead) / 2)
        return {};
    return "osw1." + user + "." + hex(token.bytes) + "." + name;
}
std::string encode_token(const Token &token) {
    check_online_id(token.online_id);
    if (!token.user || token.bytes.empty())
        throw Error(ErrorCode::token, "Empty ticket or SteamID");
    auto result = try_encode_token(token);
    if (!result)
        throw Error(ErrorCode::token, "Token exceeds the 4096-character NpAuth limit");
    return std::move(*result);
}
Token parse_token(std::string_view token) {
    if (token.empty() || token.size() > 4096)
        throw Error(ErrorCode::token, "Token length outside [1,4096]");
    std::array<std::string_view, 4> parts{};
    for (int i = 0; i < 3; ++i) {
        auto pos = token.find('.');
        if (pos == std::string_view::npos)
            throw Error(ErrorCode::token, "Token requires four fields");
        parts[i] = token.substr(0, pos);
        token.remove_prefix(pos + 1);
    }
    parts[3] = token;
    if (parts[0] != "osw1" || parts[3].find('.') != std::string_view::npos)
        throw Error(ErrorCode::token, "Token version or field count invalid");
    Token result{decimal_id(parts[1]), {}, decode64(parts[3])};
    if (parts[2].empty() || parts[2].size() % 2)
        throw Error(ErrorCode::token, "Empty or odd-length ticket");
    for (std::size_t i = 0; i < parts[2].size(); i += 2)
        result.bytes.push_back(
            static_cast<std::uint8_t>((nibble(parts[2][i]) << 4) | nibble(parts[2][i + 1])));
    return result;
}
} // namespace osw::core
