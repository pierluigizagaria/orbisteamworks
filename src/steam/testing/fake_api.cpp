// Test-only error fixture. Contains no Valve code and never contacts Steam.
#include "steam/flat.hpp"
#include <cstdio>
#include <cstdlib>
#include <string_view>
#include <string>
#include <map>
#include <deque>
#include <vector>
#include <cstring>
#include <mutex>
#ifdef _WIN32
#define EXPORT extern "C" __declspec(dllexport)
#else
#define EXPORT extern "C"
#endif
using namespace osw::steam::flat;
namespace {
bool server = false, sent = false;
bool callback_pending = false;
bool ticket_delivered = false;
bool ticket_active = false, ticket_requested = false;
int frame_count = 0;
std::string_view scenario() {
    auto p = std::getenv("OSW_FIXTURE_SCENARIO");
    return p ? p : "";
}
int marker = 1;
TicketResponse ticket_reply{1, 2};
ConnectFailure connection_reply{2, false};
std::uint64_t next_call = 100;
std::deque<CallCompleted> completed;
std::map<std::string, std::string> lobby_values, filters;
std::deque<std::pair<Identity, std::string>> network_queue;
bool room_exists = false;
bool network_failed_once = false, network_reset = false;
bool network_session_delivered = false, network_session_accepted = false;
Identity network_session_identity{};
int last_lobby_type = -1, distance = 1;
OverlayActivated overlay_reply{};
int overlay_reply_size = 0, overlay_opens = 0;
bool overlay_reply_pending = false, overlay_callback = false;
std::mutex persona_mutex;
std::string persona_value = " Fixture\x01 Persona \xf0\x9f\x8f\x8e";
} // namespace
EXPORT InitResult SteamAPI_InitFlat(ErrorMessage *) { return scenario() == "no-client" ? 2 : 0; }
EXPORT InitResult SteamInternal_GameServer_Init_V2(std::uint32_t ip, std::uint16_t game,
                                                   std::uint16_t query, ServerMode mode,
                                                   const char *, const char *, ErrorMessage *) {
    server = true;
    if (ip != 0x7f000001 || game != 0 || query != 65535 || mode != 2)
        std::abort();
    return scenario() == "init-failure" ? 1 : 0;
}
EXPORT void SteamAPI_Shutdown() {
    if (callback_pending || ticket_active)
        std::abort();
    std::puts("Fixture client shutdown");
}
EXPORT void SteamGameServer_Shutdown() {}
EXPORT void SteamAPI_ManualDispatch_Init() {}
EXPORT void SteamAPI_ManualDispatch_RunFrame(Pipe) { ++frame_count; }
EXPORT bool SteamAPI_ManualDispatch_GetNextCallback(Pipe, Callback *cb) {
    if (callback_pending)
        std::abort();
    if (overlay_reply_pending) {
        *cb = {1, overlay_activated, reinterpret_cast<std::uint8_t *>(&overlay_reply), overlay_reply_size};
        overlay_reply_pending = false;
        overlay_callback = callback_pending = true;
        return true;
    }
    if (scenario() == "p2p" && !completed.empty()) {
        *cb = {1, api_call_completed, (std::uint8_t *)&completed.front(), sizeof(CallCompleted)};
        callback_pending = true;
        return true;
    }
    if (scenario() == "p2p" && network_failed_once && !network_session_delivered) {
        network_session_delivered = true;
        network_session_identity.type = 16;
        network_session_identity.size = 8;
        network_session_identity.steam_id = 2;
        *cb = {1, networking_session_request, (std::uint8_t *)&network_session_identity,
               sizeof(network_session_identity)};
        callback_pending = true;
        return true;
    }
    if (!server && scenario() == "shutdown-pending" && !sent) {
        *cb = {1, shutdown_requested, nullptr, 0};
    } else if (!server && scenario() == "shutdown-ready" && !sent) {
        if (frame_count == 1) {
            if (ticket_delivered)
                return false;
            ticket_delivered = true;
            ticket_reply.result = 1;
            *cb = {1, ticket_response, reinterpret_cast<std::uint8_t *>(&ticket_reply),
                   sizeof(ticket_reply)};
        } else {
            *cb = {1, shutdown_requested, nullptr, 0};
            sent = true;
        }
        callback_pending = true;
        return true;
    } else {
        if (sent || scenario() == "timeout")
            return false;
        if (server && scenario() == "logon-failure") {
            *cb = {1, connect_failure, reinterpret_cast<std::uint8_t *>(&connection_reply),
                   sizeof(connection_reply)};
        } else if (!server && ticket_requested &&
                   (scenario() == "rejected" || scenario() == "bad-callback")) {
            *cb = {1, ticket_response, reinterpret_cast<std::uint8_t *>(&ticket_reply),
                   scenario() == "bad-callback" ? 1 : static_cast<int>(sizeof(ticket_reply))};
        } else
            return false;
    }
    sent = true;
    callback_pending = true;
    return true;
}
EXPORT void SteamAPI_ManualDispatch_FreeLastCallback(Pipe) {
    if (!callback_pending)
        std::abort();
    callback_pending = false;
    if (overlay_callback) {
        overlay_callback = false;
        return;
    }
    if (scenario() == "p2p" && !completed.empty())
        completed.pop_front();
}
EXPORT Pipe SteamAPI_GetHSteamPipe() { return 1; }
EXPORT Pipe SteamGameServer_GetHSteamPipe() { return 2; }
EXPORT void *SteamAPI_SteamUser_v023() { return &marker; }
EXPORT void *SteamAPI_SteamGameServer_v015() { return &marker; }
EXPORT bool SteamAPI_ISteamUser_BLoggedOn(void *) { return scenario() != "signed-out"; }
EXPORT bool SteamAPI_ISteamGameServer_BLoggedOn(void *) { return false; }
EXPORT Id SteamAPI_ISteamUser_GetSteamID(void *) { return 1; }
EXPORT Id SteamAPI_ISteamGameServer_GetSteamID(void *) { return 2; }
EXPORT Ticket SteamAPI_ISteamUser_GetAuthSessionTicket(void *, void *data, int, std::uint32_t *size,
                                                       const Identity *) {
    if (ticket_active)
        std::abort();
    ticket_requested = true;
    if (scenario() == "zero-ticket") {
        *size = 0;
        return 0;
    }
    ticket_active = true;
    *size = scenario() == "bad-ticket" ? 4096 : 1;
    *static_cast<unsigned char *>(data) = 1;
    return 1;
}
EXPORT void SteamAPI_ISteamUser_CancelAuthTicket(void *, Ticket) {
    if (!ticket_active)
        std::abort();
    ticket_active = false;
    std::puts("Fixture ticket cancelled");
}
EXPORT void SteamAPI_ISteamGameServer_SetProduct(void *, const char *) {}
EXPORT void SteamAPI_ISteamGameServer_SetGameDescription(void *, const char *) {}
EXPORT void SteamAPI_ISteamGameServer_SetDedicatedServer(void *, bool dedicated) {
    if (!dedicated)
        std::abort();
}
EXPORT void SteamAPI_ISteamGameServer_SetAdvertiseServerActive(void *, bool active) {
    if (active)
        std::abort();
}
EXPORT void SteamAPI_ISteamGameServer_LogOnAnonymous(void *) {}
EXPORT void SteamAPI_ISteamGameServer_LogOff(void *) {}
EXPORT BeginResult SteamAPI_ISteamGameServer_BeginAuthSession(void *, const void *, int, Id) {
    return 1;
}
EXPORT void SteamAPI_ISteamGameServer_EndAuthSession(void *, Id) {}

