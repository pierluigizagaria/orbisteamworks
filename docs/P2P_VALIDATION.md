# Native P2P library validation

2026-10-16. This records implementation and measured library evidence, not a
declaration that native gameplay or two-account Steam racing is accepted.
The design and local pipe protocol are replacement choices (I). The manually
declared provider ABI was checked against the user's Steamworks SDK 1.65 (P),
including callback numbers, structure sizes/offsets, invisible lobby type,
distance filters, reliable flags and Networking Messages identity layout.
No SDK source or binary is distributed by this repository.

## Implemented boundary

- ABI 1 remains intact; ABI 2 adds backend, game endpoint and companion config.
  The original ten exports plus `osw_create_v2` remain available; canonical
  endpoints add three optional exports without changing those structure layouts.
- Steam directory search uses exact public namespace filters, serial
  Default/Far/Worldwide expansion and bounded results. Public metadata is
  allow-listed and limited to 256 bytes in both directions. The advertised
  owner is verified against the actual owner after joining; Steam does not
  expose actual owner/member queries before membership.
- Current room members exchange reliable room envelopes. Race datagrams pass
  through per-peer loopback sockets using the authenticated sender identity.
  A separate read-only private advertisement query is limited to in-game
  friends in the same namespace. Room mutations are not automatically retried.
- Committed platform-party members follow the leader's selected invisible
  room. Provisional invitation joins do not count as committed membership.
  Failed/cancelled creation rolls back the room and selection. Owner departure
  closes the room; no publisher-engine migration is claimed.
- Each process owns its hidden publisher via private pipes and a Windows
  kill-on-close Job Object. EOF permits graceful child shutdown, while blocked
  writers and stalled children have bounded teardown. No session-idle cutoff
  was added. A random local capability binds admission to that child owner.
- The explicitly selected lab backend shares a locked, size-bounded local hub
  and carries race traffic over loopback UDP. It simulates accounts, friends,
  invitations, directory and authenticated sender labels. It is not Steam.

## Measured Windows checks

The unchanged starting static build passed 28/28 suites. The historical shared
build directory initially contained only its shared-load test executable; that
test passed, while other missing binaries were not counted as passing. A fresh
Release build with both static and shared targets now passes 31/31 suites.
The build uses the existing MSVC 18 toolchain and nlohmann/json headers; no new
system toolchain was installed. The C translation unit pins both config sizes
and the ABI 2 account field offset. Shared-load checks the export allow-list.

Final local Release build pins (SHA-256):

| Artifact | Hash |
| --- | --- |
| `build/p2p/orbisteamworks.dll` | `bd3f69edefc2a9bd6eee959b2b45ed7fd8bf287dc03bace2bdb253493d4362c1` |
| `build/p2p/osw-p2p-check.exe` | `89eb4d16b26c647431deed5f892f9ed53d3b3151222e1682b416aee03a57da5d` |

The final full suite completed in 12.10 seconds, with 31 tests passing. These
local artifacts stay ignored; the hashes identify evidence, not a release.

`p2p-cross-process-lab` starts a real second process and verifies identities,
friend snapshots/names, a private advertisement before any room/party, directory
search/join, binary reliable payload preservation, bidirectional UDP with stable
per-peer endpoints, invitation receipt/acceptance, explicit game membership
commit and party-following room selection. It also tests malformed input without
context failure, cancellation rollback, EOF shutdown and a non-reading child
with a blocked pipe writer. Steam's prejoin owner/member visibility restriction
is reproduced by the lab backend.
The follower's pre-party friend proxy is promoted to the selected room without
changing its advertised socket; a regression sends the first race packet from
that follower before the owner has allocated a reverse proxy, then verifies
the return datagram on the original socket.

`steam-p2p-contracts` uses the offline fake DLL to verify invisible creation,
serialized search filters, copied sender identity/payload, unapproved/departed
peer rejection, deferred session authorization and failed-session reset without
replaying a command. These fixtures do not contact Steam. A separate local
SDK-header compilation checked the declarations against SDK 1.65.

The publisher integration agent also ran two real hidden publisher children
against the built shared library and lab backend: signed HTTP admission,
encrypted Demonware handshake/tasks, pre-party friend advertisement, name
lookup, empty foreign statistics, room create/join and bidirectional proxy
datagrams passed. That evidence belongs to the publisher's own integration
record; it remains a fixture, not game or Steam acceptance.

The Windows execution sandbox silently dropped loopback UDP, including a
minimal same-process socket pair. The identical authorized test outside that
sandbox passed. No firewall, adapter or other system setting was changed.

## Live and remaining acceptance

`osw-p2p-check [--virtual-endpoints] <absolute user-supplied Steam API library path>` is a bounded
single-account diagnostic. It creates an invisible room, checks namespace and
owner/member readback, performs filtered search, updates metadata and leaves.
It prints no account IDs, room IDs, tokens or payloads. Coordinate its use with
other Steam hosts: the library intentionally permits only one Steam context in
a process, and one Steam client/account cannot qualify two live peers.

The optional `--virtual-endpoints` flag enables canonical endpoints through the
public API before startup and requires copied local endpoint readiness before
testing protocol 2 directory operations. Omitting it preserves the existing
legacy invocation and protocol 1. The search check requires the just-created
room ID to appear exactly once; empty results, unrelated rooms or duplicates
cannot print PASS. Prejoin owner/member fields are not required. A successful
search alone does not establish that distance widening occurred.

The real single-account diagnostic passed on 2026-10-16 after coordinating
exclusive use of the Steam client. Using the pinned executable and user-supplied
SDK runtime above, it completed in 3.75 seconds with exit code 0:

- Invisible lobby creation succeeded, and namespace plus actual owner/member
  readback matched the signed-in account (one member).
