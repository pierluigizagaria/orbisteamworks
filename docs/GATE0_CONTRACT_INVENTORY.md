# Gate 0: platform integration inventory

Date: 2026-10-15. Status: in progress, not an acceptance claim. This inventory
continues after the successful gate 1 authentication spike. It distinguishes
observed CTR calls, source behavior in the existing services, and proposed
replacement behavior. The user subsequently authorized the full shadPS4 and
publisher integration. Initial host and publisher changes now exist locally;
the inspected baseline table below describes the state before those edits.

## Inspected baselines

| Component | Inspected state | Consequence |
| --- | --- | --- |
| orbiSteamworks | Uncommitted gate 1 source; final identities in GATE1_AUTH_SPIKE.md | Reuse runtime loading and callback dispatch; do not rewrite the successful spike |
| shadPS4 | `0fdad68`, `feat/resolver-host-overrides`, no top-level working-tree changes | Checked-out NPAuth still returns a placeholder; accepted NPAuth work is on another branch |
| Demonware Server | `c09908a`, `cleanup/code-quality`, clean working tree | Contains 22 commits after local main; preserve this work when selecting the integration base |
| shadNet | Existing source is a behavioral reference only | Do not copy its source or mix its credentials with the Steam provider |

The first shadPS4 status command could not traverse one submodule's metadata
under sandbox permissions. A subsequent `--ignore-submodules=all` check showed
no top-level changes. This does not qualify submodule cleanliness.

## Host entry points (U-derived)

| Boundary | Existing implementation | Required Steam behavior (I) |
| --- | --- | --- |
| Lifecycle and sign-in | `NpHandler::Initialize`, `ConnectUserById`, `Shutdown`, user login/logout | Select the provider before any shadNet probe or credential check; one Steam account per host |
| Identity | `IsPsnSignedIn`, `GetNpId`, `GetOnlineId`, `GetAccountId`, reverse local-user lookups | Account ID from Steam; configured online ID; lossless uint64; no synthetic signed-in state |
| Guest WebAPI send | `np_web_api_internal.cpp` currently checks the shadNet-enabled setting, creates HTTP templates/connections/requests and attaches its bearer | Dispatch supported requests to the in-process library while retaining guest request state, result and ownership rules |
| Guest WebAPI response | Existing request-owned status/header/read/delete paths | Store and consume copied response data; preserve incremental reads, abort, errors and cleanup |
| NPAuth | Checked-out `GetIdToken` returns `TOKEN`; accepted branch `1629839` has recovered asynchronous ownership | Route Steam token acquisition through the same guest-visible request lifetime, without leaking a pointer to a released payload |
| Friend snapshots | `GetNumFriends`, `GetFriendNpid`, `GetFriendList`, `ResolveOnlineId` | Filter Steam friends by project/title/build/realm and known configured online ID |
| Send invitation | `SendSessionInvitation` directly creates a shadNet multipart HTTP request | Provider dispatch must cover internal host calls as well as guest WebAPI calls |
| Receive and accept | `PendingInvitation`, `AcceptSessionInvitation`, `PostSessionInvitationEvent` | Validate lobby namespace, accept idempotently, then deliver through the existing SystemService queue |
| Unsupported platform features | Score, TUS, trophies and Matching2 methods coexist in NpHandler | Return explicit unsupported/unavailable results; do not let calls fall through to shadNet |

The host requires a narrow provider seam, not merely replacement of the
`GetBearerToken` return value. Existing direct setting checks and invitation
HTTP calls otherwise bypass the provider.

## Requests and payload ownership

The existing shadNet implementation handles the following families. This list
is source evidence of the replacement server, **not proof that CTR calls every
route** or of Sony's contract:

- `GET /v1/users/{account}/friendList`: historical original CTR request is
  recorded in `../docs/OFFICIAL_SHADNET_FRIENDS_TEST.md`; identity nesting under
  `user` is material to the game's reader.
- `POST /v1/sessions`: multipart parts `session-request`, `session-image`,
  `session-data`, `changeable-session-data`; returns a session identifier.
- `GET/PUT/DELETE /v1/sessions/{session}`: metadata and lifecycle.
- `GET/POST /v1/sessions/{session}/members` and
  `DELETE /v1/sessions/{session}/members/{member}`: actual platform membership.
