#include "core/blobs.hpp"
#include "core/local_io.hpp"
#include "core/multipart.hpp"
#include "core/p2p.hpp"
#include "core/platform.hpp"
#include "exception_observer.hpp"
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using namespace osw::core;
using Json = nlohmann::json;
using Id = std::uint64_t;
constexpr Id self = 76561198000000001ull, peer = self + 1, lobby_id = 109775244000000001ull;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
struct Fixture : Social {
    Id local = self;
    std::map<Id, std::map<std::string, std::string>> presence_values, data;
    std::map<Id, std::map<Id, std::map<std::string, std::string>>> member_values;
    std::map<Id, std::vector<Id>> users;
    std::map<Id, Id> owners;
    std::vector<Friend> friend_values;
    std::vector<Id> left, requested;
    std::vector<Id> invited;
    std::vector<std::pair<Id, std::string>> messages;
    int joins = 0, invites = 0, writes = 0, fail_write = -1;
    std::string fail_presence;
    OverlayState overlay_state{};
    int overlay_opens = 0;
    Id overlay_lobby = 0;
    OverlayState overlay_status() override { return overlay_state; }
    bool overlay_friends() override { ++overlay_opens; return overlay_state.available; }
    bool overlay_invite(Id lobby) override {
        ++overlay_opens;
        overlay_lobby = lobby;
        return overlay_state.available;
    }
    Id call = 0;
    std::vector<Friend> friends() override { return friend_values; }
    std::string presence(Id id, const std::string &key) override {
        return presence_values[id][key];
    }
    bool set_presence(const std::string &key, const std::string &value) override {
        if (key == fail_presence && !value.empty())
            return false;
        presence_values[local][key] = value;
        return true;
    }
    Id create_lobby(int) override { return ++call; }
    Id join_lobby(Id) override {
        ++joins;
        return ++call;
    }
    void leave_lobby(Id lobby) override {
        left.push_back(lobby);
        std::erase(users[lobby], local);
    }
    bool invite(Id, Id recipient) override {
        ++invites;
        invited.push_back(recipient);
        return true;
    }
    bool request_data(Id lobby) override {
        requested.push_back(lobby);
        return true;
    }
    std::string lobby_data(Id lobby, const std::string &key) override { return data[lobby][key]; }
    bool set_lobby_data(Id lobby, const std::string &key, const std::string &value) override {
        if (++writes == fail_write)
            return false;
        data[lobby][key] = value;
        return true;
    }
    bool delete_lobby_data(Id lobby, const std::string &key) override {
        data[lobby].erase(key);
        return true;
    }
    Id owner(Id lobby) override { return owners[lobby]; }
    std::vector<Id> members(Id lobby) override { return users[lobby]; }
    std::string member_data(Id lobby, Id id, const std::string &key) override {
        return member_values[lobby][id][key];
    }
    void set_member_data(Id lobby, const std::string &key, const std::string &value) override {
        member_values[lobby][local][key] = value;
    }
    bool set_joinable(Id, bool) override { return true; }
    bool set_limit(Id, int) override { return true; }
    bool set_friends_visible(Id, bool) override { return true; }
    bool send_message(Id lobby, const std::string &bytes) override {
        messages.emplace_back(lobby, bytes);
        return true;
    }
};
void p2p_follow_lifecycle() {
    Config config;
    config.title = "5760";
    config.build = "1.21";
    config.realm = "follow-fixture";
    Fixture social;
    const Id party = lobby_id, first = lobby_id + 1, second = lobby_id + 2;
    for (auto room : {first, second}) {
        social.data[room] = {{"osw_room", "osw/1"},       {"osw_title", config.title},
                             {"osw_build", config.build}, {"osw_realm", config.realm},
                             {"osw_room_protocol", "1"},  {"osw_room_owner", std::to_string(peer)},
                             {"osw_room_metadata", "{}"}};
        social.owners[room] = peer;
        social.users[room] = {peer};
    }
    social.member_values[party][peer]["osw.room"] = std::to_string(first);
    P2P p2p(config, {true, self, "Local"}, social);
    p2p.set_party({party, peer, {self, peer}});
    auto submit = [&](Id id, const char *op, Id room) {
        p2p.submit(
            {id, OSW_P2P, 30000, Json{{"op", op}, {"room_id", std::to_string(room)}}.dump()});
    };
    auto result = [&](Id id) {
        auto update = p2p.poll();
        check(update.results.size() == 1 && update.results.front().id == id,
              "P2P lifecycle returns the requested completion");
        return update.results.front();
    };
    auto enter = [&](Id call, Id room) {
        social.users[room] = {peer, self};
        p2p.process({{SocialEvent::entered, call, room, 0, true}});
    };
    p2p.process({});
    const auto abandoned = social.call;
    check(social.joins == 1, "party selection starts automatic join");
    submit(1, "directory.leave", first);
    check(result(1).status == OSW_UNAVAILABLE, "leave cancels a not-yet-entered selection");
    submit(2, "directory.join", first);
    auto blocked = result(2);
    check(blocked.status == OSW_UNAVAILABLE && Json::parse(blocked.metadata)["error"] == "busy" &&
              social.joins == 1,
          "rejoin cannot overlap the abandoned provider join");
    enter(abandoned, first);
    check(social.users[first] == std::vector<Id>{peer} && social.joins == 1,
          "late automatic completion leaves and does not refollow suppressed selection");
    submit(3, "directory.join", first);
    check(social.joins == 2, "explicit rejoin resumes after abandoned completion cleanup");
    enter(social.call, first);
    check(result(3).status == OSW_OK && social.users[first].size() == 2,
          "fresh explicit join retains membership");
    social.member_values[party][peer]["osw.room"] = std::to_string(second);
    p2p.process({});
    check(social.users[first] == std::vector<Id>{peer} && social.joins == 3,
          "explicitly rejoined follower leaves old selection when leader moves");
    enter(social.call, second);
    check(social.users[second].size() == 2, "follower enters the new selection");
    submit(4, "directory.leave", second);
    check(result(4).status == OSW_OK, "follower explicitly leaves new selection");
    submit(5, "directory.join", second);
    enter(social.call, second);
    check(result(5).status == OSW_OK, "second explicit rejoin succeeds");
    p2p.set_party({});
    p2p.process({});
    check(social.users[second] == std::vector<Id>{peer},
          "party exit leaves an explicitly rejoined followed room");

    Fixture delayed;
    P2P bounded(config, {true, self, "Local"}, delayed);
    auto join_delayed = [&](Id id) {
        const auto room = lobby_id + 100 + id;
        delayed.data[room] = social.data[first];
        delayed.owners[room] = peer;
        delayed.users[room] = {peer};
        bounded.submit({id, OSW_P2P, 30000,
                        Json{{"op", "directory.join"}, {"room_id", std::to_string(room)}}.dump()});
    };
    for (Id id = 1; id <= 128; ++id) {
        join_delayed(id);
        bounded.cancel(id);
    }
    join_delayed(129);
    auto capped = bounded.poll();
    check(delayed.joins == 128 && capped.results.size() == 1 &&
              capped.results[0].status == OSW_UNAVAILABLE &&
              Json::parse(capped.results[0].metadata)["error"] == "busy",
          "missing abandoned callbacks bound admission without forgetting unsafe joins");
    const auto released = lobby_id + 101;
    delayed.users[released] = {peer, self};
    bounded.process({{SocialEvent::entered, 1, released, 0, true}});
    join_delayed(129);
    check(delayed.joins == 129 && delayed.users[released] == std::vector<Id>{peer},
          "late callback cleanup releases bounded admission capacity");
    const auto recovered = lobby_id + 229;
    delayed.users[recovered] = {peer, self};
    bounded.process({{SocialEvent::entered, delayed.call, recovered, 0, true}});
    auto completed = bounded.poll();
    check(completed.results.size() == 1 && completed.results[0].status == OSW_OK,
          "new join succeeds after the abandoned callback arrives");
}
Request request(Id id, const std::string &method, const std::string &path,
                const std::string &body = {}, const std::string &type = "application/json") {
    return {id, OSW_WEBAPI, 30000,
            Json{{"method", method}, {"path", path}, {"content_type", type}}.dump(),
            std::vector<std::uint8_t>(body.begin(), body.end())};
}
std::string part(const std::string &name, const std::string &value) {
    return "--fixture\r\nContent-Disposition: attachment; name=\"" + name +
           "\"\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
           std::to_string(value.size()) + "\r\n\r\n" + value + "\r\n";
}
Request creation(Id id) {
    Json description{{"sessionType", "owner-bind"},
                     {"sessionPrivacy", "private"},
                     {"sessionMaxUser", 8},
                     {"sessionLockFlag", false},
                     {"availablePlatforms", {"PS4"}}};
    auto bytes = part("session-request", description.dump()) +
                 part("session-image", std::string(52096, 'i')) +
                 part("session-data", std::string("\0\1\2\3\4", 5)) +
                 part("changeable-session-data", std::string(10, 'c')) + "--fixture--\r\n";
    return request(id, "POST", "/v1/sessions", bytes, "multipart/mixed; boundary=\"fixture\"");
}
Request length_delimited_creation(Id id) {
    auto description = Json{{"sessionType", "owner-bind"},
                            {"sessionPrivacy", "private"},
                            {"sessionMaxUser", 8},
                            {"sessionLockFlag", false},
                            {"availablePlatforms", {"PS4"}}}
                           .dump();
    description.resize(417, ' ');
    const auto part = [](const std::string &name, const std::string &type,
                         const std::string &value, bool attachment) {
        return "--synthetic-boundary-0123456789abc\r\nContent-Type:" + type +
               "\r\nContent-Description:" + name + "\r\n" +
               (attachment ? "Content-Disposition:attachment\r\n" : "") +
               "Content-Length:" + std::to_string(value.size()) + "\r\n\r\n" + value;
    };
    auto bytes = part("session-request", "application/json; charset=utf-8", description, false) +
                 "\r\n" + part("session-image", "image/jpeg", std::string(52096, 'i'), true) +
                 "\r\n" + part("session-data", "application/octet-stream",
                                std::string("\0\1\2\3\4", 5), true) +
                 "\r\n" + part("changeable-session-data", "application/octet-stream",
                                std::string(10, 'c'), true);
    check(bytes.size() == 53161, "synthetic request matches observed framing and lengths");
    return request(id, "POST", "/v1/sessions", bytes,
                   "multipart/mixed; boundary=\"synthetic-boundary-0123456789abc\"");
}
Result single(Platform &platform) {
    auto update = platform.take_update();
    check(update.results.size() == 1, "exactly one result");
    return update.results[0];
}
std::string body(const Result &result) { return {result.body.begin(), result.body.end()}; }
void create(Platform &platform, Fixture &social, Id id = 1, Id lobby = lobby_id) {
    platform.submit(creation(id));
    check(platform.take_update().results.empty(), "creation waits for callback");
    social.owners[lobby] = self;
    social.users[lobby] = {self};
    platform.process({{SocialEvent::created, social.call, lobby, 0, true}});
    auto result = single(platform);
    check(result.status == OSW_OK && result.http_status == 200, "created");
    check(Json::parse(body(result))["sessionId"] == std::to_string(lobby), "lossless lobby ID");
}
Result transfer(Platform &owner, Fixture &source, Platform &guest, Fixture &destination) {
    for (int i = 0; i < 100; ++i) {
        auto outbound = std::move(destination.messages);
        destination.messages.clear();
        for (const auto &[lobby, bytes] : outbound)
            owner.process({{SocialEvent::message, 0, lobby, peer, true, 0, bytes}});
        auto inbound = std::move(source.messages);
        source.messages.clear();
        for (const auto &[lobby, bytes] : inbound)
            guest.process({{SocialEvent::message, 0, lobby, self, true, 0, bytes}});
        auto update = guest.take_update();
        if (!update.results.empty())
            return update.results[0];
    }
    throw std::runtime_error("payload did not complete within fixture rounds");
}
void overlay_contracts() {
    Config config{"", "LocalName", "5760", "1.21", "overlay-fixture"};
    Fixture social;
    Platform platform(config, {true, self, "LocalName"}, social);
    check(platform.start(), "overlay fixture starts");
    Id next = 100;
    const auto run = [&](Json meta, std::vector<std::uint8_t> bytes = {}) {
        platform.submit({next++, OSW_OVERLAY, 30000, meta.dump(), std::move(bytes)});
        return single(platform);
    };
    check(run({{"action", "status"}}).status == OSW_NOT_SUPPORTED, "non-native overlay is unsupported");
    social.overlay_state = {true, false, false};
    auto result = run({{"action", "status"}});
    check(result.http_status == 200 && Json::parse(body(result)) ==
          Json{{"available", false}, {"active", false}}, "status does not invent availability");
    check(run({{"action", "friends"}}).status == OSW_UNAVAILABLE && !social.overlay_opens,
          "disabled overlay is not dispatched");
    social.overlay_state.available = true;
    check(run({{"action", "friends"}}).http_status == 204 && social.overlay_opens == 1,
          "friend overlay dispatch returns no invitation confirmation");
    check(run({{"action", "invite"}}).status == OSW_NOT_FOUND, "missing party refused");
    create(platform, social);
    const auto id = std::to_string(lobby_id);
    check(run({{"action", "invite"}}).http_status == 204 && social.overlay_lobby == lobby_id,
          "unique committed party selected");
    social.member_values[lobby_id][self].erase("osw.member");
    check(run({{"action", "invite"}, {"session_id", id}}).status == OSW_NOT_FOUND,
          "provisional Steam membership is not an invite target");
    social.member_values[lobby_id][self]["osw.member"] = "LocalName";
    social.users[lobby_id] = {peer};
    check(run({{"action", "invite"}, {"session_id", id}}).status == OSW_NOT_FOUND,
          "stale committed data without actual local membership refused");
    social.users[lobby_id] = {self};
    social.data[lobby_id]["osw_room"] = "osw/1";
    check(run({{"action", "invite"}, {"session_id", id}}).status == OSW_NOT_FOUND,
          "directory room never offered as party");
    social.data[lobby_id].erase("osw_room");
    for (const auto key : {"osw.protocol", "osw.title", "osw.build", "osw.realm"}) {
        const auto saved = social.data[lobby_id][key];
        social.data[lobby_id][key] = "foreign";
        check(run({{"action", "invite"}, {"session_id", id}}).status == OSW_NOT_FOUND,
              "foreign namespace field refused");
        social.data[lobby_id][key] = saved;
    }
    create(platform, social, 2, lobby_id + 1);
    check(run({{"action", "invite"}}).status == OSW_LIMIT, "ambiguous committed parties refused");
    check(run({{"action", "invite"}, {"session_id", id}}).http_status == 204,
          "explicit compatible party remains selectable");
    for (const auto &meta : {Json{{"action", "invite"}, {"session_id", 12}},
                            Json{{"action", "invite"}, {"session_id", ""}},
                            Json{{"action", "status"}, {"session_id", id}},
                            Json{{"action", "friends"}, {"url", "unrequested"}},
                            Json{{"action", 7}}})
        check(run(meta).status == OSW_INVALID_ARGUMENT, "malformed overlay schema refused");
    check(run({{"action", "friends"}}, {1}).status == OSW_INVALID_ARGUMENT, "overlay body refused");
    check(run({{"action", "arbitrary"}}).status == OSW_NOT_SUPPORTED, "unlisted overlay action refused");
    platform.process({{SocialEvent::overlay_changed, 0, 0, 0, true, 1},
                      {SocialEvent::overlay_changed, 0, 0, 0, true, 0}});
    const auto events = platform.take_update().events;
    check(events.size() == 2 && events[0].type == OSW_OVERLAY_CHANGED &&
          events[0].change_kind == 1 && events[1].change_kind == 0,
          "copied overlay lifecycle events contain no fabricated invitation result");
}
void friend_display_names() {
    Config config{"", "LocalName", "5760", "1.21", "normal"};
    Fixture social;
    Platform platform(config, {true, self, "LocalName"}, social);
    check(platform.start(), "friend fixture starts");
    social.friend_values = {{peer, "Display Name", true}, {peer + 1, "Diagnostic", true}};
    social.presence_values[peer] = social.presence_values[self];
    social.presence_values[peer]["osw.name"] = "RemoteName";
    ExceptionObserver exceptions;
    platform.submit(request(1, "GET", "/v1/users/me/friendList?fields=onlineId,region,avatarUrl,"
                                      "personalDetail,personalDetail.displayName,isOfficiallyVerified"));
    auto response = single(platform);
    check(response.http_status == 200, "WebAPI friend list succeeds");
    auto list = Json::parse(body(response))["friendList"];
    check(list.size() == 1, "diagnostic mode defaults off and excludes absent presence");
    check(list[0]["personalDetail"]["displayName"] == "Display Name",
          "known friend exposes persona display name");
    check(list[0]["onlineId"] == "RemoteName" && list[0]["user"]["onlineId"] == "RemoteName",
          "persona does not replace bound online ID");
    check(list[0]["presence"]["onlineStatus"] == "online",
          "Toolkit reads online state from presence.onlineStatus");
    std::string controls;
    for (int c = 0; c < 0x20; ++c)
        controls += static_cast<char>(c);
    controls += '\x7f';
    const std::vector<std::pair<std::string, std::string>> cases{
        {"", ""}, {controls, ""}, {"A" + controls + "B", "AB"},
        {"\xff\x80", ""}, {"A\xff" "B", "AB"}, {"\xc0\xaf", ""},
        {"\xe0\x80\x80", ""}, {"\xed\xa0\x80", ""}, {"\xf0\x80\x80\x80", ""},
        {"\xf4\x90\x80\x80", ""}, {"\xf5\x80\x80\x80", ""}, {"\xe2\x82", ""},
        {"A\xe2" "B\xa1" "C", "ABC"},
        {"\xc3\xa9\xe2\x82\xac\xf0\x9f\x8f\x81", "\xc3\xa9\xe2\x82\xac\xf0\x9f\x8f\x81"},
        {std::string(33, 'a'), std::string(32, 'a')},
        {std::string(30, 'a') + "\xc3\xa9", std::string(30, 'a') + "\xc3\xa9"},
        {std::string(31, 'a') + "\xc3\xa9", std::string(31, 'a')},
        {std::string(29, 'a') + "\xe2\x82\xac", std::string(29, 'a') + "\xe2\x82\xac"},
        {std::string(30, 'a') + "\xe2\x82\xac", std::string(30, 'a')},
        {std::string(28, 'a') + "\xf0\x9f\x8f\x81", std::string(28, 'a') + "\xf0\x9f\x8f\x81"},
        {std::string(29, 'a') + "\xf0\x9f\x8f\x81", std::string(29, 'a')},
        {std::string(31, 'a') + "\xff" "b", std::string(31, 'a') + "b"}};
    for (const auto &[input, expected] : cases) {
        social.friend_values[0].display_name = input;
        platform.submit({2, OSW_FRIENDS});
        response = single(platform);
        check(response.http_status == 200, "malformed persona still produces valid JSON");
        auto entry = Json::parse(body(response))["friendList"][0];
        if (expected.empty())
            check(!entry.contains("personalDetail"), "empty sanitized persona omits personalDetail");
        else
            check(entry["personalDetail"]["displayName"] == expected,
                  "persona preserves valid UTF-8, strips invalid/control bytes and caps at 32 bytes");
    }
    social.presence_values[peer].clear();
    social.friend_values[0].display_name = "Updated Persona";
    platform.submit({3, OSW_FRIENDS});
    check(Json::parse(body(single(platform)))["friendList"].empty(),
          "friend without game presence is hidden");
    social.presence_values[peer] = social.presence_values[self];
    social.presence_values[peer]["osw.name"] = "RemoteName";
    social.friend_values[0].online = false;
    platform.submit({4, OSW_FRIENDS});
    check(Json::parse(body(single(platform)))["friendList"].empty(),
          "cached game presence cannot override provider offline state");
    social.friend_values[0].online = true;
    platform.submit({5, OSW_FRIENDS});
    auto returned = Json::parse(body(single(platform)))["friendList"];
    check(returned.size() == 1 && returned[0]["presence"]["onlineStatus"] == "online" &&
              returned[0]["personalDetail"]["displayName"] == "Updated Persona",
          "friend back in game is listed online with refreshed persona");
    platform.stop();
    check(exceptions.quiet(), "persona sanitization raised a host first-chance exception");
    std::cout << "friend display-name cases passed; first-chance exceptions: 0\n";
}
void friend_presence_events() {
    Config config{"", "LocalName", "5760", "1.21", "normal"};
    Fixture social;
    Platform platform(config, {true, self, "LocalName"}, social);
    check(platform.start(), "friend notification fixture starts before remote user");
    const auto refresh_list = [&] {
        platform.submit({1, OSW_FRIENDS});
        check(single(platform).http_status == 200, "ordinary friend GET succeeds");
    };
    const auto changed = [&] {
        platform.process({{SocialEvent::friends_changed}});
        auto events = platform.take_update().events;
        std::erase_if(events, [](const Event &event) {
            return event.type != OSW_FRIENDS_CHANGED || !event.account_id;
        });
        return events;
    };
    social.friend_values = {{peer, "Remote Persona", true}, {peer + 1, "Foreign", true}};
    social.presence_values[peer] = social.presence_values[self];
    social.presence_values[peer]["osw.name"] = "RemoteName";
    social.presence_values[peer + 1] = social.presence_values[peer];
    social.presence_values[peer + 1]["osw.realm"] = "foreign";
    refresh_list(); // A read before callback delivery must not consume the change.
    auto events = changed();
    check(events.size() == 1 && events[0].account_id == peer &&
              events[0].online_id == "RemoteName" && events[0].change_kind == 1,
          "late compatible friend carries copied identity; foreign namespace excluded");
    refresh_list();
    platform.process({});
    check(platform.take_update().events.empty() && changed().empty(),
          "unchanged lists and polls do not duplicate per-friend notifications");
    social.presence_values[peer]["osw.name"] = "Renamed";
    refresh_list();
    events = changed();
    check(events.size() == 1 && events[0].account_id == peer &&
              events[0].online_id == "Renamed" && events[0].change_kind == 3,
          "rename emits metadata change after an intervening GET");
    social.friend_values[0].online = false;
    refresh_list();
    events = changed();
    check(events.size() == 1 && events[0].change_kind == 2 && events[0].online_id == "Renamed",
          "offline friend cannot remain eligible through stale matching presence");
    social.friend_values[0].online = true;
    refresh_list();
    events = changed();
    check(events.size() == 1 && events[0].change_kind == 1 && events[0].online_id == "Renamed",
          "provider online transition restores the current compatible friend");
    social.presence_values[peer].clear();
    refresh_list();
    events = changed();
    check(events.size() == 1 && events[0].account_id == peer &&
              events[0].online_id == "Renamed" && events[0].change_kind == 2,
          "leaving game carries last notified name");
    social.presence_values[peer] = social.presence_values[self];
    social.presence_values[peer]["osw.name"] = "Returned";
    events = changed();
    check(events.size() == 1 && events[0].change_kind == 1 && events[0].online_id == "Returned",
          "same account reappears after restart");
    social.friend_values.clear();
    refresh_list();
    events = changed();
    check(events.size() == 1 && events[0].change_kind == 2 && events[0].online_id == "Returned",
          "removed friend retains its last copied label");
}
void p2p_advertisement_eligibility() {
    struct PeerFixture : Fixture {
        std::vector<PeerMessage> incoming;
        bool send_ok = true;
        bool send_peer(Id, int, const std::string &) override { return send_ok; }
        std::vector<PeerMessage> take_peer_messages() override {
            std::vector<PeerMessage> result;
            result.swap(incoming);
            return result;
        }
    } social;
    Config config{"", "LocalName", "5760", "1.21", "grant-fixture"};
    config.game_bind = "127.0.0.124";
    UdpSocket game(config.game_bind);
    config.game_port = game.port();
    social.friend_values = {{peer, "Remote", true}};
    social.presence_values[peer] = {{"osw.protocol", "1"}, {"osw.title", config.title},
                                  {"osw.build", config.build}, {"osw.realm", config.realm}};
    P2P p2p(config, {true, self, "LocalName"}, social);
    p2p.process({});
    const auto set_ad = [&](const char *payload) {
        p2p.submit({1, OSW_P2P, 30000, Json{{"op", "advertisement.set"},
                     {"context", "fixture"}, {"payload", payload}}.dump()});
        auto update = p2p.poll();
        check(update.results.size() == 1 && update.results[0].status == OSW_OK,
              "fixture advertisement update succeeds");
    };
    const auto query = [&] {
        social.incoming.push_back({peer, 49, Json{{"v", 1}, {"op", "query"}, {"id", "1"},
            {"context", "fixture"}, {"protocol", "osw/1"}, {"title", config.title},
            {"build", config.build}, {"realm", config.realm}}.dump()});
        p2p.poll();
    };
    const std::string datagram = std::string("OSW1") + std::string(8, '\0') + "fixture";
    const auto sent_count = [&] {
        p2p.submit({2, OSW_P2P, 30000, "{\"op\":\"status\"}"});
        return Json::parse(p2p.poll().results[0].metadata)["proxy_sent"].get<Id>();
    };
    const auto datagram_delivered = [&] {
        auto before = sent_count();
        social.incoming.push_back({peer, 48, datagram});
        p2p.poll();
        return sent_count() > before;
    };
    set_ad("YWQ=");
    query();
    check(datagram_delivered(), "a served live ad authorizes initial inbound UDP");
    social.friend_values[0].online = false; // Preserve stale namespace presence deliberately.
    p2p.process({{SocialEvent::friends_changed}});
    check(!datagram_delivered(), "offline friend immediately loses advertisement grant");
    social.friend_values[0].online = true;
    p2p.process({{SocialEvent::friends_changed}});
    check(!datagram_delivered(), "returning online cannot resurrect the old grant");
    social.send_ok = false;
    query();
    check(!datagram_delivered(), "failed advertisement reply does not authorize UDP");
    social.send_ok = true;
    query();
    check(datagram_delivered(), "fresh successful read restores eligible friend's grant");
    auto before = sent_count();
    social.incoming.push_back({peer, 48, datagram});
    set_ad(""); // Withdrawal precedes an already queued carrier datagram.
    check(sent_count() == before, "withdrawal rejects an in-flight datagram before forwarding");
}
void exact_invitation_recipient() {
    Config config{"", "LocalName", "5760", "1.21", "normal"};
    Fixture social;
    Platform platform(config, {true, self, "LocalName"}, social);
    check(platform.start(), "recipient binding fixture starts");
    social.friend_values = {{101, "Numeric alias", true}, {202, "Selected account", true}};
    for (auto id : {101, 202}) social.presence_values[id] = social.presence_values[self];
    social.presence_values[101]["osw.name"] = "202";
    social.presence_values[202]["osw.name"] = "Selected";
    create(platform, social);
    const auto send = [&](const char *recipient) {
        platform.submit({2, OSW_INVITE_SEND, 30000,
                         Json{{"session_id", std::to_string(lobby_id)}, {"to", {recipient}}}.dump()});
        return single(platform);
    };
    check(send("202").http_status == 204 && social.invited.back() == 202,
          "canonical account outranks another friend's numeric name");
    social.presence_values[101]["osw.name"] = "303";
    check(send("303").http_status == 204 && social.invited.back() == 101,
          "numeric online name still resolves when no account matches");
    social.presence_values[101]["osw.name"] = "202";
    social.presence_values[202].clear();
    const auto before = social.invites;
    check(send("202").http_status == 404 && social.invites == before,
          "an offline exact account cannot redirect to another user's name");
}
void diagnostic_friends() {
    Config config{"", "LocalName", "5760", "1.21", "normal"};
    config.diag_all_friends = true;
    Fixture social;
    Platform platform(config, {true, self, "LocalName"}, social);
    check(platform.start(), "diagnostic fixture starts");
    const Id diagnostic_id = peer + 1;
    const auto diagnostic_name = "s" + std::to_string(static_cast<std::uint32_t>(diagnostic_id));
    social.friend_values = {{diagnostic_id, "Diagnostic\xff\n Persona", true},
                            {peer, "Known Persona", true}};
    social.presence_values[peer] = social.presence_values[self];
    social.presence_values[peer]["osw.name"] = "RemoteName";
    ExceptionObserver exceptions;
    const auto list_friends = [&] {
        platform.submit({1, OSW_FRIENDS});
        auto response = single(platform);
        check(response.http_status == 200, "diagnostic list succeeds");
        auto value = Json::parse(body(response));
        check(value["size"] == value["friendList"].size() && value["totalResults"] == value["size"],
              "diagnostic friend counts are consistent");
        return value["friendList"];
    };
    auto list = list_friends();
    check(list.size() == 2, "diagnostic friend without presence is listed");
    auto entry = list[1];
    check(entry["onlineId"] == diagnostic_name && entry["user"]["onlineId"] == diagnostic_name &&
              diagnostic_name.size() <= 11 && entry["accountId"] == std::to_string(diagnostic_id) &&
              entry["user"]["accountId"] == std::to_string(diagnostic_id),
          "diagnostic alias uses low 32 bits and account IDs stay lossless");
    check(entry["personalDetail"]["displayName"] == "Diagnostic Persona" &&
              entry["onlineStatus"] == "online",
          "diagnostic entry uses sanitized persona and Steam online state");
    social.friend_values[0].online = false;
    check(list_friends()[1]["onlineStatus"] == "offline", "diagnostic Steam offline state retained");
    social.friend_values[0].online = true;
    social.friend_values[0].display_name.clear();
    check(!list_friends()[1].contains("personalDetail"), "empty diagnostic persona omitted");
    create(platform, social);
    const auto base = "/v1/sessions/" + std::to_string(lobby_id);
    for (const auto &target : {std::to_string(diagnostic_id), diagnostic_name}) {
        Json metadata{{"session_id", std::to_string(lobby_id)}, {"to", {target}}};
        platform.submit({2, OSW_INVITE_SEND, 30000, metadata.dump()});
        check(single(platform).http_status == 404 && social.invites == 0,
              "diagnostic invite by ID or alias is rejected without native invitation");
        platform.submit(request(3, "POST", base + "/invitations", metadata.dump()));
        check(single(platform).http_status == 404 && social.invites == 0,
              "WebAPI cannot invite a diagnostic friend");
    }
    platform.submit({4, OSW_INVITE_SEND, 30000,
                     Json{{"session_id", std::to_string(lobby_id)},
                          {"to", {"RemoteName", diagnostic_name}}}.dump()});
    check(single(platform).http_status == 404 && social.invites == 0,
          "mixed invite validates every recipient before sending any invitation");
    platform.submit({5, OSW_RESOLVE_NAME, 30000, Json{{"online_id", diagnostic_name}}.dump()});
    check(single(platform).http_status == 404, "diagnostic alias never enters name resolution");
    social.presence_values[peer]["osw.name"] = diagnostic_name;
    list = list_friends();
    check(list.size() == 1 && list[0]["accountId"] == std::to_string(peer) &&
              list[0]["personalDetail"]["displayName"] == "Known Persona",
          "known friend wins diagnostic name collision regardless of enumeration order");
    social.presence_values[peer].clear();
    list = list_friends();
    check(list.empty(), "cached known friend out of game stays hidden and keeps its name");
    social.friend_values.pop_back();
    check(list_friends().size() == 1, "removed known friend no longer hides diagnostic entry");
    platform.submit({6, OSW_RESOLVE_NAME, 30000, Json{{"online_id", diagnostic_name}}.dump()});
    check(single(platform).http_status == 404, "refresh does not cache diagnostic name");
    social.friend_values.clear();
    check(list_friends().empty(), "diagnostic list does not retain removed friends");
    platform.stop();
    check(exceptions.quiet(), "diagnostic friend operations raised a host first-chance exception");
    std::cout << "diagnostic friend isolation passed; native invitations: 0; first-chance exceptions: 0\n";
}
int main() {
    try {
        overlay_contracts();
        p2p_follow_lifecycle();
        friend_display_names();
        friend_presence_events();
        p2p_advertisement_eligibility();
        exact_invitation_recipient();
        diagnostic_friends();
        Config config{"", "LocalName", "5760", "1.21", "normal"};
        Identity identity{true, self, "LocalName"};
        {
            Fixture invalid_social;
            Platform invalid_platform(config, identity, invalid_social);
            invalid_platform.start();
            ExceptionObserver exceptions;
            for (const auto &path : {"/v1/sessions/%", "/v1/sessions/%00", "/v1/sessions/01",
                                     "/v1/sessions/18446744073709551616"}) {
                invalid_platform.submit(request(1, "GET", path));
                check(single(invalid_platform).status == OSW_INVALID_ARGUMENT,
                      "invalid path has an explicit failure");
            }
            auto truncated = length_delimited_creation(2);
            truncated.body.pop_back();
            invalid_platform.submit(std::move(truncated));
            check(single(invalid_platform).status == OSW_INVALID_ARGUMENT,
                  "truncated multipart has an explicit failure");
            invalid_social.friend_values = {{peer, "Display", true}};
            invalid_social.presence_values[peer] = invalid_social.presence_values[self];
            invalid_social.presence_values[peer]["osw.name"] = "bad\x01" "name";
            invalid_platform.submit({3, OSW_FRIENDS});
            check(Json::parse(body(single(invalid_platform)))["friendList"].empty(),
                  "invalid remote online ID excluded");
            invalid_platform.stop();
            Fixture unavailable_social;
            unavailable_social.fail_presence = "osw.realm";
            Platform unavailable_platform(config, identity, unavailable_social);
            check(!unavailable_platform.start(), "failed presence publication rejects startup");
            check(unavailable_social.presence_values[self]["osw.protocol"].empty(),
                  "failed startup clears partial presence");
            check(exceptions.quiet(), "expected input errors raised a host first-chance exception");
        }
        Fixture social;
        Platform platform(config, identity, social);
        platform.start();
        social.friend_values = {{peer, "Display Name", true}, {peer + 1, "Foreign", true}};
        social.presence_values[peer] = social.presence_values[self];
        social.presence_values[peer]["osw.name"] = "RemoteName";
        platform.submit({1, OSW_FRIENDS});
        auto friends = Json::parse(body(single(platform)));
        check(friends["friendList"].size() == 1, "foreign namespace excluded");
        check(friends["friendList"][0]["user"]["accountId"] == std::to_string(peer),
              "lossless friend identity");
        create(platform, social);
        const auto base = "/v1/sessions/" + std::to_string(lobby_id);
        platform.submit(request(2, "GET", base + "/sessionData"));
        check(body(single(platform)) == std::string("\0\1\2\3\4", 5),
              "binary session data round trip");
        platform.submit(request(3, "GET", base + "/sessionImage"));
        check(body(single(platform)) == std::string(52096, 'i'), "captured-size image round trip");
        platform.submit(request(4, "GET", base));
        auto session = Json::parse(body(single(platform)));
        check(session["members"].size() == 1, "creator committed");
        const auto original = social.data[lobby_id]["osw.document"];
        check(original.size() < 6000, "captured image kept outside small metadata budget");
        social.fail_write = social.writes + 1;
        platform.submit(request(5, "PUT", base + "/changeableSessionData", std::string(10, 'n'),
                                "application/octet-stream"));
        check(single(platform).status == OSW_UNAVAILABLE, "failed publication reports failure");
        check(social.data[lobby_id]["osw.document"] == original,
              "failed publication retains prior document");
        platform.submit(request(6, "GET", base + "/changeableSessionData"));
        check(body(single(platform)) == std::string(10, 'c'),
              "failed publication preserves prior data");
        social.fail_write = -1;
        platform.submit(request(7, "PUT", base + "/changeableSessionData", std::string(10, 'n'),
                                "application/octet-stream"));
        check(single(platform).http_status == 204, "update accepted");
        platform.submit(request(8, "GET", base + "/changeableSessionData"));
        check(body(single(platform)) == std::string(10, 'n'), "updated binary round trip");
        platform.submit(creation(9));
        platform.cancel(9);
        social.owners[lobby_id + 1] = self;
        social.users[lobby_id + 1] = {self};
        platform.process({{SocialEvent::created, social.call, lobby_id + 1, 0, true}});
        check(platform.take_update().results.empty() && social.left.back() == lobby_id + 1,
              "late canceled creation leaves lobby");
        platform.process({{SocialEvent::invitation, 0, lobby_id + 2, peer, true}});
        platform.process({{SocialEvent::data, 0, lobby_id + 2, lobby_id + 2, true}});
        check(platform.take_update().events.empty() && social.joins == 0,
              "unrelated invitation ignored without joining");
        social.data[lobby_id + 3] = social.data[lobby_id];
        social.owners[lobby_id + 3] = self;
        auto invitation_document = Json::parse(social.data[lobby_id + 3]["osw.document"]);
        invitation_document["blobs"] = Json::object();
        social.data[lobby_id + 3]["osw.document"] = invitation_document.dump();
        social.users[lobby_id + 3] = {self, peer};
        social.member_values[lobby_id + 3][peer]["osw.member"] = "RemoteName";
        platform.process({{SocialEvent::invitation, 0, lobby_id + 3, peer, true}});
        platform.process({{SocialEvent::data, 0, lobby_id + 3, lobby_id + 3, true}});
        auto received = platform.take_update();
        check(received.events.size() == 1, "one invitation event");
        auto invitation = received.events[0].invitation_id;
        platform.submit({10, OSW_INVITE_ACCEPT, 30000, Json{{"invitation_id", invitation}}.dump()});
        check(platform.take_update().results.empty(), "accept waits for join result");
        platform.process({{SocialEvent::entered, social.call, lobby_id + 3, 0, true}});
        auto accepted = platform.take_update();
        check(accepted.results.size() == 1 && accepted.events.size() == 1,
              "accept completes and signals once");
        check(social.member_values[lobby_id + 3][self]["osw.member"].empty(),
              "Steam join is not game membership");
        platform.submit(
            request(11, "POST", "/v1/sessions/" + std::to_string(lobby_id + 3) + "/members", "{}"));
        check(single(platform).http_status == 204 &&
                  social.member_values[lobby_id + 3][self]["osw.member"] == "LocalName",
              "guest commits membership");
        platform.submit({12, OSW_INVITE_ACCEPT, 30000, Json{{"invitation_id", invitation}}.dump()});
        auto duplicate = platform.take_update();
        check(duplicate.events.empty() && duplicate.results.size() == 1, "accept deduplicated");
        social.users[lobby_id] = {peer};
        platform.submit(request(13, "GET", base));
        check(single(platform).http_status == 404, "owner-bind ends on owner departure");
        platform.stop();
        check(social.presence_values[self]["osw.protocol"].empty(), "stop removes presence");
        const std::string binary = "a\r\n--fixture\r\nb";
        auto parsed = multipart("multipart/mixed; boundary=fixture",
                                part("binary", binary) + "--fixture--\r\n");
        check(parsed && parsed->at("binary").data == binary,
              "multipart respects declared binary length");
        check(!multipart("multipart/mixed; boundary=fixture",
                         part("binary", "x") + part("binary", "y") + "--fixture--\r\n"),
              "duplicate multipart name rejected");
        const auto length_delimited = length_delimited_creation(14);
        const auto type = Json::parse(length_delimited.metadata)["content_type"].get<std::string>();
        const std::string payload_bytes(length_delimited.body.begin(), length_delimited.body.end());
        const auto length_parts = multipart(type, payload_bytes);
        check(length_parts && length_parts->size() == 4 &&
                  length_parts->at("session-request").data.size() == 417 &&
                  length_parts->at("session-image").data == std::string(52096, 'i') &&
                  length_parts->at("session-data").data == std::string("\0\1\2\3\4", 5) &&
                  length_parts->at("changeable-session-data").data == std::string(10, 'c'),
              "declared final part ending at EOF preserves all binary parts");
        const auto rejects_multipart = [](const std::string &type, const std::string &bytes) {
            return !multipart(type, bytes);
        };
        check(rejects_multipart(type, payload_bytes.substr(0, payload_bytes.size() - 1)),
              "truncated final declared part rejected");
        check(rejects_multipart(type, payload_bytes + "x"),
              "extra bytes after final declared part rejected");
        check(rejects_multipart(type, payload_bytes + "\r\n--synthetic"),
              "partial closing delimiter rejected");
        check(rejects_multipart("multipart/mixed; boundary=fixture",
                                "--fixture\r\nContent-Description:binary\r\n\r\nx"),
              "EOF without a final declared length rejected");
        Fixture length_social;
        Platform length_platform(config, identity, length_social);
        length_platform.start();
        length_platform.submit(length_delimited);
        check(length_platform.take_update().results.empty() && length_social.call == 1,
              "length-delimited creation reaches lobby creation");
        length_social.owners[lobby_id] = self;
        length_social.users[lobby_id] = {self};
        length_platform.process({{SocialEvent::created, length_social.call, lobby_id, 0, true}});
        check(single(length_platform).http_status == 200, "length-delimited creation completes");
        length_platform.submit(request(15, "GET", base + "/sessionData"));
        check(body(single(length_platform)) == std::string("\0\1\2\3\4", 5),
              "length-delimited session preserves opaque data");
        length_platform.stop();
        Fixture sender, receiver;
        sender.owners[lobby_id] = receiver.owners[lobby_id] = self;
        Blobs source(sender, self), destination(receiver, peer);
        const std::string payload(104192, 'a');
        check(source.install(lobby_id, "session-image", payload), "blob installed");
        const Json references{
            {"session-image", {{"size", payload.size()}, {"hash", Blobs::checksum(payload)}}}};
        check(destination.ensure(lobby_id, references) == 0, "remote blob starts pending");
        const auto first_request = Json::parse(receiver.messages.front().second);
        Json forged{{"osw", "blob/1"},
                    {"kind", "data"},
                    {"key", "session-image"},
                    {"hash", Blobs::checksum(payload)},
                    {"nonce", first_request["nonce"]},
                    {"offset", 0},
                    {"to", std::to_string(peer)},
                    {"bytes", std::string(2000, 'b')}};
        destination.message({SocialEvent::message, 0, lobby_id, peer + 1, true, 0, forged.dump()});
        check(destination.get(lobby_id, "session-image").empty(),
              "unrelated sender cannot inject payload");
        int rounds = 0;
        while (destination.ensure(lobby_id, references) == 0 && ++rounds < 100) {
            auto requests = std::move(receiver.messages);
            receiver.messages.clear();
            for (const auto &[lobby, bytes] : requests)
                source.message({SocialEvent::message, 0, lobby, peer, true, 0, bytes});
            auto responses = std::move(sender.messages);
            sender.messages.clear();
            for (const auto &[lobby, bytes] : responses) {
                check(bytes.size() <= 4096, "blob message fits native limit");
                destination.message({SocialEvent::message, 0, lobby, self, true, 0, bytes});
                destination.message({SocialEvent::message, 0, lobby, self, true, 0, bytes});
            }
        }
        check(rounds < 100 && destination.get(lobby_id, "session-image") == payload,
              "captured image transfers intact with duplicate delivery");
        destination.erase(lobby_id);
        check(destination.get(lobby_id, "session-image").empty(), "leave releases blob cache");
        Fixture owner_social, guest_social;
        guest_social.local = peer;
        Platform owner_platform(config, identity, owner_social);
        Platform guest_platform(config, {true, peer, "RemoteName"}, guest_social);
        owner_platform.start();
        guest_platform.start();
        create(owner_platform, owner_social);
        owner_social.users[lobby_id] = {self, peer};
        guest_social.data = owner_social.data;
        guest_social.owners = owner_social.owners;
        guest_social.users = owner_social.users;
        guest_social.member_values = owner_social.member_values;
        guest_platform.submit(request(50, "GET", base + "/sessionImage"));
        check(guest_platform.take_update().results.empty(), "remote image requires prepared join");
        guest_platform.process({{SocialEvent::entered, guest_social.call, lobby_id, 0, true}});
        auto downloaded = transfer(owner_platform, owner_social, guest_platform, guest_social);
        check(downloaded.http_status == 200 && body(downloaded) == std::string(52096, 'i'),
              "two-platform fixture transfers complete image");
        check(guest_social.member_values[lobby_id][peer]["osw.member"].empty(),
              "payload preparation does not commit guest");
        check(guest_platform.update_name("Provisional") &&
                  guest_social.presence_values[peer]["osw.name"] == "Provisional" &&
                  guest_social.member_values[lobby_id][peer]["osw.member"].empty() &&
                  guest_platform.party().members.empty(),
              "rename during prepared read never commits provisional membership");
        check(guest_platform.update_name("RemoteName"), "provisional name can change again");
        guest_platform.cancel(50);
        check(guest_social.left.size() == 1 && guest_social.left.back() == lobby_id,
              "late canceled read leaves provisional lobby");
        guest_social.users[lobby_id] = {self, peer};
        guest_platform.submit(request(52, "GET", base + "/sessionImage"));
        guest_platform.process({{SocialEvent::entered, guest_social.call, lobby_id, 0, true}});
        auto outgoing = std::move(guest_social.messages);
        guest_social.messages.clear();
        for (const auto &[lobby, bytes] : outgoing)
            owner_platform.process({{SocialEvent::message, 0, lobby, peer, true, 0, bytes}});
        check(!owner_social.messages.empty(), "owner has a pending payload response");
        guest_platform.cancel(52);
        for (const auto &[lobby, bytes] : owner_social.messages)
            guest_platform.process({{SocialEvent::message, 0, lobby, self, true, 0, bytes}});
        owner_social.messages.clear();
        check(guest_social.left.size() == 2 && guest_platform.take_update().results.empty() &&
                  guest_social.messages.empty(),
              "canceled preparation leaves lobby and ignores late payloads");
        guest_social.users[lobby_id] = {self, peer};
        guest_platform.submit(request(53, "GET", base + "/sessionImage"));
        guest_platform.process({{SocialEvent::entered, guest_social.call, lobby_id, 0, true}});
        downloaded = transfer(owner_platform, owner_social, guest_platform, guest_social);
        check(downloaded.http_status == 200 && body(downloaded) == std::string(52096, 'i'),
              "fresh request recovers after canceled preparation");
        guest_platform.submit(request(51, "POST", base + "/members", "{}"));
        check(single(guest_platform).http_status == 204,
              "guest explicitly commits after payload preparation");
        guest_platform.cancel(53);
        check(guest_social.left.size() == 2 &&
                  guest_social.member_values[lobby_id][peer]["osw.member"] == "RemoteName",
              "late canceled read preserves subsequent committed membership");
        owner_social.member_values[lobby_id] = guest_social.member_values[lobby_id];
        owner_platform.process({{SocialEvent::data, 0, lobby_id, peer, true}});
        auto notifications = owner_platform.take_update();
        check(std::any_of(notifications.events.begin(), notifications.events.end(),
                          [](const Event &event) {
                              return event.type == OSW_SESSION_CHANGED && event.change_kind == 1 &&
                                     event.account_id == peer;
                          }),
              "owner receives committed guest membership event");
        check(guest_platform.update_name("RenamedGuest"), "committed guest rename");
        owner_social.member_values[lobby_id] = guest_social.member_values[lobby_id];
        owner_platform.process({{SocialEvent::data, 0, lobby_id, peer, true}});
        notifications = owner_platform.take_update();
        check(std::any_of(notifications.events.begin(), notifications.events.end(),
                          [](const Event &event) { return event.type == OSW_SESSION_CHANGED &&
                              event.change_kind == 3 && event.account_id == peer &&
                              event.online_id == "RenamedGuest"; }) &&
                  std::none_of(notifications.events.begin(), notifications.events.end(),
                          [](const Event &event) { return event.type == OSW_SESSION_CHANGED &&
                              (event.change_kind == 1 || event.change_kind == 2); }),
              "peer rename emits update without fake join or leave");
        check(!guest_platform.update_name("bad\nname") &&
                  guest_social.member_values[lobby_id][peer]["osw.member"] == "RenamedGuest",
              "invalid name preserves committed identity");
        guest_platform.stop();
        owner_platform.stop();
        std::cout << "platform contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