- The filtered directory search returned one matching room at the initial
  distance. This live run did not exercise empty-result distance widening;
  widening remains implemented and covered separately from this observation.
- Metadata update/readback and explicit leave succeeded. The context was
  destroyed and the Steam slot released for native integration.

The full SDK output is private in ignored
`runtime/p2p-live-20261016/single-account.log`; only diagnostic PASS lines and
the exit code were reported. Its SHA-256 is
`f5cc68d7458e2ff536df7e5410b866c2d11a375d50c7cd4aab2a08e951011002`.
The library initializes Steam networking/relay
support in this path, but initialization alone proves no direct or SDR peer
connection. No account IDs, lobby IDs or token values are published here.

Two-account message,
SDR/WAN, game friend/invite UI and race-through-results acceptance remain
separate requirements. The native integration record owns the original-game
P1/P2/P3 results. Linux child/spawn/socket code is implemented but has not been
compiled or accepted on Linux in this Windows run. The lab UDP carrier limits
game datagrams to 65,487 bytes because of its additional local headers.

A subsequent targeted POSIX fix invalidates companion ownership when `waitpid`
reports `ECHILD` and retries `EINTR`, including the final reap. A platform-neutral
regression exercises these syscall outcomes, and the Windows static library plus
cross-process lifecycle regression pass. The running shared DLL was left at its
pinned hash above; this was a source/static-test update, not a runtime deployment.
Linux compilation/execution remains unavailable. POSIX hosts must reserve waiting
and reaping of the companion PID exclusively to the library; concurrent host
reaping remains unsupported while shutdown uses PID-based signaling. The pure
test proves `ECHILD` handling, not a stable-process-handle guarantee on Linux.
The POSIX launcher also checks the existing `SIGCHLD` disposition and refuses
`SIG_IGN` or `SA_NOCLDWAIT` before allocating pipes or launching a child. It never
changes that process-wide state. This catches known auto-reaping policies at
startup; the host must preserve exclusive ownership afterward. The guard's
POSIX syscall branch has not been compiled or run on Linux here. A Windows
static-only regression build and the existing pure ownership/cross-process tests
do not qualify that branch. No Windows DLL was rebuilt or deployed for this guard.

The native host agent subsequently passed its real Steam host-adapter probe
against this DLL and publisher v2: NP identity and child readiness, guest osw1
admission, the in-process friend-list query, and party creation, metadata/binary
readback and leave (native checkpoint `178420d`, 3.047 seconds, exit code 0).
This is host-boundary evidence; it does not establish a populated two-account
friend page or original-game P2 acceptance. Separately, the parent records human
confirmation of original-game Online and Pit Stop in the lab/central setup as
scoped P1 evidence. Original-game Steam P2 and a two-player P3 race remain pending.

## Two-PC procedure (pending execution)

1. Use two PCs with separate Steam accounts that are friends. Each user supplies
   their own SDK runtime. Pin the same library, native host, publisher build,
   original game version and signature patch; record SHA-256 hashes privately.
   Use canonical endpoint mode on both PCs for this integration, with matching
   title `5760`, build `1.21` and a private test realm. Address mode, title, build
   and realm must match exactly: `1.21` and
   `01.21` are distinct namespaces. The standalone directory diagnostic uses
   its own `01.21` / `p2p-gate` namespace and is stopped before game testing;
   the two-PC game example follows the native defaults and profiles.
2. On each PC configure ABI 2 Steam mode, a distinct loopback game endpoint
   from its publisher listener, and a private companion working directory.
   Physical loopback values may be the same on different PCs; each game's
   endpoint must still be distinct from its own local publisher endpoint.
   The host calls `osw_enable_virtual_endpoints` after `osw_create_v2` and before
   `osw_start`, then uses the copied endpoint/route API documented in
   [C_API.md](C_API.md). Canonical addresses and ports are provider-owned:
   never enter them manually or copy them between profiles. As a configuration
   example for the native consumer, set `BB_OSW_VIRTUAL_ENDPOINTS=1`; this is
   that host's opt-in setting, not an environment variable read by the library
   or a requirement for other hosts.
   The child environment includes `CTR_COMPANION=stdio`, `CTR_ADMISSION=local`
   and `CTR_MATCHMAKING=steam`. Keep normal and unlock-all databases and saves
   in separate directories. The companion must acknowledge
   `virtual.endpoints.v1` in its ready capabilities; missing support fails
   startup. Wait for provider readiness, companion `status.ready` and a
   successful `osw_get_local_endpoint` before entering the game's Online mode.
   Both peers must use the same mode: virtual protocol 2 and legacy protocol 1
   are isolated for friends, parties, rooms, advertisements and peer carriers.
   No central helper or remote publisher is required.
3. First run the single-account diagnostic with `--virtual-endpoints` on each
   PC separately to check the current protocol 2 mode. Stop it before
   launching the game. Verify Online identity, local admission and the in-game
   friend list on both game processes. Confirm a player from another realm is
   absent. Record observations with pseudonyms and redact all private payloads.
4. Verify directory discovery between non-party clients, one owner and one
   joining member. Search records may lack members; joined records must name
   both authenticated members. Compare the game's notices and transitions to
   the accepted central flow. Complete a race through results and verify each
   player's economy/statistics changed only in that player's own database.
5. Verify invitation send/receive/accept and the game's explicit membership
   commit, then leader search with follower autojoin. If the native invitation
   selector is unsupported, record that precise host gap instead of claiming
   UI acceptance from C API fixtures. Test leave/cancel/reentry and owner exit;
   remote room state must close and local progression must remain intact.
6. Repeat on separate Internet connections. Observe Steam's own networking
   diagnostics to distinguish direct from relayed transport; relay initialization
   alone does not prove SDR use. Record latency/loss symptoms and transport mode
   without tokens or full requests. No forced firewall/system changes are part
   of this procedure. Repeat normal shutdown and an explicitly authorized crash
   of the test host, verifying that its owned child exits and a fresh start works.

