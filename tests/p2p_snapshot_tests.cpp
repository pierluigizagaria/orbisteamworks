#include "core/p2p.hpp"
#include <algorithm>
#include <iostream>
#include <map>
#include <nlohmann/json.hpp>
#include <set>
#include <stdexcept>

using namespace osw::core;
using Json = nlohmann::json;
using Id = std::uint64_t;
constexpr Id self = 100001, peer = 100002, room = 100003;

void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct ReadFailure : std::runtime_error {
    ReadFailure() : std::runtime_error("fixture read failure") {}
};
Config configuration() {
    Config out;
    out.title = "snapshot-title";
    out.build = "1";
    out.realm = "snapshot-fixture";
    return out;
}

// Both modes expose the same backend values. The snapshot mode copies fields
// directly, while scalar getters can have observable backend error side effects.
struct Fixture : Social {
    bool snapshots;
    std::map<std::string, std::string> data;
    std::set<std::string> failed_data;
    std::map<std::string, unsigned> scalar_reads;
    std::vector<Id> users{self, peer};
    Id actual_owner = peer, call = 0;
    unsigned owner_reads = 0, member_reads = 0, member_side_effects = 0;
    unsigned sends = 0, joins = 0, leaves = 0;
    bool fail_owner = false, fail_members = false;

    explicit Fixture(bool use_snapshots) : snapshots(use_snapshots) {
        const auto config = configuration();
        data = {{"osw_room", "osw/1"}, {"osw_title", config.title},
                {"osw_build", config.build}, {"osw_realm", config.realm},
                {"osw_room_protocol", "1"}, {"osw_room_owner", std::to_string(peer)},
                {"osw_room_metadata", "{}"}};
    }
    std::string data_value(const std::string &key) const {
        if (failed_data.contains(key)) throw ReadFailure();
        const auto it = data.find(key);
        return it == data.end() ? std::string{} : it->second;
    }
    Id owner_value() const {
        if (fail_owner) throw ReadFailure();
        return actual_owner;
    }
    std::vector<Id> member_values() const {
        if (fail_members) throw ReadFailure();
        return users;
    }
    std::optional<LobbySnapshot> lobby_snapshot(
        Id id, const std::vector<std::string> &keys, bool membership) override {
        if (!snapshots) return Social::lobby_snapshot(id, keys, membership);
        LobbySnapshot out;
        for (const auto &key : keys)
            out.data[key].read([&] { return data_value(key); });
        if (membership) {
            out.owner.read([&] { return owner_value(); });
            out.members.read([&] { return member_values(); });
        }
        return out;
    }
    std::vector<Friend> friends() override { return {}; }
    std::string presence(Id, const std::string &) override { return {}; }
    bool set_presence(const std::string &, const std::string &) override { return true; }
    Id create_lobby(int) override { return ++call; }
    Id search_rooms(const std::map<std::string, std::string> &, int) override { return ++call; }
    Id join_lobby(Id) override { ++joins; return ++call; }
    void leave_lobby(Id) override { ++leaves; std::erase(users, self); }
    bool invite(Id, Id) override { return true; }
    bool request_data(Id) override { return true; }
    std::string lobby_data(Id, const std::string &key) override {
        ++scalar_reads[key];
        return data_value(key);
    }
    bool set_lobby_data(Id, const std::string &key, const std::string &value) override {
        data[key] = value;
        return true;
    }
    bool delete_lobby_data(Id, const std::string &key) override { data.erase(key); return true; }
    Id owner(Id) override { ++owner_reads; return owner_value(); }
    std::vector<Id> members(Id) override {
        ++member_reads;
        if (fail_members) ++member_side_effects;
        return member_values();
    }
    std::string member_data(Id, Id, const std::string &) override { return {}; }
    void set_member_data(Id, const std::string &, const std::string &) override {}
    bool set_joinable(Id, bool) override { return true; }
    bool set_limit(Id, int) override { return true; }
    bool set_friends_visible(Id, bool) override { return true; }
    bool send_message(Id, const std::string &) override { return true; }
    bool send_peer(Id, int, const std::string &) override { ++sends; return true; }
};

