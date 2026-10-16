#pragma once
#include <stdexcept>
namespace osw::lab {
enum class Failure {
    read_open, read_io, read_busy, missing, invalid, limit, lock, busy,
    write, replace, receiver_overflow, identity_active, identity_lost, platform_start
};
inline const char *failure_reason(Failure value) noexcept {
    switch (value) {
    case Failure::read_open: return "Lab hub read unavailable";
    case Failure::read_io: return "Lab hub read failed";
    case Failure::read_busy: return "Lab hub read sharing timeout";
    case Failure::missing: return "Lab active hub missing";
    case Failure::invalid: return "Lab hub state invalid";
    case Failure::limit: return "Lab hub size limit";
    case Failure::lock: return "Lab hub lock unavailable";
    case Failure::busy: return "Lab hub lock timeout";
    case Failure::write: return "Lab hub write failed";
    case Failure::replace: return "Lab hub replacement failed";
    case Failure::receiver_overflow: return "Lab receiver overflow";
    case Failure::identity_active: return "Lab identity already active";
    case Failure::identity_lost: return "Lab identity lost";
    case Failure::platform_start: return "Lab platform startup failed";
    }
    return "Lab backend failed";
}
class FailureError final : public std::runtime_error {
  public:
    explicit FailureError(Failure value) : std::runtime_error(failure_reason(value)), failure(value) {}
    Failure failure;
};
} // namespace osw::lab
