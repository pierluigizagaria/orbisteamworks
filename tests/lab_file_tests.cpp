#include "lab/file_replace.hpp"
#include <fstream>
#include <iostream>
#include <iterator>
#include <memory>
#include <string>

namespace {
void check(bool condition, const char *message) {
    if (!condition)
        throw std::runtime_error(message);
}
void write(const std::filesystem::path &path, const char *value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << value;
    check(static_cast<bool>(file), "fixture write");
}
std::string read(const std::filesystem::path &path) {
    std::ifstream file(path, std::ios::binary);
    return {std::istreambuf_iterator<char>(file), std::istreambuf_iterator<char>()};
}
struct Reader {
    HANDLE handle;
    explicit Reader(const std::filesystem::path &path)
        : handle(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE,
                             nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        check(handle != INVALID_HANDLE_VALUE, "fixture reader");
    }
    ~Reader() { CloseHandle(handle); }
};
} // namespace

int main() {
    namespace fs = std::filesystem;
    using Clock = std::chrono::steady_clock;
    const auto root = fs::current_path() /
        ("lab-file-test-" + std::to_string(GetCurrentProcessId()) + "-" +
         std::to_string(Clock::now().time_since_epoch().count()));
    const auto target = root / "hub.json", temporary = root / "hub.json.tmp";
    const auto cleanup = [&] {
        std::error_code ignored;
        fs::remove(temporary, ignored);
        fs::remove(target, ignored);
        fs::remove(root, ignored);
    };
    try {
        fs::create_directory(root);
        write(target, "old");
        write(temporary, "new");
        auto reader = std::make_unique<Reader>(target);
        check(!MoveFileExW(temporary.c_str(), target.c_str(),
                          MOVEFILE_REPLACE_EXISTING | MOVEFILE_WRITE_THROUGH),
              "non-delete-sharing reader must reproduce replacement conflict");
        check(osw::lab::detail::sharing_conflict(GetLastError(), target),
              "real replacement conflict is classified as sharing contention");
        const auto began = Clock::now();
        std::jthread release([owned = std::move(reader)]() mutable {
            std::this_thread::sleep_for(std::chrono::milliseconds(150));
            owned.reset();
        });
        osw::lab::detail::replace_hub_file(temporary, target);
        check(Clock::now() - began >= std::chrono::milliseconds(100) &&
                  Clock::now() - began < std::chrono::seconds(2),
              "transient contention recovers within the bounded wait");
        check(read(target) == "new" && !fs::exists(temporary),
              "successful replacement publishes the complete new file");
        release.join();

        write(temporary, "later");
        {
            Reader persistent(target);
            const auto blocked = Clock::now();
            bool failed = false;
            try {
                osw::lab::detail::replace_hub_file(temporary, target);
            } catch (const std::runtime_error &) {
                failed = true;
            }
            const auto elapsed = Clock::now() - blocked;
            check(failed && elapsed >= std::chrono::milliseconds(450) &&
                      elapsed < std::chrono::seconds(2),
                  "persistent sharing contention fails before the hub lock deadline");
            check(read(target) == "new" && read(temporary) == "later",
                  "failed replacement preserves both complete files");
        }

        // Read-only attributes are permanent permission state, not a transient
        // reader. They must fail immediately without consuming the retry budget.
        check(SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_READONLY), "set fixture readonly");
        const auto denied = Clock::now();
        bool failed = false;
        try {
            osw::lab::detail::replace_hub_file(temporary, target);
        } catch (const std::runtime_error &) {
            failed = true;
        }
        SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
        check(failed && Clock::now() - denied < std::chrono::milliseconds(400),
              "permanent access denial is not retried");
        check(read(target) == "new" && read(temporary) == "later",
              "permanent failure leaves the current hub unchanged");
        cleanup();
        std::cout << "Windows transient sharing recovery and permanent failure preservation passed\n";
        return 0;
    } catch (const std::exception &error) {
        SetFileAttributesW(target.c_str(), FILE_ATTRIBUTE_NORMAL);
        cleanup();
        std::cerr << error.what() << '\n';
        return 1;
    }
}