A pass requires both real Steam peers and original-game evidence. A synthetic
packet round trip, a single-account lobby or two lab identities cannot satisfy it.

## Follow-up candidate: explicit party departure

The publisher's real two-child lab probe exposed a lifecycle defect beyond the
initial positive-flow tests: after a party follower closed its actual publisher
TCP connection, its explicit directory leave was immediately undone by automatic
following of the retained party selection. The owner could miss the short-lived
departure. Ordinary non-party disconnect already passed the publisher probe.

The library now remembers that explicit leave until new intent is supplied:
valid create/search/join, a genuinely different selected room, or a new party or
owner context. It also abandons an in-flight automatic join. New regressions
verify stable absence, explicit rejoin, search re-enabling and a new leader
selection. Controlled provider callbacks verify that an explicit rejoin resumes
party-follow tracking and leaves the room when the leader moves or the party
ends. Rejoining an abandoned join's room is busy until its callback is cleaned
up, preventing a late callback from removing a newer membership. Pending
validations, provider calls and abandoned calls share a bound of 128; a regression
fills that bound and verifies admission recovers after callback cleanup. A
permanently missing callback keeps the affected room busy; exhausting the shared
bound requires a fresh library context. No session lifetime or idle timeout was
introduced, and tombstones are not discarded by a timer.

An isolated Release candidate in `build/p2p-candidate/` passed all 32 suites in
19.38 seconds, including the pure child-wait and controlled party lifecycle tests.
Its sources are checkpoint `d0b4f59`; the later POSIX-only prelaunch guard does
not change Windows code and was not deployed in either DLL.
Candidate DLL SHA-256:
`bc172ccc8beddf8f2045d4420407a660ce4484de12a3f456ab037c7663f110e1`.
The human-operated game's `build/p2p/orbisteamworks.dll` remains at `bd3f69...`
above and was not replaced or interrupted.

The rebuilt `osw-p2p-check` (statically linked to the same candidate sources)
repeated the real single-account Steam check at 2026-10-16 07:53:42 UTC. It exited
0 in 3.728 seconds: invisible lobby creation, exact namespace/owner/member
readback, one matching search result, metadata update and leave. Tool SHA-256:
`c435de1967793d4b52139452a80365bad852850ec1744b32181b47365a74453a`.
The user-supplied SDK runtime is the same 1.65 binary pinned above. Private log
`runtime/p2p-live-20261016/candidate-single-account.log` SHA-256:
`f5cc68d7458e2ff536df7e5410b866c2d11a375d50c7cd4aab2a08e951011002`.
The check contains no companion, second Steam account, SDR transport or game
acceptance claim.

Both actual two-child publisher lab probes passed again with this candidate and
publisher v4 (`55ebb84`, executable SHA-256
`0dcb7fc98b406b43602d6cc03addcb2ea262ea2451226a92dcd8c05ccb20df91`).
They verify signed admission, encrypted lobby requests, local statistics privacy,
bidirectional opaque UDP, and actual follower TCP disconnect followed by the
owner's 2006 seat-removal push. The ordinary room-join and committed-party modes
both pass; the latter includes simulated invite acceptance and party selection.
The publisher separately fixes event/query snapshot ordering. These tests run
real publisher child executables but use the explicit lab platform backend;
they do not qualify two-account Steam or original-game multiplayer.

## Windows Lab hub replacement contention (2026-10-16)

During the original-game Lab investigation, one publisher logged companion
closure at 11:50:52 local time while its native host remained alive. A direct
read-only inspection of the active Lab hub had been performed during that
investigation; its exact read timestamp was not retained. This makes observer
interference a plausible cause, not an established diagnosis of that shutdown.
No further direct reads of the active hub were used for this validation.

An isolated Windows experiment reproduced the concrete failure mode: a reader
opened without `FILE_SHARE_DELETE` prevents `MoveFileExW` from replacing the
hub, returning `ERROR_ACCESS_DENIED`. Opening the target with `DELETE` access
and all sharing modes then identifies `ERROR_SHARING_VIOLATION`. Previously any
replacement failure escaped the Lab worker and shut down its local companion.

The Lab replacement helper now retries sharing/lock violations for at most
approximately 500 ms, with 10 ms sleeps. `ERROR_ACCESS_DENIED` is retried only
when that non-mutating target probe confirms sharing/lock contention; other
errors remain immediate failures. The budget is shorter than the existing
two-second hub-lock acquisition deadline. The complete staged file and previous
hub remain intact on failure. There is no session or idle timeout, no retry of
arbitrary permanent errors, and no change to the Steam backend or public API.
External observation must avoid holding a live hub handle without delete
sharing; the bounded retry is not permission to hold that file indefinitely.

The real Windows regression holds an incompatible reader for 150 ms and verifies
successful complete replacement; a persistent reader verifies bounded failure
and unchanged files; a read-only target verifies immediate permanent failure.
The new MSVC static/shared candidate `build/lab-sharing-candidate/` passed all
37 CTest cases (27.21 seconds), including this new test. One final actual
two-child default Lab probe passed with the unchanged publisher SHA-256
`0fc70159dad030a978f7126e25368fec4e911cc8d418980f7268fe290a027369`:
unilateral pre-party UDP, room transport and actual disconnect/seat removal.
The previous candidate's default and committed-party probes are recorded in
[INTEGRATION_WORKLOG.md](INTEGRATION_WORKLOG.md).

