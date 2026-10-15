# Native P2P design

Decision date: 2026-10-16. Status: authorized implementation, acceptance pending.
This is an explicit replacement design (I), not a recovered Sony contract.

## Superseded decisions

This design extends FEASIBILITY revision 3.1. It supersedes waiting for shadPS4
gate 5 before native integration, keeping all publisher matchmaking unchanged,
and using a remote auth helper for local admission. The existing shadPS4 and
central Steam-admission paths retain their current behavior and helper contract.
No publication, hosting or distribution is authorized. AppID 480 is permanent.

## Ownership and privacy boundary

The native host loads the MIT library through its versioned C API. Steam provides
identity, friends, platform invitations, global room discovery and peer messages.
The GPL host contains guest ABI translation only. No source moves from hosts or
the PolyForm Shield publisher into this library. Steamworks remains user supplied.

Each player runs one hidden publisher child. Pit Stop, coins, challenges, rewards,
achievements, statistics, leaderboards, news and publisher files are local. The
only peer traffic is explicitly allow-listed matchmaking room messages and race
datagrams. Unknown operations fail closed. Room metadata contains no DTLS keys,
credentials or private publisher state. Cross-player publisher reads return only
supported local data or honest empty/error results, never fabricated progression.

## Server modes

One executable selects `CTR_ADMISSION=shadnet|steam|guest|local` and
`CTR_MATCHMAKING=central|steam`. Existing defaults stay unchanged. Steam
matchmaking requires local admission and loopback listeners; invalid combinations
fail at startup. There is no provider fallback. A later rendezvous backend can
fit the boundary but is out of scope.

The parent pipe supplies local identity; admission requires the token SteamID to
match that owner. Local admission has its own persistent identity namespace and
needs no auth helper. PIDs remain SteamID64. Normal is the default economy;
unlock-all remains an instance choice with separate databases and saves.

## Library and native host

The native base is GetLostIn `online` at `b1b4bf9647132822147e76aeec47390c6cb157bf`.
Steam mode is opt-in, using the shared library; the private-server path stays
intact. Identity is read directly from the C API, never through the Windows
environment. The online ID is cleaned UTF-8, 1–16 bytes without control characters.
NpAuth uses osw1; supported WebAPI routes run in-process. The host drains copied
events on guest-visible callback paths, never from the library worker.

Bind control separates the game's UDP 3074 from local NAT discovery. Race UDP
uses per-peer loopback proxies backed by authenticated peer messages. Address
embedding must be measured at P3; FakeIP with a socket hook is a fallback design
only, not an assumed AppID 480 capability.

Room discovery uses invisible lobbies, independent of the friends-only platform
party. Filters include protocol (`osw/1`), title, build, realm, room protocol,
playlist/type and capacity. Search widens Default → Far → Worldwide and is
serialized and bounded. Only lobby members exchange room traffic. Validate the
authenticated sender against the claimed PID; bound sizes, queues and rates.

The room owner's publisher runs the existing matchmaker. A remote `Peer` sends
pushes through the library pipe/Steam channel. A member publisher forwards only
the agreed room tasks (145 registration/search/cancel/leave/sync/documents), and
rewrites advertised peer addresses to local proxy endpoints. Friend 138/14
advertisements require a narrowly scoped read-only mechanism supported by the
existing game evidence. No economy or statistics requests use that channel.

The library launches a configured companion with private stdin/stdout pipes,
hidden on Windows, using kill-on-close Job Object ownership. Portable shutdown
uses stdin EOF. No elapsed-time or menu-idle session cutoff is introduced.
Every individual operation remains bounded. All copied request/result ownership
rules and explicit failures remain binding; extend the C API version as needed.

The opt-in lab backend emulates multiple identities, directory, messages, friends,
invitations and transport on loopback. It is labelled simulated and cannot be
presented as Steam validation. One Steam account cannot qualify two live peers.

## Acceptance gates

| Gate | Required evidence |
| --- | --- |
| P0 | Decisions, source inventory, native build and tests, library static/shared suites, publisher suites |
| P1 | Original native game, signature patch only, local admission and Online menu |
| P2 | Single-account live native Steam identity/token/party/friends and Online menu |
| P3 | Two lab native clients with independent publishers discover, share room, race through results; central/P2P notice comparison; proxy/address decision |
| P4 | Steam directory/messages/SDR fixtures plus single-account live lobby read-back; written two-PC procedure |
| P5 | Guest event fixtures and lab invite/accept/party search; two-account Steam test explicitly pending |

Record F/C/U/H/P/S/I classes, source/build hashes and limitations at each gate.
The user's later direct authorization permits automated PC/game tests for this
task, superseding the task draft's human-input-only sentence. Steam may be
launched or restarted. Preserve owned assets and back up replaced executables.

Reassess if publisher reconstruction grows broadly, the native runtime dominates,
or owner/follower propagation cannot fit the room-owner engine. A remaining
two-PC live test does not prevent implementing and validating independent work.
