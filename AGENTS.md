# Project instructions

## Superseding P2P task (2026-10-16)

`docs/P2P_DESIGN.md` and the workspace `docs/drafts/codex-p2p-native.md` now
govern the native P2P work. They supersede native-host deferral, unchanged
publisher matchmaking and mandatory helper validation for local admission only.
Central Steam admission retains its existing helper. The user authorized local
implementation, tests, commits and parallel agents, with no publication. Keep
the library title-agnostic and Steam symbols confined to `src/steam/`.

Write project documentation in English and respond in the conversation language.
Use the display name `orbiSteamworks` and repository directory `orbisteamworks`.

Read README.md, docs/FEASIBILITY.md, docs/ARCHITECTURE.md and docs/SOURCES.md
before implementation. FEASIBILITY.md revision 3.1 (2026-10-15) is the current
decision. It replaces the shadNet-issuer design, the separate local server
design and Web API ticket validation.

## Design

- orbiSteamworks is an in-process library with one versioned C API, shared by
  every host.
  - Core logic carries no Steam, host or guest types.
  - Only `src/steam/` touches Steam symbols.
- Hosts:
  - the shadPS4 fork selects a Steam provider behind `NpHandler`, with
    in-process WebAPI and NpAuth paths;
  - the GetLostIn native port later calls the same C API from `runtime_net.c`.
  - Do not duplicate platform logic in a host.
- Keep shadPS4 fork changes inside the NP seam: `NpHandler`, WebAPI transport,
  NpAuth, settings and build option. Follow shadPS4's own AGENTS.md there.
  Keep the default build and the shadNet path unchanged.
- Steam replaces shadNet in this setup. Do not mix shadNet accounts, sessions
  or invitations with Steam ones.
- The publisher change is a `steam` admission provider in the Demonware Server.
  Keep matchmaking, dispatch and room logic unchanged. The project changes that
  repository only when the user asks.
- Ticket validation uses the auth helper, which logs on to Steam as an
  anonymous game server and calls `BeginAuthSession`.
  - Do not introduce Steam Web API keys: the user rejected that route.
  - The helper never advertises itself in Spacewar's server browser.
  - The helper never disconnects players on later Steam events.

## Steamworks and licensing

- Never commit or bundle Steamworks SDK files: headers, libraries, DLLs or
  redistributables.
  - Load the Steam API library at runtime.
  - Declare only the flat-API functions and structures actually used, in
    `src/steam/`.
  - A locally downloaded SDK in `out/` may be read to write and check those
    declarations. Record the SDK version used.
- The user selected MIT for the library on 2026-10-15. Host glue retains the
  host's license; never relicense host or publisher code as MIT.
- Never copy source from shadNet, shadPS4 or GetLostIn into this repository.
  Host glue lives in the host forks under their license.
- Missing Steam library or Steam client: report signed out with a reason. Never
  fabricate identity, tokens or success.

## AppID 480

AppID 480 is a permanent constraint. Do not describe it as a placeholder or
plan around another AppID. Its selection proves nothing about:
- server-side ticket validation;
- multi-account operation;
- cold-launch routing.

Every lobby carries and validates the project namespace, title, build and
realm. Never act on unrelated AppID 480 lobbies.

## Behavior rules

- **Identity.** SteamID64 is the account ID. The online ID is the user's
  Steam persona name, cleaned to 1–16 bytes of UTF-8 without control
  characters (user decision 2026-10-15), unless the host configures one. The
  publisher binds it to the SteamID and follows renames. CTR shows only online
  IDs. Preserve 64-bit IDs losslessly.
- **Friends.** List only friends currently in the game for the same title,
  build and realm, with `presence.onlineStatus`.
- **No fabricated success.** Unsupported operations return explicit errors.
- **Membership.** Never report Steam lobby membership as committed game-party
  membership before the game's own membership request.
- **Threading.** One library thread pumps Steam callbacks. The library never
  calls host or guest code. Hosts deliver guest callbacks on their existing
  guest-visible path. No lock is held while delivering events. Every wait on
  Steam is bounded.
- **Async ownership.** Copy request buffers on entry. Completed payloads stay
  owned until the host releases them. Cancellation and teardown complete
  pending requests with errors.
- **No arbitrary disconnects.** Do not introduce menu-idle or elapsed-time
  session cutoffs. Individual requests stay bounded.

## Evidence and testing

- Classify evidence as F/C/U/H/P/S/I as defined in docs/SOURCES.md. Unknown PS4
  behavior stays unknown, and replacement choices are marked I.
- Use owned assets and lawful public references. Do not use leaked SDK
  documents or contact production Sony or Demonware services. Do not execute
  untrusted downloaded binaries.
- Qualify each gate in docs/FEASIBILITY.md separately. Offline fixture results
  are not live Steam, game or WAN results.
- Tests exercise observable contracts, ownership and failure behavior, not
  implementation details.

## Repository hygiene

- Keep builds in `build/`, the downloaded SDK and other local tools in `out/`,
  and private runtime state (logs, tickets) in `runtime/`.
- Never commit credentials, tickets, keys, account data, Steamworks SDK files,
  game or firmware assets, or generated binaries. Never print a full ticket in
  logs or reports. Refer to SteamIDs in reports pseudonymously.
- Project creation does not authorize publication, pushing, or replacing
  running lab components. Keep the game UI human-operated unless explicitly
  authorized.
