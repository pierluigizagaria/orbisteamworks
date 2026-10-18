#include "core/platform.hpp"
#include "core/blobs.hpp"
#include "core/credential.hpp"
#include "core/multipart.hpp"
#include <algorithm>
#include <charconv>
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>
#include <optional>
#include <set>

namespace osw::core {
namespace {
using Json = nlohmann::json;
using Id = std::uint64_t;
using Clock = std::chrono::steady_clock;
constexpr size_t max_document = 6000;
const std::vector<std::string> presence_keys{"osw.protocol", "osw.title", "osw.build",
                                             "osw.realm",    "osw.name",  "osw.lobby"};
std::string text(const Json &value, const char *key) {
    auto it = value.find(key);
    return it != value.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
std::string encode(std::string_view value) {
    return hex(std::vector<std::uint8_t>(value.begin(), value.end()));
}
std::optional<std::string> decode(const std::string &value) {
    if (value.size() % 2 || value.size() > 2 * 1024 * 1024)
        return {};
    std::string result(value.size() / 2, '\0');
    const auto digit = [](char c) {
        return c >= '0' && c <= '9' ? c - '0' : c >= 'a' && c <= 'f' ? c - 'a' + 10 : -1;
    };
    for (size_t i = 0; i < result.size(); ++i) {
        int a = digit(value[i * 2]), b = digit(value[i * 2 + 1]);
        if (a < 0 || b < 0)
            return {};
        result[i] = static_cast<char>((a << 4) | b);
    }
    return result;
}
std::vector<std::string> split(std::string path) {
    std::vector<std::string> result;
    size_t start = path.starts_with('/') ? 1 : 0;
    while (start < path.size()) {
        auto end = path.find('/', start);
        result.push_back(path.substr(start, end == path.npos ? path.size() - start : end - start));
        if (end == path.npos)
            break;
        start = end + 1;
    }
    return result;
}
std::optional<std::string> unescape(std::string_view value) {
    std::string result;
    for (size_t i = 0; i < value.size(); ++i) {
        if (value[i] == '%') {
            if (i + 2 >= value.size())
                return {};
            unsigned int c = 0;
            auto [end, error] = std::from_chars(value.data() + i + 1, value.data() + i + 3, c, 16);
            if (error != std::errc{} || end != value.data() + i + 3 || !c)
                return {};
            result += static_cast<char>(c);
            i += 2;
        } else
            result += value[i] == '+' ? ' ' : value[i];
    }
    return result;
}
} // namespace

struct Platform::Impl {
    Config config;
    Identity identity;
    Social &social;
    Blobs blobs;
    Update update;
    std::set<Id> joined;
    std::map<Id, std::map<Id, std::string>> member_snapshots;
    std::map<Id, std::string> document_snapshots;
    std::map<Id, std::pair<Friend, std::string>> known_friends;
    // List requests refresh known_friends independently of provider callbacks.
    // Keep notification history separate so a GET cannot swallow a transition.
    std::map<Id, std::string> notified_friends;
    struct Pending {
        Request request;
        Id lobby = 0;
        Json document;
        bool canceled = false;
        std::string invitation;
        bool read = false;
        Clock::time_point deadline = Clock::now() + std::chrono::seconds(30);
    };
    std::map<Id, Pending> calls;
    std::multimap<Id, Pending> preparing;
    std::map<Id, std::vector<Request>> reads;
    struct Mutation {
        Id lobby;
        bool provisional;
    };
    std::map<Id, Mutation> completed_mutations;
    struct Invitation {
        Id lobby = 0, sender = 0;
        std::string name;
        bool accepted = false, declined = false;
    };
    std::map<std::string, Invitation> invitations;
    struct Incoming {
        Id sender;
        bool accept;
        Clock::time_point deadline;
    };
    std::map<Id, Incoming> incoming;
    bool stopped = false;
    Id invitation_sequence = 0;
    const char *publication_stage = "none";
    int publication_detail = 0;

