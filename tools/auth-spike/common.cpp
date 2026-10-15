#include "common.hpp"
#include <array>
#include <atomic>
#include <charconv>
#include <csignal>
#include <fstream>
#include <iostream>
#include <set>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <sys/stat.h>
#endif

namespace osw::spike {
namespace {
static_assert(std::atomic<bool>::is_always_lock_free);
std::atomic<bool> stop_requested{false};
void stop_handler(int) { stop_requested.store(true, std::memory_order_relaxed); }
#ifdef _WIN32
BOOL WINAPI console_handler(DWORD event) {
    if (event == CTRL_C_EVENT || event == CTRL_BREAK_EVENT) {
        stop_requested.store(true, std::memory_order_relaxed);
        return TRUE;
    }
    return FALSE;
}
#endif
} // namespace

bool Options::has(const std::string &key) const { return values.contains(key); }
std::string Options::get(const std::string &key, const std::string &fallback) const {
    auto it = values.find(key);
    return it == values.end() ? fallback : it->second;
}
int Options::seconds(const std::string &key, int fallback) const {
    if (!has(key))
        return fallback;
    const auto &text = values.at(key);
    int value = -1;
    auto parsed = std::from_chars(text.data(), text.data() + text.size(), value);
    if (parsed.ec != std::errc{} || parsed.ptr != text.data() + text.size() || value < 0 ||
        value > 86400)
        throw Error(ErrorCode::arguments, key + " must be an integer in [0,86400]");
    return value;
}
Options options(int argc, char **argv, bool server) {
    std::set<std::string> names{"--library", "--token", "--timeout", "--stop-file"};
    if (server)
        names.insert(
            {"--repeat", "--interval", "--watch", "--ready-file", "--token-wait", "--wait-before"});
    else
        names.insert({"--online-id", "--cancel-after", "--bind-server"});
    Options opts;
    for (int i = 1; i < argc; ++i) {
        std::string key = argv[i];
        bool flag = key == "--help" || (server && key == "--end-between");
        if (!flag && !names.contains(key))
            throw Error(ErrorCode::arguments, "Unknown option (use --help)");
        if (opts.has(key))
            throw Error(ErrorCode::arguments, "Duplicate option: " + key);
        if (flag)
            opts.values[key] = "true";
        else {
            if (++i == argc)
                throw Error(ErrorCode::arguments, "Missing value for " + key);
            opts.values[key] = argv[i];
        }
    }
    return opts;
}
Token read_token(const std::filesystem::path &path) {
    std::ifstream input(path, std::ios::binary);
    if (!input)
        throw Error(ErrorCode::io, "Cannot open token file");
    std::array<char, 4099> buffer{};
    input.read(buffer.data(), buffer.size());
    auto size = static_cast<std::size_t>(input.gcount());
    if (input.bad() || size == buffer.size())
        throw Error(ErrorCode::token, "Token file too large or unreadable");
    std::string_view value(buffer.data(), size);
    if (!value.empty() && value.back() == '\n')
        value.remove_suffix(1);
    if (!value.empty() && value.back() == '\r')
        value.remove_suffix(1);
    return parse_token(value);
}
void write_private(const std::filesystem::path &path, std::string_view text) {
    auto parent = path.parent_path();
    if (!parent.empty())
        std::filesystem::create_directories(parent);
    auto temporary = path;
    temporary += ".tmp";
    {
        std::ofstream output(temporary, std::ios::binary | std::ios::trunc);
        if (!output)
            throw Error(ErrorCode::io, "Cannot create private output file");
#ifndef _WIN32
        if (chmod(temporary.c_str(), S_IRUSR | S_IWUSR) != 0)
            throw Error(ErrorCode::io, "Cannot restrict private file permissions");
#endif
        output.write(text.data(), static_cast<std::streamsize>(text.size()));
        output.close();
        if (!output)
            throw Error(ErrorCode::io, "Cannot write private output file");
    }
#ifdef _WIN32
    if (!MoveFileExW(temporary.c_str(), path.c_str(),
                     MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
        throw Error(ErrorCode::io, "Cannot publish private output file");
#else
    std::filesystem::rename(temporary, path);
#endif
}
void install_stop_handlers() {
    std::signal(SIGINT, stop_handler);
    std::signal(SIGTERM, stop_handler);
#ifdef _WIN32
    SetConsoleCtrlHandler(console_handler, TRUE);
#endif
    std::cout << std::unitbuf;
}
bool stopped(const Options &opts) {
    return stop_requested.load(std::memory_order_relaxed) ||
           (opts.has("--stop-file") && std::filesystem::exists(opts.get("--stop-file")));
}
void tick() { std::this_thread::sleep_for(std::chrono::milliseconds(20)); }
void wait_until(steam::Runtime &runtime, const Options &opts, int seconds,
                const std::function<bool(const std::vector<steam::Event> &)> &ready,
                std::string_view operation) {
    auto deadline = Clock::now() + std::chrono::seconds(seconds);
    do {
        if (stopped(opts))
            throw Error(ErrorCode::arguments, "Stopped during " + std::string(operation));
        if (ready(runtime.poll()))
            return;
        if (Clock::now() >= deadline)
            break;
        tick();
    } while (true);
    throw Error(ErrorCode::timeout, std::string(operation) + " timed out after " +
                                        std::to_string(seconds) + " seconds");
}
void log_connection_events(const std::vector<steam::Event> &events, bool server) {
    for (const auto &event : events) {
        if (event.kind == steam::Event::Kind::connected)
            std::cout << "Connected to Steam\n";
        if (event.kind == steam::Event::Kind::connect_failure) {
            std::cout << "Connection failure result=" << event.result
                      << " retrying=" << event.retrying << '\n';
            if (server)
                throw Error(ErrorCode::server_logon,
                            "Anonymous Steam logon failed; result=" + std::to_string(event.result));
        }
        if (event.kind == steam::Event::Kind::disconnected)
            std::cout << "Disconnected from Steam result=" << event.result << '\n';
    }
}
void log_validation(const steam::Event &event, bool later) {
    std::cout << (later ? "Later " : "") << "ValidateAuthTicketResponse result=" << event.result
              << " (" << steam::validation_result_name(event.result) << ") steam_id=" << event.user
              << " owner_id=" << event.owner << '\n';
}
int report_error(const std::exception &error) {
    auto typed = dynamic_cast<const Error *>(&error);
    auto code = typed ? typed->code : ErrorCode::io;
    // Filesystem exceptions can include filenames, but no file contents.
    std::cerr << "ERROR " << steam::error_name(code) << ": " << error.what() << '\n';
    return static_cast<int>(code);
}
} // namespace osw::spike
