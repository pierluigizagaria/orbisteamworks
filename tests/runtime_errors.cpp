#include "orbisteamworks/osw.h"
#include "steam/runtime.hpp"
#include "exception_observer.hpp"
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>

namespace {
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void wait(F ready) {
    const auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(3);
    while (!ready()) {
        check(std::chrono::steady_clock::now() < deadline, "runtime error wait exceeded deadline");
        std::this_thread::sleep_for(std::chrono::milliseconds(2));
    }
}
struct Context {
    osw_context *value = nullptr;
    ~Context() { osw_destroy(value); }
};
}
int main(int argc, char **argv) {
    try {
        check(argc == 4, "expected library, operation and reason");
        ExceptionObserver exceptions;
        const auto operation = std::string(argv[2]);
        const int reason = std::stoi(argv[3]);
        if (operation == "members") {
            auto runtime = osw::steam::Runtime::create(argv[1]);
            check(runtime->try_start_client() && runtime->start_social(), "fixture startup failed");
            check(runtime->members(1).empty() && runtime->error() &&
                      static_cast<int>(runtime->error()->code) == reason,
                  "invalid native count must set an explicit error");
            runtime.reset();
        } else {
            osw_config config{};
            config.size = sizeof(config);
            config.abi_version = OSW_ABI_VERSION;
            check(std::strlen(argv[1]) < sizeof(config.library_path), "test path too long");
            std::strcpy(config.library_path, argv[1]);
            if (operation == "invalid-path") {
                config.library_path[0] = static_cast<char>(0xff);
                config.library_path[1] = 0;
            }
            std::strcpy(config.online_id, "FixtureUser");
            std::strcpy(config.title, "5760");
            std::strcpy(config.build, "1.21");
            std::strcpy(config.realm, "offline-fixture");
            Context context;
            check(osw_create(&config, &context.value) == OSW_OK, "create fixture context");
            check(osw_start(context.value) == OSW_OK, "start fixture context");
            osw_identity identity{};
            identity.size = sizeof(identity);
            wait([&] {
                check(osw_get_identity(context.value, &identity) == OSW_OK, "read identity");
                return identity.signed_in || identity.reason_code;
            });
            if (operation == "startup" || operation == "invalid-path") {
                check(!identity.signed_in && identity.reason_code == reason,
                      "startup error must preserve its distinct reason");
            } else {
                check(identity.signed_in, "native fixture must start signed in");
                for (int attempt = 0; attempt < (operation == "ticket" ? 2 : 1); ++attempt) {
                    const std::string metadata =
                        R"({"client_id":"fixture","scope":"openid","server_id":"2"})";
                    osw_request request{sizeof(request), OSW_TOKEN, 1000,
                                        metadata.data(), metadata.size(), nullptr, 0};
                    std::uint64_t id = 0;
                    check(osw_submit(context.value, &request, &id) == OSW_OK, "submit token");
                    osw_result result{};
                    result.size = sizeof(result);
                    wait([&] { return osw_get_result(context.value, id, &result) == OSW_OK; });
                    check(result.status == OSW_UNAVAILABLE && !result.body_size,
                          "failed ticket/callback must not publish a credential");
                    check(osw_release(context.value, id) == OSW_OK, "release failed result");
                }
                check(osw_get_identity(context.value, &identity) == OSW_OK, "read final identity");
                if (operation == "ticket")
                    check(identity.signed_in, "ticket error must leave retry available");
                else
                    check(!identity.signed_in && identity.reason_code == reason,
                          "callback failure must terminate backend explicitly");
            }
        }
        check(exceptions.quiet(), "expected runtime failure raised a first-chance exception");
        std::cout << "Runtime failure reported without first-chance exceptions\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
