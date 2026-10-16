# Architecture

Native P2P addendum, 2026-10-16: [P2P_DESIGN.md](P2P_DESIGN.md) supersedes the
native deferral and unchanged-matchmaking statements in the historical revision
below. ABI 2 keeps the ABI 1 layout and exports, adding explicit Steam/lab backend
selection, loopback game endpoint and hidden companion configuration. The
companion uses [COMPANION_PROTOCOL.md](COMPANION_PROTOCOL.md); it owns publisher
rules and local data, while the library owns identity, directory and transport.

The optional endpoint API adds `src/core/endpoints.*`: a bounded registry of
copied canonical-to-proxy and proxy-to-canonical routes, shared by the worker and
host readers through a short mutex. It has no guest/host types or title packet
semantics. The independent C API and companion contract are sufficient for any
host/publisher integration; no sibling build or runtime layout is required.

`src/core/p2p.*` implements the bounded directory/peer protocol and per-peer UDP
proxies; `src/core/local_io.*` owns child pipes, Windows Job Object and loopback
sockets. `src/lab/` is an explicitly simulated multi-process file-backed hub with
local UDP, not a substitute for Steam validation. `src/steam/` alone declares and
calls the SDK subset for invisible lobbies and Networking Messages. No publisher
or native-host implementation is copied into the library. See
[P2P_VALIDATION.md](P2P_VALIDATION.md) for evidence and remaining acceptance.

Updated: 2026-10-15, revision 3 with PoC implementation notes. The runtime Steam
loader, C API, initial session/friend core, shadPS4 adapter and publisher helper
integration are implemented locally. See INTEGRATION_WORKLOG.md for measured
acceptance and remaining limitations. No game or remote multiplayer acceptance
is implied by implementation or compilation.
The [feasibility analysis](FEASIBILITY.md) records the evidence,
decision and gates. The [CTR case](CTR_VALIDATION_CASE.md) stays title-specific.

## Accepted direction

- orbiSteamworks is an in-process library that implements PS4 platform
  services on Steamworks.
- Both hosts use one versioned C API:
  - the shadPS4 fork first, through a Steam provider behind `NpHandler`;
  - the GetLostIn native port later, through its `runtime_net.c`.
- The Steam API library is loaded at runtime and supplied by the user.
- The publisher gains a `steam` admission provider. It validates tickets
  through an auth helper that logs on to Steam as an anonymous game server.
- AppID 480 is the only AppID and a permanent constraint.

## Components

| Component | Owns | Boundary |
| --- | --- | --- |
| orbiSteamworks library | Platform semantics: identity, friends, sessions on lobbies, invitation state machine, WebAPI route handling, ID tokens | C API only; no host or guest types; no Steamworks SDK files |
| Steam loader (inside the library) | Loading the Steam API library, its own declarations of the flat-API subset, manual callback dispatch | The only code that touches Steam symbols |
| shadPS4 fork | `NpHandler` provider selection, in-process WebAPI transport, NpAuth, guest event delivery, invitation prompt | Changes confined to the NP seam, settings and build option |
| Native port (later) | `runtime_net.c` calling the C API, guest event delivery | Same C API; native network prerequisites are separate |
| Auth helper (server side) | Anonymous game-server logon, `BeginAuthSession`/`EndAuthSession`, validation results | C++ process on the Demonware host only; reuses the runtime loader; never advertised |
| Demonware Server | Admission through `steam`, publisher services, matchmaking | Separate repository; asks the auth helper over a local channel |
| Steam client and `steam_api64.dll` | Steam session on the machine | Installed by the user; one account per client |

## Library layout

```text
include/orbisteamworks/osw.h   versioned C API
src/core/                      identity, session model, invitation state machine,
                               WebAPI route handlers, event queue
src/steam/                     runtime loader, flat-API declarations, callback pump
src/fixture/                   offline backend simulating several Steam users
server/auth-helper/            anonymous game-server ticket validator (after gate 1)
tests/                         contract tests against the fixture backend
tools/auth-spike/              gate 1 client and anonymous game-server validator
```

- **Core** uses no Steam or host types. The Steam backend and the fixture
  backend implement the same internal interface, so tests run without Steam.
- **Implementation language:** C++. The public boundary is C, for the C runtime
  of the native port and so that both hosts share one boundary.
- **No STL types, exceptions or Steam handles cross the C API.**

## C API shape (I)

The current unreleased layout is documented in C_API.md. The API is message-like: asynchronous
requests plus an event queue. All data is copied across the boundary.

- **Lifecycle:** create with a config (AppID, realm, title, build, local online
  ID), then start, stop and destroy.
- **Identity:** sign-in state, SteamID64, online ID.
- **ID token:** request with client ID and scope; the result arrives
  asynchronously.
- **WebAPI:** request with method, path, headers and body; the response (status,
  headers, body) arrives asynchronously.
- **Friends:** a snapshot query.
- **Invitations:** send, accept and decline.
- **Events, polled by the host:** sign-in changed, friend changed, invitation
  received, invitation accepted from the overlay, session changed, request
  completed.

Ownership rules:
- Request buffers are copied on entry.
- Completed payloads stay owned by the library until the host releases them.
- Cancellation and teardown complete pending requests with explicit errors.

## Threading

