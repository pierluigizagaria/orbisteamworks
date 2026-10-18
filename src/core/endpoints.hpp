#pragma once
#include "orbisteamworks/osw.h"
#include <array>
#include <compare>
#include <cstdint>
#include <map>
#include <mutex>
#include <set>
#include <string>

namespace osw::core {
struct Config;
struct Endpoint {
    std::array<std::uint8_t, 4> address{};
    std::uint16_t port = 0;
    auto operator<=>(const Endpoint &) const = default;
};
Endpoint virtual_endpoint(std::uint64_t account, const Config &);
std::string endpoint_address(const Endpoint &);
bool read_endpoint(const osw_endpoint &, Endpoint &);
void copy_endpoint(const Endpoint &, osw_endpoint &);

// The worker publishes/revokes routes before sending UDP. Hosts only copy POD
// values under this mutex; no provider call, allocation or wait is made here.
class EndpointRoutes {
    mutable std::mutex mutex;
    bool enabled = false, ready = false, stopped = false;
    Endpoint local;
    struct Route { Endpoint canonical, proxy; };
    std::map<std::uint64_t, Route> routes;
    std::map<Endpoint, std::uint64_t> outgoing, incoming;
    std::set<std::uint64_t> collisions;
    void erase_locked(std::uint64_t);
    bool permitted_locked(std::uint64_t, Endpoint canonical);
  public:
    static constexpr std::size_t limit = 1024;
    void enable();
    void initialize(Endpoint);
    void set_ready();
    void close();
    bool install(std::uint64_t, Endpoint canonical, Endpoint proxy);
    bool permits(std::uint64_t, Endpoint canonical);
    void erase(std::uint64_t);
    bool contains(std::uint64_t) const;
    int local_endpoint(Endpoint &) const;
    int lookup(std::uint32_t direction, Endpoint input, Endpoint &output) const;
};
}
