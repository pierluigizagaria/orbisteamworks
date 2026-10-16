#include "steam/runtime.hpp"
#include <iostream>
#include <stdexcept>
void check(bool value, const char *message) {
    if (!value)
        throw std::runtime_error(message);
}
int main(int argc, char **argv) {
    try {
        check(argc == 2, "fixture path required");
        osw::steam::Runtime runtime(argv[1]);
        runtime.start_client();
        check(runtime.start_social() && runtime.start_p2p(), "network initialization");
        auto create = runtime.create_room(8);
        check(create != 0, "create issued");
        runtime.poll();
        auto events = runtime.take_social_events();
        check(events.size() == 1 && events[0].call == create && events[0].success,
              "invisible lobby completion");
        auto room = events[0].lobby;
        check(runtime.set_lobby_data(room, "osw_room", "osw/1"), "room metadata");
        auto search = runtime.search_rooms({{"osw_room", "osw/1"}}, 1);
        check(search != 0, "search issued");
        check(runtime.search_rooms({}, 1) == 0, "parallel search refused");
        runtime.poll();
        events = runtime.take_social_events();
        check(events.size() == 1 && events[0].kind == osw::core::SocialEvent::listed &&
                  events[0].lobbies.size() == 1,
              "search result mapping");
        runtime.allow_peers({2});
        check(!runtime.send_peer(3, 47, "no"), "unapproved identity rejected");
        check(!runtime.send_peer(2, 47, "not replayed"), "broken session reported and reset");
        runtime.poll();
        events = runtime.take_social_events();
        check(events.size() == 1 && events[0].kind == osw::core::SocialEvent::friends_changed,
              "session request waits for refreshed eligibility");
        runtime.allow_peers({2});
        check(runtime.send_peer(2, 47, std::string("\0\xffmessage", 9)),
              "authenticated reliable message");
        auto messages = runtime.take_peer_messages();
        check(messages.size() == 1 && messages[0].sender == 2 &&
                  messages[0].bytes == std::string("\0\xffmessage", 9),
              "copied message sender/payload");
        runtime.allow_peers({});
        check(!runtime.send_peer(2, 47, "no"), "departed identity rejected");
        runtime.leave_lobby(room);
        std::cout << "Offline Steam flat API room/filter/channel/identity contracts passed\n";
        return 0;
    } catch (const std::exception &error) {
        std::cerr << "FAIL: " << error.what() << "\n";
        return 1;
    }
}
