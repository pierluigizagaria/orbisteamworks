#pragma once
#include <map>
#include <optional>
#include <string>
#include <string_view>

namespace osw::core {
struct Part {
    std::string type, data;
};
std::optional<std::map<std::string, Part>> multipart(std::string_view type, std::string_view body);
} // namespace osw::core
