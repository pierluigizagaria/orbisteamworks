// Offline fixtures only; no Steam client, rendering window or host callbacks.
#include "steam/runtime.hpp"
#include "orbisteamworks/osw.h"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
extern "C" void OSWFixtureOverlayCallback(unsigned, unsigned, unsigned, int);
extern "C" int OSWFixtureOverlayOpens();
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        check(argc == 3, "fixture path and scenario required");
        const std::string mode = argv[2];
        {
            osw::steam::Runtime runtime(argv[1]);
            runtime.start_client();
            check(runtime.start_social(), "platform starts with optional overlay capability");
            auto status = runtime.overlay_status();
            if (mode == "overlay-unsupported") {
                check(!status.supported && !runtime.overlay_friends() && !runtime.overlay_invite(9001),
                      "missing utility interface never fabricates overlay support");
                return 0;
            }
            check(status.supported && !status.active, "overlay begins inactive");
            if (mode == "overlay-disabled") {
                check(!status.available && !runtime.overlay_friends() && !runtime.overlay_invite(9001) &&
                          !OSWFixtureOverlayOpens(), "disabled overlay never dispatches");
                return 0;
            }
            check(status.available && runtime.overlay_friends() && runtime.overlay_invite(9001) &&
                      OSWFixtureOverlayOpens() == 2, "flat overlay functions receive exact arguments");
            check(!runtime.overlay_status().active, "void dispatch does not invent active UI");
            OSWFixtureOverlayCallback(1, 1, 481, 12);
            runtime.poll();
            check(runtime.take_social_events().empty() && !runtime.overlay_status().active,
                  "foreign AppID callback ignored");
            for (unsigned active : {1u, 0u}) {
                OSWFixtureOverlayCallback(active, 1, 480, 12);
                runtime.poll();
                const auto events = runtime.take_social_events();
                check(events.size() == 1 && events[0].kind == osw::core::SocialEvent::overlay_changed &&
                          events[0].detail == static_cast<int>(active) &&
                          runtime.overlay_status().active == (active != 0), "validated lifecycle copied");
            }
        }
        for (int malformed : {1, 2, 3}) {
            osw::steam::Runtime runtime(argv[1]);
            runtime.start_client();
            check(runtime.start_social(), "malformed callback fixture starts");
            OSWFixtureOverlayCallback(malformed == 2 ? 2u : 1u, malformed == 3 ? 2u : 1u,
                                      480, malformed == 1 ? 1 : 12);
            runtime.try_poll();
            check(runtime.error() && runtime.error()->code == osw::ErrorCode::callback_abi,
                  "invalid callback layout or boolean fails visibly");
        }
        // Public C API dispatch goes through its worker; new enum values change no layouts.
        osw_config config{};
        config.size = sizeof(config); config.abi_version = OSW_ABI_VERSION;
        std::strcpy(config.library_path, argv[1]);
        std::strcpy(config.title, "fixture"); std::strcpy(config.build, "1");
        std::strcpy(config.realm, "overlay");
        osw_context *context = nullptr;
        check(osw_create(&config, &context) == OSW_OK && osw_start(context) == OSW_OK, "C API starts");
        for (const auto action : {"status", "friends"}) {
            const auto meta = std::string("{\"action\":\"") + action + "\"}";
            osw_request request{sizeof(request), OSW_OVERLAY, 2000, meta.data(), meta.size()};
            std::uint64_t id = 0;
            check(osw_submit(context, &request, &id) == OSW_OK, "additive operation accepted");
            osw_result result{sizeof(result)};
            const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
            int state;
            while ((state = osw_get_result(context, id, &result)) == OSW_PENDING &&
                   std::chrono::steady_clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(1));
            check(state == OSW_OK && result.status == OSW_OK &&
                      result.http_status == (!std::strcmp(action, "status") ? 200 : 204),
                  "worker overlay operation completes without UI acknowledgement");
            osw_release(context, id);
        }
        osw_destroy(context);
        std::cout << "Offline overlay dispatch/callback/ABI contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
