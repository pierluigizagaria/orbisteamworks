#include "common.hpp"
#include <array>
#include <cstdio>
#include <map>
#include <optional>
#include <sstream>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <io.h>
#include <windows.h>
#else
#include <poll.h>
#include <unistd.h>
#endif

namespace {
using namespace osw;
using namespace osw::spike;
constexpr std::size_t max_line = 8192, max_pending = 64;

class Channel {
  public:
    Channel() {
#ifdef _WIN32
        if (GetFileType(GetStdHandle(STD_INPUT_HANDLE)) != FILE_TYPE_PIPE)
            throw Error(ErrorCode::arguments, "The helper requires a private input pipe");
        int fd = _dup(_fileno(stdout));
        if (fd < 0 || _dup2(_fileno(stderr), _fileno(stdout)) != 0)
            throw Error(ErrorCode::io, "Cannot reserve protocol output");
        output = _fdopen(fd, "w");
        if (!SetStdHandle(STD_OUTPUT_HANDLE, GetStdHandle(STD_ERROR_HANDLE)))
            throw Error(ErrorCode::io, "Cannot redirect provider diagnostics");
#else
        int fd = dup(fileno(stdout));
        if (fd < 0 || dup2(fileno(stderr), fileno(stdout)) < 0)
            throw Error(ErrorCode::io, "Cannot reserve protocol output");
        output = fdopen(fd, "w");
#endif
        if (!output)
            throw Error(ErrorCode::io, "Cannot open protocol output");
    }
    ~Channel() {
        if (output)
            std::fclose(output);
    }
    void send(const std::string &line) {
        auto value = "OSW1\t" + line + "\n";
        if (std::fwrite(value.data(), 1, value.size(), output) != value.size() ||
            std::fflush(output))
            throw Error(ErrorCode::io, "Protocol output closed");
    }
    std::optional<std::string> receive() {
        auto newline = buffer.find('\n');
        if (newline != std::string::npos) {
            if (newline >= max_line)
                throw Error(ErrorCode::arguments, "Protocol line exceeds limit");
            auto line = buffer.substr(0, newline);
            buffer.erase(0, newline + 1);
            if (!line.empty() && line.back() == '\r')
                line.pop_back();
            return line;
        }
        if (buffer.size() >= max_line)
            throw Error(ErrorCode::arguments, "Protocol line exceeds limit");
        std::array<char, 1024> bytes{};
        std::size_t count = 0;
#ifdef _WIN32
        DWORD available = 0, read = 0;
        auto handle = GetStdHandle(STD_INPUT_HANDLE);
        if (!PeekNamedPipe(handle, nullptr, 0, nullptr, &available, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                eof = true;
                return {};
            }
            throw Error(ErrorCode::io, "Cannot inspect input pipe");
        }
        if (!available)
            return {};
        if (!ReadFile(handle, bytes.data(), static_cast<DWORD>(bytes.size()), &read, nullptr)) {
            if (GetLastError() == ERROR_BROKEN_PIPE) {
                eof = true;
                return {};
            }
            throw Error(ErrorCode::io, "Cannot read input pipe");
        }
        count = read;
#else
        pollfd input{STDIN_FILENO, POLLIN, 0};
        int ready = poll(&input, 1, 0);
        if (ready < 0)
            throw Error(ErrorCode::io, "Cannot inspect input pipe");
        if (!ready)
            return {};
        auto read_count = read(STDIN_FILENO, bytes.data(), bytes.size());
        if (read_count < 0)
            throw Error(ErrorCode::io, "Cannot read input pipe");
        count = static_cast<std::size_t>(read_count);
#endif
        if (!count) {
            eof = true;
            return {};
        }
        buffer.append(bytes.data(), count);
        return receive();
    }
    bool ended() const { return eof; }

  private:
    FILE *output = nullptr;
    std::string buffer;
    bool eof = false;
};

struct Pending {
    std::uint64_t request;
    Clock::time_point deadline;
};
struct Active {
    std::uint64_t user;
};
std::vector<std::string> fields(const std::string &line) {
    std::vector<std::string> result;
    std::size_t start = 0;
    for (;;) {
        auto end = line.find('\t', start);
        result.push_back(line.substr(start, end == std::string::npos ? end : end - start));
        if (end == std::string::npos)
            return result;
        start = end + 1;
        if (result.size() > 4)
            throw Error(ErrorCode::arguments, "Invalid protocol fields");
    }
}
} // namespace

