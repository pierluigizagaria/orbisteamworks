# Feasibility: Steam as the PS4 platform provider, in-process

Date: 2026-10-15, revision 3.1. Revision 3 replaced two earlier same-day
designs: the shadNet-issuer binding and the separate local shadNet-compatible
server. Revision 3.1 replaces Web API ticket validation with an anonymous
game-server helper. Status update, 2026-10-15: the runtime loader and standalone
gate 1 tools are implemented; anonymous AppID 480 validation passed live.
See [the gate report](GATE1_AUTH_SPIKE.md) for the matrix and limits.
Initial C API, host and publisher integration now exist locally. A later host
startup completed real game authentication, encrypted publisher admission and
peer registration; human gameplay/multiplayer acceptance remains pending, as
does attribution of logged C++ exceptions. The user deferred distribution
decisions (including an external DLL adapter for official builds) and selected
the local fork as the immediate proof-of-concept host. Current validation is
tracked in INTEGRATION_WORKLOG.md. Evidence classes
(F/C/U/H/P/S/I) are defined in [sources](SOURCES.md).

## Decision

Steam replaces shadNet as the platform provider for this setup.

- **orbiSteamworks is a library.** It implements the PS4 platform services CTR
  uses on top of Steamworks, and hosts link it in-process.
- **shadPS4 fork.** A fork adds a Steam provider behind `NpHandler`, its
  existing NP facade, plus an in-process path for the WebAPI transport and for
  NpAuth.
- **Publisher.** The user's Demonware Server gains a `steam` admission provider.
  It validates tickets through a small server-side helper that logs on to Steam
  as an anonymous game server.
- **No Steamworks files are distributed.** The library loads the Steam API
  library at runtime, and users install it from the Steamworks SDK.

Assessment:

- **Feasible** for CTR in a shadPS4 fork for a private group. The condition is
  gate 1: anonymous game-server validation of an AppID 480 ticket.
- **Not a public product**, because AppID 480 is the only AppID available.
- **Compatible with the native port**, which reaches the same library through
  its C API.

## Rejected alternatives

| Design | Why it was rejected |
| --- | --- |
| Steam identity bound to shadNet accounts, sessions on lobbies | Costs of both providers: account binding plus a new session protocol, and incompatible invitation and session namespaces |
| Separate local server emulating the shadNet protocol | No fork, but an emulated protobuf protocol, a second process and a launcher. Its only real advantage was keeping Steamworks out of distributed GPL binaries, which the distribution model now handles |
| Steamworks linked or bundled into distributed builds | Distributes a GPL program combined with a proprietary library |
| Ticket validation through the Steam Web API (`AuthenticateUserTicket`) | Requires a personal Web API key with account-level access. The user rejected it on 2026-10-15 |

## The shadPS4 seam (U-derived)

Observed at local `0fdad68`, with upstream main at `0ae50a4` (2026-10-12):

- **`NpHandler` is the facade.** 22 source files call
  `NpHandler::GetInstance()`, 436 call sites in total, covering NpManager,
  NpTus, NpScore, Matching2, signaling, invitation dialogs and UI layers. None of
  them use shadNet types directly.
- **shadNet types appear in 11 files:**
  - the 7 files of `core/libraries/np/np_handler/`;
  - the shadNet client (`src/shadnet/client.*`);
  - Matching2 matchmaking (`np_matching2_mm.*`), which CTR does not need.
- **The WebAPI transport** in `np_web_api_internal.cpp` builds requests against
  `shadnet_webapi_server` with the `NpHandler` bearer. Its `sceHttp` calls are
  concentrated in the send, read, response-header and delete paths.
- **NpAuth:** upstream `sceNpAuthGetIdToken` returns the placeholder `"TOKEN"`.
  The user's draft [shadPS4#5350](https://github.com/shadps4-emu/shadPS4/pull/5350)
  fetches a real ID token from shadNet. It is useful for shadNet but not
  required here: the fork's Steam path obtains the token from the library.
- **Low upstream churn:** two upstream commits touched `np_handler`,
  `src/shadnet` or `np_web_api` in the 90 days before 2026-10-12.

What the fork changes:

- **`NpHandler`** selects a provider (`shadnet` or `steam`) and keeps its public
  interface. With `steam`:
  - identity, sign-in state, friends and invitations come from the library;
  - score, TUS, trophies and Matching2 report unavailable.