void submit(P2P &p2p, Id request, Json command) {
    p2p.submit({request, OSW_P2P, 30000, command.dump()});
}
Result completed(P2P &p2p, Id request) {
    auto update = p2p.poll();
    check(!update.failed && update.results.size() == 1 && update.results[0].id == request,
          "operation has exactly one completion and no provider failure");
    return update.results[0];
}
SocialEvent listed(Id call) {
    SocialEvent event{SocialEvent::listed, call, 0, 0, true};
    event.lobbies = {room};
    return event;
}
Result search(P2P &p2p, Fixture &social) {
    submit(p2p, 1, {{"op", "directory.search"}});
    check(p2p.poll().results.empty(), "search remains pending before backend completion");
    for (unsigned attempt = 0; attempt < 8; ++attempt) {
        p2p.process({listed(social.call)});
        auto update = p2p.poll();
        if (update.results.empty()) continue;
        check(!update.failed && update.results.size() == 1 && update.results[0].id == 1,
              "search completes once after listed callbacks");
        return update.results[0];
    }
    throw std::runtime_error("search did not complete within the bounded fixture callbacks");
}
void begin_join(P2P &p2p, Fixture &social) {
    submit(p2p, 1, {{"op", "directory.join"}, {"room_id", std::to_string(room)}});
    check(social.joins == 1 && p2p.poll().results.empty(),
          "join waits for the backend entered callback");
}
void entered(P2P &p2p, const Fixture &social) {
    p2p.process({{SocialEvent::entered, social.call, room, self, true}});
}
void check_no_membership_access(const Fixture &social) {
    check(social.owner_reads == 0 && social.member_reads == 0 && social.member_side_effects == 0,
          "rejected or public room must not consult scalar membership or mutate backend errors");
}

void public_discovery(bool snapshots) {
    for (bool foreign : {false, true}) {
        Fixture social(snapshots);
        social.fail_owner = social.fail_members = true;
        if (foreign) {
            social.data["osw_build"] = "foreign-build";
            social.failed_data.insert("osw_room_metadata");
        }
        P2P p2p(configuration(), {true, self, "Local"}, social);
        const auto result = search(p2p, social);
        check(result.status == OSW_OK, "public discovery succeeds without membership visibility");
        const auto rooms = Json::parse(result.metadata).at("rooms");
        if (foreign) {
            check(rooms.empty(), "foreign namespace is excluded before later field errors");
            check(snapshots || social.scalar_reads["osw_room_metadata"] == 0,
                  "scalar discovery preserves the namespace short circuit");
        } else {
            check(rooms.size() == 1 && rooms[0]["members"].empty() &&
                      rooms[0]["owner"] == std::to_string(peer),
                  "public result uses declared owner and exposes no private membership");
        }
        check_no_membership_access(social);
    }
    Fixture social(snapshots);
    social.data["osw_room_metadata"] = R"({"unrecognized":1})";
    social.fail_owner = social.fail_members = true;
    P2P p2p(configuration(), {true, self, "Local"}, social);
    const auto result = search(p2p, social);
    check(result.status == OSW_OK && Json::parse(result.metadata)["rooms"].empty(),
          "compatible namespace with invalid metadata is excluded");
    check_no_membership_access(social);
}

void entered_revalidation(bool snapshots) {
    for (unsigned failure = 0; failure < 3; ++failure) {
        Fixture social(snapshots);
        P2P p2p(configuration(), {true, self, "Local"}, social);
        begin_join(p2p, social);
        social.fail_members = true;
        if (failure == 0) {
            social.data["osw_build"] = "foreign-build";
            social.failed_data.insert("osw_room_metadata");
            social.fail_owner = true;
        } else if (failure == 1) {
            social.data["osw_room_metadata"] = "[]";
            social.fail_owner = true;
        } else {
            social.actual_owner = self;
        }
        entered(p2p, social);
        const auto result = completed(p2p, 1);
        check(result.status == OSW_UNAVAILABLE && social.leaves == 1 &&
                  Json::parse(result.metadata)["error"] == "namespace_mismatch",
              "entered callback revalidates current room and leaves rejected membership");
        check(social.member_reads == 0 && social.member_side_effects == 0,
              "namespace, metadata and owner rejection precede member getters");
        if (failure != 2) check_no_membership_access(social);
    }
}

