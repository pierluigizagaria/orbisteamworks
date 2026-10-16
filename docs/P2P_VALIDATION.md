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
  The shared library exports the original ten functions plus `osw_create_v2`.
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

`osw-p2p-check <absolute user-supplied Steam API library path>` is a bounded
single-account diagnostic. It creates an invisible room, checks namespace and
owner/member readback, performs filtered search, updates metadata and leaves.
It prints no account IDs, room IDs, tokens or payloads. Coordinate its use with
other Steam hosts: the library intentionally permits only one Steam context in
a process, and one Steam client/account cannot qualify two live peers.

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
   Start with matching title `5760`, build `1.21` and a private test realm.
   Build and realm strings must be exactly equal on both PCs: `1.21` and
   `01.21` are distinct namespaces. The standalone directory diagnostic uses
   its own `01.21` / `p2p-gate` namespace and is stopped before game testing;
   the two-PC game example follows the native defaults and profiles.
2. On each PC configure ABI 2 Steam mode, a distinct loopback game endpoint
   from its publisher listener, and a private companion working directory.
   The child environment includes `CTR_COMPANION=stdio`, `CTR_ADMISSION=local`
   and `CTR_MATCHMAKING=steam`. Keep normal and unlock-all databases and saves
   in separate directories. Wait for companion `status.ready` before entering
   the game's Online mode. No central helper or remote publisher is required.
3. First run the single-account diagnostic on each PC separately. Stop it before
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
