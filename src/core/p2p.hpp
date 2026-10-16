#pragma once
#include "core/backend.hpp"
#include "core/social.hpp"
#include <memory>
namespace osw::core {
class P2P {
  public:
    P2P(const Config &, const Identity &, Social &);
    ~P2P();
    bool handles(const Request &) const;
    void submit(Request);
    void process(const std::vector<SocialEvent> &);
    void set_party(Party);
    void cancel(std::uint64_t);
    Update poll();
    bool failed() const;

  private:
    struct Impl;
    std::unique_ptr<Impl> p;
};
} // namespace osw::core