- **One library thread pumps Steam callbacks** through manual dispatch and turns
  results into core events.
- **The library never calls host or guest code.** The host drains events on its
  own thread and delivers guest callbacks only through its existing
  guest-visible path: `NpHandler` worker, `sceNpCheckCallback` and the
  SystemService event queue in shadPS4.
- **The library never holds a lock while delivering guest callbacks.** Event
  polling copies data to the host; guest delivery happens after the API returns.
- **Every wait on Steam is bounded.** On timeout, requests complete with
  PS4-shaped errors.

## shadPS4 fork integration

- **`NpHandler`** keeps its public interface. A provider setting
  (`shadnet | steam`) selects the backend. With `steam`:
  - sign-in, NP ID, account ID, friends and invitations map to the C API;
  - `IsMatching2Enabled` and `IsTrophiesEnabled` report false;
  - score and TUS calls fail with explicit unavailable errors.
- **WebAPI transport** in `np_web_api_internal.cpp`: when the provider handles
  WebAPI in-process, send, status, header and read operations use the library
  response instead of `sceHttp`.
- **NpAuth** `GetIdToken*` asks the library when the provider is `steam`.
- **Invitations:**
  - received invitations feed the existing pending list and prompt;
  - prompt acceptance and overlay acceptance converge on one path, which raises
    the SystemService `SessionInvitation` event once.
- **Build:** a fork-only build option compiles the library from source. The
  default build and the shadNet path are unchanged.

Fork changes follow shadPS4's own repository instructions and stay minimal and
contained, so rebasing onto upstream remains cheap.

## Invitation state machine

Large immutable session payloads are fetched from the current lobby owner
after the provisional Steam join, before acceptance completes. `core/blobs`
owns bounded copied payloads and message framing; `src/steam` owns native lobby
chat calls and callback decoding. Metadata stays under the observed aggregate
limit. The transport is for platform data only and does not replace game UDP.

On the receiving side:

```text
Received (LobbyInvite_t, lobby data fetched and namespace validated)
  -> Pending (offered to the host prompt)
  -> Accepted (prompt, or GameLobbyJoinRequested_t from the overlay)
  -> Joining (JoinLobby pending)
  -> Joined (the game posted its own membership)
  |  RolledBack (decline, failure or timeout; lobby left)
```

- Duplicate invitations and double acceptance are idempotent.
- An invitation whose lobby no longer exists is rejected at acceptance.
- Steam membership is reported as committed only in `Joined`.

## Lifecycle

1. **Start.** The host creates the library at NP initialization. The library
   loads the Steam API library, sets AppID 480 and calls the flat-API init.
2. **Failure.** If the library is missing or Steam is not running, sign-in fails
   with a reason the host logs and shows. Nothing reports success.
3. **Shutdown.** On host shutdown or user sign-out, the library leaves lobbies,
   cancels tickets, completes pending requests with errors and shuts Steam down.

## Demonware changes

These changes belong in the separate `nextendo-demonware` repository:

- **A new `internal/platform/steam` provider.**
  - It reuses `ReadPSNEnvelope` and parses the
    `osw1.<SteamID64>.<ticket>.<online ID>` token.
  - It sends the SteamID and the ticket to its child auth helper over inherited
    stdin/stdout pipes: one validation request per admission, plus an end-session
    message when the admitted connection closes. The helper opens no local HTTP
    service or public query listener.
  - It admits `PID = SteamID64` and `Sub = "steam:<SteamID64>"`.
  - If the helper is unreachable or times out, admission fails with an explicit
    error.
- **Configuration.**
  - `CTR_ADMISSION=steam`, with the helper executable and user-supplied Steam
    library configured as documented by the publisher's STEAM_ADMISSION.md.
  - No Web API key.
  - The trust ID names AppID 480 and the realm, so the instance uses its own
    database.
  - The proposed interim `steam-unverified` mode was not implemented. Current
    Steam admission always waits for native ticket validation.
- **Online-ID binding.** The binding between online ID and SteamID is persisted
  and enforced in the instance's SQLite database. Canonical consumed-ticket
  hashes are also persisted for replay rejection. These contracts passed the
  native-helper/provider/SQLite and verified HTTPS checks in the work log.
- **No change** to matchmaking, dispatch or room logic.

## Native port integration (later)

Prerequisites on a branch from GetLostIn `main`:
- `runtime_net.c` brought over from `online`;
- sockets and HTTP qualified against Demonware.

Then:
- `sceNpWebApi` calls go to the C API in-process;
- NpManager identity and NpAuth tokens come from the library;
- library events become NP and SystemService callbacks on the guest-visible
  path;
- the existing ImGui menu shows the invitation prompt.

The library does not change for this host.

## Acceptance discipline

The optional provider overlay uses `OSW_OVERLAY` requests and copied
`OSW_OVERLAY_CHANGED` events. Core code validates the generic party namespace
and committed local membership; only the Steam adapter resolves native overlay
entry points and handles their callbacks on the worker. Lab providers report
unsupported. Panel dispatch and invitation acceptance are separate results;
the exact request contract is in [C_API.md](C_API.md).

- Offline fixture results do not count as live Steam or in-game results.
- Each live gate records the library, shadPS4 fork, Demonware and Steam API
  library versions, together with human observations.
- Unsupported operations return explicit errors, never synthetic success.
