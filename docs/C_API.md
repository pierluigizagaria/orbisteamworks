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
Nonempty invitation messages/data are explicitly unsupported in this PoC.

Events carry copied IDs and names. Session `change_kind` is 1 for a committed
member joining, 2 for leaving, and 3 for metadata changes. Steam membership
alone produces no committed membership event. Hosts translate these events
onto their existing guest callback queues.

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
