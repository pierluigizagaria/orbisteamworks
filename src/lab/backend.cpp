#include "lab/backend.hpp"
#include "lab/file_replace.hpp"
#include "lab/file_read.hpp"
#include "core/credential.hpp"
#include "core/local_io.hpp"
#include "core/p2p.hpp"
#include "core/platform.hpp"
#include <algorithm>
#include <chrono>
#include <filesystem>
#include <fstream>
#include <nlohmann/json.hpp>
#include <set>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <fcntl.h>
#include <signal.h>
#include <sys/file.h>
#include <unistd.h>
#endif

namespace osw::lab {
using Json = nlohmann::json;
namespace {
std::uint64_t process_id() {
#ifdef _WIN32
    return GetCurrentProcessId();
#else
    return getpid();
#endif
}
bool process_alive(std::uint64_t pid) {
#ifdef _WIN32
    HANDLE h = OpenProcess(SYNCHRONIZE, FALSE, (DWORD)pid);
    if (!h)
        return false;
    bool alive = WaitForSingleObject(h, 0) == WAIT_TIMEOUT;
    CloseHandle(h);
    return alive;
#else
    return kill((pid_t)pid, 0) == 0;
#endif
}
// The explicitly simulated hub is a locked local state file, not an Internet
// service. Reliable control events are queued there; race datagrams use UDP.
class Store {
    std::filesystem::path path;
#ifdef _WIN32
    HANDLE lock = nullptr;
    OVERLAPPED overlapped{};
#else
    int lock = -1;
#endif
  public:
    Json state;
    explicit Store(const std::filesystem::path &root, bool allow_create = false) : path(root / "hub.json") {
        std::filesystem::create_directories(root);
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(2);
#ifdef _WIN32
        lock = CreateFileW((root / "hub.lock").c_str(), GENERIC_READ | GENERIC_WRITE,
                           FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_ALWAYS,
                           FILE_ATTRIBUTE_NORMAL, nullptr);
        if (lock == INVALID_HANDLE_VALUE)
            throw FailureError(Failure::lock);
        while (!LockFileEx(lock, LOCKFILE_EXCLUSIVE_LOCK | LOCKFILE_FAIL_IMMEDIATELY, 0, 1, 0,
                           &overlapped)) {
            if (std::chrono::steady_clock::now() > deadline) {
                CloseHandle(lock);
                throw FailureError(Failure::busy);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#else
        lock = open((root / "hub.lock").c_str(), O_CREAT | O_RDWR, 0600);
        if (lock < 0)
            throw FailureError(Failure::lock);
        while (flock(lock, LOCK_EX | LOCK_NB)) {
            if (std::chrono::steady_clock::now() > deadline) {
                close(lock);
                throw FailureError(Failure::busy);
            }
            std::this_thread::sleep_for(std::chrono::milliseconds(1));
        }
#endif
        try {
            const auto bytes = detail::read_hub_file(path);
            if (bytes)
                state = Json::parse(*bytes, nullptr, false);
            else if (allow_create)
                state = {{"next", 1000000}, {"users", Json::object()}, {"lobbies", Json::object()}};
            else
                throw FailureError(Failure::missing);
            if (!state.is_object() || !state.contains("users") || !state["users"].is_object() ||
                !state.contains("lobbies") || !state["lobbies"].is_object())
                throw FailureError(Failure::invalid);
            std::vector<std::string> stale;
            for (auto &[id, user] : state["users"].items())
                if (!process_alive(user.value("pid", std::uint64_t{})))
                    stale.push_back(id);
            for (auto &id : stale)
                remove_user(id);
        } catch (...) {
#ifdef _WIN32
            UnlockFileEx(lock, 0, 1, 0, &overlapped);
            CloseHandle(lock);
            lock = nullptr;
#else
            flock(lock, LOCK_UN);
            close(lock);
            lock = -1;
#endif
            throw;
        }
    }
    void remove_user(const std::string &id) {
        state["users"].erase(id);
        for (auto it = state["lobbies"].begin(); it != state["lobbies"].end();) {
            it.value()["members"].erase(id);
            if (it.value()["members"].empty() || it.value().value("owner", std::string{}) == id)
                it = state["lobbies"].erase(it);
            else
                ++it;
        }
    }
    ~Store() {
#ifdef _WIN32
        if (lock && lock != INVALID_HANDLE_VALUE) {
            UnlockFileEx(lock, 0, 1, 0, &overlapped);
            CloseHandle(lock);
        }
#else
        if (lock >= 0) {
            flock(lock, LOCK_UN);
            close(lock);
        }
#endif
    }
    void save() {
        auto text = state.dump();
        if (text.size() > 8 * 1024 * 1024)
            throw FailureError(Failure::limit);
        auto temporary = path;
        temporary += ".tmp";
        {
            std::ofstream out(temporary, std::ios::binary | std::ios::trunc);
            out << text;
            out.flush();
            if (!out)
                throw FailureError(Failure::write);
            out.close();
            if (!out)
                throw FailureError(Failure::write);
        }
#ifdef _WIN32
        detail::replace_hub_file(temporary, path);
#else
        std::filesystem::rename(temporary, path);
#endif
    }
};
class LabSocial final : public core::Social {
    std::filesystem::path root;
    std::string self;
    std::uint64_t account;
    std::uint64_t call = 1;
    core::UdpSocket udp{"127.0.0.1"};
    std::set<std::uint64_t> allowed;
    std::vector<core::SocialEvent> local_events;
    std::vector<core::PeerMessage> peer_messages;
    std::string friend_snapshot;
    void queue(Json &state, const std::string &target, Json event) {
        if (!state["users"].contains(target))
            return;
        auto &events = state["users"][target]["events"];
        if (events.size() >= 256)
            throw FailureError(Failure::receiver_overflow);
        events.push_back(std::move(event));
    }
    void broadcast(Json &state, std::uint64_t lobby, core::SocialEvent::Kind kind,
                   std::string payload = {}) {
        auto key = std::to_string(lobby);
        if (!state["lobbies"].contains(key))
            return;
        for (auto &[id, data] : state["lobbies"][key]["members"].items())
            queue(state, id,
                  {{"kind", (int)kind},
                   {"lobby", key},
                   {"user", self},
                   {"payload", core::base64(payload)}});
    }
    Json *lobby(Store &store, std::uint64_t id) {
        auto key = std::to_string(id);
        auto &lobbies = store.state["lobbies"];
        return lobbies.contains(key) ? &lobbies[key] : nullptr;
    }
    std::uint64_t create(int limit, bool room) {
        Store store(root);
        std::uint64_t id = store.state["next"].get<std::uint64_t>();
        store.state["next"] = id + 1;
        store.state["lobbies"][std::to_string(id)] = {
            {"owner", self},          {"members", {{self, Json::object()}}},
            {"data", Json::object()}, {"limit", limit},
            {"joinable", true},       {"room", room}};
        store.save();
        auto operation = call++;
        local_events.push_back({core::SocialEvent::created, operation, id, account, true});
        return operation;
    }

  public:
    LabSocial(const core::Config &config)
        : root(std::u8string(config.lab_root.begin(), config.lab_root.end())),
          self(std::to_string(config.lab_account_id)), account(config.lab_account_id) {
        Store store(root, true);
        if (store.state["users"].contains(self))
            throw FailureError(Failure::identity_active);
        store.state["users"][self] = {
            {"pid", process_id()},
            {"name", config.online_id.empty() ? "LabPlayer" : config.online_id},
            {"presence", Json::object()},
            {"port", udp.port()},
            {"events", Json::array()}};
        store.save();
    }
    ~LabSocial() {
        try {
            Store store(root);
            store.remove_user(self);
            store.save();
        } catch (...) {
        }
    }
    std::vector<core::SocialEvent> poll() {
        Store store(root);
        if (!store.state["users"].contains(self))
            throw FailureError(Failure::identity_lost);
        auto &events = store.state["users"][self]["events"];
        for (auto &event : events) {
            auto user = core::parse_decimal_id(event.value("user", std::string{})).value_or(0);
            std::string payload;
            if (!core::unbase64(event.value("payload", std::string{}), payload, 65536))
                continue;
            if (event.value("peer", false))
                peer_messages.push_back({user, event.value("channel", 47), payload});
            else
                local_events.push_back(
                    {(core::SocialEvent::Kind)event.value("kind", 0), 0,
                     core::parse_decimal_id(event.value("lobby", std::string{})).value_or(0), user,
                     true, 0, payload});
        }
        events = Json::array();
        store.save();
        for (int n = 0; n < 256; ++n) {
            std::string bytes, address;
            std::uint16_t port = 0;
            if (!udp.receive(bytes, address, port))
                break;
            if (address != "127.0.0.1" || bytes.size() < 8)
                continue;
            std::uint64_t sender = 0;
            for (int i = 0; i < 8; ++i)
                sender |= (std::uint64_t)(unsigned char)bytes[i] << (8 * i);
            auto id = std::to_string(sender);
            if (!allowed.contains(sender) || !store.state["users"].contains(id) ||
                store.state["users"][id].value("port", 0) != port)
                continue;
            peer_messages.push_back({sender, 48, bytes.substr(8)});
        }
        Json friends = Json::object();
        for (auto &[id, user] : store.state["users"].items())
            if (id != self)
                friends[id] = {{"name", user["name"]}, {"presence", user["presence"]}};
        auto snapshot = friends.dump();
        if (snapshot != friend_snapshot) {
            friend_snapshot = std::move(snapshot);
            local_events.push_back({core::SocialEvent::friends_changed});
        }
        std::vector<core::SocialEvent> out;
        out.swap(local_events);
        return out;
    }
    std::vector<core::Friend> friends() override {
        Store store(root);
        std::vector<core::Friend> out;
        for (auto &[id, user] : store.state["users"].items())
            if (id != self)
                out.push_back({core::decimal_id(id), user.value("name", std::string{}), true});
        return out;
    }
    std::string presence(std::uint64_t id, const std::string &key) override {
        Store store(root);
        auto user = std::to_string(id);
        return store.state["users"].contains(user)
                   ? store.state["users"][user]["presence"].value(key, std::string{})
                   : std::string{};
    }
    bool set_presence(const std::string &key, const std::string &value) override {
        Store store(root);
        store.state["users"][self]["presence"][key] = value;
        store.save();
        return true;
    }
    std::uint64_t create_lobby(int limit) override { return create(limit, false); }
    std::uint64_t create_room(int limit) override { return create(limit, true); }
    std::uint64_t search_rooms(const std::map<std::string, std::string> &filters, int) override {
        Store store(root);
        core::SocialEvent event{core::SocialEvent::listed, call++, 0, 0, true};
        for (auto &[id, value] : store.state["lobbies"].items()) {
            if (!value.value("room", false) || !value.value("joinable", false) ||
                value["members"].size() >= (std::size_t)value.value("limit", 0))
                continue;
            bool match = true;
            for (auto &[key, expected] : filters)
                if (value["data"].value(key, std::string{}) != expected)
                    match = false;
            if (match && event.lobbies.size() < 32)
                event.lobbies.push_back(core::decimal_id(id));
        }
        local_events.push_back(event);
        return event.call;
    }
    std::uint64_t join_lobby(std::uint64_t id) override {
        Store store(root);
        auto entry = lobby(store, id);
        auto operation = call++;
        bool ok = entry && entry->value("joinable", false) &&
                  ((*entry)["members"].contains(self) ||
                   (*entry)["members"].size() < (std::size_t)entry->value("limit", 0));
        if (ok) {
            (*entry)["members"][self] = Json::object();
            broadcast(store.state, id, core::SocialEvent::member);
            store.save();
        }
        local_events.push_back({core::SocialEvent::entered, operation, id, account, ok});
        return operation;
    }
    void leave_lobby(std::uint64_t id) override {
        Store store(root);
        auto entry = lobby(store, id);
        if (!entry)
            return;
        (*entry)["members"].erase(self);
        broadcast(store.state, id, core::SocialEvent::member);
        if ((*entry)["members"].empty() || entry->value("owner", std::string{}) == self)
            store.state["lobbies"].erase(std::to_string(id));
        store.save();
    }
    bool invite(std::uint64_t id, std::uint64_t peer) override {
        Store store(root);
        auto entry = lobby(store, id);
        auto target = std::to_string(peer);
        if (!entry || !(*entry)["members"].contains(self) || !store.state["users"].contains(target))
            return false;
        queue(store.state, target,
              {{"kind", (int)core::SocialEvent::invitation},
               {"lobby", std::to_string(id)},
               {"user", self}});
        store.save();
        return true;
    }
    bool request_data(std::uint64_t id) override {
        Store store(root);
        bool ok = lobby(store, id) != nullptr;
        local_events.push_back({core::SocialEvent::data, 0, id, 0, ok});
        return ok;
    }
    std::string lobby_data(std::uint64_t id, const std::string &key) override {
        Store store(root);
        auto e = lobby(store, id);
        return e ? (*e)["data"].value(key, std::string{}) : std::string{};
    }
    bool set_lobby_data(std::uint64_t id, const std::string &key,
                        const std::string &value) override {
        Store store(root);
        auto e = lobby(store, id);
        if (!e || e->value("owner", std::string{}) != self)
            return false;
        (*e)["data"][key] = value;
        broadcast(store.state, id, core::SocialEvent::data);
        store.save();
        return true;
    }
    bool delete_lobby_data(std::uint64_t id, const std::string &key) override {
        Store store(root);
        auto e = lobby(store, id);
        if (!e || e->value("owner", std::string{}) != self)
            return false;
        (*e)["data"].erase(key);
        store.save();
        return true;
    }
    std::uint64_t owner(std::uint64_t id) override {
        Store store(root);
        auto e = lobby(store, id);
        return e && (*e)["members"].contains(self)
                   ? core::parse_decimal_id(e->value("owner", std::string{})).value_or(0)
                   : 0;
    }
    std::vector<std::uint64_t> members(std::uint64_t id) override {
        Store store(root);
        auto e = lobby(store, id);
        std::vector<std::uint64_t> out;
        if (e && (*e)["members"].contains(self))
            for (auto &[user, value] : (*e)["members"].items())
                out.push_back(core::decimal_id(user));
        return out;
    }
    std::string member_data(std::uint64_t id, std::uint64_t user, const std::string &key) override {
        Store store(root);
        auto e = lobby(store, id);
        auto account_key = std::to_string(user);
        return e && (*e)["members"].contains(account_key)
                   ? (*e)["members"][account_key].value(key, std::string{})
                   : std::string{};
    }
    void set_member_data(std::uint64_t id, const std::string &key,
                         const std::string &value) override {
        Store store(root);
        auto e = lobby(store, id);
        if (!e || !(*e)["members"].contains(self))
            return;
        (*e)["members"][self][key] = value;
        broadcast(store.state, id, core::SocialEvent::data);
        store.save();
    }
    bool set_joinable(std::uint64_t id, bool value) override {
        Store store(root);
        auto e = lobby(store, id);
        if (!e || e->value("owner", std::string{}) != self)
            return false;
        (*e)["joinable"] = value;
        store.save();
        return true;
    }
    bool set_limit(std::uint64_t id, int value) override {
        Store store(root);
        auto e = lobby(store, id);
        if (!e || e->value("owner", std::string{}) != self)
            return false;
        (*e)["limit"] = value;
        store.save();
        return true;
    }
    bool set_friends_visible(std::uint64_t id, bool) override {
        Store store(root);
        auto e = lobby(store, id);
        return e && e->value("owner", std::string{}) == self;
    }
    bool send_message(std::uint64_t id, const std::string &bytes) override {
        Store store(root);
        auto e = lobby(store, id);
        if (bytes.size() > 4096 || !e || !(*e)["members"].contains(self))
            return false;
        broadcast(store.state, id, core::SocialEvent::message, bytes);
        store.save();
        return true;
    }
    bool send_peer(std::uint64_t peer, int channel, const std::string &bytes) override {
        if (!allowed.contains(peer) || bytes.size() > 65507 - 8)
            return false;
        Store store(root);
        auto target = std::to_string(peer);
        if (!store.state["users"].contains(target))
            return false;
        if (channel == 47 || channel == 49) {
            queue(store.state, target,
                  {{"peer", true},
                   {"channel", channel},
                   {"user", self},
                   {"payload", core::base64(bytes)}});
            store.save();
            return true;
        }
        if (channel != 48)
            return false;
        std::string packet;
        for (int n = 0; n < 8; ++n)
            packet += (char)(account >> (8 * n));
        packet += bytes;
        return udp.send(packet, "127.0.0.1",
                        store.state["users"][target].value("port", (std::uint16_t)0));
    }
    std::vector<core::PeerMessage> take_peer_messages() override {
        std::vector<core::PeerMessage> out;
        out.swap(peer_messages);
        return out;
    }
    void allow_peers(const std::vector<std::uint64_t> &peers) override {
        allowed = {peers.begin(), peers.end()};
    }
};
class LabBackend final : public core::Backend {
    core::Identity value;
    std::unique_ptr<LabSocial> social;
    std::unique_ptr<core::Platform> platform;
    std::unique_ptr<core::P2P> p2p;

  public:
    void start(const core::Config &config) override {
        social = std::make_unique<LabSocial>(config);
        value = {true, config.lab_account_id,
                 config.online_id.empty() ? "LabPlayer" : config.online_id, "Simulated local lab",
                 0};
        platform = std::make_unique<core::Platform>(config, value, *social);
        if (!platform->start())
            throw FailureError(Failure::platform_start);
        p2p = std::make_unique<core::P2P>(config, value, *social);
    }
    core::Identity identity() const override { return value; }
    void submit(core::Request request) override {
        if (p2p->handles(request))
            p2p->submit(std::move(request));
        else
            platform->submit(std::move(request));
    }
    void cancel(std::uint64_t id) override {
        if (platform)
            platform->cancel(id);
        if (p2p)
            p2p->cancel(id);
    }
    core::Update poll() override {
        auto events = social->poll();
        platform->process(events);
        p2p->set_party(platform->party());
        p2p->process(events);
        auto out = platform->take_update();
        auto network = p2p->poll();
        for (auto &result : network.results)
            out.results.push_back(std::move(result));
        out.failed = network.failed;
        return out;
    }
    void stop() override {
        p2p.reset();
        if (platform)
            platform->stop();
        platform.reset();
        social.reset();
        value.signed_in = false;
    }
};
} // namespace
std::unique_ptr<core::Backend> make_backend() { return std::make_unique<LabBackend>(); }
} // namespace osw::lab
