#include "lab/file_read.hpp"
#include "orbisteamworks/osw.h"
#include <cstring>
#include <iostream>
#include <iterator>
using Clock = std::chrono::steady_clock;
void check(bool condition, const char *message) {
    if (!condition) throw std::runtime_error(message);
}
template<class F> void wait(F predicate) {
    const auto deadline = Clock::now() + std::chrono::seconds(3);
    while (!predicate()) {
        check(Clock::now() < deadline, "Lab state fixture deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
}
void write(const std::filesystem::path &path, const std::string &value) {
    std::ofstream file(path, std::ios::binary | std::ios::trunc);
    file << value;
    file.close();
    check(static_cast<bool>(file), "fixture complete write");
}
struct HubLock {
    HANDLE handle;
    OVERLAPPED overlapped{};
    explicit HubLock(const std::filesystem::path &path)
        : handle(CreateFileW(path.c_str(), GENERIC_READ | GENERIC_WRITE,
                             FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr,
                             OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)) {
        check(handle != INVALID_HANDLE_VALUE, "fixture hub lock open");
        try {
            wait([&] { return LockFileEx(handle, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY,
                                         0, 1, 0, &overlapped) != 0; });
        } catch (...) { CloseHandle(handle); throw; }
    }
    ~HubLock() { UnlockFileEx(handle, 0, 1, 0, &overlapped); CloseHandle(handle); }
};
struct Context {
    osw_context *value = nullptr;
    explicit Context(const std::filesystem::path &root) {
        osw_config_v2 config{};
        config.base.size = sizeof(config); config.base.abi_version = OSW_ABI_VERSION_2;
        std::strcpy(config.base.title, "fixture"); std::strcpy(config.base.build, "1");
        std::strcpy(config.base.realm, "read-failure"); std::strcpy(config.base.online_id, "Fixture");
        config.backend = 1; config.lab_account_id = 600001; config.game_port = 12345;
        std::strcpy(config.game_bind, "127.0.0.128");
        const auto text = root.string();
        check(text.size() < sizeof(config.lab_root), "fixture path length");
        std::strcpy(config.lab_root, text.c_str());
        check(osw_create_v2(&config, &value) == OSW_OK && osw_start(value) == OSW_OK,
              "Lab state fixture start");
    }
    ~Context() { osw_destroy(value); }
    osw_identity identity() {
        osw_identity out{sizeof(out)};
        check(osw_get_identity(value, &out) == OSW_OK, "fixture identity read");
        return out;
    }
    void failed(const char *reason) {
        wait([&] { return identity().reason_code != 0; });
        const auto out = identity();
        check(!out.signed_in && out.reason_code == OSW_UNAVAILABLE &&
                  std::strcmp(out.reason, reason) == 0, "fixed failure classification without private data");
        check(osw_stop(value) == OSW_OK, "join failed worker");
    }
};
int main() {
    namespace fs = std::filesystem;
    const auto root = fs::current_path() / ("lab-state-test-" + std::to_string(GetCurrentProcessId()) +
        "-" + std::to_string(Clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(root);
        const auto blocked = root / "blocked";
        fs::create_directory(blocked);
        const auto file = blocked / "hub.json";
        const auto bytes = "{\"next\":1000000,\"users\":{\"700001\":{\"pid\":" +
            std::to_string(GetCurrentProcessId()) +
            ",\"name\":\"Retained\",\"presence\":{},\"port\":12345,\"events\":[]}},\"lobbies\":{}}";
        write(file, bytes);
        // The handle performs no writes; requested access makes read denial
        // effective against a reader that permits all sharing modes.
        HANDLE denial = CreateFileW(file.c_str(), GENERIC_WRITE, FILE_SHARE_WRITE | FILE_SHARE_DELETE,
                                     nullptr, OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        check(denial != INVALID_HANDLE_VALUE, "deny fixture reads");
        try {
            Context context(blocked);
            context.failed("Lab hub read sharing timeout");
        } catch (...) { CloseHandle(denial); throw; }
        CloseHandle(denial);
        check(osw::lab::detail::read_hub_file(file) == bytes && !fs::exists(blocked / "hub.json.tmp"),
              "failed startup and teardown preserve every existing user byte, with no partial save");

        const auto directory = root / "directory";
        fs::create_directories(directory / "hub.json");
        {
            Context context(directory);
            context.failed("Lab hub read unavailable");
        }
        check(fs::is_directory(directory / "hub.json") && !fs::exists(directory / "hub.json.tmp"),
              "permanent read failure never creates a replacement state");

        const auto active = root / "active";
        {
            Context context(active);
            wait([&] { return context.identity().signed_in; });
            // Only this newly created fixture's hub is removed. Missing active
            // state must not silently create an empty replacement during stop.
            {
                HubLock lock(active / "hub.lock");
                check(fs::remove(active / "hub.json"), "remove private fixture hub");
            }
            context.failed("Lab active hub missing");
        }
        check(!fs::exists(active / "hub.json") && !fs::exists(active / "hub.json.tmp"),
              "active missing hub remains missing after teardown");
        fs::remove_all(root);
        std::cout << "Lab read failure preserves existing users, reports safe reasons and refuses active reset\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
