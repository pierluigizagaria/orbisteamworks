#pragma once
#include <cerrno>
#include <cstdint>

namespace osw::core {
struct ChildWaitResult {
    std::int64_t pid;
    int error;
};

// Returns true only while waitpid confirms the child is still running.
// The syscall is supplied separately so ownership loss can be tested off POSIX.
template <class Pid, class Wait> bool wait_owned_child(Pid &process, Wait wait) {
    if (process <= 0)
        return false;
    for (;;) {
        auto result = wait(process);
        if (result.pid < 0 && result.error == EINTR)
            continue;
        if (result.pid == process || (result.pid < 0 && result.error == ECHILD)) {
            process = -1;
            return false;
        }
        if (result.pid == 0)
            return true;
        // Do not signal a PID when a non-retryable wait failure prevents checking ownership.
        process = -1;
        return false;
    }
}
} // namespace osw::core
