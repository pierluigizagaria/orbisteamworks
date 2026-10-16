#include "core/local_io.hpp"
#include <algorithm>
#include <array>
#include <atomic>
#include <condition_variable>
#include <deque>
#include <filesystem>
#include <mutex>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <winsock2.h>
#include <ws2tcpip.h>
#include <windows.h>
#include <bcrypt.h>
#else
#include "core/child_wait.hpp"
#include <arpa/inet.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <pthread.h>
#include <sys/random.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>
extern char **environ;
#endif

namespace osw::core {
namespace {
std::int64_t tick_ms() {
    return std::chrono::duration_cast<std::chrono::milliseconds>(
               std::chrono::steady_clock::now().time_since_epoch())
        .count();
}
constexpr char alphabet[] = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
#ifdef _WIN32
using Socket = SOCKET;
constexpr Socket invalid_socket = INVALID_SOCKET;
void close_socket(Socket s) { closesocket(s); }
struct ScopedHandle {
    HANDLE value = nullptr;
    ~ScopedHandle() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
    }
    operator HANDLE() const { return value; }
};
std::wstring wide(const std::string &s) {
    if (s.empty())
        return {};
    int n = MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), nullptr, 0);
    if (!n)
        throw std::runtime_error("invalid UTF8 path");
    std::wstring out(n, 0);
    MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, s.data(), (int)s.size(), out.data(), n);
    return out;
}
#else
using Socket = int;
constexpr Socket invalid_socket = -1;
void close_socket(Socket s) { close(s); }
#endif
sockaddr_in endpoint(const std::string &address, std::uint16_t port) {
    sockaddr_in out{};
    out.sin_family = AF_INET;
    out.sin_port = htons(port);
    if (inet_pton(AF_INET, address.c_str(), &out.sin_addr) != 1)
        throw std::runtime_error("invalid address");
    return out;
}
} // namespace
bool loopback_address(const std::string &address) {
    in_addr a{};
    return inet_pton(AF_INET, address.c_str(), &a) == 1 && (ntohl(a.s_addr) >> 24) == 127;
}
std::vector<std::uint8_t> random_secret() {
    std::vector<std::uint8_t> out(32);
#ifdef _WIN32
    if (BCryptGenRandom(nullptr, out.data(), (ULONG)out.size(), BCRYPT_USE_SYSTEM_PREFERRED_RNG) !=
        0)
        throw std::runtime_error("random source failed");
#else
    if (getrandom(out.data(), out.size(), 0) != (ssize_t)out.size())
        throw std::runtime_error("random source failed");
#endif
    return out;
}
std::string base64(const std::string &bytes) {
    std::string out;
    for (std::size_t i = 0; i < bytes.size(); i += 3) {
        unsigned v = (unsigned char)bytes[i] << 16;
        if (i + 1 < bytes.size())
            v |= (unsigned char)bytes[i + 1] << 8;
        if (i + 2 < bytes.size())
            v |= (unsigned char)bytes[i + 2];
        out += alphabet[(v >> 18) & 63];
        out += alphabet[(v >> 12) & 63];
        out += i + 1 < bytes.size() ? alphabet[(v >> 6) & 63] : '=';
        out += i + 2 < bytes.size() ? alphabet[v & 63] : '=';
    }
    return out;
}
bool unbase64(const std::string &text, std::string &bytes, std::size_t limit) {
    bytes.clear();
    if (text.size() % 4 || text.size() > ((limit + 2) / 3) * 4)
        return false;
    for (std::size_t i = 0; i < text.size(); i += 4) {
        unsigned v = 0;
        int pad = 0;
        for (int k = 0; k < 4; ++k) {
            auto c = text[i + k];
            if (c == '=') {
                if (i + 4 != text.size() || k < 2)
                    return false;
                ++pad;
                v <<= 6;
            } else {
                auto at = std::find(std::begin(alphabet), std::end(alphabet) - 1, c);
                if (pad || at == std::end(alphabet) - 1)
                    return false;
                v = (v << 6) | (unsigned)(at - alphabet);
            }
        }
        bytes += (char)(v >> 16);
        if (pad < 2)
            bytes += (char)(v >> 8);
        if (pad < 1)
            bytes += (char)v;
    }
    return bytes.size() <= limit && base64(bytes) == text;
}
struct UdpSocket::Impl {
    Socket socket = invalid_socket;
    std::uint16_t port = 0;
#ifdef _WIN32
    bool winsock = false;
#endif
    ~Impl() {
        if (socket != invalid_socket)
            close_socket(socket);
#ifdef _WIN32
        if (winsock)
            WSACleanup();
#endif
    }
};
UdpSocket::UdpSocket(const std::string &address, std::uint16_t port) : p(std::make_unique<Impl>()) {
    if (!loopback_address(address))
        throw std::runtime_error("nonlocal proxy address");
#ifdef _WIN32
    WSADATA ws{};
    if (WSAStartup(MAKEWORD(2, 2), &ws))
        throw std::runtime_error("socket startup");
    p->winsock = true;
#endif
    p->socket = socket(AF_INET, SOCK_DGRAM, 0);
    if (p->socket == invalid_socket)
        throw std::runtime_error("socket allocation");
    auto e = endpoint(address, port);
    if (bind(p->socket, (sockaddr *)&e, sizeof(e)))
        throw std::runtime_error("proxy bind");
#ifdef _WIN32
    u_long enabled = 1;
    ioctlsocket(p->socket, FIONBIO, &enabled);
    int length = sizeof(e);
#else
    fcntl(p->socket, F_SETFL, O_NONBLOCK);
    fcntl(p->socket, F_SETFD, FD_CLOEXEC);
    socklen_t length = sizeof(e);
#endif
    getsockname(p->socket, (sockaddr *)&e, &length);
    p->port = ntohs(e.sin_port);
}
UdpSocket::~UdpSocket() = default;
std::uint16_t UdpSocket::port() const { return p->port; }
bool UdpSocket::receive(std::string &bytes, std::string &address, std::uint16_t &port) {
    std::array<char, 65536> buffer{};
    sockaddr_in from{};
#ifdef _WIN32
    int length = sizeof(from);
#else
    socklen_t length = sizeof(from);
#endif
    int n =
        (int)recvfrom(p->socket, buffer.data(), (int)buffer.size(), 0, (sockaddr *)&from, &length);
    if (n < 0)
        return false;
    char addr[16]{};
    inet_ntop(AF_INET, &from.sin_addr, addr, sizeof(addr));
    bytes.assign(buffer.data(), n);
    address = addr;
    port = ntohs(from.sin_port);
    return true;
}
bool UdpSocket::send(const std::string &bytes, const std::string &address, std::uint16_t port) {
    if (!loopback_address(address) || bytes.size() > 65507)
        return false;
    auto to = endpoint(address, port);
    return sendto(p->socket, bytes.data(), (int)bytes.size(), 0, (sockaddr *)&to, sizeof(to)) ==
           (int)bytes.size();
}

