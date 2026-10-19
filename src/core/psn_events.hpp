#pragma once
#include "backend.hpp"
#include <deque>

namespace osw::core {
// Owned by the context mutex. This opt-in queue never shares consumption with
// legacy events and never calls consumer code.
class PsnEvents {
  public:
    int enable(std::uint32_t version) noexcept;
    void append(const Event &, const Identity &) noexcept;
    int poll(char *buffer, std::size_t capacity, std::size_t *required) noexcept;

  private:
    void fail() noexcept;
    std::deque<std::string> queue;
    std::size_t bytes = 0;
    bool enabled = false, failed = false;
};
} // namespace osw::core