int main(int argc, char **argv) {
    try {
        auto opts = options(argc, argv, true);
        if (opts.has("--help")) {
            std::puts("osw-auth-helper [--library PATH] [--timeout SECONDS]\nPrivate OSW1 pipe "
                      "protocol; EOF stops the helper.");
            return 0;
        }
        auto timeout = opts.seconds("--timeout", 30);
        if (timeout < 1 || timeout > 60)
            throw Error(ErrorCode::arguments, "Timeout must be in [1,60]");
        install_stop_handlers();
        Channel channel;
        steam::Runtime runtime(opts.get("--library"));
        runtime.start_server();
        wait_until(
            runtime, opts, timeout,
            [&](const auto &events) {
                log_connection_events(events, true);
                return runtime.logged_on() && runtime.id() != 0;
            },
            "anonymous game-server logon");
        channel.send("READY\t" + std::to_string(runtime.id()));
        std::map<std::uint64_t, Pending> pending;
        std::map<std::uint64_t, Active> leases;
        std::uint64_t next_lease = 1, last_request = 0;
        auto fail = [&](std::uint64_t request, const char *reason) {
            channel.send("RESULT\t" + std::to_string(request) + "\tERROR\t" + reason);
        };
        while (!stopped(opts) && !channel.ended()) {
            for (int i = 0; i < 64; ++i) {
                auto input = channel.receive();
                if (!input)
                    break;
                auto parts = fields(*input);
                if (parts.size() != 4 || parts[0] != "OSW1")
                    throw Error(ErrorCode::arguments, "Invalid protocol request");
                auto request = decimal_id(parts[1]);
                if (request <= last_request)
                    throw Error(ErrorCode::arguments, "Request sequence must increase");
                last_request = request;
                if (parts[2] == "END") {
                    auto lease = decimal_id(parts[3]);
                    if (auto it = leases.find(lease); it != leases.end()) {
                        runtime.end_auth(it->second.user);
                        leases.erase(it);
                    }
                    channel.send("RESULT\t" + std::to_string(request) + "\tENDED");
                } else if (parts[2] == "VALIDATE") {
                    try {
                        auto token = parse_token(parts[3]);
                        if (pending.size() >= max_pending || !runtime.logged_on()) {
                            fail(request, "UNAVAILABLE");
                            continue;
                        }
                        bool busy = pending.contains(token.user);
                        for (const auto &[lease, active] : leases)
                            busy |= active.user == token.user;
                        if (busy) {
                            fail(request, "BUSY");
                            continue;
                        }
                        int result = runtime.begin_auth(token.bytes, token.user);
                        if (result != 0) {
                            fail(request, "REJECTED");
                            continue;
                        }
                        pending.emplace(
                            token.user,
                            Pending{request, Clock::now() + std::chrono::seconds(timeout)});
                    } catch (const Error &error) {
                        if (error.code != ErrorCode::token)
                            throw;
                        fail(request, "INVALID_TOKEN");
                    }
                } else
                    throw Error(ErrorCode::arguments, "Unknown protocol operation");
            }
            for (const auto &event : runtime.poll()) {
                if (event.kind != steam::Event::Kind::validation)
                    continue;
                auto it = pending.find(event.user);
                if (it == pending.end()) {
                    std::fprintf(stderr, "Later Steam validation result=%d; admission unchanged\n",
                                 event.result);
                    continue;
                }
                auto request = it->second.request;
                pending.erase(it);
                if (event.result != 0 || !event.owner) {
                    runtime.end_auth(event.user);
                    fail(request, "REJECTED");
                } else {
                    auto lease = next_lease++;
                    leases.emplace(lease, Active{event.user});
                    channel.send("RESULT\t" + std::to_string(request) + "\tOK\t" +
                                 std::to_string(event.user) + "\t" + std::to_string(event.owner) +
                                 "\t" + std::to_string(lease));
                }
            }
            for (auto it = pending.begin(); it != pending.end();) {
                if (Clock::now() < it->second.deadline) {
                    ++it;
                    continue;
                }
                runtime.end_auth(it->first);
                fail(it->second.request, "TIMEOUT");
                it = pending.erase(it);
            }
            tick();
        }
        return 0;
    } catch (const std::exception &error) {
        return osw::spike::report_error(error);
    }
}
