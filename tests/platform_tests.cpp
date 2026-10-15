#include "core/blobs.hpp"
#include "core/multipart.hpp"
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
    std::vector<std::pair<Id, std::string>> messages;
    int joins = 0, invites = 0, writes = 0, fail_write = -1;
    std::string fail_presence;
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
    bool invite(Id, Id) override {
        ++invites;
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
    auto returned = Json::parse(body(single(platform)))["friendList"];
    check(returned.size() == 1 && returned[0]["presence"]["onlineStatus"] == "online" &&
              returned[0]["personalDetail"]["displayName"] == "Updated Persona",
          "friend back in game is listed online with refreshed persona");
    platform.stop();
    check(exceptions.quiet(), "persona sanitization raised a host first-chance exception");
    std::cout << "friend display-name cases passed; first-chance exceptions: 0\n";
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
        friend_display_names();
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
        guest_platform.stop();
        owner_platform.stop();
        std::cout << "platform contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
