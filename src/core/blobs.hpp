#pragma once
#include "core/social.hpp"
#include <chrono>
#include <map>
#include <nlohmann/json.hpp>

namespace osw::core {
// Lobby messages carry immutable payloads which exceed the metadata budget.
// Hashes detect inconsistent copies; the current lobby owner authenticates the source.
class Blobs {
  public:
    using Id = std::uint64_t;
    using Json = nlohmann::json;
    Blobs(Social &social, Id self) : social(social), self(self) {}
    static std::string checksum(std::string_view bytes);
    bool install(Id lobby, const std::string &key, const std::string &encoded);
    int ensure(Id lobby, const Json &references);
    std::string get(Id lobby, const std::string &key) const;
    void message(const SocialEvent &event);
    void tick();
    void erase(Id lobby);

  private:
    using Clock = std::chrono::steady_clock;
    struct Copy {
        std::string hash, bytes;
        size_t size = 0;
        Id source = 0, nonce = 0;
        bool complete = false, failed = false;
        Clock::time_point deadline, retry;
    };
    Social &social;
    Id self, nonce = 0;
    std::map<std::pair<Id, std::string>, Copy> copies;
    Clock::time_point rate_window{};
    unsigned responses = 0;
    bool reserve(Id lobby, const std::string &key, size_t size) const;
    void request(const std::pair<Id, std::string> &key, Copy &copy);
};
} // namespace osw::core
