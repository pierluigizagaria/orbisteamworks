# Runtime-loadable library (plugin) design

Date: 2026-10-15. Status: design note and local prototype (I). This is not a
release, a distribution decision or a host change. Distribution decisions stay
deferred by the user.

## Relation to the current decision

This note keeps every decision in [FEASIBILITY.md](FEASIBILITY.md) revision 3.1:

- orbiSteamworks stays an in-process library with one versioned C API.
- Steam replaces shadNet in this setup. The Demonware Server keeps its `steam`
  admission provider, and matchmaking does not change.
- Users install the Steam API library themselves. No Steamworks file is
  distributed.
- AppID 480 stays a permanent constraint.

The only change is how a host obtains the library. Today the shadPS4 fork
compiles it from source and links it statically. A host can instead load a
prebuilt shared library at runtime. Both forms run the same code in the same
process. The plugin form is a different way to deploy the same library, not a
separate service.

## What the prototype builds

`-DOSW_BUILD_SHARED=ON` (default OFF) adds the `orbisteamworks_shared` target:

| Platform | File | Export control |
| --- | --- | --- |
| Windows x64 | `orbisteamworks.dll` | `src/orbisteamworks.def` |
| Linux x86-64 | `liborbisteamworks.so` | `src/orbisteamworks.map` (version node `OSW_ABI_1`) plus hidden visibility |

- **Exports:** the ten functions in `include/orbisteamworks/osw.h`, and nothing
  else.
- **Sources:** compiled separately from the static `orbisteamworks` target.
  The plugin's runtime library and symbol visibility therefore never change
  what an in-process host links.
- **Windows runtime:** the DLL links the C runtime statically. No allocation,
  `FILE*` or C++ object crosses the C API, so host and plugin can use
  different runtimes, compilers and allocators. The DLL needs no VC++
  redistributable.
- **Steam:** unchanged. The plugin loads `steam_api64.dll` or `libsteam_api.so`
  at runtime from `osw_config.library_path`. An empty path selects the file
  next to the host executable, not next to the plugin.
- **Other options:** the default build, `OSW_BUILD_TOOLS`, `OSW_BUILD_TESTS`
  and the `add_subdirectory` use by the shadPS4 fork stay unchanged.

Measured locally on Windows with MSVC 19.50, Release (2026-10-15):

| Check | Result |
| --- | --- |
| `dumpbin /exports orbisteamworks.dll` | 10 functions, 10 names, all `osw_*` |
| `dumpbin /dependents orbisteamworks.dll` | `KERNEL32.dll` only |
| DLL size | 544,768 bytes |
| `ctest`, `OSW_BUILD_SHARED=ON` | 28/28 passed: the 27 existing tests plus `shared-load` |
| `ctest`, default configuration | 27/27 passed |

`shared-load` (`tests/shared_load.c`) is a C program that never links the
library. It loads the DLL with `LoadLibraryA` or `dlopen`, resolves all ten
symbols and walks the PE export table to reject any non-`osw_*` export. It then:
- checks that an unknown `abi_version` is refused;
- starts a context with an absent Steam library;
- waits for `OSW_IDENTITY_CHANGED`;
- requires signed out, `account_id` 0 and `reason_code` 10;
- checks that a token request does not succeed;
- stops, destroys and unloads the library.

The Linux branch of the CMake target and of the test has not been built or run.
On Linux the export check relies on the version script alone.

## The boundary

`osw.h` already has the properties a runtime-loaded library needs:

- **Plain C.** Fixed-size structures, integer status codes and an opaque
  context pointer. No C++ type, STL container, exception or Steam handle
  crosses the boundary. Every exported function catches internally.
- **Self-describing structures.** Each structure starts with `size`, and
  `osw_config` carries `abi_version`. The library rejects mismatches with
  `OSW_INVALID_ARGUMENT` and creates nothing.
- **Library-owned buffers.** Request data is copied on entry. Result buffers
  stay owned by the library until `osw_release` or `osw_destroy`. The host
  never frees library memory.
- **One calling convention.** x64 Windows and the System V AMD64 ABI each have
  a single C calling convention. Names are undecorated.

## Symbol resolution

A host loader shim (I) does the following:

1. **Location.** Take an explicit plugin path from host configuration, or
   default to the directory of the host executable. Never search `PATH` or the
   current directory.
2. **Loading.** On Windows, call `LoadLibraryExW` with an absolute path and
   `LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS`, as the
   library already does for `steam_api64.dll`. On Linux, call
   `dlopen(path, RTLD_NOW | RTLD_LOCAL)`.
3. **Resolution.** Resolve all ten names at load time into one function table.
   If any name is missing, treat the whole plugin as unavailable. Never use a
   partially resolved table.
