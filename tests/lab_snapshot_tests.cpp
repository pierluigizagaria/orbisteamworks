// Exercise the private Lab implementation with an isolated hub and no polling
// worker. Snapshots must preserve the scalar readers' visibility and failures.
#include "../src/lab/backend.cpp"
#include <iostream>

namespace {
using osw::lab::LabSocial;
using osw::core::LobbySnapshot;
using osw::core::Social;
using Event = osw::core::SocialEvent;
constexpr std::uint64_t owner_id = 720001, member_id = 720002, other_id = 720003;

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
std::string read(const std::filesystem::path &path) {
    const auto bytes = osw::lab::detail::read_hub_file(path);
    check(bytes.has_value(), "fixture hub exists");
    return *bytes;
}
std::map<std::string, std::string> values(const LobbySnapshot &snapshot) {
    std::map<std::string, std::string> out;
    for (const auto &[key, field] : snapshot.data) out[key] = field.get();
    return out;
}
LobbySnapshot snapshot(Social &social, std::uint64_t room,
                       const std::vector<std::string> &keys, bool membership = true) {
    auto result = social.lobby_snapshot(room, keys, membership);
    check(result.has_value(), "Lab backend supplies a copied snapshot");
    check(!social.Social::lobby_snapshot(room, keys, membership).has_value(),
          "default backend leaves scalar reads to the caller");
    std::map<std::string, std::string> scalar;
    for (const auto &key : keys) scalar[key] = social.lobby_data(room, key);
    check(values(*result) == scalar &&
          result->owner.get() == (membership ? social.owner(room) : 0) &&
          result->members.get() == (membership ? social.members(room) : std::vector<std::uint64_t>{}),
          "Lab snapshot matches the existing scalar readers");
    return *result;
}
template <class F> void throws(F read_field, const char *message) {
    bool failed = false;
    try { read_field(); } catch (const std::exception &) { failed = true; }
    check(failed, message);
}
std::size_t count(const std::vector<Event> &events, Event::Kind kind) {
    return std::count_if(events.begin(), events.end(),
                         [=](const auto &event) { return event.kind == kind; });
}
void join(LabSocial &social, std::uint64_t room) {
    const auto operation = social.join_lobby(room);
    bool entered = false;
    for (const auto &event : social.poll())
        if (event.kind == Event::entered && event.call == operation &&
            event.lobby == room && event.success) entered = true;
    check(entered, "fixture joins room");
}

void run(const std::filesystem::path &root) {
    osw::core::Config config;
    const auto utf8 = root.u8string();
    config.lab_root.assign(utf8.begin(), utf8.end());
    config.lab_account_id = owner_id;
    LabSocial owner(config);
    config.lab_account_id = member_id;
    LabSocial member(config);
    config.lab_account_id = other_id;
    LabSocial other(config);
    const auto operation = owner.create_room(4);
    std::uint64_t room = 0;
    for (const auto &event : owner.poll())
        if (event.kind == Event::created && event.call == operation && event.success)
            room = event.lobby;
    check(room != 0, "fixture room creation");
    check(owner.set_lobby_data(room, "field", "before") &&
          owner.set_lobby_data(room, "empty", "") &&
          owner.set_lobby_data(room, "unrequested", "private copy sentinel"),
          "fixture metadata publication");
    const std::vector<std::string> keys{"field", "empty", "missing", "field"};
    const std::map<std::string, std::string> expected{
        {"field", "before"}, {"empty", ""}, {"missing", ""}};
    const auto prejoin = snapshot(member, room, keys);
    check(values(prejoin) == expected && prejoin.owner.get() == 0 &&
          prejoin.members.get().empty(), "prejoin metadata is public but membership is hidden");
    join(member, room);
    join(other, room);
    const auto joined = snapshot(member, room, keys);
    check(values(joined) == expected && joined.owner.get() == owner_id &&
          joined.members.get() == std::vector<std::uint64_t>{owner_id, member_id, other_id},
          "joined snapshot includes requested keys and current membership");
    const auto metadata_only = snapshot(member, room, keys, false);
    check(values(metadata_only) == expected && metadata_only.owner.get() == 0 &&
          metadata_only.members.get().empty(), "membership is omitted when not requested");
    const auto membership_only = snapshot(member, room, {});
    check(membership_only.data.empty() && membership_only.owner.get() == owner_id &&
          membership_only.members.get() == joined.members.get(), "empty key selection copies no data");

    // Keep both in-memory and persisted events pending while snapshots are read.
    owner.poll();
    member.poll();
    other.poll();
    check(member.request_data(room), "fixture local data event");
    check(owner.set_lobby_data(room, "field", "after") &&
          owner.send_message(room, "pending fixture message"), "fixture queued change and message");
    const auto file = root / "hub.json";
    const auto bytes = read(file);
    const auto modified = std::filesystem::last_write_time(file);
#ifdef _WIN32
    IO_COUNTERS before{}, after{};
    check(GetProcessIoCounters(GetCurrentProcess(), &before), "fixture initial IO counters");
#endif
    const auto changed = snapshot(member, room, keys);
    snapshot(owner, room, keys);
    snapshot(other, room, keys, false);
#ifdef _WIN32
    check(GetProcessIoCounters(GetCurrentProcess(), &after), "fixture final IO counters");
    check(before.WriteOperationCount == after.WriteOperationCount &&
          before.WriteTransferCount == after.WriteTransferCount, "snapshots perform no writes");
#endif
    check(read(file) == bytes && std::filesystem::last_write_time(file) == modified &&
          !std::filesystem::exists(root / "hub.json.tmp"), "snapshots preserve persisted state");
    check(changed.data.at("field").get() == "after" && values(joined) == expected &&
          values(prejoin) == expected, "fresh metadata does not mutate earlier copied snapshots");

#ifdef _WIN32
    {
        struct Denial {
            HANDLE handle;
            ~Denial() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
        } denial{CreateFileW(file.c_str(), GENERIC_WRITE,
                            FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr)};
        check(denial.handle != INVALID_HANDLE_VALUE, "fixture current-read denial handle");
        bool failed = false;
        try { member.lobby_snapshot(room, keys, true); }
        catch (const osw::lab::FailureError &error) {
            failed = error.failure == osw::lab::Failure::read_busy;
        }
        check(failed, "current read failure propagates instead of reusing prior snapshot");
    }
    check(read(file) == bytes, "failed snapshot preserves queued state bytes");
    check(snapshot(member, room, keys).data.at("field").get() == "after",
          "snapshot recovers after read sharing is restored");
#endif
    const auto events = member.poll();
    check(count(events, Event::data) == 2 && count(events, Event::message) == 1,
          "snapshots preserve local and persisted events without adding events");
    for (const auto &event : events)
        if (event.kind == Event::message)
            check(event.lobby == room && event.user == owner_id &&
                  event.payload == "pending fixture message", "pending message retains its contents");
    check(count(member.poll(), Event::data) == 0, "snapshot does not replay consumed events");

    // Invalid unused fields must not defeat callers' earlier short-circuit checks.
    // Only this test's hub is changed, under the same lock as the real backend.
    osw::lab::Json valid;
    {
        osw::lab::Store store(root);
        valid = store.state;
        auto &entry = store.state["lobbies"][std::to_string(room)];
        entry["data"]["namespace"] = "different namespace";
        entry["data"]["malformed"] = 17;
        entry["owner"] = 17;
        entry["members"]["invalid member id"] = osw::lab::Json::object();
        store.save();
    }
    check(!member.Social::lobby_snapshot(room, {"malformed"}, true).has_value(),
          "default snapshot does not eagerly read malformed scalar fields");
    const auto acquired = member.lobby_snapshot(room, {"namespace", "malformed"}, true);
    check(acquired.has_value(), "Lab snapshot captures individual field failures");
    const auto malformed = *acquired;
    check(malformed.data.at("namespace").get() == "different namespace",
          "valid earlier field remains readable despite later malformed fields");
    throws([&] { malformed.data.at("malformed").get(); }, "malformed snapshot metadata throws on access");
    throws([&] { member.lobby_data(room, "malformed"); }, "scalar malformed metadata also throws");
    throws([&] { malformed.owner.get(); }, "malformed snapshot owner throws on access");
    throws([&] { member.owner(room); }, "scalar malformed owner also throws");
    throws([&] { malformed.members.get(); }, "malformed snapshot members throw on access");
    throws([&] { member.members(room); }, "scalar malformed members also throw");
    {
        osw::lab::Store store(root);
        store.state = valid;
        store.save();
    }
    throws([&] { malformed.owner.get(); }, "captured field failure remains owned after state repair");
    check(snapshot(member, room, keys).owner.get() == owner_id,
          "fresh snapshot sees repaired state rather than a cached failure");

    other.leave_lobby(room);
    check(snapshot(member, room, keys).members.get() ==
              std::vector<std::uint64_t>{owner_id, member_id} &&
          joined.members.get().size() == 3, "member departure is fresh and old membership is copied");
    member.leave_lobby(room);
    const auto left = snapshot(member, room, keys);
    check(left.data.at("field").get() == "after" && left.owner.get() == 0 &&
          left.members.get().empty(), "self departure immediately hides membership");
    owner.leave_lobby(room);
    for (auto *actor : {&owner, &member}) {
        const auto missing = snapshot(*actor, room, keys);
        check(values(missing) == std::map<std::string, std::string>{
                  {"field", ""}, {"empty", ""}, {"missing", ""}} &&
              missing.owner.get() == 0 && missing.members.get().empty(),
              "deleted room returns defaults without retaining earlier state");
    }
    const auto absent = snapshot(member, room + 1, {});
    check(absent.data.empty() && absent.owner.get() == 0 && absent.members.get().empty(),
          "never-existing room returns empty defaults");
    check(values(joined) == expected && joined.owner.get() == owner_id &&
          joined.members.get().size() == 3, "owned snapshot survives mutations and room deletion");
}
} // namespace

int main() {
    namespace fs = std::filesystem;
    const auto root = fs::current_path() / ("lab-snapshot-test-" +
        std::to_string(osw::lab::process_id()) + "-" +
        std::to_string(std::chrono::steady_clock::now().time_since_epoch().count()));
    try {
        check(fs::create_directory(root), "fixture creates a new isolated directory");
        run(root);
        fs::remove_all(root);
        std::cout << "PASS Lab snapshot visibility, ownership, freshness, failures and read-only events\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
