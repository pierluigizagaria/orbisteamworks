# Architecture

orbiSteamworks is an in-process C++20 library with one versioned C-compatible
boundary. Static and shared builds expose the same public header. Hosts can be
emulators, native applications or independent bridges.

## Responsibilities

| Component | Owns |
| --- | --- |
| Library core | Identity, friends, sessions, request lifetimes, invitations and copied events |
| Steam adapter | Runtime loading, Steam calls, callback dispatch and opaque peer messaging |
| Host | Guest ABI, application lifecycle, socket translation and user interface |
| Optional companion | Its application protocol, rules, configuration and persistent data |
| Optional authentication helper | Server-side Steam ticket validation over private pipes |

The library does not parse title packets, encode game-specific addresses, own
progression or import a host's implementation. Integrations use the
[C API](C_API.md) and optional [companion protocol](COMPANION_PROTOCOL.md).

## Source layout

- `include/orbisteamworks/osw.h`: public C declarations.
- `src/core/`: platform model, requests, events, session payloads, directory,
  endpoint registry and transport coordination.
- `src/steam/`: runtime loader, flat-API declarations and Steam backend.
- `src/lab/`: the explicitly selected simulated test backend.
- `src/steam/testing/`: a fake Steam API library and its tests.
- `server/auth-helper/`: optional pipe-based authentication helper.
- `tests/`: automated contracts and fixtures.

Only the Steam adapter carries Steam types. No C++ objects, exceptions, Steam
handles or caller-owned allocations cross the public C boundary.

## Threading and ownership

One worker owns Steam calls and callback pumping. Hosts poll copied events on
their own threads; the worker never calls host or guest code. Request inputs are
copied on entry. Completed buffers belong to the library until the caller releases
them. Stopping joins the worker and aborts pending requests; destruction releases
all retained state. Callers must stop concurrent API use before destruction.

Individual requests have bounded waits; established sessions have no idle or
session-length limit. An unavailable Steam client, missing dependency, unsupported
operation or account change produces an explicit failure.

## Sessions and invitations

Sessions use Steam lobbies scoped by AppID, title, build and realm. Invitations
progress from receipt through acceptance and provisional join to application
membership. Steam lobby membership alone does not commit game-party membership.
Duplicate acceptance is idempotent; failures roll back provisional membership.

Large session payloads use a bounded chunk protocol with owner, recipient, nonce
and content-hash checks. The library keeps platform data separate from opaque
peer datagrams. A host is responsible for delivering invitation events through
its own interface; cold-launch invitation routing is unsupported.

## Optional peer transport

The opt-in ABI extension provides directory operations, peer authorization and
canonical UDP endpoints. The endpoint registry supplies copied routes through
short synchronized operations. A host can translate virtual endpoints, or select
provider-bound loopback endpoints. Title-specific address formats remain outside
the library. See [P2P transport](P2P_DESIGN.md).

A configured companion is a supervised child with private inherited pipes.
Its capabilities are negotiated explicitly. The companion owns application data;
the provider owns authorization and transport. Closing the context tears down
both transport and the owned child.