Final candidate DLL SHA-256:
`69df08ab135b599fb48b9d3951ab8640fdd1c012bfbee4f37b588b5cb864ca1d`.
Private evidence: `out/lab-sharing-candidate-build.log` and publisher
`.cache/p2p-probe/lab-sharing-final-default.log`. These remain implementation
fixtures; no active game or Steam process was controlled or restarted, and no
candidate was installed by these checks. Original-game race acceptance remains
separate. Linux behavior is unchanged and was not qualified by this Windows test.

## Canonical endpoint transport (2026-10-16)

The next original-game Lab attempt delivered proxy UDP in both directions but
did not establish a peer connection. Owned-game analysis in the publisher/host
projects showed that rewriting embedded transport addresses changed the peer
identity used by its traversal handshake. That title evidence motivates an
optional generic library interface; no game parser or hash implementation was
added here. The C API supplies a stable self label and authorized bidirectional
endpoint mappings while carrying the game bytes unchanged (replacement design,
I). The host owns socket ABI translation and the publisher owns its title's
address validation. This extension is reusable by other hosts and does not alter
the legacy ABI 1/2 layouts or opt-out behavior.

The isolated MSVC static/shared build in `build/virtual-candidate/` passed all
39 CTest cases in 26.57 seconds. New checks cover deterministic full-width account
labels, namespace separation, malformed POD input, both routing directions,
copied-output ownership/aliasing, startup/stop behavior, collision revocation and
tombstones, fixed capacity, and concurrent lookup/revocation. Real local Lab
sockets verify unilateral pre-party bootstrap with the source mapping published
before first delivery, opaque binary replies, grant withdrawal, and a fresh
authorization using a different physical port. An old companion without the
capability acknowledgement fails closed; an acknowledging fixture becomes ready.
Virtual/legacy friend lists, directory searches/direct joins, party invitations
and documents, private advertisements, and carrier headers are isolated. The
carrier regression rejects the other mode even with a cached authorized friend.
Shared-library loading verifies all 14 C exports and the unchanged configuration
layouts; the new endpoint structure is checked as 24 bytes in a C translation unit.

Private build evidence: `out/virtual-candidate-final-build.log`.
Candidate DLL SHA-256:
`bb6bb92567f6b14c7ebf07a7b3fbec159c776132320b02d60df72e6f54785bf7`.
No live Steam process, game UI or installed executable was changed by these
tests. The publisher's actual two-child integration is an optional external
consumer of the public API; its final matching-build evidence is recorded
separately. Original-game racing, Linux runtime behavior and two-account Steam
transport remain separate acceptance gates.

The final actual two-child publisher probes also passed in both ordinary and
committed-party modes with library source `93c4b5c` and the exact DLL above.
The publisher executable was built from `112e884`, SHA-256
`75767b9adb27d7f2f979285b3e2291858a79cc24c6642a516f836c938175bbd0`.
Both artifact hashes were checked before and after the runs. These optional
integration probes verify exact address-document preservation, canonical local
discovery replies, forward/reverse C API mappings with actual opaque proxy UDP,
follower selection and disconnect/seat removal. An isolated owned-instruction
check validates the original target-identity gate against the actual returned
synthetic advertisement; this is not an original-game session or a race.
Private publisher evidence is `.cache/p2p-probe/canonical-final-default.log`,
`canonical-final-party.log`, and `canonical-final-acceptance-report.json` in
that same directory. The publisher remains independently built; the library's
standalone suite neither requires nor starts it.

A separate live single-account Steam host/companion check passed at
2026-10-16T11:26:59Z (exit 0, 3.016 seconds), using native host source
`583744a61e212cd3a88a1a3ffe5a9e290cd86437` and probe SHA-256
`49fb6ca1a60c188e4d78746a1c7a825cda04de9843e4709fd7ee6247a42f7899`.
It used the same library source `93c4b5c`/DLL `bb6bb925...` and publisher
`75767b9a...` pinned above. Private evidence is the consuming host's
`out/p2p/virtual-endpoint-steam-check/result.json`. This qualifies the measured
one-account host API/companion path with virtual endpoints enabled, not a peer
exchange. Original-game Join/race acceptance and two-account Steam transport
remain unqualified. This documentation follow-up does not change those binaries.

The standalone P4 diagnostic was subsequently updated to support the explicit
`--virtual-endpoints` mode and to fail unless search returns its created room
exactly once. Only `osw-p2p-check` was rebuilt in `build/virtual-candidate/`;
the DLL hash above stayed unchanged. Five invalid-CLI checks (missing library,
flag only, unknown option, duplicate mode, and extra library) all returned the
usage error before provider startup. Private evidence:
`out/p2p-check-virtual-build.log` and `out/p2p-check-virtual-cli.json`.
Tool SHA-256:
`c80bfbf8c4885c529e2b69593a29154f055d6408ab5eaa0c43a1b7d7d9f4967d`.
These build/CLI checks do not themselves exercise the live Steam directory.

The updated diagnostic then passed a bounded live single-account Steam run at
2026-10-16T11:35:21.778159Z (exit 0, 3.0 seconds), using source `992723f`, the
exact tool hash above and `--virtual-endpoints`. Its four PASS results establish
canonical endpoint readiness with protocol 2, invisible room creation with
matching owner/member readback, filtered search containing the created room
exactly once (one result), and metadata update/readback followed by leave.
The user-supplied Steam API runtime SHA-256 was
`e6d9bafb9a41e42fba7b21553db49f8719027af3f0deb23ff86cfc44d60e776d`.
Private evidence is `runtime/p2p-live-20261016/canonical-directory-result.json`
and `canonical-directory.log` in the same directory; the log SHA-256 is
`d3c1123978607fd813f4f35ff1af8f4106fb39146c3ae99c8305244b8d1a108a`.
The matching search does not qualify distance widening. This one-account
directory check does not qualify remote peer/SDR transport, original-game Join
or racing, or two-account Steam behavior. No source or build changed during
this documentation follow-up.

