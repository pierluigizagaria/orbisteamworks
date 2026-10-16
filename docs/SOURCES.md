# Evidence and source references

Native P2P update (2026-10-16): `online` at
`b1b4bf9647132822147e76aeec47390c6cb157bf` is now the selected native base;
the historical main-baseline/reference-only labels below are superseded by
[P2P_DESIGN.md](P2P_DESIGN.md). The user's SDK 1.65 headers were consulted for
`ISteamMatchmaking`, `ISteamNetworkingMessages`, `ISteamNetworkingUtils` and
their flat entry points (P). In particular, actual lobby owner and member
enumeration require membership; public discovery uses metadata first and
verifies the actual owner after join. Broken Networking Messages sessions
require explicit reset before a later send. These restrictions are covered by
the new fixture tests. The local `steam_api64.dll` SHA-256 is
`e6d9bafb9a41e42fba7b21553db49f8719027af3f0deb23ff86cfc44d60e776d`.
The SDK remains private under ignored `out/`; no provider files are committed.

Updated 2026-10-15. These references support feasibility and the initial
architecture. The [gate 1 report](GATE1_AUTH_SPIKE.md) records the implemented
runtime loader and live authentication spike; it does not qualify the full
library or any host integration.

## Evidence classes

| Class | Meaning |
| --- | --- |
| F | Direct evidence from owned firmware bytes |
| C | Direct evidence from original game or Toolkit callers |
| U | Upstream emulator reconstruction |
| H | Public homebrew/reverse-engineering source |
| P | Provider's official documentation |
| S | Generic public standard; not proof of a PS4-specific contract |
| I | Inference, design proposal or explicitly chosen replacement behavior |

No F/C captures are included in this repository. Host source code is evidence
of that host's implementation, not proof of Sony behavior. The local shadPS4
snapshot below includes local changes and must not be described as entirely
upstream U evidence.

## Examined host baselines

