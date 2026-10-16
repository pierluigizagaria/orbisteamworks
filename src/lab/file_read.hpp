#pragma once
#include "lab/error.hpp"
#include <chrono>
#include <filesystem>
#include <fstream>
#include <optional>
#include <string>
#include <thread>
#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

namespace osw::lab::detail {
// nullopt means an absent file, never an unreadable existing file. Callers alone
// decide whether creation is allowed; active operations must reject absence.
inline std::optional<std::string> read_hub_file(const std::filesystem::path &path) {
    constexpr std::size_t limit = 8 * 1024 * 1024;
#ifdef _WIN32
    using Clock = std::chrono::steady_clock;
    const auto deadline = Clock::now() + std::chrono::milliseconds(500);
    for (;;) {
        struct Handle {
            HANDLE value;
            ~Handle() { if (value != INVALID_HANDLE_VALUE) CloseHandle(value); }
        } file{CreateFileW(path.c_str(), GENERIC_READ,
                           FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                           OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        if (file.value == INVALID_HANDLE_VALUE) {
            const auto error = GetLastError();
            if (error == ERROR_FILE_NOT_FOUND || error == ERROR_PATH_NOT_FOUND)
                return std::nullopt;
            if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
                throw FailureError(Failure::read_open);
            if (Clock::now() >= deadline)
                throw FailureError(Failure::read_busy);
            std::this_thread::sleep_for(std::chrono::milliseconds(10));
            continue;
        }
        LARGE_INTEGER size{};
        if (!GetFileSizeEx(file.value, &size) || size.QuadPart < 0)
            throw FailureError(Failure::read_io);
        if (size.QuadPart > static_cast<LONGLONG>(limit))
            throw FailureError(Failure::limit);
        std::string text(static_cast<std::size_t>(size.QuadPart), '\0');
        std::size_t offset = 0;
        bool retry = false;
        while (offset < text.size()) {
            DWORD count = 0;
            if (!ReadFile(file.value, text.data() + offset,
                          static_cast<DWORD>(text.size() - offset), &count, nullptr)) {
                const auto error = GetLastError();
                if (error != ERROR_SHARING_VIOLATION && error != ERROR_LOCK_VIOLATION)
                    throw FailureError(Failure::read_io);
                if (Clock::now() >= deadline)
                    throw FailureError(Failure::read_busy);
                retry = true;
                break;
            }
            if (!count) throw FailureError(Failure::read_io);
            offset += count;
        }
        if (!retry) return text;
        // This retries only a read, before any hub/event mutation. It never
        // retries a mutating request, resets state or changes a session lifetime.
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
#else
    std::ifstream file(path, std::ios::binary);
    if (!file) {
        std::error_code error;
        if (std::filesystem::exists(path, error) || error)
            throw FailureError(Failure::read_open);
        return std::nullopt;
    }
    file.seekg(0, std::ios::end);
    const auto size = file.tellg();
    if (size < 0) throw FailureError(Failure::read_io);
    if (size > static_cast<std::streamoff>(limit)) throw FailureError(Failure::limit);
    file.seekg(0);
    std::string text(static_cast<std::size_t>(size), '\0');
    file.read(text.data(), static_cast<std::streamsize>(text.size()));
    if (!file) throw FailureError(Failure::read_io);
    return text;
#endif
}
} // namespace osw::lab::detail
