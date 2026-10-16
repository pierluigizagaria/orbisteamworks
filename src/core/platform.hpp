#pragma once
#include "core/backend.hpp"
#include "core/social.hpp"
#include <memory>

namespace osw::core {
class Platform {
  public:
    Platform(Config config, Identity identity, Social &social);
    ~Platform();
    bool start();
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