- **The WebAPI transport** gains an in-process path: request in; status,
  headers and body out.
- **NpAuth** asks the library for the ID token.
- **Settings and build:** a provider setting and a build option. The default
  build is unchanged.

## Distribution model

- **No Steamworks files in sources or builds.** The library declares the subset
  of the flat API it uses and loads `steam_api64.dll` or `libsteam_api.so` at
  runtime. It provides AppID 480 through `steam_appid.txt` or the `SteamAppId`
  environment variable before initialization.
- **Users install the SDK themselves.** They download it from Valve, accept its
  terms and place the library next to the executable.
- **Behavior without the library:** the Steam provider reports signed out and
  states why. It never fabricates success.
- **Server side.** The auth helper runs only on the Demonware host. Its operator
  installs the Steam game-server libraries, either from a Steam client
  installation or from Valve's redistributable through SteamCMD with anonymous
  login. The helper is never distributed to players.
- **Licensing risk.** Distributing only GPL code with source leaves the
  combination to the user's machine. Some interpretations, including the FSF's,
  still treat a GPL program designed to load a non-free library as one work.
  The risk is judged low for a community project but not zero. No GPL exception
  can be added, because shadPS4 has many authors. This is an engineering
  assessment, not legal advice.

## Identity

- **Account ID:** SteamID64. Use decimal strings in JSON and never floating
  point.
- **Online ID (user decision, 2026-10-15):** the user's Steam persona name,
  derived at each start unless the host configures one. It keeps any valid
  UTF-8: invalid bytes and control characters are dropped, surrounding spaces
  trimmed and the result cut to 16 bytes on a character boundary. The game may
  not render every character; that is accepted. An empty result falls back to
  `Player<digits>`.
  - CTR displays only the online ID, both for the local player and on the
    friend page, and resolves friends on the publisher by that string
    ([evidence](../../docs/research/CTR_FRIEND_LIST_NAMES.md)). It must
    therefore be unique on the publisher.
  - The library publishes it to Steam friends through rich presence.
  - Demonware binds it case-insensitively to the SteamID. The same SteamID may
    change it on a later admission (following a persona rename), which also
    updates its 12/10 name and leaderboard rows. A name bound to another
    SteamID is refused; that player then configures a different online ID.
- **Friends:** only Steam friends currently running orbiSteamworks for the same
  title, build and realm are listed, with `presence.onlineStatus` online.
  Friends who leave the game are hidden; their names stay cached for the
  context so pending invitations and name resolution keep working.
- **One local user per machine.** Steam allows one account per client.

## Authentication

Tickets are validated through Steam's native game-server path. No Web API key
is used.

1. The game calls `sceNpAuthGetIdToken`, and the fork's NpAuth asks the library.
2. The library requests a session ticket (`GetAuthSessionTicket`) and waits for
   `GetAuthSessionTicketResponse_t`. It returns
   `osw1.<SteamID64>.<hex ticket>.<base64url online ID>` (I). The charset fits
   the NpAuth contract, and the size must fit the 4097-byte `OrbisNpIdToken`
   buffer (checked in gate 1).
3. The game forwards the token inside the unchanged PS4 envelope.
   `ReadPSNEnvelope` already extracts it (up to 4096 characters).
4. The Demonware `steam` provider passes the SteamID and the ticket to the auth
   helper over a local channel.
5. The helper calls `BeginAuthSession` and waits, with a bound, for
   `ValidateAuthTicketResponse_t`. It returns the result and the owner SteamID.
   The helper is a long-running C++ process that has logged on to Steam
   anonymously as a game server for AppID 480.
6. Demonware checks the online-ID binding and admits:

   ```text
   PID      = SteamID64
   Username = online ID
   Sub      = "steam:<SteamID64>"
   Kind     = "steam"
   ```

7. When the Demonware connection ends, the provider asks the helper to call
   `EndAuthSession`.

Helper rules (I):

- **Not advertised.** It never appears in Spacewar's server browser: server
  advertising is off and it opens no public query port.
- **Sessions.** One auth session per admitted connection. Later
  `ValidateAuthTicketResponse_t` events (ticket cancelled, Steam logoff) are
  logged and never used to disconnect players.
