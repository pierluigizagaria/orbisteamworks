#include "orbisteamworks/osw.h"
#include <nlohmann/json.hpp>
#include <chrono>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using Json = nlohmann::json;
using Clock = std::chrono::steady_clock;
template <std::size_t N> void copy(char (&out)[N], const char *value) {
    auto size = std::strlen(value);
    if (size >= N)
        throw std::runtime_error("configuration string too long");
    std::memcpy(out, value, size + 1);
}
int main(int argc, char **argv) {
    osw_context *context = nullptr;
    try {
        if (argc != 2)
            throw std::runtime_error("usage: osw-p2p-check <user-supplied Steam API library>");
        osw_config_v2 config{};
        config.base.size = sizeof(config);
        config.base.abi_version = OSW_ABI_VERSION_2;
        copy(config.base.library_path, argv[1]);
        copy(config.base.title, "5760");
        copy(config.base.build, "01.21");
        copy(config.base.realm, "p2p-gate");
        copy(config.game_bind, "127.0.0.31");
        config.game_port = 3074;
        if (osw_create_v2(&config, &context) != OSW_OK || osw_start(context) != OSW_OK)
            throw std::runtime_error("context start failed");
        auto end = Clock::now() + std::chrono::seconds(15);
        osw_identity identity{sizeof(identity)};
        do {
            osw_get_identity(context, &identity);
            if (identity.signed_in)
                break;
            std::this_thread::sleep_for(std::chrono::milliseconds(20));
        } while (Clock::now() < end);
        if (!identity.signed_in)
            throw std::runtime_error("Steam P2P initialization did not sign in");
        auto request = [&](Json metadata) {
            auto encoded = metadata.dump();
            osw_request input{sizeof(input),  OSW_P2P, 30000, encoded.data(),
                              encoded.size(), nullptr, 0};
            std::uint64_t id = 0;
            if (osw_submit(context, &input, &id) != OSW_OK)
                throw std::runtime_error("P2P submission failed");
            osw_result output{sizeof(output)};
            auto deadline = Clock::now() + std::chrono::seconds(31);
            int status;
            while ((status = osw_get_result(context, id, &output)) == OSW_PENDING &&
                   Clock::now() < deadline)
                std::this_thread::sleep_for(std::chrono::milliseconds(20));
            if (status != OSW_OK || output.status != OSW_OK)
                throw std::runtime_error("P2P request failed");
            auto value = Json::parse(output.metadata, output.metadata + output.metadata_size);
            osw_release(context, id);
            return value;
        };
        auto room = request({{"op", "directory.create"},
                             {"metadata", {{"playlist", "osw-p4-check"}, {"type", "probe"}}}});
        if (room["owner"] != std::to_string(identity.account_id) || room["members"].size() != 1)
            throw std::runtime_error("owner/member readback mismatch");
        std::cout << "PASS live invisible lobby creation, namespace and owner/member readback\n";
        auto found = request({{"op", "directory.search"},
                              {"metadata", {{"playlist", "osw-p4-check"}, {"type", "probe"}}}});
        std::cout << "PASS live serialized distance-widened search; matching rooms="
                  << found["rooms"].size() << "\n";
        request(
            {{"op", "directory.update"},
             {"room_id", room["room_id"]},
             {"metadata", {{"playlist", "osw-p4-check"}, {"type", "probe"}, {"free_slots", 7}}}});
        auto current = request({{"op", "directory.members"}, {"room_id", room["room_id"]}});
        if (current["metadata"]["free_slots"] != 7)
            throw std::runtime_error("metadata readback mismatch");
        request({{"op", "directory.leave"}, {"room_id", room["room_id"]}});
        osw_destroy(context);
        std::cout << "PASS live metadata update and leave; two-peer transport remains untested\n";
        return 0;
    } catch (const std::exception &error) {
        osw_destroy(context);
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
