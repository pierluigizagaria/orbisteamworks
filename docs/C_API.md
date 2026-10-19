# C API version 1

The public header is `include/orbisteamworks/osw.h`. The caller initializes each
structure's `size` and configures ABI version 1. Configuration contains an explicit
runtime-library path, optional online ID (empty: derived from the Steam persona), title, build
and realm. Only one
production context may exist in a process. Steam owns one signed-in account.

`osw_start` starts the library thread; callers must query identity to learn whether
initialization succeeded. Steam symbols and callback dispatch run only on that
thread. The library never invokes host or guest callbacks. Missing dependencies
produce signed-out identity with a reason, rather than fabricated success.

The opt-in Lab backend reports terminal storage failures through the same
`osw_identity.reason_code = OSW_UNAVAILABLE` and a fixed, non-sensitive reason
category (for example, `Lab hub read sharing timeout`, `Lab active hub missing`,
or `Lab hub state invalid`). These messages never contain a file path, account
data or arbitrary exception text. A failed context cannot restart. A temporary
Windows read-sharing conflict is retried before any state mutation for at most
approximately 500 ms; permanent errors fail immediately. Failed reads never
initialize or save replacement state, and active operations require the hub to
exist. These rules add no session lifetime or idle timeout.

`osw_submit` copies metadata and binary data. Requests have monotonically increasing
IDs, a 1–60000 ms timeout, at most 64 KiB of JSON metadata and 1 MiB of body data.
There are at most 128 retained requests and separate 16 MiB input/output budgets.
`osw_get_result` returns PENDING until completion, then an operation status and
borrowed response buffers. Those buffers remain valid until `osw_release` or
context destruction, including across `osw_stop`. Release does not cancel a
successfully issued credential. A new token replaces the prior issued ticket.

Cancellation completes pending requests with ABORTED and ignores late replies.
Timeouts produce TIMEOUT. Stop joins the library thread and aborts pending work;
an already stopped context cannot restart. Destroy must not race any other API
call. The caller must serialize release/destruction against reads of borrowed data.
The bounded event queue reports RESYNC after overflow; request results remain
queryable independently of events.

TOKEN metadata contains string fields `client_id`, `scope` and decimal `server_id`.
Supported scopes are `openid`, `openid openid:duid`, and `openid,openid:duid`.
The body of a successful response is the opaque `osw1` credential, without a NUL
terminator. The host obtains the helper identity from its configured trusted
publisher endpoint. This replacement contract does not assert Sony OAuth behavior.

`WEBAPI` metadata contains `method`, `path`, optional `api_group`,
`content_type` and `headers`. The body is opaque, including multipart data.
The result owns its HTTP status, metadata (`content_type`) and response body.
Platform HTTP errors have an HTTP status and JSON error body; library failures
use a negative operation status. Unsupported routes return HTTP 501.

Expected validation and Steam runtime failures use explicit return values
internally as well: a host's Windows vectored handler can observe an exception
even when the C boundary catches it. Windows offline regressions verify no
first-chance C++ exception for malformed paths/parts and identifiers, missing
libraries/symbols, failed client/presence initialization, invalid native counts,
ticket failures and incompatible callback payloads. A fatal backend failure
completes pending work and stops the library thread; completed buffers remain
owned until release. A ticket-issuance failure leaves retry available. This is
coverage of the tested failure contracts, not a guarantee against every possible
internal exception. See the work log for build and game-acceptance status.

`FRIENDS` returns a `friendList` snapshot with decimal-string account IDs,
online IDs, `presence.onlineStatus` and `personalDetail.displayName` (the
cleaned persona, at most 32 bytes; CTR ignores it). Only Steam friends
currently running the project for the same title, build and realm are listed.
Names of friends who leave stay cached for name resolution and invitations in
the current context, are not listed, and are not persisted across restarts.
Online IDs are 1–16 bytes of UTF-8 without control characters or surrounding
spaces.
`RESOLVE_NAME` takes `online_id` and resolves the local user or known friends.
`INVITE_SEND` takes `session_id` and a `to` array of known online IDs or decimal
account IDs. `INVITE_ACCEPT` and `INVITE_DECLINE` take `invitation_id`.
An exact known canonical decimal account ID takes precedence over a different
friend's numeric online ID. An offline exact account fails without redirecting
the invitation. If no known account matches, the existing online-name lookup
still applies, including numeric names. Hosts selecting an account should pass
its decimal account ID.
Nonempty invitation messages/data are explicitly unsupported in this PoC.

