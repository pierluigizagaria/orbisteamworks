#pragma once
#include "core/backend.hpp"
#include "core/social.hpp"
#include <memory>

namespace osw::core {
// Opaque replacement-platform ID; at most 58 bytes even for full-width inputs.
std::string make_invitation_id(std::uint64_t lobby, std::uint64_t sender, std::uint64_t sequence);
class Platform {
  public:
    Platform(Config config, Identity identity, Social &social);
    ~Platform();
    bool start();
    bool update_name(const std::string &name);
    void submit(Request request);
    void cancel(std::uint64_t request);
    void process(const std::vector<SocialEvent> &events);
    Update take_update();
    Party party();
    void stop();

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace osw::core
