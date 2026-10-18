#include "core/backend.hpp"
#include "core/endpoints.hpp"
#include <atomic>
#include <cstring>
#include <iostream>
#include <stdexcept>
#include <thread>
using namespace osw::core;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
Endpoint point(unsigned n, unsigned port = 2000) {
    return {{{198, 18, static_cast<std::uint8_t>(n >> 8), static_cast<std::uint8_t>(n)}},
            static_cast<std::uint16_t>(port)};
}
void labels_and_inputs() {
    Config config{"", "Name", "5760", "1.21", "fixture"};
    const auto first = virtual_endpoint(42, config);
    check(first == virtual_endpoint(42, config), "stable endpoint label");
    check(first != virtual_endpoint(42 + (1ull << 32), config), "full account width retained");
    config.realm = "other";
    check(first != virtual_endpoint(42, config), "namespace changes endpoint");
    for (unsigned n = 1; n < 10000; ++n) {
        const auto value = virtual_endpoint(n, config);
        check(value.address[0] == 198 && (value.address[1] == 18 || value.address[1] == 19) &&
                  value.port >= 1024, "endpoint range");
        osw_endpoint encoded{};
        Endpoint decoded;
        copy_endpoint(value, encoded);
        check(read_endpoint(encoded, decoded) && value == decoded, "copied endpoint round trip");
    }
    osw_endpoint input{sizeof(input)};
    Endpoint parsed;
    input.port = 3074;
    for (const auto text : {"127.00.0.1", "256.0.0.1", "1.2.3", "1.2.3.4x", "-1.2.3.4"}) {
        std::strcpy(input.address, text);
        check(!read_endpoint(input, parsed), "malformed address rejected");
    }
    std::memset(input.address, '1', sizeof(input.address));
    check(!read_endpoint(input, parsed), "unterminated address rejected");
    std::strcpy(input.address, "127.0.0.1");
    input.port = 65536;
    check(!read_endpoint(input, parsed), "oversize port rejected");
    input.port = 0;
    check(!read_endpoint(input, parsed), "zero port rejected");
    config.loopback_endpoints = true;
    const auto loop = virtual_endpoint(42, config);
    check(loop == virtual_endpoint(42, config), "loopback canonical identity is stable");
    config.app_id = 481;
    check(loop != virtual_endpoint(42, config), "AppID changes loopback namespace");
    for (unsigned n = 1; n < 10000; ++n) {
        const auto value = virtual_endpoint(n, config);
        check(value.address[0] == 127 && value.address[1] >= 64 && value.address[1] < 128 &&
              value.port >= 1024, "canonical loopback reserved range and unprivileged port");
    }
}
void lifecycle_collision_and_bound() {
    EndpointRoutes routes;
    Endpoint output = point(99);
    check(routes.local_endpoint(output) == OSW_NOT_SUPPORTED, "legacy route opt out");
    routes.enable();
    check(routes.local_endpoint(output) == OSW_PENDING, "startup route pending");
    routes.initialize(point(0));
    routes.set_ready();
    check(routes.local_endpoint(output) == OSW_OK && output == point(0), "local endpoint copied");
    check(routes.permits(1, point(1)) &&
              routes.lookup(OSW_ROUTE_OUTBOUND, point(1), output) == OSW_NOT_FOUND,
          "pre-bind collision check publishes no route");
    check(routes.install(1, point(1), point(1, 3000)), "first route");
    check(routes.lookup(OSW_ROUTE_OUTBOUND, point(1), output) == OSW_OK && output == point(1, 3000),
          "outbound translation");
    check(routes.lookup(OSW_ROUTE_INBOUND, point(1, 3000), output) == OSW_OK && output == point(1),
          "inbound translation");
    routes.erase(1);
    check(routes.lookup(OSW_ROUTE_INBOUND, point(1, 3000), output) == OSW_NOT_FOUND &&
              routes.lookup(OSW_ROUTE_OUTBOUND, point(1), output) == OSW_NOT_FOUND,
          "revocation removes both directions");
    check(routes.install(1, point(1), point(1, 3001)), "fresh authorization installs route");
    check(!routes.install(2, point(1), point(2, 3000)) && !routes.contains(1) && !routes.contains(2),
          "canonical collision revokes both peers");
    check(!routes.install(1, point(3), point(1, 3001)) &&
              !routes.install(2, point(4), point(2, 3001)), "collision tombstones cannot revive");
    check(!routes.install(3, point(0), point(3, 3000)), "self collision fails closed");
    for (unsigned n = 4; n <= EndpointRoutes::limit; ++n)
        check(routes.install(n, point(n), point(n, 3000)), "bounded registry capacity");
    check(!routes.install(EndpointRoutes::limit + 1, point(2000), point(2000, 3000)),
          "registry refuses beyond fixed capacity including tombstones");
    routes.close();
    routes.set_ready();
    check(routes.local_endpoint(output) == OSW_UNAVAILABLE &&
              !routes.install(4, point(4), point(4, 3000)), "stop never revives mappings");
}
void concurrent_copies() {
    EndpointRoutes routes;
    routes.enable(); routes.initialize(point(0)); routes.set_ready();
    std::atomic<bool> done = false, bad = false;
    std::thread reader([&] {
        while (!done.load()) {
            Endpoint output;
            const auto status = routes.lookup(OSW_ROUTE_OUTBOUND, point(1), output);
            if (status != OSW_NOT_FOUND &&
                (status != OSW_OK || (output != point(1, 3000) && output != point(1, 3001))))
                bad = true;
        }
    });
    for (unsigned n = 0; n < 10000; ++n) {
        routes.install(1, point(1), point(1, 3000 + n % 2));
        routes.erase(1);
    }
    done = true; reader.join();
    check(!bad, "concurrent lookup returns a complete authorized snapshot or not found");
}
int main() {
    try {
        labels_and_inputs(); lifecycle_collision_and_bound(); concurrent_copies();
        std::cout << "Endpoint labels, copied routes, revocation, collisions and concurrency passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