4. **ABI check.** Call `osw_create` with the host's `OSW_ABI_VERSION`. A refusal
   makes the plugin unavailable for this process.

`tests/shared_load.c` is a working example of steps 2–4 in about 50 lines of C.

Two copies of the library in one process would each pass the one-context guard
and initialize Steam twice. A host therefore uses exactly one form: it either
links the static library or loads the plugin, never both. It always loads the
plugin from one path.

## ABI versioning

Current state: ABI version 1 is unreleased. C_API.md requires host and library
to be rebuilt together while the layout is being qualified.

Proposed rules for the first plugin release (I):

- **Freeze v1 first.** Freeze the layouts of `osw_config`, `osw_identity`,
  `osw_request`, `osw_result` and `osw_event`, the status, operation and event
  values, and the operation schemas in C_API.md.
- **Compatible additions keep version 1:**
  - new exported functions, which hosts resolve as optional;
  - new operation values, which older libraries already refuse with
    `OSW_INVALID_ARGUMENT`;
  - new event types, which hosts must ignore when unknown;
  - new optional JSON fields, which consumers must ignore when unknown.
- **Incompatible changes increment `OSW_ABI_VERSION`:** layout changes,
  changed semantics of an existing field, or removal of anything.
  - On Linux the version script gains a new node (`OSW_ABI_2`).
  - A newer library may keep accepting version 1 configurations and dispatch
    on `abi_version` and `size`. The library decides; hosts never guess.
- **Gap before freezing.** A host cannot tell an ABI mismatch from an invalid
  configuration: both return `OSW_INVALID_ARGUMENT` from `osw_create`.
  Proposal, not implemented because it adds a symbol to the ABI: export
  `uint32_t osw_get_abi_version(void)`, returning the highest supported
  version, and let hosts call it first. Decide this before the first release.

## When the plugin or Steam is missing

Nothing in this path reports success. Each case ends signed out with a reason.

| Situation | Who detects it | Observable result |
| --- | --- | --- |
| Plugin file absent, unloadable, wrong architecture or missing a symbol | Host shim | The provider stays signed out with a host-written reason, such as "orbiSteamworks library not found". There is no token or identity, and WebAPI requests fail with an explicit error |
| `osw_create` refuses the ABI | Host shim | Same as above, with the reason "incompatible orbiSteamworks ABI" |
| Plugin loads, Steam API library absent | Library | `signed_in` 0 and `reason_code` 10 (`library_missing`); verified by `shared-load` |
| Steam library present but unusable | Library | Signed out with the existing codes: 11 symbol missing, 12 Steam client unavailable, 13 Steam user signed out, 20 invalid arguments or presence failure, 22 callback ABI mismatch, 23 library load failure |

- **No silent fallback.** A host configured for the Steam provider never falls
  back to shadNet, because the two providers must not mix accounts, sessions or
  invitations.
- **Reason codes stay library-owned.** Host-side failures use the host's own
  message and leave `reason_code` to the library.
- **Unloading.** A host unloads the plugin only after `osw_destroy` has
  returned, which joins the library thread, and after it stops reading borrowed
  result pointers. Simple hosts keep the plugin loaded until process exit.
  - On Windows, no host may call `osw_stop` or `osw_destroy` from `DllMain` or
    from static destructors that run under the loader lock: stop joins a
    thread.
  - The plugin itself defines no `DllMain` work and starts no thread before
    `osw_start`.

## Threading

Threading does not change with the loading mechanism. C_API.md stays
authoritative:

- `osw_start` creates one library thread. Every Steam call and every callback
  dispatch runs on that thread.
- The library never calls host or guest code. Hosts poll events and results on
  their own threads, and deliver guest callbacks on their existing
  guest-visible path.
- No library lock is held while the host delivers events. Every wait on Steam
  is bounded.
