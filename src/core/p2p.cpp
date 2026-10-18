#include "core/p2p.hpp"
#include "core/endpoints.hpp"
#include "core/credential.hpp"
#include "core/local_io.hpp"
#include <algorithm>
#include <chrono>
#include <deque>
#include <map>
#include <nlohmann/json.hpp>
#include <set>

namespace osw::core {
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
namespace {
std::string idstr(std::uint64_t id) { return std::to_string(id); }
std::uint64_t idfield(const Json &j, const char *key) {
    if (!j.contains(key) || !j[key].is_string())
        return 0;
    return parse_decimal_id(j[key].get<std::string>()).value_or(0);
}
bool safe_metadata(const Json &j) {
    if (!j.is_object() || j.dump().size() > 256)
        return false;
    for (auto &[key, value] : j.items())
        if ((key != "playlist" && key != "type" && key != "free_slots") ||
            !(value.is_string() || value.is_number_unsigned() || value.is_number_integer()))
            return false;
    return true;
}
std::string scalar(const Json &j) { return j.is_string() ? j.get<std::string>() : j.dump(); }
} // namespace
struct P2P::Impl {
    Config config;
    Identity identity;
    Social &social;
    Child child;
    std::string token;
    bool ready = false, broken = false, owner_name_updates = false;
    std::uint64_t proxy_received = 0, network_sent = 0, network_received = 0, proxy_sent = 0,
                  proxy_rejected = 0;
    Clock::time_point start = Clock::now();
    struct Pending {
        Json command;
        std::uint64_t request = 0;
        Clock::time_point deadline;
        int distance = 1;
    };
    std::map<std::uint64_t, Pending> pending;
    std::map<std::uint64_t, Pending> validating, awaiting_party;
    std::map<std::uint64_t, std::uint64_t> completed_rooms;
    std::map<std::uint64_t, std::uint64_t> abandoned;
    Party party;
    std::uint64_t following = 0, following_pending = 0;
    std::uint64_t suppressed_selection = 0;
    Clock::time_point following_retry{};
    std::set<std::uint64_t> rooms;
    std::map<std::uint64_t, std::string> room_snapshots;
    struct Proxy {
        std::uint64_t room = 0;
        bool friend_scope = false;
        // An authenticated friend that read a live local advertisement may
        // initiate pre-party UDP without a reciprocal advertisement lookup.
        std::set<std::string> advertisement_grants;
        std::unique_ptr<UdpSocket> socket;
    };
    std::map<std::uint64_t, Proxy> proxies;
    // A revoked physical port cannot be reused for another peer while a host
    // syscall might still hold an earlier copied lookup result.
    std::vector<std::unique_ptr<UdpSocket>> retired_sockets;
    std::size_t allocated_sockets = 0;
    std::map<std::uint64_t, std::pair<Clock::time_point, unsigned>> rates;
    std::set<std::uint64_t> friends;
    Clock::time_point friend_refresh{};
    std::map<std::string, std::string> advertisements;
    struct AdPending {
        Pending pending;
        std::uint64_t peer;
        std::string context;
        Clock::time_point retry = Clock::now() + std::chrono::milliseconds(500);
    };
    std::map<std::string, AdPending> ad_pending;
    std::uint64_t next_ad = 1;
    std::deque<Json> events;
    Update update;
    Impl(const Config &c, const Identity &i, Social &s) : config(c), identity(i), social(s) {
        token = encode_token({identity.account_id, random_secret(), identity.online_id});
        if (config.canonical_endpoints()) {
            if (!config.endpoints) config.endpoints = std::make_shared<EndpointRoutes>();
            config.endpoints->enable();
            config.endpoints->initialize(virtual_endpoint(identity.account_id, config));
        }
        if (!config.companion_path.empty()) {
            child.start(config);
            Json owner{{"v", 1},
                                 {"event", "owner"},
                                 {"steam_id", idstr(i.account_id)},
                                 {"online_id", i.online_id},
                                 {"token", token},
                                 {"title", c.title},
                                 {"build", c.build},
                                 {"realm", c.realm},
                                 {"backend", c.backend ? "lab" : "steam"}};
            owner["app_id"] = c.app_id;
            if (config.canonical_endpoints()) {
                const auto endpoint = virtual_endpoint(identity.account_id, config);
                owner["transport"] = {{"mode", config.loopback_endpoints ? "loopback-v1" : "virtual-v1"},
                    {"address", endpoint_address(endpoint)}, {"port", endpoint.port},
                    {"game_address", config.game_bind}, {"game_port", config.game_port}};
            }
            if (!child.send(owner.dump()))
                throw std::runtime_error("companion handshake");
        } else {
            ready = true;
            if (config.canonical_endpoints()) config.endpoints->set_ready();
        }
    }
    ~Impl() {
        if (config.canonical_endpoints()) config.endpoints->close();
        try {
            for (auto room : rooms)
                social.leave_lobby(room);
            social.allow_peers({});
        } catch (...) {
            // Destruction must still close the companion and proxy sockets after a backend failure.
        }
    }
    std::map<std::string, std::string> filters(const Json &metadata) const {
        std::map<std::string, std::string> out{{"osw_room", "osw/1"},
                                               {"osw_title", config.title},
                                               {"osw_build", config.build},
                                               {"osw_realm", config.realm},
                                               {"osw_room_protocol", config.protocol()}};
        for (auto &[key, value] : metadata.items())
            if (key != "free_slots")
                out["osw_room_" + key] = scalar(value);
        return out;
    }
    std::optional<LobbySnapshot> room_state(std::uint64_t room, bool joined) {
        static const std::vector<std::string> keys{
            "osw_room", "osw_title", "osw_build", "osw_realm", "osw_room_protocol",
            "osw_room_owner", "osw_room_metadata"};
        return social.lobby_snapshot(room, keys, joined);
    }
    bool matches(std::uint64_t room, const std::optional<LobbySnapshot> &state, bool joined) {
        const auto data = [&](const std::string &key) {
            return state ? state->data.at(key).get() : social.lobby_data(room, key);
        };
        for (auto &[key, value] : filters(Json::object()))
            if (data(key) != value)
                return false;
        auto owner = parse_decimal_id(data("osw_room_owner")).value_or(0);
        const auto metadata = data("osw_room_metadata");
        if (!owner || metadata.size() > 256 || !safe_metadata(Json::parse(metadata, nullptr, false)))
            return false;
        // Steam exposes the actual owner and member list only to lobby members.
        return !joined ||
               (owner == (state ? state->owner.get() : social.owner(room)) &&
                (state ? state->members.get().size() : social.members(room).size()) <= 16);
    }
    bool matches(std::uint64_t room, bool joined = true) {
        return matches(room, room_state(room, joined), joined);
    }
    bool member(std::uint64_t room, std::uint64_t peer) {
        if (!room)
            return friends.contains(peer);
        if (!rooms.contains(room))
            return false;
        const auto state = room_state(room, true);
        if (!matches(room, state, true)) return false;
        const auto members = state ? state->members.get() : social.members(room);
        return std::find(members.begin(), members.end(), peer) != members.end();
    }
    bool friend_authorized(const Proxy &proxy) const {
        return proxy.friend_scope || !proxy.advertisement_grants.empty();
    }
    bool prepare_proxy(std::uint64_t peer, Proxy &proxy) {
        if (config.loopback_endpoints) {
            const auto canonical = virtual_endpoint(peer, config);
            // Check collisions before binding, but publish no route until the
            // exact physical socket exists. Host readers never see a failed bind.
            if (!config.endpoints->permits(peer, canonical)) return false;
            if (!proxy.socket) {
                if (allocated_sockets >= EndpointRoutes::limit) {
                    config.endpoints->erase(peer);
                    return false;
                }
                try {
                    proxy.socket = std::make_unique<UdpSocket>(endpoint_address(canonical), canonical.port, true);
                    ++allocated_sockets;
                } catch (...) {
                    config.endpoints->erase(peer);
                    return false; // Never substitute another address or ephemeral port.
                }
            }
            return config.endpoints->install(peer, canonical, canonical);
        }
        if (!proxy.socket) {
            if (config.canonical_endpoints() && allocated_sockets >= EndpointRoutes::limit)
                return false;
            proxy.socket = std::make_unique<UdpSocket>(config.game_bind);
            ++allocated_sockets;
        }
        if (!config.canonical_endpoints()) return true;
        osw_endpoint raw{sizeof(raw)};
        std::copy(config.game_bind.begin(), config.game_bind.end(), raw.address);
        raw.port = proxy.socket->port();
        Endpoint physical;
        return read_endpoint(raw, physical) && config.endpoints->install(
            peer, virtual_endpoint(peer, config), physical);
    }
    Json proxy_endpoint(std::uint64_t peer, const Proxy &proxy) const {
        if (config.canonical_endpoints()) {
            const auto value = virtual_endpoint(peer, config);
            return {{"address", endpoint_address(value)}, {"port", value.port}};
        }
        return {{"address", config.game_bind}, {"port", proxy.socket->port()}};
    }
    auto erase_proxy(std::map<std::uint64_t, Proxy>::iterator it) {
        if (config.canonical_endpoints()) {
            config.endpoints->erase(it->first);
            if (it->second.socket) {
                // Canonical loopback identities are fixed, so a fresh grant
                // rebinds the same identity. Closing discards queued stale UDP.
                if (config.loopback_endpoints) { it->second.socket.reset(); --allocated_sockets; }
                else retired_sockets.push_back(std::move(it->second.socket));
            }
        }
        return proxies.erase(it);
    }
    Json ad_frame(const std::string &op, const std::string &id, const std::string &context) {
        return {{"v", 1},
                {"op", op},
                {"id", id},
                {"context", context},
                {"protocol", config.ad_protocol()},
                {"title", config.title},
                {"build", config.build},
                {"realm", config.realm}};
    }
    Json record(std::uint64_t room, bool joined = true) {
        const auto state = social.lobby_snapshot(
            room, {"osw_room_metadata", "osw_room_owner"}, joined);
        Json members = Json::array();
        if (joined)
            for (auto peer : state ? state->members.get() : social.members(room)) {
                if (members.size() >= 16)
                    break;
                members.push_back(idstr(peer));
            }
        auto metadata = Json::parse(state ? state->data.at("osw_room_metadata").get()
                                          : social.lobby_data(room, "osw_room_metadata"),
                                    nullptr, false);
        if (!safe_metadata(metadata))
            metadata = Json::object();
        return {{"room_id", idstr(room)},
                {"owner",
                 joined ? idstr(state ? state->owner.get() : social.owner(room))
                        : state ? state->data.at("osw_room_owner").get()
                                : social.lobby_data(room, "osw_room_owner")},
                {"members", members},
                {"metadata", metadata}};
    }
    void reply(const Json &command, std::uint64_t request, bool ok, Json result = Json::object(),
               const char *error = "unavailable") {
        if (command.is_object() && command.value("_autojoin", false)) {
            following_pending = 0;
            following_retry = Clock::now() + std::chrono::milliseconds(500);
            if (ok) {
                following = idfield(result, "room_id");
                result["event"] = "room.selected";
                emit(result);
            }
            return;
        }
        if (request) {
            Result response{request, ok ? OSW_OK : OSW_UNAVAILABLE};
            response.metadata = (ok ? result : Json{{"error", error}}).dump();
            update.results.push_back(std::move(response));
        } else if (!child.send(Json{{"v", 1},
                                    {"id", command.is_object() ? command.value("id", std::string{})
                                                               : std::string{}},
                                    {"ok", ok},
                                    {ok ? "result" : "error", ok ? result : Json(error)}}
                                   .dump()))
            broken = true;
    }
    void emit(Json event) {
        event["v"] = 1;
        if (events.size() < 128)
            events.push_back(event);
        else {
            events.clear();
            events.push_back({{"v", 1}, {"event", "error"}, {"error", "event_overflow"}});
        }
        if (!config.companion_path.empty() && !child.send(event.dump()))
            broken = true;
    }
    bool set_metadata(std::uint64_t room, const Json &metadata) {
        for (auto &[key, value] : filters(metadata))
            if (!social.set_lobby_data(room, key, value))
                return false;
        return social.set_lobby_data(room, "osw_room_owner", idstr(identity.account_id)) &&
               social.set_lobby_data(room, "osw_room_metadata", metadata.dump());
    }
    void track_follow(std::uint64_t room) {
        if (party.lobby && party.owner != identity.account_id &&
            social.member_data(party.lobby, party.owner, "osw.room") == idstr(room) &&
            std::find(party.members.begin(), party.members.end(), party.owner) !=
                party.members.end()) {
            if (following && following != room)
                rollback(following);
            following = room;
        }
    }
    void joined(std::uint64_t room, Pending value) {
        rooms.insert(room);
        track_follow(room);
        if (value.request) {
            if (completed_rooms.size() >= 128)
                completed_rooms.erase(completed_rooms.begin());
            completed_rooms[value.request] = room;
        }
        if (party.lobby && party.owner == identity.account_id) {
            social.set_member_data(party.lobby, "osw.room", idstr(room));
            if (party.members.size() > 1) {
                awaiting_party[room] = std::move(value);
                return;
            }
        }
        reply(value.command, value.request, true, record(room));
    }
    void rollback(std::uint64_t room) {
        social.leave_lobby(room);
        rooms.erase(room);
        room_snapshots.erase(room);
        std::erase_if(completed_rooms, [&](const auto &entry) { return entry.second == room; });
        for (auto it = proxies.begin(); it != proxies.end();) {
            if (it->second.room != room) {
                ++it;
                continue;
            }
            if (friend_authorized(it->second) && friends.contains(it->first)) {
                it->second.room = 0;
                ++it;
            } else
                it = erase_proxy(it);
        }
        if (party.lobby && party.owner == identity.account_id &&
            social.member_data(party.lobby, identity.account_id, "osw.room") == idstr(room))
            social.set_member_data(party.lobby, "osw.room", "");
        emit({{"event", "room.closed"}, {"room_id", idstr(room)}});
    }
    void suppress_follow(std::uint64_t room) {
        if (!party.lobby || party.owner == identity.account_id ||
            social.member_data(party.lobby, party.owner, "osw.room") != idstr(room))
            return;
        suppressed_selection = room;
        if (following == room)
            following = 0;
        if (following_pending == room) {
            following_pending = 0;
            auto validating_room = validating.find(room);
            if (validating_room != validating.end() &&
                validating_room->second.command.value("_autojoin", false))
                validating.erase(validating_room);
            for (auto it = pending.begin(); it != pending.end();)
                if (idfield(it->second.command, "room_id") == room &&
                    it->second.command.value("_autojoin", false)) {
                    abandoned[it->first] = room;
                    it = pending.erase(it);
                } else
                    ++it;
        }
    }
    void execute(Json command, std::uint64_t request) {
        try {
            if (!command.is_object() || !command.contains("op") || !command["op"].is_string()) {
                reply(command, request, false, {}, "invalid_request");
                return;
            }
            auto op = command["op"].get<std::string>();
            if (op == "status") {
                reply(command, request, true,
                      {{"ready", ready},
                       {"owner_name_updates", config.companion_path.empty() || owner_name_updates},
                       {"backend", config.backend ? "lab" : "steam"},
                       {"proxy_received", proxy_received},
                       {"network_sent", network_sent},
                       {"network_received", network_received},
                       {"proxy_sent", proxy_sent},
                       {"proxy_rejected", proxy_rejected}});
                return;
            }
            if (op == "events.poll" && request) {
                Json out = Json::array();
                while (!events.empty() && out.size() < 32) {
                    out.push_back(std::move(events.front()));
                    events.pop_front();
                }
                reply(command, request, true, {{"events", out}});
                return;
            }
            if (op == "advertisement.set" || op == "advertisement.get") {
                if (op == "advertisement.get") {
                    friend_refresh = {};
                    refresh_rooms();
                }
                auto context = command.value("context", std::string{});
                if (context.empty() || context.size() > 32 ||
                    context.find_first_not_of("ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmn"
                                              "opqrstuvwxyz0123456789_.-") != std::string::npos) {
                    reply(command, request, false, {}, "invalid_context");
                    return;
                }
                if (op == "advertisement.set") {
                    std::string bytes;
                    auto payload = command.value("payload", std::string{});
                    if (!unbase64(payload, bytes, 4096) ||
                        (!advertisements.contains(context) && advertisements.size() >= 8)) {
                        reply(command, request, false, {}, "limit");
                        return;
                    }
                    if (payload.empty()) {
                        advertisements.erase(context);
                        for (auto &[peer, proxy] : proxies)
                            proxy.advertisement_grants.erase(context);
                        refresh_rooms();
                    } else
                        advertisements[context] = payload;
                    reply(command, request, true);
                    return;
                }
                auto peer = idfield(command, "peer");
                if (peer == identity.account_id) {
                    auto it = advertisements.find(context);
                    if (it != advertisements.end())
                        reply(command, request, true, {{"payload", it->second}});
                    else
                        reply(command, request, false, {}, "not_found");
                    return;
                }
                if (!friends.contains(peer) || ad_pending.size() >= 32) {
                    reply(command, request, false, {}, "not_friend");
                    return;
                }
                auto id = idstr(next_ad++);
                auto frame = ad_frame("query", id, context);
                social.send_peer(peer, 49, frame.dump());
                ad_pending[id] = {
                    {command, request, Clock::now() + std::chrono::seconds(5), 1}, peer, context};
                return;
            }
            if (op == "name.resolve" || op == "name.lookup") {
                friend_refresh = {};
                refresh_rooms();
                auto friend_list = social.friends();
                Json found = Json::array();
                auto names = command.value("names", Json::array());
                if (op == "name.lookup" && (!names.is_array() || names.size() > 500)) {
                    reply(command, request, false, {}, "invalid_request");
                    return;
                }
                for (const auto &friend_ : friend_list) {
                    if (!friends.contains(friend_.id))
                        continue;
                    auto name = social.presence(friend_.id, "osw.name");
                    if (!valid_online_id(name))
                        name = online_id_from_persona(friend_.display_name, friend_.id);
                    if (op == "name.resolve" && friend_.id == idfield(command, "peer")) {
                        reply(command, request, true, {{"online_id", name}});
                        return;
                    }
                    if (op == "name.lookup" &&
                        std::find(names.begin(), names.end(), Json(name)) != names.end() &&
                        found.size() < 100)
                        found.push_back({{"steam_id", idstr(friend_.id)}, {"online_id", name}});
                }
                if (op == "name.lookup")
                    reply(command, request, true, {{"players", found}});
                else if (idfield(command, "peer") == identity.account_id)
                    reply(command, request, true, {{"online_id", identity.online_id}});
                else
                    reply(command, request, false, {}, "not_found");
                return;
            }
            if (op == "directory.create" || op == "directory.search") {
                if (pending.size() + validating.size() + abandoned.size() >= 128) {
                    reply(command, request, false, {}, "busy");
                    return;
                }
                auto metadata = command.value("metadata", Json::object());
                int capacity = command.value("capacity", 8);
                int limit = command.value("limit", 32);
                if (!safe_metadata(metadata) || capacity < 1 || capacity > 16 || limit < 1 ||
                    limit > 32 || pending.size() >= 32 ||
                    (op == "directory.create" && rooms.size() >= 2)) {
                    reply(command, request, false, {}, "invalid_request");
                    return;
                }
                if (std::any_of(pending.begin(), pending.end(), [&](const auto &pair) {
                        return pair.second.command.value("op", std::string{}) == op;
                    })) {
                    reply(command, request, false, {}, "busy");
                    return;
                }
                auto call = op == "directory.create" ? social.create_room(capacity)
                                                     : social.search_rooms(filters(metadata), 1);
                if (!call) {
                    reply(command, request, false);
                    return;
                }
                pending[call] = {command, request, Clock::now() + std::chrono::seconds(25), 1};
                suppressed_selection = 0;
                return;
            }
            auto room = idfield(command, "room_id");
            if (op == "peer.endpoint" && command.value("scope", std::string{}) == "friend") {
                friend_refresh = {};
                refresh_rooms();
                auto peer = idfield(command, "peer");
                if (!friends.contains(peer)) {
                    reply(command, request, false, {}, "not_friend");
                    return;
                }
                auto &proxy = proxies[peer];
                proxy.room = 0;
                proxy.friend_scope = true;
                for (auto candidate : rooms)
                    if (member(candidate, peer)) {
                        proxy.room = candidate;
                        break;
                    }
                if (!prepare_proxy(peer, proxy)) {
                    reply(command, request, false, {}, "endpoint_unavailable");
                    return;
                }
                reply(command, request, true, proxy_endpoint(peer, proxy));
                return;
            }
            if (op == "directory.join") {
                if (awaiting_party.contains(room) || validating.contains(room) ||
                    std::any_of(abandoned.begin(), abandoned.end(),
                                [&](const auto &entry) { return entry.second == room; }) ||
                    std::any_of(pending.begin(), pending.end(), [&](const auto &entry) {
                        return idfield(entry.second.command, "room_id") == room;
                    })) {
                    reply(command, request, false, {}, "busy");
                    return;
                }
                if (!room || rooms.size() >= 2 || pending.size() >= 32) {
                    reply(command, request, false, {}, "limit");
                    return;
                }
                if (rooms.contains(room)) {
                    if (!command.value("_autojoin", false))
                        suppressed_selection = 0;
                    track_follow(room);
                    reply(command, request, true, record(room));
                    return;
                }
                if (pending.size() + validating.size() + abandoned.size() >= 128) {
                    reply(command, request, false, {}, "busy");
                    return;
                }
                Pending value{command, request, Clock::now() + std::chrono::seconds(25), 1};
                if (!matches(room, false)) {
                    if (validating.contains(room) || validating.size() >= 16 ||
                        !social.request_data(room)) {
                        reply(command, request, false);
                        return;
                    }
                    validating[room] = std::move(value);
                    if (!command.value("_autojoin", false))
                        suppressed_selection = 0;
                    return;
                }
                auto call = social.join_lobby(room);
                if (!call) {
                    reply(command, request, false);
                    return;
                }
                pending[call] = std::move(value);
                if (!command.value("_autojoin", false))
                    suppressed_selection = 0;
                return;
            }
            if (op == "directory.leave" && room)
                suppress_follow(room);
            if (!room || !member(room, identity.account_id)) {
                reply(command, request, false, {}, "not_member");
                return;
            }
            if (op == "directory.leave") {
                social.leave_lobby(room);
                rooms.erase(room);
                room_snapshots.erase(room);
                if (party.lobby && party.owner == identity.account_id &&
                    social.member_data(party.lobby, identity.account_id, "osw.room") == idstr(room))
                    social.set_member_data(party.lobby, "osw.room", "");
                reply(command, request, true);
                refresh_rooms();
                return;
            }
            if (op == "directory.members") {
                reply(command, request, true, record(room));
                return;
            }
            if (op == "directory.update") {
                auto metadata = command.value("metadata", Json::object());
                if (social.owner(room) != identity.account_id || !safe_metadata(metadata) ||
                    !set_metadata(room, metadata)) {
                    reply(command, request, false, {}, "not_owner");
                    return;
                }
                reply(command, request, true, record(room));
                return;
            }
            auto peer = idfield(command, "peer");
            if (!peer || peer == identity.account_id || !member(room, peer)) {
                reply(command, request, false, {}, "not_member");
                return;
            }
            if (op == "peer.endpoint") {
                auto &proxy = proxies[peer];
                proxy.room = room;
                if (!prepare_proxy(peer, proxy)) {
                    reply(command, request, false, {}, "endpoint_unavailable");
                    return;
                }
                reply(command, request, true, proxy_endpoint(peer, proxy));
                return;
            }
            if (op == "peer.send") {
                std::string payload;
                if (!command.contains("payload") || !command["payload"].is_string() ||
                    !unbase64(command["payload"], payload, 32768)) {
                    reply(command, request, false, {}, "invalid_payload");
                    return;
                }
                std::string packet = config.wire_protocol();
                for (int n = 0; n < 8; ++n)
                    packet += (char)(room >> (8 * n));
                packet += payload;
                reply(command, request, social.send_peer(peer, 47, packet));
                return;
            }
            reply(command, request, false, {}, "unsupported");
        } catch (...) {
            reply(command, request, false, {}, "invalid_request");
        }
    }
    void refresh_rooms() {
        std::set<std::uint64_t> allowed;
        if (Clock::now() - friend_refresh > std::chrono::seconds(1)) {
            friend_refresh = Clock::now();
            friends.clear();
            for (auto &f : social.friends())
                if (f.online && social.presence(f.id, "osw.protocol") == (config.protocol()) &&
                    social.presence(f.id, "osw.title") == config.title &&
                    social.presence(f.id, "osw.build") == config.build &&
                    social.presence(f.id, "osw.realm") == config.realm && friends.size() < 500)
                    friends.insert(f.id);
        }
        allowed.insert(friends.begin(), friends.end());
        for (auto it = rooms.begin(); it != rooms.end();) {
            auto room = *it;
            if (!member(room, identity.account_id)) {
                emit({{"event", "room.closed"}, {"room_id", idstr(room)}});
                social.leave_lobby(room);
                room_snapshots.erase(room);
                it = rooms.erase(it);
                continue;
            }
            auto r = record(room);
            auto snapshot = r.dump();
            if (room_snapshots[room] != snapshot) {
                room_snapshots[room] = snapshot;
                r["event"] = "room.changed";
                emit(r);
            }
            for (auto peer : social.members(room))
                if (peer != identity.account_id) {
                    allowed.insert(peer);
                    if (auto proxy = proxies.find(peer);
                        proxy != proxies.end() && !proxy->second.room)
                        proxy->second.room = room;
                }
            ++it;
        }
        for (auto it = proxies.begin(); it != proxies.end();) {
            if (!friends.contains(it->first))
                it->second.advertisement_grants.clear();
            if (it->second.room && !member(it->second.room, it->first) && friend_authorized(it->second) &&
                friends.contains(it->first))
                it->second.room = 0;
            if (!allowed.contains(it->first) || !member(it->second.room, it->first) ||
                (!it->second.room && !friend_authorized(it->second)))
                it = erase_proxy(it);
            else
                ++it;
        }
        social.allow_peers({allowed.begin(), allowed.end()});
    }
    void process(const std::vector<SocialEvent> &input) {
        if (std::any_of(input.begin(), input.end(), [](const auto &event) {
                return event.kind == SocialEvent::friends_changed;
            }))
            friend_refresh = {};
        for (auto &event : input) {
            if (event.call && abandoned.erase(event.call)) {
                if (event.success &&
                    (event.kind == SocialEvent::created || event.kind == SocialEvent::entered))
                    social.leave_lobby(event.lobby);
                continue;
            }
            if (event.kind == SocialEvent::data && validating.contains(event.lobby)) {
                auto value = std::move(validating.at(event.lobby));
                validating.erase(event.lobby);
                if (!event.success || !matches(event.lobby, false)) {
                    reply(value.command, value.request, false, {}, "namespace_mismatch");
                    continue;
                }
                auto call = social.join_lobby(event.lobby);
                if (call)
                    pending[call] = std::move(value);
                else
                    reply(value.command, value.request, false);
            }
            auto it = pending.find(event.call);
            if (event.call && it != pending.end()) {
                auto value = it->second;
                pending.erase(it);
                if (!event.success) {
                    reply(value.command, value.request, false);
                    continue;
                }
                if (event.kind == SocialEvent::listed) {
                    Json found = Json::array();
                    for (auto room : event.lobbies)
                        if (matches(room, false) && found.size() < value.command.value("limit", 32))
                            found.push_back(record(room, rooms.contains(room)));
                    if (found.empty() && value.distance < 3 && Clock::now() < value.deadline) {
                        auto call = social.search_rooms(
                            filters(value.command.value("metadata", Json::object())),
                            ++value.distance);
                        if (call) {
                            pending[call] = std::move(value);
                            continue;
                        }
                    }
                    reply(value.command, value.request, true, {{"rooms", found}});
                } else {
                    if (rooms.size() >= 2 && !rooms.contains(event.lobby)) {
                        social.leave_lobby(event.lobby);
                        reply(value.command, value.request, false, {}, "limit");
                        continue;
                    }
                    bool ok = event.kind == SocialEvent::created
                                  ? set_metadata(event.lobby,
                                                 value.command.value("metadata", Json::object()))
                                  : matches(event.lobby);
                    if (ok)
                        joined(event.lobby, std::move(value));
                    else {
                        social.leave_lobby(event.lobby);
                        reply(value.command, value.request, false, {}, "namespace_mismatch");
                    }
                }
            }
        }
        refresh_rooms();
        for (auto it = awaiting_party.begin(); it != awaiting_party.end();) {
            if (!member(it->first, identity.account_id)) {
                reply(it->second.command, it->second.request, false, {}, "room_closed");
                rollback(it->first);
                it = awaiting_party.erase(it);
                continue;
            }
            auto list = social.members(it->first);
            bool complete = std::all_of(party.members.begin(), party.members.end(), [&](auto peer) {
                return std::find(list.begin(), list.end(), peer) != list.end();
            });
            if (complete) {
                reply(it->second.command, it->second.request, true, record(it->first));
                it = awaiting_party.erase(it);
            } else if (Clock::now() >= it->second.deadline) {
                reply(it->second.command, it->second.request, false, {}, "party_timeout");
                rollback(it->first);
                it = awaiting_party.erase(it);
            } else
                ++it;
        }
        if (party.lobby && party.owner != identity.account_id &&
            std::find(party.members.begin(), party.members.end(), party.owner) !=
                party.members.end()) {
            auto selected =
                parse_decimal_id(social.member_data(party.lobby, party.owner, "osw.room"))
                    .value_or(0);
            if (suppressed_selection && selected != suppressed_selection)
                suppressed_selection = 0;
            if (selected && !rooms.contains(selected) && selected != following_pending &&
                selected != suppressed_selection && Clock::now() >= following_retry) {
                following_pending = selected;
                execute(
                    {{"op", "directory.join"}, {"room_id", idstr(selected)}, {"_autojoin", true}},
                    0);
            }
            if (following && following != selected) {
                social.leave_lobby(following);
                rooms.erase(following);
                emit({{"event", "room.closed"}, {"room_id", idstr(following)}});
                following = 0;
            }
        } else if (following) {
            social.leave_lobby(following);
            rooms.erase(following);
            emit({{"event", "room.closed"}, {"room_id", idstr(following)}});
            following = 0;
        }
    }
    void poll() {
        if (!config.companion_path.empty()) {
            for (auto &line : child.poll()) {
                auto command = Json::parse(line, nullptr, false);
                if (!command.is_object() || command.value("v", 0) != 1) {
                    broken = true;
                    break;
                }
                if (command.value("event", std::string{}) == "ready") {
                    if (ready) {
                        broken = true;
                        break;
                    }
                    const auto capabilities = command.value("capabilities", Json::array());
                    if (!capabilities.is_array() || capabilities.size() > 16 ||
                        !std::all_of(capabilities.begin(), capabilities.end(), [](const Json &entry) {
                            return entry.is_string() && entry.get_ref<const std::string &>().size() <= 64;
                        })) {
                        broken = true;
                        break;
                    }
                    owner_name_updates = std::find(capabilities.begin(), capabilities.end(),
                                                   Json("owner.name.v1")) != capabilities.end();
                    if (config.canonical_endpoints() &&
                        std::find(capabilities.begin(), capabilities.end(),
                                  Json(config.loopback_endpoints ? "loopback.endpoints.v1" : "virtual.endpoints.v1")) == capabilities.end()) {
                        broken = true;
                        break;
                    }
                    ready = true;
                    if (config.canonical_endpoints()) config.endpoints->set_ready();
                    continue;
                }
                if (!command.contains("id") || !command["id"].is_string() ||
                    command["id"].get_ref<const std::string &>().empty() ||
                    command["id"].get_ref<const std::string &>().size() > 64) {
                    broken = true;
                    break;
                }
                command.erase("_autojoin");
                execute(std::move(command), 0);
            }
            if (!child.alive() || (!ready && Clock::now() - start > std::chrono::seconds(15)))
                broken = true;
        }
        for (auto it = pending.begin(); it != pending.end();) {
            if (Clock::now() >= it->second.deadline) {
                reply(it->second.command, it->second.request, false, {}, "timeout");
                abandoned[it->first] = idfield(it->second.command, "room_id");
                it = pending.erase(it);
            } else
                ++it;
        }
        for (auto it = validating.begin(); it != validating.end();)
            if (Clock::now() >= it->second.deadline) {
                reply(it->second.command, it->second.request, false, {}, "timeout");
                it = validating.erase(it);
            } else
                ++it;
        for (auto it = ad_pending.begin(); it != ad_pending.end();)
            if (Clock::now() >= it->second.pending.deadline) {
                reply(it->second.pending.command, it->second.pending.request, false, {}, "timeout");
                it = ad_pending.erase(it);
            } else {
                if (Clock::now() >= it->second.retry && friends.contains(it->second.peer)) {
                    social.send_peer(it->second.peer, 49,
                                     ad_frame("query", it->first, it->second.context).dump());
                    it->second.retry = Clock::now() + std::chrono::milliseconds(500);
                }
                ++it;
            }
        for (auto &[peer, proxy] : proxies) {
            if (!proxy.socket)
                continue;
            for (int count = 0; count < 128; ++count) {
                std::string bytes, address;
                std::uint16_t port = 0;
                if (!proxy.socket->receive(bytes, address, port))
                    break;
                if (config.canonical_endpoints() && !config.endpoints->contains(peer))
                    continue;
                ++proxy_received;
                if (address != config.game_bind || port != config.game_port ||
                    bytes.size() > 65507) {
                    ++proxy_rejected;
                    continue;
                }
                std::string packet = config.wire_protocol();
                for (int n = 0; n < 8; ++n)
                    packet += (char)(proxy.room >> (8 * n));
                packet += bytes;
                if (social.send_peer(peer, 48, packet))
                    ++network_sent;
            }
        }
        for (auto &message : social.take_peer_messages()) {
            ++network_received;
            auto &rate = rates[message.sender];
            if (Clock::now() - rate.first > std::chrono::seconds(1))
                rate = {Clock::now(), 0};
            if (++rate.second > 1024)
                continue;
            if (message.channel == 49) {
                try {
                    if (!friends.contains(message.sender) || message.bytes.size() > 8192)
                        continue;
                    auto frame = Json::parse(message.bytes, nullptr, false);
                    if (!frame.is_object() || frame.value("v", 0) != 1 ||
                        frame.value("protocol", std::string{}) != (config.ad_protocol()) ||
                        frame.value("title", std::string{}) != config.title ||
                        frame.value("build", std::string{}) != config.build ||
                        frame.value("realm", std::string{}) != config.realm)
                        continue;
                    auto id = frame.value("id", std::string{}),
                         context = frame.value("context", std::string{}),
                         op = frame.value("op", std::string{});
                    if (id.empty() || id.size() > 32 || context.empty() || context.size() > 32)
                        continue;
                    if (op == "query") {
                        auto response = ad_frame("reply", id, context);
                        auto found = advertisements.find(context);
                        response["found"] = found != advertisements.end();
                        if (found != advertisements.end())
                            response["payload"] = found->second;
                        if (social.send_peer(message.sender, 49, response.dump()) &&
                            found != advertisements.end())
                            proxies[message.sender].advertisement_grants.insert(context);
                    } else if (op == "reply") {
                        auto it = ad_pending.find(id);
                        if (it == ad_pending.end() || it->second.peer != message.sender ||
                            it->second.context != context)
                            continue;
                        std::string bytes;
                        auto payload = frame.value("payload", std::string{});
                        bool ok = frame.value("found", false) && unbase64(payload, bytes, 4096);
                        reply(it->second.pending.command, it->second.pending.request, ok,
                              {{"payload", payload}}, "not_found");
                        ad_pending.erase(it);
                    }
                } catch (const Json::exception &) {
                    // A malformed peer frame is discarded without affecting the owner context.
                }
                continue;
            }
            if (message.bytes.size() < 12 ||
                message.bytes.compare(0, 4, config.wire_protocol()) != 0)
                continue;
            std::uint64_t room = 0;
            for (int n = 0; n < 8; ++n)
                room |= (std::uint64_t)(unsigned char)message.bytes[4 + n] << (8 * n);
            if (!member(room, message.sender))
                continue;
            auto bytes = message.bytes.substr(12);
            if (message.channel == 47 && room && bytes.size() <= 32768)
                emit({{"event", "peer.message"},
                      {"room_id", idstr(room)},
                      {"sender", idstr(message.sender)},
                      {"payload", base64(bytes)}});
            else if (message.channel == 48 && bytes.size() <= 65507) {
                if (!room &&
                    (!proxies.contains(message.sender) || !friend_authorized(proxies.at(message.sender))))
                    continue;
                auto &proxy = proxies[message.sender];
                proxy.room = room;
                if (!prepare_proxy(message.sender, proxy))
                    continue;
                if (proxy.socket->send(bytes, config.game_bind, (std::uint16_t)config.game_port))
                    ++proxy_sent;
            }
        }
    }
};
P2P::P2P(const Config &c, const Identity &i, Social &s) : p(std::make_unique<Impl>(c, i, s)) {}
P2P::~P2P() = default;
bool P2P::handles(const Request &request) const {
    return request.operation == OSW_P2P ||
           (request.operation == OSW_TOKEN &&
            (!p->config.companion_path.empty() || p->config.backend == 1));
}
void P2P::submit(Request request) {
    if (request.operation == OSW_TOKEN) {
        auto j = Json::parse(request.metadata, nullptr, false);
        if (!j.is_object() || !j.contains("client_id") || !j["client_id"].is_string() ||
            j["client_id"].get_ref<const std::string &>().empty() ||
            j["client_id"].get_ref<const std::string &>().size() > 128 || !j.contains("scope") ||
            !j["scope"].is_string() ||
            (j["scope"] != "openid" && j["scope"] != "openid openid:duid" &&
             j["scope"] != "openid,openid:duid")) {
            p->update.results.push_back({request.id, OSW_INVALID_ARGUMENT});
            return;
        }
        Result result{request.id};
        result.body.assign(p->token.begin(), p->token.end());
        p->update.results.push_back(std::move(result));
    } else {
        auto command = Json::parse(request.metadata, nullptr, false);
        if (command.is_object())
            command.erase("_autojoin");
        p->execute(std::move(command), request.id);
    }
}
void P2P::process(const std::vector<SocialEvent> &events) { p->process(events); }
void P2P::set_party(Party party) {
    if (party.lobby != p->party.lobby || party.owner != p->party.owner)
        p->suppressed_selection = 0;
    p->party = std::move(party);
}
bool P2P::supports_name_updates() const {
    return p->config.companion_path.empty() || (p->ready && p->owner_name_updates);
}
bool P2P::update_name(const std::string &name) {
    if (!valid_online_id(name) || !supports_name_updates())
        return false;
    if (p->identity.online_id == name)
        return true;
    if (!p->config.companion_path.empty() &&
        !p->child.send(Json{{"v", 1}, {"event", "owner.name"},
                            {"steam_id", idstr(p->identity.account_id)}, {"online_id", name}}.dump())) {
        p->broken = true;
        return false;
    }
    // The token remains the exact bootstrap capability, including its initial label.
    p->identity.online_id = name;
    return true;
}
void P2P::cancel(std::uint64_t request) {
    for (auto it = p->pending.begin(); it != p->pending.end();)
        if (it->second.request == request) {
            p->abandoned[it->first] = idfield(it->second.command, "room_id");
            it = p->pending.erase(it);
        } else
            ++it;
    std::erase_if(p->validating,
                  [&](const auto &entry) { return entry.second.request == request; });
    for (auto it = p->awaiting_party.begin(); it != p->awaiting_party.end();)
        if (it->second.request == request) {
            p->rollback(it->first);
            it = p->awaiting_party.erase(it);
        } else
            ++it;
    if (auto it = p->completed_rooms.find(request); it != p->completed_rooms.end()) {
        auto room = it->second;
        p->completed_rooms.erase(it);
        p->rollback(room);
    }
    std::erase_if(p->ad_pending,
                  [&](const auto &entry) { return entry.second.pending.request == request; });
}
Update P2P::poll() {
    p->poll();
    if (p->broken && p->config.canonical_endpoints())
        p->config.endpoints->close();
    Update out;
    std::swap(out, p->update);
    out.failed = p->broken;
    return out;
}
bool P2P::failed() const { return p->broken; }
} // namespace osw::core
