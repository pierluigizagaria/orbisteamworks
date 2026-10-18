#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "core/local_io.hpp"
#include <chrono>
#include <cstdint>
#include <cstdlib>
#include <filesystem>
#include <iostream>
#include <limits>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>

namespace {
using Clock = std::chrono::steady_clock;
constexpr DWORD wait_ms = 5000;
constexpr DWORD exit_code = 43;
constexpr DWORD killed_code = 44;

void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}

struct Handle {
    HANDLE value = nullptr;
    Handle() = default;
    Handle(const Handle &) = delete;
    Handle &operator=(const Handle &) = delete;
    ~Handle() { reset(); }
    void reset() {
        if (value && value != INVALID_HANDLE_VALUE)
            CloseHandle(value);
        value = nullptr;
    }
};

std::filesystem::path executable() {
    std::wstring path(32768, L'\0');
    auto size = GetModuleFileNameW(nullptr, path.data(), (DWORD)path.size());
    check(size && size < path.size(), "fixture executable path");
    path.resize(size);
    return path;
}

int companion() {
    const auto text = std::getenv("OSW_CHILD_LIFETIME_CONTROLLER");
    check(text != nullptr, "fixture controller missing");
    auto pid = std::stoull(text);
    check(pid > 0 && pid <= std::numeric_limits<DWORD>::max(), "fixture controller invalid");
    Handle controller;
    controller.value = OpenProcess(PROCESS_DUP_HANDLE, FALSE, (DWORD)pid);
    check(controller.value != nullptr, "fixture controller handle");
    HANDLE copied = nullptr;
    check(DuplicateHandle(GetCurrentProcess(), GetCurrentProcess(), controller.value, &copied,
                          SYNCHRONIZE | PROCESS_QUERY_LIMITED_INFORMATION | PROCESS_TERMINATE,
                          FALSE, 0) != FALSE,
          "fixture child handle transfer");
    std::cout << (std::uintptr_t)copied << std::endl;
    // Deliberately never read stdin: EOF cannot make this child exit.
    Sleep(INFINITE);
    return 1;
}

