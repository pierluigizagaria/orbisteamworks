#include "core/backend.hpp"
#include "core/platform.hpp"
#include "core/psn_events.hpp"
#include <algorithm>
#include <chrono>
#include <cstring>
#include <iostream>
#include <limits>
#include <mutex>
#include <nlohmann/json.hpp>
#include <optional>
#include <stdexcept>
#include <thread>

namespace {
using namespace osw::core;
using namespace std::chrono_literals;
using Json = nlohmann::json;
constexpr std::uint64_t local_id = 76561198000000001ull, remote_id = local_id + 1;
const Identity local{true, local_id, "LocalName"};
const Event friend_event{OSW_FRIENDS_CHANGED, 0, remote_id, {}, {}, "RemoteName", 1};
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
template <class F> void wait(F predicate) {
    const auto deadline = std::chrono::steady_clock::now() + 3s;
    while (!predicate()) {
        check(std::chrono::steady_clock::now() < deadline, "fixture deadline exceeded");
        std::this_thread::sleep_for(2ms);
    }
}
template <class F> Json read(F poll) {
    std::size_t required = 0;
    check(poll(nullptr, 0, &required) == OSW_LIMIT && required > 1 && required <= 16384,
          "bounded size query must not consume the event");
    std::vector<char> bytes(required, '?');
    std::size_t again = 0;
    check(poll(bytes.data(), bytes.size() - 1, &again) == OSW_LIMIT && again == required,
          "short buffer must retain the same front event");
    check(std::all_of(bytes.begin(), bytes.end(), [](char c) { return c == '?'; }),
          "short-buffer failure modified caller storage");
    check(poll(bytes.data(), bytes.size(), &again) == OSW_OK && again == required &&
              bytes.back() == 0,
          "event must be copied including NUL");
    return Json::parse(bytes.data());
}
Json read(PsnEvents &queue) {
    return read(
        [&](char *p, std::size_t n, std::size_t *required) { return queue.poll(p, n, required); });
}
Json read(osw_context *context) {
    return read([&](char *p, std::size_t n, std::size_t *required) {
        return osw_poll_psn_event(context, p, n, required);
    });
}
void empty(PsnEvents &queue) {
    std::size_t required = 19;
    check(queue.poll(nullptr, 0, &required) == OSW_PENDING && required == 19,
          "empty queue must preserve outputs");
}
void mapping_and_copied_snapshots() {
    PsnEvents queue;
    check(queue.enable(1) == OSW_OK, "enable mapping");
    auto identity = local;
    auto event = friend_event;
    queue.append(event, identity);
    identity.online_id = "LaterName";
    event.online_id = "LaterRemote";
    auto status = read(queue), title = read(queue);
    check(status["version"] == 1 && status["kind"] == "push" && status["service"].is_null() &&
              status["serviceLabel"] == 0 &&
              status["dataType"] == "np:service:presence:onlineStatus" && status["data"] == "" &&
              status["extData"].empty(),
          "friend availability must invalidate presence without inventing a relationship/body");
    check(status["from"] ==
                  Json{{"accountId", std::to_string(remote_id)}, {"onlineId", "RemoteName"}} &&
              status["to"] ==
                  Json{{"accountId", std::to_string(local_id)}, {"onlineId", "LocalName"}},
          "queued notification must retain exact account/name snapshots");
    check(title["service"] == "inGamePresence" &&
              title["dataType"] == "np:service:presence:gameTitleInfo" && title["data"] == "",
          "friend availability must invalidate in-game presence");
    empty(queue);
    for (auto change : {2u, 3u}) {
        event = friend_event;
        event.change_kind = change;
        queue.append(event, local);
        check(read(queue)["dataType"] == status["dataType"] &&
                  read(queue)["dataType"] == title["dataType"],
              "departure/rename must preserve the presence contract");
    }
    queue.append({OSW_FRIENDS_CHANGED}, local);
    queue.append({OSW_REQUEST_COMPLETE, 4}, local);
    queue.append({OSW_OVERLAY_CHANGED}, local);
    empty(queue);

    const char *types[] = {"np:service:session:game:join:to:member",
                           "np:service:session:game:remove:to:member",
                           "np:service:session:game:update:to:member"};
    for (unsigned change = 1; change <= 3; ++change) {
        queue.append(
            {OSW_SESSION_CHANGED, 0, remote_id, "109775244000000001", {}, "RemoteName", change},
            local);
        auto value = read(queue);
        check(value["service"] == "sessionInvitation" && value["dataType"] == types[change - 1] &&
                  value["data"] == "" &&
                  value["extData"] == Json{{"sessionId", "109775244000000001"},
                                           {"fromAccountId", std::to_string(remote_id)},
                                           {"toAccountId", std::to_string(local_id)}},
              "session event must carry exact PSN type and session/identity extension fields");
    }
    queue.append({OSW_SESSION_CHANGED, 0, 0, "109775244000000001", {}, {}, 3}, local);
    auto unknown = read(queue);
    check(unknown["from"]["accountId"] == "0" && unknown["from"]["onlineId"] == "" &&
              !unknown["extData"].contains("fromAccountId"),
          "unknown metadata actor must never become the local identity");

    event = {OSW_INVITATION_RECEIVED, 0, remote_id, "109775244000000001", "invite-1", "RemoteName"};
    queue.append(event, local);
    auto received = read(queue);
    const auto body = Json::parse(received.at("data").get<std::string>());
    check(received["kind"] == "push" && received["dataType"] == "np:service:invitation" &&
              body["invitationId"] == "invite-1" && body["sessionId"] == event.session_id &&
              body["fromUser"] == received["from"] &&
              received["extData"]["invitationId"] == "invite-1",
          "received invitation must have an invitation body without signalling acceptance");
    empty(queue);
    event.type = OSW_INVITATION_ACCEPTED;
    queue.append(event, local);
    auto accepted = read(queue);
    check(accepted["kind"] == "invitationAccepted" && accepted["sessionId"] == event.session_id &&
              accepted["invitationId"] == "invite-1" && accepted["from"] == received["from"] &&
              !accepted.contains("dataType"),
          "acceptance must remain a distinct host action, not a membership push");
    event.online_id.clear();
    queue.append(event, local);
    check(read(queue)["from"]["onlineId"] == "",
          "unknown sender name must remain empty, not fabricated");
    const auto largest = std::numeric_limits<std::uint64_t>::max();
    event.account_id = largest;
    event.session_id = std::to_string(largest);
    event.invitation_id = make_invitation_id(largest, largest, largest);
    check(
        event.invitation_id.size() == 58 &&
            event.invitation_id != make_invitation_id(largest, largest, largest - 1),
        "full-width identifiers must fit PSN invitation storage without truncation or collisions");
    queue.append(event, local);
    auto maximum = read(queue);
    check(maximum["invitationId"] == event.invitation_id &&
              maximum["from"]["accountId"] == std::to_string(largest),
          "largest provider identifier survives the notification contract losslessly");
    queue.append({OSW_IDENTITY_CHANGED}, local);
    check(read(queue) == Json{{"version", 1},
                              {"kind", "identity"},
                              {"signedIn", true},
                              {"accountId", std::to_string(local_id)},
                              {"onlineId", "LocalName"}},
          "identity record schema");
    identity.signed_in = false;
    queue.append({OSW_IDENTITY_CHANGED}, identity);
    auto signed_out = read(queue);
    check(signed_out["signedIn"] == false && signed_out["accountId"] == "0" &&
              signed_out["onlineId"] == "",
          "signed-out identity must not advertise a stale account/name");
}
void bounded_failures() {
    PsnEvents queue;
    std::size_t size = 73;
    check(queue.poll(nullptr, 0, &size) == OSW_NOT_SUPPORTED && size == 73,
          "stream must be opt-in");
    check(queue.enable(0) == OSW_NOT_SUPPORTED && queue.enable(2) == OSW_NOT_SUPPORTED,
          "unknown versions must fail");
    check(queue.enable(1) == OSW_OK, "supported version enables");
    queue.append({OSW_IDENTITY_CHANGED}, local);
    for (unsigned i = 0; i < 255; ++i)
        queue.append(friend_event, local); // 511 queued records.
    queue.append(friend_event, local);     // The pair cannot partially fit.
    char sentinel = '!';
    check(queue.poll(&sentinel, 1, &size) == OSW_UNAVAILABLE && sentinel == '!' && size == 73,
          "overflow must fail atomically instead of publishing a partial pair or stale prefix");
    check(queue.enable(1) == OSW_OK, "repeat enable remains idempotent");
    queue.append({OSW_IDENTITY_CHANGED}, local);
    check(queue.poll(nullptr, 0, &size) == OSW_UNAVAILABLE,
          "enable or new events cannot reset lost-event failure");

    for (const auto &event : std::vector<Event>{
             {OSW_FRIENDS_CHANGED, 0, remote_id, {}, {}, "Bad\x01Name", 1},
             {OSW_FRIENDS_CHANGED, 0, remote_id, {}, {}, "RemoteName", 4},
             {OSW_SESSION_CHANGED, 0, 0, "1", {}, {}, 1},
             {OSW_INVITATION_ACCEPTED, 0, remote_id, "1", std::string(61, 'a'), "RemoteName"},
             {OSW_INVITATION_ACCEPTED, 0, remote_id, "", "invite", "RemoteName"},
             {OSW_RESYNC}}) {
        PsnEvents invalid;
        invalid.enable(1);
        invalid.append(event, local);
        check(invalid.poll(nullptr, 0, &size) == OSW_UNAVAILABLE,
              "invalid meaningful source event must fail visibly");
    }
}
struct State {
    std::mutex mutex;
    std::vector<Event> events;
    std::optional<Identity> identity;
};
class Fixture final : public Backend {
  public:
    explicit Fixture(std::shared_ptr<State> shared) : shared(std::move(shared)) {}
    void start(const Config &) override {}
    Identity identity() const override { return current; }
    void submit(Request request) override { results.push_back({request.id, OSW_OK, 204}); }
    void cancel(std::uint64_t) override {}
    Update poll() override {
        std::lock_guard lock(shared->mutex);
        Update update;
        update.events.swap(shared->events);
        update.results.swap(results);
        if (shared->identity) {
            current = std::move(*shared->identity);
            shared->identity.reset();
        }
        return update;
    }
    void stop() override {}