EXPORT void *SteamAPI_SteamFriends_v018() {
    return scenario() == "missing-social" ? nullptr : &marker;
}
EXPORT void *SteamAPI_SteamMatchmaking_v009() { return &marker; }
EXPORT void *SteamAPI_SteamUtils_v011() {
    return scenario() == "overlay-unsupported" ? nullptr : &marker;
}
EXPORT bool SteamAPI_ISteamUtils_IsOverlayEnabled(void *) { return scenario() == "overlay"; }
EXPORT void SteamAPI_ISteamFriends_ActivateGameOverlay(void *, const char *dialog) {
    if (scenario() != "overlay" || std::strcmp(dialog, "friends")) std::abort();
    ++overlay_opens;
}
EXPORT void SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog(void *, Id lobby) {
    if (scenario() != "overlay" || lobby != 9001) std::abort();
    ++overlay_opens;
}
EXPORT void OSWFixtureOverlayCallback(unsigned active, unsigned user, unsigned app, int size) {
    if (overlay_reply_pending || callback_pending) std::abort();
    overlay_reply = {static_cast<std::uint8_t>(active), static_cast<std::uint8_t>(user), app, 42};
    overlay_reply_size = size;
    overlay_reply_pending = true;
}
EXPORT int OSWFixtureOverlayOpens() { return overlay_opens; }
EXPORT bool SteamAPI_ManualDispatch_GetAPICallResult(Pipe, std::uint64_t, void *out, int size,
                                                     int kind, bool *failed) {
    if (scenario() != "p2p")
        return false;
    *failed = false;
    if (kind == lobby_created && size == sizeof(LobbyCreated)) {
        *(LobbyCreated *)out = {1, 9001};
        return true;
    }
    if (kind == lobby_list && size == sizeof(LobbyList)) {
        bool match = room_exists;
        for (auto &[key, value] : filters)
            if (lobby_values[key] != value)
                match = false;
        *(LobbyList *)out = {(std::uint32_t)(match ? 1 : 0)};
        return true;
    }
    if (kind == lobby_enter && size == sizeof(LobbyEnter)) {
        *(LobbyEnter *)out = {9001, 0, false, 1};
        return true;
    }
    return false;
}
EXPORT int SteamAPI_ISteamFriends_GetFriendCount(void *, int) {
    return scenario() == "bad-friend-count" ? -1 : 0;
}
EXPORT Id SteamAPI_ISteamFriends_GetFriendByIndex(void *, int, int) { return 0; }
EXPORT int SteamAPI_ISteamFriends_GetFriendPersonaState(void *, Id) { return 0; }
EXPORT const char *SteamAPI_ISteamFriends_GetFriendPersonaName(void *, Id) { return ""; }
EXPORT const char *SteamAPI_ISteamFriends_GetPersonaName(void *) {
    thread_local std::string value;
    std::lock_guard lock(persona_mutex);
    value = persona_value;
    return value.c_str();
}
EXPORT void OSWFixtureSetPersona(const char *name) {
    std::lock_guard lock(persona_mutex);
    persona_value = name;
}
EXPORT const char *SteamAPI_ISteamFriends_GetFriendRichPresence(void *, Id, const char *) {
    return "";
}
EXPORT bool SteamAPI_ISteamFriends_SetRichPresence(void *, const char *, const char *value) {
    return scenario() != "presence-failure" || !*value;
}
EXPORT void SteamAPI_ISteamFriends_RequestFriendRichPresence(void *, Id) {}
EXPORT std::uint64_t SteamAPI_ISteamMatchmaking_CreateLobby(void *, int type, int) {
    if (scenario() != "p2p")
        return 0;
    last_lobby_type = type;
    room_exists = true;
    auto call = next_call++;
    completed.push_back({call, lobby_created, sizeof(LobbyCreated)});
    return call;
}
EXPORT std::uint64_t SteamAPI_ISteamMatchmaking_JoinLobby(void *, Id) { return 0; }
EXPORT void SteamAPI_ISteamMatchmaking_LeaveLobby(void *, Id) { room_exists = false; }
EXPORT bool SteamAPI_ISteamMatchmaking_InviteUserToLobby(void *, Id, Id) { return false; }
EXPORT bool SteamAPI_ISteamMatchmaking_RequestLobbyData(void *, Id) { return false; }
EXPORT const char *SteamAPI_ISteamMatchmaking_GetLobbyData(void *, Id, const char *key) {
    return scenario() == "p2p" ? lobby_values[key].c_str() : "";
}
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyData(void *, Id, const char *key,
                                                    const char *value) {
    if (scenario() != "p2p")
        return false;
    lobby_values[key] = value;
    return true;
}
EXPORT bool SteamAPI_ISteamMatchmaking_DeleteLobbyData(void *, Id, const char *) { return false; }
EXPORT Id SteamAPI_ISteamMatchmaking_GetLobbyOwner(void *, Id) {
    return scenario() == "p2p" ? 1 : 0;
}
EXPORT int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void *, Id) {
    return scenario() == "bad-member-count" ? -1 : scenario() == "p2p" ? 1 : 0;
}
EXPORT Id SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void *, Id, int) {
    return scenario() == "p2p" ? 1 : 0;
}
EXPORT const char *SteamAPI_ISteamMatchmaking_GetLobbyMemberData(void *, Id, Id, const char *) {
    return "";
}
EXPORT void SteamAPI_ISteamMatchmaking_SetLobbyMemberData(void *, Id, const char *, const char *) {}
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyJoinable(void *, Id, bool) { return false; }
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit(void *, Id, int) { return false; }
EXPORT bool SteamAPI_ISteamMatchmaking_SendLobbyChatMsg(void *, Id, const void *, int) {
    return false;
}
EXPORT int SteamAPI_ISteamMatchmaking_GetLobbyChatEntry(void *, Id, int, Id *, void *, int,
                                                        std::int32_t *) {
    return 0;
}
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyType(void *, Id, int) { return false; }
EXPORT void *SteamAPI_SteamNetworkingMessages_SteamAPI_v002() { return &marker; }
EXPORT void *SteamAPI_SteamNetworkingUtils_SteamAPI_v004() { return &marker; }
EXPORT void SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess(void *) {}
EXPORT void SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter(void *, const char *key,
                                                                       const char *value,
                                                                       int compare) {
    if (compare != 0)
        std::abort();
    filters[key] = value;
}
EXPORT void SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter(void *, int value) {
    if (value < 1 || value > 3)
        std::abort();
    distance = value;
}
EXPORT void SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable(void *, int value) {
    if (value != 1)
        std::abort();
}
EXPORT void SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter(void *, int value) {
    if (value != 32)
        std::abort();
}
EXPORT std::uint64_t SteamAPI_ISteamMatchmaking_RequestLobbyList(void *) {
    auto call = next_call++;
    completed.push_back({call, lobby_list, sizeof(LobbyList)});
    return call;
}
EXPORT Id SteamAPI_ISteamMatchmaking_GetLobbyByIndex(void *, int index) {
    if (index != 0 || last_lobby_type != 3)
        std::abort();
    return 9001;
}
EXPORT int SteamAPI_ISteamNetworkingMessages_SendMessageToUser(void *, const Identity *identity,
                                                               const void *bytes,
                                                               std::uint32_t size, int flags,
                                                               int channel) {
    if (identity->type != 16 || identity->size != 8 || (channel == 48 ? flags != 1 : flags != 9))
        std::abort();
    if (!network_failed_once) {
        network_failed_once = true;
        return 3;
    }
    if (!network_reset || !network_session_accepted)
        std::abort();
    auto peer = *identity;
    network_queue.push_back({peer, std::string((const char *)bytes, size)});
    return 1;
}
EXPORT int SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel(void *, int channel,
                                                                      NetworkingMessage **output,
                                                                      int count) {
    if (channel != 47 || network_queue.empty() || count < 1)
        return 0;
    auto value = std::move(network_queue.front());
    network_queue.pop_front();
    auto message = new NetworkingMessage{};
    message->peer = value.first;
    message->size = (int)value.second.size();
    message->channel = channel;
    message->data = new char[value.second.size()];
    std::memcpy(message->data, value.second.data(), value.second.size());
    *output = message;
    return 1;
}
EXPORT bool SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser(void *, const Identity *) {
    network_session_accepted = true;
    return true;
}
EXPORT bool SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser(void *, const Identity *) {
    network_reset = true;
    return true;
}
EXPORT void SteamAPI_SteamNetworkingMessage_t_Release(NetworkingMessage *message) {
    delete[] (char *)message->data;
    delete message;
}
