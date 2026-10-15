#include "common.hpp"
#include <iostream>

int main(int argc, char **argv) {
    using namespace osw;
    using namespace osw::spike;
    try {
        auto opts = options(argc, argv, false);
        if (opts.has("--help")) {
            std::cout << "osw-auth-client [--library PATH] [--token PATH] [--online-id OswTest]\n"
                         "  [--timeout 30] [--cancel-after SECONDS] [--bind-server SteamID64]\n"
                         "  [--stop-file PATH]\n"
                         "Keeps pumping callbacks until Ctrl+C or the stop file exists.\n";
            return 0;
        }
        auto bound = opts.has("--bind-server") ? decimal_id(opts.get("--bind-server")) : 0;
        auto timeout = opts.seconds("--timeout", 30);
        auto cancel_after = opts.seconds("--cancel-after", 0);
        auto token_path = opts.get("--token", "runtime/auth-spike/ticket.txt");
        // Validate the name before interacting with Steam.
        encode_token({1, {1}, opts.get("--online-id", "OswTest")});
        install_stop_handlers();
        steam::Runtime runtime(opts.get("--library"));
        runtime.start_client();
        std::cout << "Client steam_id=" << runtime.id() << " AppID=480 SDK ABI=1.65\n"
                  << "Steam API=" << runtime.library_path().string() << '\n'
                  << "steamclient=" << runtime.steamclient_path() << '\n';
        auto ticket = runtime.request_ticket(bound);
        wait_until(
            runtime, opts, timeout,
            [&](const auto &events) {
                log_connection_events(events, false);
                for (const auto &event : events)
                    if (event.kind == steam::Event::Kind::shutdown_requested)
                        throw Error(ErrorCode::steam_shutdown,
                                    "Steam requested shutdown before the ticket was ready");
                for (const auto &event : events) {
                    if (event.kind == steam::Event::Kind::ticket && event.ticket == ticket.handle) {
                        std::cout << "GetAuthSessionTicketResponse result=" << event.result << '\n';
                        if (event.result != 1)
                            throw Error(ErrorCode::ticket, "Steam refused the session ticket");
                        return true;
                    }
                }
                return false;
            },
            "session ticket callback");
        auto token = encode_token({runtime.id(), ticket.bytes, opts.get("--online-id", "OswTest")});
        write_private(token_path, token + "\n");
        auto ticket_hex = hex(ticket.bytes);
        // Even unexpectedly short tickets must never be printed in full.
        auto preview = ticket_hex.size() > 16 ? ticket_hex.substr(0, 8) + "..." +
                                                    ticket_hex.substr(ticket_hex.size() - 8)
                                              : "[short ticket redacted]";
        std::cout << "Ticket ready token_length=" << token.size()
                  << " ticket_bytes=" << ticket.bytes.size() << " preview=" << preview << '\n'
                  << "Keep this process open.\n";
        auto issued = Clock::now();
        bool cancelled = false;
        while (!stopped(opts)) {
            auto events = runtime.poll();
            log_connection_events(events, false);
            bool shutdown = false;
            for (const auto &event : events)
                shutdown |= event.kind == steam::Event::Kind::shutdown_requested;
            if (shutdown) {
                std::cout << "Steam requested shutdown; stopping client cleanly\n";
                break;
            }
            if (!cancelled && opts.has("--cancel-after") &&
                Clock::now() - issued >= std::chrono::seconds(cancel_after)) {
                runtime.cancel_ticket(ticket.handle);
                cancelled = true;
                std::cout << "CancelAuthTicket called\n";
            }
            tick();
        }
        std::cout << "Client stopping: cancel outstanding tickets and shut down\n";
        return 0;
    } catch (const std::exception &error) {
        return report_error(error);
    }
}