Events carry copied IDs and names. Session `change_kind` is 1 for a committed
member joining, 2 for leaving, and 3 for metadata changes. Steam membership
alone produces no committed membership event. Hosts translate these events
onto their existing guest callback queues.

`OSW_FRIENDS_CHANGED` also carries per-friend copied `account_id` and `online_id`:
`change_kind` 1 means newly compatible/in game, 2 removed or no longer in game,
and 3 a changed online-ID label. Removal retains the last notified label.
Only friends matching the existing title/build/realm rules produce these
details. A separate notification snapshot ensures that an intervening friend
list GET cannot consume the transition. An unchanged ordinary poll produces no
duplicate detail event. The existing generic refresh event (`account_id=0`)
is retained for provider friend callbacks; hosts must not treat it as a user.
The public structure layout and enum values are unchanged.

Supported route families currently include session creation, metadata,
members, immutable/changeable data, images, invitations and the local user's
session, invitation and friend lists. The local platform entitlement catalog
is explicitly empty: this grants no PSN purchase or publisher progression.
Unknown routes fail rather than forwarding to Sony or shadNet. Pagination,
remote user session enumeration and the broader PS4 API are not yet qualified.

Session metadata uses one document capped at 6,000 bytes, plus namespace
keys. The live Steam client refused the old multi-key image representation
after approximately 8 KB of aggregate metadata. Large immutable payloads
therefore remain in a bounded cache and are fetched from the current lobby
owner through private lobby messages, in blocks below the native 4 KB limit.
The receiver validates namespace, sender, transfer ID, offset, total size and
checksum. The checksum detects inconsistent copies, not cryptographic trust;
the authenticated Steam sender and current owner determine the source.
Transfers have request deadlines and a 16 MiB cache budget. These deadlines
do not disconnect committed game parties. A joining client obtains all large
immutable payloads before acceptance completes; game membership still waits
for its own POST. Race UDP is unaffected.

This remains ABI version 1 in an unreleased PoC. Rebuild host and library
together while the layout is being qualified. Current evidence and remaining
limits are recorded in INTEGRATION_WORKLOG.md.

The build uses nlohmann/json, supplied by the host CMake target or installed headers.
Standalone builds may set `OSW_JSON_INCLUDE_DIR`. No Steamworks SDK headers or
libraries are required at compile time. `OSW_BUILD_TOOLS` and `OSW_BUILD_TESTS`
default off when the library is included with `add_subdirectory`.
`OSW_BUILD_SHARED` (default off) also builds `orbisteamworks.dll` or
`liborbisteamworks.so`, which exports only the functions above for hosts that
load the library at runtime; see PLUGIN_DESIGN.md.

## Native provider overlay (additive operation, 2026-10-16)

`OSW_OVERLAY` (9) uses the existing copied request/result model in ABI 1 and
ABI 2, without changing any structure layout. Its body must be empty. Metadata
accepts only `action` and, for `invite`, an optional decimal-string `session_id`.

| Metadata | Successful result |
| --- | --- |
| `{"action":"status"}` | HTTP 200, JSON body `{"available":bool,"active":bool}` |
| `{"action":"friends"}` | HTTP 204 after dispatching the native Friends panel request |
| `{"action":"invite","session_id":"..."}` | HTTP 204 after dispatching the native party invitation panel request |

An invitation panel requires a party in the exact title/build/realm namespace,
committed game membership and current provider membership for the local user.
Invisible room directories are refused. With no `session_id`, exactly one
eligible party must exist: none returns `OSW_NOT_FOUND`, several return
`OSW_LIMIT`. Invalid metadata or a nonempty body returns `OSW_INVALID_ARGUMENT`.
An unknown action, the lab backend or missing optional overlay capabilities
returns `OSW_NOT_SUPPORTED`. When Steam supports the operation but its overlay
is unavailable, `status` succeeds with `available:false`; opening a panel
returns `OSW_UNAVAILABLE`.

HTTP 204 acknowledges dispatch of a provider call, not a visible panel, a sent
invitation or acceptance. Cancelling an already dispatched request cannot undo
the native UI. Closing that UI creates no invitation success. Join requests
continue through the existing invitation/namespace checks and the game's
explicit membership commit.

`OSW_OVERLAY_CHANGED` (8) is a copied event with `change_kind` 0 for inactive or
1 for active; its identity/session fields are empty. Hosts can recover current
state with `status`. The library does not pause gameplay or disconnect a party
when the overlay opens or closes. Steam calls and callback handling stay on the
library worker.