  private:
    std::shared_ptr<State> shared;
    Identity current = local;
    std::vector<Result> results;
};
struct Context {
    std::shared_ptr<State> state = std::make_shared<State>();
    osw_context *value = create_context({}, std::make_unique<Fixture>(state));
    ~Context() { osw_destroy(value); }
    void send(std::vector<Event> events, std::optional<Identity> identity = {}) {
        std::lock_guard lock(state->mutex);
        state->events.insert(state->events.end(), events.begin(), events.end());
        state->identity = std::move(identity);
    }
    void ready() {
        std::size_t size = 0;
        wait([&] { return osw_poll_psn_event(value, nullptr, 0, &size) == OSW_LIMIT; });
    }
};
void public_lifecycle_and_independent_consumption() {
    Context context;
    std::size_t size = 0;
    check(osw_enable_psn_events(nullptr, 1) == OSW_INVALID_ARGUMENT &&
              osw_poll_psn_event(nullptr, nullptr, 0, &size) == OSW_INVALID_ARGUMENT &&
              osw_poll_psn_event(context.value, nullptr, 0, nullptr) == OSW_INVALID_ARGUMENT &&
              osw_poll_psn_event(context.value, nullptr, 1, &size) == OSW_INVALID_ARGUMENT,
          "invalid C API arguments");
    check(osw_poll_psn_event(context.value, nullptr, 0, &size) == OSW_NOT_SUPPORTED &&
              osw_enable_psn_events(context.value, 17) == OSW_NOT_SUPPORTED &&
              osw_enable_psn_events(context.value, 1) == OSW_OK &&
              osw_poll_psn_event(context.value, nullptr, 0, &size) == OSW_PENDING &&
              osw_start(context.value) == OSW_OK,
          "pre-start opt-in lifecycle");
    context.ready();
    check(osw_enable_psn_events(context.value, 1) == OSW_INVALID_ARGUMENT,
          "stream mode must freeze at start");
    check(read(context.value)["signedIn"] == true, "initial identity is visible");
    osw_event legacy{sizeof(legacy)};
    check(osw_poll_event(context.value, &legacy) == OSW_OK && legacy.type == OSW_IDENTITY_CHANGED,
          "PSN poll must not consume the legacy identity event");
    context.send({friend_event}, Identity{true, local_id, "RenamedLocal"});
    context.ready();
    while (osw_poll_event(context.value, &legacy) == OSW_OK) {
    }
    check(read(context.value)["to"]["onlineId"] == "LocalName",
          "legacy draining/rename cannot overwrite a queued recipient");
    check(read(context.value)["to"]["onlineId"] == "LocalName",
          "friend notification pair has one recipient snapshot");
    check(read(context.value)["onlineId"] == "RenamedLocal",
          "identity change follows prior immutable notifications");

    // Legacy loss/resync must not poison the independently consumed PSN stream.
    context.send(std::vector<Event>(600, Event{OSW_OVERLAY_CHANGED}));
    wait([&] {
        return osw_poll_event(context.value, &legacy) == OSW_OK && legacy.type == OSW_RESYNC;
    });
    context.send({friend_event});
    context.ready();
    check(read(context.value)["to"]["onlineId"] == "RenamedLocal" &&
              read(context.value)["kind"] == "push",
          "legacy overflow must not lose PSN notifications");

    // Conversely a failed optional stream must preserve ordinary APIs/results.
    context.send(std::vector<Event>(257, friend_event));
    wait([&] { return osw_poll_psn_event(context.value, nullptr, 0, &size) == OSW_UNAVAILABLE; });
    std::uint64_t request_id = 0;
    osw_request request{sizeof(request), OSW_FRIENDS, 1000};
    check(osw_submit(context.value, &request, &request_id) == OSW_OK,
          "optional stream failure must not disable ordinary requests");
    osw_result result{sizeof(result)};
    wait([&] { return osw_get_result(context.value, request_id, &result) == OSW_OK; });
    check(result.status == OSW_OK && osw_release(context.value, request_id) == OSW_OK,
          "request ownership survives stream overflow");
    check(osw_stop(context.value) == OSW_OK &&
              osw_enable_psn_events(context.value, 1) == OSW_INVALID_ARGUMENT &&
              osw_poll_psn_event(context.value, nullptr, 0, &size) == OSW_UNAVAILABLE,
          "stop cannot hide latched loss");

    Context stopped;
    check(osw_enable_psn_events(stopped.value, 1) == OSW_OK && osw_start(stopped.value) == OSW_OK,
          "second context starts");
    stopped.ready();
    read(stopped.value);
    check(osw_stop(stopped.value) == OSW_OK && read(stopped.value)["signedIn"] == false,
          "normal teardown retains a copied signed-out record");
}
} // namespace
int main() {
    try {
        mapping_and_copied_snapshots();
        bounded_failures();
        public_lifecycle_and_independent_consumption();
        std::cout
            << "PSN notification mapping, ownership, versioning and failure contracts passed\n";
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
