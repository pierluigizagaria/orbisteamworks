# orbiSteamworks

orbiSteamworks implements PS4 platform services on top of Steamworks, so a PS4
game running in an emulator or a native port can use Steam for its online
identity, friends, sessions and invitations.

The host links the library in-process and forwards the game's NP calls to it:
NpManager identity, NpAuth ID tokens and NpWebApi requests. The library answers
them using the Steam client already running on the player's PC. A private game
server can then admit players by verified SteamID.

The native P2P extension uses ABI 2: each host owns a hidden local publisher,
discovers rooms through invisible Steam lobbies and sends room messages and
race datagrams through authenticated Steam Networking Messages. The publisher's
progression, economy, statistics and files stay local. This is an opt-in mode;
the original ABI 1 and central ticket-validation path remain available.
See [P2P_DESIGN.md](docs/P2P_DESIGN.md) and
[P2P_VALIDATION.md](docs/P2P_VALIDATION.md) for its separate acceptance evidence.

```text
PS4 game ──► host (emulator / native port) ──► orbiSteamworks ──► steam_api64 ──► Steam client
                 │                                (C API, in-process)          (supplied by the user)
                 ├─► game publisher server ──► osw-auth-helper ──► Steam (ticket validation)
                 └─► peer-to-peer game traffic (unchanged)
```

## What it provides

| PS4 service | Steam implementation |
| --- | --- |
| Account ID | SteamID64 |
| Online ID | The Steam persona name, cleaned to at most 16 bytes of UTF-8 (or a name set by the host) |
| NpAuth ID token | `osw1` Steam ticket for central admission, or a per-process capability bound to the hidden local publisher |
| Friend list | Steam friends currently running the same title, build and realm, shown online |
| Sessions | Friends-only Steam lobbies, with session data, changeable data and images |
| Invitations | Steam lobby invitations, accepted from the game or from the Steam overlay |
| Entitlements | An empty list: the library grants no purchases |

The WebAPI surface is the Session and Invitation v1 set that NpToolkit2 uses.
Requests outside it fail with HTTP 501; nothing is forwarded to Sony.
Scores, TUS, trophies, Matching2 and signaling are not implemented. Game traffic
uses the host's sockets; ABI 2 can supply per-peer loopback UDP proxies backed
by Steam transport.

## Requirements and limits

- **Steam on every player's PC.** Steam allows one account per client, so each
  player needs their own PC.
- **No Steamworks files are distributed.** The library loads the Steam API
  library at runtime and declares only the flat-API functions it uses. Each
  user downloads the Steamworks SDK from Valve and places `steam_api64.dll`
  (or `libsteam_api.so`) next to the host. Without it the provider stays
  signed out and says why.
- **AppID 480 (Spacewar), permanently.** Friends see "Playing Spacewar".
  Invitations work only while the game is running: accepting one with the game
  closed launches Spacewar. Lobbies share the 480 namespace; the library ignores
  any lobby that does not carry its own protocol, title, build and realm.
  Valve tolerates this use but does not support it.

orbiSteamworks therefore suits private groups and small communities, not a
public release.

## Hosts and servers

A host needs a small amount of glue that translates its PS4 structures and
callbacks into the [C API](docs/C_API.md). The game logic stays in the library.

- **shadPS4:** a fork adds a Steam provider behind `NpHandler`, the WebAPI
  transport and NpAuth. It is not published yet.
- **GetLostIn native port of CTR Nitro-Fueled:** the first native P2P host, through
  the shared ABI 2 from its `runtime_net.c`; its game acceptance is tracked
  separately from the library's fixture tests.