- `GET /v1/sessions/{session}/sessionData` and
  `GET/PUT /v1/sessions/{session}/changeableSessionData`: opaque binary data.
- User/friend session enumeration and session invitation create/read/consume
  routes: required coverage must be narrowed against CTR captures.

An allowlisted extraction of five retained bridge logs now pins the following
historical original-client route/status observations (C). Counts describe log
records across those files, not unique gameplay attempts:

| Method and route | Observed status and count |
| --- | --- |
| `GET /v1/users/{user}/friendList` | 200: 36 |
| `POST /v1/sessions` | 200: 37 |
| `GET /v1/sessions/{session}` | 200: 6 |
| `PUT /v1/sessions/{session}` | 204: 22 |
| `POST /v1/sessions/{session}/members` | 204: 3 |
| `DELETE /v1/sessions/{session}/members/me` | 204: 53 |
| `POST /v1/sessions/{session}/invitations` | 204: 7; 400: 9 |
| `GET /v1/users/me/entitlements` | 404: 4 |

Reproducer: `out/gate0-bridge-routes.py`; private result:
`runtime/inventory/bridge-routes.json`. Every observation retains its source
line and file hash, with identities replaced by route placeholders. No body,
query string, name or credential is exported. The bridge also logged a `bytes`
field; the result preserves it as `logged_bytes`, but its direction and
pre/post-transformation meaning have not been established from retained emitter
source. It is therefore not asserted as a measured original response size.
These records strengthen historical invitation/membership coverage; they do
not establish current Steam acceptance, a complete grouped-search sequence or
Sony service behavior.

The historical local capture now pins one CTR create request at 53,161 bytes:
417 bytes of `session-request` JSON, a 52,096-byte image, 5 bytes of immutable
data and 10 bytes of changeable data. Seven creates in the retained
`docs/evidence/local-runs/server-20261008T132928Z/stderr.log` have these sizes
and report accepted type `owner-bind`. These are captured client requests (C)
observed through the local server. The inspected shadNet source at
`aa49e602ff924e0f06aa147e682c473e032da683` reads `sessionType` from the JSON
without a type default and logs that parsed value (U-derived). This supports
`owner-bind` for that captured path, not every possible CTR session.
The capture SHA-256 is
`6dedea5a2809b9e967ee4735c60cea0df7a6e481c304e877f7d1b725df461817`.
The private allowlisted extraction is `runtime/inventory/session-contract.json`,
reproduced by `out/gate0-session-contract.py`. It exports no request bodies,
session identifiers or account names.

The 2026-10-15 Online reentry investigation resolved a framing discrepancy (C).
The owned Toolkit SHA-256 is
`480a769db6697a8aa3b17019f0efe60095b85f05f8bfc67f13ed3efb675e5c9c`.
At module-relative `0x5c0c7`–`0x5c0f3`, its session builder saves the length
after the last binary part, appends the 38-byte closing delimiter, then passes
the saved length to the WebAPI wrapper. The submitted body therefore ends
exactly at the last part's declared Content-Length, without a closing delimiter
or trailing CRLF. Reconstructing only the framing with synthetic payloads gives
53,161 bytes before that delimiter, matching all seven historical captures.
The wrapper supplies the multipart Content-Type through the content parameter.
Private reproducible checks: `out/gate0-multipart-contract.py` and
`runtime/inventory/multipart-20261015/caller-evidence.json`. No original bytes
or request bodies are exported.

The core parser now accepts EOF only at the end of a complete explicitly
length-delimited part. It still rejects truncated data, extra trailing bytes,
partial delimiters, duplicate part names and unbounded final parts. A synthetic
four-part fixture reproduces the original sizes and Content-Description naming;
it failed with `Invalid multipart request` before the correction and passes
afterward. This compatibility rule belongs to the library, not a game-specific
host patch. Offline success does not qualify the repaired game flow.

Current shadNet caps are 160 KiB for images, 1 MiB for immutable session data,
1 KiB for changeable data and 1 MiB for invitation data. They are **not measured
CTR payload sizes or established firmware limits**. Steam lobby metadata must
not silently truncate data to its smaller per-value capacity. Before choosing
chunking or another representation, recover actual CTR payload sizes and
write ownership from the retained captures.

The source accepts `owner-bind` and `owner-migration`. It treats update
permissions differently and allows ordinary membership requests to commit
membership. Steam automatically joining a lobby does not complete that guest
operation. The Steam implementation supports both types; its real single-account
checks do not replace the original game's platform-session acceptance.

