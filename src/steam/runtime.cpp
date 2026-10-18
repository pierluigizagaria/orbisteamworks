#include "steam/runtime.hpp"
#include "steam/flat.hpp"

#include <algorithm>
#include <array>
#include <cstdlib>
#include <cstring>
#include <map>
#include <set>
#include <utility>
#ifdef _WIN32
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#else
#include <dlfcn.h>
#include <link.h>
#include <unistd.h>
#endif

namespace osw::steam {
namespace {
std::filesystem::path executable_directory(std::error_code &error) {
#ifdef _WIN32
    std::array<wchar_t, 32768> path{};
    auto length = GetModuleFileNameW(nullptr, path.data(), static_cast<DWORD>(path.size()));
    if (!length || length == path.size()) {
        error = std::make_error_code(std::errc::io_error);
        return {};
    }
    return std::filesystem::path(std::wstring(path.data(), length)).parent_path();
#else
    return std::filesystem::read_symlink("/proc/self/exe", error).parent_path();
#endif
}
bool set_app_id(std::uint32_t value) {
#ifdef _WIN32
    return SetEnvironmentVariableW(L"SteamAppId", std::to_wstring(value).c_str()) != 0;
#else
    return setenv("SteamAppId", std::to_string(value).c_str(), 1) == 0;
#endif
}
template <class T> T payload(const flat::Callback &cb, std::optional<Error> &error) {
    if (!cb.data || cb.size != sizeof(T)) {
        error.emplace(ErrorCode::callback_abi,
                      "Unexpected callback payload size for " + std::to_string(cb.kind));
        return {};
    }
    T value{};
    std::memcpy(&value, cb.data, sizeof(value));
    return value;
}
} // namespace

struct Runtime::Impl {
    std::uint32_t app_id = 480;
    std::optional<Error> error;
    std::filesystem::path path;
#ifdef _WIN32
    HMODULE module = nullptr;
#else
    void *module = nullptr;
#endif
    enum class Mode { none, client, server } mode = Mode::none;
    void *api = nullptr;
    flat::Pipe pipe = 0;
    std::set<std::uint32_t> tickets;
    std::set<std::uint64_t> sessions;
    flat::Void shutdown = nullptr, dispatch_init = nullptr;
    flat::RunFrame frame = nullptr;
    flat::NextCallback next = nullptr;
    flat::FreeCallback free_callback = nullptr;
    flat::LoggedOn logged = nullptr;
    flat::GetId get_id = nullptr;
    flat::GetTicket get_ticket = nullptr;
    flat::CancelTicket cancel = nullptr;
    flat::InterfaceVoid logoff = nullptr;
    flat::BeginAuth begin = nullptr;
    flat::EndAuth end = nullptr;
    void *friends_api = nullptr;
    void *matchmaking = nullptr;
    flat::GetCallResult call_result = nullptr;
    flat::FriendCount friend_count = nullptr;
    flat::FriendByIndex friend_by_index = nullptr;
    flat::FriendState friend_state = nullptr;
    flat::FriendName friend_name = nullptr;
    flat::PersonaName persona_name = nullptr;
    void *overlay_utils = nullptr;
    flat::OverlayEnabled overlay_enabled = nullptr;
    flat::SetString activate_overlay = nullptr;
    flat::LobbyVoid activate_invite = nullptr;
    bool overlay_active = false;
    flat::GetPresence get_presence = nullptr;
    flat::SetPresence set_presence = nullptr;
    flat::RequestPresence request_presence = nullptr;
    flat::CreateLobby create_lobby = nullptr;
    flat::JoinLobby join_lobby = nullptr;
    flat::LobbyVoid leave_lobby = nullptr;
    flat::Invite invite = nullptr;
    flat::LobbyBool request_data = nullptr;
    flat::LobbyString lobby_data = nullptr;
    flat::LobbySet set_lobby_data = nullptr;
    flat::LobbySend send_message = nullptr;
    flat::LobbyRead read_message = nullptr;
    flat::LobbyLimit set_type = nullptr;
    flat::LobbyDelete delete_lobby_data = nullptr;
    flat::LobbyOwner owner = nullptr;
    flat::MemberCount member_count = nullptr;
    flat::MemberByIndex member_by_index = nullptr;
    flat::MemberString member_data = nullptr;
    flat::MemberSet set_member_data = nullptr;
    flat::LobbyJoinable set_joinable = nullptr;
    flat::LobbyLimit set_limit = nullptr;
    std::map<std::uint64_t, int> calls;
    std::vector<core::SocialEvent> social_events;
    std::set<std::uint64_t> presence_requested;
    void *networking = nullptr;
    flat::LobbyFilterString filter_string = nullptr;
    flat::InterfaceInt filter_distance = nullptr, filter_slots = nullptr, filter_count = nullptr;
    flat::LobbySearch lobby_search = nullptr;
    flat::LobbyIndex lobby_index = nullptr;
    flat::NetworkSend network_send = nullptr;
    flat::NetworkReceive network_receive = nullptr;
    flat::NetworkSession network_accept = nullptr, network_close = nullptr;
    flat::NetworkRelease network_release = nullptr;
    std::set<std::uint64_t> allowed_peers;
    std::set<std::uint64_t> pending_peer_requests;

