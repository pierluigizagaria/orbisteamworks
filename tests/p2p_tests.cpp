#include "core/credential.hpp"
#include "core/local_io.hpp"
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
template <std::size_t N> void copy(char (&out)[N], const std::string &in) {
    check(in.size() < N, "test string too long");
    std::memcpy(out, in.data(), in.size());
    out[in.size()] = 0;
}
struct Client {
    osw_context *context = nullptr;
    Client(const std::string &root, std::uint64_t id, const std::string &bind,
           const std::string &companion = {}, const std::string &environment = {}) {
        osw_config_v2 config{};
        config.base.size = sizeof(config);
        config.base.abi_version = OSW_ABI_VERSION_2;
        copy(config.base.title, "5760");
        copy(config.base.build, "01.21");
        copy(config.base.realm, std::getenv("OSW_LOOPBACK_TEST") ? "p2ploopback13" : "p2ptest");
        copy(config.base.online_id, id == 100001 ? "LabOne" : "LabTwo");
        config.backend = 1;
        config.game_port = 3074;
        config.lab_account_id = id;
        copy(config.lab_root, root);
        copy(config.game_bind, bind);
        copy(config.companion_path, companion);
        copy(config.companion_environment, environment);
        check(osw_create_v2(&config, &context) == OSW_OK, "v2 create failed");
        if (std::getenv("OSW_LOOPBACK_TEST"))
            check(osw_enable_loopback_endpoints(context) == OSW_OK, "loopback enable");
        check(osw_start(context) == OSW_OK, "v2 start failed");
        auto end = Clock::now() + std::chrono::seconds(5);
        for (;;) {
            osw_identity identity{sizeof(identity)};
            osw_get_identity(context, &identity);
            if (identity.signed_in)
                break;
            check(Clock::now() < end, "lab identity unavailable");
            pause();
        }
    }
    ~Client() { osw_destroy(context); }
    Json call(Json metadata, unsigned operation = OSW_P2P, const std::string &body = {}) {
        return call_raw(metadata.dump(), operation, body);
    }
    Json call_raw(const std::string &text, unsigned operation = OSW_P2P,
                  const std::string &body = {}) {
        osw_request request{sizeof(request), operation,   5000,
                            text.data(),     text.size(), (const std::uint8_t *)body.data(),
                            body.size()};
        std::uint64_t id = 0;
        check(osw_submit(context, &request, &id) == OSW_OK, "submit failed");
        osw_result result{sizeof(result)};
        auto end = Clock::now() + std::chrono::seconds(6);
        while (osw_get_result(context, id, &result) == OSW_PENDING) {
            check(Clock::now() < end, "result deadline");
            pause();
        }
        Json answer{
            {"status", result.status},
            {"http", result.http_status},
            {"body", osw::core::base64(std::string((const char *)result.body, result.body_size))}};
        answer["result"] =
            result.metadata_size
                ? Json::parse(result.metadata, result.metadata + result.metadata_size)
                : Json::object();
        osw_release(context, id);
        return answer;
    }
};
Json child_result(osw::core::Child &child) {
    auto end = Clock::now() + std::chrono::seconds(8);
    for (;;) {
        auto lines = child.poll();
        if (!lines.empty())
            return Json::parse(lines.front());
        check(child.alive() && Clock::now() < end, "child response deadline");
        pause();
    }
}
Json remote(osw::core::Child &child, Json request) {
    check(child.send(request.dump()), "child send failed");
    return child_result(child);
}
Json receive(osw::core::UdpSocket &socket) {
    auto end = Clock::now() + std::chrono::seconds(3);
    std::string bytes, address;
    std::uint16_t port = 0;
    while (!socket.receive(bytes, address, port)) {
        check(Clock::now() < end, "UDP receive deadline");
        pause();
    }
    return {{"payload", osw::core::base64(bytes)}, {"address", address}, {"port", port}};
}
int peer_role() {
    Client client(std::getenv("OSW_TEST_ROOT"), 100002, "127.0.0.122");
    osw::core::UdpSocket game("127.0.0.122", 3074);
    std::cout << Json{{"ready", true}}.dump() << std::endl;
    std::string line;
    while (std::getline(std::cin, line)) {
        auto command = Json::parse(line);
        Json answer;
        if (command.value("test", std::string{}) == "udp.recv") {
            try {
                answer = receive(game);
            } catch (...) {
                answer = client.call({{"op", "status"}});
            }
        } else if (command.value("test", std::string{}) == "udp.empty") {
            std::string bytes, address;
            std::uint16_t port = 0;
            bool empty = true;
            const auto until = Clock::now() + std::chrono::milliseconds(200);
            while (Clock::now() < until && empty) {
                empty = !game.receive(bytes, address, port);
                pause();
            }
            answer = {{"empty", empty}};
        } else if (command.value("test", std::string{}) == "udp.send") {
            std::string bytes;
            check(osw::core::unbase64(command["payload"], bytes, 65507), "decode");
            answer = {{"sent", game.send(bytes, command["address"], command["port"])}};
        } else if (command.value("test", std::string{}) == "event") {
            auto end = Clock::now() + std::chrono::seconds(5);
            osw_event event{sizeof(event)};
            while (Clock::now() < end) {
                if (osw_poll_event(client.context, &event) == OSW_OK &&
                    event.type == command["type"].get<unsigned>()) {
                    answer = {{"type", event.type},
                              {"session_id", event.session_id},
                              {"invitation_id", event.invitation_id}};
                    break;
                }
                pause();
            }
        } else {
            std::string body;
            osw::core::unbase64(command.value("body", std::string{}), body, 1024 * 1024);
            answer = client.call(command["metadata"], command.value("operation", (unsigned)OSW_P2P),
                                 body);
        }
        std::cout << answer.dump() << std::endl;
    }
    return 0;
}
int companion_role() {
    std::string line;
    check((bool)std::getline(std::cin, line), "owner missing");
    auto owner = Json::parse(line);
    check(owner["event"] == "owner" && owner["backend"] == "lab", "owner format");
    auto token = osw::core::parse_token(owner["token"].get<std::string>());
    check(std::to_string(token.user) == owner["steam_id"].get<std::string>() &&
              token.bytes.size() == 32,
          "owner capability");
    std::cout << Json{{"v",1},{"event","ready"},{"capabilities",Json::array({"loopback.endpoints.v1"})}}.dump() << std::endl;
    while (std::getline(std::cin, line)) {
    }
    std::ofstream(std::getenv("OSW_TEST_DONE")) << "EOF";
    return 0;
}
std::string part(const std::string &name, const std::string &value) {
    return "--fixture\r\nContent-Disposition: attachment; name=\"" + name +
           "\"\r\nContent-Type: application/octet-stream\r\nContent-Length: " +
           std::to_string(value.size()) + "\r\n\r\n" + value + "\r\n";
}
int main(int argc, char **argv) {
    try {
        auto role = std::getenv("OSW_TEST_ROLE");
        if (role && std::string(role) == "peer")
            return peer_role();
        if (role && std::string(role) == "companion")
            return companion_role();
        if (role && std::string(role) == "blocked") {
            std::this_thread::sleep_for(std::chrono::seconds(10));
            return 0;
        }
        check(argc >= 1, "executable missing");
        auto exe = std::filesystem::absolute(argv[0]).string();
        auto root = std::filesystem::current_path() /
                    ("p2p-test-" + osw::core::hex(osw::core::random_secret()).substr(0, 12));
        std::filesystem::create_directories(root);
        {
            Client local(root.string(), 100001, "127.0.0.121");
            for (const auto &invalid : {"null", "[]", "{"})
                check(local.call_raw(invalid)["status"] != 0, "invalid JSON rejected");
            check(local.call({{"op", "status"}})["result"]["ready"] == true,
                  "invalid requests preserve context");
            for (const auto action : {"status", "friends", "invite"})
                check(local.call({{"action", action}}, OSW_OVERLAY)["status"] == OSW_NOT_SUPPORTED,
                      "lab backend never fabricates native overlay support");
            osw::core::UdpSocket game("127.0.0.121", 3074);
            osw::core::Config config;
            config.companion_path = exe;
            config.companion_environment =
                Json{{"OSW_TEST_ROLE", "peer"}, {"OSW_TEST_ROOT", root.string()}}.dump();
            osw::core::Child peer;
            peer.start(config);
            check(child_result(peer)["ready"] == true, "peer readiness");
            std::cout << "peer ready\n";
            auto token = local.call({{"client_id", "fixture"}, {"scope", "openid"}}, OSW_TOKEN);
            std::string encoded;
            check(osw::core::unbase64(token["body"], encoded, 4096), "token body");
            auto decoded = osw::core::parse_token(encoded);
            check(decoded.user == 100001 && decoded.bytes.size() == 32, "local token binding");
            auto friends = local.call(Json::object(), OSW_FRIENDS);
            std::string list;
            check(osw::core::unbase64(friends["body"], list, 65536) &&
                      Json::parse(list)["friendList"].size() == 1,
                  "cross-process friend list");
            auto lookup = local.call({{"op", "name.lookup"}, {"names", {"LabTwo"}}});
            check(lookup["result"]["players"].size() == 1, "local friend name lookup");
            const auto initial_reply = local.call(
                {{"op", "peer.endpoint"}, {"scope", "friend"}, {"peer", "100002"}});
            check(initial_reply["status"] == 0, "initial peer endpoint unavailable");
            auto initial_endpoint = initial_reply["result"];
            const auto expect_preparty_drop = [&] {
                auto before = remote(peer, {{"metadata", {{"op", "status"}}}})["result"];
                check(game.send("ungranted", initial_endpoint["address"], initial_endpoint["port"]),
                      "ungranted datagram sent by local game");
                const auto until = Clock::now() + std::chrono::seconds(3);
                for (;;) {
                    auto after = remote(peer, {{"metadata", {{"op", "status"}}}})["result"];
                    if (after["network_received"].get<std::uint64_t>() >
                        before["network_received"].get<std::uint64_t>()) {
                        check(after["proxy_sent"] == before["proxy_sent"] &&
                                  remote(peer, {{"test", "udp.empty"}})["empty"] == true,
                              "ungranted preparty datagram never reaches remote game");
                        break;
                    }
                    check(Clock::now() < until, "ungranted carrier delivery deadline");
                    pause();
                }
            };
            expect_preparty_drop();
            check(remote(peer, {{"metadata",
                                 {{"op", "advertisement.set"},
                                  {"context", "fixture"},
                                  {"payload", "YWQ="}}}})["status"] == 0,
                  "private ad installed");
            Json ad;
            auto ad_end = Clock::now() + std::chrono::seconds(3);
            do {
                ad = local.call(
                    {{"op", "advertisement.get"}, {"context", "fixture"}, {"peer", "100002"}});
                if (ad["status"] == 0)
                    break;
                pause();
            } while (Clock::now() < ad_end);
            check(ad["result"]["payload"] == "YWQ=", "private friend ad query before party/room");
            check(game.send("first", initial_endpoint["address"], initial_endpoint["port"]),
                  "unilateral friend bootstrap sends before any room");
            auto bootstrap = remote(peer, {{"test", "udp.recv"}});
            check(bootstrap.value("payload", std::string{}) == osw::core::base64("first"),
                  "serving a live advertisement authorizes the reverse proxy");
            check(remote(peer, {{"test", "udp.send"}, {"payload", osw::core::base64("reply")},
                                {"address", bootstrap["address"]}, {"port", bootstrap["port"]}})["sent"] == true &&
                      receive(game)["payload"] == osw::core::base64("reply"),
                  "unilateral preparty bootstrap returns without reciprocal lookup");
            check(remote(peer, {{"metadata", {{"op", "advertisement.set"}, {"context", "fixture"},
                                               {"payload", ""}}}})["status"] == 0,
                  "withdraw local advertisement");
            expect_preparty_drop();
            check(remote(peer, {{"metadata", {{"op", "advertisement.set"}, {"context", "fixture"},
                                               {"payload", "YWQ="}}}})["status"] == 0,
                  "publish a later advertisement");
            expect_preparty_drop();
            check(local.call({{"op", "advertisement.get"}, {"context", "fixture"},
                              {"peer", "100002"}})["status"] == 0,
                  "fresh advertisement read restores only that friend's authorization");
            check(game.send("renewed", initial_endpoint["address"], initial_endpoint["port"]) &&
                      remote(peer, {{"test", "udp.recv"}}).value("payload", std::string{}) ==
                          osw::core::base64("renewed"), "fresh grant delivers again");
            auto bad =
                local.call({{"op", "directory.create"}, {"metadata", {{"secret", "forbidden"}}}});
            check(bad["status"] != 0, "private metadata rejected");
            std::cout << "friends ready\n";
            auto created = local.call({{"op", "directory.create"},
                                       {"capacity", 8},
                                       {"metadata", {{"playlist", "race"}, {"type", "public"}}}});
            check(created["status"] == 0, "create room");
            auto room = created["result"]["room_id"];
            auto found = remote(
                peer,
                {{"metadata", {{"op", "directory.search"}, {"metadata", {{"playlist", "race"}}}}}});
            check(found["result"]["rooms"].size() == 1, "cross-process directory search");
            check(found["result"]["rooms"][0]["members"].empty() &&
                      found["result"]["rooms"][0]["owner"] == "100001",
                  "search uses public owner metadata without member enumeration");
            std::cout << "directory found\n";
            auto joined =
                remote(peer, {{"metadata", {{"op", "directory.join"}, {"room_id", room}}}});
            check(joined["result"]["members"].size() == 2, "cross-process room join");
            auto endpoint = local.call(
                {{"op", "peer.endpoint"}, {"room_id", room}, {"peer", "100002"}})["result"];
            auto remote_endpoint = remote(
                peer,
                {{"metadata",
                  {{"op", "peer.endpoint"}, {"room_id", room}, {"peer", "100001"}}}})["result"];
            std::cout << "endpoints allocated\n";
            const std::string bytes("\0\xFF\x7Froom", 7);
            check(local.call({{"op", "peer.send"},
                              {"room_id", room},
                              {"peer", "100002"},
                              {"payload", osw::core::base64(bytes)}})["status"] == 0,
                  "binary peer send");
            auto until = Clock::now() + std::chrono::seconds(4);
            bool received = false;
            while (!received && Clock::now() < until) {
                auto polled = remote(peer, {{"metadata", {{"op", "events.poll"}}}});
                for (auto &event : polled["result"]["events"])
                    if (event["event"] == "peer.message") {
                        check(event["payload"] == osw::core::base64(bytes), "binary preservation");
                        received = true;
                    }
                pause();
            }
            check(received, "reliable peer receive");
            std::cout << "message received\n";
            check(game.send(bytes, endpoint["address"], endpoint["port"]), "game sends to proxy");
            auto incoming = remote(peer, {{"test", "udp.recv"}});
            if (!incoming.contains("payload")) {
                std::cout << "local UDP counters "
                          << local.call({{"op", "status"}})["result"].dump() << " remote "
                          << incoming["result"].dump() << "\n";
            }
            check(incoming["payload"] == osw::core::base64(bytes) &&
                      incoming["port"] == remote_endpoint["port"],
                  "proxy receive preserves remote endpoint");
            check(remote(peer, {{"test", "udp.send"},
                                {"payload", osw::core::base64(bytes)},
                                {"address", remote_endpoint["address"]},
                                {"port", remote_endpoint["port"]}})["sent"] == true,
                  "remote game sends");
            check(receive(game)["payload"] == osw::core::base64(bytes), "bidirectional UDP proxy");
            std::cout << "UDP passed\n";
            Json description{{"sessionType", "owner-bind"},
                             {"sessionPrivacy", "private"},
                             {"sessionMaxUser", 8},
                             {"sessionLockFlag", false},
                             {"availablePlatforms", {"PS4"}}};
            auto body = part("session-request", description.dump()) +
                        part("session-image", "image") + part("session-data", "data") +
                        part("changeable-session-data", "change") + "--fixture--\r\n";
            auto party = local.call({{"method", "POST"},
                                     {"path", "/v1/sessions"},
                                     {"content_type", "multipart/mixed; boundary=fixture"}},
                                    OSW_WEBAPI, body);
            std::string party_body;
            osw::core::unbase64(party["body"], party_body, 65536);
            check(party["http"] == 200 || party["http"] == 201, "platform party created");
            auto session = Json::parse(party_body)["sessionId"];
            check(local.call({{"session_id", session}, {"to", {"LabTwo"}}},
                             OSW_INVITE_SEND)["http"] == 204,
                  "lab invite sent");
            auto invitation = remote(peer, {{"test", "event"}, {"type", OSW_INVITATION_RECEIVED}});
            check(invitation.is_object() && invitation.contains("invitation_id"),
                  "lab invitation event");
            check(remote(peer, {{"metadata", {{"invitation_id", invitation["invitation_id"]}}},
                                {"operation", OSW_INVITE_ACCEPT}})["status"] == 0,
                  "lab invitation accepted");
            check(remote(peer, {{"metadata",
                                 {{"op", "directory.leave"}, {"room_id", room}}}})["status"] == 0,
                  "member leave");
            check(local.call({{"op", "peer.send"},
                              {"room_id", room},
                              {"peer", "100002"},
                              {"payload", ""}})["status"] != 0,
                  "departed peer rejected");
            check(
                remote(peer, {{"metadata",
                               {{"method", "POST"},
                                {"path", "/v1/sessions/" + session.get<std::string>() + "/members"},
                                {"content_type", "application/json"}}},
                              {"operation", OSW_WEBAPI},
                              {"body", "e30="}})["status"] == 0,
                "game commits party member");
            // The owner must observe committed membership before issuing search.
            std::this_thread::sleep_for(std::chrono::milliseconds(100));
            check(local.call({{"op", "directory.leave"}, {"room_id", room}})["status"] == 0,
                  "owner leaves old room");
            auto friend_endpoint = remote(
                peer,
                {{"metadata",
                  {{"op", "peer.endpoint"}, {"scope", "friend"}, {"peer", "100001"}}}})["result"];
            auto party_room =
                local.call({{"op", "directory.create"},
                            {"metadata", {{"playlist", "party"}, {"type", "public"}}}});
            check(party_room["status"] == 0 && party_room["result"]["members"].size() == 2,
                  "committed party follower joins selected room before owner completion");
            bool selected = false;
            auto selection_end = Clock::now() + std::chrono::seconds(3);
            while (!selected && Clock::now() < selection_end) {
                auto polled = remote(peer, {{"metadata", {{"op", "events.poll"}}}});
                for (auto &event : polled["result"]["events"])
                    if (event["event"] == "room.selected")
                        selected = true;
                pause();
            }
            check(selected, "follower companion receives selected room");
            check(remote(peer, {{"test", "udp.send"},
                                {"payload", osw::core::base64(bytes)},
                                {"address", friend_endpoint["address"]},
                                {"port", friend_endpoint["port"]}})["sent"] == true,
                  "follower sends through pre-party friend endpoint");
            auto first = receive(game);
            check(first["payload"] == osw::core::base64(bytes),
                  "friend endpoint promotes to joined room for first race packet");
            check(game.send(bytes, first["address"], first["port"]), "owner replies to lazy proxy");
            auto returned = remote(peer, {{"test", "udp.recv"}});
            check(returned["payload"] == osw::core::base64(bytes) &&
                      returned["port"] == friend_endpoint["port"],
                  "promotion preserves the advertised friend socket");
            auto party_room_id = party_room["result"]["room_id"];
            auto leave_selected = [&] {
                check(remote(peer, {{"metadata",
                                     {{"op", "directory.leave"},
                                      {"room_id", party_room_id}}}})["status"] == 0,
                      "follower explicitly leaves selected room");
                for (int pass = 0; pass < 8; ++pass) {
                    std::this_thread::sleep_for(std::chrono::milliseconds(100));
                    check(local.call({{"op", "directory.members"},
                                      {"room_id", party_room_id}})["result"]["members"]
                                  .size() == 1,
                          "explicit leave is not undone by retained party selection");
                    auto events = remote(peer, {{"metadata", {{"op", "events.poll"}}}});
                    for (auto &event : events["result"]["events"])
                        check(event["event"] != "room.selected", "no automatic rejoin after leave");
                }
            };
            leave_selected();
            auto rejoined = remote(
                peer, {{"metadata", {{"op", "directory.join"}, {"room_id", party_room_id}}}});
            check(rejoined["status"] == 0 && rejoined["result"]["members"].size() == 2,
                  "explicit join restores membership after a leave");
            leave_selected();
            check(remote(peer, {{"metadata",
                                 {{"op", "directory.search"},
                                  {"metadata", {{"playlist", "party"}}}}}})["status"] == 0,
                  "explicit search supplies a new follow intent");
            auto rejoin_end = Clock::now() + std::chrono::seconds(3);
            while (local
                       .call({{"op", "directory.members"},
                              {"room_id", party_room_id}})["result"]["members"]
                       .size() != 2) {
                check(Clock::now() < rejoin_end, "explicit search re-enables party following");
                pause();
            }
            remote(peer, {{"metadata", {{"op", "events.poll"}}}});
            leave_selected();
            check(local.call({{"op", "directory.leave"}, {"room_id", party_room_id}})["status"] ==
                      0,
                  "leader clears prior selection");
            auto new_selection =
                local.call({{"op", "directory.create"}, {"metadata", {{"playlist", "new-party"}}}});
            check(new_selection["status"] == 0 && new_selection["result"]["members"].size() == 2,
                  "a genuinely new leader selection re-enables automatic following");
            auto cancelled = local.call({{"op", "directory.create"},
                                         {"capacity", 1},
                                         {"metadata", {{"playlist", "cancel-test"}}}});
            check(cancelled["status"] != 0, "party room cannot complete without capacity");
            auto after_cancel = local.call(
                {{"op", "directory.search"}, {"metadata", {{"playlist", "cancel-test"}}}});
            check(after_cancel["result"]["rooms"].empty(),
                  "timed-out party creation rolls back advertised room");
            std::string cancelled_room;
            bool rollback_seen = false;
            for (int batch = 0; batch < 4; ++batch) {
                auto polled = local.call({{"op", "events.poll"}});
                for (auto &event : polled["result"]["events"]) {
                    if (event.value("event", std::string{}) == "room.changed" &&
                        event.value("metadata", Json::object()).value("playlist", std::string{}) ==
                            "cancel-test")
                        cancelled_room = event["room_id"].get<std::string>();
                    if (!cancelled_room.empty() &&
                        event.value("event", std::string{}) == "room.closed" &&
                        event["room_id"] == cancelled_room)
                        rollback_seen = true;
                }
            }
            check(rollback_seen, "timed-out party creation emits room closure");
            peer.stop();
        }
        auto marker = (root / "companion.done").string();
        {
            Client child(root.string(), 100003, "127.0.0.123", exe,
                         Json{{"OSW_TEST_ROLE", "companion"}, {"OSW_TEST_DONE", marker}}.dump());
            auto end = Clock::now() + std::chrono::seconds(5);
            while (!child.call({{"op", "status"}})["result"]["ready"].get<bool>()) {
                check(Clock::now() < end, "companion ready deadline");
                pause();
            }
        }
        check(std::filesystem::is_regular_file(marker), "companion observes stdin EOF");
        {
            osw::core::Config config;
            config.companion_path = exe;
            config.companion_environment = Json{{"OSW_TEST_ROLE", "blocked"}}.dump();
            osw::core::Child blocked;
            blocked.start(config);
            check(blocked.send(std::string(98000, 'x')), "queue blocked child write");
            std::this_thread::sleep_for(std::chrono::milliseconds(30));
            auto began = Clock::now();
            blocked.stop();
            check(Clock::now() - began < std::chrono::seconds(3),
                  "blocked child writer shutdown is bounded");
        }
        std::filesystem::remove_all(root);
        std::cout << "P2P simulated cross-process directory, friends, invitations, "
                     "binary messages, UDP and child ownership passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        if (std::getenv("OSW_TEST_ROOT"))
            std::ofstream(std::filesystem::path(std::getenv("OSW_TEST_ROOT")) / "peer.error")
                << error.what();
        return 1;
    }
}
