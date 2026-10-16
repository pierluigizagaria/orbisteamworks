#pragma once
#include "orbisteamworks/osw.h"
#include <cstdint>
#include <memory>
#include <string>
#include <vector>

namespace osw::core {
struct Config {
    std::string library_path, online_id, title, build, realm;
    bool diag_all_friends = false;
    bool p2p = false;
    std::uint32_t backend = 0, game_port = 3074;
    std::uint64_t lab_account_id = 0;
    std::string lab_root, companion_path, companion_working_dir, companion_environment, game_bind;
};
struct Identity {
    bool signed_in = false;
    std::uint64_t account_id = 0;
    std::string online_id, reason;
    int reason_code = 0;
};
struct Request {
    std::uint64_t id = 0;
    std::uint32_t operation = 0, timeout_ms = 30000;
    std::string metadata;
    std::vector<std::uint8_t> body;
};
struct Result {
    std::uint64_t id = 0;
    int status = OSW_OK, http_status = 0;
    std::string metadata;
    std::vector<std::uint8_t> body;
};
struct Event {
    std::uint32_t type = 0;
    std::uint64_t request_id = 0, account_id = 0;
    std::string session_id, invitation_id, online_id;
    std::uint32_t change_kind = 0;
};
struct Update {
    std::vector<Result> results;
    std::vector<Event> events;
    bool failed = false;
};
class Backend {
  public:
    virtual ~Backend() = default;
    virtual void start(const Config &) = 0;
    virtual Identity identity() const = 0;
    virtual void submit(Request) = 0;
    virtual void cancel(std::uint64_t) = 0;
    virtual Update poll() = 0;
    virtual void stop() = 0;
};
// Internal injection seam for contract fixtures; never selected by public
// config.
osw_context *create_context(Config, std::unique_ptr<Backend>);
} // namespace osw::core
