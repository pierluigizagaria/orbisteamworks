#include "steam/backend.hpp"
#include "core/credential.hpp"
#include "core/p2p.hpp"
#include "core/platform.hpp"
#include "steam/runtime.hpp"
#include <nlohmann/json.hpp>
#include <optional>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

namespace osw::steam {
namespace {
class SteamBackend final : public core::Backend {
  public:
    void start(const core::Config &value) override {
        config = value;
#ifdef _WIN32
        if (!config.library_path.empty() &&
            !MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, config.library_path.data(),
                                 static_cast<int>(config.library_path.size()), nullptr, 0)) {
            identity_value.reason = "Invalid UTF-8 Steam library path";
            identity_value.reason_code = static_cast<int>(ErrorCode::arguments);
            failed = true;
            return;
        }
#endif
        auto utf8 = std::u8string(config.library_path.begin(), config.library_path.end());
        runtime = Runtime::create(std::filesystem::path(utf8));
        if (!runtime->try_start_client() || !runtime->start_social()) {
            fail_runtime();
            return;
        }
        const auto account = runtime->id();
        identity_value = {true,
                          account,
                          config.online_id.empty()
                              ? core::online_id_from_persona(runtime->persona(), account)
                              : config.online_id,
                          {},
                          0};
        platform = std::make_unique<core::Platform>(config, identity_value, *runtime);
        if (!platform->start()) {
            stop();
            identity_value.reason = "Presence publication failed";
            identity_value.reason_code = static_cast<int>(ErrorCode::arguments);
            failed = true;
        }
        if (!failed && config.p2p) {
            if (!runtime->start_p2p()) {
                fail_runtime();
                return;
            }
            p2p = std::make_unique<core::P2P>(config, identity_value, *runtime);
        }
        fail_runtime();
    }
    core::Identity identity() const override { return identity_value; }
    void submit(core::Request request) override {
        if (!runtime || !identity_value.signed_in) {
            finish(request.id, OSW_SIGNED_OUT);
            return;
        }
        if (p2p && p2p->handles(request)) {
            p2p->submit(std::move(request));
            return;
        }
        if (request.operation != OSW_TOKEN) {
            platform->submit(std::move(request));
            fail_runtime();
            return;
        }
        auto json = nlohmann::json::parse(request.metadata, nullptr, false);
        if (!json.is_object() || !json.contains("client_id") || !json["client_id"].is_string() ||
            !json.contains("scope") || !json["scope"].is_string() || !json.contains("server_id") ||
            !json["server_id"].is_string()) {
            finish(request.id, OSW_INVALID_ARGUMENT);
            return;
        }
        auto client = json["client_id"].get<std::string>();
        auto scope = json["scope"].get<std::string>();
        if (client.empty() || client.size() > 128 || client.find('\0') != std::string::npos ||
            (scope != "openid" && scope != "openid openid:duid" && scope != "openid,openid:duid")) {
            finish(request.id, OSW_INVALID_ARGUMENT);
            return;
        }
        if (pending) {
            finish(request.id, OSW_LIMIT);
            return;
        }
        const auto server = core::parse_decimal_id(json["server_id"].get<std::string>());
        if (!server) {
            finish(request.id, OSW_INVALID_ARGUMENT);
            return;
        }
        if (active_ticket)
            runtime->cancel_ticket(active_ticket->second);
        active_ticket.reset();
        auto ticket = runtime->try_request_ticket(*server);
        if (!ticket) {
            runtime->take_error();
            finish(request.id, OSW_UNAVAILABLE);
            return;
        }
        pending = Pending{request.id, std::move(*ticket)};
    }
    void cancel(std::uint64_t id) override {
        if (p2p)
            p2p->cancel(id);
        if (platform)
            platform->cancel(id);
        if (pending && pending->id == id) {
            runtime->cancel_ticket(pending->ticket.handle);
            pending.reset();
        }
        if (active_ticket && active_ticket->first == id) {
            runtime->cancel_ticket(active_ticket->second);
            active_ticket.reset();
        }
    }
    core::Update poll() override {
        if (runtime) {
            const auto events = runtime->try_poll();
            fail_runtime();
            for (const auto &event : events) {
                if (event.kind == Event::Kind::shutdown_requested) {
                    if (pending)
                        finish(pending->id, OSW_SIGNED_OUT);
                    stop();
                    failed = true;
                    break;
                }
                if (event.kind != Event::Kind::ticket || !pending ||
                    event.ticket != pending->ticket.handle)
                    continue;
                if (event.result != 1) {
                    runtime->cancel_ticket(pending->ticket.handle);
                    finish(pending->id, OSW_UNAVAILABLE);
                } else {
                    auto token =
                        core::try_encode_token({identity_value.account_id, pending->ticket.bytes,
                                                identity_value.online_id});
                    if (token) {
                        core::Result result{pending->id};
                        result.body.assign(token->begin(), token->end());
                        results.push_back(std::move(result));
                        active_ticket = std::pair{pending->id, pending->ticket.handle};
                    } else {
                        runtime->cancel_ticket(pending->ticket.handle);
                        finish(pending->id, OSW_LIMIT);
                    }
                }
                pending.reset();
            }
            if (runtime) {
                identity_value.signed_in = runtime->logged_on();
                identity_value.reason_code =
                    identity_value.signed_in ? 0 : static_cast<int>(ErrorCode::signed_out);
            }
        }
        core::Update update;
        if (platform && runtime) {
            auto events = runtime->take_social_events();
            platform->process(events);
            if (p2p) {
                p2p->set_party(platform->party());
                p2p->process(events);
            }
            if (!fail_runtime())
                update = platform->take_update();
        }
        if (p2p) {
            auto network = p2p->poll();
            for (auto &result : network.results)
                update.results.push_back(std::move(result));
            if (network.failed)
                failed = true;
        }
        for (auto &result : results)
            update.results.push_back(std::move(result));
        results.clear();
        update.failed = failed;
        return update;
    }
    void stop() override {
        p2p.reset();
        if (platform) {
            platform->stop();
            platform.reset();
        }
        pending.reset();
        active_ticket.reset();
        runtime.reset();
        identity_value.signed_in = false;
        identity_value.account_id = 0;
    }

  private:
    struct Pending {
        std::uint64_t id;
        Ticket ticket;
    };
    core::Config config;
    core::Identity identity_value;
    std::unique_ptr<Runtime> runtime;
    std::unique_ptr<core::Platform> platform;
    std::unique_ptr<core::P2P> p2p;
    std::optional<Pending> pending;
    std::optional<std::pair<std::uint64_t, std::uint32_t>> active_ticket;
    std::vector<core::Result> results;
    bool failed = false;
    bool fail_runtime() {
        if (!runtime || !runtime->error())
            return false;
        const auto code = static_cast<int>(runtime->error()->code);
        if (platform) {
            auto update = platform->take_update();
            for (const auto &result : update.results)
                finish(result.id, OSW_UNAVAILABLE);
        }
        if (pending)
            finish(pending->id, OSW_UNAVAILABLE);
        stop();
        identity_value.reason = "Steam runtime failed";
        identity_value.reason_code = code;
        failed = true;
        return true;
    }
    void finish(std::uint64_t id, int status) { results.push_back({id, status}); }
};
} // namespace
std::unique_ptr<core::Backend> make_backend() { return std::make_unique<SteamBackend>(); }
} // namespace osw::steam