- **Separate process.** Demonware stays in Go without cgo. The helper reuses the
  library's runtime Steam loader.

**Freshness and replay (I, integration requirement).** The intended 120-second
freshness rule is not established by the Steam token contract: an unused ticket
still passes after five minutes. The envelope has no authenticated issue time.
`BeginAuthSession` reports a duplicate when the SteamID already has an active
auth session. See [measured behavior](GATE1_AUTH_SPIKE.md), covering:
- behavior after `EndAuthSession`;
- behavior after the client's `CancelAuthTicket`;
- the effect of ticket age;
- whether binding the ticket to the server identity works.

The tested replays after session end were rejected asynchronously. The client
still needs a new ticket per admission and cancellation of the previous one.
A helper cache must hash canonical ticket bytes; its retention and any stronger
freshness guarantee need an explicit contract before publisher integration.
Do not assume that a 120-second hash-cache window proves ticket freshness.

**No other route.** If anonymous game-server validation does not work for AppID
480, no alternative is selected and the project stops.

**Database.** The `steam` provider has its own trust ID, so Demonware binds it
to a separate database. Existing shadNet-bound data is not migrated.

**Current admission.** The proposed lab-only `steam-unverified` interim was
not implemented. The helper is integrated and `steam` admission requires native
ticket validation. The existing `guest` mode assigns a random identity per login
and cannot substitute for Steam friends or invitations.

## Sessions and invitations on Steam lobbies

| PS4 platform behavior | Steam mapping |
| --- | --- |
| Session create | `CreateLobby`, friends-only type |
| Session data, changeable data | Lobby data, written by the owner. Non-owner writes go to the owner as a lobby chat message (at most 4 KB) or are refused, according to the PS4 rules found in gate 0 |
| Members | Lobby membership. Provisional until the game's own membership request |
| Leader | Lobby owner. Steam transfers ownership automatically when the owner leaves; the PS4 meaning (owner-bound or migrating) is decided in gate 0 |
| Send invitation | `InviteUserToLobby` per recipient, plus an invitation record in lobby data |
| Receive invitation | `LobbyInvite_t`, then `RequestLobbyData`, then a pending invitation shown by the host prompt |
| Accept invitation | Host prompt or Steam overlay (`GameLobbyJoinRequested_t`), then `JoinLobby`, then the host raises the SystemService `SessionInvitation` event and the game reads the session and posts membership |
| Join from friend list | `GetFriendGamePlayed`, which returns the friend's lobby ID |

Rules (I):

- **Payload revision from live evidence.** The original many-key metadata
  representation failed: Steam refused a second 6,000-byte chunk and a third
  3,000-byte chunk. Metadata now holds a bounded document and references to
  large immutable payloads. Those payloads transfer through owner-authenticated
  lobby messages before a remote acceptance completes. The 52,096-byte CTR
  image remains intact. Single-account creation/readback and a 4,096-byte
  native message round trip passed; remote transfer is fixture-tested only.

- **Namespace.** Lobby data carries the protocol (`osw/1`), title, build and
  publisher realm. They are validated on read and on join. Unrelated AppID 480
  lobbies are ignored.
- **Provisional membership.** It is never reported as a committed game-party
  member. Failure, decline or timeout leaves the lobby.
- **Duplicate or stale invitations.** Prompt and overlay acceptance of the same
  invitation are idempotent. Invitations to a lobby that no longer exists are
  rejected.
- **Cold launch.** Invitations accepted while the game is closed start
  Spacewar, so they are unsupported.
- **Visibility.** Lobby data is visible to members and to friends who can see
  the lobby, which is comparable to PSN session visibility. It contains no
  secrets.

## Peer transport

Peer transport is unchanged. WAN and NAT reachability remain open and do not
depend on Steam. Steam networking (FakeIP, Datagram Relay) could carry game
traffic later through the host's socket layer. It is out of scope.

## Native port

Observed in GetLostIn (H): `main` at `46c810b`, `online` at `b1b4bf9`.

- GPL-2.0-or-later. The game's x86-64 code runs natively, imports are resolved
  by NID, and the runtime is written in C.
- It vendors shadPS4's video core and links the game's own `libSceNpToolkit2`
  as native code.