| Host | Pin | Relevant observation |
| --- | --- | --- |
| [GetLostIn native main](https://github.com/GetLostIn/CTR-NitroFueled-PC/tree/46c810b2a30451873034e5b17c8a61ddd780618f) (H) | 46c810b2a30451873034e5b17c8a61ddd780618f | Runtime import resolution offers a central host integration boundary; this is the chosen native baseline |
| [GetLostIn online](https://github.com/GetLostIn/CTR-NitroFueled-PC/tree/b1b4bf9647132822147e76aeec47390c6cb157bf) (H) | b1b4bf9647132822147e76aeec47390c6cb157bf | Adds a network resolver entry and a separate runtime_net implementation, plus other runtime changes; reference only |
| Local shadPS4 checkout (U-derived, with local changes) | 0fdad68cd9bfe3492cdb3f223313ed10040494f4, feat/resolver-host-overrides | `NpHandler` is the NP facade and the provider seam; WebAPI transport and NpAuth are the other integration points |

In the local shadPS4 snapshot, the examined files are:

- src/core/libraries/libs.cpp and libs.h: InitHLELibs, LinkSymbolImpl, LIB_FUNCTION.
- src/core/loader/symbols_resolver.cpp and .h: registration and lookup;
  matching uses the symbol, library and module.
- src/core/linker.cpp: HLE symbol lookup precedes loaded-module export lookup.
- src/core/tls.h: host-call wrapper boundary.
- src/core/libraries/np/np_manager.cpp: sceNpCheckCallback and NP event dispatch.
- src/core/libraries/np/np_web_api/np_web_api.cpp: registration of push-event draining.

These examined files had no uncommitted differences from the recorded local
HEAD when checked. This statement does not qualify installed lab binaries or
submodule state. No host source has been copied into this project.

## Steamworks documentation (P)

- [API overview](https://partner.steamgames.com/doc/sdk/api): initialization,
  App ID/client requirements, shutdown and event processing. Steam callbacks
  execute on the thread that calls SteamAPI_RunCallbacks.
- [ISteamFriends](https://partner.steamgames.com/doc/api/ISteamFriends): friend
  enumeration, persona data, presence and invitation-related operations.
- [Authentication and ownership](https://partner.steamgames.com/doc/features/auth):
  ticket-based identity verification; client acquisition and server validation
  are distinct steps.

The initial documentation was accessed on 2026-10-14. Gate 1 uses the user's
local SDK **1.65**, dated 23 July 2026, with its ZIP and runtime DLL pinned in
the gate report. No SDK file is bundled or included in the source build.

## Reference stack examined on 2026-10-15

- Local shadPS4 HEAD remains `0fdad68cd9bfe3492cdb3f223313ed10040494f4`.
  Also examined invitation handler, invitation ABI, SystemService queue and
  WebAPI files. Its checked-out NPAuth implementation is not the accepted
  authentication branch; do not use that stub as the implemented contract.
- The separate NPAuth branch was inspected with `git show` at
  `162983963d76c19a6a7e931f977a7738dda14ab1`, without changing the checkout.
- Local shadNet HEAD: `4f5d7fb835a23120a7a21db423ac9e85ef6d424c`.
  Examined `src/npauth.cpp`, `src/webapi/webapi_routes_npauth.cpp` and
  `src/webapi/webapi_routes_session.cpp`. Friend-field fix `7431494` is a
  separate required baseline change, not part of this HEAD.
- Demonware Server production sources were examined at
  `769869ce0d3a94949b64bff0b65ad99d7beec3c8`, with a follow-up read at
  `b1d092c77d6c7ffafcc06fed0deea55a7574d57b`. Package/test organization was
  changing concurrently and had uncommitted edits. A final check also found
  constant namespace relocations in `internal/ctr/dispatch.go`; the findings
  refer to the recorded snapshots, not a continuously moving HEAD.
  No changes were made during that source-feasibility inspection. The later
  user-authorized PoC adds Steam admission on the retained `c09908a` base;
  its source manifest and tests are recorded in INTEGRATION_WORKLOG.md.
  See the [CTR evidence index](CTR_VALIDATION_CASE.md#workspace-evidence-index)
  for inspected files. Source observations do not qualify installed binaries.

The subsequent session implementation also uses Valve's
[ISteamMatchmaking reference](https://partner.steamgames.com/doc/api/ISteamMatchmaking)
(P, checked 2026-10-15) for lobby data, visibility and chat operations. The
locally supplied SDK 1.65 audit checks the actually used signatures and callback
layouts. Live checks rejected a second 6,000-byte metadata chunk and a third
3,000-byte chunk; the PoC therefore keeps one small metadata document and uses
bounded lobby messages for large immutable payloads. This is an I replacement
protocol supported by local tests, not a recovered PS4 wire contract or a claim
about remote multiplayer acceptance.

The private workspace contains earlier F/C evidence and dated human acceptance
records, referenced by path in the CTR case. They were read, not regenerated
or copied into this project. Earlier lab workarounds are not implementation
requirements. Generic replacement choices in the feasibility report are I.

Additional official sources checked on 2026-10-15:

- [Spacewar example](https://partner.steamgames.com/doc/sdk/api/example).
- [ISteamMatchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking).
- [ISteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer).
- [Matchmaking overview](https://partner.steamgames.com/doc/features/multiplayer/matchmaking).
- [Networking Messages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages).
- [Networking Sockets](https://partner.steamgames.com/doc/api/ISteamNetworkingSockets).
- [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay).

## Revision 3 evidence (2026-10-15)

Measured in the local shadPS4 checkout `0fdad68` (U-derived):

- 22 source files call `NpHandler::GetInstance()`, 436 call sites in total.
- shadNet types (`ShadNet::`, `ShadNetClient`) appear in 11 files: the 7 files
  of `core/libraries/np/np_handler/`, `src/shadnet/client.*` and
  `np_matching2_mm.*`.
- `np_web_api_internal.cpp` uses `shadnet_webapi_server` as the WebAPI base URL
  and `NpHandler::GetBearerToken` for authorization. Its `sceHttp` calls sit in
  the send, read, response-header and delete paths.
- `src/core/emulator_settings.h` defines `shadnet_server` and
  `shadnet_webapi_server`.
- `OrbisNpIdToken` holds a 4097-byte token buffer (`np_types.h`).

Other pins and observations:

- Upstream `main` at `0ae50a4` (2026-10-12) still returns `"TOKEN"` from
  `sceNpAuthGetIdToken`. Two upstream commits touched `np_handler`,
  `src/shadnet` or `np_web_api` in the preceding 90 days.
- The user's draft [shadPS4#5350](https://github.com/shadps4-emu/shadPS4/pull/5350),
  branch head inspected at `1629839`, exchanges the shadNet bearer at
  `POST /2.0/oauth/token` and accepts `[A-Za-z0-9-_.]` tokens shorter than 4097
  characters.
- Demonware: `ReadPSNEnvelope` accepts ID tokens up to 4096 characters. The
  `guest` provider and `CTR_ADMISSION=shadnet|guest` show the existing
  multi-provider admission.
- GetLostIn `online` at `b1b4bf9` (H): `src/runtime_net.c`, read through an
  automated summary only. It describes real sockets and HTTP, about 25
  forwarding `sceNpWebApi` functions, fabricated NpAuth tokens and no push
  delivery.
- Valve's [ISteamUserAuth](https://partner.steamgames.com/doc/webapi/ISteamUserAuth)
  reference (P): `AuthenticateUserTicket` requires a publisher key on the
  partner host, or a user key with rate limits on `api.steampowered.com`. This
  route is **not used**: on 2026-10-15 the user rejected Web API keys in favor
  of anonymous game-server validation.
- Valve's [ISteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer)
  and [ISteamUser](https://partner.steamgames.com/doc/api/ISteamUser)
  references (P): anonymous logon, `BeginAuthSession`, `EndAuthSession`,
  `ValidateAuthTicketResponse_t`, `GetAuthSessionTicket` and
  `CancelAuthTicket`. The SDK's Spacewar example includes a game server.
  Subsequent local AppID 480 observations are recorded in the gate 1 report.

## Implementation evidence and open contracts

The external references above do not establish our C API or publisher protocol.
Those are now explicit replacement contracts (I), implemented locally: see
[C_API.md](C_API.md), [ARCHITECTURE.md](ARCHITECTURE.md) and the publisher's
`docs/STEAM_ADMISSION.md`. Native validation, persisted online-ID binding,
replay rejection and the original game's encrypted publisher handshake have
the separate acceptance evidence recorded in [INTEGRATION_WORKLOG.md](INTEGRATION_WORKLOG.md).

The [gate 0 inventory](GATE0_CONTRACT_INVENTORY.md) pins the original CTR client
ID/scope and historical session-create type/sizes. Full route coverage, exact
PS4 friend/callback behavior beyond the inspected paths, in-game platform
sessions, two-account invitations and grouped search remain unqualified.
Record resolved contracts with their evidence class, source pin and validation;
successful fixtures or a single-account lobby do not qualify those live gates.
