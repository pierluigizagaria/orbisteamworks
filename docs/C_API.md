# C API version 1

The public header is `include/orbisteamworks/osw.h`. The caller initializes each
structure's `size` and configures ABI version 1. Configuration contains an explicit
runtime-library path, configured online ID, title, build and realm. Only one
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
configured online IDs and presence. Only Steam friends with a known project
online ID are exposed. Known names survive a friend's loss of presence for
the current context; they are not persisted across process restarts.
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