    template <class T> T find_symbol(const char *name) {
        if (error)
            return nullptr;
#ifdef _WIN32
        auto address = GetProcAddress(module, name);
#else
        auto address = dlsym(module, name);
#endif
        if (!address)
            error.emplace(ErrorCode::symbol_missing,
                          std::string("Required symbol missing (SDK too old or incompatible): ") +
                              name);
        return reinterpret_cast<T>(address);
    }
    template <class T> T symbol(const char *name) {
        const auto address = find_symbol<T>(name);
        if (!address)
            throw *error;
        return address;
    }
    template <class T> T optional_symbol(const char *name) {
#ifdef _WIN32
        return reinterpret_cast<T>(GetProcAddress(module, name));
#else
        return reinterpret_cast<T>(dlsym(module, name));
#endif
    }
    void require(Mode expected) const {
        if (mode != expected || !api)
            throw Error(ErrorCode::arguments, "Wrong runtime mode or initialization incomplete");
    }
    ~Impl() {
        if (mode == Mode::client) {
            for (auto ticket : tickets)
                cancel(api, ticket);
        } else if (mode == Mode::server && api) {
            for (auto user : sessions)
                end(api, user);
            logoff(api);
        }
        if (mode != Mode::none)
            shutdown();
        if (module) {
#ifdef _WIN32
            FreeLibrary(module);
#else
            dlclose(module);
#endif
        }
    }
};

Runtime::Runtime(const std::filesystem::path &library, std::uint32_t app_id, Checked) : p(std::make_unique<Impl>()) {
    p->app_id = app_id;
#ifdef _WIN32
    constexpr auto filename = "steam_api64.dll";
#else
    constexpr auto filename = "libsteam_api.so";
#endif
    std::error_code error;
    auto path = library.empty() ? executable_directory(error) / filename : library;
    if (error) {
        p->error.emplace(ErrorCode::io, "Cannot locate executable");
        return;
    }
    p->path = std::filesystem::absolute(path, error);
    if (error || !std::filesystem::is_regular_file(p->path, error)) {
        p->error.emplace(ErrorCode::library_missing,
                         "Steam API library is missing or inaccessible");
        return;
    }
#ifdef _WIN32
    p->module = LoadLibraryExW(p->path.c_str(), nullptr,
                               LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
#else
    p->module = dlopen(p->path.c_str(), RTLD_NOW | RTLD_LOCAL);
#endif
    if (!p->module) {
        p->error.emplace(ErrorCode::library_load,
                         "Steam API library could not be loaded (architecture or dependency)");
        return;
    }
    p->dispatch_init = p->find_symbol<flat::Void>("SteamAPI_ManualDispatch_Init");
    p->frame = p->find_symbol<flat::RunFrame>("SteamAPI_ManualDispatch_RunFrame");
    p->next = p->find_symbol<flat::NextCallback>("SteamAPI_ManualDispatch_GetNextCallback");
    p->free_callback =
        p->find_symbol<flat::FreeCallback>("SteamAPI_ManualDispatch_FreeLastCallback");
}
Runtime::Runtime(const std::filesystem::path &library, std::uint32_t app_id) : Runtime(library, app_id, Checked{}) {
    if (p->error)
        throw *p->error;
}
std::unique_ptr<Runtime> Runtime::create(const std::filesystem::path &library, std::uint32_t app_id) {
    return std::unique_ptr<Runtime>(new Runtime(library, app_id, Checked{}));
}
Runtime::~Runtime() = default;
const Error *Runtime::error() const noexcept { return p->error ? &*p->error : nullptr; }
std::optional<Error> Runtime::take_error() { return std::exchange(p->error, std::nullopt); }

void Runtime::start_client() {
    if (!try_start_client())
        throw *p->error;
}
bool Runtime::try_start_client() {
    if (p->error)
        return false;
    if (p->mode != Impl::Mode::none) {
        p->error.emplace(ErrorCode::arguments, "Runtime already initialized");
        return false;
    }
    auto init = p->find_symbol<flat::ClientInit>("SteamAPI_InitFlat");
    auto get_api = p->find_symbol<flat::GetInterface>("SteamAPI_SteamUser_v023");
    auto get_pipe = p->find_symbol<flat::GetPipe>("SteamAPI_GetHSteamPipe");
    p->shutdown = p->find_symbol<flat::Void>("SteamAPI_Shutdown");
    p->logged = p->find_symbol<flat::LoggedOn>("SteamAPI_ISteamUser_BLoggedOn");
    p->get_id = p->find_symbol<flat::GetId>("SteamAPI_ISteamUser_GetSteamID");
    p->get_ticket = p->find_symbol<flat::GetTicket>("SteamAPI_ISteamUser_GetAuthSessionTicket");
    p->cancel = p->find_symbol<flat::CancelTicket>("SteamAPI_ISteamUser_CancelAuthTicket");
    if (p->error)
        return false;
    if (!set_app_id(p->app_id)) {
        p->error.emplace(ErrorCode::io, "Cannot set process SteamAppId");
        return false;
    }
    flat::ErrorMessage detail{};
    int result = init(&detail);
    // Do not forward opaque SDK messages: they can contain machine/account data.
    if (result != 0) {
        p->error.emplace(ErrorCode::client_unavailable,
                         "Steam client unavailable or incompatible; start Steam "
                         "and sign in. Init result=" +
                             std::to_string(result));
        return false;
    }
    p->mode = Impl::Mode::client;
    p->api = get_api();
    p->pipe = get_pipe();
    if (!p->api || !p->pipe) {
        p->error.emplace(ErrorCode::symbol_missing, "SteamUser023 interface unavailable");
        return false;
    }
    p->dispatch_init();
    if (!logged_on()) {
        p->error.emplace(ErrorCode::signed_out, "Steam is not logged on");
        return false;
    }
    return true;
}

void Runtime::start_server() {
    if (p->mode != Impl::Mode::none)
        throw Error(ErrorCode::arguments, "Runtime already initialized");
    // SDK 1.65 exports this C entry point, not the inline SteamGameServer_InitEx.
    auto init = p->symbol<flat::ServerInit>("SteamInternal_GameServer_Init_V2");
    auto get_api = p->symbol<flat::GetInterface>("SteamAPI_SteamGameServer_v015");
    auto get_pipe = p->symbol<flat::GetPipe>("SteamGameServer_GetHSteamPipe");
    p->shutdown = p->symbol<flat::Void>("SteamGameServer_Shutdown");
    p->logged = p->symbol<flat::LoggedOn>("SteamAPI_ISteamGameServer_BLoggedOn");
    p->get_id = p->symbol<flat::GetId>("SteamAPI_ISteamGameServer_GetSteamID");
    p->logoff = p->symbol<flat::InterfaceVoid>("SteamAPI_ISteamGameServer_LogOff");
    p->begin = p->symbol<flat::BeginAuth>("SteamAPI_ISteamGameServer_BeginAuthSession");
    p->end = p->symbol<flat::EndAuth>("SteamAPI_ISteamGameServer_EndAuthSession");
    auto product = p->symbol<flat::SetString>("SteamAPI_ISteamGameServer_SetProduct");
    auto description = p->symbol<flat::SetString>("SteamAPI_ISteamGameServer_SetGameDescription");
    auto dedicated = p->symbol<flat::SetBool>("SteamAPI_ISteamGameServer_SetDedicatedServer");
    auto advertise = p->symbol<flat::SetBool>("SteamAPI_ISteamGameServer_SetAdvertiseServerActive");
    auto logon = p->symbol<flat::InterfaceVoid>("SteamAPI_ISteamGameServer_LogOnAnonymous");
    if (!set_app_id(p->app_id))
        throw Error(ErrorCode::io, "Cannot set process SteamAppId");
    flat::ErrorMessage detail{};
    // Query-port shared mode opens no query socket. We own no game socket and
    // never forward discovery packets. Advertising defaults false in SDK 1.65.
    // Check only the interface we use (double-NUL terminated version list).
    int result =
        init(0x7f000001, 0, flat::query_port_shared, 2, "1.0.0.0", "SteamGameServer015\0", &detail);
    if (result != 0)
        throw Error(ErrorCode::server_init, "Anonymous game-server initialization failed; result=" +
                                                std::to_string(result));
    p->mode = Impl::Mode::server;
    p->api = get_api();
    p->pipe = get_pipe();
    if (!p->api || !p->pipe)
        throw Error(ErrorCode::symbol_missing, "SteamGameServer015 interface unavailable");
    advertise(p->api, false);
    dedicated(p->api, true);
    product(p->api, std::to_string(p->app_id).c_str());
    description(p->api, "orbiSteamworks private auth spike");
    p->dispatch_init();
    logon(p->api);
}

bool Runtime::start_social() {
    if (p->error)
        return false;
    if (p->mode != Impl::Mode::client || !p->api) {
        p->error.emplace(ErrorCode::arguments, "Wrong runtime mode or initialization incomplete");
        return false;
    }
    const auto friends = p->find_symbol<flat::GetInterface>("SteamAPI_SteamFriends_v018");
    const auto matchmaking = p->find_symbol<flat::GetInterface>("SteamAPI_SteamMatchmaking_v009");
    if (p->error)
        return false;
    p->friends_api = friends();
    p->matchmaking = matchmaking();
    if (!p->friends_api || !p->matchmaking) {
        p->error.emplace(ErrorCode::symbol_missing, "Steam social interfaces unavailable");
        return false;
    }
    p->call_result =
        p->find_symbol<flat::GetCallResult>("SteamAPI_ManualDispatch_GetAPICallResult");
    p->friend_count = p->find_symbol<flat::FriendCount>("SteamAPI_ISteamFriends_GetFriendCount");
    p->friend_by_index =
        p->find_symbol<flat::FriendByIndex>("SteamAPI_ISteamFriends_GetFriendByIndex");
    p->friend_state =
        p->find_symbol<flat::FriendState>("SteamAPI_ISteamFriends_GetFriendPersonaState");
    p->friend_name =
        p->find_symbol<flat::FriendName>("SteamAPI_ISteamFriends_GetFriendPersonaName");
    p->persona_name = p->find_symbol<flat::PersonaName>("SteamAPI_ISteamFriends_GetPersonaName");
    p->get_presence =
        p->find_symbol<flat::GetPresence>("SteamAPI_ISteamFriends_GetFriendRichPresence");
    p->set_presence = p->find_symbol<flat::SetPresence>("SteamAPI_ISteamFriends_SetRichPresence");
    p->request_presence =
        p->find_symbol<flat::RequestPresence>("SteamAPI_ISteamFriends_RequestFriendRichPresence");
    p->create_lobby = p->find_symbol<flat::CreateLobby>("SteamAPI_ISteamMatchmaking_CreateLobby");
    p->join_lobby = p->find_symbol<flat::JoinLobby>("SteamAPI_ISteamMatchmaking_JoinLobby");
    p->leave_lobby = p->find_symbol<flat::LobbyVoid>("SteamAPI_ISteamMatchmaking_LeaveLobby");
    p->invite = p->find_symbol<flat::Invite>("SteamAPI_ISteamMatchmaking_InviteUserToLobby");
    p->request_data =
        p->find_symbol<flat::LobbyBool>("SteamAPI_ISteamMatchmaking_RequestLobbyData");
    p->lobby_data = p->find_symbol<flat::LobbyString>("SteamAPI_ISteamMatchmaking_GetLobbyData");
    p->set_lobby_data = p->find_symbol<flat::LobbySet>("SteamAPI_ISteamMatchmaking_SetLobbyData");
    p->delete_lobby_data =
        p->find_symbol<flat::LobbyDelete>("SteamAPI_ISteamMatchmaking_DeleteLobbyData");
    p->owner = p->find_symbol<flat::LobbyOwner>("SteamAPI_ISteamMatchmaking_GetLobbyOwner");
    p->member_count =
        p->find_symbol<flat::MemberCount>("SteamAPI_ISteamMatchmaking_GetNumLobbyMembers");
    p->member_by_index =
        p->find_symbol<flat::MemberByIndex>("SteamAPI_ISteamMatchmaking_GetLobbyMemberByIndex");
    p->member_data =
        p->find_symbol<flat::MemberString>("SteamAPI_ISteamMatchmaking_GetLobbyMemberData");
    p->set_member_data =
        p->find_symbol<flat::MemberSet>("SteamAPI_ISteamMatchmaking_SetLobbyMemberData");
    p->set_joinable =
        p->find_symbol<flat::LobbyJoinable>("SteamAPI_ISteamMatchmaking_SetLobbyJoinable");
    p->set_limit =
        p->find_symbol<flat::LobbyLimit>("SteamAPI_ISteamMatchmaking_SetLobbyMemberLimit");
    p->send_message =
        p->find_symbol<flat::LobbySend>("SteamAPI_ISteamMatchmaking_SendLobbyChatMsg");
    p->read_message =
        p->find_symbol<flat::LobbyRead>("SteamAPI_ISteamMatchmaking_GetLobbyChatEntry");
    p->set_type = p->find_symbol<flat::LobbyLimit>("SteamAPI_ISteamMatchmaking_SetLobbyType");
    if (p->error) return false;
    // Optional capability: older runtimes must retain their existing platform services.
    const auto utils = p->optional_symbol<flat::GetInterface>("SteamAPI_SteamUtils_v011");
    p->overlay_enabled = p->optional_symbol<flat::OverlayEnabled>("SteamAPI_ISteamUtils_IsOverlayEnabled");
    p->activate_overlay = p->optional_symbol<flat::SetString>("SteamAPI_ISteamFriends_ActivateGameOverlay");
    p->activate_invite = p->optional_symbol<flat::LobbyVoid>("SteamAPI_ISteamFriends_ActivateGameOverlayInviteDialog");
    if (utils && p->overlay_enabled && p->activate_overlay && p->activate_invite)
        p->overlay_utils = utils();
    return !p->error;
}

core::OverlayState Runtime::overlay_status() {
    if (p->error || !p->friends_api || !p->overlay_utils) return {};
    return {true, p->overlay_enabled(p->overlay_utils), p->overlay_active};
}
bool Runtime::overlay_friends() {
    if (!overlay_status().available) return false;
    p->activate_overlay(p->friends_api, "friends");
    return true;
}
bool Runtime::overlay_invite(std::uint64_t lobby) {
    if (!lobby || !overlay_status().available) return false;
    p->activate_invite(p->friends_api, lobby);
    return true;
}

std::vector<core::Friend> Runtime::friends() {
    std::vector<core::Friend> result;
    const auto count = p->friend_count(p->friends_api, 4);
    if (count < 0 || count > 5000) {
        p->error.emplace(ErrorCode::callback_abi, "Invalid friend count");
        return {};
    }
    for (int i = 0; i < count; ++i) {
        auto user = p->friend_by_index(p->friends_api, i, 4);
        auto name = p->friend_name(p->friends_api, user);
        result.push_back({user, name ? std::string(name, strnlen(name, 256)) : std::string{},
                          p->friend_state(p->friends_api, user) != 0});
        if (p->presence_requested.insert(user).second)
            p->request_presence(p->friends_api, user);
    }
    return result;
}
bool Runtime::start_p2p() {
    auto utilities =
        p->find_symbol<flat::GetInterface>("SteamAPI_SteamNetworkingUtils_SteamAPI_v004");
    auto init_relay = p->find_symbol<flat::InterfaceVoid>(
        "SteamAPI_ISteamNetworkingUtils_InitRelayNetworkAccess");
    auto get = p->find_symbol<flat::GetInterface>("SteamAPI_SteamNetworkingMessages_SteamAPI_v002");
    p->filter_string = p->find_symbol<flat::LobbyFilterString>(
        "SteamAPI_ISteamMatchmaking_AddRequestLobbyListStringFilter");
    p->filter_distance = p->find_symbol<flat::InterfaceInt>(
        "SteamAPI_ISteamMatchmaking_AddRequestLobbyListDistanceFilter");
    p->filter_slots = p->find_symbol<flat::InterfaceInt>(
        "SteamAPI_ISteamMatchmaking_AddRequestLobbyListFilterSlotsAvailable");
    p->filter_count = p->find_symbol<flat::InterfaceInt>(
        "SteamAPI_ISteamMatchmaking_AddRequestLobbyListResultCountFilter");
    p->lobby_search =
        p->find_symbol<flat::LobbySearch>("SteamAPI_ISteamMatchmaking_RequestLobbyList");
    p->lobby_index = p->find_symbol<flat::LobbyIndex>("SteamAPI_ISteamMatchmaking_GetLobbyByIndex");
    p->network_send =
        p->find_symbol<flat::NetworkSend>("SteamAPI_ISteamNetworkingMessages_SendMessageToUser");
    p->network_receive = p->find_symbol<flat::NetworkReceive>(
        "SteamAPI_ISteamNetworkingMessages_ReceiveMessagesOnChannel");
    p->network_accept = p->find_symbol<flat::NetworkSession>(
        "SteamAPI_ISteamNetworkingMessages_AcceptSessionWithUser");
    p->network_close = p->find_symbol<flat::NetworkSession>(
        "SteamAPI_ISteamNetworkingMessages_CloseSessionWithUser");
    p->network_release =
        p->find_symbol<flat::NetworkRelease>("SteamAPI_SteamNetworkingMessage_t_Release");
    if (p->error)
        return false;
    p->networking = get();
    if (!p->networking) {
        p->error.emplace(ErrorCode::symbol_missing, "Steam networking interface unavailable");
        return false;
    }
    auto utils = utilities();
    if (!utils) {
        p->error.emplace(ErrorCode::symbol_missing, "Steam networking utilities unavailable");
        return false;
    }
    init_relay(utils);
    return true;
}
std::uint64_t Runtime::create_room(int limit) {
    if (!p->networking || p->calls.size() >= 128)
        return 0;
    auto call = p->create_lobby(p->matchmaking, 3, limit);
    if (call)
        p->calls[call] = flat::lobby_created;
    return call;
}
std::uint64_t Runtime::search_rooms(const std::map<std::string, std::string> &filters,
                                    int distance) {
    if (!p->networking || p->calls.size() >= 128 ||
        std::any_of(p->calls.begin(), p->calls.end(),
                    [](auto &value) { return value.second == flat::lobby_list; }))
        return 0;
    for (auto &[key, value] : filters)
        p->filter_string(p->matchmaking, key.c_str(), value.c_str(), 0);
    p->filter_distance(p->matchmaking, distance);
    p->filter_slots(p->matchmaking, 1);
    p->filter_count(p->matchmaking, 32);
    auto call = p->lobby_search(p->matchmaking);
    if (call)
        p->calls[call] = flat::lobby_list;
    return call;
}
bool Runtime::send_peer(std::uint64_t peer, int channel, const std::string &bytes) {
    if (!p->networking || !p->allowed_peers.contains(peer) || bytes.size() > 65519 ||
        (channel != 47 && channel != 48 && channel != 49))
        return false;
    flat::Identity identity{};
    identity.type = 16;
    identity.size = 8;
    identity.steam_id = peer;
    auto result = p->network_send(p->networking, &identity, bytes.data(),
                                  (std::uint32_t)bytes.size(), channel == 48 ? 1 : 9, channel);
    if (result == 3) // NoConnection: reset a broken session, but do not replay the command.
        p->network_close(p->networking, &identity);
    return result == 1;
}
std::vector<core::PeerMessage> Runtime::take_peer_messages() {
    std::vector<core::PeerMessage> out;
    if (!p->networking)
        return out;
    for (int channel : {47, 48, 49}) {
        flat::NetworkingMessage *messages[128]{};
        int count = p->network_receive(p->networking, channel, messages, 128);
        if (count < 0 || count > 128) {
            p->error.emplace(ErrorCode::callback_abi, "Invalid networking message count");
            return {};
        }
        for (int i = 0; i < count; ++i) {
            auto *message = messages[i];
            if (!message)
                continue;
            if (message->peer.type == 16 && message->peer.size == 8 &&
                p->allowed_peers.contains(message->peer.steam_id) && message->channel == channel &&
                message->size >= 0 && message->size <= 65519 && message->data)
                out.push_back({message->peer.steam_id, channel,
                               std::string((char *)message->data, message->size)});
            p->network_release(message);
        }
    }
    return out;
}
void Runtime::allow_peers(const std::vector<std::uint64_t> &peers) {
    if (!p->networking)
        return;
    std::set<std::uint64_t> next(peers.begin(), peers.end());
    for (auto peer : p->allowed_peers)
        if (!next.contains(peer)) {
            flat::Identity identity{};
            identity.type = 16;
            identity.size = 8;
            identity.steam_id = peer;
            p->network_close(p->networking, &identity);
        }
    p->allowed_peers = std::move(next);
    for (auto peer : p->pending_peer_requests) {
        flat::Identity identity{};
        identity.type = 16;
        identity.size = 8;
        identity.steam_id = peer;
        if (p->allowed_peers.contains(peer))
            p->network_accept(p->networking, &identity);
        else
            p->network_close(p->networking, &identity);
    }
    p->pending_peer_requests.clear();
}
std::string Runtime::persona() {
    auto name = p->persona_name(p->friends_api);
    return name ? std::string(name, strnlen(name, 256)) : std::string{};
}
std::string Runtime::presence(std::uint64_t user, const std::string &key) {
    auto value = p->get_presence(p->friends_api, user, key.c_str());
    return value ? std::string(value, strnlen(value, 256)) : std::string{};
}
bool Runtime::set_presence(const std::string &key, const std::string &value) {
    return p->set_presence(p->friends_api, key.c_str(), value.c_str());
}
std::uint64_t Runtime::create_lobby(int limit) {
    if (p->calls.size() >= 128)
        return 0;
    auto call = p->create_lobby(p->matchmaking, 0, limit);
    if (call)
        p->calls[call] = flat::lobby_created;
    return call;
}
std::uint64_t Runtime::join_lobby(std::uint64_t lobby) {
    if (p->calls.size() >= 128)
        return 0;
    auto call = p->join_lobby(p->matchmaking, lobby);
    if (call)
        p->calls[call] = flat::lobby_enter;
    return call;
}
void Runtime::leave_lobby(std::uint64_t lobby) { p->leave_lobby(p->matchmaking, lobby); }
bool Runtime::invite(std::uint64_t lobby, std::uint64_t user) {
    return p->invite(p->matchmaking, lobby, user);
}
bool Runtime::request_data(std::uint64_t lobby) { return p->request_data(p->matchmaking, lobby); }
std::string Runtime::lobby_data(std::uint64_t lobby, const std::string &key) {
    const auto value = p->lobby_data(p->matchmaking, lobby, key.c_str());
    return value ? std::string(value, strnlen(value, 8192)) : std::string{};
}
bool Runtime::set_lobby_data(std::uint64_t lobby, const std::string &key,
                             const std::string &value) {
    return p->set_lobby_data(p->matchmaking, lobby, key.c_str(), value.c_str());
}
bool Runtime::delete_lobby_data(std::uint64_t lobby, const std::string &key) {
    return p->delete_lobby_data(p->matchmaking, lobby, key.c_str());
}
std::uint64_t Runtime::owner(std::uint64_t lobby) { return p->owner(p->matchmaking, lobby); }
std::vector<std::uint64_t> Runtime::members(std::uint64_t lobby) {
    std::vector<std::uint64_t> result;
    const auto count = p->member_count(p->matchmaking, lobby);
    if (count < 0 || count > 250) {
        p->error.emplace(ErrorCode::callback_abi, "Invalid lobby member count");
        return {};
    }
    for (int i = 0; i < count; ++i)
        result.push_back(p->member_by_index(p->matchmaking, lobby, i));
    return result;
}
std::string Runtime::member_data(std::uint64_t lobby, std::uint64_t user, const std::string &key) {
    auto value = p->member_data(p->matchmaking, lobby, user, key.c_str());
    return value ? std::string(value, strnlen(value, 8192)) : std::string{};
}
void Runtime::set_member_data(std::uint64_t lobby, const std::string &key,
                              const std::string &value) {
    p->set_member_data(p->matchmaking, lobby, key.c_str(), value.c_str());
}
bool Runtime::set_joinable(std::uint64_t lobby, bool joinable) {
    return p->set_joinable(p->matchmaking, lobby, joinable);
}
bool Runtime::set_limit(std::uint64_t lobby, int limit) {
    return p->set_limit(p->matchmaking, lobby, limit);
}
bool Runtime::set_friends_visible(std::uint64_t lobby, bool visible) {
    return p->set_type(p->matchmaking, lobby, visible ? 1 : 0);
}
bool Runtime::send_message(std::uint64_t lobby, const std::string &bytes) {
    return bytes.size() <= 4096 &&
           p->send_message(p->matchmaking, lobby, bytes.data(), static_cast<int>(bytes.size()));
}
std::vector<core::SocialEvent> Runtime::take_social_events() {
    std::vector<core::SocialEvent> events;
    events.swap(p->social_events);
    return events;
}
bool Runtime::logged_on() const { return p->api && p->logged(p->api); }
std::uint64_t Runtime::id() const {
    if (!p->api)
        throw Error(ErrorCode::arguments, "Runtime is not initialized");
    return p->get_id(p->api);
}
Ticket Runtime::request_ticket(std::uint64_t bound_server) {
    auto ticket = try_request_ticket(bound_server);
    if (!ticket)
        throw *p->error;
    return std::move(*ticket);
}
std::optional<Ticket> Runtime::try_request_ticket(std::uint64_t bound_server) {
    if (p->error)
        return {};
    if (p->mode != Impl::Mode::client || !p->api) {
        p->error.emplace(ErrorCode::arguments, "Wrong runtime mode or initialization incomplete");
        return {};
    }
    Ticket ticket{0, std::vector<std::uint8_t>(2048)};
    std::uint32_t length = 0;
    flat::Identity identity{};
    identity.type = 16;
    identity.size = 8;
    identity.steam_id = bound_server;
    ticket.handle =
        p->get_ticket(p->api, ticket.bytes.data(), static_cast<int>(ticket.bytes.size()), &length,
                      bound_server ? &identity : nullptr);
    if (ticket.handle)
        p->tickets.insert(ticket.handle);
    if (!ticket.handle || !length || length > ticket.bytes.size()) {
        if (ticket.handle) {
            p->cancel(p->api, ticket.handle);
            p->tickets.erase(ticket.handle);
        }
        p->error.emplace(ErrorCode::ticket, "GetAuthSessionTicket failed or exceeded buffer");
        return {};
    }
    ticket.bytes.resize(length);
    return ticket;
}
void Runtime::cancel_ticket(std::uint32_t handle) {
    p->require(Impl::Mode::client);
    if (p->tickets.erase(handle))
        p->cancel(p->api, handle);
}
int Runtime::begin_auth(const std::vector<std::uint8_t> &ticket, std::uint64_t user) {
    p->require(Impl::Mode::server);
    if (ticket.empty() || ticket.size() > 2048 || !user)
        throw Error(ErrorCode::token, "Invalid ticket size or user");
    int result = p->begin(p->api, ticket.data(), static_cast<int>(ticket.size()), user);
    if (result == 0)
        p->sessions.insert(user);
    return result;
}
void Runtime::end_auth(std::uint64_t user) {
    p->require(Impl::Mode::server);
    if (p->sessions.erase(user))
        p->end(p->api, user);
}
std::vector<Event> Runtime::poll() {
    auto events = try_poll();
    if (p->error)
        throw *p->error;
    return events;
}
std::vector<Event> Runtime::try_poll() {
    if (p->error)
        return {};
    if (!p->pipe) {
        p->error.emplace(ErrorCode::arguments, "No callback pipe");
        return {};
    }
    p->frame(p->pipe);
    std::vector<Event> events;
    flat::Callback cb{};
    // Limit each drain so a callback flood cannot defeat the caller's deadline.
    for (int count = 0; count < 1024 && p->next(p->pipe, &cb); ++count) {
        struct Release {
            Impl *p;
            ~Release() { p->free_callback(p->pipe); }
        } release{p.get()};
        switch (cb.kind) {
        case flat::overlay_activated: {
            if (!p->friends_api) break;
            const auto value = payload<flat::OverlayActivated>(cb, p->error);
            if (p->error) return {};
            if (value.active > 1 || value.user_initiated > 1) {
                p->error.emplace(ErrorCode::callback_abi, "Invalid overlay callback state");
                return {};
            }
            if (value.app != p->app_id) break;
            p->overlay_active = value.active != 0;
            p->social_events.push_back({core::SocialEvent::overlay_changed, 0, 0, 0, true,
                                       static_cast<int>(value.active)});
            break;
        }
        case flat::api_call_completed: {
            if (!p->call_result)
                break;
            const auto completed = payload<flat::CallCompleted>(cb, p->error);
            if (p->error)
                return {};
            const auto it = p->calls.find(completed.call);
            if (it == p->calls.end())
                break;
            bool failed = true;
            if (it->second == flat::lobby_created) {
                flat::LobbyCreated value{};
                const bool ok = completed.callback == it->second &&
                                completed.size == sizeof(value) &&
                                p->call_result(p->pipe, completed.call, &value, sizeof(value),
                                               it->second, &failed);
                p->social_events.push_back({core::SocialEvent::created, completed.call, value.lobby,
                                            0, ok && !failed && value.result == 1,
                                            !ok      ? -1
                                            : failed ? -2
                                                     : value.result});
            } else if (it->second == flat::lobby_list) {
                flat::LobbyList value{};
                const bool ok = completed.callback == it->second &&
                                completed.size == sizeof(value) &&
                                p->call_result(p->pipe, completed.call, &value, sizeof(value),
                                               it->second, &failed);
                core::SocialEvent event{core::SocialEvent::listed, completed.call, 0, 0,
                                        ok && !failed && value.count <= 32};
                if (event.success)
                    for (std::uint32_t index = 0; index < value.count; ++index)
                        event.lobbies.push_back(p->lobby_index(p->matchmaking, (int)index));
                p->social_events.push_back(std::move(event));
            } else {
                flat::LobbyEnter value{};
                const bool ok = completed.callback == it->second &&
                                completed.size == sizeof(value) &&
                                p->call_result(p->pipe, completed.call, &value, sizeof(value),
                                               it->second, &failed);
                p->social_events.push_back({core::SocialEvent::entered, completed.call, value.lobby,
                                            0, ok && !failed && value.response == 1,
                                            !ok      ? -1
                                            : failed ? -2
                                                     : static_cast<int>(value.response)});
            }
            p->calls.erase(it);
            break;
        }
        case flat::lobby_data: {
            if (!p->matchmaking)
                break;
            auto value = payload<flat::LobbyData>(cb, p->error);
            if (p->error)
                return {};
            p->social_events.push_back(
                {core::SocialEvent::data, 0, value.lobby, value.member, value.success != 0});
            break;
        }
        case flat::networking_session_request: {
            if (!p->networking)
                break;
            auto identity = payload<flat::Identity>(cb, p->error);
            if (p->error)
                return {};
            if (identity.type == 16 && identity.size == 8 && identity.steam_id &&
                p->pending_peer_requests.size() < 128) {
                // Core refreshes friend/room eligibility before allow_peers decides this batch.
                p->pending_peer_requests.insert(identity.steam_id);
                p->social_events.push_back({core::SocialEvent::friends_changed});
            } else
                p->network_close(p->networking, &identity);
            break;
        }
        case flat::lobby_chat: {
            if (!p->matchmaking)
                break;
            auto value = payload<flat::LobbyChat>(cb, p->error);
            if (p->error)
                return {};
            p->social_events.push_back(
                {core::SocialEvent::member, 0, value.lobby, value.user, true});
            break;
        }
        case flat::lobby_message: {
            if (!p->matchmaking)
                break;
            auto value = payload<flat::LobbyMessage>(cb, p->error);
            if (p->error)
                return {};
            if (value.type != 1 || value.entry > 0x7fffffff)
                break;
            std::uint64_t sender = 0;
            std::int32_t type = 0;
            char bytes[4096];
            const int size =
                p->read_message(p->matchmaking, value.lobby, static_cast<int>(value.entry), &sender,
                                bytes, sizeof(bytes), &type);
            if (size <= 0 || size > sizeof(bytes) || type != 1 || sender != value.user)
                break;
            p->social_events.push_back({core::SocialEvent::message, 0, value.lobby, sender, true, 0,
                                        std::string(bytes, size)});
            break;
        }
        case flat::lobby_invite: {
            if (!p->matchmaking)
                break;
            auto value = payload<flat::LobbyInvite>(cb, p->error);
            if (p->error)
                return {};
            if (value.game == p->app_id)
                p->social_events.push_back(
                    {core::SocialEvent::invitation, 0, value.lobby, value.user, true});
            break;
        }
        case flat::lobby_join_requested: {
            if (!p->matchmaking)
                break;
            auto value = payload<flat::LobbyJoinRequested>(cb, p->error);
            if (p->error)
                return {};
            p->social_events.push_back(
                {core::SocialEvent::join_requested, 0, value.lobby, value.user, true});
            break;
        }
        case flat::persona_changed:
            if (p->friends_api)
                p->social_events.push_back({core::SocialEvent::friends_changed});
            break;
        case flat::rich_presence_changed: {
            if (!p->friends_api)
                break;
            auto value = payload<flat::RichPresenceChanged>(cb, p->error);
            if (p->error)
                return {};
            if (value.app == p->app_id)
                p->social_events.push_back({core::SocialEvent::friends_changed});
            break;
        }
        case flat::connected:
            events.push_back({Event::Kind::connected});
            break;
        // SteamShutdown_t has no fields; no SDK payload pointer escapes poll.
        case flat::shutdown_requested:
            events.push_back({Event::Kind::shutdown_requested});
            break;
        case flat::connect_failure: {
            auto value = payload<flat::ConnectFailure>(cb, p->error);
            if (p->error)
                return {};
            events.push_back({Event::Kind::connect_failure, value.result, value.retrying});
            break;
        }
        case flat::disconnected: {
            const auto value = payload<flat::Disconnected>(cb, p->error);
            if (p->error)
                return {};
            events.push_back({Event::Kind::disconnected, value.result});
            break;
        }
        case flat::ticket_response: {
            auto value = payload<flat::TicketResponse>(cb, p->error);
            if (p->error)
                return {};
            events.push_back({Event::Kind::ticket, value.result, false, value.ticket});
            break;
        }
        case flat::validation: {
            auto value = payload<flat::Validation>(cb, p->error);
            if (p->error)
                return {};
            events.push_back(
                {Event::Kind::validation, value.result, false, 0, value.user, value.owner});
            break;
        }
        default:
            break;
        }
    }
    return events;
}
const std::filesystem::path &Runtime::library_path() const { return p->path; }
std::string Runtime::steamclient_path() const {
#ifdef _WIN32
    auto module = GetModuleHandleW(L"steamclient64.dll");
    if (!module)
        return "not observed";
    std::array<wchar_t, 32768> path{};
    auto length = GetModuleFileNameW(module, path.data(), static_cast<DWORD>(path.size()));
    return std::filesystem::path(std::wstring(path.data(), length)).string();
#else
    std::string result = "not observed";
    dl_iterate_phdr(
        [](dl_phdr_info *info, std::size_t, void *context) {
            if (std::string(info->dlpi_name).find("steamclient.so") != std::string::npos) {
                *static_cast<std::string *>(context) = info->dlpi_name;
                return 1;
            }
            return 0;
        },
        &result);
    return result;
#endif
}
const char *error_name(ErrorCode code) {
    switch (code) {
    case ErrorCode::library_missing:
        return "LIBRARY_MISSING";
    case ErrorCode::symbol_missing:
        return "SYMBOL_MISSING";
    case ErrorCode::client_unavailable:
        return "CLIENT_UNAVAILABLE";
    case ErrorCode::signed_out:
        return "SIGNED_OUT";
    case ErrorCode::server_init:
        return "SERVER_INIT_FAILED";
    case ErrorCode::server_logon:
        return "SERVER_LOGON_FAILED";
    case ErrorCode::timeout:
        return "TIMEOUT";
    case ErrorCode::ticket:
        return "TICKET_FAILED";
    case ErrorCode::token:
        return "INVALID_TOKEN";
    case ErrorCode::io:
        return "IO_FAILED";
    case ErrorCode::arguments:
        return "INVALID_ARGUMENTS";
    case ErrorCode::auth_rejected:
        return "AUTH_REJECTED";
    case ErrorCode::callback_abi:
        return "CALLBACK_ABI_MISMATCH";
    case ErrorCode::library_load:
        return "LIBRARY_LOAD_FAILED";
    case ErrorCode::steam_shutdown:
        return "STEAM_SHUTDOWN";
    }
    return "UNKNOWN";
}
const char *begin_result_name(int result) {
    constexpr const char *names[] = {
        "OK",           "InvalidTicket", "DuplicateRequest", "InvalidVersion",
        "GameMismatch", "ExpiredTicket"};
    return result >= 0 && result < 6 ? names[result] : "Unknown";
}
const char *validation_result_name(int result) {
    constexpr const char *names[] = {"OK",
                                     "UserNotConnectedToSteam",
                                     "NoLicenseOrExpired",
                                     "VACBanned",
                                     "LoggedInElseWhere",
                                     "VACCheckTimedOut",
                                     "AuthTicketCanceled",
                                     "AuthTicketInvalidAlreadyUsed",
                                     "AuthTicketInvalid",
                                     "PublisherIssuedBan",
                                     "AuthTicketNetworkIdentityFailure"};
    return result >= 0 && result < 11 ? names[result] : "Unknown";
}
} // namespace osw::steam
