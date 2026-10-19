# orbiSteamworks

A C-compatible library for implementing PS4 platform services on Steam.
Written in C++20, orbiSteamworks can be linked into an emulator, native port or
bridge, or loaded as a shared library through the same public API.

It provides Steam identity, friends, platform sessions, invitations and optional
peer transport. Guest ABI translation belongs to the host; title protocols,
matchmaking rules and progression belong to the application or its server.
Neither a particular host nor a game server is required to build the library.

## Features

- SteamID64 identity and Steam persona names.
- Friends filtered by title, build and realm.
- Session and Invitation v1 WebAPI operations backed by Steam lobbies.
- Asynchronous requests, copied events and explicit result ownership.
- Opt-in PSN presence/session notifications and accepted-invitation events.
- Optional room discovery, authorized UDP endpoints and opaque peer transport.
- Optional local companion process or server-side Steam ticket validation.

Unsupported platform operations return explicit errors. Scores, trophies, TUS,
Matching2 and signaling are not implemented; the entitlement catalog is empty.
The library does not contact Sony services or parse game packets.

[ctrnf-p2p](https://github.com/pierluigizagaria/ctrnf-p2p) uses orbiSteamworks to
add Steam multiplayer to the native Crash Team Racing Nitro-Fueled port; players
looking for that mod should start there.

## Build

Windows preview binaries, the public header and authentication helper are available in
[Releases](https://github.com/pierluigizagaria/orbisteamworks/releases).
Consumers that embed the static library should build from source.

Requires CMake 3.20+, a 64-bit C++20 compiler and nlohmann/json 3.11+.
No Steamworks SDK is needed at compile time.

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

CMake can find an installed nlohmann/json package, use an existing
`nlohmann_json::nlohmann_json` target, or accept
`-DOSW_JSON_INCLUDE_DIR=/path/to/include` containing `nlohmann/json.hpp`.

The default library is static. Add `-DOSW_BUILD_SHARED=ON` to also build
`orbisteamworks.dll` on Windows or `liborbisteamworks.so` on Linux.
See [Building](docs/BUILDING.md) for targets, options and platform limits.

## Use from C or C++

Include [`orbisteamworks/osw.h`](include/orbisteamworks/osw.h). For a source
integration, add this repository to your project and link its CMake target:

```cmake
add_subdirectory(external/orbisteamworks)
target_link_libraries(my_host PRIVATE orbisteamworks)
```

This example supplies application startup and shutdown hooks:

```c
#include <orbisteamworks/osw.h>
#include <string.h>

int32_t platform_open(osw_context **context)
{
    osw_config config = {0};
    int32_t status;

    if (context == NULL)
        return OSW_INVALID_ARGUMENT;
    *context = NULL;
    config.size = sizeof(config);
    config.abi_version = OSW_ABI_VERSION;
    memcpy(config.title, "my-title", sizeof("my-title"));
    memcpy(config.build, "1", sizeof("1"));
    memcpy(config.realm, "default", sizeof("default"));
    /* Empty online_id: derive it from the Steam persona.
       Empty library_path: look beside the host executable. */

    status = osw_create(&config, context);
    if (status != OSW_OK)
        return status;
    status = osw_start(*context);
    if (status != OSW_OK) {
        osw_destroy(*context);
        *context = NULL;
    }
    return status;
}

void platform_close(osw_context **context)
{
    if (context != NULL) {
        osw_destroy(*context); /* Stops and joins the worker before freeing it. */
        *context = NULL;
    }
}
```

`osw_start` starts asynchronous initialization; it does not confirm Steam
sign-in. In your event loop, call `osw_poll_event` and read `osw_get_identity`
after `OSW_IDENTITY_CHANGED`. Initialize every output structure's `size`.
Stop all other API use before calling `platform_close`.

[Integration](docs/INTEGRATION.md) covers the event loop, shared-library loading,
AppID selection and optional P2P. [C API](docs/C_API.md) defines operation schemas,
lifetimes and errors.

## Runtime requirements

Supply Valve's Steam API library separately and run Steam signed in.
An explicit `osw_config.library_path` selects the library; otherwise it is
loaded beside the host executable. Missing dependencies report a signed-out
identity with a reason.

The client AppID defaults to **480** and can be set before startup with
`osw_set_app_id`. Peers must agree on AppID, title, build and realm.
The separate central authentication helper currently supports AppID 480 only.
Cold-launch invitations are unsupported.

Windows x64 is the supported preview platform. Linux support is experimental;
macOS is not currently supported.

## Documentation

- [Quickstart](QUICKSTART.md)
- [Build options](docs/BUILDING.md) and [host integration](docs/INTEGRATION.md)
- [C API](docs/C_API.md), [companion protocol](docs/COMPANION_PROTOCOL.md) and
  [token / authentication helper protocol](docs/OSW1_TOKEN_AND_HELPER.md)
- [PSN notification stream](docs/PSN_EVENTS.md)
- [Architecture and reference index](docs/README.md)
- [Contributing](CONTRIBUTING.md)

## License

[MIT](LICENSE). Steamworks files, game assets and host implementations are not
included. Dependencies and consuming projects retain their own licenses.
