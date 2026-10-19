// Synthetic Steam API fixture only. No Steam client starts.
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstdint>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <string>
#include <thread>
#ifdef _WIN32
extern "C" __declspec(dllimport) void OSWFixtureSetSteamID(std::uint64_t);
#else
extern "C" void OSWFixtureSetSteamID(std::uint64_t);
#endif
using Clock = std::chrono::steady_clock;
void check(bool ok, const char *message) { if (!ok) throw std::runtime_error(message); }
void pause() { std::this_thread::sleep_for(std::chrono::milliseconds(10)); }
osw_identity identity(osw_context *context) {
    osw_identity id{sizeof(id)};
    check(osw_get_identity(context, &id) == OSW_OK, "identity snapshot");
    return id;
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture path required");
        OSWFixtureSetSteamID(1);
        osw_config_v2 config{};
        config.base.size = sizeof(config); config.base.abi_version = OSW_ABI_VERSION_2;
        std::strcpy(config.base.library_path, argv[1]);
        std::strcpy(config.base.title, "5760"); std::strcpy(config.base.build, "1.21");
        std::strcpy(config.base.realm, "account-fixture");
        std::strcpy(config.game_bind, "127.0.0.39"); config.game_port = 3074;
        osw_context *context = nullptr;
        check(osw_create_v2(&config, &context) == OSW_OK && osw_start(context) == OSW_OK, "context starts");
        auto end = Clock::now() + std::chrono::seconds(4);
        while (!identity(context).signed_in) { check(Clock::now() < end, "identity deadline"); pause(); }
        check(identity(context).account_id == 1, "starting account");
        osw_event event{sizeof(event)};
        while (osw_poll_event(context, &event) == OSW_OK) {}

        // Another account signed in behind the same client fails the context.
        OSWFixtureSetSteamID(3);
        end = Clock::now() + std::chrono::seconds(3);
        while (identity(context).signed_in) { check(Clock::now() < end, "account change ignored"); pause(); }
        const auto after = identity(context);
        check(after.account_id == 0 && std::string(after.reason).find("account changed") != std::string::npos,
              "account change reported");
        bool changed = false;
        while (osw_poll_event(context, &event) == OSW_OK) changed |= event.type == OSW_IDENTITY_CHANGED;
        check(changed, "identity event published");
        const std::string meta = R"({"op":"status"})";
        osw_request request{sizeof(request), OSW_P2P, 1000, meta.data(), meta.size()};
        std::uint64_t id = 0;
        auto status = osw_submit(context, &request, &id);
        if (status == OSW_OK) {
            osw_result result{sizeof(result)};
            end = Clock::now() + std::chrono::seconds(2);
            while ((status = osw_get_result(context, id, &result)) == OSW_PENDING) {
                check(Clock::now() < end, "request deadline"); pause();
            }
            check(status != OSW_OK || result.status != OSW_OK, "failed context still serves requests");
            osw_release(context, id);
        }
        osw_stop(context);
        osw_destroy(context);
        OSWFixtureSetSteamID(1);
        std::cout << "Steam account change contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
