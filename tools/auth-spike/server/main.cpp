#include "common.hpp"
#include <iostream>

int main(int argc, char **argv) {
    using namespace osw;
    using namespace osw::spike;
    try {
        auto opts = options(argc, argv, true);
        if (opts.has("--help")) {
            std::cout << "osw-auth-server [--library PATH] [--token PATH] [--timeout 30]\n"
                         "  [--repeat N] [--interval SECONDS] [--end-between] [--watch SECONDS]\n"
                         "  [--wait-before SECONDS] [--token-wait 60] [--ready-file PATH] "
                         "[--stop-file PATH]\n"
                         "Repeat counts total attempts. Interval starts after the prior attempt "
                         "completes.\n";
            return 0;
        }
        auto timeout = opts.seconds("--timeout", 30);
        auto repeat = opts.seconds("--repeat", 1);
        auto interval = opts.seconds("--interval", 0);
        auto watch = opts.seconds("--watch", 0);
        auto delay = opts.seconds("--wait-before", 0);
        auto token_wait = opts.seconds("--token-wait", 60);
        if (repeat < 1 || repeat > 100)
            throw Error(ErrorCode::arguments, "--repeat must be in [1,100]");
        install_stop_handlers();
        steam::Runtime runtime(opts.get("--library"));
        runtime.start_server();
        std::cout << "Server AppID=480 SDK ABI=1.65 dedicated=true authentication=2 "
                     "advertising=false query=shared-no-socket\n"
                  << "Steam API=" << runtime.library_path().string() << '\n'
                  << "steamclient=" << runtime.steamclient_path() << '\n';
        wait_until(
            runtime, opts, timeout,
            [&](const auto &events) {
                log_connection_events(events, true);
                return runtime.logged_on() && runtime.id() != 0;
            },
            "anonymous game-server logon");
        std::cout << "Anonymous server steam_id=" << runtime.id() << '\n';
        if (opts.has("--ready-file"))
            write_private(opts.get("--ready-file"), std::to_string(runtime.id()) + "\n");
        auto token_path = opts.get("--token", "runtime/auth-spike/ticket.txt");
        wait_until(
            runtime, opts, token_wait,
            [&](const auto &events) {
                log_connection_events(events, true);
                return std::filesystem::is_regular_file(token_path);
            },
            "token file");
        auto token = read_token(token_path);
        bool rejected = false;
        auto observe = [&](int seconds) {
            auto deadline = Clock::now() + std::chrono::seconds(seconds);
            while (!stopped(opts) && Clock::now() < deadline) {
                auto events = runtime.poll();
                log_connection_events(events, false);
                for (const auto &event : events)
                    if (event.kind == steam::Event::Kind::validation)
                        log_validation(
                            event, true); // Observations never disconnect an established session.
                tick();
            }
        };
        observe(delay);
        for (int attempt = 1; attempt <= repeat && !stopped(opts); ++attempt) {
            if (attempt > 1)
                observe(interval);
            if (stopped(opts))
                break;
            int result = runtime.begin_auth(token.bytes, token.user);
            std::cout << "Attempt=" << attempt << " BeginAuthSession result=" << result << " ("
                      << steam::begin_result_name(result) << ")\n";
            if (result == 0) {
                wait_until(
                    runtime, opts, timeout,
                    [&](const auto &events) {
                        log_connection_events(events, false);
                        bool matched = false;
                        for (const auto &event : events)
                            if (event.kind == steam::Event::Kind::validation) {
                                log_validation(event);
                                if (event.user == token.user) {
                                    matched = true;
                                    if (event.result != 0 || event.owner == 0)
                                        rejected = true;
                                }
                            }
                        return matched;
                    },
                    "ticket validation callback");
            } else
                rejected = true;
            if (opts.has("--end-between")) {
                runtime.end_auth(token.user);
                std::cout << "EndAuthSession between attempts\n";
                // Drain immediately queued notifications before starting a new request.
                observe(1);
            }
        }
        observe(watch);
        runtime.end_auth(token.user);
        std::cout << "Server stopping: EndAuthSession, LogOff, shutdown\n";
        return rejected ? static_cast<int>(ErrorCode::auth_rejected) : 0;
    } catch (const std::exception &error) {
        return report_error(error);
    }
}
