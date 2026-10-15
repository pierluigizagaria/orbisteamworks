#pragma once
#include "core/credential.hpp"
#include "steam/runtime.hpp"
#include <chrono>
#include <functional>
#include <map>
#include <string_view>

namespace osw::spike {
using Clock = std::chrono::steady_clock;
using steam::Error;
using steam::ErrorCode;
struct Options {
    std::map<std::string, std::string> values;
    bool has(const std::string &key) const;
    std::string get(const std::string &key, const std::string &fallback = {}) const;
    int seconds(const std::string &key, int fallback) const;
};
Options options(int argc, char **argv, bool server);
using core::decimal_id;
using core::encode_token;
using core::hex;
using core::parse_token;
using core::Token;
Token read_token(const std::filesystem::path &path);
void write_private(const std::filesystem::path &path, std::string_view text);
void install_stop_handlers();
bool stopped(const Options &opts);
void tick();
void wait_until(steam::Runtime &runtime, const Options &opts, int seconds,
                const std::function<bool(const std::vector<steam::Event> &)> &ready,
                std::string_view operation);
void log_connection_events(const std::vector<steam::Event> &events, bool server);
void log_validation(const steam::Event &event, bool later = false);
int report_error(const std::exception &error);
} // namespace osw::spike