    Impl(Config cfg, Identity id, Social &api)
        : config(std::move(cfg)), identity(std::move(id)), social(api),
          blobs(api, identity.account_id) {}
    void result(Id id, int status, int http = 0, const std::string &body = {},
                const std::string &type = "application/json") {
        Result value;
        value.id = id;
        value.status = status;
        value.http_status = http;
        value.metadata = Json{{"content_type", type}}.dump();
        value.body.assign(body.begin(), body.end());
        update.results.push_back(std::move(value));
    }
    void json(Id id, int status, const Json &body) { result(id, OSW_OK, status, body.dump()); }
    void unavailable(Id id, const char *stage, int detail = 0) {
        result(id, OSW_UNAVAILABLE);
        update.results.back().metadata = Json{{"stage", stage}, {"detail", detail}}.dump();
    }
    void error(Id id, int status) {
        json(id, status,
             Json{{"error", {{"code", status}, {"message", "Platform request failed"}}}});
    }
    bool compatible(Id lobby, const std::optional<LobbySnapshot> &state) {
        const auto data = [&](const char *key) {
            return state ? state->data.at(key).get() : social.lobby_data(lobby, key);
        };
        return data("osw.protocol") == (config.virtual_endpoints ? "2" : "1") &&
               data("osw.title") == config.title && data("osw.build") == config.build &&
               data("osw.realm") == config.realm;
    }
    bool compatible(Id lobby) {
        return compatible(lobby, social.lobby_snapshot(
                                     lobby, {"osw.protocol", "osw.title", "osw.build", "osw.realm"}, false));
    }
    bool compatible_document(const Json &document) {
        return document.is_object() && text(document, "protocol") == (config.virtual_endpoints ? "2" : "1") &&
               text(document, "title") == config.title && text(document, "build") == config.build &&
               text(document, "realm") == config.realm && document.contains("description") &&
               valid_description(document["description"]) && document.contains("created") &&
               document["created"].is_number_unsigned() && !text(document, "creator").empty() &&
               document.contains("blobs") && document["blobs"].is_object();
    }
    bool valid_description(const Json &value) {
        if (!value.is_object() || !value.contains("sessionMaxUser") ||
            !value["sessionMaxUser"].is_number_integer())
            return false;
        const auto limit = value["sessionMaxUser"].get<std::int64_t>();
        const auto kind = text(value, "sessionType"), privacy = text(value, "sessionPrivacy");
        if (limit < 1 || limit > 250 || (kind != "owner-bind" && kind != "owner-migration") ||
            (privacy != "private" && privacy != "public"))
            return false;
        if (value.contains("sessionLockFlag") && !value["sessionLockFlag"].is_boolean())
            return false;
        return true;
    }
    void refresh_friends(std::vector<Friend> *diagnostic = nullptr) {
        auto list = social.friends();
        std::set<Id> remaining;
        for (auto &user : list) {
            remaining.insert(user.id);
            if (!user.online || social.presence(user.id, "osw.protocol") != (config.virtual_endpoints ? "2" : "1") ||
                social.presence(user.id, "osw.title") != config.title ||
                social.presence(user.id, "osw.build") != config.build ||
                social.presence(user.id, "osw.realm") != config.realm) {
                if (auto old = known_friends.find(user.id); old != known_friends.end()) {
                    old->second.first = user;
                    old->second.first.online = false;
                } else if (diagnostic)
                    diagnostic->push_back(user);
                continue;
            }
            auto name = social.presence(user.id, "osw.name");
            if (valid_online_id(name)) {
                user.online = true;
                known_friends[user.id] = {user, name};
            }
        }
        std::erase_if(known_friends,
                      [&](const auto &item) { return !remaining.contains(item.first); });
        for (auto &[key, invitation] : invitations)
            if (auto friend_ = known_friends.find(invitation.sender); friend_ != known_friends.end())
                invitation.name = friend_->second.second;
    }
    void notify_friend_changes() {
        std::map<Id, std::string> current;
        for (const auto &[id, value] : known_friends)
            if (value.first.online)
                current[id] = value.second;
        for (const auto &[id, name] : current) {
            const auto previous = notified_friends.find(id);
            if (previous == notified_friends.end() || previous->second != name)
                update.events.push_back({OSW_FRIENDS_CHANGED, 0, id, {}, {}, name,
                                         previous == notified_friends.end() ? 1u : 3u});
        }
        for (const auto &[id, name] : notified_friends)
            if (!current.contains(id))
                update.events.push_back({OSW_FRIENDS_CHANGED, 0, id, {}, {}, name, 2});
        notified_friends = std::move(current);
    }
    Json user(Id id, const std::string &name) {
        return Json{{"accountId", std::to_string(id)}, {"onlineId", name}};
    }
    Json friend_entry(const Friend &value, const std::string &name) {
        const auto status = value.online ? "online" : "offline";
        Json entry{{"user", user(value.id, name)},
                   {"onlineId", name},
                   {"accountId", std::to_string(value.id)},
                   {"onlineStatus", status},
                   {"presence", {{"onlineStatus", status}}}};
        const auto display_name = clean_text(value.display_name, 32);
        if (!display_name.empty())
            entry["personalDetail"] = {{"displayName", display_name}};
        return entry;
    }
    Json friend_list() {
        std::vector<Friend> diagnostic;
        refresh_friends(config.diag_all_friends ? &diagnostic : nullptr);
        auto entries = Json::array();
        std::set<std::string> names;
        for (const auto &[id, value] : known_friends) {
            names.insert(value.second);
            if (value.first.online)
                entries.push_back(friend_entry(value.first, value.second));
        }
        for (const auto &value : diagnostic) {
            const auto name = "s" + std::to_string(static_cast<std::uint32_t>(value.id));
            if (names.insert(name).second)
                entries.push_back(friend_entry(value, name));
        }
        return Json{{"totalResults", entries.size()},
                    {"start", 0},
                    {"size", entries.size()},
                    {"friendList", entries}};
    }
    std::optional<Json> load(Id lobby) {
        const auto state = social.lobby_snapshot(
            lobby, {"osw.protocol", "osw.title", "osw.build", "osw.realm", "osw.document"}, false);
        if (!compatible(lobby, state))
            return {};
        const auto bytes = state ? state->data.at("osw.document").get()
                                 : social.lobby_data(lobby, "osw.document");
        if (bytes.size() > max_document)
            return {};
        auto document = Json::parse(bytes, nullptr, false);
        if (!compatible_document(document))
            return {};
        if (!parse_decimal_id(text(document, "creator")))
            return {};
        return document;
    }
    bool save(Id lobby, Json document) {
        publication_stage = "owner";
        publication_detail = 0;
        if (social.owner(lobby) != identity.account_id)
            return false;
        if (!document.contains("blobs"))
            document["blobs"] = Json::object();
        for (const auto &key : {"session-image", "session-data"}) {
            auto encoded = text(document, key);
            if (encoded.size() <= 1024)
                continue;
            if (!blobs.install(lobby, key, encoded))
                return false;
            document["blobs"][key] =
                Json{{"size", encoded.size()}, {"hash", Blobs::checksum(encoded)}};
            document.erase(key);
        }
        const auto bytes = document.dump();
        if (bytes.size() > max_document)
            return false;
        publication_stage = "document";
        publication_detail = 0;
        return social.set_lobby_data(lobby, "osw.document", bytes);
    }
    Json members(Id lobby) {
        auto result = Json::array();
        for (auto id : social.members(lobby)) {
            auto name = social.member_data(lobby, id, "osw.member");
            if (valid_online_id(name)) {
                auto value = user(id, name);
                value["platform"] = "PS4";
                result.push_back(value);
            }
        }
        return result;
    }
    bool committed(Id lobby) {
        return joined.contains(lobby) &&
               valid_online_id(social.member_data(lobby, identity.account_id, "osw.member"));
    }
    bool overlay_party(Id lobby) {
        if (!compatible(lobby) || !committed(lobby) ||
            !social.lobby_data(lobby, "osw_room").empty()) return false;
        const auto users = social.members(lobby);
        return std::find(users.begin(), users.end(), identity.account_id) != users.end();
    }
    void overlay(const Request &request, const Json &meta) {
        const auto id = request.id;
        const auto action = text(meta, "action");
        if (!request.body.empty() || !meta.contains("action") || !meta["action"].is_string() ||
            (meta.contains("session_id") &&
             (action != "invite" || !meta["session_id"].is_string()))) {
            result(id, OSW_INVALID_ARGUMENT);
            return;
        }
        for (auto it = meta.begin(); it != meta.end(); ++it)
            if (it.key() != "action" && it.key() != "session_id") {
                result(id, OSW_INVALID_ARGUMENT);
                return;
            }
        if (action != "status" && action != "friends" && action != "invite") {
            result(id, OSW_NOT_SUPPORTED);
            return;
        }
        const auto state = social.overlay_status();
        if (!state.supported) {
            result(id, OSW_NOT_SUPPORTED);
            return;
        }
        if (action == "status") {
            json(id, 200, Json{{"available", state.available}, {"active", state.active}});
            return;
        }
        if (!state.available) {
            result(id, OSW_UNAVAILABLE);
            return;
        }
        if (action == "friends") {
            const bool issued = social.overlay_friends();
            result(id, issued ? OSW_OK : OSW_UNAVAILABLE, issued ? 204 : 0);
            return;
        }
        Id selected = 0;
        if (meta.contains("session_id")) {
            const auto value = parse_decimal_id(text(meta, "session_id"));
            if (!value) {
                result(id, OSW_INVALID_ARGUMENT);
                return;
            }
            if (overlay_party(*value)) selected = *value;
        } else {
            for (const auto lobby : joined) {
                if (!overlay_party(lobby)) continue;
                if (selected) {
                    result(id, OSW_LIMIT);
                    return;
                }
                selected = lobby;
            }
        }
        if (!selected) {
            result(id, OSW_NOT_FOUND);
            return;
        }
        const bool issued = social.overlay_invite(selected);
        result(id, issued ? OSW_OK : OSW_UNAVAILABLE, issued ? 204 : 0);
    }
    bool alive(Id lobby, const Json &document) {
        if (!joined.contains(lobby))
            return true;
        if (document["description"].value("sessionType", "") != "owner-bind")
            return true;
        auto list = social.members(lobby);
        const auto creator = parse_decimal_id(text(document, "creator"));
        return creator && std::find(list.begin(), list.end(), *creator) != list.end();
    }
    Json session(Id lobby, const Json &document) {
        auto value = document["description"];
        value["sessionId"] = std::to_string(lobby);
        value["members"] = members(lobby);
        const auto owner = social.owner(lobby);
        value["sessionCreator"] = user(owner, social.member_data(lobby, owner, "osw.member"));
        value["sessionCreateTimestamp"] = document.value("created", Id{0});
        return value;
    }
    void remember_mutation(Id request, Id lobby, bool provisional = false) {
        if (completed_mutations.size() >= 128)
            completed_mutations.erase(completed_mutations.begin());
        completed_mutations[request] = {lobby, provisional};
    }
    void commit(Id lobby) {
        social.set_member_data(lobby, "osw.member", identity.online_id);
        social.set_presence("osw.lobby", std::to_string(lobby));
    }
    void session_events(Id lobby) {
        if (!joined.contains(lobby) || !compatible(lobby))
            return;
        const auto document = load(lobby);
        std::map<Id, std::string> current;
        if (document && alive(lobby, *document)) {
            for (auto id : social.members(lobby)) {
                const auto name = social.member_data(lobby, id, "osw.member");
                if (valid_online_id(name))
                    current[id] = name;
            }
        }
        auto &previous = member_snapshots[lobby];
        for (const auto &[id, name] : current)
            if (!previous.contains(id))
                update.events.push_back(
                    {OSW_SESSION_CHANGED, 0, id, std::to_string(lobby), {}, name, 1});
            else if (previous.at(id) != name)
                update.events.push_back(
                    {OSW_SESSION_CHANGED, 0, id, std::to_string(lobby), {}, name, 3});
        for (const auto &[id, name] : previous)
            if (!current.contains(id))
                update.events.push_back(
                    {OSW_SESSION_CHANGED, 0, id, std::to_string(lobby), {}, name, 2});
        previous = std::move(current);
        const auto bytes = social.lobby_data(lobby, "osw.document");
        if (document_snapshots[lobby] != bytes) {
            document_snapshots[lobby] = bytes;
            update.events.push_back({OSW_SESSION_CHANGED, 0, 0, std::to_string(lobby), {}, {}, 3});
        }
    }
    void leave(Id lobby) {
        if (!joined.erase(lobby))
            return;
        if (compatible(lobby))
            social.set_member_data(lobby, "osw.member", "");
        social.leave_lobby(lobby);
        blobs.erase(lobby);
        member_snapshots.erase(lobby);
        document_snapshots.erase(lobby);
        social.set_presence("osw.lobby", "");
    }
    void create(Request request, const Json &metadata) {
        if (joined.size() >= 16 || calls.size() >= 128) {
            result(request.id, OSW_LIMIT);
            return;
        }
        const std::string bytes(request.body.begin(), request.body.end());
        auto parsed = multipart(text(metadata, "content_type"), bytes);
        if (!parsed) {
            result(request.id, OSW_INVALID_ARGUMENT);
            return;
        }
        const auto &parts = *parsed;
        auto it = parts.find("session-request");
        if (it == parts.end() || it->second.data.size() > 32768) {
            error(request.id, 400);
            return;
        }
        auto description = Json::parse(it->second.data, nullptr, false);
        if (!valid_description(description)) {
            error(request.id, 400);
            return;
        }
        const auto limit = description["sessionMaxUser"].get<int>();
        const auto kind = text(description, "sessionType");
        const auto privacy = text(description, "sessionPrivacy");
        if (limit < 1 || limit > 250 || (kind != "owner-bind" && kind != "owner-migration") ||
            (privacy != "public" && privacy != "private")) {
            error(request.id, 400);
            return;
        }
        Json document{{"protocol", config.virtual_endpoints ? "2" : "1"},
                      {"title", config.title},
                      {"build", config.build},
                      {"realm", config.realm},
                      {"description", description},
                      {"creator", std::to_string(identity.account_id)},
                      {"created", std::chrono::duration_cast<std::chrono::milliseconds>(
                                      std::chrono::system_clock::now().time_since_epoch())
                                      .count()}};
        for (const auto &[name, part] : parts) {
            if (name == "session-request")
                continue;
            size_t limit_bytes = name == "session-image"             ? 160 * 1024
                                 : name == "session-data"            ? 1024 * 1024
                                 : name == "changeable-session-data" ? 1024
                                                                     : 0;
            if (!limit_bytes || part.data.size() > limit_bytes) {
                error(request.id, 413);
                return;
            }
            document[name] = encode(part.data);
        }
        if (description.dump().size() > 3000) {
            error(request.id, 413);
            return;
        }
        auto call = social.create_lobby(limit);
        if (!call) {
            unavailable(request.id, "create_submission");
            return;
        }
        calls.emplace(call, Pending{std::move(request), 0, std::move(document)});
    }
    void accept(Request request, const std::string &invitation_id) {
        auto it = invitations.find(invitation_id);
        if (it == invitations.end() || it->second.declined) {
            error(request.id, 404);
            return;
        }
        if (it->second.accepted) {
            if (joined.contains(it->second.lobby))
                result(request.id, OSW_OK, 204);
            else
                error(request.id, 404);
            return;
        }
        auto &invitation = it->second;
        if (!compatible(invitation.lobby)) {
            error(request.id, 404);
            return;
        }
        if (joined.contains(invitation.lobby)) {
            invitation.accepted = true;
            update.events.push_back({OSW_INVITATION_ACCEPTED, 0, invitation.sender,
                                     std::to_string(invitation.lobby), invitation_id,
                                     invitation.name});
            result(request.id, OSW_OK, 204);
            return;
        }
        if (calls.size() >= 128 || joined.size() >= 16) {
            result(request.id, OSW_LIMIT);
            return;
        }
        for (const auto &[call, pending] : calls)
            if (pending.invitation == invitation_id && !pending.canceled) {
                result(request.id, OSW_LIMIT);
                return;
            }
        auto call = social.join_lobby(invitation.lobby);
        if (!call) {
            result(request.id, OSW_UNAVAILABLE);
            return;
        }
        calls.emplace(call,
                      Pending{std::move(request), invitation.lobby, {}, false, invitation_id});
    }
    void send_invite(Id request, Id lobby, const Json &data) {
        if (!compatible(lobby) || !committed(lobby)) {
            error(request, 403);
            return;
        }
        if (!text(data, "message").empty()) {
            error(request, 501);
            return;
        }
        if (!data.contains("to") || !data["to"].is_array() || data["to"].empty() ||
            data["to"].size() > 16) {
            error(request, 400);
            return;
        }
        refresh_friends();
        std::vector<Id> targets;
        for (const auto &recipient : data["to"]) {
            if (!recipient.is_string()) {
                error(request, 400);
                return;
            }
            const auto target = recipient.get<std::string>();
            const auto account = parse_decimal_id(target);
            auto found = account ? known_friends.find(*account) : known_friends.end();
            // An exact account selection cannot be redirected by another
            // friend's numeric online ID. Numeric names still work otherwise.
            if (found == known_friends.end())
                found = std::find_if(known_friends.begin(), known_friends.end(),
                                     [&](const auto &pair) { return pair.second.second == target; });
            if (found == known_friends.end() || !found->second.first.online) {
                error(request, 404);
                return;
            }
            targets.push_back(found->first);
        }
        for (auto target : targets)
            if (!social.invite(lobby, target)) {
                result(request, OSW_UNAVAILABLE);
                return;
            }
        result(request, OSW_OK, 204);
    }
    void session_request(Request request, Id lobby, const std::vector<std::string> &path,
                         const Json &meta) {
        const auto loaded = load(lobby);
        if (!loaded || !alive(lobby, *loaded)) {
            error(request.id, 404);
            return;
        }
        auto document = *loaded;
        const auto method = text(meta, "method");
        const auto suffix = path.size() > 3 ? path[3] : std::string{};
        if (method == "GET") {
            if (suffix.empty()) {
                json(request.id, 200, session(lobby, document));
                return;
            }
            if (suffix == "members") {
                json(request.id, 200, Json{{"members", members(lobby)}});
                return;
            }
            const auto key = suffix == "sessionData"             ? "session-data"
                             : suffix == "changeableSessionData" ? "changeable-session-data"
                             : suffix == "sessionImage"          ? "session-image"
                                                                 : "";
            if (*key) {
                auto value = decode(document["blobs"].contains(key) ? blobs.get(lobby, key)
                                                                    : text(document, key));
                if (!value) {
                    error(request.id, 503);
                    return;
                }
                result(request.id, OSW_OK, 200, *value,
                       suffix == "sessionImage" ? "image/jpeg" : "application/octet-stream");
                return;
            }
        }
        if (method == "POST" && suffix == "members") {
            if (document["description"].value("sessionLockFlag", false) && !committed(lobby)) {
                error(request.id, 403);
                return;
            }
            if (joined.contains(lobby)) {
                commit(lobby);
                result(request.id, OSW_OK, 204);
                return;
            }
            if (calls.size() >= 128 || joined.size() >= 16) {
                result(request.id, OSW_LIMIT);
                return;
            }
            if (document["description"].value("sessionLockFlag", false)) {
                error(request.id, 403);
                return;
            }
            auto call = social.join_lobby(lobby);
            if (!call) {
                result(request.id, OSW_UNAVAILABLE);
                return;
            }
            calls.emplace(call, Pending{std::move(request), lobby});
            return;
        }
        if (method == "DELETE" && suffix == "members" && path.size() == 5) {
            if (path[4] != "me" && path[4] != identity.online_id &&
                path[4] != std::to_string(identity.account_id)) {
                error(request.id, 403);
                return;
            }
            if (!committed(lobby)) {
                error(request.id, 404);
                return;
            }
            leave(lobby);
            result(request.id, OSW_OK, 204);
            return;
        }
        if (method == "POST" && suffix == "invitations") {
            Json data;
            const std::string body(request.body.begin(), request.body.end());
            if (text(meta, "content_type").starts_with("multipart/")) {
                const auto parsed = multipart(text(meta, "content_type"), body);
                if (!parsed) {
                    result(request.id, OSW_INVALID_ARGUMENT);
                    return;
                }
                const auto &parts = *parsed;
                auto it = parts.find("invitation-request");
                if (it == parts.end()) {
                    error(request.id, 400);
                    return;
                }
                if (parts.contains("invitation-data") &&
                    !parts.at("invitation-data").data.empty()) {
                    error(request.id, 501);
                    return;
                }
                data = Json::parse(it->second.data, nullptr, false);
            } else
                data = Json::parse(body, nullptr, false);
            send_invite(request.id, lobby, data);
            return;
        }
        if ((method == "PUT" || method == "DELETE") && social.owner(lobby) != identity.account_id) {
            error(request.id, 403);
            return;
        }
        if (method == "DELETE" && suffix.empty()) {
            social.set_joinable(lobby, false);
            if (!social.delete_lobby_data(lobby, "osw.document")) {
                result(request.id, OSW_UNAVAILABLE);
                return;
            }
            leave(lobby);
            result(request.id, OSW_OK, 204);
            return;
        }
        if (method == "PUT" && suffix == "changeableSessionData") {
            if (request.body.size() > 1024) {
                error(request.id, 413);
                return;
            }
            document["changeable-session-data"] = hex(request.body);
            if (!save(lobby, document)) {
                result(request.id, OSW_UNAVAILABLE);
                return;
            }
            result(request.id, OSW_OK, 204);
            return;
        }
        if (method == "PUT" && suffix.empty()) {
            auto patch = Json::parse(request.body.begin(), request.body.end(), nullptr, false);
            if (!patch.is_object()) {
                error(request.id, 400);
                return;
            }
            static const std::set<std::string> allowed{
                "sessionName",           "sessionStatus",
                "localizedSessionNames", "localizedSessionStatus",
                "sessionPrivacy",        "sessionMaxUser",
                "sessionLockFlag",       "sendNotificationFlag"};
            for (auto it = patch.begin(); it != patch.end(); ++it) {
                if (!allowed.contains(it.key())) {
                    error(request.id, 400);
                    return;
                }
                document["description"][it.key()] = it.value();
            }
            auto &description = document["description"];
            if (!valid_description(description)) {
                error(request.id, 400);
                return;
            }
            const int limit = description.value("sessionMaxUser", 0);
            const auto &previous = (*loaded)["description"];
            const bool ok =
                social.set_limit(lobby, limit) &&
                social.set_joinable(lobby, !description.value("sessionLockFlag", false)) &&
                social.set_friends_visible(lobby,
                                           text(description, "sessionPrivacy") == "public") &&
                save(lobby, document);
            if (!ok) {
                social.set_limit(lobby, previous.value("sessionMaxUser", 8));
                social.set_joinable(lobby, !previous.value("sessionLockFlag", false));
                social.set_friends_visible(lobby, text(previous, "sessionPrivacy") == "public");
                result(request.id, OSW_UNAVAILABLE);
                return;
            }
            result(request.id, OSW_OK, 204);
            return;
        }
        error(request.id, 501);
    }
    void webapi(Request request, Json meta) {
        auto raw = text(meta, "path");
        if (raw.size() > 8192 || raw.find('\0') != raw.npos || !raw.starts_with("/v1/")) {
            error(request.id, 400);
            return;
        }
        const auto query = raw.find('?');
        auto path = split(raw.substr(0, query));
        for (auto &item : path) {
            auto decoded = unescape(item);
            if (!decoded) {
                result(request.id, OSW_INVALID_ARGUMENT);
                return;
            }
            item = std::move(*decoded);
        }
        const auto method = text(meta, "method");
        if (path.size() == 2 && path[1] == "sessions" && method == "POST") {
            create(std::move(request), meta);
            return;
        }
        if (path.size() >= 3 && path[1] == "sessions") {
            const auto parsed = parse_decimal_id(path[2]);
            if (!parsed) {
                result(request.id, OSW_INVALID_ARGUMENT);
                return;
            }
            const auto lobby = *parsed;
            if (!load(lobby)) {
                size_t pending = 0;
                for (const auto &[key, list] : reads)
                    pending += list.size();
                if (pending >= 128) {
                    result(request.id, OSW_LIMIT);
                    return;
                }
                if (!social.request_data(lobby)) {
                    error(request.id, 404);
                    return;
                }
                reads[lobby].push_back(std::move(request));
                return;
            }
            if (method == "GET" && !joined.contains(lobby)) {
                if (calls.size() >= 128 || joined.size() >= 16) {
                    result(request.id, OSW_LIMIT);
                    return;
                }
                auto call = social.join_lobby(lobby);
                if (!call) {
                    result(request.id, OSW_UNAVAILABLE);
                    return;
                }
                calls.emplace(call, Pending{std::move(request), lobby, {}, false, {}, true});
                return;
            }
            if (joined.contains(lobby)) {
                const auto document = load(lobby);
                const int state = document ? blobs.ensure(lobby, (*document)["blobs"]) : -1;
                if (state < 0) {
                    result(request.id, OSW_UNAVAILABLE);
                    return;
                }
                if (!state) {
                    if (preparing.size() >= 128) {
                        result(request.id, OSW_LIMIT);
                        return;
                    }
                    preparing.emplace(request.id,
                                      Pending{std::move(request), lobby, {}, false, {}, true});
                    return;
                }
            }
            session_request(std::move(request), lobby, path, meta);
            return;
        }
        if (path.size() >= 4 && path[1] == "users") {
            const bool self = path[2] == "me" || path[2] == identity.online_id ||
                              path[2] == std::to_string(identity.account_id);
            if (path[3] == "friendList" && method == "GET" && self) {
                json(request.id, 200, friend_list());
                return;
            }
            if (path.size() == 4 && path[3] == "entitlements" && method == "GET" && self) {
                json(request.id, 200,
                     Json{{"entitlements", Json::array()},
                          {"totalResults", 0},
                          {"size", 0},
                          {"start", 0}});
                return;
            }
            if (path[3] == "sessions" && method == "GET" && self) {
                auto sessions = Json::array();
                for (auto lobby : joined)
                    if (committed(lobby))
                        if (auto doc = load(lobby); doc && alive(lobby, *doc))
                            sessions.push_back(session(lobby, *doc));
                json(request.id, 200,
                     Json{{"sessions", sessions},
                          {"size", sessions.size()},
                          {"totalResults", sessions.size()},
                          {"start", 0}});
                return;
            }
            if (path[3] == "invitations" && self) {
                if (path.size() == 4 && method == "GET") {
                    auto list = Json::array();
                    for (const auto &[key, value] : invitations)
                        if (!value.declined)
                            list.push_back(Json{{"invitationId", key},
                                                {"sessionId", std::to_string(value.lobby)},
                                                {"fromUser", user(value.sender, value.name)},
                                                {"usedFlag", value.accepted}});
                    json(request.id, 200,
                         Json{{"invitations", list},
                              {"size", list.size()},
                              {"totalResults", list.size()},
                              {"start", 0}});
                    return;
                }
                if (path.size() >= 5 && method == "GET") {
                    auto found = invitations.find(path[4]);
                    if (found == invitations.end() || found->second.declined ||
                        !load(found->second.lobby)) {
                        error(request.id, 404);
                        return;
                    }
                    const auto &value = found->second;
                    if (path.size() == 5) {
                        json(request.id, 200,
                             Json{{"invitationId", found->first},
                                  {"sessionId", std::to_string(value.lobby)},
                                  {"fromUser", user(value.sender, value.name)},
                                  {"usedFlag", value.accepted},
                                  {"expired", false}});
                        return;
                    }
                    if (path.size() == 6 && path[5] == "invitationData") {
                        result(request.id, OSW_OK, 200, {}, "application/octet-stream");
                        return;
                    }
                }
                if (path.size() == 5 && method == "PUT") {
                    auto data =
                        Json::parse(request.body.begin(), request.body.end(), nullptr, false);
                    if (!data.is_object() || !data.contains("usedFlag") ||
                        !data["usedFlag"].is_boolean() || !data["usedFlag"].get<bool>()) {
                        error(request.id, 400);
                        return;
                    }
                    accept(std::move(request), path[4]);
                    return;
                }
            }
        }
        error(request.id, 501);
    }
};

Platform::Platform(Config config, Identity identity, Social &social)
    : p(std::make_unique<Impl>(std::move(config), std::move(identity), social)) {}
Platform::~Platform() = default;
bool Platform::start() {
    const std::vector<std::string> values{
        p->config.virtual_endpoints ? "2" : "1", p->config.title, p->config.build, p->config.realm, p->identity.online_id, ""};
    for (size_t i = 0; i < presence_keys.size(); ++i)
        if (!p->social.set_presence(presence_keys[i], values[i])) {
            stop();
            return false;
        }
    p->refresh_friends();
    return true;
}
bool Platform::update_name(const std::string &name) {
    if (p->stopped || !valid_online_id(name))
        return false;
    if (p->identity.online_id == name)
        return true;
    if (!p->social.set_presence("osw.name", name))
        return false;
    p->identity.online_id = name;
    for (auto lobby : p->joined)
        if (p->committed(lobby)) {
            p->social.set_member_data(lobby, "osw.member", name);
            p->session_events(lobby);
        }
    return true;
}
void Platform::submit(Request request) {
    const auto id = request.id;
    if (p->stopped) {
        p->result(id, OSW_SIGNED_OUT);
        return;
    }
    try {
        auto meta = Json::parse(request.metadata, nullptr, false);
        if (request.operation == OSW_FRIENDS) {
            p->json(id, 200, p->friend_list());
            return;
        }
        if (!meta.is_object()) {
            p->result(id, OSW_INVALID_ARGUMENT);
            return;
        }
        switch (request.operation) {
        case OSW_OVERLAY:
            p->overlay(request, meta);
            break;
        case OSW_WEBAPI:
            p->webapi(std::move(request), std::move(meta));
            break;
        case OSW_INVITE_SEND: {
            const auto lobby = parse_decimal_id(text(meta, "session_id"));
            if (!lobby) {
                p->result(id, OSW_INVALID_ARGUMENT);
                break;
            }
            p->send_invite(id, *lobby, meta);
            break;
        }
        case OSW_INVITE_ACCEPT:
            p->accept(std::move(request), text(meta, "invitation_id"));
            break;
        case OSW_INVITE_DECLINE: {
            auto it = p->invitations.find(text(meta, "invitation_id"));
            if (it == p->invitations.end()) {
                p->error(id, 404);
                break;
            }
            it->second.declined = true;
            if (!p->committed(it->second.lobby))
                p->leave(it->second.lobby);
            p->result(id, OSW_OK, 204);
            break;
        }
        case OSW_RESOLVE_NAME: {
            const auto name = text(meta, "online_id");
            if (name == p->identity.online_id) {
                p->json(id, 200, p->user(p->identity.account_id, name));
                break;
            }
            p->refresh_friends();
            const auto it =
                std::find_if(p->known_friends.begin(), p->known_friends.end(),
                             [&](const auto &item) { return item.second.second == name; });
            if (it == p->known_friends.end()) {
                p->error(id, 404);
                break;
            }
            p->json(id, 200, p->user(it->first, it->second.second));
            break;
        }
        default:
            p->result(id, OSW_NOT_SUPPORTED);
            break;
        }
    } catch (const std::exception &) {
        p->result(id, OSW_INVALID_ARGUMENT);
    }
}
void Platform::cancel(Id request) {
    for (auto &[call, pending] : p->calls)
        if (pending.request.id == request)
            pending.canceled = true;
    for (auto &[lobby, list] : p->reads)
        std::erase_if(list, [&](const Request &value) { return value.id == request; });
    std::erase_if(p->reads, [](const auto &item) { return item.second.empty(); });
    if (auto it = p->preparing.find(request); it != p->preparing.end()) {
        if (!p->committed(it->second.lobby))
            p->leave(it->second.lobby);
        p->preparing.erase(it);
    }
    if (auto it = p->completed_mutations.find(request); it != p->completed_mutations.end()) {
        if (!it->second.provisional || !p->committed(it->second.lobby))
            p->leave(it->second.lobby);
        p->completed_mutations.erase(it);
    }
}
void Platform::process(const std::vector<SocialEvent> &events) {
    for (const auto &event : events) {
        if (event.kind == SocialEvent::overlay_changed && (event.detail == 0 || event.detail == 1)) {
            Event value{OSW_OVERLAY_CHANGED};
            value.change_kind = static_cast<std::uint32_t>(event.detail);
            p->update.events.push_back(std::move(value));
            continue;
        }
        if (event.kind == SocialEvent::created || event.kind == SocialEvent::entered) {
            auto it = p->calls.find(event.call);
            if (it == p->calls.end())
                continue;
            auto pending = std::move(it->second);
            p->calls.erase(it);
            if (!event.success || !event.lobby) {
                if (!pending.canceled)
                    p->unavailable(pending.request.id, "lobby_completion", event.detail);
                continue;
            }
            if (pending.canceled || (pending.lobby && pending.lobby != event.lobby)) {
                p->social.leave_lobby(event.lobby);
                continue;
            }
            p->joined.insert(event.lobby);
            if (event.kind == SocialEvent::created) {
                p->publication_stage = "namespace";
                p->publication_detail = 0;
                bool ok =
                    p->social.set_lobby_data(event.lobby, "osw.protocol", p->config.virtual_endpoints ? "2" : "1") &&
                    p->social.set_lobby_data(event.lobby, "osw.title", p->config.title) &&
                    p->social.set_lobby_data(event.lobby, "osw.build", p->config.build) &&
                    p->social.set_lobby_data(event.lobby, "osw.realm", p->config.realm) &&
                    p->save(event.lobby, pending.document) &&
                    p->social.set_friends_visible(
                        event.lobby,
                        text(pending.document["description"], "sessionPrivacy") == "public") &&
                    p->social.set_joinable(event.lobby, !pending.document["description"].value(
                                                            "sessionLockFlag", false));
                if (!ok) {
                    p->leave(event.lobby);
                    p->unavailable(pending.request.id, p->publication_stage, p->publication_detail);
                    continue;
                }
                p->commit(event.lobby);
                p->remember_mutation(pending.request.id, event.lobby);
                p->json(pending.request.id, 200, Json{{"sessionId", std::to_string(event.lobby)}});
            } else {
                auto document = p->load(event.lobby);
                if (!document || !p->alive(event.lobby, *document)) {
                    p->leave(event.lobby);
                    p->error(pending.request.id, 404);
                    continue;
                }
                pending.lobby = event.lobby;
                p->preparing.emplace(pending.request.id, std::move(pending));
            }
        } else if (event.kind == SocialEvent::message) {
            if (!p->joined.contains(event.lobby) || !p->compatible(event.lobby))
                continue;
            const auto users = p->social.members(event.lobby);
            if (std::find(users.begin(), users.end(), event.user) == users.end())
                continue;
            p->blobs.message(event);
        } else if (event.kind == SocialEvent::friends_changed) {
            p->refresh_friends();
            p->notify_friend_changes();
            p->update.events.push_back({OSW_FRIENDS_CHANGED});
        } else if (event.kind == SocialEvent::invitation ||
                   event.kind == SocialEvent::join_requested) {
            if (p->invitations.size() >= 128)
                std::erase_if(p->invitations, [&](const auto &item) {
                    return item.second.declined ||
                           (item.second.accepted && !p->joined.contains(item.second.lobby));
                });
            if (p->incoming.size() >= 128 || p->invitations.size() >= 128)
                continue;
            p->refresh_friends();
            if (!p->known_friends.contains(event.user) ||
                !p->known_friends[event.user].first.online)
                continue;
            auto [it, inserted] = p->incoming.try_emplace(
                event.lobby,
                Impl::Incoming{event.user, false, Clock::now() + std::chrono::seconds(10)});
            it->second.accept |= event.kind == SocialEvent::join_requested;
            if (inserted && !p->social.request_data(event.lobby))
                p->incoming.erase(it);
        } else if (event.kind == SocialEvent::data || event.kind == SocialEvent::member) {
            if (auto it = p->reads.find(event.lobby); it != p->reads.end()) {
                auto requests = std::move(it->second);
                p->reads.erase(it);
                for (auto &request : requests) {
                    if (!event.success || !p->load(event.lobby))
                        p->error(request.id, 404);
                    else
                        submit(std::move(request));
                }
            }
            if (auto it = p->incoming.find(event.lobby); it != p->incoming.end()) {
                const auto incoming = it->second;
                p->incoming.erase(it);
                auto document = p->load(event.lobby);
                if (event.success && document && p->alive(event.lobby, *document) &&
                    p->known_friends.contains(incoming.sender)) {
                    const auto prior = std::find_if(
                        p->invitations.begin(), p->invitations.end(), [&](const auto &item) {
                            return item.second.lobby == event.lobby &&
                                   item.second.sender == incoming.sender && !item.second.declined &&
                                   (!item.second.accepted || p->joined.contains(event.lobby));
                        });
                    const auto key = prior != p->invitations.end()
                                         ? prior->first
                                         : std::to_string(event.lobby) + "-" +
                                               std::to_string(incoming.sender) + "-" +
                                               std::to_string(++p->invitation_sequence);
                    auto [invite, inserted] = p->invitations.try_emplace(
                        key, Impl::Invitation{event.lobby, incoming.sender,
                                              p->known_friends[incoming.sender].second});
                    if (inserted)
                        p->update.events.push_back({OSW_INVITATION_RECEIVED, 0, incoming.sender,
                                                    std::to_string(event.lobby), key,
                                                    invite->second.name});
                    if (incoming.accept && !invite->second.accepted && !invite->second.declined)
                        p->accept(Request{}, key);
                }
            }
            p->session_events(event.lobby);
        }
    }
    for (auto &[call, pending] : p->calls) {
        if (!pending.canceled && Clock::now() >= pending.deadline) {
            pending.canceled = true;
            if (pending.request.id)
                p->result(pending.request.id, OSW_TIMEOUT);
        }
    }
    p->blobs.tick();
    std::vector<Request> resume;
    for (auto it = p->preparing.begin(); it != p->preparing.end();) {
        const auto document = p->load(it->second.lobby);
        const int state = document && p->alive(it->second.lobby, *document)
                              ? p->blobs.ensure(it->second.lobby, (*document)["blobs"])
                              : -1;
        if (!state) {
            ++it;
            continue;
        }
        auto pending = std::move(it->second);
        it = p->preparing.erase(it);
        const auto lobby = pending.lobby;
        if (state < 0) {
            if (!p->committed(lobby))
                p->leave(lobby);
            if (pending.request.id)
                p->result(pending.request.id, OSW_UNAVAILABLE);
            continue;
        }
        if (pending.read) {
            if (!p->committed(lobby))
                p->remember_mutation(pending.request.id, lobby, true);
            resume.push_back(std::move(pending.request));
            continue;
        }
        if (pending.invitation.empty()) {
            if ((*document)["description"].value("sessionLockFlag", false)) {
                p->leave(lobby);
                p->error(pending.request.id, 403);
                continue;
            }
            p->commit(lobby);
        } else {
            auto invite = p->invitations.find(pending.invitation);
            if (invite == p->invitations.end() || invite->second.declined) {
                p->leave(lobby);
                if (pending.request.id)
                    p->error(pending.request.id, 404);
                continue;
            }
            if (!invite->second.accepted) {
                invite->second.accepted = true;
                p->update.events.push_back({OSW_INVITATION_ACCEPTED, 0, invite->second.sender,
                                            std::to_string(lobby), pending.invitation,
                                            invite->second.name});
            }
        }
        if (pending.request.id) {
            p->remember_mutation(pending.request.id, lobby);
            p->result(pending.request.id, OSW_OK, 204);
        }
    }
    for (auto &request : resume)
        submit(std::move(request));
    std::erase_if(p->incoming,
                  [](const auto &item) { return Clock::now() >= item.second.deadline; });
}
Update Platform::take_update() {
    Update value;
    std::swap(value, p->update);
    return value;
}
Party Platform::party() {
    for (auto lobby : p->joined)
        if (p->committed(lobby) && p->compatible(lobby)) {
            Party party{lobby, p->social.owner(lobby)};
            for (auto peer : p->social.members(lobby))
                if (valid_online_id(p->social.member_data(lobby, peer, "osw.member")))
                    party.members.push_back(peer);
            return party;
        }
    return {};
}
void Platform::stop() {
    if (p->stopped)
        return;
    p->stopped = true;
    auto joined = p->joined;
    for (auto lobby : joined)
        p->leave(lobby);
    for (const auto &key : presence_keys)
        p->social.set_presence(key, "");
}
} // namespace osw::core