void required_errors(bool snapshots) {
    {
        Fixture social(snapshots);
        social.failed_data.insert("osw_room_metadata");
        P2P p2p(configuration(), {true, self, "Local"}, social);
        submit(p2p, 1, {{"op", "directory.search"}});
        bool propagated = false;
        try { p2p.process({listed(social.call)}); }
        catch (const ReadFailure &) { propagated = true; }
        check(propagated && p2p.poll().results.empty(),
              "required read failure propagates from asynchronous processing without success");
    }
    {
        Fixture social(snapshots);
        social.failed_data.insert("osw_room_metadata");
        P2P p2p(configuration(), {true, self, "Local"}, social);
        submit(p2p, 1, {{"op", "directory.join"}, {"room_id", std::to_string(room)}});
        const auto result = completed(p2p, 1);
        check(result.status == OSW_UNAVAILABLE && social.joins == 0 &&
                  Json::parse(result.metadata)["error"] == "invalid_request",
              "required read failure in command dispatch refuses the join");
    }
    {
        Fixture social(snapshots);
        P2P p2p(configuration(), {true, self, "Local"}, social);
        begin_join(p2p, social);
        social.fail_members = true;
        bool propagated = false;
        try { entered(p2p, social); }
        catch (const ReadFailure &) { propagated = true; }
        check(propagated && p2p.poll().results.empty(),
              "required membership read error is not converted into a successful entry");
    }
}

void fresh_authorization(bool snapshots) {
    Fixture social(snapshots);
    P2P p2p(configuration(), {true, self, "Local"}, social);
    begin_join(p2p, social);
    entered(p2p, social);
    check(completed(p2p, 1).status == OSW_OK, "valid entered callback admits the room");
    const Json send{{"op", "peer.send"}, {"room_id", std::to_string(room)},
                    {"peer", std::to_string(peer)}, {"payload", "AQ=="}};
    submit(p2p, 2, send);
    check(completed(p2p, 2).status == OSW_OK && social.sends == 1,
          "current peer membership authorizes the opaque send");
    social.users = {self};
    submit(p2p, 3, send);
    check(completed(p2p, 3).status == OSW_UNAVAILABLE && social.sends == 1,
          "peer revocation is enforced before another send without a backend event");
    social.users = {self, peer};
    social.data["osw_build"] = "foreign-build";
    social.failed_data.insert("osw_room_metadata");
    social.fail_owner = social.fail_members = true;
    social.owner_reads = social.member_reads = social.member_side_effects = 0;
    submit(p2p, 4, send);
    const auto changed = completed(p2p, 4);
    check(changed.status == OSW_UNAVAILABLE && social.sends == 1 &&
              Json::parse(changed.metadata)["error"] == "not_member",
          "known room namespace change rejects before later read errors");
    check_no_membership_access(social);
    social.data["osw_build"] = configuration().build;
    social.failed_data.clear();
    social.fail_owner = social.fail_members = false;
    social.users = {peer};
    submit(p2p, 5, {{"op", "directory.members"}, {"room_id", std::to_string(room)}});
    check(completed(p2p, 5).status == OSW_UNAVAILABLE,
          "local membership revocation rejects reads without a backend event");
}

int main() {
    try {
        for (bool snapshots : {false, true}) {
            public_discovery(snapshots);
            entered_revalidation(snapshots);
            required_errors(snapshots);
            fresh_authorization(snapshots);
        }
        std::cout << "P2P snapshot authorization contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n';
        return 1;
    }
}
