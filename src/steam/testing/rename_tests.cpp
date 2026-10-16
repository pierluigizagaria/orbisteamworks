// Synthetic provider and pipe fixtures only. No Steam client or publisher starts.
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
extern "C" __declspec(dllimport) void OSWFixtureSetPersona(const char *);
#else
extern "C" void OSWFixtureSetPersona(const char *);
#endif
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
int child(const std::string &mode) {
    std::string line;
    check(static_cast<bool>(std::getline(std::cin, line)), "owner missing");
    auto owner = Json::parse(line);
    check(owner["event"] == "owner", "owner frame");
    std::cout << Json{{"v", 1}, {"event", "ready"},
                      {"capabilities", mode == "modern" ? Json::array({"owner.name.v1"}) : Json::array()}}
                     .dump() << std::endl;
    while (std::getline(std::cin, line)) {
        auto update = Json::parse(line);
        check(mode == "modern" && update.size() == 4 && update["v"] == 1 &&
                  update["event"] == "owner.name" && update["steam_id"] == owner["steam_id"] &&
                  update["online_id"] == "Renamed", "private negotiated name frame");
        std::ofstream(std::getenv("OSW_RENAME_REPORT")) << "renamed";
    }
    return 0;
}
struct Context {
    osw_context *value = nullptr;
    ~Context() { osw_destroy(value); }
    osw_identity identity() {
        osw_identity id{sizeof(id)};
        check(osw_get_identity(value, &id) == OSW_OK, "identity snapshot");
        return id;
    }
    std::string request(unsigned operation, const std::string &meta, bool metadata = false) {
        osw_request req{sizeof(req), operation, 2000, meta.data(), meta.size()};
        std::uint64_t id;
        check(osw_submit(value, &req, &id) == OSW_OK, "request accepted");
        osw_result result{sizeof(result)};
        const auto end = Clock::now() + std::chrono::seconds(3);
        while (osw_get_result(value, id, &result) == OSW_PENDING) {
            check(Clock::now() < end, "request deadline"); pause();
        }
        check(result.status == OSW_OK, "request succeeds");
        std::string data = metadata ? std::string(result.metadata, result.metadata_size)
                                   : std::string(reinterpret_cast<const char *>(result.body), result.body_size);
        osw_release(value, id);
        return data;
    }
};
void run(const char *library, const std::string &exe, const std::string &mode, bool fixed) {
    OSWFixtureSetPersona("Initial");
    osw_config_v2 config{};
    config.base.size = sizeof(config); config.base.abi_version = OSW_ABI_VERSION_2;
    std::strcpy(config.base.library_path, library);
    std::strcpy(config.base.title, "5760"); std::strcpy(config.base.build, "1.21");
    std::strcpy(config.base.realm, "rename-fixture");
    std::strcpy(config.game_bind, "127.0.0.39"); config.game_port = 3074;
    if (fixed) std::strcpy(config.base.online_id, "Configured");
    const auto report = std::filesystem::absolute("rename-" + mode + (fixed ? "-fixed" : "") + ".txt");
    std::filesystem::remove(report);
    if (!mode.empty()) {
        std::strcpy(config.companion_path, exe.c_str());
        const auto env = Json{{"OSW_RENAME_CHILD", mode}, {"OSW_RENAME_REPORT", report.string()}}.dump();
        std::strcpy(config.companion_environment, env.c_str());
    }
    Context context;
    check(osw_create_v2(&config, &context.value) == OSW_OK && osw_start(context.value) == OSW_OK, "context starts");
    auto end = Clock::now() + std::chrono::seconds(4);
    while (!context.identity().signed_in) { check(Clock::now() < end, "identity deadline"); pause(); }
    while (!Json::parse(context.request(OSW_P2P, R"({"op":"status"})", true))["ready"].get<bool>()) {
        check(Clock::now() < end, "companion deadline"); pause();
    }
    check(Json::parse(context.request(OSW_P2P, R"({"op":"status"})", true))["owner_name_updates"] ==
              (mode != "legacy"), "negotiated capability is observable");
    const auto before = context.identity();
    check(std::string(before.online_id) == (fixed ? "Configured" : "Initial"), "initial name");
    std::string token;
    const std::string token_meta = R"({"client_id":"fixture","scope":"openid"})";
    if (!mode.empty()) token = context.request(OSW_TOKEN, token_meta);
    osw_event event{sizeof(event)};
    while (osw_poll_event(context.value, &event) == OSW_OK) {}
    OSWFixtureSetPersona("  Renamed\x01  ");
    const bool changes = !fixed && mode != "legacy";
    end = Clock::now() + std::chrono::seconds(changes ? 3 : 1);
    while (Clock::now() < end && (!changes || std::string(context.identity().online_id) != "Renamed")) pause();
    const auto after = context.identity();
    check(after.signed_in && after.account_id == before.account_id &&
              std::string(after.online_id) == (changes ? "Renamed" : before.online_id), "name and immutable account");
    bool identity_event = false;
    while (osw_poll_event(context.value, &event) == OSW_OK)
        identity_event |= event.type == OSW_IDENTITY_CHANGED;
    check(identity_event == changes, "name changes publish identity event only when applied");
    if (!mode.empty()) check(token == context.request(OSW_TOKEN, token_meta), "bootstrap capability remains exact");
    auto own_name = Json::parse(context.request(OSW_P2P,
        Json{{"op", "name.resolve"}, {"peer", std::to_string(before.account_id)}}.dump(), true));
    check(own_name["online_id"] == after.online_id, "P2P self name follows identity");
    if (mode == "modern" && changes) {
        end = Clock::now() + std::chrono::seconds(2);
        while (!std::filesystem::exists(report)) { check(Clock::now() < end, "name delivered to child"); pause(); }
    } else check(!std::filesystem::exists(report), "old child and configured name receive no update");
    check(osw_stop(context.value) == OSW_OK, "fixture closes");
    std::filesystem::remove(report);
}
int main(int argc, char **argv) {
    try {
        if (auto mode = std::getenv("OSW_RENAME_CHILD")) return child(mode);
        check(argc == 2, "fixture path required");
        const auto exe = std::filesystem::absolute(argv[0]).string();
        run(argv[1], exe, "", false);
        run(argv[1], exe, "modern", false);
        run(argv[1], exe, "legacy", false);
        run(argv[1], exe, "modern", true);
        std::cout << "Offline rename identity, capability and negotiated companion contracts passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