struct Child::Impl {
    std::atomic<bool> running{false};
    std::atomic<std::int64_t> write_started{0};
    std::thread writer;
    std::mutex mutex;
    std::condition_variable wake;
    std::deque<std::string> writes;
    std::size_t queued = 0;
    std::string partial;
#ifdef _WIN32
    HANDLE input = nullptr, output = nullptr, process = nullptr, job = nullptr;
#else
    int input = -1, output = -1;
    pid_t process = -1;
#endif
};
Child::Child() : p(std::make_unique<Impl>()) {}
Child::~Child() { stop(); }
void Child::start(const Config &config) {
    if (p->running || config.companion_path.empty())
        throw std::runtime_error("invalid companion");
    auto env = nlohmann::json::parse(
        config.companion_environment.empty() ? "{}" : config.companion_environment);
    if (!env.is_object() || env.size() > 64)
        throw std::runtime_error("invalid companion environment");
    for (auto &[key, value] : env.items())
        if (key.empty() || key.find_first_of("=\r\n") != std::string::npos ||
            key.find('\0') != std::string::npos || !value.is_string() ||
            value.get<std::string>().find('\0') != std::string::npos)
            throw std::runtime_error("invalid companion environment");
#ifdef _WIN32
    SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
    ScopedHandle child_in, child_out;
    if (!CreatePipe(&child_in.value, &p->input, &sa, 1024 * 1024) ||
        !CreatePipe(&p->output, &child_out.value, &sa, 1024 * 1024))
        throw std::runtime_error("companion pipe");
    if (!SetHandleInformation(p->input, HANDLE_FLAG_INHERIT, 0) ||
        !SetHandleInformation(p->output, HANDLE_FLAG_INHERIT, 0))
        throw std::runtime_error("companion pipe inheritance");
    auto insensitive = [](const std::wstring &a, const std::wstring &b) {
        return CompareStringOrdinal(a.data(), (int)a.size(), b.data(), (int)b.size(), TRUE) ==
               CSTR_LESS_THAN;
    };
    std::map<std::wstring, std::wstring, decltype(insensitive)> environment(insensitive);
    auto raw = GetEnvironmentStringsW();
    if (!raw)
        throw std::runtime_error("companion environment");
    for (auto s = raw; *s; s += wcslen(s) + 1) {
        std::wstring value = s;
        auto at = value.find(L'=', 1);
        if (at != std::wstring::npos)
            environment[value.substr(0, at)] = value.substr(at + 1);
    }
    FreeEnvironmentStringsW(raw);
    for (auto &[key, value] : env.items())
        environment[wide(key)] = wide(value.get<std::string>());
    environment[L"OSW_COMPANION_PROTOCOL"] = L"1";
    std::wstring block;
    for (auto &[key, value] : environment) {
        block += key + L"=" + value;
        block += L'\0';
    }
    block += L'\0';
    auto app = wide(config.companion_path), cwd = wide(config.companion_working_dir);
    std::wstring command = L"\"" + app + L"\"";
    STARTUPINFOEXW startup{};
    startup.StartupInfo.cb = sizeof(startup);
    startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES | STARTF_USESHOWWINDOW;
    startup.StartupInfo.wShowWindow = SW_HIDE;
    startup.StartupInfo.hStdInput = child_in;
    startup.StartupInfo.hStdOutput = child_out;
    auto error_path = env.value("OSW_COMPANION_STDERR", std::string{});
    ScopedHandle null_error;
    null_error.value = CreateFileW(error_path.empty() ? L"NUL" : wide(error_path).c_str(),
                                   GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE, &sa,
                                   error_path.empty() ? OPEN_EXISTING : CREATE_ALWAYS, 0, nullptr);
    if (null_error.value == INVALID_HANDLE_VALUE)
        throw std::runtime_error("companion diagnostics");
    startup.StartupInfo.hStdError = null_error;
    SIZE_T attrs = 0;
    InitializeProcThreadAttributeList(nullptr, 1, 0, &attrs);
    std::vector<unsigned char> storage(attrs);
    startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
    if (!InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &attrs))
        throw std::runtime_error("companion attributes");
    HANDLE inherited[] = {child_in, child_out, null_error};
    if (!UpdateProcThreadAttribute(startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST,
                                   inherited, sizeof(inherited), nullptr, nullptr)) {
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        throw std::runtime_error("companion handle allowlist");
    }
    p->job = CreateJobObjectW(nullptr, nullptr);
    JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
    limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
    bool job_ok = p->job && SetInformationJobObject(p->job, JobObjectExtendedLimitInformation,
                                                    &limits, sizeof(limits));
    PROCESS_INFORMATION process{};
    bool ok =
        job_ok && CreateProcessW(app.c_str(), command.data(), nullptr, nullptr, TRUE,
                                 CREATE_NO_WINDOW | CREATE_SUSPENDED | CREATE_UNICODE_ENVIRONMENT |
                                     EXTENDED_STARTUPINFO_PRESENT,
                                 block.data(), cwd.empty() ? nullptr : cwd.c_str(),
                                 &startup.StartupInfo, &process);
    DeleteProcThreadAttributeList(startup.lpAttributeList);
    if (!ok)
        throw std::runtime_error("companion launch");
    p->process = process.hProcess;
    if (!AssignProcessToJobObject(p->job, p->process)) {
        TerminateProcess(p->process, 1);
        CloseHandle(process.hThread);
        throw std::runtime_error("companion ownership");
    }
    auto resumed = ResumeThread(process.hThread);
    CloseHandle(process.hThread);
    if (resumed == (DWORD)-1)
        throw std::runtime_error("companion resume");
