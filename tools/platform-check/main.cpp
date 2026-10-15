#include "common.hpp"
#include "orbisteamworks/osw.h"
#include <cstring>
#include <iostream>
#include <nlohmann/json.hpp>
#include <thread>

namespace {
using Json = nlohmann::json;
struct Context {
    osw_context *value = nullptr;
    ~Context() { osw_destroy(value); }
};
struct Reply {
    int status = OSW_UNAVAILABLE, http = 0;
    std::string body, metadata;
};
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
Reply call(osw_context *context, unsigned op, const Json &metadata, const std::string &body = {}) {
    const auto encoded = metadata.dump();
    osw_request request{};
    request.size = sizeof(request);
    request.operation = op;
    request.timeout_ms = 30000;
    request.metadata = encoded.data();
    request.metadata_size = encoded.size();
    request.body = reinterpret_cast<const std::uint8_t *>(body.data());
    request.body_size = body.size();
    std::uint64_t id = 0;
    check(osw_submit(context, &request, &id) == OSW_OK, "request submission failed");
    for (;;) {
        osw_result result{};
        result.size = sizeof(result);
        auto status = osw_get_result(context, id, &result);
        if (status != OSW_PENDING) {
            check(status == OSW_OK, "request result unavailable");
            Reply reply{result.status, result.http_status, {}};
            if (result.body_size)
                reply.body.assign(reinterpret_cast<const char *>(result.body), result.body_size);
            if (result.metadata_size)
                reply.metadata.assign(result.metadata, result.metadata_size);
            osw_release(context, id);
            return reply;
        }
        std::this_thread::sleep_for(std::chrono::milliseconds(10));
    }
}
Reply web(osw_context *context, const char *method, const std::string &path,
          const std::string &body = {}, const std::string &type = "application/json") {
    return call(context, OSW_WEBAPI,
                Json{{"method", method}, {"path", path}, {"content_type", type}}, body);
}
std::string part(const std::string &name, const std::string &bytes) {
    return "--check\r\nContent-Description: " + name +
           "\r\nContent-Length: " + std::to_string(bytes.size()) + "\r\n\r\n" + bytes + "\r\n";
}
} // namespace
int main(int argc, char **argv) {
    try {
        const auto options = osw::spike::options(argc, argv, false);
        osw::spike::install_stop_handlers();
        Context context;
        osw_config config{};
        config.size = sizeof(config);
        config.abi_version = OSW_ABI_VERSION;
        auto library = options.get("--library");
        auto name = options.get("--online-id", "OswTest");
        check(library.size() < sizeof(config.library_path) &&
                  name.size() < sizeof(config.online_id),
              "configuration too long");
        std::memcpy(config.library_path, library.c_str(), library.size() + 1);
        std::memcpy(config.online_id, name.c_str(), name.size() + 1);
        std::memcpy(config.title, "5760", 5);
        std::memcpy(config.build, "1.21", 5);
        std::memcpy(config.realm, "local-test", 11);
        check(osw_create(&config, &context.value) == OSW_OK && osw_start(context.value) == OSW_OK,
              "C API startup failed");
        auto deadline = std::chrono::steady_clock::now() + std::chrono::seconds(30);
        for (;;) {
            osw_identity identity{};
            identity.size = sizeof(identity);
            check(osw_get_identity(context.value, &identity) == OSW_OK, "identity query failed");
            if (identity.signed_in)
                break;
            if (identity.reason_code) {
                std::cerr << "Identity unavailable, reason=" << identity.reason_code << '\n';
                return 2;
            }
            check(std::chrono::steady_clock::now() < deadline, "identity startup timed out");
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        }
        if (options.has("--bind-server")) {
            auto result = call(context.value, OSW_TOKEN,
                               Json{{"client_id", "orbiSmoke"},
                                    {"scope", "openid"},
                                    {"server_id", options.get("--bind-server")}});
            check(result.status == OSW_OK, "C API ticket issuance failed");
            osw::spike::write_private(options.get("--token"), result.body);
            std::cout << "C API bound credential issued, bytes=" << result.body.size() << '\n';
            while (!osw::spike::stopped(options))
                osw::spike::tick();
            return 0;
        }
        auto friends = call(context.value, OSW_FRIENDS, Json::object());
        check(friends.status == OSW_OK, "friend enumeration failed");
        Json description{
            {"sessionType", "owner-bind"},   {"sessionPrivacy", "private"},
            {"sessionMaxUser", 8},           {"sessionLockFlag", false},
            {"availablePlatforms", {"PS4"}}, {"sessionName", "orbiSteamworks local check"}};
        const auto data = std::string("\0\1\2\3\4", 5);
        const auto image = std::string(52096, 'i');
        const auto body = part("session-request", description.dump()) +
                          part("session-image", image) + part("session-data", data) +
                          part("changeable-session-data", std::string(10, 'c')) + "--check--\r\n";
        auto created =
            web(context.value, "POST", "/v1/sessions", body, "multipart/mixed; boundary=check");
        std::cout << "Creation status=" << created.status << " http=" << created.http << '\n';
        auto diagnostic = Json::parse(created.metadata, nullptr, false);
        if (diagnostic.is_object() && diagnostic.contains("stage"))
            std::cout << "Creation diagnostic=" << diagnostic.dump() << '\n';
        check(created.status == OSW_OK && created.http == 200, "private lobby creation failed");
        auto lobby = Json::parse(created.body).at("sessionId").get<std::string>();
        auto base = "/v1/sessions/" + lobby;
        auto loaded = web(context.value, "GET", base + "/sessionData");
        check(loaded.status == OSW_OK && loaded.http == 200 && loaded.body == data,
              "binary data round trip failed");
        auto loaded_image = web(context.value, "GET", base + "/sessionImage");
        check(loaded_image.status == OSW_OK && loaded_image.http == 200 &&
                  loaded_image.body == image,
              "image round trip failed");
        auto info = web(context.value, "GET", base);
        check(info.http == 200 && Json::parse(info.body)["members"].size() == 1,
              "committed creator missing");
        auto changed = web(context.value, "PUT", base + "/changeableSessionData",
                           std::string(10, 'n'), "application/octet-stream");
        check(changed.http == 204, "binary update failed");
        auto read = web(context.value, "GET", base + "/changeableSessionData");
        check(read.http == 200 && read.body == std::string(10, 'n'),
              "binary update round trip failed");
        auto deleted = web(context.value, "DELETE", base);
        check(deleted.http == 204, "private lobby cleanup failed");
        std::cout << "C API live private lobby: create, namespace, committed creator, binary data, "
                     "52096-byte image, update and delete passed\n";
        std::cout
            << "This is one-account local readback, not remote visibility or game acceptance\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