## Lab hub read failures and unattended stability (2026-10-16)

The subsequently installed original-game Lab pair lost both companions at
13:28:59 local time while both host processes remained alive. The companions
reported EOF; a separately authorized, read-only host-state inspection found
both library identities signed out with `OSW_UNAVAILABLE` and the old generic
`Platform backend failed` reason, while the host's stop flags remained clear.
This establishes that both library workers reached their exception path. It
does not identify the actual exception. No active hub was opened during this
investigation, and neither the earlier startup checks nor this diagnosis
qualify an original-game Join or race.

A focused source review found that an unsuccessful `ifstream` open was treated
as a missing hub. On an existing unreadable file this could erase the in-memory
user state, then fail polling or save partial replacement state during cleanup.
An isolated two-context baseline stayed ready for 120 seconds. A corrected
Windows fault injector then opened only its newly created fixture hub with
`GENERIC_WRITE`, without performing any writes, and omitted `FILE_SHARE_READ`.
It explicitly verified that a full-share `GENERIC_READ` open failed with
`ERROR_SHARING_VIOLATION` (32). Holding that handle for 200 ms reproduced both
old providers failing at 2.2 seconds with the generic reason. This is a concrete
storage defect reproduction, not proof that the same file-open condition caused
the unattended original-game failure. An earlier metadata-only fault handle
used access 0 and did not establish that read denial; the corrected experiment
supersedes that interpretation.

The reader now distinguishes genuine absence from an unreadable existing hub.
Only initial identity registration may create absent state; all active and
cleanup operations require an existing, successfully read hub. Windows opens
permit read, write and delete sharing. Only proven sharing/lock errors 32/33
are retried, before state mutation, within approximately 500 ms, below the
existing two-second lock acquisition deadline. Permanent read errors and invalid
state fail closed. Saving checks both explicit flush and close before replacing
the old file. A failed read cannot publish partial state. Fixed safe Lab failure
categories now reach the existing identity reason field without exposing paths,
identities, payloads or arbitrary exception text. There is no request replay,
reconnection policy or session/idle cutoff.

The isolated MSVC static/shared candidate in `build/lab-read-candidate/` built
successfully. The initial CTest run passed 38 cases and exposed two mistakes in
the new tests' metadata-only denial handles. After changing only those injectors
to request actual access, both focused regressions passed in 2.11 seconds;
there are 40 passing cases across those two runs, not a claimed single clean
40-case run. The tests verify transient recovery, bounded persistent failure,
exact existing-byte/user preservation through failed startup and teardown,
immediate permanent-open failure, and refusal to recreate a missing active hub.
The DLL was not rebuilt between those runs. Linux's absence/read
distinction was updated but its runtime behavior was not qualified on Windows.

The new DLL then passed the complete 120-second isolated paired-provider run
with the verified 200 ms error-32 fault at two seconds. Both identities and
canonical endpoint APIs remained ready after the fault and throughout the
remaining interval (exit 0). DLL SHA-256:
`103a115e54542d8f3472a1eb02d614f677c36947b1f6526541594899edbc2583`.
This bounds the measured fixture result; it is not an unattended lifetime
guarantee or qualification of the still-unexplained live failure's cause.

Private evidence: `out/lab-idle-probe.log`, `out/lab-read-verified-before.log`,
`out/lab-read-candidate-build.log`, `out/lab-read-focused.log`, and
`out/lab-read-stability-after.log`. The private reproducer is
`out/lab-read-stability-probe.py`; it accepts `--dll`, `--duration`, and the
old-binary-only `--expect-failure`. These are
standalone library fixtures; no actual publisher, game or Steam process is
required by them. No active game or installed DLL was changed by these checks.

## Bounded platform blob request window (2026-10-16)

The user subsequently reported that the original-game Lab invitation, private
match and complete race succeeded with library source `4ab63f2`/DLL
`103a115e...`, native source `cd41018` and publisher source `112e884`. This is
human-reported original-game Lab acceptance of that artifact set. It does not
qualify two-account Steam transport or the new candidate described below.
The remaining observation was approximately four to five seconds entering a
lobby. The observed interval was not instrumented, so its cause is not established.

Source review found a measurable serial dependency in the generic platform
payload transfer. `Platform::save` moves immutable image/data values larger than
1,024 hexadecimal characters into private owner blob storage, leaving references
in the lobby document. `Platform::process` waits for those copies before completing
acceptance. Both Lab and Steam use the same `blob/1` messages; Lab does not bypass
this exchange through shared metadata. These lobby events do not contribute to
P2P `network_received`, and an internal prefetch does not appear as a large host
WebAPI GET response. Fresh host logs expose only response sizes, not the actual
blob size or transfer duration. Nothing was read from the active Lab hub.

A small in-memory `Social` fixture compared the prior source and the candidate
with the same MSVC options, synthetic 52,096-byte image, and explicit 20 ms sleep
per simulated hop. Three sequential runs took 3,294–3,304 ms; three four-request
window runs took 859–874 ms. Both exchanged exactly 53 requests and 53 responses
with identical final bytes; dependency rounds fell from 53 to 14 and the largest
request batch rose from one to four. This is a controlled, modeled-cadence fixture,
not measured Lab file-backend latency, Steam latency or a game loading benchmark.
The private harness and output are `out/blob-window-bench/bench.cpp` and
`out/blob-window-bench/comparison.log`. Prior source copies used for comparison
remain private in that same ignored directory.