#else
    // The host must preserve waitable child ownership for this context's lifetime.
    // Refuse known auto-reaping policies without changing process-wide signal state.
    struct sigaction child_policy {};
    if (sigaction(SIGCHLD, nullptr, &child_policy) != 0 ||
        child_policy.sa_handler == SIG_IGN || (child_policy.sa_flags & SA_NOCLDWAIT) != 0)
        throw std::runtime_error("companion requires exclusive child reaping");
    int in[2], out[2];
    if (pipe2(in, O_CLOEXEC))
        throw std::runtime_error("companion pipe");
    if (pipe2(out, O_CLOEXEC)) {
        close(in[0]);
        close(in[1]);
        throw std::runtime_error("companion pipe");
    }
    std::map<std::string, std::string> environment;
    for (auto e = environ; *e; ++e) {
        std::string value = *e;
        auto at = value.find('=');
        if (at != std::string::npos)
            environment[value.substr(0, at)] = value.substr(at + 1);
    }
    for (auto &[key, value] : env.items())
        environment[key] = value.get<std::string>();
    environment["OSW_COMPANION_PROTOCOL"] = "1";
    std::vector<std::string> storage;
    for (auto &[key, value] : environment)
        storage.push_back(key + "=" + value);
    std::vector<char *> block;
    for (auto &value : storage)
        block.push_back(value.data());
    block.push_back(nullptr);
    posix_spawn_file_actions_t actions;
    posix_spawn_file_actions_init(&actions);
    posix_spawn_file_actions_adddup2(&actions, in[0], 0);
    posix_spawn_file_actions_adddup2(&actions, out[1], 1);
    if (!config.companion_working_dir.empty())
        posix_spawn_file_actions_addchdir_np(&actions, config.companion_working_dir.c_str());
    auto error_path = env.value("OSW_COMPANION_STDERR", std::string{});
    if (!error_path.empty())
        posix_spawn_file_actions_addopen(&actions, 2, error_path.c_str(),
                                         O_CREAT | O_WRONLY | O_TRUNC, 0600);
    char *arguments[] = {const_cast<char *>(config.companion_path.c_str()), nullptr};
    auto error = posix_spawn(&p->process, config.companion_path.c_str(), &actions, nullptr,
                             arguments, block.data());
    posix_spawn_file_actions_destroy(&actions);
    close(in[0]);
    close(out[1]);
    p->input = in[1];
    p->output = out[0];
    if (error) {
        p->process = -1;
        throw std::runtime_error("companion spawn");
    }
    fcntl(p->output, F_SETFL, O_NONBLOCK);
    fcntl(p->input, F_SETFL, O_NONBLOCK);
