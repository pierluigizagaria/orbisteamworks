// Test-only error fixture. Contains no Valve code and never contacts Steam.
#include "steam/flat.hpp"
#include <cstdio>
#include <cstdlib>
#include <string_view>
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
EXPORT bool SteamAPI_ManualDispatch_GetAPICallResult(Pipe, std::uint64_t, void *, int, int,
                                                   bool *) { return false; }
EXPORT int SteamAPI_ISteamFriends_GetFriendCount(void *, int) {
    return scenario() == "bad-friend-count" ? -1 : 0;
}
EXPORT Id SteamAPI_ISteamFriends_GetFriendByIndex(void *, int, int) { return 0; }
EXPORT int SteamAPI_ISteamFriends_GetFriendPersonaState(void *, Id) { return 0; }
EXPORT const char *SteamAPI_ISteamFriends_GetFriendPersonaName(void *, Id) { return ""; }
EXPORT const char *SteamAPI_ISteamFriends_GetFriendRichPresence(void *, Id, const char *) {
    return "";
}
EXPORT bool SteamAPI_ISteamFriends_SetRichPresence(void *, const char *, const char *value) {
    return scenario() != "presence-failure" || !*value;
}
EXPORT void SteamAPI_ISteamFriends_RequestFriendRichPresence(void *, Id) {}
EXPORT std::uint64_t SteamAPI_ISteamMatchmaking_CreateLobby(void *, int, int) { return 0; }
EXPORT std::uint64_t SteamAPI_ISteamMatchmaking_JoinLobby(void *, Id) { return 0; }
EXPORT void SteamAPI_ISteamMatchmaking_LeaveLobby(void *, Id) {}
EXPORT bool SteamAPI_ISteamMatchmaking_InviteUserToLobby(void *, Id, Id) { return false; }
EXPORT bool SteamAPI_ISteamMatchmaking_RequestLobbyData(void *, Id) { return false; }
EXPORT const char *SteamAPI_ISteamMatchmaking_GetLobbyData(void *, Id, const char *) { return ""; }
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyData(void *, Id, const char *, const char *) {
    return false;
}
EXPORT bool SteamAPI_ISteamMatchmaking_DeleteLobbyData(void *, Id, const char *) { return false; }
EXPORT Id SteamAPI_ISteamMatchmaking_GetLobbyOwner(void *, Id) { return 0; }
EXPORT int SteamAPI_ISteamMatchmaking_GetNumLobbyMembers(void *, Id) {
    return scenario() == "bad-member-count" ? -1 : 0;
}
EXPORT Id SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex(void *, Id, int) { return 0; }
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
                                                      std::int32_t *) { return 0; }
EXPORT bool SteamAPI_ISteamMatchmaking_SetLobbyType(void *, Id, int) { return false; }
