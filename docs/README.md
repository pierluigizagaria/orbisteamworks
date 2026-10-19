# Documentation

## Using the library

- [Quickstart](../QUICKSTART.md): choose a build or an existing integration.
- [Building](BUILDING.md): dependencies, CMake targets, tests and platform status.
- [Integration](INTEGRATION.md): host lifecycle, events, dynamic loading and P2P.
- [Contributing](../CONTRIBUTING.md): boundaries and validation expectations.

## Interface references

- [C API](C_API.md): structures, operation schemas, ownership and errors.
- [Companion protocol](COMPANION_PROTOCOL.md): optional child-process contract.
- [Token and authentication helper](OSW1_TOKEN_AND_HELPER.md): central admission.
- [Helper executable](../server/auth-helper/README.md): process and pipe behavior.

These interfaces are owned by this repository. A host, bridge or publisher uses
them without importing another component's sources or private runtime state.

## Design and validation history

The following documents retain dated decisions and observations. Later addenda
supersede earlier constraints; they are not installation or release instructions.
Current setup is described above.

- [Architecture record](ARCHITECTURE.md)
- [P2P design record](P2P_DESIGN.md)
- [Runtime-loadable library design record](PLUGIN_DESIGN.md)
- [Feasibility and original gates](FEASIBILITY.md)
- [Evidence sources](SOURCES.md)
- [Contract inventory](GATE0_CONTRACT_INVENTORY.md)
- [Authentication validation](GATE1_AUTH_SPIKE.md)
- [P2P validation](P2P_VALIDATION.md)
- [CTR validation case](CTR_VALIDATION_CASE.md)
- [Integration work log](INTEGRATION_WORKLOG.md)
- [Local shadPS4 proof of concept](LOCAL_POC.md)
- [Historical shadPS4 setup](history/SHADPS4_SETUP_20261015.md)

Private workspace paths in these records identify historical evidence, not build
dependencies. Offline contracts, live Steam checks and game or multi-PC acceptance
are separate kinds of evidence.
