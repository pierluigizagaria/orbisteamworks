# Host integration

orbiSteamworks has one public boundary:
[`orbisteamworks/osw.h`](../include/orbisteamworks/osw.h). Link the
`orbisteamworks` CMake target or load the shared library at runtime. Use one
form and one production context per process.

The host translates guest structures, socket operations and callbacks. The
library supplies platform identity, friends, sessions and opaque peer transport.
The application or its server owns title-specific packets, game rules and
persistence. No sibling source tree or private runtime directory is part of the
interface.

## Configure and start

1. Zero-initialize `osw_config`, set `size = sizeof(osw_config)` and
   `abi_version = OSW_ABI_VERSION`.
2. Set nonempty `title`, `build` and `realm`. Title/build accept ASCII
   letters, digits, `_`, `-` and `.`; realm excludes `.`. All peers must agree.
3. Leave `online_id` empty to derive it from the Steam persona, or supply a
   valid explicit ID. Leave `library_path` empty for the Steam API library
   beside the host executable, or provide its UTF-8 path.
4. Call `osw_create`. To select a nondefault AppID, call `osw_set_app_id`
   with a nonzero ID before `osw_start`. The default is 480.
5. Call `osw_start`, then observe identity asynchronously.

`osw_start == OSW_OK` means that the worker started, not that the user signed
in. Missing Steam libraries, an unavailable Steam client or an account change
are reported through identity state and events. An account change requires a
new context. A stopped context cannot restart.

The default Steam library names are `steam_api64.dll` on Windows and
`libsteam_api.so` on Linux. Valve's libraries are supplied separately; they
are not build dependencies or repository contents.

## Events and requests

The application polls on its own thread; the library never calls host or guest
callbacks. For example, call this from the application's normal update loop:

```c
#include <orbisteamworks/osw.h>

int32_t platform_poll_identity(osw_context *context, osw_identity *identity)
{
    osw_event event = {0};
    int32_t status;

    if (identity == NULL)
        return OSW_INVALID_ARGUMENT;
    event.size = sizeof(event);
    while ((status = osw_poll_event(context, &event)) == OSW_OK) {
        if (event.type == OSW_IDENTITY_CHANGED || event.type == OSW_RESYNC) {
            identity->size = sizeof(*identity);
            status = osw_get_identity(context, identity);
            if (status != OSW_OK)
                return status;
        }
        /* Handle other event types here when adding requests or sessions. */
    }
    return status == OSW_PENDING ? OSW_OK : status;
}
```

Initialize the identity output to zero before the first call. This minimal loop
handles identity only; an application using requests must also process request,
friend and session events. On `OSW_RESYNC`, refresh all snapshots it uses.

Submit operations with `osw_submit`. Metadata and body buffers are copied
before the call returns. Use `osw_get_result` until completion, process the
status and buffers, then call `osw_release`. The library owns result buffers;
never free them or use them after release/destruction. Each request has its own
timeout.

Before shutdown, stop all concurrent API use and borrowed-buffer reads.
`osw_stop` joins the worker and aborts pending requests; completed results remain
readable until released. `osw_destroy` stops and frees the context.
See [C API](C_API.md) for operation schemas, limits and error codes.

## Shared-library loading

Build with `OSW_BUILD_SHARED=ON`. Load the library from an explicit, trusted
path using the platform loader (`LoadLibraryExW` or `dlopen`), and resolve every
function the integration uses. Missing required symbols are an incompatibility,
not a reason to continue with a partial function table.

Use the public header that matches the selected provider version. Configuration
structure sizes and ABI values are checked at creation. ABI 1 remains available;
`osw_create_v2` opts into the P2P extension without changing ABI 1 structures.
There is no API-version query; resolve additive exports before enabling their
features.

Unload only after context destruction and after all API calls and buffer reads
have stopped. On Windows, never call stop/destroy from `DllMain` or another
loader-lock callback. [`tests/shared_load.c`](../tests/shared_load.c) demonstrates
the C function table and lifecycle checks.

## Optional P2P and companion

`osw_create_v2` enables room discovery and opaque peer transport. Configure an
exact loopback game address and UDP port. Select a canonical endpoint mode
before starting, when the host needs one:

- `virtual-v1`: the host uses the public route lookups to translate endpoints.
- `loopback-v1`: provider-owned sockets bind the canonical peer endpoints
  directly, so the host needs no socket translation.

The provider allocates endpoints and authorizes peers. The application treats
payloads as opaque and keeps title-specific address encoding outside the provider.

A companion executable is optional. When configured, the library launches it
with private inherited pipes, supervises its lifetime and negotiates the
capabilities required by the selected endpoint mode. The companion owns its
application settings and data. A deferred game endpoint requires a companion
and an explicit acknowledged registration; it is never learned from UDP packets.

See [C API](C_API.md#p2p-extension-abi-2) and
[Companion protocol](COMPANION_PROTOCOL.md) before enabling this extension.
The [simulated test backend](C_API.md#simulated-test-backend) is an explicit
test option, never a Steam fallback.

## Server authentication

For central admission, an application can request a server-bound `osw1` Steam
ticket and validate it through `osw-auth-helper`. The server obtains the helper's
identity through its own trusted discovery mechanism and owns persistence and
replay policy. The helper is optional and currently fixed to AppID 480.

With a local companion, the credential is instead a process-local capability
bound to the pipe owner. It must never be treated as a verified Steam ticket by
a central server. The [token/helper specification](OSW1_TOKEN_AND_HELPER.md) and
[companion protocol](COMPANION_PROTOCOL.md) define these distinct flows.