- On `main`, online features are stubs.
- On `online`, `runtime_net.c` (about 1000 lines) already contains:
  - real sockets, epoll and an HTTP/1.1 client;
  - about 25 `sceNpWebApi` functions that forward requests over HTTP;
  - fabricated NpAuth tokens and identity taken from environment variables;
  - no push event delivery.

  This summary comes from an automated reading of the file and must be checked
  line by line before relying on it.

Plan (I). Bring `runtime_net.c` onto a branch from `main`, then:
1. serve WebAPI from the library in-process;
2. take identity and the ID token from the library;
3. deliver library events as SystemService and NP callbacks on the
   guest-visible path.

Because WebAPI no longer travels over the network, the runtime's single-server
resolver can stay for Demonware traffic. The native port stays paused until the
shadPS4 path passes gate 5.

## Acceptance gates

| Gate | Pass | Stop or reassess if |
| --- | --- | --- |
| 0: inventory | From existing shadNet captures and source: the `NpHandler` methods and WebAPI routes CTR exercises, payload sizes, who writes which session part, and CTR's NpAuth client ID and scope | CTR's online flow depends on score, TUS or Matching2 |
| 1: authentication | A client that loads the Steam API at runtime (no SDK files in the repository) obtains a 480 session ticket. An anonymous game-server helper validates it with `BeginAuthSession` and establishes the SteamID. Duplicate, end-session, cancel and age behavior are known | Anonymous game-server validation fails for AppID 480 |
| 2: login and public race | The shadPS4 fork with `provider=steam` signs in. Demonware admits it with verified `steam` admission. Two Steam accounts on two machines complete a public race | Platform session creation cannot satisfy CTR. Historical evidence shows that skipping platform-party setup can block search |
| 3: friends | CTR's friend page shows Steam friends with correct presence, and the publisher resolves their names (12/10) | Name binding conflicts cannot be resolved without an account service |
| 4: invitation to party | A invites B; B accepts from the prompt or the overlay; both share the game party. Leave and reinvite work without restarts | Lobby semantics cannot represent CTR's session rules |
| 5: grouped search | The pass criteria in the [CTR case](CTR_VALIDATION_CASE.md#minimal-decisive-live-experiment) | Only independent solo searches work |
| 6: native host | The same library through the C API in the native port. Native network prerequisites are recorded separately | Native runtime work dominates |

Gates 2–5 use verified admission. A successful authentication exchange alone
does not pass their platform-party or gameplay requirements.

Negative cases from gate 2 onward:
- Steam not running or its library missing;
- auth helper down;
- wrong realm or build;
- a foreign AppID 480 lobby;
- full party;
- stale invitation;
- owner departure;
- cancellation during join;
- shutdown with requests pending.

Each live gate records build identities and human observations.

## Risks, most severe first

1. **Anonymous game-server validation** fails or is unusable with AppID 480.
   No other route is selected, so the project stops.
2. **AppID 480 tolerance** changes. Public distribution is not possible.
3. **Licensing interpretation** of a GPL host that loads a user-supplied
   proprietary library.
4. **Auth helper operations:** Steam game-server libraries and an outbound Steam
   connection on the Demonware host. If the helper is down, admission is down.
5. **Fork maintenance:** rebasing the NP seam onto upstream shadPS4. Churn is
   currently low.
6. **Lobby semantics differ** from PS4 sessions (owner writes, ownership
   transfer).
7. **WAN and NAT reachability** of peers, independent of this project.
8. **Lab cost.** One Steam account per client means two machines or virtual
   machines for gates 2–4 and three for gate 5.

## Remaining dependencies

Progress is tracked by the acceptance gates, without delivery estimates.
Current implementation and single-account evidence are in INTEGRATION_WORKLOG.md.
Paired gameplay requires an independent Steam account/host and human-operated
sessions. The native port stays deferred until the shadPS4 path passes gate 5.

## References

- [Steam authentication](https://partner.steamgames.com/doc/features/auth)
- [ISteamUser](https://partner.steamgames.com/doc/api/ISteamUser)
- [ISteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer)
- [ISteamMatchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking)
- [GetLostIn native port at 46c810b](https://github.com/GetLostIn/CTR-NitroFueled-PC/tree/46c810b2a30451873034e5b17c8a61ddd780618f)