The host must initialize Steam before creating the graphics device. Overlay
availability can initially be false while Steam prepares its renderer hook;
the game must continue rendering. These requirements and the native invite
panel are documented in Valve's [overlay guide](https://partner.steamgames.com/doc/features/overlay)
and [ISteamFriends reference](https://partner.steamgames.com/doc/api/ISteamFriends#ActivateGameOverlayInviteDialog).

## Native P2P opt-in (ABI extension 2, 2026-10-16)

`osw_config`, `osw_create` and all request/result/event layouts remain ABI 1.
The additional export `osw_create_v2(const osw_config_v2*, osw_context**)`
accepts the extension from `osw.h`; set `base.size=sizeof(osw_config_v2)` and
`base.abi_version=OSW_ABI_VERSION_2`. No host callback runs on library threads.

| v2 field | Contract |
| --- | --- |
| `backend` | 0: Steam; 1: explicitly simulated local lab; no fallback |
| `lab_account_id`, `lab_root` | Required only for lab; nonzero synthetic ID and shared private hub directory |
| `game_bind`, `game_port` | Exact IPv4 loopback address and 1..65535 port of the game's UDP socket |
| `companion_path` | Optional configured publisher executable; launched hidden with inherited private pipes |
| `companion_working_dir` | Private per-player/per-economy working directory |
| `companion_environment` | JSON string-to-string environment map, <=64 entries and <=8191 bytes |

Configuration strings are UTF-8; obtain Steam identity from `osw_get_identity`,
not Windows environment variables. Lab callers may provide an explicit UTF-8
`base.online_id`. Keep normal/unlock-all save roots and child databases separate.

With a companion, `OSW_TOKEN` requires only `client_id` and `scope`. Its osw1
ticket field is a random 32-byte local process capability, matching the owner
pipe handshake exactly. It is never sent to Steam's ticket validator and must
never be accepted by the central Steam admission provider. Without a companion,
Steam token issuance retains the existing server-bound ticket behavior.

`OSW_P2P` (8) uses the normal copied request/result model. Metadata is a command
object from [COMPANION_PROTOCOL.md](COMPANION_PROTOCOL.md); no `v` or correlation
`id` is needed through C API. Successful result metadata contains `result`
directly; failure metadata contains an `error`. The host should wait for
`{op:"status"}` to return `ready:true` before reporting its publisher ready.
Diagnostic counters describe proxy datagrams without exposing their contents.
`events.poll` drains bounded diagnostic copies; the publisher receives events
over its private pipe automatically. Game hosts do not relay room operations.

The lab hub uses an exclusively locked, bounded local JSON state file for
directory/control events and loopback UDP for race traffic. It simulates friends
among active clients sharing that hub. Peer identity is checked against the
registered lab UDP endpoint. This is a test facility, never Steam authentication.
Process liveness removes abandoned lab registrations; no menu-idle expiry exists.
The Windows sandbox can silently drop loopback UDP: run explicitly authorized
network acceptance tests outside that sandbox. Fixture results are not live gates.

## Optional canonical UDP endpoints (additive exports, 2026-10-16)

This host-independent transport extension is available to any emulator or native
host. ABI 1/2 configuration and request/result/event layouts are unchanged. A
caller that chooses this mode resolves all three new exports and calls
`osw_enable_virtual_endpoints(context)` after `osw_create_v2` and before
`osw_start`. It returns `OSW_NOT_SUPPORTED` for a non-P2P context and
`OSW_INVALID_ARGUMENT` after startup/stop. Legacy callers need no new exports.

`osw_endpoint` has exactly 24 bytes: `uint32_t size`, `char address[16]`, and
`uint32_t port`. Initialize `size=sizeof(osw_endpoint)`. The address is bounded,
NUL-terminated canonical dotted IPv4 with no leading zeroes; port is host-order
1..65535. There are no borrowed pointers or release operation.

| Function | Meaning |
| --- | --- |
| `osw_get_local_endpoint(context, output)` | Copy this identity's canonical endpoint once startup and any companion negotiation finish |
| `osw_route_endpoint(context, OSW_ROUTE_OUTBOUND, input, output)` | Copy an authorized canonical peer endpoint's local UDP proxy destination |
| `osw_route_endpoint(context, OSW_ROUTE_INBOUND, input, output)` | Copy a local proxy source's canonical peer endpoint |

Getters return `OSW_OK` for a copied value, `OSW_PENDING` before readiness,
`OSW_NOT_SUPPORTED` when not enabled, `OSW_NOT_FOUND` for unknown/revoked/colliding
routes, `OSW_UNAVAILABLE` after stop/failure, or `OSW_INVALID_ARGUMENT` for
malformed input. Errors leave the caller's output unchanged. Input and output
may alias. Calls take only a short registry mutex, perform no heap allocation,
and invoke no provider/network API or worker wait. They may race route updates
and stop, but must not race destruction. A copied route is a snapshot, not a new
authorization: the worker rechecks its existing room/friend authorization before
forwarding. Revoked physical sockets are retained without forwarding until
context shutdown, so stale snapshots cannot be reassigned to another peer.

The library derives stable labels from the full 64-bit account and exact
title/build/realm. It uses FNV-1a64 (offset 14695981039346656037, prime
1099511628211), over each of `osw-virtual-endpoint-v1`, title, build and realm
prefixed by its one-byte byte length, then the account's eight little-endian
bytes. Let `i=hash mod (131072*64512)`: address ordinal `i/64512` selects
`198.18.0.0/15`, and port is `1024+i mod 64512`. These are private virtual labels,
not physical interfaces, Steam FakeIP allocations, identity proof or a promise
of mathematical uniqueness. The registry detects a collision with self or
another registered peer, revokes affected peer routes and keeps collision
tombstones until context shutdown. The identity-to-endpoint scheme and range
are library policy; hosts do not duplicate the algorithm or infer authorization
from an address range. At most 1024 active routes/tombstones and 1024 physical
proxy sockets over a context lifetime are permitted; exhaustion fails closed.
No timer recycles a revoked port or clears a collision tombstone.

The host keeps its actual UDP socket at configured `game_bind:game_port`, exposes
the copied self endpoint where its guest ABI needs a local transport identity,
and translates peer send/receive addresses through these functions. Unknown
peer routes must fail before a host OS send; do not fall back to direct routing.
Publisher/discovery endpoints configured by that host stay physical. Only the
host knows its guest structures and OS calls. The library handles opaque bytes;
it contains no title packet parser or address-document rewrite rules.

Mode is immutable per context. Virtual mode uses platform/party protocol `2`,
room protocol `2`, advertisement protocol `osw/2`, and carrier header `OSW2`;
legacy retains `1`, `osw/1`, and `OSW1`. Title/build/realm equality is still
mandatory. Mixed-mode friends, invitations, party documents, rooms and peer
frames are incompatible. A configured companion must explicitly acknowledge
`virtual.endpoints.v1` before readiness; see [COMPANION_PROTOCOL.md](COMPANION_PROTOCOL.md).


## Bridge endpoint mode and AppID (additive exports, 2026-10-19)

`osw_enable_loopback_endpoints(context)` selects `loopback-v1` after
`osw_create_v2` and before `osw_start`. It is mutually exclusive with
`osw_enable_virtual_endpoints`; selecting either freezes that choice even before
start. Repeating the same selection before start is harmless. A non-P2P context
returns `OSW_NOT_SUPPORTED`; an incompatible selection or post-start call returns
`OSW_INVALID_ARGUMENT`. The ABI 1/2 structures remain byte-for-byte unchanged.

In this mode the library binds each authorized remote peer's proxy **exactly** at
that peer's canonical endpoint. `peer.endpoint` and `osw_get_local_endpoint`
return these same stable endpoints; hosts require no route translation. Existing
route getters remain available for inspection, mapping a canonical endpoint to
itself. Unknown/revoked peers have no route. The configured game endpoint must be
an exact loopback source outside the reserved canonical range. Incoming Steam
bytes are emitted from the remote peer's canonical socket to that game endpoint;
only UDP from the configured game source address and port is forwarded.

Canonical derivation is the virtual-v1 FNV-1a64 construction above with these
changes: first string `osw-loopback-endpoint-v1`; after the account's eight bytes,
append the configured AppID's four little-endian bytes. Let
`i=hash mod (4194304*64512)`; ordinal `i/64512` selects `127.64.0.0/10`, and port
is `1024+i mod 64512`. Hosts/publishers must reserve this subrange and must not
reimplement allocation. Endpoint labels are not authentication. Registry
collisions with self/other peers revoke the affected routes and remain
quarantined for the context lifetime. Windows sockets use `SO_EXCLUSIVEADDRUSE`.
Bind failure, including an OS-reserved port, returns `endpoint_unavailable` with
no route and **no fallback** to a different endpoint or transport.

Revocation closes the loopback socket and discards its queued datagrams. A fresh
peer authorization may rebind the same canonical identity; this differs from
virtual-v1's retained physical sockets. There are at most 1024 active loopback
sockets/routes or collision tombstones. Normal leave/rejoin does not consume a
lifetime socket quota. Two players per PC is the supported Lab maximum: a third
process needing an endpoint already bound in another bridge cannot bind it.

Windows measurement (S): a UDP game socket bound to `0.0.0.0` sends to a
`127.64/10` proxy from `127.0.0.1` with its bound port; the reply retains the
canonical proxy source. Configure `game_bind=127.0.0.1` for that wildcard host.
This does not permit wildcard source matching or prove Linux/macOS behavior.

Loopback mode uses platform/party/room protocol `3`, advertisement `osw/3`, and
carrier header `OSW3`. It cannot discover or join legacy/virtual peers, parties,
or rooms. The companion must acknowledge `loopback.endpoints.v1`. Namespace
validation remains mandatory, including title/build/realm; economy is never a
namespace field.

`osw_set_app_id(context, uint32_t app_id)` selects a nonzero AppID before start
(default 480), for both ABI 1 and ABI 2. Calls after start/stop fail with
`OSW_INVALID_ARGUMENT`. The configured value reaches Steam initialization,
product identification and app/game callback filters. Every player must use the
same AppID; account licensing and initialization errors remain Steam's decision.
The central standalone authentication helper is unchanged and still uses 480.
The Lab carrier partitions its synthetic hub by non-default AppID, mirroring
Steam's application isolation. No runtime API distributes Steamworks files.

`osw_get_persona_name(context, char *buffer, size_t capacity, size_t *required)`
copies the raw provider persona captured at startup, before online-ID cleaning,
trimming or truncation. `required` includes the terminating NUL. A null buffer
with zero capacity queries that size and returns `OSW_LIMIT`; short buffers are
unchanged and also return `OSW_LIMIT`. Signed-out contexts return
`OSW_SIGNED_OUT` without changing outputs. The getter only takes the identity
mutex and makes no Steam/network call. The Lab persona is its configured name.
An explicit online ID does not alter the raw persona. A launcher may bootstrap
without a companion, read the raw persona, destroy that context, then create a
fresh context with its own explicit online ID policy. Only one production
context may exist at once; the override is fixed for that context.

## Deferred game endpoint registration (additive, 2026-10-19)

A launcher that cannot know its child's bound UDP port before launch calls
`osw_defer_game_endpoint(context)` after `osw_enable_loopback_endpoints` and
before `osw_start`. This requires a configured companion and loopback mode;
otherwise it returns `OSW_NOT_SUPPORTED`. Calls after start/stop return
`OSW_INVALID_ARGUMENT`. ABI 1/2 layouts and the other endpoint modes are unchanged.
This is an explicit opt-in contract (I), not first-packet learning.

The original config's physical endpoint satisfies creation validation but is
never used for transport in deferred mode. The owner descriptor contains no
physical endpoint. The companion must acknowledge `game.endpoint.v1` as well
as `loopback.endpoints.v1`. P2P `status.ready` means the companion is ready to
serve identity/publisher requests; `status.game_endpoint_pending` remains true.
`osw_get_local_endpoint` and route lookups remain `OSW_PENDING`.

After observing its own child's actual UDP socket, the host submits:

```json
{"op":"game.endpoint.bind","address":"127.0.0.1","port":3104}
```

This is `OSW_P2P`, with exactly those three metadata fields and an empty body.
Only a local C API request may bind; a companion request cannot. Address is a
canonical IPv4 loopback literal outside `127.64.0.0/10`, and port is an integer
1..65535. The host is responsible for establishing socket/process ownership;
the library accepts no PID, inspects no host process and parses no game packet.
At most one binding may be in flight and one may complete per context.

The request completes `OSW_OK` only after the child has applied the exact source
and returned a correlated acknowledgement. Result metadata is `{address,port}`.
The source filter and incoming delivery destination then change together on
the provider worker. `game_endpoint_pending` becomes false. Canonical endpoints,
transport protocol, identity, AppID and namespace never change. Further binding
requests fail, even for the same endpoint; there is no hot rebind or restart.

While pending, room, peer and advertisement operations fail with
`game_endpoint_pending`, automatic room following is disabled, no peer transport
is authorized, and incoming carrier messages are discarded. Identity, token,
friend/name reads and WebAPI GETs remain available. WebAPI mutations, invitation
actions and other platform actions return `OSW_UNAVAILABLE` until registration.
The deferred companion must withhold discovery replies before binding and
must answer only the exact registered physical source afterwards.

Acknowledgement has a five-second maximum, additionally bounded by the caller's
request deadline. Missing, late, mismatched or duplicate acknowledgements break
the context. Cancellation after dispatch also breaks it: the child may already
have committed, so rollback would invent agreement. No route/source fallback is
allowed. A host that loses or changes the bound socket must close its supervised
session rather than silently change an established endpoint.
