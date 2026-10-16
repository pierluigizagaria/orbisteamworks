#pragma once
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <chrono>
#include <filesystem>
#include <stdexcept>
#include <thread>

namespace osw::lab::detail {
inline bool sharing_conflict(DWORD error, const std::filesystem::path &target) {
    if (error == ERROR_SHARING_VIOLATION || error == ERROR_LOCK_VIOLATION)
        return true;
    if (error != ERROR_ACCESS_DENIED)
        return false;
    // MoveFileEx reports ACCESS_DENIED for an existing reader that did not
    // share deletion. Check that specific condition without modifying the file;
    // permissions, read-only files and other permanent failures are not retried.
    HANDLE probe = CreateFileW(target.c_str(), DELETE,
                              FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                              OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (probe != INVALID_HANDLE_VALUE) {
        CloseHandle(probe);
        return false;
    }
    const auto cause = GetLastError();
    return cause == ERROR_SHARING_VIOLATION || cause == ERROR_LOCK_VIOLATION;
}

inline void replace_hub_file(const std::filesystem::path &temporary,
                             const std::filesystem::path &target) {
    using Clock = std::chrono::steady_clock;
    // Shorter than the hub's two-second lock acquisition deadline. This is
    // bounded I/O contention recovery, never a session lifetime or idle timer.
    const auto deadline = Clock::now() + std::chrono::milliseconds(500);
    for (;;) {
        if (MoveFileExW(temporary.c_str(), target.c_str(),
                        MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH))
            return;
        const auto error = GetLastError();
        if (!sharing_conflict(error, target) || Clock::now() >= deadline)
            throw std::runtime_error("lab hub replace");
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
} // namespace osw::lab::detail
#endif
