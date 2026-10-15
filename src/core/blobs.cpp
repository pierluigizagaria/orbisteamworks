#include "core/blobs.hpp"
#include <algorithm>

namespace osw::core {
namespace {
constexpr size_t max_copy = 2 * 1024 * 1024, max_total = 16 * 1024 * 1024, chunk = 2000;
bool valid_key(const std::string &key) { return key == "session-image" || key == "session-data"; }
bool encoded(const std::string &bytes) {
    return bytes.size() % 2 == 0 && std::all_of(bytes.begin(), bytes.end(), [](char c) {
               return (c >= '0' && c <= '9') || (c >= 'a' && c <= 'f');
           });
}
std::string string(const nlohmann::json &json, const char *key) {
    auto it = json.find(key);
    return it != json.end() && it->is_string() ? it->get<std::string>() : std::string{};
}
} // namespace
std::string Blobs::checksum(std::string_view bytes) {
    Id hash = 14695981039346656037ull;
    for (unsigned char c : bytes) {
        hash ^= c;
        hash *= 1099511628211ull;
    }
    return std::to_string(hash);
}
bool Blobs::reserve(Id lobby, const std::string &key, size_t size) const {
    if (!valid_key(key) || size > max_copy)
        return false;
    size_t total = size;
    for (const auto &[k, copy] : copies)
        if (k != std::pair{lobby, key})
            total += copy.size;
    return total <= max_total;
}
bool Blobs::install(Id lobby, const std::string &key, const std::string &bytes) {
    if (!reserve(lobby, key, bytes.size()) || !encoded(bytes))
        return false;
    auto &copy = copies[{lobby, key}];
    copy = {};
    copy.hash = checksum(bytes);
    copy.bytes = bytes;
    copy.size = bytes.size();
    copy.complete = true;
    return true;
}
void Blobs::request(const std::pair<Id, std::string> &key, Copy &copy) {
    Json packet{{"osw", "blob/1"},
                {"kind", "get"},
                {"key", key.second},
                {"hash", copy.hash},
                {"nonce", std::to_string(copy.nonce)},
                {"offset", copy.bytes.size()}};
    social.send_message(key.first, packet.dump());
    copy.retry = Clock::now() + std::chrono::seconds(1);
}
int Blobs::ensure(Id lobby, const Json &references) {
    if (!references.is_object() || references.size() > 2)
        return -1;
    bool ready = true;
    for (auto it = references.begin(); it != references.end(); ++it) {
        const auto &ref = it.value();
        if (!valid_key(it.key()) || !ref.is_object() || !ref.contains("size") ||
            !ref["size"].is_number_unsigned())
            return -1;
        const auto size = ref["size"].get<Id>();
        const auto hash = string(ref, "hash");
        if (!size || size % 2 || size > max_copy || hash.empty() || hash.size() > 20)
            return -1;
        const auto key = std::pair{lobby, it.key()};
        auto found = copies.find(key);
        if (found == copies.end() || found->second.hash != hash || found->second.size != size) {
            if (!reserve(lobby, it.key(), static_cast<size_t>(size)))
                return -1;
            auto &copy = copies[key];
            copy = {};
            copy.hash = hash;
            copy.size = static_cast<size_t>(size);
            copy.source = social.owner(lobby);
            copy.nonce = ++nonce;
            copy.deadline = Clock::now() + std::chrono::seconds(20);
            if (!copy.source || copy.source == self) {
                copy.failed = true;
                return -1;
            }
            copy.bytes.reserve(copy.size);
            request(key, copy);
            found = copies.find(key);
        }
        if (found->second.failed)
            return -1;
        ready &= found->second.complete;
    }
    return ready ? 1 : 0;
}
std::string Blobs::get(Id lobby, const std::string &key) const {
    const auto found = copies.find({lobby, key});
    return found != copies.end() && found->second.complete ? found->second.bytes : std::string{};
}
void Blobs::message(const SocialEvent &event) {
    if (event.payload.size() > 4096)
        return;
    const auto packet = Json::parse(event.payload, nullptr, false);
    if (!packet.is_object() || string(packet, "osw") != "blob/1" || !packet.contains("offset") ||
        !packet["offset"].is_number_unsigned())
        return;
    const auto key = string(packet, "key"), hash = string(packet, "hash"),
               serial = string(packet, "nonce");
    if (!valid_key(key) || serial.empty() || serial.size() > 20)
        return;
    const auto found = copies.find({event.lobby, key});
    if (found == copies.end() || found->second.hash != hash)
        return;
    auto &copy = found->second;
    const auto offset = packet["offset"].get<Id>();
    if (string(packet, "kind") == "get") {
        if (social.owner(event.lobby) != self || !copy.complete || offset >= copy.size ||
            offset % chunk)
            return;
        if (Clock::now() - rate_window >= std::chrono::seconds(1)) {
            rate_window = Clock::now();
            responses = 0;
        }
        if (responses >= 200)
            return;
        ++responses;
        Json response{
            {"osw", "blob/1"},  {"kind", "data"},
            {"key", key},       {"hash", hash},
            {"nonce", serial},  {"to", std::to_string(event.user)},
            {"offset", offset}, {"bytes", copy.bytes.substr(static_cast<size_t>(offset), chunk)}};
        social.send_message(event.lobby, response.dump());
    } else if (string(packet, "kind") == "data") {
        if (copy.complete || copy.failed || event.user != copy.source ||
            event.user != social.owner(event.lobby) ||
            string(packet, "to") != std::to_string(self) || serial != std::to_string(copy.nonce) ||
            offset != copy.bytes.size())
            return;
        const auto bytes = string(packet, "bytes");
        if (bytes.size() != std::min(chunk, copy.size - copy.bytes.size()) || !encoded(bytes))
            return;
        copy.bytes += bytes;
        if (copy.bytes.size() == copy.size) {
            copy.complete = checksum(copy.bytes) == copy.hash;
            copy.failed = !copy.complete;
        } else
            request({event.lobby, key}, copy);
    }
}
void Blobs::tick() {
    for (auto &[key, copy] : copies) {
        if (copy.complete || copy.failed)
            continue;
        if (Clock::now() >= copy.deadline || copy.source != social.owner(key.first)) {
            copy.failed = true;
            continue;
        }
        if (Clock::now() >= copy.retry)
            request(key, copy);
    }
}
void Blobs::erase(Id lobby) {
    std::erase_if(copies, [&](const auto &item) { return item.first.first == lobby; });
}
} // namespace osw::core
