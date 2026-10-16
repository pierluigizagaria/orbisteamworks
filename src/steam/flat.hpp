#pragma once

// Independently written ABI subset, checked against the user's Steamworks SDK
// 1.65 (Readme: 23 July 2026). No SDK headers are required by the build.
// Sources: steam_api.h, steam_gameserver.h, steam_api_flat.h,
// steam_api_internal.h, isteamuser.h, isteamutils.h and steamnetworkingtypes.h.
#include <cstddef>
#include <cstdint>

namespace osw::steam::flat {
using Pipe = std::int32_t;
using Ticket = std::uint32_t;
using Id = std::uint64_t;
using ErrorMessage = char[1024];
// Enum values use the SDK's 32-bit signed representation at the C boundary.
using InitResult = std::int32_t;
using ServerMode = std::int32_t;
using BeginResult = std::int32_t;

#ifdef _WIN32
#pragma pack(push, 8)
#else
#pragma pack(push, 4)
#endif
struct Callback {
    std::int32_t user;
    std::int32_t kind;
    std::uint8_t *data;
    std::int32_t size;
};
struct ConnectFailure {
    std::int32_t result;
    bool retrying;
};
struct Disconnected {
    std::int32_t result;
};
struct TicketResponse {
    Ticket ticket;
    std::int32_t result;
};
struct CallCompleted {
    std::uint64_t call;
    std::int32_t callback;
    std::uint32_t size;
};
struct LobbyCreated {
    std::int32_t result;
    Id lobby;
};
struct LobbyList {
    std::uint32_t count;
};
struct LobbyEnter {
    Id lobby;
    std::uint32_t permissions;
    bool locked;
    std::uint32_t response;
};
struct LobbyData {
    Id lobby, member;
    std::uint8_t success;
};
struct LobbyChat {
    Id lobby, user, actor;
    std::uint32_t change;
};
struct LobbyMessage {
    Id lobby, user;
    std::uint8_t type;
    std::uint32_t entry;
};
struct LobbyInvite {
    Id user, lobby, game;
};
struct PersonaChanged {
    Id user;
    std::int32_t flags;
};
struct OverlayActivated {
    std::uint8_t active, user_initiated;
    std::uint32_t app, process;
};
#pragma pack(pop)
// CSteamID itself has alignment 1 in the SDK. Consequently this callback has
// alignment 4 and owner offset 12 even on Windows (not the usual uint64 ABI).
#pragma pack(push, 4)
struct Validation {
    Id user;
    std::int32_t result;
    Id owner;
};
struct RichPresenceChanged {
    Id user;
    std::uint32_t app;
};
#pragma pack(pop)
#pragma pack(push, 1)
struct Identity {
    std::int32_t type;
    std::int32_t size;
    union {
        Id steam_id;
        std::uint32_t reserved[32];
    };
};
struct LobbyJoinRequested {
    Id lobby, user;
};
#pragma pack(pop)
static_assert(sizeof(void *) == 8 && sizeof(bool) == 1);
static_assert(sizeof(TicketResponse) == 8 && sizeof(ConnectFailure) == 8);
static_assert(sizeof(OverlayActivated) == 12 && offsetof(OverlayActivated, app) == 4 &&
              offsetof(OverlayActivated, process) == 8);
static_assert(sizeof(Identity) == 136 && alignof(Identity) == 1);
#pragma pack(push, 8)
struct NetworkingMessage {
    void *data;
    std::int32_t size;
    std::uint32_t connection;
    Identity peer;
    std::int64_t connection_data, received, number;
    void (*free_data)(NetworkingMessage *);
    void (*release)(NetworkingMessage *);
    std::int32_t channel, flags;
    std::int64_t user_data;
    std::uint16_t lane, padding;
};
#pragma pack(pop)
static_assert(offsetof(NetworkingMessage, peer) == 16 &&
              offsetof(NetworkingMessage, channel) == 192);
#ifdef _WIN32
static_assert(sizeof(Callback) == 24);
#else
static_assert(sizeof(Callback) == 20);
#endif
static_assert(sizeof(Validation) == 20 && alignof(Validation) == 4 &&
              offsetof(Validation, owner) == 12);
inline constexpr int connected = 101, connect_failure = 102, disconnected = 103;
inline constexpr int validation = 143, ticket_response = 163;
inline constexpr int shutdown_requested = 704;
inline constexpr int api_call_completed = 703, lobby_created = 513, lobby_enter = 504;
inline constexpr int lobby_data = 505, lobby_chat = 506, lobby_invite = 503;
inline constexpr int lobby_message = 507;
inline constexpr int lobby_list = 510, networking_session_request = 1251;
inline constexpr int persona_changed = 304, lobby_join_requested = 333, rich_presence_changed = 336;
inline constexpr int overlay_activated = 331;
inline constexpr std::uint16_t query_port_shared = 0xffff;

// All exports use the platform C calling convention (cdecl on Windows x64).
using ClientInit = InitResult (*)(ErrorMessage *);
using ServerInit = InitResult (*)(std::uint32_t, std::uint16_t, std::uint16_t, ServerMode,
                                  const char *, const char *, ErrorMessage *);
using Void = void (*)();
using GetPipe = Pipe (*)();
using RunFrame = void (*)(Pipe);
using NextCallback = bool (*)(Pipe, Callback *);
using FreeCallback = void (*)(Pipe);
using GetInterface = void *(*)();
using LoggedOn = bool (*)(void *);
using GetId = Id (*)(void *);
using GetTicket = Ticket (*)(void *, void *, int, std::uint32_t *, const Identity *);
using CancelTicket = void (*)(void *, Ticket);
using SetString = void (*)(void *, const char *);
using SetBool = void (*)(void *, bool);
using InterfaceVoid = void (*)(void *);
using BeginAuth = BeginResult (*)(void *, const void *, int, Id);
using EndAuth = void (*)(void *, Id);
using GetCallResult = bool (*)(Pipe, std::uint64_t, void *, int, int, bool *);
using FriendCount = int (*)(void *, int);
using FriendByIndex = Id (*)(void *, int, int);
using FriendState = int (*)(void *, Id);
using FriendName = const char *(*)(void *, Id);
using PersonaName = const char *(*)(void *);
using OverlayEnabled = bool (*)(void *);
using GetPresence = const char *(*)(void *, Id, const char *);
using SetPresence = bool (*)(void *, const char *, const char *);
using RequestPresence = void (*)(void *, Id);
using CreateLobby = std::uint64_t (*)(void *, int, int);
using JoinLobby = std::uint64_t (*)(void *, Id);
using LobbyVoid = void (*)(void *, Id);
using LobbyBool = bool (*)(void *, Id);
using Invite = bool (*)(void *, Id, Id);
using LobbyString = const char *(*)(void *, Id, const char *);
using LobbySet = bool (*)(void *, Id, const char *, const char *);
using LobbyDelete = bool (*)(void *, Id, const char *);
using LobbyOwner = Id (*)(void *, Id);
using MemberCount = int (*)(void *, Id);
using MemberByIndex = Id (*)(void *, Id, int);
using MemberString = const char *(*)(void *, Id, Id, const char *);
using MemberSet = void (*)(void *, Id, const char *, const char *);
using LobbyJoinable = bool (*)(void *, Id, bool);
using LobbyLimit = bool (*)(void *, Id, int);
using LobbySend = bool (*)(void *, Id, const void *, int);
using LobbyRead = int (*)(void *, Id, int, Id *, void *, int, std::int32_t *);
using LobbyFilterString = void (*)(void *, const char *, const char *, int);
using InterfaceInt = void (*)(void *, int);
using LobbySearch = std::uint64_t (*)(void *);
using LobbyIndex = Id (*)(void *, int);
using NetworkSend = int (*)(void *, const Identity *, const void *, std::uint32_t, int, int);
using NetworkReceive = int (*)(void *, int, NetworkingMessage **, int);
using NetworkSession = bool (*)(void *, const Identity *);
using NetworkRelease = void (*)(NetworkingMessage *);
} // namespace osw::steam::flat
