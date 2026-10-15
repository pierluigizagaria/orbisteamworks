# CTR as a platform-contract validation case

Date: 2026-10-15. Read with [generic feasibility](FEASIBILITY.md).
This document contains title-specific evidence and hypotheses. None of its
task numbers, layouts, addresses or matching rules belongs in the library.

## Reference stack and evidence limit

Use the retained PS4 CTR:NF CUSA13795 01.21 as a human-operated test client in
the shadPS4 fork with the orbiSteamworks Steam provider, and the current
independent Demonware Server in `nextendo-demonware/` as publisher. Reuse the
latter's service dispatch, sessions and matchmaker; admission uses its new
`steam` provider.

The accepted shadNet local race at publisher checkpoint `33dc1c6` and the older
private friend/invitation captures establish useful baseline behavior. They do
not qualify Steam, the current architecture refactor, WAN transport or an
invited party entering public search. The currently checked-out shadPS4 branch
is also not the accepted NPAuth branch or necessarily the installed executable.

Evidence classes: C = owned game/Toolkit observations; U-derived = host
implementation; I = proposed replacement or inference. Current publisher and
shadNet behavior is source evidence of our implementations, not Sony policy.

## Four distinct objects

| Object | Purpose | Owner |
| --- | --- | --- |
| Platform session | Identity/referral, discoverability, invitation and platform membership | PS4 facade backed by Steam |
| Game party | Players that the game considers grouped, including its peer state | Original CTR client logic |
| Publisher advertisement | Existing service 138 context session used to locate a friend/host | Existing publisher and game |
| Publisher matchmaking room | Existing service 145 searches, host document and expected members | Existing publisher and game |

A Steam lobby maps to the platform object. There is no requirement that its
Steam ID equal a Demonware session ID or room ID. The library preserves opaque
platform data; the original game performs any title-specific interpretation.

## Expected invitation-to-search flow

1. A enters the game's online flow, authenticates and creates the platform and
   publisher state requested by the original game. All identities refer to the
   same canonical platform account.
2. A selects B through the supported invitation path. The library resolves B's
   SteamID and sends a lobby invitation carrying the correct title, protocol
   generation and publisher realm.
3. B accepts while the host is running, from the host prompt or the Steam
   overlay. The host raises the PS4 session invitation event expected by the guest.
   Session metadata and opaque data must be retrievable at the required stage.
4. CTR consumes that event and executes its own session reads/join operation.
   The library fulfills those platform calls with real state and proper errors.
   Provisional Steam membership is rolled back if the game cannot finish.
5. CTR resolves the friend in the publisher namespace and retrieves the existing
   publisher advertisement. The publisher returns the actual owner's published
   address/session data. A and B must become a real game party through the
   game's own connection logic.
6. The human party leader starts public matchmaking. The original game must
   serialize the group's membership into its existing search request. The
   library neither invents that request nor calls the CTR matchmaker directly.
7. The publisher handles the group as one search with one seat per member,
   allocates a room, sends the host-create notification, accepts the host's
   document, and then notifies the joining search leaders.
8. The original game completes peer joins and races. Observe both clients and
   the publisher's non-sensitive state to establish that the invited follower
   participates through results.

Steps 3-8 describe the intended Steam-backed path (I), supported by individual
existing platform/game/publisher observations. They have not been demonstrated
end to end with orbiSteamworks. In particular, delivery to a search leader does
not by itself prove that the game's follower propagation succeeds.

## Concrete host and game evidence

In shadPS4, `np_handler_invitation.cpp` constructs a SystemService
`SessionInvitation` event with session ID, invitation ID, local user identity
and inviter referral identity. `sceSystemServiceReceiveEvent` consumes the
queue. The invitation structure is reconstructed with explicit offsets and
size assertions. This is a usable host boundary, not independent proof of all
firmware semantics. A Steam callback must be translated through that boundary.

The current invitation helper otherwise calls shadNet directly. The fork's
provider selection in `NpHandler` must cover both guest API calls and these
internal paths. Sending a Steam-backed session to the old shadNet invitation
HTTP path would mix incompatible session namespaces.

The historical friends investigation found the Toolkit expected identity
fields nested under `user`. It also observed selected-recipient invitation
dialog calls, so implementing only a generic friend picker would miss the
game's path. The separate shadNet friend-field fix must remain represented
in a test baseline using shadNet.

An older accepted platform invitation reached session GET 200 and membership
POST 204 but the game then failed to retrieve a publisher session. Missing
publisher operations were friend-name resolution (12/10) and friend-session
lookup (138/14). Later fixes and human private-flow acceptance show why a
successful platform invitation is necessary but insufficient.

These observations are recorded in the workspace's
`docs/OFFICIAL_SHADNET_FRIENDS_TEST.md`. Its dated deployments and removed lab
configuration are historical evidence, not current setup instructions.

## Current publisher already represents parties