The production change preserves the fixed 2,000-character chunks, 4,096-byte
frames and `blob/1` schema. It bounds attempted/buffered offsets to four per copy,
accepts only successfully requested offsets, and reassembles a contiguous prefix.
Existing sequential receivers and independent-offset owners remain compatible.
Focused regressions cover reverse delivery, duplicates, missing chunks, forged
senders/recipients/nonces, unaligned or unrequested offsets, invalid sizes,
leave/late replies, owner change and final hash failure. They also check first
send failure, failed retransmission backoff, recovery, the unchanged 200-response
owner rate limit, and concurrent 120,000-byte image/data copies with a lost reply.
That concurrent test uses the existing 20-second transfer bound; the 30-second
CTest bound applies only to the test process. No session lifetime changed.

An isolated static/shared build in `build/blob-window-candidate/` completed with
two build jobs. `platform-contracts`, `blob-window-contracts` and `shared-load`
passed together in 5.07 seconds. A review finding in the first iteration was
fixed before that run: failed retransmissions must advance the retry deadline
without authorizing a first failed send. The new fixture verifies this behavior.
The broader unchanged suite was not repeated for this focused core change.
Private build evidence: `out/blob-window-candidate-final-build.log`; the final
test-only deadline adjustment is recorded in `out/blob-window-final-regression.log`.
Candidate DLL SHA-256:
`8c9157a32f9faf396fdac0ba5a7b19d271e4eaf8880936f07f4939b6b026a1f4`.
No installed DLL, profile, live game, publisher process or Steam session was
changed by the library work. Actual consumer checks and a human game retest are
separate; this result does not establish how much of the reported lobby wait
the transfer accounts for.

## Lab identical metadata publication (2026-10-16)

After the blob-window candidate, the user reported faster invitation entry
without a precise duration, but a remaining wait before both players appeared
in a public lobby. Publisher source review identified repeated identical
directory updates. The library publishes nine metadata keys per directory
update; the Lab implementation previously rewrote the hub and queued data
events for each key even when the value was unchanged. This is concrete
avoidable work, not proof of the whole observed game's delay.

The narrow correction changes only `LabSocial::set_lobby_data`: it acquires the
existing lock, reads current state and verifies the owner before comparing an
existing string. Equality returns success without mutation, save or broadcast.
A missing key, even with a requested empty value, still publishes normally.
There is no cache, publisher deduplication, protocol/API change, Steam change,
timer change or altered stale-user polling. Failed current-state reads still
fail closed rather than reporting cached success.

The same private Python harness used two actual Lab contexts through the public
C API, with virtual endpoints enabled, fresh UUID hub directories, identical
synthetic metadata and the existing worker cadence. Each DLL ran ten cycles;
the first two were warmup. The runs were adjacent, before then after, on this
Windows machine. DLL hashes were checked before and after each run. These are
real Lab file-backend timings, with no publisher child, game or Steam process:

| Operation | Samples | Previous median [range], ms | Candidate median [range], ms |
| --- | ---: | ---: | ---: |
| Identical directory update | 16 | 103.217 [78.556–128.189] | 44.707 [30.866–72.473] |
| Changed directory update | 8 | 101.476 [70.754–133.496] | 63.420 [46.081–77.059] |
| Create directory room | 8 | 94.337 [73.787–106.317] | 90.734 [73.697–107.640] |

Process write-operation medians fell from 11 to 2 for identical updates and
from 11 to 4 for changed updates. These counters include both workers' normal
polling, so they are not exact setter-only counts. Changed updates also benefit
because their unchanged namespace keys need no write. Creation still publishes
new keys. A separate direct Lab fixture, with no background worker, confirms
zero writes for the identical setter itself, exact preservation of queued hub
bytes, no extra data event, missing-versus-empty handling, denial of non-owner
updates, later real updates, read-failure handling and deleted-room rejection.

The isolated MSVC static/shared build in `build/lab-metadata-candidate/` used
two jobs. `platform-contracts`, `lab-file-sharing`, `lab-state-read-failure`,
`lab-metadata-publication` and `shared-load` passed in the initial sandbox run.
`p2p-cross-process-lab` initially failed its UDP carrier-delivery deadline there;
the exact same binaries and timeout passed with host loopback access in 18.10
seconds. No code change was needed, and the initial run is not reported as a
clean six-test pass. The coordinator also ran the actual DLL with the unchanged
publisher in the two-child party fixture: authentication, simulated invite/
accept, follower selection, directory/departure, canonical routes and
bidirectional UDP passed. That consumer fixture remains separate from original
game and live Steam acceptance.

Private evidence: `out/lab-metadata-candidate-build.log`,
`out/lab-metadata-cross-process-host.log`, `out/lab-directory-timing-before.json`
and `out/lab-directory-timing-after.json`. The timing harness is
`out/lab-directory-timing.py --dll <library-path>`; it emits only aggregate
results. The optional consumer log belongs to the publisher project at
`.cache/p2p-probe/lab-metadata-party-host.log`. No consumer checkout is needed
to build or run the library's own tests.

Previous DLL SHA-256:
`8c9157a32f9faf396fdac0ba5a7b19d271e4eaf8880936f07f4939b6b026a1f4`.
Candidate DLL SHA-256:
`251c4d97a305ef547785bfa5fbdefb36009961de48287b2a676126c4ca488e02`.
The previous/pinned DLL and profiles were not changed. No active profile hub
was read. The measured benefit is Lab-specific; the fraction of the public
game-entry wait attributable to it is still unknown.

## Fresh Lab lobby snapshots and opaque roundtrip timing (2026-10-18)

Human public-entry traces isolated a repeated interval of approximately three
seconds between the publisher's Join push write and the host's first report of
both players connected, with opaque UDP exchange already progressing. Source
inspection found that Lab room authorization reopened the same hub for each
scalar metadata, owner and membership getter. A successful member check used
ten reads; a stable one-room/one-proxy refresh used 34. These are source-path
counts, not measured physical disk operations or a decomposition of game time.