- One production context exists per process, per loaded copy of the library;
  see [Symbol resolution](#symbol-resolution).

## Effect on the shadPS4 fork

Measured at the local fork (`0fdad68` plus uncommitted Steam changes,
2026-10-15):
- the diff touches 10 files, +533/−11 lines;
- the new files `steam_provider.{h,cpp}` (291 lines) and
  `steam_np_auth.{h,cpp}` (255 lines) add about 1,090 lines in total.

The lines fall into three groups:

| Group | Today | With the plugin (I) |
| --- | --- | --- |
| Build coupling | `ENABLE_ORBISTEAMWORKS`, `add_subdirectory` of the library source, static linking, a per-file compile definition; the library shares the host's nlohmann/json | **Removed.** The fork compiles no orbiSteamworks source and shares no C++ dependency |
| `steam_provider.cpp` | `osw_*` calls behind `#ifdef`, configuration mapping, publisher discovery over HTTPS (`/orbi/steam/v1`, CA, title and realm check) | **Loader shim:** the function table and load path from [Symbol resolution](#symbol-resolution), configuration mapping and status translation. Discovery stays in the host, because the library performs no network I/O and C_API.md assigns helper identity to the host's trusted publisher endpoint |
| Guest translation: `NpHandler` (+213), WebAPI transport (+191), `steam_np_auth` (240), friends and invitations (+38), settings (+35) | Translates PS4 guest structures, request handles and callbacks into C API messages | **Unchanged in size, provider-neutral in content.** This code speaks `osw.h`, not Steam, and would form the generic seam below |

The total host line count does not shrink to a few dozen lines. What changes is
that no host line references Steam or builds orbiSteamworks: the Steam-specific
part reduces to choosing and loading a plugin.

## A generic "external NP provider" seam for upstream shadPS4 (I)

With a plugin, upstream shadPS4 could accept a provider-neutral seam:

- **Settings:** `np_provider = shadnet | external`, a plugin path, and the
  generic provider fields the ABI already has: online ID, title, build and
  realm.
- **Behavior:** under `external`, NpManager identity, NpAuth ID tokens,
  in-process WebAPI, friends and invitations go through the C API. Matching2,
  score, TUS and trophies report unavailable.
- **Content:** no Steam code, names or build options. orbiSteamworks would be
  one implementation; another platform library could implement the same ABI.
- **Open choices:**
  - Upstream would either adopt `osw.h` under a neutral prefix or define its
    own ABI that orbiSteamworks adapts to.
  - `library_path` names a vendor dependency of the provider. A neutral seam
    would pass it as an opaque provider setting.
  - Discovery of the ticket-validating server (`server_id`) is
    publisher-specific. A neutral seam would let the provider or the host
    configuration supply the value.

Limits:
- Proposing this upstream needs a new explicit request from the user, an
  upstream discussion first, and shadPS4's rules on disclosed AI use and
  human-written descriptions.
- Acceptance is unknown.
- The seam does not remove the licensing question in the README: a GPL host
  that loads an MIT plugin, which in turn loads a user-supplied proprietary
  library. This is an engineering note, not legal advice.

## The GetLostIn native port

Observed on 2026-10-15 from public GitHub sources (H), without building or
running anything:

- **`runtime_net.c`.** On `online` at `b1b4bf9`, `src/runtime_net.c` has 1,600
  lines. This count replaces the earlier automated estimate of about 1,000
  lines in FEASIBILITY.md.
- **Plain C.** `build.sh` compiles the runtime as C11 on Linux
  (`$CC -std=c11 ... -Werror -pthread src/runtime*.c`).
  - The separate GPU library, which vendors shadPS4's video core, is built
    with CMake and clang++.
  - Windows uses MSYS2 CLANG64.
- **Identity.** Comes from `BB_ONLINE_ID` and `BB_ONLINE_ACCOUNT_ID`, or from a
  hash of the online ID. `sceNpAuthGetIdTokenA` returns a fabricated
  `sacredfire.<online ID>.<account>` string.
- **WebAPI.** 22 `sceNpWebApi*` imports forward requests as plain HTTP to the
  private server: "no TLS, no certificates".

With the plugin, the C runtime needs:
- `osw.h`, for types and constants only;
- `dlopen`/`dlsym` or `LoadLibrary`/`GetProcAddress`;
- the ten function pointers.

It compiles no C++ for orbiSteamworks, needs no nlohmann/json and links no
library code. The fabricated token and identity would be replaced by
`OSW_TOKEN` and `osw_get_identity`. The WebAPI imports would submit
`OSW_WEBAPI` requests in-process. Library events would become NP and
SystemService callbacks on the runtime's guest-visible path.

Native prerequisites stay separate (gate 6):
- **Authenticated discovery.** Fetching the helper `server_id` from the
  publisher's HTTPS discovery endpoint needs TLS with certificate validation,
  which `runtime_net.c` does not provide. Without that, a ticket bound to an
  unauthenticated server ID loses its forwarding protection.
- **Native networking.** Demonware networking and guest event delivery still
  need native qualification.
- **No resumed work.** This note does not resume native-port work, which stays
  deferred until the shadPS4 path passes gate 5.

## Open decisions for the user

- Whether to release a plugin at all, for which hosts, and how to package it.
  The distribution decision stays deferred.
- Whether to freeze ABI v1 and add `osw_get_abi_version` before any release.
- Whether to move the shadPS4 fork from static linking to the loader shim. This
  changes shadPS4 and needs the user's approval first.
- Whether to propose a generic seam upstream.