In `internal/ctr/async.go`, `SearchDoc` contains members, lobby slot bounds and
matching filters. `ParseSearch` requires unique nonzero members, the authenticated
leader among them, valid capacity and registered peer data. Seat count is the
member count. The room document includes all expected members.

In `internal/ctr/dispatch.go`, the relevant existing flow is:

| Operation | Responsibility |
| --- | --- |
| 138/14 | Retrieve published friend sessions in the correct context |
| 145/2 | Register the client's peer/listen data |
| 145/3 | Return the current implementation's matchmaking token |
| 145/18 | Parse and reserve a search; activate after sending its reply |
| 2005 notification | Ask the selected host to create the room |
| 145/15 | Accept/version the host document |
| 2001 / 2006 notifications | Join/update the relevant searches |
| 2008 notification | Existing room merge flow |

Existing tests cover group seat accounting, duplicate claims, atomic reservations,
followers leaving/researching and keeping a party together during merge. These
tests were inspected in `internal/ctr`; they were not rerun in this analysis.
They qualify engine behaviors, not the Steam-to-game-party boundary.

This supports reusing the current matchmaker. No evidence found requires a
Steam-specific search algorithm or a Steam API dependency inside that engine.

## Boundaries that remain unproven

**Membership authority.** The source explicitly treats the member list as the
leader's claim. It prevents stealing already occupied seats but does not prove
each follower consented to join. The returned 145/3 value is currently the PID,
not a cryptographic membership attestation. Do not interpret a Steam lobby list
as automatic server-verified consent. This existing publisher limitation is
separate from Steam provider compatibility and does not justify changing the original
wire contract. Investigate the original token/party contract before proposing
any publisher hardening.

**Joining a particular active player.** The recovered original search serializer
has a `user_to_join` mode distinct from normal slot/filter search. The current
`SearchDoc` does not model it, and `ParseSearch` requires ordinary slot bounds.
Therefore joining an already matching/racing friend is a separate unsupported
or unqualified scenario. It must not be conflated with accepting an invitation
into a pre-search party and then searching together.

**Follower progression.** The publisher sends search notifications to the
associated connections, normally the party leaders. Actual propagation to
followers and peer connectivity must be demonstrated in the original game.
Correct member counts in server fixtures alone are insufficient.

**Platform-session payload.** Preserve binary contents and retrieval behavior.
Do not assume a platform blob is simply a Demonware room ID, or substitute the
public-search security pair for the context advertisement's distinct pair.

**Lifecycle.** Old captures showed leaving private play changed advertisements
and that rejected updates or stale reservations prevented reentry. A Steam
provider must exercise leave/reinvite/research on the current server. Old
acceptance does not cover every later lifecycle change.

## Minimal decisive live experiment

First demonstrate A inviting B into a private game party with the existing
publisher contract intact. Then use a third independent account C to search
the same public playlist while A searches for the A+B party.

Record only non-sensitive evidence: source/build IDs, stage/error codes,
pseudonymous member references, seat counts, search/room correlation, event
order, peer-state transitions and human-observed race/results. Do not record
credentials, complete request bodies, DTLS keys or account secrets.

Pass requires all of the following:

- A and B share the actual game party after Steam acceptance.
- The original game sends the expected grouped search; no synthetic library
  request or title-address patch is involved.
- The publisher counts A+B as two people in one search and can place C with them.
- The follower B completes the peer join and coherent race through results.
- Leaving and sending a second invitation works without restarting services.

Additional cases include wrong publisher realm, different build/title, full
party, stale invite, leader departure, cancellation during join, and repeated
public search. WAN and native/shadPS4 interoperability are subsequent distinct
acceptance cases. No game UI automation or lab replacement is part of the
current analysis.

## Workspace evidence index

- `docs/PS4_MULTIPLAYER_FLOW_MAP.md`: authentication, registration, search,
  host-document synchronization and peer progression; original serializer.
- `docs/OFFICIAL_SHADNET_FRIENDS_TEST.md`: invitation, identity-field and
  publisher friend-lookup captures and dated human observations.
- `docs/research/NP_AUTH_CONTRACT_RESEARCH.md`: owned firmware/game token
  contract, asynchronous ownership and the distinction from replacement claims.
- `docs/research/CTR_ANONYMOUS_UNLOCK_ALL.md`: separate historical evidence
  that bypassing platform-party setup can stop search progression. Its game
  patch is not a generic platform design.
- `nextendo-demonware/internal/platform/{ps4,shadnet}`: existing admission.
- `nextendo-demonware/internal/presence/friends.go`: name/ID lookup consistency.
- `nextendo-demonware/internal/lobby/matchmaking.go`: existing advertisements.
- `nextendo-demonware/internal/ctr/{async,dispatch,filling,merge}.go` and tests:
  current grouped search and room lifecycle implementation.

Paths above are relative to the containing private workspace. No game bytes,
private captures, proprietary SDK material or external implementation source
have been copied into this independent repository.
