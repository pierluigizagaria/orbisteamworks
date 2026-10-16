// Exercise the actual private Lab implementation without a worker polling and
// writing concurrently. No test hook or additional public API is required.
#include "../src/lab/backend.cpp"
#include <iostream>

namespace {
using osw::lab::LabSocial;
using Event = osw::core::SocialEvent;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
    auto bytes = osw::lab::detail::read_hub_file(path);
    check(bytes.has_value(), "fixture hub exists");
    return *bytes;
}
IO_COUNTERS io() {
    IO_COUNTERS value{};
    check(GetProcessIoCounters(GetCurrentProcess(), &value), "fixture IO counters");
    return value;
}
std::size_t data_events(LabSocial &social) {
    const auto events = social.poll();
    return std::count_if(events.begin(), events.end(),
                         [](const auto &event) { return event.kind == Event::data; });
}
void run(const std::filesystem::path &root) {
    osw::core::Config config;
    const auto utf8 = root.u8string();
    config.lab_root.assign(utf8.begin(), utf8.end());
    config.lab_account_id = 710001;
    LabSocial owner(config);
    config.lab_account_id = 710002;
    LabSocial member(config);
    const auto operation = owner.create_room(4);
    std::uint64_t room = 0;
    for (const auto &event : owner.poll())
        if (event.kind == Event::created && event.call == operation && event.success)
            room = event.lobby;
    check(room != 0, "fixture room creation");
    member.join_lobby(room);
    owner.poll();
    member.poll();
    const auto file = root / "hub.json";

    auto set = [&](LabSocial &actor, std::uint64_t id, const std::string &key,
                   const std::string &value, bool accepted, bool writes) {
        const auto bytes = read(file);
        const auto before = io();
        const bool result = actor.set_lobby_data(id, key, value);
        const auto after = io();
        check(result == accepted, "metadata authorization result");
        if (writes) {
            check(after.WriteOperationCount > before.WriteOperationCount &&
                  after.WriteTransferCount > before.WriteTransferCount,
                  "changed metadata reaches storage");
            check(read(file) != bytes, "changed metadata updates current state");
        } else {
            check(after.WriteOperationCount == before.WriteOperationCount &&
                  after.WriteTransferCount == before.WriteTransferCount,
                  "unchanged or denied metadata performs no writes");
            check(read(file) == bytes, "unchanged metadata preserves all queued state bytes");
        }
    };
    set(owner, room, "field", "value", true, true);
    // The no-op must neither remove an earlier event nor add another one.
    set(owner, room, "field", "value", true, false);
    set(member, room, "field", "value", false, false);
    check(data_events(member) == 1 && data_events(owner) == 1,
          "one real change remains queued across no-op and denied publication");
    set(owner, room, "field", "value", true, false);
    check(data_events(member) == 0 && data_events(owner) == 0,
          "no-op publishes no new data event");
    set(owner, room, "empty", "", true, true);
    set(owner, room, "empty", "", true, false);
    check(data_events(member) == 1 && data_events(owner) == 1,
          "missing key differs from existing empty string");
    set(owner, room, "field", "changed", true, true);
    check(member.lobby_data(room, "field") == "changed" &&
          data_events(member) == 1 && data_events(owner) == 1,
          "a later real update remains visible without a cache");

    const auto bytes = read(file);
    HANDLE denial = CreateFileW(file.c_str(), GENERIC_WRITE,
                               FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                               OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
    check(denial != INVALID_HANDLE_VALUE, "fixture read denial handle");
    bool failed_closed = false;
    try {
        owner.set_lobby_data(room, "field", "changed");
    } catch (const osw::lab::FailureError &error) {
        failed_closed = error.failure == osw::lab::Failure::read_busy;
    } catch (...) {
        CloseHandle(denial);
        throw;
    }
    CloseHandle(denial);
    check(failed_closed && read(file) == bytes,
          "identical metadata still requires a successful current-state read");
    check(data_events(member) == 0 && data_events(owner) == 0,
          "failed current-state read publishes nothing");
    owner.leave_lobby(room);
    set(owner, room, "field", "changed", false, false);
    set(member, room, "field", "changed", false, false);
    check(data_events(member) == 0, "deleted room cannot be republished from old values");
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::current_path() / ("lab-metadata-test-" +
        std::to_string(GetCurrentProcessId()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        fs::create_directory(root);
        run(root);
        fs::remove_all(root);
        std::cout << "PASS Lab metadata authorization, events, state and no-op IO\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
