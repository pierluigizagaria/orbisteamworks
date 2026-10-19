# Documentation

## Getting started

- [Quickstart](../QUICKSTART.md): build the library or use an existing integration.
- [Building](BUILDING.md): dependencies, CMake targets and supported platforms.
- [Integration](INTEGRATION.md): host lifecycle, events, shared loading and P2P.
- [Contributing](../CONTRIBUTING.md): component boundaries and automated checks.

## Reference

- [C API](C_API.md): structures, operation schemas, ownership and errors.
- [Companion protocol](COMPANION_PROTOCOL.md): optional child-process interface.
- [Token and authentication helper](OSW1_TOKEN_AND_HELPER.md): central admission.
- [Helper executable](../server/auth-helper/README.md): process and pipe behavior.
- [Architecture](ARCHITECTURE.md): implementation responsibilities and lifecycle.
- [P2P transport](P2P_DESIGN.md): room discovery, endpoints and authorization.
- [Sources](SOURCES.md): platform references and compatibility scope.

These interfaces are owned by this repository. A host, bridge or server uses
them without importing another component's sources or private runtime state.