## Original CTR NPAuth parameters (C)

The owned CUSA13795 01.21 executable is pinned at SHA-256
`e3b7a068b5e7be0f976102aa223a74425e7817c67e26e21416163291750c1b83`.
Its call at module-relative `0x174f22` invokes `sceNpAuthGetIdTokenA` after
creating an async request at `0x174e88`. The caller builds its 36-byte UUID-shaped
client ID by XORing the bytes at `0x1bf1f50` with those at `0x1bf1f80`.
The decoded client ID has SHA-256
`e1af64e0144d044f5e3237190900eca82e6a9cb1791f3075eb4bb3f1086317e0`.
It is identified reproducibly without adding its value or the separate client
secret to the repository. The scope is exactly `openid,openid:duid`.

Private reproducer: `out/gate0-client-contract.py`; result:
`runtime/auth-spike/gate0-client-contract/result.json`. The reproducer checks
the owned image hash and selected caller instructions before decoding only the
client ID. It neither decodes the client secret nor saves original bytes.
This establishes the original caller parameters, not Sony endpoint or issuer
rules. The Steam credential remains the explicit replacement described in C_API.md.

## Admission boundary

The existing publisher `PSNPlatform.Authenticate` reads the original PS4
envelope, verifies through its injected verifier, then enforces the shadNet
decimal subject and emits a shadNet player identity. A Steam provider must reuse
the envelope reader while owning its own identity construction; replacing only
the verifier would retain the wrong subject/type contract.

The current verifier interface returns identity but no per-connection auth
session handle. The native Steam validator is session-owning and needs
`EndAuthSession` when the admitted connection ends. Helper request ownership,
publisher-ticket handoff, reconnect overlap and failure cleanup must be explicit
before code is integrated. Do not terminate established players because Steam
later emits a cancellation/logoff callback.

Gate 1 also disproved the assumption of an inherent 120-second Steam ticket
lifetime. The intended freshness rule requires a separate decision and must
not be represented as implemented by a request timeout or client timestamp.

## Gate 0 completion audit

1. **Identified:** original CTR NPAuth client ID and scope, with the hash and
   bounded reproducer above. No client secret is published.
2. **Partial:** historical friend GET and session creation/read/membership
   observations, create sizes and accepted `owner-bind` type are established.
   A complete route/response-size inventory across invitation, reentry and
   grouped search is still needed. Broader server support alone is not proof
   of an original CTR dependency.
3. **Unproven:** the retained named Toolkit import inventory has UserProfile,
   Auth, Core, Friend, Session and Commerce candidates, and no named Score,
   TUS or Matching2 candidates. Imports alone cannot exclude indirect calls.
   The full supported online flow still requires game acceptance.
4. **Specified and implemented:** [C_API.md](C_API.md) defines copied inputs,
   retained outputs, cancellation, shutdown, event consumption and request
   bounds. The current 27 offline suites exercise those contracts and expected
   runtime failure propagation; build identities are in the work log.
5. **Selected:** host `0fdad68` and publisher `c09908a`, preserving their local
   histories. The opt-in host build leaves the checked-out default shadNet
   path intact. Both modes compile. This base's placeholder NPAuth differs
   from the previously accepted lab executable; that executable was backed up,
   and compilation does not requalify its older game acceptance.

An earlier Steam game process completed publisher auth, the encrypted lobby
handshake and peer registration. A bounded read-only snapshot at 11:54 UTC on
2026-10-15 verified the loaded caller against the owned image, then found no
current game party, party-creation task, registration task or retained publisher
registration session. Search state was 0; selected global/user error lists were
empty. This cannot establish the visible menu or the reason the earlier
registration state was released. No UI input or process-memory write was made.
Private evidence: `../shadps4/out/profiles/A/steam-poc/party-state-20261015T115402Z.json`.
The later multipart fix and successful human-operated party/search/reentry
supersede that failed state; see INTEGRATION_WORKLOG.md for the accepted build
and subsequent candidate verification. The earlier observation is retained as
diagnostic history.

The full multiplayer acceptance requires independent Steam accounts and hosts,
then human-observed game sessions. One account on one machine can qualify the
library/validator and some lifecycle behavior, but cannot establish a paired
race or an invited party entering public search.