Provider `cc141ec` adds an optional internal copied lobby read. Lab obtains the
requested fields with one fresh locked Store. A membership decision now reads
once and a room record reads once; the same stable refresh uses five reads.
There is no authorization cache, polling change, shortened timeout, skipped
identity check, C ABI extension or protocol change. Current file-read failures
remain immediate. Conversion errors are retained in copied fields and raised
only on access, so namespace rejection still precedes unrelated field errors.
Steam and other backends decline the optional read and use the previous scalar
calls in their previous order, including their error side effects.

The private benchmark loads the actual DLL through the public C API and creates
two Lab contexts in a new UUID hub. It enables canonical endpoints and sends
only to returned proxy routes. The same contexts run friend scope first, then
room scope; each uses two warmups and sixteen 64-byte ping-pong samples with
alternating initiators. Every packet and both route directions are checked.
There is no API polling within the timed exchanges. Each context's four
transport counters increases by exactly sixteen per measured phase; rejection
counts stay zero. The immediate Python echo costs less than one millisecond.

| Pair and scope | Previous median, ms | Snapshot median, ms | Previous p95/max, ms | Snapshot p95/max, ms |
| --- | ---: | ---: | ---: | ---: |
| First, friend control | 77.935 | 77.717 | 102.032 | 100.018 |
| First, room | 368.683 | 84.412 | 556.956 | 137.342 |
| Confirmation, friend control | 78.002 | 77.507 | 94.506 | 86.938 |
| Confirmation, room | 407.664 | 93.223 | 560.297 | 135.488 |

The confirmation repeats baseline then candidate once, with the unchanged
harness and fresh hubs. With sixteen samples, nearest-rank p95 equals maximum;
this is not a production tail estimate. Room setup is measured separately and
falls from 573.824 to 183.731 ms in the first pair and 663.545 to 165.946 ms in
the confirmation. Friend RTT remains approximately unchanged. These results
support the specific Lab correction but do not establish full game-entry time,
Steam/SDR latency, or behavior with a committed platform party.

The MSVC static/shared build passes all 44 standalone Windows tests in 31.56 s
with host loopback access. New tests cover prejoin visibility, independent
copies, fresh changes/departures/deletion, read-sharing failures, event
preservation, zero writes, deferred conversion errors, scalar short circuits
with backend side effects, and revocation before a subsequent send without an
event. Independent reviews found no remaining blocker. The actual DLL also
passes the optional two-publisher integration with separate public searches and
with a committed party/invitation: local authentication, preserved address
identity, private statistics, follower selection, bidirectional proxy traffic
and member departure. Default integration emits 68/44 timing records; the party
run with timing disabled emits none. Those fixtures live in the consuming
publisher project and remain separate from the library's ordinary tests. At
this benchmark checkpoint, human entry timing and a complete public race were
unverified; the subsequent entry retest is recorded below.

Previous DLL SHA-256:
`251c4d97a305ef547785bfa5fbdefb36009961de48287b2a676126c4ca488e02`.
Snapshot DLL SHA-256:
`220bcad3605cb716f7c3d33195b644424ab7875b0af5323ee28b5cc3e0122e87`.
Frozen harness SHA-256:
`95cda36524fe073b21ad8d00dca0457b2aff0aaf1a9c4937d9ca6d2e08500f90`.
Private evidence and exact commands: `out/lab-udp-timing-20261018.md`, its four
JSON records, `out/lab-snapshot-candidate-build.log`,
`out/lab-snapshot-candidate-final-build.log`, and
`out/lab-snapshot-candidate-ctest-host.log`. This benchmark does not read active
profile hubs, operate game windows or modify the installed profiles.

## Subsequent game entry and abrupt child lifetime, 2026-10-18

The user confirms shared public lobby entry with the snapshot DLL. Four
original-game local Lab intervals, from the joining publisher's search receipt
to the first host document reporting both connected, measure 2.070416,
2.133127, 1.964782 and 1.929805 seconds. The previous provider's two comparable
intervals were 4.782978 and 4.618266 seconds. All 686 captured timing records
are paired with no traced errors/timeouts. This small sample is not a precise
button-to-UI or Steam/WAN latency estimate; complete public racing is still
pending. Host/publisher timing evidence remains an optional integration record,
not a dependency of the library's build or tests.

The additional Windows `child-abrupt-parent-exit` regression exercises the
production `Child` launcher with a standalone fixture executable, without a
game, Lab hub, Steam client or publisher. The fixture companion never reads
stdin, so EOF cannot satisfy the assertion. After acquiring an exact duplicated
process handle and verifying the companion is alive, the controller checks
both host `std::_Exit` (no destructors) and forced termination of its owned
host. The companion must terminate within the bounded wait. A separate safety
job remains open during assertions and cleans up only fixture descendants on
failure. There are no name-based process searches or termination decisions.

The targeted MSVC build and CTest pass in 0.45 seconds. A build-local negative
control removing the production `JOB_OBJECT_LIMIT_KILL_ON_JOB_CLOSE` flag fails
with `companion survived abrupt parent exit`, demonstrating that the outer
safety job does not make this test pass. Cleanup leaves no fixture processes.
Production source and the installed DLL are unchanged; the prior 44-test run
remains its own evidence, supplemented by this single new regression. Private
logs: `out/child-lifetime-test-20261018.log` and
`out/child-lifetime-negative-20261018.log`. The negative variant is confined
to the ignored build directory and is not a shipped configuration.

## Explicit discovery-distance regression, 2026-10-18

The existing `p2p-snapshot-authorization` fixture now records each requested
distance and its namespace/playlist/type filters. For both scalar and optional
snapshot reads, it verifies exact Default (1), Far (2), Worldwide (3) order
after empty or incompatible results; identical filters at every step; no fourth
search after exhaustion; and immediate completion at the first compatible room
at each of the three distances. Eight scenarios exercise returned results and
asynchronous callback completion, not only a counter in the implementation.

