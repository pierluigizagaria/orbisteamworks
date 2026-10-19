# C API reference

The public header is `include/orbisteamworks/osw.h`. Every structure starts with
a `size` field that the caller initializes. `osw_create` takes an ABI 1
`osw_config`; `osw_create_v2` takes the [P2P extension](#p2p-extension-abi-2).
All later additions are separate exports that leave both layouts unchanged.

## Contexts and lifecycle

`osw_config` contains an optional runtime-library path, an optional online ID
(empty: derived from the Steam persona), and the `title`, `build` and `realm`
namespace. Only one production context may exist in a process, and Steam owns
one signed-in account.

`osw_start` starts the library thread; callers query identity to learn whether
initialization succeeded. Steam symbols and callback dispatch run only on that
thread, and the library never invokes host or guest callbacks. Missing
dependencies produce a signed-out identity with a reason, never fabricated
success.

A context keeps the SteamID it started with. If another account signs in behind
the same Steam client, the context fails (signed out, account ID 0, reason
`Steam account changed; restart required`) and publishes `OSW_IDENTITY_CHANGED`;
the host creates a new context to adopt that account. A temporary sign-out does
not fail the context.

`osw_stop` joins the library thread and completes pending requests with
`OSW_ABORTED`; a stopped or failed context cannot restart. `osw_destroy` stops
and frees the context and must not race any other API call. The caller must
serialize release and destruction against reads of borrowed result data.

### AppID

`osw_set_app_id(context, uint32_t app_id)` selects a nonzero AppID after creation
and before `osw_start` (default 480), for ABI 1 and ABI 2 contexts. Calls after
start or stop return `OSW_INVALID_ARGUMENT`. The value reaches Steam
initialization, product identification and app/game callback filters. Every
player must use the same AppID; account licensing and initialization errors are
Steam's decision. The [authentication helper](OSW1_TOKEN_AND_HELPER.md) always
uses 480.

### Persona name

`osw_get_persona_name(context, char *buffer, size_t capacity, size_t *required)`
copies the raw Steam persona captured at startup, before online-ID cleaning,
trimming or truncation. `required` includes the terminating NUL. A null buffer
with zero capacity queries that size and returns `OSW_LIMIT`; short buffers are
left unchanged and also return `OSW_LIMIT`. Signed-out contexts return
`OSW_SIGNED_OUT` without changing outputs. The call only takes the identity mutex
and makes no Steam or network call. An explicit online ID does not alter the raw
persona.

A launcher can create a context without a companion, read the raw persona,
destroy that context, then create a new context with its own explicit online ID.
The override is fixed for the lifetime of that context.

## Status codes

| Code | Typical meaning |
| --- | --- |
| `OSW_OK` (0) | Success |
| `OSW_PENDING` (1) | Not complete or not ready yet |
| `OSW_INVALID_ARGUMENT` (-1) | Malformed input, or a call in the wrong lifecycle state |
| `OSW_SIGNED_OUT` (-2) | No signed-in Steam account |
| `OSW_UNAVAILABLE` (-3) | Steam, the context or a required service is unavailable |
| `OSW_NOT_SUPPORTED` (-4) | Operation, mode or capability not supported |
| `OSW_ABORTED` (-5) | Cancelled, or aborted by stop |
| `OSW_TIMEOUT` (-6) | The request deadline expired |
| `OSW_LIMIT` (-7) | A size, count or buffer limit was reached |
| `OSW_NOT_FOUND` (-8) | Unknown item, route or session |

Expected validation and Steam runtime failures use explicit return values inside
the library as well, so a host's Windows vectored exception handler does not see
first-chance C++ exceptions for them. Automated Windows tests cover malformed
paths and identifiers, missing libraries and symbols, failed client initialization,
invalid native counts, ticket failures and incompatible callback payloads; this
is not a guarantee against every possible internal exception. A fatal backend
failure completes pending work and stops the library thread; completed buffers
remain owned until release.

## Requests

`osw_submit` copies metadata and binary data and returns a monotonically
increasing request ID. Each request has a 1–60000 ms timeout, at most 64 KiB of
JSON metadata and 1 MiB of body data. At most 128 requests are retained, with
separate 16 MiB input and output budgets.

`osw_get_result` returns `OSW_PENDING` until completion, then an operation status
and borrowed response buffers. Those buffers remain valid until `osw_release` or
context destruction, including across `osw_stop`. `osw_cancel` completes a
pending request with `OSW_ABORTED` and ignores late replies; timeouts produce
`OSW_TIMEOUT`.

## Operations

### `OSW_TOKEN` (1)

Metadata contains string fields `client_id`, `scope` and decimal `server_id`.
Supported scopes are `openid`, `openid openid:duid` and `openid,openid:duid`.
A successful response body is the opaque `osw1` credential, without a NUL
terminator. The host obtains `server_id` from its trusted server's discovery
endpoint. A new token replaces the previously issued ticket; releasing a result
does not cancel an issued credential. The format is specified in
[osw1 token and auth-helper protocol](OSW1_TOKEN_AND_HELPER.md); it is a
project-defined credential, not a Sony OAuth token. With a companion, see
[Local companion token](#local-companion-token).

### `OSW_WEBAPI` (2)

Metadata contains `method`, `path`, optional `api_group`, `content_type` and
`headers`. The body is opaque, including multipart data. The result owns its
HTTP status, metadata (`content_type`) and response body. Platform HTTP errors
have an HTTP status and a JSON error body; library failures use a negative
operation status. Unsupported routes return HTTP 501.

Supported route families include session creation, metadata, members,
immutable and changeable data, images, invitations, and the local user's
session, invitation and friend lists. The platform entitlement catalog is empty:
it grants no purchases. Unknown routes fail; requests are never forwarded to
another service. Pagination, remote user session enumeration and the wider PS4
API are incomplete.

Session metadata uses one document of at most 6,000 bytes plus namespace keys,
because Steam refuses lobby metadata beyond roughly 8 KB in total. Large
immutable payloads therefore stay in a bounded cache and are fetched from the
current lobby owner through private lobby messages, in blocks below Steam's
4 KB message limit. The receiver validates namespace, sender, transfer ID,
offset, total size and checksum. The checksum detects inconsistent copies; the
authenticated Steam sender and current owner determine trust. Transfers have
request deadlines and a 16 MiB cache budget, and these deadlines do not
disconnect committed game parties. A joining client obtains all large immutable
payloads before acceptance completes; game membership is committed only by the
application's own membership request. Peer UDP transport is unaffected.

### `OSW_FRIENDS` (3) and `OSW_RESOLVE_NAME` (7)

`OSW_FRIENDS` returns a `friendList` snapshot with decimal-string account IDs,
online IDs, `presence.onlineStatus` and `personalDetail.displayName` (the
cleaned persona, at most 32 bytes). Only Steam friends currently running a
compatible integration with the same title, build and realm are listed. Names
of friends who leave stay cached for name resolution and invitations in the
current context; they are not listed and not persisted across restarts.
Online IDs are 1–16 bytes of UTF-8 without control characters or surrounding
spaces.

`OSW_RESOLVE_NAME` takes `online_id` and resolves the local user or a known friend.

### `OSW_INVITE_SEND` (4), `OSW_INVITE_ACCEPT` (5), `OSW_INVITE_DECLINE` (6)

`OSW_INVITE_SEND` takes `session_id` and a `to` array of known online IDs or
decimal account IDs. `OSW_INVITE_ACCEPT` and `OSW_INVITE_DECLINE` take
`invitation_id`. An exact known canonical decimal account ID takes precedence
over a different friend's numeric online ID. An offline exact account fails
without redirecting the invitation. If no known account matches, online-name
lookup still applies, including numeric names; hosts selecting an account should
pass its decimal account ID. Nonempty invitation messages and data are not
supported. Cold-launch invitation routing is not supported.

### `OSW_OVERLAY` (9)

The body must be empty. Metadata accepts only `action` and, for `invite`, an
optional decimal-string `session_id`.

| Metadata | Successful result |
| --- | --- |
| `{"action":"status"}` | HTTP 200, JSON body `{"available":bool,"active":bool}` |
| `{"action":"friends"}` | HTTP 204 after dispatching Steam's Friends panel request |
| `{"action":"invite","session_id":"..."}` | HTTP 204 after dispatching Steam's party invitation panel request |

An invitation panel requires a party in the exact title/build/realm namespace,
committed game membership and current provider membership for the local user.
Invisible room directories are refused. With no `session_id`, exactly one
eligible party must exist: none returns `OSW_NOT_FOUND`, several return
`OSW_LIMIT`. Invalid metadata or a nonempty body returns `OSW_INVALID_ARGUMENT`.
An unknown action, the simulated test backend or missing optional overlay
capabilities return `OSW_NOT_SUPPORTED`. When Steam supports the operation but
its overlay is unavailable, `status` succeeds with `available:false` and opening
a panel returns `OSW_UNAVAILABLE`.

HTTP 204 acknowledges dispatch of a Steam call, not a visible panel, a sent
invitation or an acceptance. Cancelling an already dispatched request cannot
undo the Steam UI, and closing that UI creates no invitation success. Join
requests still go through the invitation and namespace checks and the game's
explicit membership commit.

The host must initialize Steam before creating the graphics device. Overlay
availability can be false at first while Steam prepares its renderer hook; the
game must keep rendering. See Valve's
[overlay guide](https://partner.steamgames.com/doc/features/overlay) and
[ISteamFriends reference](https://partner.steamgames.com/doc/api/ISteamFriends#ActivateGameOverlayInviteDialog).

## Events

Consumers needing PSN notification semantics can opt into the separate
[`osw_enable_psn_events` / `osw_poll_psn_event` stream](PSN_EVENTS.md) before
startup. It maps presence/session invalidations and invitation receipt into
copied PSN push records, and preserves accepted invitations as a distinct host
system action. The legacy queue below and its layouts remain unchanged.

`osw_poll_event` returns copied events. The bounded event queue reports
`OSW_RESYNC` after overflow; refresh all snapshots in use. Request results
remain queryable independently of events.

- **Session** (`OSW_SESSION_CHANGED`): `change_kind` 1 for a committed member
  joining, 2 for leaving and 3 for metadata changes. Steam lobby membership alone
  produces no committed-membership event. Hosts translate these events onto
  their own guest callback queues.
- **Friends** (`OSW_FRIENDS_CHANGED`): per-friend `account_id` and `online_id`,
  with `change_kind` 1 for newly compatible and in game, 2 for removed or no
  longer in game, and 3 for a changed online ID. Removal retains the last
  notified name. A separate notification snapshot ensures that an intervening
  friend-list request cannot consume a transition, and an unchanged poll produces
  no duplicate. An event with `account_id=0` is a generic refresh, not a user.
- **Overlay** (`OSW_OVERLAY_CHANGED`): `change_kind` 0 for inactive, 1 for active;
  identity and session fields are empty. Use the `status` action to recover the
  current state. The library does not pause gameplay or disconnect a party when
  the overlay opens or closes.

## P2P extension (ABI 2)

`osw_create_v2(const osw_config_v2*, osw_context**)` enables room discovery and
opaque peer transport. Set `base.size=sizeof(osw_config_v2)` and
`base.abi_version=OSW_ABI_VERSION_2`. Request, result and event layouts are the
same as ABI 1, and no host callback runs on library threads.

| Field | Contract |
| --- | --- |
| `backend` | 0: Steam; 1: the [simulated test backend](#simulated-test-backend). Never a fallback |
| `lab_account_id`, `lab_root` | Test backend only: nonzero synthetic account ID and shared private hub directory |
| `game_bind`, `game_port` | Exact IPv4 loopback address and 1–65535 port of the game's UDP socket |
| `companion_path` | Optional companion executable; launched hidden with inherited private pipes |
| `companion_working_dir` | Working directory chosen by the integrating application |
| `companion_environment` | JSON string-to-string environment map, at most 64 entries and 8191 bytes |

Configuration strings are UTF-8. Obtain the Steam identity from
`osw_get_identity`, not from environment variables. Storage layout and saves
belong to the host and companion; the provider does not choose or partition
their data.

### Local companion token

With a companion, `OSW_TOKEN` requires only `client_id` and `scope`. The ticket
field of the returned `osw1` envelope is a random 32-byte local process
capability that matches the companion's owner handshake exactly. It is never
sent to Steam's ticket validator and must never be accepted by a central Steam
admission service. Without a companion, `OSW_TOKEN` issues the server-bound
Steam ticket described above.

### `OSW_P2P` (8)

Metadata is a command object from the [companion protocol](COMPANION_PROTOCOL.md);
no `v` or correlation `id` is needed through the C API. Successful result
metadata contains `result` directly; failure metadata contains an `error`. Wait
for `{"op":"status"}` to return `ready:true` before reporting the companion
ready. Diagnostic counters describe proxy datagrams without exposing their
contents, and `events.poll` drains bounded diagnostic copies; a companion
receives its events over its own pipe. Game hosts do not relay room operations.

### Virtual endpoints (`virtual-v1`)

A host that translates socket addresses itself calls
`osw_enable_virtual_endpoints(context)` after `osw_create_v2` and before
`osw_start`. It returns `OSW_NOT_SUPPORTED` for a non-P2P context and
`OSW_INVALID_ARGUMENT` after start or stop. Callers that use neither endpoint
mode need none of these exports.

`osw_endpoint` has exactly 24 bytes: `uint32_t size`, `char address[16]` and
`uint32_t port`. Initialize `size=sizeof(osw_endpoint)`. The address is
NUL-terminated canonical dotted IPv4 without leading zeroes; the port is in host
order, 1–65535. There are no borrowed pointers and no release operation.

| Function | Meaning |
| --- | --- |
| `osw_get_local_endpoint(context, output)` | Copy this identity's canonical endpoint once startup and any companion negotiation finish |
| `osw_route_endpoint(context, OSW_ROUTE_OUTBOUND, input, output)` | Copy an authorized canonical peer endpoint's local UDP proxy destination |
| `osw_route_endpoint(context, OSW_ROUTE_INBOUND, input, output)` | Copy a local proxy source's canonical peer endpoint |

These return `OSW_OK` for a copied value, `OSW_PENDING` before readiness,
`OSW_NOT_SUPPORTED` when not enabled, `OSW_NOT_FOUND` for unknown, revoked or
colliding routes, `OSW_UNAVAILABLE` after stop or failure, and
`OSW_INVALID_ARGUMENT` for malformed input. Errors leave the output unchanged;
input and output may alias. Calls take only a short registry mutex, perform no
heap allocation and invoke no provider, network or worker wait. They may race
route updates and stop, but not destruction. A copied route is a snapshot, not
an authorization: the worker rechecks room and friend authorization before
forwarding. Revoked physical sockets are retained without forwarding until
context shutdown, so stale snapshots cannot be reassigned to another peer.

The library derives stable endpoints from the full 64-bit account and the exact
title/build/realm. It computes FNV-1a64 (offset 14695981039346656037, prime
1099511628211) over each of `osw-virtual-endpoint-v1`, title, build and realm,
each prefixed by its one-byte length, then the account's eight little-endian
bytes. With `i = hash mod (131072*64512)`, address ordinal `i/64512` selects
`198.18.0.0/15` and the port is `1024 + i mod 64512`. These are private labels,
not physical interfaces, Steam FakeIP allocations, identity proof or a promise of
uniqueness. The registry detects collisions with self or another registered
peer, revokes affected routes and keeps collision tombstones until context
shutdown. Hosts must not reimplement the scheme or infer authorization from an
address range. At most 1024 active routes or tombstones and 1024 physical proxy
sockets are permitted over a context's lifetime; exhaustion fails closed.

The host keeps its UDP socket at `game_bind:game_port`, exposes the copied self
endpoint where its guest ABI needs a local transport identity, and translates
peer send and receive addresses through these functions. Unknown peer routes
must fail before an OS send, with no fallback to direct routing. Server and
discovery endpoints configured by the host stay physical. The library handles
opaque bytes and parses no title packets or address documents.

The mode is fixed per context. Virtual mode uses platform/party protocol `2`,
room protocol `2`, advertisement protocol `osw/2` and carrier header `OSW2`;
the default mode uses `1`, `osw/1` and `OSW1`. Title/build/realm equality is
always required, and peers in different modes cannot see or join each other. A
configured companion must acknowledge `virtual.endpoints.v1` before readiness.

### Loopback endpoints (`loopback-v1`)

`osw_enable_loopback_endpoints(context)` selects `loopback-v1` after
`osw_create_v2` and before `osw_start`. It is mutually exclusive with
`osw_enable_virtual_endpoints`; selecting either fixes the choice, and repeating
the same selection before start is harmless. A non-P2P context returns
`OSW_NOT_SUPPORTED`; a conflicting selection or a call after start returns
`OSW_INVALID_ARGUMENT`.

In this mode the library binds each authorized remote peer's proxy **exactly**
at that peer's canonical endpoint. `peer.endpoint` and `osw_get_local_endpoint`
return those endpoints, so hosts need no route translation; the route getters
remain available and map a canonical endpoint to itself. Unknown or revoked
peers have no route. The configured game endpoint must be an exact loopback
source outside the reserved range. Incoming Steam data is sent from the remote
peer's canonical socket to the game endpoint, and only UDP from the configured
game address and port is forwarded.

Endpoints use the virtual-v1 construction with two changes: the first string is
`osw-loopback-endpoint-v1`, and the configured AppID's four little-endian bytes
follow the account bytes. With `i = hash mod (4194304*64512)`, ordinal `i/64512`
selects `127.64.0.0/10` and the port is `1024 + i mod 64512`. Hosts and
companions must reserve this range and must not reimplement allocation.
Endpoints are not authentication. Collisions revoke the affected routes for the
context lifetime. Windows sockets use `SO_EXCLUSIVEADDRUSE`. A bind failure,
including an OS-reserved port, returns `endpoint_unavailable` with no route and
**no fallback** to another endpoint or transport.

Revocation closes the loopback socket and discards its queued datagrams; a new
authorization of the same peer may bind the same endpoint again. At most 1024
active sockets, routes or collision tombstones are permitted, and normal leave
and rejoin do not consume a lifetime quota. Each context binds its peers'
endpoints on the local machine, so two contexts on one PC cannot both bind the
same third peer's endpoint: a session with two contexts on one PC is limited to
those two players.

On Windows, a game socket bound to `0.0.0.0` that sends to a `127.64/10` proxy
uses source `127.0.0.1` with its bound port, and replies keep the canonical proxy
source. Configure `game_bind=127.0.0.1` for such a game. Wildcard source matching
is not permitted, and this behavior has only been observed on Windows.

Loopback mode uses platform, party and room protocol `3`, advertisement `osw/3`
and carrier header `OSW3`; it cannot see or join peers in other modes. A
configured companion must acknowledge `loopback.endpoints.v1`. Title, build and
realm are validated as in the other modes.

### Deferred game endpoint

A launcher that cannot know its child's UDP port before launch calls
`osw_defer_game_endpoint(context)` after `osw_enable_loopback_endpoints` and
before `osw_start`. It requires a configured companion and loopback mode,
otherwise it returns `OSW_NOT_SUPPORTED`; calls after start or stop return
`OSW_INVALID_ARGUMENT`. The endpoint is registered explicitly; it is never
learned from the first packet.

The configured physical endpoint satisfies creation validation but is never
used for transport in this mode, and the owner descriptor sent to the companion
contains no physical endpoint. The companion must acknowledge `game.endpoint.v1`
as well as `loopback.endpoints.v1`. P2P `status.ready` means the companion is
ready to serve identity and server requests, while `status.game_endpoint_pending`
stays true and `osw_get_local_endpoint` and route lookups return `OSW_PENDING`.

After observing its own child's UDP socket, the host submits:

```json
{"op":"game.endpoint.bind","address":"127.0.0.1","port":3104}
```

This is an `OSW_P2P` request with exactly these three metadata fields and an
empty body. Only a local C API request may bind; a companion request cannot. The
address is a canonical IPv4 loopback literal outside `127.64.0.0/10` and the port
an integer 1–65535. The host establishes socket and process ownership; the
library accepts no PID, inspects no process and parses no game packets. At most
one binding may be in flight, and one may complete per context.

The request completes with `OSW_OK` only after the companion has applied the exact
source and returned a correlated acknowledgement; result metadata is
`{address,port}`. The source filter and incoming delivery destination then change
together on the library worker and `game_endpoint_pending` becomes false.
Canonical endpoints, transport protocol, identity, AppID and namespace never
change. Further binding requests fail, even for the same endpoint.

While pending, room, peer and advertisement operations fail with
`game_endpoint_pending`, automatic room following is disabled, no peer transport
is authorized and incoming carrier messages are discarded. Identity, token,
friend and name reads and WebAPI GETs remain available; WebAPI mutations,
invitation actions and other platform actions return `OSW_UNAVAILABLE` until
registration. The companion must not answer discovery before binding and must
answer only the registered source afterwards.

The acknowledgement must arrive within five seconds and within the request
deadline. A missing, late, mismatched or duplicate acknowledgement breaks the
context. Cancellation after dispatch also breaks it, because the companion may
already have applied the binding. A host that loses or changes the bound socket
must close its session rather than change an established endpoint.

## Simulated test backend

`backend = 1` selects a local simulation for tests. It is never Steam
authentication and never a fallback for an unavailable Steam client. A hub
directory (`lab_root`) holds an exclusively locked, bounded JSON state file for
directory and control events; peer datagrams use loopback UDP. It simulates
friends among active clients sharing the hub, checks peer identity against each
registered UDP endpoint and removes abandoned registrations when their process
exits. Callers may provide an explicit UTF-8 `base.online_id`, which is also the
persona name. A non-default AppID partitions the hub. The overlay operation is
not supported.

Storage failures are reported as `osw_identity.reason_code = OSW_UNAVAILABLE`
with a fixed reason such as `Lab hub read sharing timeout`, `Lab active hub missing`
or `Lab hub state invalid`; reasons never contain file paths, account data or
exception text. A temporary Windows read-sharing conflict is retried for about
500 ms before any state change; permanent errors fail immediately. Failed reads
never initialize or save replacement state.

The simulated carrier reserves 20 bytes for its own headers, so it accepts game
datagrams of at most 65,487 bytes. This limit is specific to the simulation.
