#pragma once
#include "core/error.hpp"
#include "core/social.hpp"

#include <cstdint>
#include <filesystem>
#include <memory>
#include <optional>
#include <stdexcept>
#include <string>
#include <vector>

namespace osw::steam {
using ::osw::Error;
using ::osw::ErrorCode;
const char *error_name(ErrorCode code);
struct Event {
    enum class Kind {
        connected,
        connect_failure,
        disconnected,
        ticket,
        validation,
        shutdown_requested
    } kind;
    int result = 0;
    bool retrying = false;
    std::uint32_t ticket = 0;
    std::uint64_t user = 0, owner = 0;
};
struct Ticket {
    std::uint32_t handle;
    std::vector<std::uint8_t> bytes;
};

// Owned by one thread. Poll returns copied payloads, never SDK-owned pointers.
// Destruction cancels tickets, ends accepted sessions and shuts down the API.
class Runtime : public core::Social {
  public:
    explicit Runtime(const std::filesystem::path &library = {});
    static std::unique_ptr<Runtime> create(const std::filesystem::path &library = {});
    ~Runtime();
    Runtime(const Runtime &) = delete;
    Runtime &operator=(const Runtime &) = delete;
    void start_client();
    bool try_start_client();
    void start_server();
    bool start_social();
    std::vector<core::SocialEvent> take_social_events();
    std::vector<core::Friend> friends() override;
    std::string persona();
    std::string presence(std::uint64_t user, const std::string &key) override;
    bool set_presence(const std::string &key, const std::string &value) override;
    std::uint64_t create_lobby(int limit) override;
    std::uint64_t join_lobby(std::uint64_t lobby) override;
    void leave_lobby(std::uint64_t lobby) override;
    bool invite(std::uint64_t lobby, std::uint64_t user) override;
    bool request_data(std::uint64_t lobby) override;
    std::string lobby_data(std::uint64_t lobby, const std::string &key) override;
    bool set_lobby_data(std::uint64_t lobby, const std::string &key,
                        const std::string &value) override;
    bool delete_lobby_data(std::uint64_t lobby, const std::string &key) override;
    std::uint64_t owner(std::uint64_t lobby) override;
    std::vector<std::uint64_t> members(std::uint64_t lobby) override;
    std::string member_data(std::uint64_t lobby, std::uint64_t user,
                            const std::string &key) override;
    void set_member_data(std::uint64_t lobby, const std::string &key,
                         const std::string &value) override;
    bool set_joinable(std::uint64_t lobby, bool joinable) override;
    bool set_limit(std::uint64_t lobby, int limit) override;
    bool set_friends_visible(std::uint64_t lobby, bool visible) override;
    bool send_message(std::uint64_t lobby, const std::string &bytes) override;
    bool logged_on() const;
    std::uint64_t id() const;
    Ticket request_ticket(std::uint64_t bound_server = 0);
    std::optional<Ticket> try_request_ticket(std::uint64_t bound_server = 0);
    void cancel_ticket(std::uint32_t handle);
    int begin_auth(const std::vector<std::uint8_t> &ticket, std::uint64_t user);
    void end_auth(std::uint64_t user);
    std::vector<Event> poll();
    std::vector<Event> try_poll();
    const Error *error() const noexcept;
    std::optional<Error> take_error();
    const std::filesystem::path &library_path() const;
    std::string steamclient_path() const;

  private:
    struct Checked {};
    Runtime(const std::filesystem::path &library, Checked);
    struct Impl;
    std::unique_ptr<Impl> p;
};
const char *begin_result_name(int result);
const char *validation_result_name(int result);
} // namespace osw::steam