int host(const char *controller) {
    osw::core::Config config;
    auto path = executable().u8string();
    config.companion_path.assign(path.begin(), path.end());
    config.companion_environment = nlohmann::json{
        {"OSW_CHILD_LIFETIME_ROLE", "companion"},
        {"OSW_CHILD_LIFETIME_CONTROLLER", controller}}.dump();
    osw::core::Child child;
    child.start(config);
    const auto deadline = Clock::now() + std::chrono::milliseconds(wait_ms);
    for (;;) {
        auto lines = child.poll();
        if (!lines.empty()) {
            check(lines.size() == 1, "fixture child readiness frame");
            std::cout << lines.front() << std::endl;
            break;
        }
        check(child.alive() && Clock::now() < deadline, "fixture child readiness deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    std::string command;
    check((bool)std::getline(std::cin, command) && command == "exit", "fixture exit command");
    // Skip Child's destructor, stop(), writer join and all normal host teardown.
    std::_Exit(exit_code);
}

struct Fixture {
    Handle job, input, output, parent, child;

    ~Fixture() {
        // This controller owns only its newly created fixture subtree. Its safety
        // job remains open during assertions; it cannot cause a passing result.
        if (parent.value && WaitForSingleObject(parent.value, 0) == WAIT_TIMEOUT)
            TerminateProcess(parent.value, 1);
        job.reset();
        if (parent.value)
            WaitForSingleObject(parent.value, wait_ms);
        if (child.value && WaitForSingleObject(child.value, 0) == WAIT_TIMEOUT) {
            TerminateProcess(child.value, 1);
            WaitForSingleObject(child.value, wait_ms);
        }
    }

    void start() {
        SECURITY_ATTRIBUTES sa{sizeof(sa), nullptr, TRUE};
        Handle child_in, child_out, error, thread;
        check(CreatePipe(&child_in.value, &input.value, &sa, 0) &&
                  CreatePipe(&output.value, &child_out.value, &sa, 0),
              "fixture pipes");
        check(SetHandleInformation(input.value, HANDLE_FLAG_INHERIT, 0) &&
                  SetHandleInformation(output.value, HANDLE_FLAG_INHERIT, 0),
              "fixture pipe inheritance");
        error.value = CreateFileW(L"NUL", GENERIC_WRITE, FILE_SHARE_READ | FILE_SHARE_WRITE,
                                  &sa, OPEN_EXISTING, 0, nullptr);
        check(error.value != INVALID_HANDLE_VALUE, "fixture error sink");
        job.value = CreateJobObjectW(nullptr, nullptr);
        JOBOBJECT_EXTENDED_LIMIT_INFORMATION limits{};
        limits.BasicLimitInformation.LimitFlags = JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE;
        check(job.value && SetInformationJobObject(job.value, JobObjectExtendedLimitInformation,
                                                   &limits, sizeof(limits)),
              "fixture safety job");
        STARTUPINFOEXW startup{};
        startup.StartupInfo.cb = sizeof(startup);
        startup.StartupInfo.dwFlags = STARTF_USESTDHANDLES;
        startup.StartupInfo.hStdInput = child_in.value;
        startup.StartupInfo.hStdOutput = child_out.value;
        startup.StartupInfo.hStdError = error.value;
        SIZE_T size = 0;
        InitializeProcThreadAttributeList(nullptr, 1, 0, &size);
        std::vector<unsigned char> storage(size);
        startup.lpAttributeList = (LPPROC_THREAD_ATTRIBUTE_LIST)storage.data();
        check(InitializeProcThreadAttributeList(startup.lpAttributeList, 1, 0, &size) != FALSE,
              "fixture process attributes");
        HANDLE inherited[]{child_in.value, child_out.value, error.value};
        const bool attributes = UpdateProcThreadAttribute(
            startup.lpAttributeList, 0, PROC_THREAD_ATTRIBUTE_HANDLE_LIST, inherited,
            sizeof(inherited), nullptr, nullptr) != FALSE;
        auto app = executable().wstring();
        auto command = L"\"" + app + L"\" --host " + std::to_wstring(GetCurrentProcessId());
        PROCESS_INFORMATION process{};
        const bool launched = attributes && CreateProcessW(
            app.c_str(), command.data(), nullptr, nullptr, TRUE,
            CREATE_NO_WINDOW | CREATE_SUSPENDED | EXTENDED_STARTUPINFO_PRESENT,
            nullptr, nullptr, &startup.StartupInfo, &process);
        DeleteProcThreadAttributeList(startup.lpAttributeList);
        check(launched, "fixture host launch");
        parent.value = process.hProcess;
        thread.value = process.hThread;
        check(AssignProcessToJobObject(job.value, parent.value) != FALSE,
              "fixture safety ownership");
        check(ResumeThread(thread.value) != (DWORD)-1, "fixture host resume");
    }

    void acquire_child() {
        const auto deadline = Clock::now() + std::chrono::milliseconds(wait_ms);
        std::string line;
        while (line.find('\n') == std::string::npos) {
            DWORD available = 0;
            check(PeekNamedPipe(output.value, nullptr, 0, nullptr, &available, nullptr) != FALSE,
                  "fixture readiness pipe");
            if (available) {
                char bytes[128];
                DWORD read = 0;
                check(ReadFile(output.value, bytes, (DWORD)sizeof(bytes), &read, nullptr) && read,
                      "fixture readiness read");
                line.append(bytes, read);
                check(line.size() < 128, "fixture readiness bound");
            } else {
                check(WaitForSingleObject(parent.value, 0) == WAIT_TIMEOUT &&
                          Clock::now() < deadline,
                      "fixture host readiness deadline");
                std::this_thread::sleep_for(std::chrono::milliseconds(5));
            }
        }
        const auto copied = std::stoull(line);
        check(copied && copied <= std::numeric_limits<std::uintptr_t>::max(),
              "fixture transferred handle range");
        child.value = (HANDLE)(std::uintptr_t)copied;
        check(WaitForSingleObject(child.value, 100) == WAIT_TIMEOUT,
              "fixture companion must remain alive before parent exit");
    }

    void abrupt_exit(bool terminate) {
        if (terminate) {
            check(TerminateProcess(parent.value, killed_code) != FALSE,
                  "fixture terminate owned parent");
        } else {
            DWORD written = 0;
            check(WriteFile(input.value, "exit\n", 5, &written, nullptr) && written == 5,
                  "fixture abrupt exit request");
        }
        check(WaitForSingleObject(parent.value, wait_ms) == WAIT_OBJECT_0,
              "fixture parent termination deadline");
        DWORD status = 0;
        check(GetExitCodeProcess(parent.value, &status) &&
                  status == (terminate ? killed_code : exit_code),
              "fixture parent exit code");
        check(WaitForSingleObject(child.value, wait_ms) == WAIT_OBJECT_0,
              "companion survived abrupt parent exit");
        check(GetExitCodeProcess(child.value, &status) && status != STILL_ACTIVE,
              "fixture companion exit confirmed");
    }
};
} // namespace

int main(int argc, char **argv) {
    try {
        const auto role = std::getenv("OSW_CHILD_LIFETIME_ROLE");
        if (role && std::string(role) == "companion")
            return companion();
        if (argc == 3 && std::string(argv[1]) == "--host")
            return host(argv[2]);
        check(argc == 1, "fixture arguments");
        for (const bool terminate : {false, true}) {
            Fixture fixture;
            fixture.start();
            fixture.acquire_child();
            fixture.abrupt_exit(terminate);
            std::cout << (terminate ? "PASS child exits after owned parent termination\n"
                                    : "PASS child exits after parent _Exit\n");
        }
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
