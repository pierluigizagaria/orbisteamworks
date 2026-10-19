# Quickstart

orbiSteamworks is a library. An application must integrate its C API before a
player can use it; copying the DLL beside an arbitrary game does not add support.

## Build or embed the library

1. Follow [Building](docs/BUILDING.md) to build the static library or the optional
   shared library. No Steamworks SDK or game assets are needed to compile.
2. Start with the [C example](README.md#use-from-c-or-c) and
   [Integration guide](docs/INTEGRATION.md).
3. Select matching AppID, title, build and realm values for all peers. The AppID
   defaults to 480; it can be set before startup.
4. Supply the Steam API library separately, then run Steam signed in before
   starting your application. Check identity events for initialization failures.

Use [C API](docs/C_API.md) for exact request schemas and ownership rules.
The [companion protocol](docs/COMPANION_PROTOCOL.md) and
[authentication helper](docs/OSW1_TOKEN_AND_HELPER.md) are optional integration
paths; neither requires a particular publisher server.

## Use an existing integration

Follow that application's installation instructions for provider selection,
configuration and game files. Host settings, saves and server data belong to
that application; orbiSteamworks does not provide a universal game launcher.

Each player uses their own Steam account. Invitations require a running,
compatible integration; cold-launch routing is unsupported. Missing Steam
dependencies leave the provider signed out with a reason.

The [documentation index](docs/README.md) links the architecture and interface
references.