- **Game servers:** a server verifies `osw1` tokens by running the
  `osw-auth-helper` included here, which logs on to Steam as an anonymous game
  server. The token format and the helper's protocol are specified in
  [OSW1_TOKEN_AND_HELPER.md](docs/OSW1_TOKEN_AND_HELPER.md). A `steam`
  admission provider for the
  [Demonware server](https://github.com/pierluigizagaria/demonware-server)
  exists locally and is not published yet.

See [QUICKSTART.md](QUICKSTART.md) for player and operator setup.

## Status

The library is a working proof of concept, not a release. It has been
exercised with CTR Nitro-Fueled (CUSA13795 1.21) in the shadPS4 fork on Windows,
with one Steam account. Steam sign-in, the Steam name in game, publisher
admission, party creation and matchmaking search work. Friends, invitations,
a shared party and a race between two Steam accounts are not verified yet,
nor is Linux. The [feasibility document](docs/FEASIBILITY.md#acceptance-gates)
lists the acceptance gates.

The native P2P candidate additionally passes 32 Windows static/shared contract
tests, including a two-process simulated backend and hidden-child ownership.
These results do not establish a Steam peer race or WAN/SDR acceptance.

## Building

Requirements: CMake 3.20+, a 64-bit C++20 compiler (MSVC, Clang or GCC) and
the nlohmann/json headers. No Steamworks SDK is needed to compile.

```sh
cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release -DOSW_JSON_INCLUDE_DIR=<path>
cmake --build build
ctest --test-dir build --output-on-failure
```

The offline test suites use a simulated Steam library and need no Steam
client. A standalone build also produces the authentication tools and
`osw-auth-helper`.

| Option | Default | Effect |
| --- | --- | --- |
| `OSW_BUILD_TESTS` | on when standalone | Contract tests |
| `OSW_BUILD_TOOLS` | on when standalone | `osw-auth-helper` and test tools |
| `OSW_BUILD_SHARED` | off | Runtime-loadable `orbisteamworks` library ([design](docs/PLUGIN_DESIGN.md)) |

A host normally adds this repository with `add_subdirectory`, links the
`orbisteamworks` target and includes `orbisteamworks/osw.h`. Tools and tests
then default to off.

## Using the C API

The API is versioned, message-based and C-compatible. Requests are copied on
entry, results stay owned by the library until released, and events are
polled by the host on its own thread. The library never calls back into the
host.

```c
osw_config config = {sizeof(config), OSW_ABI_VERSION};
strcpy(config.title, "5760");      /* project namespace values agreed by all players */
strcpy(config.build, "1.21");
strcpy(config.realm, "my-server");
/* config.online_id left empty: derived from the Steam persona */

osw_context *context;
osw_create(&config, &context);
osw_start(context);

osw_identity identity = {sizeof(identity)};
osw_get_identity(context, &identity);  /* signed_in, account_id, online_id, reason */
```

WebAPI requests, ID tokens, friend snapshots and invitations go through
`osw_submit` / `osw_get_result` / `osw_release`; Steam changes arrive through
`osw_poll_event`. See [C_API.md](docs/C_API.md).

## Repository layout

| Path | Contents |
| --- | --- |
| `include/orbisteamworks/osw.h` | Public C API |
| `src/core/` | Platform logic: identity, sessions, invitations, WebAPI routes; no Steam types |
| `src/steam/` | Runtime Steam loader and the flat-API subset; the only code that touches Steam |
| `server/auth-helper/` | Server-side ticket validator |
| `tests/` | Offline contract tests |
| `tools/` | Authentication spike and live platform check |
| `docs/` | Design, API, protocol and validation records |

## Documentation

- Setup: [QUICKSTART.md](QUICKSTART.md)
- Interfaces: [C API](docs/C_API.md), [osw1 token and auth helper](docs/OSW1_TOKEN_AND_HELPER.md),
  [runtime-loadable library](docs/PLUGIN_DESIGN.md)
- Design: [architecture](docs/ARCHITECTURE.md), [feasibility and gates](docs/FEASIBILITY.md)
- Validation records: [contract inventory](docs/GATE0_CONTRACT_INVENTORY.md),
  [ticket validation](docs/GATE1_AUTH_SPIKE.md),
  [CTR validation case](docs/CTR_VALIDATION_CASE.md),
  [integration log](docs/INTEGRATION_WORKLOG.md), [local test setup](docs/LOCAL_POC.md),
  [sources](docs/SOURCES.md)
- Contributing: [AGENTS.md](AGENTS.md)

## License

MIT; see [LICENSE](LICENSE). The repository contains no Steamworks SDK files,
no source from host projects, and no firmware or game assets. Host glue stays
in the host's repository under the host's license.

A GPL host that loads a user-supplied proprietary library leaves the
combination on the user's machine. Some interpretations, including the FSF's,
still treat such a program as one work. This is an engineering note, not legal
advice.
