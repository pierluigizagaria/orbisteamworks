#include "core/credential.hpp"
#include "core/local_io.hpp"
#include "core/p2p.hpp"
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstdlib>
#include <cstring>
#include <filesystem>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
using namespace std::chrono_literals;
using Id = std::uint64_t;
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void wait(F predicate) {
    auto end = Clock::now() + 6s;
    while (!predicate()) {
        check(Clock::now() < end, "deferred fixture deadline");
        std::this_thread::sleep_for(5ms);
    }
}
template<size_t N> void copy(char (&out)[N], const std::string &value) {
    check(value.size() < N, "fixture field bound");
    std::memcpy(out, value.c_str(), value.size() + 1);
}
struct Client {
    osw_context *context = nullptr;
    Client(const std::string &root, const std::string &exe, std::uint64_t account,
           const std::string &mode = "good") {
        osw_config_v2 config{};
        config.base.size = sizeof(config); config.base.abi_version = OSW_ABI_VERSION_2;
        copy(config.base.title, "fixture"); copy(config.base.build, "1");
        copy(config.base.realm, "virtual"); copy(config.base.online_id, "Peer" + std::to_string(account));
        config.backend = 1; config.lab_account_id = account; config.game_port = 12345;
        copy(config.game_bind, "127.0.0.1"); copy(config.lab_root, root);
        copy(config.companion_path, exe);
        copy(config.companion_environment, Json{{"OSW_DEFERRED_CHILD", mode}}.dump());
        check(osw_create_v2(&config, &context) == OSW_OK, "deferred context create");
        check(osw_defer_game_endpoint(context) == OSW_NOT_SUPPORTED, "defer requires loopback selection");
        check(osw_enable_loopback_endpoints(context) == OSW_OK, "loopback enable");
        check(osw_defer_game_endpoint(context) == OSW_OK, "defer before start");
        check(osw_start(context) == OSW_OK, "deferred context start");
        check(osw_defer_game_endpoint(context) == OSW_INVALID_ARGUMENT, "defer immutable after start");
        wait([&] { osw_identity id{sizeof(id)}; osw_get_identity(context, &id); return id.signed_in != 0; });
    }
    ~Client() { osw_destroy(context); }
    std::uint64_t submit(Json metadata, unsigned operation = OSW_P2P, unsigned timeout = 3000) {
        const auto text = metadata.dump();
        osw_request request{sizeof(request), operation, timeout, text.data(), text.size()};
        std::uint64_t id = 0;
        check(osw_submit(context, &request, &id) == OSW_OK, "deferred submit");
        return id;
    }
    Json result(std::uint64_t id) {
        osw_result value{sizeof(value)};
        wait([&] { return osw_get_result(context, id, &value) != OSW_PENDING; });
        Json result{{"status", value.status}, {"http", value.http_status}};
        result["data"] = value.metadata_size ? Json::parse(value.metadata, value.metadata + value.metadata_size) : Json::object();
        if (value.body_size) result["body"] = Json::parse(value.body, value.body + value.body_size);
        osw_release(context, id); return result;
    }
    Json call(Json metadata, unsigned operation = OSW_P2P) { return result(submit(metadata, operation)); }
    void ready() { wait([&] { return call({{"op", "status"}})["data"]["ready"] == true; }); }
    void failed() {
        osw_endpoint value{sizeof(value)};
        wait([&] { return osw_get_local_endpoint(context, &value) == OSW_UNAVAILABLE; });
    }
    osw_endpoint local() {
        osw_endpoint value{sizeof(value)};
        check(osw_get_local_endpoint(context, &value) == OSW_OK, "bound canonical endpoint ready");
        return value;
    }
    void bind(const char *address, unsigned port) {
        auto result = call({{"op", "game.endpoint.bind"}, {"address", address}, {"port", port}});
        check(result["status"] == OSW_OK && result["data"]["address"] == address && result["data"]["port"] == port,
              "publisher acknowledged exact endpoint");
    }
};
int child(std::string mode) {
    std::string line;
    check(static_cast<bool>(std::getline(std::cin, line)), "child owner missing");
    const auto owner = Json::parse(line);
    check(owner["transport"].size() == 4 && owner["transport"]["mode"] == "loopback-v1" &&
          owner["transport"]["game_pending"] == true && !owner["transport"].contains("game_address") &&
          !owner["transport"].contains("game_port"), "pending descriptor has no fabricated physical source");
    Json capabilities = Json::array({"loopback.endpoints.v1"});
    if (mode != "missing") capabilities.push_back("game.endpoint.v1");
    std::cout << Json{{"v", 1}, {"event", "ready"}, {"capabilities", capabilities}}.dump() << std::endl;
    if (mode == "child-bind") {
        std::cout << Json{{"v", 1}, {"id", "forbidden"}, {"op", "game.endpoint.bind"},
                         {"address", "127.0.0.1"}, {"port", 12345}}.dump() << std::endl;
        check(static_cast<bool>(std::getline(std::cin, line)) && Json::parse(line)["ok"] == false,
              "publisher cannot register its own source");
    }
    while (std::getline(std::cin, line)) {
        auto frame = Json::parse(line);
        if (frame.value("event", std::string{}) != "game.endpoint") continue;
        check(frame.size() == 5, "exact binding frame");
        if (mode == "noack") continue;
        if (mode == "delay") std::this_thread::sleep_for(300ms);
        frame["event"] = "game.endpoint.bound";
        if (mode == "wrong-id") frame["id"] = "other";
        if (mode == "wrong-source") frame["port"] = frame["port"].get<unsigned>() + 1;
        std::cout << frame.dump() << std::endl;
        if (mode == "duplicate") std::cout << frame.dump() << std::endl;
    }
    return 0;
}
struct EmptySocial : osw::core::Social {
    std::vector<osw::core::Friend> friends() override { return {}; }
    std::string presence(Id, const std::string &) override { return {}; }
    bool set_presence(const std::string &, const std::string &) override { return true; }
    Id create_lobby(int) override { return 0; }
    Id join_lobby(Id) override { return 0; }
    void leave_lobby(Id) override {}
    bool invite(Id, Id) override { return false; }
    bool request_data(Id) override { return false; }
    std::string lobby_data(Id, const std::string &) override { return {}; }
    bool set_lobby_data(Id, const std::string &, const std::string &) override { return false; }
    bool delete_lobby_data(Id, const std::string &) override { return false; }
    Id owner(Id) override { return 0; }
    std::vector<Id> members(Id) override { return {}; }
    std::string member_data(Id, Id, const std::string &) override { return {}; }
    void set_member_data(Id, const std::string &, const std::string &) override {}
    bool set_joinable(Id, bool) override { return false; }
    bool set_limit(Id, int) override { return false; }
    bool set_friends_visible(Id, bool) override { return false; }
    bool send_message(Id, const std::string &) override { return false; }
};
void cancellation_at_completion(const std::string &exe) {
    osw::core::Config config;
    config.backend = 1; config.title = "fixture"; config.build = "1"; config.realm = "virtual";
    config.loopback_endpoints = true; config.defer_game_endpoint = true;
    config.companion_path = exe;
    config.companion_environment = R"({"OSW_DEFERRED_CHILD":"good"})";
    EmptySocial social;
    osw::core::P2P p2p(config, {true, 200020, "Fixture"}, social);
    Id next = 1;
    wait([&] {
        p2p.submit({next++, OSW_P2P, 3000, R"({"op":"status"})"});
        for (auto &result : p2p.poll().results)
            if (Json::parse(result.metadata).value("ready", false)) return true;
        return false;
    });
    const Id bind = next++;
    p2p.submit({bind, OSW_P2P, 3000, R"({"op":"game.endpoint.bind","address":"127.0.0.1","port":3104})"});
    wait([&] {
        for (auto &result : p2p.poll().results)
            if (result.id == bind) { check(result.status == OSW_OK, "core ack committed"); return true; }
        return false;
    });
    // Model a deadline/cancel applied by the C API after the worker consumes the
    // ack but before its completed result is accepted by the public job queue.
    p2p.cancel(bind);
    check(p2p.poll().failed, "cancel at completion closes uncertain committed context");
}
void pair(const std::string &root, const std::string &exe) {
    osw::core::UdpSocket game_a("127.0.0.125"), game_b("127.0.0.126"), wrong("127.0.0.125");
    Client a(root, exe, 200001, "child-bind"), b(root, exe, 200002);
    a.ready(); b.ready();
    osw_endpoint untouched{sizeof(untouched)};
    check(osw_get_local_endpoint(a.context, &untouched) == OSW_PENDING, "canonical routes pending before bind");
    check(a.call({}, OSW_FRIENDS)["status"] == OSW_OK, "friend reads available while pending");
    check(a.call({{"method", "GET"}, {"path", "/v1/users/me/friendList"}}, OSW_WEBAPI)["status"] == OSW_OK,
          "WebAPI reads available while pending");
    check(a.call({{"method", "POST"}, {"path", "/v1/sessions"}}, OSW_WEBAPI)["status"] == OSW_UNAVAILABLE,
          "WebAPI mutations blocked while pending");
    for (const auto &op : {"directory.create", "directory.join", "directory.search", "peer.endpoint", "advertisement.set"})
        check(a.call({{"op", op}})["data"]["error"] == "game_endpoint_pending", "transport operation gated");
    for (auto endpoint : {Json{{"address", "127.64.0.1"}, {"port", 3104}},
                          Json{{"address", "0.0.0.0"}, {"port", 3104}},
                          Json{{"address", "192.0.2.1"}, {"port", 3104}},
                          Json{{"address", "127.0.0.1"}, {"port", 0}},
                          Json{{"address", "127.0.0.1"}, {"port", -1}},
                          Json{{"address", "127.0.0.1"}, {"port", 65536}}}) {
        endpoint["op"] = "game.endpoint.bind";
        check(a.call(endpoint)["status"] != OSW_OK, "invalid game source rejected without leaving pending");
    }
    a.bind("127.0.0.125", game_a.port()); b.bind("127.0.0.126", game_b.port());
    const auto local_a = a.local(), local_b = b.local();
    check(a.call({{"op", "status"}})["data"]["game_endpoint_pending"] == false, "registration completed");
    check(a.call({{"op", "game.endpoint.bind"}, {"address", "127.0.0.125"}, {"port", wrong.port()}})["status"] != OSW_OK,
          "bound source cannot change during the context");
    auto room = a.call({{"op", "directory.create"}, {"capacity", 2}, {"metadata", Json::object()}})["data"]["room_id"];
    check(b.call({{"op", "directory.join"}, {"room_id", room}})["status"] == OSW_OK, "room permitted after binding");
    check(a.call({{"op", "peer.endpoint"}, {"room_id", room}, {"peer", "200002"}})["status"] == OSW_OK,
          "authorized peer route after binding");
    check(b.call({{"op", "peer.endpoint"}, {"room_id", room}, {"peer", "200001"}})["status"] == OSW_OK,
          "reciprocal route after binding");
    check(wrong.send("wrong", local_b.address, static_cast<std::uint16_t>(local_b.port)), "send unauthorized source");
    wait([&] { return a.call({{"op", "status"}})["data"]["proxy_rejected"].get<unsigned>() > 0; });
    std::string payload, address; std::uint16_t port = 0;
    check(!game_b.receive(payload, address, port), "unauthorized source never forwarded");
    check(game_a.send("opaque", local_b.address, static_cast<std::uint16_t>(local_b.port)), "send from registered source");
    wait([&] { return game_b.receive(payload, address, port); });
    check(payload == "opaque" && address == local_a.address && port == local_a.port,
          "delivery uses new source port and preserves canonical sender");
    check(game_b.send("reply", local_a.address, static_cast<std::uint16_t>(local_a.port)), "send reverse opaque data");
    wait([&] { return game_a.receive(payload, address, port); });
    check(payload == "reply" && address == local_b.address && port == local_b.port, "reverse registered delivery");
}
int main(int argc, char **argv) {
    try {
        if (const auto mode = std::getenv("OSW_DEFERRED_CHILD")) return child(mode);
        check(argc > 0, "fixture path");
        const auto exe = std::filesystem::absolute(argv[0]).string();
        const auto root = std::filesystem::current_path() /
            ("deferred-test-" + osw::core::hex(osw::core::random_secret()).substr(0, 12));
        pair(root.string(), exe);
        cancellation_at_completion(exe);
        { Client missing(root.string(), exe, 200010, "missing"); missing.failed(); }
        for (const auto &mode : {"wrong-id", "wrong-source", "duplicate"}) {
            Client invalid(root.string(), exe, 200011, mode); invalid.ready();
            auto id = invalid.submit({{"op", "game.endpoint.bind"}, {"address", "127.0.0.1"}, {"port", 3104}});
            invalid.failed(); invalid.result(id);
        }
        { Client timeout(root.string(), exe, 200012, "noack"); timeout.ready();
          auto id = timeout.submit({{"op", "game.endpoint.bind"}, {"address", "127.0.0.1"}, {"port", 3104}}, OSW_P2P, 100);
          check(timeout.result(id)["status"] != OSW_OK, "missing ack cannot report success"); timeout.failed(); }
        { Client canceled(root.string(), exe, 200013, "delay"); canceled.ready();
          auto id = canceled.submit({{"op", "game.endpoint.bind"}, {"address", "127.0.0.1"}, {"port", 3104}});
          std::this_thread::sleep_for(80ms);
          check(osw_cancel(canceled.context, id) == OSW_OK, "cancel binding");
          check(canceled.result(id)["status"] == OSW_ABORTED, "canceled result stays aborted"); canceled.failed(); }
        std::filesystem::remove_all(root); // Only this newly created private fixture directory.
        std::cout << "Deferred endpoint negotiation, exact UDP source, lifecycle and fail-closed contracts passed\n";
        return 0;
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
