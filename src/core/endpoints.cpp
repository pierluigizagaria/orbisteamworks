#include "core/endpoints.hpp"
#include "core/backend.hpp"
#include <cstdio>
#include <cstring>

namespace osw::core {
Endpoint virtual_endpoint(std::uint64_t account, const Config &config) {
    // Stable FNV-1a over length-delimited namespace bytes and the full account
    // ID in little-endian order. This labels endpoints; it is not authentication.
    std::uint64_t hash = 14695981039346656037ull;
    const auto byte = [&](std::uint8_t value) { hash ^= value; hash *= 1099511628211ull; };
    for (const auto &part : {std::string("osw-virtual-endpoint-v1"), config.title,
                             config.build, config.realm}) {
        byte(static_cast<std::uint8_t>(part.size()));
        for (unsigned char value : part) byte(value);
    }
    for (int n = 0; n < 8; ++n) byte(static_cast<std::uint8_t>(account >> (8 * n)));
    constexpr std::uint64_t ports = 65536 - 1024, addresses = 131072;
    const auto index = hash % (ports * addresses);
    const auto address = index / ports;
    return {{{198, static_cast<std::uint8_t>(18 + (address >> 16)),
               static_cast<std::uint8_t>(address >> 8), static_cast<std::uint8_t>(address)}},
            static_cast<std::uint16_t>(1024 + index % ports)};
}
std::string endpoint_address(const Endpoint &value) {
    char text[16];
    std::snprintf(text, sizeof(text), "%u.%u.%u.%u", value.address[0], value.address[1],
                  value.address[2], value.address[3]);
    return text;
}
bool read_endpoint(const osw_endpoint &value, Endpoint &out) {
    if (value.size != sizeof(value) || !value.port || value.port > 65535)
        return false;
    std::size_t pos = 0;
    for (std::size_t part = 0; part < 4; ++part) {
        const auto start = pos;
        unsigned n = 0;
        while (pos < sizeof(value.address) && value.address[pos] >= '0' && value.address[pos] <= '9') {
            n = n * 10 + (value.address[pos++] - '0');
            if (n > 255 || pos - start > 3) return false;
        }
        if (pos == start || (pos - start > 1 && value.address[start] == '0') ||
            pos >= sizeof(value.address) || value.address[pos++] != (part == 3 ? '\0' : '.'))
            return false;
        out.address[part] = static_cast<std::uint8_t>(n);
    }
    out.port = static_cast<std::uint16_t>(value.port);
    return true;
}
void copy_endpoint(const Endpoint &value, osw_endpoint &out) {
    out = {};
    out.size = sizeof(out);
    std::snprintf(out.address, sizeof(out.address), "%u.%u.%u.%u", value.address[0], value.address[1],
                  value.address[2], value.address[3]);
    out.port = value.port;
}
void EndpointRoutes::enable() { std::lock_guard lock(mutex); enabled = true; }
void EndpointRoutes::initialize(Endpoint value) { std::lock_guard lock(mutex); local = value; }
void EndpointRoutes::set_ready() { std::lock_guard lock(mutex); if (!stopped) ready = true; }
void EndpointRoutes::close() {
    std::lock_guard lock(mutex);
    stopped = true;
    routes.clear(); outgoing.clear(); incoming.clear();
}
void EndpointRoutes::erase_locked(std::uint64_t peer) {
    if (const auto found = routes.find(peer); found != routes.end()) {
        outgoing.erase(found->second.canonical);
        incoming.erase(found->second.proxy);
        routes.erase(found);
    }
}
bool EndpointRoutes::install(std::uint64_t peer, Endpoint canonical, Endpoint proxy) {
    std::lock_guard lock(mutex);
    if (!enabled || stopped || !peer || collisions.contains(peer) ||
        (!routes.contains(peer) && routes.size() + collisions.size() >= limit))
        return false;
    const auto conflict = outgoing.find(canonical);
    if (canonical == local || (conflict != outgoing.end() && conflict->second != peer)) {
        if (conflict != outgoing.end()) {
            const auto other = conflict->second;
            erase_locked(other);
            collisions.insert(other);
        }
        erase_locked(peer);
        collisions.insert(peer);
        return false;
    }
    if (const auto existing = incoming.find(proxy);
        existing != incoming.end() && existing->second != peer)
        return false;
    if (const auto existing = routes.find(peer); existing != routes.end() &&
        existing->second.canonical == canonical && existing->second.proxy == proxy)
        return true;
    erase_locked(peer);
    routes.emplace(peer, Route{canonical, proxy});
    outgoing.emplace(canonical, peer);
    incoming.emplace(proxy, peer);
    return true;
}
void EndpointRoutes::erase(std::uint64_t peer) { std::lock_guard lock(mutex); erase_locked(peer); }
bool EndpointRoutes::contains(std::uint64_t peer) const {
    std::lock_guard lock(mutex);
    return !stopped && routes.contains(peer);
}
int EndpointRoutes::local_endpoint(Endpoint &out) const {
    std::lock_guard lock(mutex);
    if (!enabled) return OSW_NOT_SUPPORTED;
    if (stopped) return OSW_UNAVAILABLE;
    if (!ready) return OSW_PENDING;
    out = local;
    return OSW_OK;
}
int EndpointRoutes::lookup(std::uint32_t direction, Endpoint input, Endpoint &output) const {
    std::lock_guard lock(mutex);
    if (!enabled) return OSW_NOT_SUPPORTED;
    if (stopped) return OSW_UNAVAILABLE;
    if (!ready) return OSW_PENDING;
    const auto &index = direction == OSW_ROUTE_OUTBOUND ? outgoing : incoming;
    const auto found = index.find(input);
    if (found == index.end()) return OSW_NOT_FOUND;
    const auto &route = routes.at(found->second);
    output = direction == OSW_ROUTE_OUTBOUND ? route.proxy : route.canonical;
    return OSW_OK;
}
}
