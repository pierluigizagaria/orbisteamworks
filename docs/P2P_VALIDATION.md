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

## Two-PC procedure (pending execution)

1. Use two PCs with separate Steam accounts that are friends. Each user supplies
   their own SDK runtime. Pin the same library, native host, publisher build,
   original game version and signature patch; record SHA-256 hashes privately.
   Start with matching title `5760`, build `01.21` and a private test realm.
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