The targeted test passes in 0.05 seconds. An ignored build-local negative
control that skips Far fails the exact-order assertion. Production `p2p.cpp`
and the installed DLL remain unchanged. Private logs are
`out/directory-widening-test-20261018.log` and
`out/directory-widening-negative-20261018.log`. This closes the specific fixture
coverage gap; the earlier single-account live check found a room at the first
distance and still does not establish live geographical expansion or WAN/SDR.

## Invitation acceptance lifecycle regression, 2026-10-18

After a native Lab user reported that accepting an invitation closes its inbox
without entering the party, `platform-contracts` now exercises fresh entry,
an already prepared target, and an already committed target. Each first accept
completes with HTTP 204 and one copied accepted event. Acceptance does not
commit provisional game membership or implicitly leave the previous party;
those remain explicit guest requests. Duplicate acceptance is idempotent,
the inbox reports the invitation as used, and reinvitation after leaving
produces a fresh identity and accepted event.

The targeted fixture passes in 0.15 seconds, with no provider production change.
Private log: `out/invitation-lifecycle-test-20261018.log`. This verifies the
provider side only. The native host must expose queued accepted events through
both SystemService status polling and event consumption; the reported game
failure remains open until that transition is fixed and retested.

The subsequent original-game Lab retest passed after native host `065173e`
corrected its SystemService event count. The user confirms shared-group entry
after accepting a fresh invite, corroborated by host acceptance/queue/delivery
diagnostics and later guest requests. Provider production code remains
`cc141ec`; no automatic membership or host-specific behavior was added here.
This accepts the local host integration only, not remote Steam invitation
delivery. The earlier public Lab race also reached results on both clients by
explicit human confirmation; two-account Steam/SDR remains the written handoff.

## Current platform metadata reads and invitation timing, 2026-10-18

The user subsequently reported delay while the native inbox displays acceptance.
An isolated actual-DLL Lab benchmark separates receipt, explicit acceptance and
prior preparation. Each sample uses two in-process contexts, a fresh UUID hub,
canonical endpoint mode and synthetic session data. No publisher child, game,
renderer, Steam client or active profile hub participates. Results measure C API
submission to observed HTTP 204/event, not click-to-UI or guest follow-up time.
The 52,096-byte synthetic image comes from the existing platform fixture; it is
not a measured image size from the human session.

Baseline production `cc141ec`, DLL
`220bcad3605cb716f7c3d33195b644424ab7875b0af5323ee28b5cc3e0122e87`, took a
median 73.747 ms to accept a fresh target with a 64-byte inline image and
3942.151 ms with the large image (three samples each). Preparing the large
target through GET first moved approximately 3.76 s into that GET, then accept
took 8.983 ms. Acceptance waits for referenced blobs by contract; no fixed
success wait was found. It remains provisional until the guest commits it.

A first candidate batched only the namespace/document reads in `Platform::load`.
In an interleaved comparison it did not materially improve the large transfer:
3580.151 ms baseline versus 3603.340 ms candidate. The final change also batches
the four namespace fields within each `compatible()` decision, including each
received blob message. Lab supplies one fresh read; Steam's default optional
snapshot remains absent and preserves its scalar getter/short-circuit behavior.
Snapshots are never retained across decisions, messages or worker passes.
Membership, owner checks, blob framing, window, retry/backoff and deadlines are
unchanged. A failed current read is never replaced with a cached authorization.

The unchanged benchmark workload was then run in alternating
baseline/candidate, candidate/baseline, baseline/candidate order. Three samples
per binary/case gave the following median [minimum, maximum] values in ms:

| Operation | Baseline | Final candidate |
| --- | ---: | ---: |
| Fresh accept, 64-byte image | 69.338 [64.478, 83.403] | 47.323 [40.873, 57.529] |
| Fresh accept, 52,096-byte image | 3612.993 [3550.004, 3719.516] | 2604.737 [2523.774, 2906.391] |
| Preparation GET, 52,096-byte image | 3767.377 [3566.923, 3870.509] | 2689.537 [2624.259, 2786.325] |

The large fresh-accept median decreased 27.9% in this bounded Lab comparison.
Already prepared acceptance remained sensitive to one worker tick (approximately
8–22 ms in the final candidate), with no demonstrated improvement. All runs
verified byte-exact images, one copied accepted event, idempotent duplicate
acceptance and no implicit membership commit. HTTP completion and accepted event
were observed in the same polling cycle. With only three samples, nearest-rank
p95 equals the maximum and is not a population percentile estimate. Host load
was not controlled, so these figures do not predict Steam or human-dialog time.

MSVC Release static/shared builds completed at parallelism two. The full
independent suite passed **45/45 in 30.90 s**, including new scalar/snapshot
equivalence, validation-order, delayed-field-error, current-store-failure,
namespace-revocation and document-freshness assertions in `platform-contracts`.
Independent parent and publisher reviews found no blocker. The final DLL hash
is `05aa1e3b14a6eccb709e111426cb8f849156d6b9982aff173cfc7eaeeb1c7e27`, pinned
under `build/pinned-lab-invitation/05aa1e3b14a6eccb/` without another build.

Private evidence: `out/lab-invitation-timing-20261018.md` contains exact commands
and pins; `out/lab-invitation-timing-20261018.json` is the initial baseline;
`out/lab-invitation-load-20261018-comparison.json` and
`out/lab-invitation-namespace-20261018-comparison.json` retain both comparisons;
`out/lab-invitation-snapshot-full-20261018.log` records the full build/suite.
The frozen timing helper hash is
`a54a6079f703ec86fe4194e7459d3af47e04c076813bdc7337309ce05e8cac40`.
This records component tests and measurements only; installation, optional
integration checks and a human retest of the new candidate remain separate.
