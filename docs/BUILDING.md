# Building orbiSteamworks

## Requirements

- CMake 3.20 or later.
- A 64-bit C++20 toolchain, with a C compiler for the public-header tests.
- nlohmann/json 3.11 or later.
- The build tool for your CMake generator, such as Ninja, Make or MSBuild.

The source build does not need Steam, a Steamworks SDK, a game, a host checkout
or a publisher server. It downloads no dependencies.

CMake first uses an existing `nlohmann_json::nlohmann_json` target, then looks for
an installed package. Otherwise, set `OSW_JSON_INCLUDE_DIR` to the directory
containing `nlohmann/json.hpp`.

## Standalone build

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
ctest --test-dir build -C Release --output-on-failure
```

For Ninja, add `-G Ninja` to the configure command. On Windows, run from a
developer shell with the chosen compiler available. Visual Studio generators
select the configuration at build time; Ninja and Make use
`CMAKE_BUILD_TYPE`.

| Option | Default | Purpose |
| --- | --- | --- |
| `OSW_BUILD_SHARED` | `OFF` | Also build the runtime-loadable library |
| `OSW_BUILD_TESTS` | `ON` standalone, `OFF` as a subdirectory | Build and register offline contract tests |
| `OSW_BUILD_TOOLS` | `ON` standalone, `OFF` as a subdirectory | Build diagnostics and the authentication helper |
| `OSW_JSON_INCLUDE_DIR` | Discovered | Header directory when no nlohmann/json CMake target is available |

For a library-only build, disable both tools and tests. The options are
independent: tests can run with tools disabled. Tests specific to the diagnostic
executables are registered only when tools are enabled.

## Library targets

`orbisteamworks` is always a static library. A source consumer should use
CMake's target to retain its include directories and link dependencies:

```cmake
project(my_host LANGUAGES C CXX)
add_subdirectory(external/orbisteamworks)
add_executable(my_host main.c)
target_link_libraries(my_host PRIVATE orbisteamworks)
```

The dependency directory is chosen by the consumer; it has no required
relationship to any other repository.

`OSW_BUILD_SHARED=ON` adds `orbisteamworks_shared` without changing the static
target. It exports the functions in `orbisteamworks/osw.h`:

| Platform | Shared file |
| --- | --- |
| Windows x64 | `orbisteamworks.dll` |
| Linux x86-64 | `liborbisteamworks.so` |

Use the public header and an explicit runtime loader; see
[Integration](INTEGRATION.md#shared-library-loading).
The MSVC shared target uses the static C runtime. No C++ object or allocation
ownership crosses the API.

There are currently no CMake install rules or exported package configuration.
Use `add_subdirectory` for source integration, or select the shared artifact and
public header explicitly when packaging. Retain [LICENSE](../LICENSE) and the
notices required by the dependencies included in your build.

## Tools and tests

`OSW_BUILD_TOOLS` builds:

- `osw-auth-helper`: private pipe-based ticket validation for a server.
- `osw-platform-check` and `osw-p2p-check`: integration diagnostics.
- `osw-auth-client` and `osw-auth-server`: authentication diagnostics.

Consult each executable's `--help` before a live check. These tools are not
required by hosts that only consume the library.

CTest uses repository-owned simulated backends and a fake Steam API library.
It checks request ownership, failure behavior, platform operations, routing,
companion lifecycle and the C boundary without a Steam account. Shared builds
also check dynamic symbol loading. These results do not qualify real Steam
transport, a game session or WAN connectivity.

## Platform status

Windows x64 is the exercised build and runtime. Linux has loader, transport and
shared-library code, but live acceptance remains pending. The shared target
explicitly rejects other operating systems; the current POSIX loader depends on
Linux facilities. macOS support requires additional implementation and testing.
