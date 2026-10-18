#include "steam/runtime.hpp"
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#endif
extern "C" void OSWFixtureAppCallback(unsigned, bool);
extern "C" void OSWFixtureOverlayCallback(unsigned, unsigned, unsigned, int);
extern "C" void OSWFixtureSetPersona(const char *);
void check(bool value, const char *message) { if (!value) throw std::runtime_error(message); }
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture DLL path required");
        {
            osw::steam::Runtime runtime(argv[1], 12345);
            runtime.start_client();
            check(runtime.start_social(), "configured app platform starts");
#ifdef _WIN32
            char app[16]{};
            check(GetEnvironmentVariableA("SteamAppId", app, sizeof(app)) && std::strcmp(app, "12345") == 0,
                  "configured AppID reaches the process environment");
#else
            check(std::string(std::getenv("SteamAppId")) == "12345", "configured AppID environment");
#endif
            for (const bool invite : {false, true}) {
                OSWFixtureAppCallback(480, invite); runtime.poll();
                check(runtime.take_social_events().empty(), "default AppID callback rejected for configured AppID");
                OSWFixtureAppCallback(12345, invite); runtime.poll();
                const auto events = runtime.take_social_events();
                check(events.size() == 1 && events[0].kind == (invite ? osw::core::SocialEvent::invitation : osw::core::SocialEvent::friends_changed),
                      "configured AppID callback accepted");
            }
            OSWFixtureOverlayCallback(1, 1, 480, 12); runtime.poll();
            check(runtime.take_social_events().empty(), "foreign overlay callback rejected");
            OSWFixtureOverlayCallback(1, 1, 12345, 12); runtime.poll();
            check(runtime.take_social_events().size() == 1 && runtime.overlay_status().active,
                  "configured overlay callback accepted");
        }
        const char raw[] = "  Persona \xf0\x9f\x8f\x8e beyond sixteen bytes  ";
        OSWFixtureSetPersona(raw);
        osw_config config{};
        config.size = sizeof(config); config.abi_version = 1;
        std::strcpy(config.library_path, argv[1]); std::strcpy(config.online_id, "Explicit");
        std::strcpy(config.title, "fixture"); std::strcpy(config.build, "1"); std::strcpy(config.realm, "app");
        osw_context *context = nullptr;
        check(osw_create(&config, &context) == OSW_OK, "persona context");
        check(osw_enable_loopback_endpoints(context) == OSW_NOT_SUPPORTED, "ABI1 cannot enable P2P");
        std::size_t needed = 999; char buffer[256] = "unchanged";
        check(osw_get_persona_name(context, buffer, sizeof(buffer), &needed) == OSW_SIGNED_OUT && needed == 999,
              "persona signed out leaves output unchanged");
        check(osw_set_app_id(context, 12345) == OSW_OK && osw_start(context) == OSW_OK, "configured C API starts");
        const auto end = std::chrono::steady_clock::now() + std::chrono::seconds(4);
        osw_identity id{sizeof(id)};
        do {
            check(std::chrono::steady_clock::now() < end, "persona identity deadline");
            std::this_thread::sleep_for(std::chrono::milliseconds(5));
            osw_get_identity(context, &id);
        } while (!id.signed_in);
        check(osw_get_persona_name(context, nullptr, 0, &needed) == OSW_LIMIT && needed == sizeof(raw), "raw persona required size");
        check(osw_get_persona_name(context, buffer, 2, &needed) == OSW_LIMIT && std::strcmp(buffer, "unchanged") == 0,
              "short persona buffer unchanged");
        check(osw_get_persona_name(context, buffer, sizeof(buffer), &needed) == OSW_OK && std::strcmp(buffer, raw) == 0 &&
              std::strcmp(id.online_id, "Explicit") == 0, "raw persona keeps UTF8 and whitespace separate from explicit online ID");
        osw_destroy(context);
        std::cout << "Configured AppID environment and callback filters; raw copied persona passed\n";
    } catch (const std::exception &error) { std::cerr << error.what() << '\n'; return 1; }
}
