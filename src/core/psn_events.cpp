#include "psn_events.hpp"
#include "credential.hpp"
#include <cstring>
#include <nlohmann/json.hpp>

namespace osw::core {
namespace {
using Json = nlohmann::json;
constexpr std::size_t max_events = 512, max_bytes = 1024 * 1024, max_event_bytes = 16 * 1024;
bool identifier(const std::string &value, std::size_t maximum) {
    if (value.empty() || value.size() > maximum)
        return false;
    for (const unsigned char byte : value)
        if (byte < 0x21 || byte > 0x7e)
            return false;
    return true;
}
Json person(std::uint64_t account, const std::string &name) {
    return Json{{"accountId", std::to_string(account)}, {"onlineId", name}};
}
Json push(const Event &event, const Identity &identity, Json service, const char *type) {
    return Json{{"version", 1},
                {"kind", "push"},
                {"from", person(event.account_id, event.online_id)},
                {"to", person(identity.account_id, identity.online_id)},
                {"service", std::move(service)},
                {"serviceLabel", 0},
                {"dataType", type},
                {"data", ""},
                {"extData", Json::object()}};
}
} // namespace

int PsnEvents::enable(std::uint32_t version) noexcept {
    if (version != 1)
        return OSW_NOT_SUPPORTED;
    enabled = true;
    return OSW_OK;
}
void PsnEvents::fail() noexcept {
    failed = true;
    queue.clear();
    bytes = 0;
}
void PsnEvents::append(const Event &event, const Identity &identity) noexcept {
    if (!enabled || failed)
        return;
    try {
        std::vector<Json> batch;
        if (event.type == OSW_RESYNC) {
            fail(); // An accepted invitation cannot be recovered from a snapshot.
            return;
        }
        if (event.type == OSW_IDENTITY_CHANGED) {
            if (identity.signed_in &&
                (!identity.account_id || !valid_online_id(identity.online_id))) {
                fail();
                return;
            }
            batch.push_back(
                Json{{"version", 1},
                     {"kind", "identity"},
                     {"signedIn", identity.signed_in},
                     {"accountId", std::to_string(identity.signed_in ? identity.account_id : 0)},
                     {"onlineId", identity.signed_in ? identity.online_id : ""}});
        } else if (event.type == OSW_FRIENDS_CHANGED || event.type == OSW_SESSION_CHANGED ||
                   event.type == OSW_INVITATION_RECEIVED || event.type == OSW_INVITATION_ACCEPTED) {
            // A generic friend refresh is not an identifiable person/relationship event.
            if (event.type == OSW_FRIENDS_CHANGED && !event.account_id)
                return;
            if (!identity.signed_in || !identity.account_id ||
                !valid_online_id(identity.online_id) ||
                (!event.online_id.empty() && !valid_online_id(event.online_id))) {
                fail();
                return;
            }
            if (event.type == OSW_FRIENDS_CHANGED) {
                if (!valid_online_id(event.online_id) || event.change_kind < 1 ||
                    event.change_kind > 3) {
                    fail();
                    return;
                }
                // Compatibility/in-game visibility is presence, not Steam friendship.
                // Empty bodies invalidate the consumer's current friend/presence view.
                batch.push_back(push(event, identity, nullptr, "np:service:presence:onlineStatus"));
                batch.push_back(
                    push(event, identity, "inGamePresence", "np:service:presence:gameTitleInfo"));
            } else {
                if (!identifier(event.session_id, 31)) {
                    fail();
                    return;
                }
                if (event.type == OSW_SESSION_CHANGED) {
                    const char *type =
                        event.change_kind == 1   ? "np:service:session:game:join:to:member"
                        : event.change_kind == 2 ? "np:service:session:game:remove:to:member"
                        : event.change_kind == 3 ? "np:service:session:game:update:to:member"
                                                 : nullptr;
                    if (!type || (!event.account_id && event.change_kind != 3)) {
                        fail();
                        return;
                    }
                    batch.push_back(push(event, identity, "sessionInvitation", type));
                } else {
                    if (!event.account_id || !identifier(event.invitation_id, 60)) {
                        fail();
                        return;
                    }
                    if (event.type == OSW_INVITATION_ACCEPTED) {
                        batch.push_back(
                            Json{{"version", 1},
                                 {"kind", "invitationAccepted"},
                                 {"from", person(event.account_id, event.online_id)},
                                 {"to", person(identity.account_id, identity.online_id)},
                                 {"sessionId", event.session_id},
                                 {"invitationId", event.invitation_id}});
                    } else {
                        auto value =
                            push(event, identity, "sessionInvitation", "np:service:invitation");
                        value["data"] =
                            Json{{"invitationId", event.invitation_id},
                                 {"sessionId", event.session_id},
                                 {"fromUser", person(event.account_id, event.online_id)}}
                                .dump();
                        value["extData"]["invitationId"] = event.invitation_id;
                        batch.push_back(std::move(value));
                    }
                }
                for (auto &value : batch) {
                    if (value["kind"] != "push")
                        continue;
                    value["extData"]["sessionId"] = event.session_id;
                    value["extData"]["toAccountId"] = std::to_string(identity.account_id);
                    if (event.account_id)
                        value["extData"]["fromAccountId"] = std::to_string(event.account_id);
                }
            }
        }
        std::vector<std::string> encoded;
        std::size_t incoming = 0;
        for (const auto &value : batch) {
            auto text = value.dump();
            if (text.size() + 1 > max_event_bytes) {
                fail();
                return;
            }
            incoming += text.size() + 1;
            encoded.push_back(std::move(text));
        }
        if (encoded.size() > max_events - queue.size() || incoming > max_bytes - bytes) {
            fail();
            return;
        }
        for (auto &value : encoded)
            queue.push_back(std::move(value));
        bytes += incoming;
    } catch (...) {
        // Serialization/allocation failure has the same visible meaning as
        // overflow. Never leave a partially published notification batch.
        fail();
    }
}
int PsnEvents::poll(char *buffer, std::size_t capacity, std::size_t *required) noexcept {
    if (!required || (!buffer && capacity))
        return OSW_INVALID_ARGUMENT;
    if (!enabled)
        return OSW_NOT_SUPPORTED;
    if (failed)
        return OSW_UNAVAILABLE;
    if (queue.empty())
        return OSW_PENDING;
    const auto &value = queue.front();
    *required = value.size() + 1;
    if (capacity < *required)
        return OSW_LIMIT;
    std::memcpy(buffer, value.c_str(), *required);
    bytes -= *required;
    queue.pop_front();
    return OSW_OK;
}
} // namespace osw::core
