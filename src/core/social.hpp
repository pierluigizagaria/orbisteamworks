#pragma once
#include <cstdint>
#include <map>
#include <string>
#include <vector>

namespace osw::core {
struct Friend {
    std::uint64_t id = 0;
    std::string display_name;
    bool online = false;
};
struct SocialEvent {
    enum Kind {
        created,
        entered,
        data,
        member,
        invitation,
        join_requested,
        friends_changed,
        message,
        listed,
        overlay_changed
    } kind;
    std::uint64_t call = 0, lobby = 0, user = 0;
    bool success = false;
    int detail = 0;
    std::string payload;
    std::vector<std::uint64_t> lobbies;
};
struct PeerMessage {
    std::uint64_t sender = 0;
    int channel = 0;
    std::string bytes;
};
struct Party {
    std::uint64_t lobby = 0, owner = 0;
    std::vector<std::uint64_t> members;
};
struct OverlayState {
    bool supported = false, available = false, active = false;
};
class Social {
  public:
    virtual ~Social() = default;
    virtual std::vector<Friend> friends() = 0;
    virtual std::string presence(std::uint64_t user, const std::string &key) = 0;
    virtual bool set_presence(const std::string &key, const std::string &value) = 0;
    virtual std::uint64_t create_lobby(int limit) = 0;
    virtual std::uint64_t join_lobby(std::uint64_t lobby) = 0;
    virtual void leave_lobby(std::uint64_t lobby) = 0;
    virtual bool invite(std::uint64_t lobby, std::uint64_t user) = 0;
    virtual bool request_data(std::uint64_t lobby) = 0;
    virtual std::string lobby_data(std::uint64_t lobby, const std::string &key) = 0;
    virtual bool set_lobby_data(std::uint64_t lobby, const std::string &key,
                                const std::string &value) = 0;
    virtual bool delete_lobby_data(std::uint64_t lobby, const std::string &key) = 0;
    virtual std::uint64_t owner(std::uint64_t lobby) = 0;
    virtual std::vector<std::uint64_t> members(std::uint64_t lobby) = 0;
    virtual std::string member_data(std::uint64_t lobby, std::uint64_t user,
                                    const std::string &key) = 0;
    virtual void set_member_data(std::uint64_t lobby, const std::string &key,
                                 const std::string &value) = 0;
    virtual bool set_joinable(std::uint64_t lobby, bool joinable) = 0;
    virtual bool set_limit(std::uint64_t lobby, int limit) = 0;
    virtual bool set_friends_visible(std::uint64_t lobby, bool visible) = 0;
    virtual bool send_message(std::uint64_t lobby, const std::string &bytes) = 0;
    virtual std::uint64_t create_room(int) { return 0; }
    virtual std::uint64_t search_rooms(const std::map<std::string, std::string> &, int) {
        return 0;
    }
    virtual bool send_peer(std::uint64_t, int, const std::string &) { return false; }
    virtual std::vector<PeerMessage> take_peer_messages() { return {}; }
    virtual void allow_peers(const std::vector<std::uint64_t> &) {}
    virtual OverlayState overlay_status() { return {}; }
    virtual bool overlay_friends() { return false; }
    virtual bool overlay_invite(std::uint64_t) { return false; }
};
} // namespace osw::core