#endif
    p->running = true;
    p->writer = std::thread([this] {
#ifndef _WIN32
        sigset_t blocked;
        sigemptyset(&blocked);
        sigaddset(&blocked, SIGPIPE);
        pthread_sigmask(SIG_BLOCK, &blocked, nullptr);
#endif
        while (p->running) {
            std::string line;
            {
                std::unique_lock lock(p->mutex);
                p->wake.wait_for(lock, std::chrono::milliseconds(20),
                                 [&] { return !p->running || !p->writes.empty(); });
                if (!p->running)
                    break;
                if (p->writes.empty())
                    continue;
                line = std::move(p->writes.front());
                p->writes.pop_front();
                p->queued -= line.size();
            }
            std::size_t done = 0;
            p->write_started = tick_ms();
            auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(5);
            while (done < line.size() && p->running) {
#ifdef _WIN32
                DWORD n = 0;
                if (!WriteFile(p->input, line.data() + done, (DWORD)(line.size() - done), &n,
                               nullptr) ||
                    !n) {
                    p->running = false;
                    break;
                }
                done += n;
#else
                auto n = write(p->input, line.data() + done, line.size() - done);
                if (n > 0)
                    done += (std::size_t)n;
                else if (errno != EAGAIN && errno != EINTR) {
                    p->running = false;
                    break;
                } else
                    std::this_thread::sleep_for(std::chrono::milliseconds(1));
#endif
                if (std::chrono::steady_clock::now() > deadline)
                    p->running = false;
            }
            p->write_started = 0;
        }
    });
}
bool Child::send(std::string line) {
    if (line.size() > 98302 || !p->running)
        return false;
    line += '\n';
    std::lock_guard lock(p->mutex);
    if (p->queued + line.size() > 1024 * 1024 || p->writes.size() >= 128)
        return false;
    p->queued += line.size();
    p->writes.push_back(std::move(line));
    p->wake.notify_one();
    return true;
}
std::vector<std::string> Child::poll() {
    std::vector<std::string> lines;
    auto started = p->write_started.load();
    if (started && tick_ms() - started > 5000) {
        p->running = false;
#ifdef _WIN32
        if (p->writer.joinable())
            CancelSynchronousIo(p->writer.native_handle());
        // A writer may enter WriteFile just after cancellation; closing its reader bounds it too.
        if (p->job)
            TerminateJobObject(p->job, 1);
#endif
    }
    if (!alive()) {
        p->running = false;
        return lines;
    }
    for (int pass = 0; pass < 32; ++pass) {
        char bytes[8192];
        int n = 0;
#ifdef _WIN32
        DWORD available = 0, read = 0;
        if (!PeekNamedPipe(p->output, nullptr, 0, nullptr, &available, nullptr)) {
            p->running = false;
            break;
        }
        if (!available)
            break;
        if (!ReadFile(p->output, bytes, std::min<DWORD>(available, sizeof(bytes)), &read,
                      nullptr)) {
            p->running = false;
            break;
        }
        n = (int)read;
#else
        n = (int)read(p->output, bytes, sizeof(bytes));
        if (n < 0)
            break;
        if (!n) {
            p->running = false;
            break;
        }
#endif
        p->partial.append(bytes, n);
        for (;;) {
            auto end = p->partial.find('\n');
            if (end == std::string::npos)
                break;
            if (end > 98302) {
                p->running = false;
                return {};
            }
            lines.push_back(p->partial.substr(0, end));
            p->partial.erase(0, end + 1);
        }
        if (p->partial.size() > 98302) {
            p->running = false;
            return {};
        }
    }
    return lines;
}
bool Child::alive() const {
    if (!p->running)
        return false;
#ifdef _WIN32
    return WaitForSingleObject(p->process, 0) == WAIT_TIMEOUT;
#else
    return kill(p->process, 0) == 0;
#endif
}
void Child::stop() {
    p->running = false;
    p->wake.notify_all();
#ifdef _WIN32
    if (p->writer.joinable()) {
        CancelSynchronousIo(p->writer.native_handle());
        if (WaitForSingleObject(p->writer.native_handle(), 100) == WAIT_TIMEOUT) {
            if (p->job)
                TerminateJobObject(p->job, 1);
            else if (p->process)
                TerminateProcess(p->process, 1);
            CancelSynchronousIo(p->writer.native_handle());
        }
    }
#endif
    if (p->writer.joinable())
        p->writer.join();
#ifdef _WIN32
    if (p->input) {
        CloseHandle(p->input);
        p->input = nullptr;
    }
    if (p->process)
        WaitForSingleObject(p->process, 500);
    if (p->job) {
        CloseHandle(p->job);
        p->job = nullptr;
    }
    if (p->process) {
        CloseHandle(p->process);
        p->process = nullptr;
    }
    if (p->output) {
        CloseHandle(p->output);
        p->output = nullptr;
    }
#else
    if (p->input >= 0) {
        close(p->input);
        p->input = -1;
    }
    if (p->process > 0) {
        auto reap = [&](int options) {
            return wait_owned_child(p->process, [&](auto process) {
                auto result = waitpid(process, nullptr, options);
                return ChildWaitResult{result, result < 0 ? errno : 0};
            });
        };
        for (int i = 0; i < 50; ++i) {
            if (!reap(WNOHANG))
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
        }
        if (p->process > 0) {
            kill(p->process, SIGKILL);
            reap(0);
            p->process = -1;
        }
    }
    if (p->output >= 0) {
        close(p->output);
        p->output = -1;
    }
#endif
}
} // namespace osw::core
