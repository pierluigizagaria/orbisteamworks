#include "core/multipart.hpp"
#include <algorithm>
#include <charconv>

namespace osw::core {
namespace {
std::string trim(std::string_view text) {
    const auto a = text.find_first_not_of(" \t");
    return a == text.npos ? std::string{}
                          : std::string(text.substr(a, text.find_last_not_of(" \t") - a + 1));
}
std::string lower(std::string text) {
    for (char &c : text)
        if (c >= 'A' && c <= 'Z')
            c += 'a' - 'A';
    return text;
}
std::string parameter(std::string_view value, std::string_view wanted) {
    auto offset = value.find(';');
    while (offset != value.npos) {
        auto next = value.find(';', ++offset);
        auto item =
            value.substr(offset, next == value.npos ? value.size() - offset : next - offset);
        auto eq = item.find('=');
        if (eq != item.npos && lower(trim(item.substr(0, eq))) == wanted) {
            auto result = trim(item.substr(eq + 1));
            if (result.size() >= 2 && result.front() == '"' && result.back() == '"')
                result = result.substr(1, result.size() - 2);
            return result;
        }
        offset = next;
    }
    return {};
}
} // namespace
std::optional<std::map<std::string, Part>> multipart(std::string_view type, std::string_view body) {
    if (!type.starts_with("multipart/") || body.size() > 1024 * 1024)
        return {};
    const auto boundary = parameter(type, "boundary");
    if (boundary.empty() || boundary.size() > 70 ||
        boundary.find_first_of("\r\n\"\\") != boundary.npos)
        return {};
    const auto marker = "--" + boundary;
    std::map<std::string, Part> result;
    size_t position = 0;
    for (;;) {
        if (body.substr(position, marker.size()) != marker)
            return {};
        position += marker.size();
        if (body.substr(position, 2) == "--") {
            position += 2;
            if (body.substr(position) != "\r\n" && position != body.size())
                return {};
            return result;
        }
        if (body.substr(position, 2) != "\r\n" || result.size() >= 8)
            return {};
        position += 2;
        auto header_end = body.find("\r\n\r\n", position);
        if (header_end == body.npos || header_end - position > 8192)
            return {};
        std::map<std::string, std::string> headers;
        while (position < header_end) {
            auto end = body.find("\r\n", position);
            auto line = body.substr(position, end - position);
            auto colon = line.find(':');
            if (colon == line.npos ||
                !headers.emplace(lower(trim(line.substr(0, colon))), trim(line.substr(colon + 1)))
                     .second)
                return {};
            position = end + 2;
        }
        position = header_end + 4;
        auto name = parameter(headers["content-disposition"], "name");
        if (name.empty())
            name = headers["content-description"];
        if (name.empty() || name.size() > 64)
            return {};
        size_t length = 0;
        const auto it = headers.find("content-length");
        if (it != headers.end()) {
            const auto &text = it->second;
            auto [end, error] = std::from_chars(text.data(), text.data() + text.size(), length);
            if (error != std::errc{} || end != text.data() + text.size())
                return {};
        } else {
            auto end = body.find("\r\n" + marker, position);
            if (end == body.npos)
                return {};
            length = end - position;
        }
        if (length > body.size() - position)
            return {};
        // Toolkit callers can omit the closing delimiter from the submitted body length.
        const bool ends_at_length = it != headers.end() && position + length == body.size();
        if (!ends_at_length && body.substr(position + length, 2) != "\r\n")
            return {};
        if (!result
                 .emplace(name,
                          Part{headers["content-type"], std::string(body.substr(position, length))})
                 .second)
            return {};
        if (ends_at_length)
            return result;
        position += length + 2;
    }
}
} // namespace osw::core
