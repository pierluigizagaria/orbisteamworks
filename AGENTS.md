# Project instructions

Use the display name **orbiSteamworks** and repository name `orbisteamworks`.
Write project documentation in English; respond in the user's language.

## Read first

- [README.md](README.md): purpose, build and runtime limits.
- [Integration](docs/INTEGRATION.md) and [C API](docs/C_API.md): consumer contract.
- [Companion protocol](docs/COMPANION_PROTOCOL.md) when changing P2P or children.
- [Sources](docs/SOURCES.md) when making platform-compatibility claims.

The feasibility, architecture and P2P design records contain dated decisions.
Current public interfaces and explicit user instructions take precedence over
superseded host choices, fixed-AppID rules and historical publication restrictions.

## Architecture and ownership

- The implementation is C++20 with one versioned C-compatible API. Static and
  shared forms run the same library in their consumer's process. Consumers can
  be an emulator, native port or independent bridge.
- Keep Steam symbols and flat-API declarations in `src/steam/`. Core code carries
  no Steam, guest or host types.
- Identity, platform sessions, authorization, canonical endpoint allocation and
  opaque peer transport belong here. Guest ABI/socket translation belongs to
  the host. Title packets, address formats, matchmaking rules and economy belong
  to the application or publisher. Do not copy their implementation here.
- The public C API and optional companion pipe are the integration boundaries.
  Do not require sibling checkouts, build paths or shared private runtime state.
- Keep existing ABI layouts and defaults when adding opt-in features. Support
  legacy, virtual-v1 and loopback-v1 consumers through explicit capabilities;
  incompatible configurations fail rather than silently falling back.

## Runtime behavior

- The client AppID is configurable before start, default 480. Peers must agree
  on AppID, title, build and realm. Never act on unrelated lobbies. Economy mode
  is not a platform namespace. The central auth helper currently remains 480.
- SteamID64 is the account identity; preserve it losslessly. The online ID is
  derived from the Steam persona unless explicitly provided by the consumer.
  Keep it valid UTF-8, at most 16 bytes, without controls or surrounding spaces.
  Host-specific name restrictions stay in the host. An explicit name override
  does not promise live persona renames.
- An account change ends the current context; the consumer must create another.
  Missing Steam dependencies or unsupported operations report explicit failures.
  Never fabricate identity, tokens or successful operations.
- One library worker owns Steam calls and callback pumping. Never call host or
  guest code from the library. Hosts poll copied events on their own threads.
- Copy requests on entry, retain results until release, and preserve documented
  cancellation/teardown ownership. Bound individual requests; do not introduce
  menu-idle or elapsed-time limits for established sessions.
- Steam lobby membership is not committed game-party membership until the
  application's membership operation completes.
- Companion configuration and persistence belong to its caller. Keep the child
  lifecycle, inherited-pipe authority and endpoint acknowledgements explicit.

## Dependencies and licensing

- This repository is MIT. Host and publisher code retain their own licenses.
- Load the user-supplied Steam API library at runtime. Never commit or bundle
  Steamworks SDK headers, libraries, redistributables or other Valve binaries.
  Declare only the flat-API subset used; record the SDK revision audited.
- Preserve required notices for other dependencies. Do not copy source from
  shadNet, shadPS4, native ports or publishers into this repository.

## Validation and repository hygiene

- Build and run ordinary tests independently of any host, publisher or game.
  Keep integration fixtures optional. Test observable contracts, ownership and
  failure behavior rather than mirroring implementation details.
- Report offline, live Steam, game and multi-PC results separately. Do not infer
  Linux or macOS acceptance from Windows tests or source portability.
- Use owned assets and lawful public references. No leaked SDK documents,
  production Sony/Demonware requests or untrusted downloaded executables.
- Keep builds in `build/`, local tools in `out/`, private state in `runtime/`.
  Never commit credentials, tickets, private identities, profiles, game assets
  or generated binaries. Do not print full tickets or sensitive request bodies.
- Follow the current user's scope for commits, publishing and releases. Do not
  change installed profiles, launch games or operate game input without explicit
  authorization. Review staged paths and ignore rules before each commit.
