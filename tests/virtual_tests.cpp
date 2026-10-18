#include "core/credential.hpp"
#include "core/local_io.hpp"
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
using namespace std::chrono_literals;
using Clock = std::chrono::steady_clock;
bool loopback() { return std::getenv("OSW_LOOPBACK_TEST") != nullptr; }
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
template<class F> void wait(F predicate) {
    const auto end = Clock::now() + 5s;
    while (!predicate()) {
        check(Clock::now() < end, "virtual fixture deadline");
        std::this_thread::sleep_for(5ms);
    }
}
template<std::size_t N> void copy(char (&out)[N], const std::string &in) {
    check(in.size() < N, "fixture string limit");
    std::memcpy(out, in.c_str(), in.size() + 1);
}
struct Client {
    osw_context *context = nullptr;
    Client(const std::string &root, std::uint64_t id, const char *bind, unsigned port,
           bool enabled = true, const std::string &child = {}, const std::string &kind = {},
           unsigned app_id = 480, bool force_virtual = false) {
        osw_config_v2 config{};
        config.base.size = sizeof(config); config.base.abi_version = OSW_ABI_VERSION_2;
        copy(config.base.title, "fixture"); copy(config.base.build, "1");
        copy(config.base.realm, "virtual"); copy(config.base.online_id, "Peer" + std::to_string(id));
        config.backend = 1; config.lab_account_id = id; config.game_port = port;
        copy(config.lab_root, root); copy(config.game_bind, bind);
        copy(config.companion_path, child);
        if (!child.empty()) copy(config.companion_environment, Json{{"OSW_VIRTUAL_CHILD", kind}}.dump());
        check(osw_create_v2(&config, &context) == OSW_OK, "virtual context create");
        check(osw_set_app_id(context, app_id) == OSW_OK, "fixture AppID configuration");
        if (enabled) check((loopback() && !force_virtual ? osw_enable_loopback_endpoints(context) : osw_enable_virtual_endpoints(context)) == OSW_OK, "endpoint enable");
        check(osw_start(context) == OSW_OK, "virtual start");
    }
    ~Client() { osw_destroy(context); }
    osw_endpoint local() {
        osw_endpoint value{sizeof(value)};
        wait([&] { return osw_get_local_endpoint(context, &value) != OSW_PENDING; });
        check(osw_get_local_endpoint(context, &value) == OSW_OK, "virtual readiness");
        return value;
    }
    Json call(Json metadata, unsigned op = OSW_P2P) {
        const auto text = metadata.dump();
        osw_request request{sizeof(request), op, 3000, text.data(), text.size()};
        std::uint64_t id = 0;
        check(osw_submit(context, &request, &id) == OSW_OK, "virtual request submit");
        osw_result result{sizeof(result)};
        wait([&] { return osw_get_result(context, id, &result) != OSW_PENDING; });
        Json out{{"status", result.status}, {"http", result.http_status}};
        out["data"] = result.metadata_size ? Json::parse(result.metadata, result.metadata + result.metadata_size)
                                           : Json::object();
        if (result.body_size) out["body"] = Json::parse(result.body, result.body + result.body_size);
        osw_release(context, id);
        return out;
    }
    osw_endpoint route(const osw_endpoint &in, unsigned direction) {
        osw_endpoint out{sizeof(out)};
        check(osw_route_endpoint(context, direction, &in, &out) == OSW_OK, "authorized endpoint route");
        return out;
    }
};
bool equal(const osw_endpoint &a, const osw_endpoint &b) {
    return a.port == b.port && std::strcmp(a.address, b.address) == 0;
}
osw_endpoint received(osw::core::UdpSocket &socket, const std::string &expected) {
    std::string bytes, address; std::uint16_t port = 0;
    wait([&] { return socket.receive(bytes, address, port); });
    check(bytes == expected, "opaque datagram preserved byte for byte");
    osw_endpoint value{sizeof(value)}; copy(value.address, address); value.port = port;
    return value;
}
void pair(const std::string &root) {
    osw::core::UdpSocket game_a("127.0.0.125"), game_b("127.0.0.126");
    Client a(root, 200001, "127.0.0.125", game_a.port());
    Client b(root, 200002, "127.0.0.126", game_b.port());
    Client legacy(root, 200003, "127.0.0.127", 12345, false);
    std::unique_ptr<Client> other_app, other_mode;
    if (loopback()) {
        other_app = std::make_unique<Client>(root, 200006, "127.0.0.129", 12345, true, "", "", 481);
        other_mode = std::make_unique<Client>(root, 200007, "127.0.0.130", 12345, true, "", "", 480, true);
        other_app->local(); other_mode->local();
    }
    const auto self_a = a.local(), self_b = b.local();
    check(!equal(self_a, self_b), "different full identities have distinct fixture endpoints");
    wait([&] { return a.call({}, OSW_FRIENDS)["body"]["friendList"].size() == 1; });
    check(b.call({}, OSW_FRIENDS)["body"]["friendList"].size() == 1 &&
              legacy.call({}, OSW_FRIENDS)["body"]["friendList"].empty(),
          "friends exclude the other address mode");
    if (loopback()) check(other_app->call({}, OSW_FRIENDS)["body"]["friendList"].empty() &&
                            other_mode->call({}, OSW_FRIENDS)["body"]["friendList"].empty(),
                          "AppID and virtual-v1 isolation before any room");
    check(b.call({{"op", "advertisement.set"}, {"context", "opaque"}, {"payload", "YWQ="}})["status"] == 0,
          "publish private fixture advertisement");
    wait([&] { return a.call({{"op", "advertisement.get"}, {"context", "opaque"}, {"peer", "200002"}})["status"] == 0; });
    if (loopback()) {
        osw::core::UdpSocket occupied(self_b.address, static_cast<std::uint16_t>(self_b.port), true);
        check(a.call({{"op", "peer.endpoint"}, {"scope", "friend"}, {"peer", "200002"}})["status"] != 0,
              "occupied canonical endpoint fails without fallback");
        osw_endpoint absent{sizeof(absent)};
        check(osw_route_endpoint(a.context, OSW_ROUTE_OUTBOUND, &self_b, &absent) == OSW_NOT_FOUND,
              "failed bind creates no route");
        check(a.call({{"op", "status"}})["data"]["ready"] == true, "bind failure preserves provider");
    }
    auto endpoint = a.call({{"op", "peer.endpoint"}, {"scope", "friend"}, {"peer", "200002"}})["data"];
    check(endpoint["address"] == self_b.address && endpoint["port"] == self_b.port,
          "peer endpoint equals remote self identity before any room");
    auto proxy_b = a.route(self_b, OSW_ROUTE_OUTBOUND);
    if (loopback()) check(equal(proxy_b, self_b), "no host route translation needed");
    const std::string bytes("\0opaque\xff", 8);
    if (loopback()) {
        osw::core::UdpSocket foreign("127.0.0.125");
        const auto before = a.call({{"op", "status"}})["data"]["proxy_rejected"].get<unsigned>();
        check(foreign.send(bytes, self_b.address, static_cast<std::uint16_t>(self_b.port)), "foreign source send");
        wait([&] { return a.call({{"op", "status"}})["data"]["proxy_rejected"].get<unsigned>() > before; });
        std::string rejected, address; std::uint16_t port;
        check(!game_b.receive(rejected, address, port), "wrong game source never reaches remote game");
    }
    check(game_a.send(bytes, proxy_b.address, static_cast<std::uint16_t>(proxy_b.port)), "send to local proxy");
    auto proxy_a = received(game_b, bytes);
    check(equal(b.route(proxy_a, OSW_ROUTE_INBOUND), self_a),
          "first inbound packet already has its canonical source mapping");
    check(equal(b.route(self_a, OSW_ROUTE_OUTBOUND), proxy_a), "first inbound mapping is reversible");
    check(game_b.send(bytes, proxy_a.address, static_cast<std::uint16_t>(proxy_a.port)), "opaque reply send");
    check(equal(a.route(received(game_a, bytes), OSW_ROUTE_INBOUND), self_b),
          "opaque reply source retains remote canonical identity");
    check(b.call({{"op", "advertisement.set"}, {"context", "opaque"}, {"payload", ""}})["status"] == 0,
          "withdraw private advertisement");
    osw_endpoint out{sizeof(out)};
    wait([&] { return osw_route_endpoint(b.context, OSW_ROUTE_INBOUND, &proxy_a, &out) == OSW_NOT_FOUND; });
    check(osw_route_endpoint(b.context, OSW_ROUTE_OUTBOUND, &self_a, &out) == OSW_NOT_FOUND,
          "withdrawal revokes both directions without closing the context");
    check(b.call({{"op", "advertisement.set"}, {"context", "opaque"}, {"payload", "YWQ="}})["status"] == 0,
          "republish advertisement");
    check(osw_route_endpoint(b.context, OSW_ROUTE_OUTBOUND, &self_a, &out) == OSW_NOT_FOUND,
          "republish alone does not revive authorization");
    check(a.call({{"op", "advertisement.get"}, {"context", "opaque"}, {"peer", "200002"}})["status"] == 0,
          "fresh authenticated query restores permission");
    check(game_a.send(bytes, proxy_b.address, static_cast<std::uint16_t>(proxy_b.port)), "fresh grant send");
    const auto new_proxy_a = received(game_b, bytes);
    check((loopback() ? equal(new_proxy_a, proxy_a) : !equal(new_proxy_a, proxy_a)) && equal(b.route(new_proxy_a, OSW_ROUTE_INBOUND), self_a),
          "revoked physical endpoint is never reused in this context");
    if (!loopback()) check(osw_route_endpoint(b.context, OSW_ROUTE_INBOUND, &proxy_a, &out) == OSW_NOT_FOUND,
          "stale copied source cannot acquire a new route");
    auto created = a.call({{"op", "directory.create"}, {"capacity", 8}, {"metadata", Json::object()}});
    check(created["status"] == 0, "create virtual directory room");
    check(legacy.call({{"op", "directory.search"}, {"metadata", Json::object()}})["data"]["rooms"].empty(),
          "legacy directory search cannot discover virtual rooms");
    check(legacy.call({{"op", "directory.join"}, {"room_id", created["data"]["room_id"]}})["status"] != 0,
          "legacy direct room join cannot cross address modes");
    check(legacy.call({{"op", "peer.endpoint"}, {"scope", "friend"}, {"peer", "200001"}})["status"] != 0,
          "legacy friend endpoint cannot cross address modes");
}
int child(const char *mode) {
    std::string line;
    check(static_cast<bool>(std::getline(std::cin, line)), "owner frame missing");
    const auto owner = Json::parse(line);
    check(owner["transport"].size() == 5 && owner["transport"]["mode"] == (loopback() ? "loopback-v1" : "virtual-v1") &&
              owner["transport"]["game_address"] == "127.0.0.125", "virtual owner handshake");
    Json ready{{"v", 1}, {"event", "ready"}};
    if (std::string(mode) == "new") ready["capabilities"] = {loopback() ? "loopback.endpoints.v1" : "virtual.endpoints.v1"};
    std::cout << ready.dump() << std::endl;
    while (std::getline(std::cin, line)) {}
    return 0;
}
int main(int argc, char **argv) {
    try {
        if (const auto mode = std::getenv("OSW_VIRTUAL_CHILD")) return child(mode);
        check(argc >= 1, "fixture executable path");
        const auto root = std::filesystem::current_path() /
            ("virtual-test-" + osw::core::hex(osw::core::random_secret()).substr(0, 12));
        pair(root.string());
        const auto exe = std::filesystem::absolute(argv[0]).string();
        {
            Client old(root.string(), 200004, "127.0.0.125", 12345, true, exe, "old");
            osw_endpoint out{sizeof(out)};
            wait([&] { return osw_get_local_endpoint(old.context, &out) == OSW_UNAVAILABLE; });
        }
        {
            Client modern(root.string(), 200005, "127.0.0.125", 12345, true, exe, "new");
            modern.local();
        }
        std::filesystem::remove_all(root); // Exact newly created fixture directory only.
        std::cout << "Virtual Lab routes, unilateral bootstrap, revocation, mode isolation and child negotiation passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
