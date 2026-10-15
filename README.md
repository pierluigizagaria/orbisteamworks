# orbiSteamworks

A library that implements the PS4 platform services a game needs (identity,
friends, platform sessions, invitations, ID tokens) on top of Steamworks. Hosts
link it in-process. The first host is a shadPS4 fork with a Steam provider
behind its existing NP facade. The GetLostIn native CTR port is planned as the
second host.

## Status

Project initialized on 2026-10-14. The current design was decided on
2026-10-15: Steam replaces shadNet for this setup, and the library runs inside
the host process. **Gate 1 passed:** an SDK-free runtime loader and standalone
CMake client/server tools validate AppID 480 tickets through an anonymous game
server. See the [gate report](docs/GATE1_AUTH_SPIKE.md) and
[build/test instructions](tools/auth-spike/README.md). The C API, initial host
glue and verified publisher provider are implemented locally and undergoing
integration testing. The current scope is the shadPS4 proof of concept;
distribution decisions are deferred. A runtime-loadable build
(`OSW_BUILD_SHARED`, off by default) exists only as a
[design prototype](docs/PLUGIN_DESIGN.md). There is no
release or paired-gameplay acceptance. The actual game has passed Steam-backed
login, platform-party creation, publisher search and manual cancel/reentry on
one account. See the [local PoC instructions](docs/LOCAL_POC.md)
and [work log](docs/INTEGRATION_WORKLOG.md).

## How it works

```text
CTR (PS4 build)
  |
  shadPS4 fork: NpHandler --provider=steam--> orbiSteamworks --runtime load--> steam_api64.dll --> Steam client
                WebAPI transport --in-process--^                                (installed by the user, AppID 480)
  |
  +-- Demonware protocol --> Demonware Server (CTR_ADMISSION=steam) --> auth helper (anonymous game server) --> Steam
  +-- peer UDP ------------> other game clients (unchanged)
```

- **The shadPS4 provider is selected behind `NpHandler`.** The WebAPI transport
  and NpAuth call the library in-process. The existing guest interfaces and
  default shadNet path are retained; the wider NP surface is not implemented
  by this PoC.
- **Steam supplies the platform facilities:** SteamID64 as account ID, friends
  and presence, lobbies as platform sessions, lobby invitations.
- **The ID token carries a Steam session ticket.** The publisher validates it
  through a new `steam` admission provider and a small server-side helper. The
  helper logs on to Steam as an anonymous game server, so no Web API key is
  needed.
- **Publisher services and peer transport stay as they are.**

Players run Steam and the shadPS4 fork; orbiSteamworks runs inside the emulator.
The Demonware host additionally runs the ticket-validation helper, which
Demonware starts and manages as its child process. Players do not run that
helper or a separate local orbiSteamworks server. In the current single-machine
PoC, the same PC hosts both the player and Demonware.

## Distribution model

Builds and sources contain **no Steamworks files**:
- The library loads the Steam API library at runtime.
- It declares the small subset of the flat API it uses itself.
- Users download the Steamworks SDK from Valve, accept Valve's terms and place
  `steam_api64.dll` next to the executable.
- Without the DLL, the Steam provider reports signed out and the host behaves
  as it does today.

Shipping only GPL code with source, while the user supplies the proprietary
library, keeps the combination on the user's machine. Some interpretations,
including the FSF's, still consider a GPL program designed to load a non-free
library a single work. The risk is judged low for a community project but not
zero. This is an engineering assessment, not legal advice.

## Explicit constraint: AppID 480

orbiSteamworks runs on Spacewar, AppID 480, and has no other AppID. This is a
permanent constraint, not a development placeholder:

- Steam shows friends "Playing Spacewar".
- An invitation accepted while the game is closed launches Spacewar, not CTR.
- Lobbies share a namespace with every other project that uses 480.
- Valve tolerates this use but does not support it, and it can change.

The project is suitable for private groups and small communities. It is not a
path to a public product.

## Scope

- **Target:** CTR Nitro-Fueled CUSA13795 1.21 in a shadPS4 fork, with the
  user's Demonware Server as publisher.
- **Later:** the GetLostIn native port through the same C API.
- **Out of scope:** other titles, score/TUS/trophies/Matching2, carrying game
  traffic over Steam networking.

## First milestone

Gate 1 passed anonymous AppID 480 session-ticket validation with the runtime
Steam loader. Its measured results and freshness limitations are in the
[gate report](docs/GATE1_AUTH_SPIKE.md). The next milestone is to sign in to CTR
through the shadPS4 fork and complete one public race between two Steam
accounts. See the
[acceptance gates](docs/FEASIBILITY.md#acceptance-gates).

## Read next

- [Quickstart for players and operators](QUICKSTART.md)
- [Feasibility, decision and gates](docs/FEASIBILITY.md)
- [Architecture](docs/ARCHITECTURE.md)
- [C API](docs/C_API.md) and [runtime-loadable library design](docs/PLUGIN_DESIGN.md)
- [osw1 token and auth-helper protocol](docs/OSW1_TOKEN_AND_HELPER.md)
- [CTR invitation-to-public-matchmaking validation case](docs/CTR_VALIDATION_CASE.md)
- [Evidence and pinned references](docs/SOURCES.md)
- [Contributor instructions](AGENTS.md)

## Licensing

The user selected the [MIT license](LICENSE) on 2026-10-15, compatible with
the GPL-2.0-or-later hosts. The library contains no source from shadNet
or shadPS4. Host glue lives in the host forks
under their license. No third-party source, Steamworks SDK file, firmware or
game asset is included.
