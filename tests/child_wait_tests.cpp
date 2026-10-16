#include "core/child_wait.hpp"
#include <iostream>
#include <stdexcept>

using osw::core::ChildWaitResult;
void check(bool ok, const char *message) {
    if (!ok)
        throw std::runtime_error(message);
}
int main() {
    try {
        int process = 123, calls = 0, signals = 0;
        auto externally_reaped = [&](int pid) {
            check(pid == 123, "wait targets owned child");
            ++calls;
            return ChildWaitResult{-1, calls == 1 ? EINTR : ECHILD};
        };
        check(!osw::core::wait_owned_child(process, externally_reaped),
              "host-reaped child is no longer owned");
        if (process > 0)
            ++signals;
        check(process == -1 && calls == 2 && signals == 0,
              "interrupted wait retries and ECHILD prevents signaling stale PID");
        check(!osw::core::wait_owned_child(process,
                                           [&](int) {
                                               throw std::runtime_error(
                                                   "released child must not be waited again");
                                               return ChildWaitResult{};
                                           }),
              "released child stays released");

        process = 456;
        calls = 0;
        check(osw::core::wait_owned_child(process,
                                          [&](int) {
                                              ++calls;
                                              return calls < 3 ? ChildWaitResult{-1, EINTR}
                                                               : ChildWaitResult{0, 0};
                                          }),
              "running child retains ownership after interruptions");
        check(process == 456 && calls == 3, "running child can still be shut down");
        calls = 0;
        check(!osw::core::wait_owned_child(process,
                                           [&](int) {
                                               ++calls;
                                               return calls == 1 ? ChildWaitResult{-1, EINTR}
                                                                 : ChildWaitResult{456, 0};
                                           }),
              "post-termination reap retries interrupted wait");
        check(process == -1 && calls == 2, "reaped child ownership released");

        process = 789;
        check(!osw::core::wait_owned_child(process,
                                           [](int) { return ChildWaitResult{-1, EINVAL}; }) &&
                  process == -1,
              "unexpected wait errors cannot authorize a signal");
        std::cout << "PASS child wait ownership: reaped, interrupted, running and error paths\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << '\n';
        return 1;
    }
}
