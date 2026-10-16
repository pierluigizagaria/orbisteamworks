#include "core/blobs.hpp"
#include <algorithm>
#include <iostream>
#include <thread>
#include <utility>

using namespace osw::core;
using Json = nlohmann::json;
using Id = std::uint64_t;
constexpr Id owner_id = 1, guest_id = 2, lobby = 3;
void check(bool value, const char *message) {
    if (!value) throw std::runtime_error(message);
}
struct Wire : Social {
    Id current_owner = owner_id;
    bool fail_next = false;
    bool fail_all = false;
    size_t attempts = 0;
    std::vector<std::string> messages;
    std::vector<Friend> friends() override { return {}; }
    std::string presence(Id, const std::string &) override { return {}; }
    bool set_presence(const std::string &, const std::string &) override { return false; }
    Id create_lobby(int) override { return 0; }
    Id join_lobby(Id) override { return 0; }
    void leave_lobby(Id) override {}
    bool invite(Id, Id) override { return false; }
    bool request_data(Id) override { return false; }
    std::string lobby_data(Id, const std::string &) override { return {}; }
    bool set_lobby_data(Id, const std::string &, const std::string &) override { return false; }
    bool delete_lobby_data(Id, const std::string &) override { return false; }
    Id owner(Id) override { return current_owner; }
    std::vector<Id> members(Id) override { return {owner_id, guest_id}; }
    std::string member_data(Id, Id, const std::string &) override { return {}; }
    void set_member_data(Id, const std::string &, const std::string &) override {}
    bool set_joinable(Id, bool) override { return false; }
    bool set_limit(Id, int) override { return false; }
    bool set_friends_visible(Id, bool) override { return false; }
    bool send_message(Id id, const std::string &bytes) override {
        check(id == lobby && bytes.size() <= 4096, "lobby and wire frame bounds");
        ++attempts;
        if (std::exchange(fail_next, false) || fail_all) return false;
        messages.push_back(bytes);
        return true;
    }
    std::vector<std::string> take() { return std::exchange(messages, {}); }
};
Json references(const std::string &bytes) {
    return {{"session-image", {{"size", bytes.size()}, {"hash", Blobs::checksum(bytes)}}}};
}
SocialEvent event(const Json &packet, Id sender = owner_id) {
    return {SocialEvent::message, 0, lobby, sender, true, 0, packet.dump()};
}
// A fixed blob/1 owner fixture: no knowledge of windows, new fields or receiver
// state. It serves the same independent aligned offsets as a legacy owner.
Json legacy_reply(const std::string &request, const std::string &payload) {
    const auto packet = Json::parse(request);
    check(packet.size() == 6 && packet["osw"] == "blob/1" && packet["kind"] == "get" &&
              packet["key"] == "session-image" && packet["hash"] == Blobs::checksum(payload),
          "unchanged legacy request schema");
    const auto offset = packet["offset"].get<size_t>();
    check(offset < payload.size() && offset % 2000 == 0, "legacy chunk alignment");
    return {{"osw", "blob/1"}, {"kind", "data"}, {"key", "session-image"},
            {"hash", packet["hash"]}, {"nonce", packet["nonce"]},
            {"to", std::to_string(guest_id)}, {"offset", offset},
            {"bytes", payload.substr(offset, 2000)}};
}
void transfer_with_legacy_owner() {
    Wire wire;
    Blobs receiver(wire, guest_id);
    const std::string payload(104192, 'a');
    const auto refs = references(payload);
    check(receiver.ensure(lobby, refs) == 0, "transfer initially pending");
    size_t requests = 0, rounds = 0;
    while (receiver.ensure(lobby, refs) == 0 && ++rounds <= 53) {
        auto batch = wire.take();
        check(!batch.empty() && batch.size() <= 4, "bounded request window");
        requests += batch.size();
        // Deliberate reverse order and duplicates exercise bounded reassembly.
        std::reverse(batch.begin(), batch.end());
        for (const auto &request : batch) {
            const auto response = legacy_reply(request, payload);
            receiver.message(event(response));
            receiver.message(event(response));
        }
    }
    check(rounds == 14 && requests == 53 && receiver.get(lobby, "session-image") == payload,
          "legacy owner transfers exact bytes in fourteen windows without extra traffic");
}
void gaps_and_untrusted_offsets() {
    Wire wire;
    Blobs receiver(wire, guest_id);
    const std::string payload(20000, 'b');
    const auto refs = references(payload);
    receiver.ensure(lobby, refs);
    auto batch = wire.take();
    check(batch.size() == 4, "initial four requests");
    auto first = legacy_reply(batch.front(), payload);
    auto malformed = first;
    malformed["offset"] = 8000; // Well-formed bytes, but outside the requested window.
    receiver.message(event(malformed));
    malformed = first; malformed["offset"] = 1;
    receiver.message(event(malformed));
    malformed = first; malformed["nonce"] = "stale";
    receiver.message(event(malformed));
    malformed = first; malformed["to"] = "99";
    receiver.message(event(malformed));
    malformed = first; malformed["bytes"] = std::string(1998, 'b');
    receiver.message(event(malformed));
    receiver.message(event(first, 99));
    for (size_t i = 1; i < batch.size(); ++i)
        receiver.message(event(legacy_reply(batch[i], payload)));
    check(wire.messages.empty() && receiver.get(lobby, "session-image").empty(),
          "gap prevents window growth; invalid replies do not fill it");
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    receiver.tick();
    auto retry = wire.take();
    check(retry.size() == 1 && Json::parse(retry.front())["offset"] == 0,
          "only the missing requested chunk is retried after one second");
    receiver.message(event(legacy_reply(retry.front(), payload)));
    check(wire.messages.size() == 4, "filled gap releases the next bounded window");
    while (receiver.ensure(lobby, refs) == 0) {
        const auto next = wire.take();
        check(!next.empty() && next.size() <= 4, "continued bounded progress");
        for (const auto &request : next) receiver.message(event(legacy_reply(request, payload)));
    }
    check(receiver.get(lobby, "session-image") == payload, "gap recovery preserves exact bytes");
}
void cancellation_owner_and_hash() {
    Wire wire;
    Blobs receiver(wire, guest_id);
    const std::string payload(8000, 'c');
    const auto refs = references(payload);
    receiver.ensure(lobby, refs);
    const auto old = wire.take();
    receiver.erase(lobby);
    for (const auto &request : old) receiver.message(event(legacy_reply(request, payload)));
    check(wire.messages.empty() && receiver.get(lobby, "session-image").empty(),
          "leave releases state and late responses cannot recreate it");
    receiver.ensure(lobby, refs);
    auto fresh = wire.take();
    for (const auto &request : old) receiver.message(event(legacy_reply(request, payload)));
    check(receiver.get(lobby, "session-image").empty(), "old nonce cannot fill a new transfer");
    wire.current_owner = 99;
    receiver.message(event(legacy_reply(fresh.front(), payload)));
    receiver.tick();
    check(receiver.ensure(lobby, refs) == -1, "owner change fails the transfer");
    receiver.erase(lobby);
    wire.current_owner = owner_id;
    receiver.ensure(lobby, refs);
    fresh = wire.take();
    for (const auto &request : fresh) {
        auto response = legacy_reply(request, payload);
        response["bytes"] = std::string(2000, 'd');
        receiver.message(event(response));
    }
    check(receiver.ensure(lobby, refs) == -1 && receiver.get(lobby, "session-image").empty(),
          "complete corrupt content fails its unchanged checksum");
}
void legacy_sequential_receiver_and_rate() {
    Wire wire;
    Blobs owner(wire, owner_id);
    const std::string payload(104192, 'e');
    owner.install(lobby, "session-image", payload);
    std::string received;
    for (size_t offset = 0; offset < payload.size(); offset += 2000) {
        Json request{{"osw", "blob/1"}, {"kind", "get"}, {"key", "session-image"},
                     {"hash", Blobs::checksum(payload)}, {"nonce", "1"}, {"offset", offset}};
        owner.message(event(request, guest_id));
        auto messages = wire.take();
        check(messages.size() == 1, "legacy sequential receiver gets one response per request");
        const auto response = Json::parse(messages.front());
        check(response["offset"] == offset && response["nonce"] == "1" && response["to"] == "2",
              "legacy response identity and offset preserved");
        received += response["bytes"].get<std::string>();
    }
    check(received == payload, "old sequential receiver reconstructs exact bytes");
    Wire limited;
    Blobs rate(limited, owner_id);
    rate.install(lobby, "session-image", payload);
    Json request{{"osw", "blob/1"}, {"kind", "get"}, {"key", "session-image"},
                 {"hash", Blobs::checksum(payload)}, {"nonce", "1"}, {"offset", 0}};
    for (int i = 0; i < 201; ++i) rate.message(event(request, guest_id));
    check(limited.messages.size() == 200, "owner rate limit is unchanged");
}
void failed_send_and_parallel_copies() {
    Wire denied;
    denied.fail_next = true;
    Blobs blocked(denied, guest_id);
    const std::string small(8000, 'a');
    blocked.ensure(lobby, references(small));
    auto requested = denied.take();
    check(requested.size() == 3, "failed send does not queue a request");
    auto fabricated = legacy_reply(requested.front(), small);
    fabricated["offset"] = 0;
    blocked.message(event(fabricated));
    for (const auto &request : requested) blocked.message(event(legacy_reply(request, small)));
    check(blocked.get(lobby, "session-image").empty() && denied.messages.empty(),
          "failed send cannot authorize a fabricated otherwise valid response");
    blocked.erase(lobby);

    Wire retry_wire;
    Blobs retrying(retry_wire, guest_id);
    retrying.ensure(lobby, references(small));
    retry_wire.take();
    retry_wire.fail_all = true;
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    retrying.tick();
    const auto attempts = retry_wire.attempts;
    retrying.tick();
    check(attempts == 8 && retry_wire.attempts == attempts,
          "failed retransmissions preserve the one-second backoff");
    retry_wire.fail_all = false;
    std::this_thread::sleep_for(std::chrono::milliseconds(1050));
    retrying.tick();
    const auto recovered = retry_wire.take();
    check(recovered.size() == 4, "failed retransmissions recover after their bounded backoff");
    for (const auto &request : recovered) retrying.message(event(legacy_reply(request, small)));
    check(retrying.get(lobby, "session-image") == small, "retry recovery retains authorization");

    Wire outgoing, incoming;
    Blobs owner(outgoing, owner_id), guest(incoming, guest_id);
    const std::string image(240000, 'b'), data(240000, 'c');
    owner.install(lobby, "session-image", image);
    owner.install(lobby, "session-data", data);
    Json refs = references(image);
    refs["session-data"] = {{"size", data.size()}, {"hash", Blobs::checksum(data)}};
    bool dropped = false;
    const auto began = std::chrono::steady_clock::now();
    size_t served = 0;
    while (guest.ensure(lobby, refs) == 0) {
        check(std::chrono::steady_clock::now() - began < std::chrono::seconds(20),
              "parallel copies recover within the existing twenty-second deadline");
        auto requests = incoming.take();
        check(requests.size() <= 8, "two copies retain separate four-chunk bounds");
        for (const auto &bytes : requests) owner.message(event(Json::parse(bytes), guest_id));
        auto responses = outgoing.take();
        served += responses.size();
        std::reverse(responses.begin(), responses.end());
        for (const auto &bytes : responses) {
            if (!dropped) { dropped = true; continue; }
            guest.message(event(Json::parse(bytes)));
        }
        guest.tick();
        std::this_thread::sleep_for(std::chrono::milliseconds(5));
    }
    check(dropped && served > 200 && guest.get(lobby, "session-image") == image &&
              guest.get(lobby, "session-data") == data,
          "both larger copies survive loss and the shared owner rate limit without starvation");
}
int main() {
    try {
        transfer_with_legacy_owner(); gaps_and_untrusted_offsets();
        cancellation_owner_and_hash(); legacy_sequential_receiver_and_rate();
        failed_send_and_parallel_copies();
        std::cout << "Blob window ordering, bounded recovery and legacy compatibility passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << error.what() << '\n'; return 1;
    }
}
