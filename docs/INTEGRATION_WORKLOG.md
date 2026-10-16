# Steam integration work log

## Native P2P library extension, 2026-10-16

The user authorized the native-first design in P2P_DESIGN.md. ABI 2, the owned
hidden companion, invisible room directory, authenticated peer channels,
per-peer loopback proxy and explicitly simulated multi-process lab backend are
implemented on `feat/native-p2p`. ABI 1 and central Steam admission remain
available. The detailed implementation/validation record and pending two-PC
procedure are in [P2P_VALIDATION.md](P2P_VALIDATION.md). Its evidence must not be
merged with the historical shadPS4 acceptance below or presented as native-game
or two-account Steam acceptance.

The subsequent real single-account `osw-p2p-check` passed invisible lobby
creation, namespace/actual owner/member readback, initial-distance filtered
search (one result), metadata update and leave. It completed in 3.75 seconds
with exit code 0 and released its Steam context. Full SDK output stays in the
ignored private log referenced by P2P_VALIDATION.md. This qualifies that narrow
P4 live directory check, not two-peer transport, relay use or gameplay.

## Historical shadPS4 integration record

Updated: 2026-10-15. The user expanded the task from gate 1 to the complete
shadPS4 and Demonware integration, including friends, invitations and playable
multiplayer. MIT was selected for orbiSteamworks. Changes remain local and
uncommitted. Native-port implementation stays after shadPS4 gate 5.

Only one machine/account is available. The user confirmed that a second
machine is not currently available. This limits live acceptance, not development:
two-account races and grouped search cannot be claimed from local fixtures.
Game interaction remains human-operated.

The user clarified that the immediate deliverable is the proof of concept.
Keep the local shadPS4 fork as the test host; an unmodified official build plus
an external DLL/loader is a later distribution decision, not a current requirement.

## Work order

1. Complete gate 1 lifecycle coverage: **done**. 13 offline tests, SDK ABI
   audit, live Steam exit, explicit absent-client error; see the gate report.
2. Implement the anonymous helper's private pipe protocol and verified
   Demonware admission. Preserve connection-owned session cleanup, persistent
   replay rejection and online-ID binding. **Implemented; initial verification
   passes.** Full publisher `go test ./...` passes, plus a real bound-ticket
   validation through the native helper, Go provider and SQLite. Evidence:
   `runtime/auth-spike/publisher-admission-2149661675/result.json`.
   Verified HTTPS auth-handler integration subsequently passed. The first host
   observation below also establishes the actual game's encrypted lobby handshake.
3. Implement the versioned C API, one callback thread, retained responses,
   cancellation, identity and token requests. **Implemented.** Fourteen offline
   tests pass, including copied buffers, retained results, cancellation, timeout,
   shutdown and request limits. The final suite has fifteen tests; live C API
   checks also pass. Host guest-ABI/game acceptance remains separate.
4. Implement friends, namespaced lobby sessions and invitation state, with
   explicit guest membership commit and bounded multipart/binary payloads.
   **Implemented and locally checked.** Fifteen offline suites passed before
   the bulk-transfer revision. The real C API initialized Steam identity/friends
   and created a private lobby, but metadata publication failed. A 6,000-byte
   chunk failed at index 1; a 3,000-byte chunk failed at index 2. This disproves
   the original assumption that an image could be stored as many lobby keys.
   Private evidence: `runtime/auth-spike/c-api-platform-20261015T103039Z/`
   and `runtime/auth-spike/c-api-platform-20261015T103146Z/`.
   The implementation now uses one bounded metadata document plus
   owner-authenticated lobby-message transfers for large immutable payloads.
   CTR's captured image is 52,096 bytes; immutable data 5 bytes; changeable
   data 10 bytes. These are preserved, not discarded or truncated.
   **Revision verified:** all fifteen offline suites pass, including a
   two-instance platform fixture that transfers the complete image before
   the guest commits membership. Real C API creation/readback/update/delete
   passed in `runtime/auth-spike/c-api-platform-20261015T103815Z/`.
   A real 4,096-byte binary lobby-message round trip, including NUL bytes,
   passed in `runtime/auth-spike/lobby-chat-20261015T104047Z/`.
   Neither result proves remote visibility or two-account operation.
5. Integrate shadPS4 at NpHandler, WebAPI and NPAuth; preserve the default
   provider and recovered guest ownership. Add settings and opt-in build.
   **Both build modes pass.** The first build landed in the inherited
   nested runtime-output directory, leaving the lab executable unchanged.
   The local build helper now selects `shadps4/build/`; the final rebuild and
   default-provider check passed. No game was started.
   A subsequent build succeeded in the required `build/shadps4.exe` location,
   SHA-256 `4938498f512f805de29c1fe6017700edc9b7d189baea8a61f84e0ca447528171`.
   That hash is superseded by the final artifacts below.
6. Build all components, run fixture/negative tests and live single-account
   checks. Prepare the exact human-operated game and two-machine acceptance.

## Decisions and limits

- The helper is a child of Demonware using inherited stdin/stdout pipes. It
  opens no local HTTP or public game/query listener. Steam calls and callbacks
  belong to one thread. Unexpected stdout from Valve is redirected to stderr;
  the protocol uses a separate saved output descriptor.
- Steam validation is asynchronous. Immediate Begin OK never admits a player.
- Authentication owns a pending admission until the publisher ticket is
  claimed by a lobby connection. Only the pending admission has a bounded
  handoff window. The admitted connection has no elapsed-time cutoff.
- Each admission requires a new ticket. Native Steam determines ticket
  validity; no unproven 120-second Steam age rule is claimed. Canonical ticket
  hashes are consumed persistently, without a short replay-cache expiry.
  The publisher's existing 120-second *publisher ticket handoff* remains a
  separate rule. Host-issued tickets use the current helper identity when
  available; identity binding does not prove age or online-ID ownership.
- Online IDs are bound by the publisher to verified SteamIDs. The binding,
  consumed-ticket hashes and normal name lookup live in the instance's SQLite
  database, under a separate Steam trust namespace. No shadNet data is migrated.
- Existing bases: shadPS4 `0fdad68` (`feat/resolver-host-overrides`), publisher
  `c09908a` (`cleanup/code-quality`, includes later work beyond main).
  Preserve those changes and keep the shared Demonware protocol/matchmaker
  independent of the admission provider.
- shadPS4 AGENTS requests an external-contributor marker file. The workspace
  instruction explicitly prohibits that file; it will not be created.

## Acceptance status

Gate 1 passes for the recorded Windows setup. Gate 0 inventory is in progress.
Native-helper/provider/SQLite integration passed locally with account A.
The C API issuer passed the same real validation in
`runtime/auth-spike/publisher-admission-540227123/`. The test was then extended
through the actual publisher handler over verified local HTTPS: discovery,
signed auth response, publisher-ticket identity handoff, native admission
ownership and replay rejection passed in
`runtime/auth-spike/publisher-admission-1448771442/`.
At that fixture checkpoint, host glue had no game acceptance. The first actual
host observation below subsequently qualified the publisher HTTPS/lobby
handshake. Paired race, friends, invitations, grouped search and WAN acceptance
remain unqualified. The HTTPS fixture itself uses the production handler and an
ephemeral test certificate/signing key; it does not exercise shadPS4's HTTPS
adapter or the actual game lobby handshake.

## Final local PoC verification

The final cancellation review found that a GET which joined a lobby and
completed payload preparation could leave provisional membership behind if
the C API deadline won just after completion. The core now records that
provisional side effect for cancellation, while preserving membership that a
later game request has explicitly committed. Fixtures cover cancellation during
transfer, ignored late payloads, cancellation after read completion, recovery
through a fresh request and preservation of subsequently committed membership.
All fifteen offline suites pass after the fix; the public header also compiles
as C. No host tests were added, per that repository's instructions.

The final native Steam C API run passed at
`runtime/auth-spike/c-api-platform-20261015T111455Z/`: private create, creator
commit, 5-byte data and 52,096-byte image readback, 10-byte changeable-data update
and deletion. This remains local readback on account A.

The actual publisher executable and prepared loopback configuration passed
verified HTTPS discovery and anonymous helper readiness. Its private record is
`../nextendo-demonware/runtime/steam-poc/smoke-20261015T110508Z.json`.
The smoke process and helper were stopped after the check, with no game or
player connection. Normal operation has no elapsed-time shutdown.

Both the Steam-enabled and default-disabled shadPS4 builds pass. The final
Steam-enabled executable also exits successfully for `--help`; this verifies
process startup only. The full publisher suite, vet and executable build pass.
Final hashes and the cross-component source manifest are recorded at
`runtime/auth-spike/poc-final-20261015T111626Z/`:

| Artifact | SHA-256 |
| --- | --- |
| `shadps4/build/shadps4.exe` | `9a476ea5abb54133ebca061e6728a744a353fa2af6bbeb2af89f075d0feebc63` |
| Default-disabled build | `5f93d2ee542d274df9af545f165c47fc7a38876585cc5e7f1fd194123002d5a1` |
| Publisher PoC executable | `53a331210d545c2d4cfd36e9fdc7b766e7bc5edb8c0aa68562f81591db89d42e` |
| Native auth helper | `17b65e83f01735b8aff93eeef2569e335073a5fd89619cd3c0cf0653fad814b5` |
| C API platform check | `8991fa74f7dfcd451258dc58d78fd8b94e1011ba9c7ffa672a723dabd3ebf392` |
| Source manifest | `e4d161a3dabc3f4b3b5bdc7b8b0ee7ddd4ef9f64cd6d8b91631f95739ea11c4c` |

At the build-only checkpoint, profile A's Steam settings were prepared but not
activated. The subsequent first host observation below activated them. The launch operator
backs up settings on activation and uses a separate publisher database.
The earlier installed emulator was preserved at
`../shadps4/build/backups/before-steam-20261015T095941Z/shadps4.exe`, SHA-256
`67f07f2cfdd89975a171fa2fd31150c720249a51f0cc82d6d8106bc8e99392f5`.
The default build check qualifies compilation against the current source base;
it does not requalify the older NPAuth lab binary. See [LOCAL_POC.md](LOCAL_POC.md)
for launch/restore instructions and the remaining manual acceptance boundary.

## First host process observation

After the build/CLI checks above, the continuation started the prepared local
publisher and activated profile A through the existing launcher, with its
settings backed up first. The actual process loaded `steam_api64.dll` and
`steamclient64.dll`; guest NP initialization ran. No UI inputs were sent and
no human-observed Online-menu or race acceptance is claimed. The process and publisher were
left available for human interaction, without an idle shutdown.

The current game log records C++ exception code `0xe06d7363` while the process
remains live. The existing Windows vectored handler invokes emulator shutdown
logic for this code even before normal C++ exception dispatch completes. This
does not identify the throw's source. A 30-second debugger observation captured
no recurrence and detached without terminating the game. A subsequent
330-second observation also completed without a new C++ throw and detached;
the source therefore remains unknown. No source change or exception suppression
has been made on speculation. Private records:

- `../shadps4/out/profiles/A/steam-poc/startup-observation.json`
- `../shadps4/out/profiles/A/steam-poc/observation-20261015T112845Z/`
- `../shadps4/out/profiles/A/steam-poc/observation-20261015T113057Z/`

The publisher log from that actual game process now proves the guest-to-publisher
path: at 11:21:28 UTC, HTTPS auth returned 200/code 700, the lobby accepted 0x82
authentication and established the encrypted session with 0x83. Subsequent
publisher operations include server time, Pit Stop inventory/balances and a
statistics upload. At 11:27:08 UTC, `bdAsyncMatchMaking.setPlayerInfo` succeeded
and the game requested its matchmaking player token. The separate PoC database
contains one Steam binding and one consumed-ticket hash. The bound publisher
PID matches the actual host's Steam ID, compared privately without publishing
either value. No synthetic auth client was run against this PoC database.

This establishes the real shadPS4 NPAuth/HTTPS adapter and original game's
encrypted publisher handshake, beyond the earlier handler fixture. It does
not establish a successful platform-session creation, human UI acceptance,
search or race. The unresolved logged C++ exceptions also prevent claiming
clean startup. The earlier "no game launch" statements describe their respective
checks only. No UI inputs were sent by the agent.

## Inventory and current party state

The bounded original-caller analysis now identifies CTR's NPAuth client ID by
its length, SHA-256 and original decoding operands, and confirms the exact
`openid,openid:duid` scope. The private reproducer ran against the pinned owned
executable without decoding the client secret or exporting original bytes.
The historical local session capture independently gives seven `owner-bind`
creates, each with a 53,161-byte body: JSON 417, image 52,096, immutable data 5,
and changeable data 10 bytes. The source/capture identities and remaining
inventory gaps are recorded in [GATE0_CONTRACT_INVENTORY.md](GATE0_CONTRACT_INVENTORY.md).

At 11:54 UTC on 2026-10-15, profile A's process identity was confirmed live.
A read-only snapshot matched its loaded caller to the original executable and
found search state 0 with no game party, creation task, registration task or
retained registration session. Selected error lists were empty. The publisher
operator was also confirmed live through its existing process session. No
replacement process, UI input, memory write or automatic shutdown was used.
The earlier successful peer-registration log remains valid, but the snapshot
cannot explain why its local state is no longer retained or identify the menu.

At that checkpoint, the next decisive step was a human-operated Online-menu
attempt correlated with the live logs. The user subsequently answered the
screen-state question with "Menu Online", confirming that limited UI boundary.
Platform-party creation, an accepted search and gameplay are still separate.
A second
independent account/host is still unavailable. Neither missing input is replaced
by fixture results, historical shadNet acceptance or additional quiet polling.
The full integration goal remains incomplete. No product source or running
binary changed during this inventory pass; only private diagnostics and
documentation changed.

After the user's Online-menu confirmation, a manual Matchmaking attempt was
requested. A new bounded exception observer and read-only party-state sampler
were started against the same verified process to correlate that attempt.
Their deadlines stop the diagnostic tools only; they do not stop the game or
publisher. No automated game input was sent. The Windows computer-use plugin
identified the game window, but its capture approval timed out and no screenshot
was obtained; the Online-menu observation is the user's report.

Both 180-second diagnostic processes completed normally and the game remained
live. `observation-20261015T121800Z/exceptions.log` recorded zero new C++
exceptions; `party-watch-20261015T121915Z.jsonl` retained only its initial
unchanged snapshot. The publisher log also remained unchanged. These quiet
observations do not prove that the requested manual Matchmaking action occurred
or resolve the earlier exception. Its outcome is still awaiting the user's
report; no automatic retry or game restart was performed.

## Original multipart framing correction

The user subsequently reported "Ricerca giocatori in corso" and, after manual
Online reentry, "Nuovamente ricerca giocatori". Correlated observations showed
party creation starting and failing, followed by successful publisher peer
registration but no publisher search request. The visible search text alone
therefore did not establish an accepted search.

The repeated manual Online entry captured `osw::Error`, code 20,
`Invalid multipart request`. Private evidence:
`../shadps4/out/profiles/A/steam-poc/observation-20261015T122922Z/exceptions.log`
and `party-watch-20261015T122550Z.jsonl`. Both observers finished and detached;
the game and publisher stayed running. No UI input or memory write was used.
This attributes the reproduced party-creation exception, without claiming that
every earlier exception necessarily had the same source.

The original Toolkit's length accounting explains the failure: it appends a
38-byte closing delimiter but submits the saved length before that delimiter.
The evidence and caller pin are in GATE0_CONTRACT_INVENTORY.md. The library's
multipart parser now accepts a final part that ends exactly at its declared
Content-Length. It keeps bounds and rejects truncated data, extra bytes,
partial delimiters and final parts without an explicit length. Host exception
handling, the guest ABI and publisher matchmaking were not changed.

A synthetic four-part 53,161-byte fixture, including binary NULs, failed before
the correction and passed afterward. It also drives creation through the
platform fixture and verifies data readback. All fifteen offline suites pass;
before/after logs are under `runtime/inventory/multipart-20261015/`. The updated
Steam host builds separately at `../shadps4/build/steam-candidate/shadps4.exe`,
SHA-256 `ed93fe0061dd85975ca7ace96002addd23904ed0193e6e9bfc27feb9c5573fb1`.
The running executable was not replaced during compilation. A fresh human
Online/search check is still required before accepting the repaired game path.

The user then closed CTR/shadPS4 and left Steam open. The previous executable
and game log were saved under
`../shadps4/build/backups/before-multipart-20261015T124203Z/`, the corrected
executable was installed at the shared `build/shadps4.exe`, and profile A was
relaunched. The existing publisher/helper and separate database stayed running.

Read-only observation matched the loaded original caller and recorded a valid
party/session at 12:42:30 UTC. After the user manually selected Matchmaking,
the publisher registered search 1 with one seat at 12:42:51 UTC (local log
14:42:51). Search state advanced to 7 and registration was ready. The user
reported "Ricerca giocatori in corso". Unlike the earlier stalled attempt,
this is an actual publisher search.

The user then canceled, returned through the main menu, reentered Online and
searched again, reporting no error. The log records session deletion/recreation
and search 2 with one seat at 12:44:39 UTC. The read-only sampler confirms the
party/session was recreated and search state returned to 7. Neither the fresh
game log nor the bounded exception observation recorded the earlier C++ error.
The test does not establish a matched lobby, peer transport or a race.

Private evidence is under profile A's `steam-poc/`:
`party-watch-20261015T124228Z.jsonl`, `observation-20261015T124228Z/` and
`multipart-install-20261015T124203Z.json`. The publisher log is
`../nextendo-demonware/runtime/steam-poc/server-20261015T112056Z.log`.
The updated CLI smoke and source manifest are at
`runtime/auth-spike/poc-final-20261015T124630Z/`; manifest SHA-256:
`524f30a03a904fe0cd87f6e1eaaf5e2cab25cc660a73186a91e19d90d9e68034`.
No game UI input, process-memory write, commit or publication was performed.
The full goal remains incomplete: paired friends, invitations, grouped search
and racing need an independent second Steam account/host. Host first-chance
exception handling was not changed, so this flow also does not qualify all
negative-input/error paths.

Both final 300-second observers completed normally, the debugger detached with
zero C++ exceptions, and the game and publisher remained live. The allowlisted
acceptance record, including source-log prefix hashes/lengths, is
`runtime/inventory/multipart-20261015/game-acceptance.json`. No elapsed-time
shutdown was added to either service or game.

## Expected validation errors without first-chance exceptions

The next continuation addressed a remaining host-integration issue exposed by
the multipart diagnosis. A caught C++ exception still reaches the original
Windows vectored handler before the library's catch block; merely catching it
at the C API boundary does not prevent that handler's shutdown side effects.
The core now returns explicit failure values for invalid multipart framing,
URL escapes, decimal IDs, online IDs, oversized emitted credentials and failed
presence publication. Presence startup rolls back its partial publication.
Invitation `usedFlag` input is checked for a boolean before reading it. The
standalone credential parser retains its throwing interface for command-line
tools and the separate server helper; embedded paths use the shared checked
helpers instead. The public C API layout and host signal handler are unchanged.

Tests install a read-only Windows first-chance observer in their own process.
The new API/platform cases reproduced the exception before the change. They
now verify explicit rejection with no observed C++ exception; token cases also
cover the exact 4,096-byte bound, overflow and invalid identifiers. All fifteen
offline suites pass. This is stronger than merely checking a caught exception
or an error return, but remains an offline test, not game acceptance.

Private evidence: `runtime/inventory/expected-errors-20261015/`, including
before/after test logs and source hashes. The Steam host compiles and its CLI
smoke passes. Candidate SHA-256:
`c8372909a0c2c4683551466597a2f9051443fdde003f7bd6bbefcd5fc627f8ad`, at
`../shadps4/build/steam-candidate/shadps4.exe`. It is **not installed**; the
human-accepted multipart build `ed93fe00...` and its publisher/helper remain
running. Standalone outputs were placed under `build/expected-errors/` to avoid
replacing the active helper.

At that core-only checkpoint the error-path audit was incomplete. A bounded,
separate-process probe through the public C API with a deliberately absent
Steam library reported signed out and reason 10 correctly, but still observed
a first-chance C++ exception in the native loader. Reproducer:
`out/runtime-error-audit.cpp` and `.cmd`. It neither loads Steam nor touches the
running game. Native loader/initialization, ticket and callback-ABI failures
therefore required the following continuation.

### Native runtime failure propagation

The embedded Steam backend now uses checked runtime creation, client/social
initialization, ticket issuance and callback polling. Missing files/symbols,
client-unavailable and signed-out states retain distinct reason codes. Invalid
UTF-8 paths, impossible friend/member counts and incompatible callback payloads
are explicit failures. The standalone tools and helper retain their existing
throwing convenience interfaces; the C API uses the checked forms.

Ticket failures cancel any returned handle, publish no credential and leave
retry available. Fatal runtime failures release native resources, report signed
out and terminate the library owner loop, completing outstanding requests with
errors. Already completed buffers retain their documented ownership. Partial
presence publication is rolled back. Callback storage is released even when
its payload is incompatible. No new inactivity or elapsed-session cutoff was
introduced.

All **27 offline suites pass**. Twelve added native-runtime cases exercise the
public C API or checked runtime with the synthetic API library, including
missing DLL/symbols, unavailable client/social interfaces, signed out, failed
presence, invalid counts, failed ticket issuance (including retry), callback
ABI failure and invalid Windows UTF-8 paths. The fixture verifies callback and
ticket cleanup; the Windows observer confirms no first-chance C++ exception in
these expected failures. A core fixture separately verifies that a fatal
backend failure completes pending requests. The formerly failing missing-DLL
probe now reports `signed_in=0 reason_code=10 first_chance_exception=0`.

The full Steam-enabled host build and CLI startup pass. Built candidate:
`../shadps4/build/steam-candidate/shadps4.exe`, SHA-256
`5f8cffcec1b582606c1ee7e778a9a68aab87f3daed72aee46bf2fb46e10faa1a`.
It supersedes the core-only candidate above. At the offline checkpoint it was
not installed; the multipart build `ed93fe00...` remained active until the user
confirmed manual closure. Standalone outputs remain under
`build/expected-errors/`, preserving the active helper.

Evidence: `runtime/inventory/expected-errors-20261015/native-final.json`,
`native-after-fix.log` and `native-source-manifest.json`. The earlier
`result.json` and before/after logs preserve the core-only checkpoint. These
offline checks do not establish paired friends, invitations, grouped search or
a race; an independent second Steam account/host is still unavailable. The
global host exception handler, guest ABI and publisher matchmaking are
unchanged. No game input, process-memory write, commit or publication was used.

### Installation and renewed game search

After the user confirmed closure with Steam left open, the candidate was
installed at `../shadps4/build/shadps4.exe` at 13:29:41 UTC. The previous
executable and game log were backed up under
`../shadps4/build/backups/before-expected-errors-20261015T132941Z/`. The shared
executable hash matches `5f8cffce...`; profile A alone was relaunched. The
existing publisher/helper remained running and passed verified HTTPS discovery.

The publisher logged the new authenticated encrypted handshake at 13:29:50 UTC.
Read-only observation verified the original loaded caller and a valid game
party/session. The human then reported the search screen without errors, and
the publisher registered search 3 with one seat at 13:31:01 UTC. The game
reached search state 7 with publisher registration ready. Both 300-second
observers completed, the debugger detached with zero observed C++ exceptions,
and the game and publisher remained live. Cancellation/main-menu reentry on
this exact build was initially awaiting the requested manual action/report;
the subsequent verification is recorded below.

Private evidence: `runtime/inventory/expected-errors-20261015/game-acceptance.json`,
profile A's `steam-poc/expected-errors-install-20261015T132941Z.json`,
`party-watch-20261015T133043Z.jsonl` and
`observation-20261015T133040Z/exceptions.log`. The acceptance record pins the
observed log prefixes by byte length and SHA-256. Earlier offline records keep
their original installation status as historical checkpoints.

Separately, five historical bridge logs now supply a reproducible route/status
inventory covering friends, session create/read/update, invitation sending,
membership commit/leave and entitlement requests. See GATE0_CONTRACT_INVENTORY.md.
This narrows the historical inventory gap without claiming response sizes whose
logging semantics are unproven or current Steam invitation/gameplay acceptance.

### Current-build manual reentry accepted

The user initially selected the error response, then promptly corrected it:
the flow was working and no error was currently shown. No error message or
code was provided, so this does not establish a reproduced defect.

The same emulator process remained running. The publisher registered search 4
with one seat at 14:53:38 UTC and search 5 at 14:53:53 UTC, with session
deletion/recreation between them. A read-only snapshot at 14:54:37 UTC matched
the owned original caller and verified search state 7, a valid party/session,
ready publisher registration and empty selected global/user error lists. The
current game log contains no `Unhandled Exception` entry. The earlier debugger
observation had already ended: its zero first-chance exception count is not
claimed as continuous coverage of this later reentry.

This accepts the requested single-account cancel/main-menu/reentry/search
check on installed build `5f8cffce...`, over an hour after the first search in
the same process. It does not establish paired friends, invitations, grouped
search or racing. The game and publisher remain running without an automatic
session cutoff.

Evidence: profile A's `steam-poc/party-state-20261015T145437Z.json` and publisher
log lines 145–154; updated
`runtime/inventory/expected-errors-20261015/game-acceptance.json` pins their
observed contents. The preceding record is retained as
`game-acceptance-before-reentry.json`. No binary or configuration changed for
this acceptance update.

## Native Steam overlay request contract (2026-10-16)

Implemented additive operation `OSW_OVERLAY` (9) and event
`OSW_OVERLAY_CHANGED` (8), preserving all public structure layouts. The Steam
worker resolves optional Friends/invitation panel calls and the Utils overlay
availability query. Core code allows invitation panels only for a compatible,
committed party whose actual provider members include the local user. Invisible
room directories, foreign namespaces and ambiguous implicit selection are
rejected. Lab providers explicitly report unsupported. HTTP 204 records only
dispatch; it is never evidence that a user sent or accepted an invitation.

The separate `build/steam-overlay/` Release build produced static and shared
libraries successfully with MSVC 19.50. Its initial 35-test suite passed 34
tests inside the sandbox, including the three new overlay fixtures. The one
loopback UDP cross-process test failed at proxy receive in that sandbox and
passed when rerun on the authorized host (15.87 seconds). Fixtures cover the
copied worker request path, unavailable/missing-interface behavior, native call
arguments, callback lifetime/size/boolean/AppID checks, active-state events and
party namespace/membership gates. A subsequent small change adds explicit lab
overlay rejection assertions to the cross-process fixture and stops optional
resolution after a required social export has failed; final integration checks
are recorded with the combined overlay/persona candidate.

A private compile-time audit against the user's SDK 1.65 headers verified the
independent callback-331 declaration: size 12, alignment 4 and field offsets
0/1/4/8. The audit compiled without running Steam. Provider SDK files remain
under ignored `out/` and are not distributed. Public documentation references
and implementation-choice classification are in [SOURCES.md](SOURCES.md).

No live Steam client, game window or native overlay was exercised for this
library change. No candidate DLL was installed. Rendering, native overlay
availability after early host initialization and real cross-account invitation
delivery remain live acceptance gates; these offline results do not satisfy
them.

## Combined overlay and live persona candidate (2026-10-16)

The worker now observes sanitized Steam persona changes when no online-ID
override is configured. It updates the copied C API identity, rich presence,
P2P self-name and already committed party member labels. A name-only member
event uses change kind 3; provisional membership remains provisional. Cached
invitation sender labels follow the current compatible friend record.

The optional local child contract is explicitly negotiated as
`owner.name.v1`; see [COMPANION_PROTOCOL.md](COMPANION_PROTOCOL.md). The private
name update never rotates or retransmits the bootstrap capability. A legacy
child freezes the context's initial label and reports lack of support through
P2P status. The matching publisher updates admission, persisted names and copied
presence records while retaining PID, subject, database namespace and existing
ticket/session objects. Late tickets cannot restore an old display label.

An isolated MSVC Release build in `build/rename-candidate/` produced both static
and shared libraries from base `3b3bfd49ddcbe4aba4f96f8e83a08c7cd07d8e2b` plus
the uncommitted overlay/persona changes. All 36 CTest cases passed on the
authorized host (22.61 seconds), including the final overlay error guard and
explicit lab rejection checks. The initial sandbox run passed 35 cases; its
UDP fixture received no proxy datagrams and passed in the host rerun. No test
was reclassified as Steam or game acceptance.

The synthetic Steam DLL/child fixture covers persona sanitization, copied
identity-change events, stable account/capability, P2P name resolution,
capability visibility, modern/legacy child negotiation and a fixed configured
name. The platform fixture verifies that a prepared member is not committed
by a rename and that a committed rename emits update rather than join/leave.
The final test-only additions were rebuilt and passed separately (2.63 seconds).
Publisher tests additionally cover rename before the first authentication,
malformed/foreign/unnegotiated pipe messages and concurrent admission, late
ticket registration, name persistence and retained connections. Its complete
Go test suite, `go vet ./...`, candidate build and targeted race detector for
local admission, companion and presence passed. Windows ThreadSanitizer could
not allocate its arena inside the sandbox; the same tests passed on the host.

Candidate SHA-256 pins (not installed):

- `build/rename-candidate/orbisteamworks.dll`:
  `508a135f34052108481c7427a3e8633b7d7b9a48794a071feef41ad2243e3786`.
- Publisher `bin/demonware-server-rename-candidate.exe`:
  `0fc70159dad030a978f7126e25368fec4e911cc8d418980f7268fe290a027369`.

These are implementation fixtures (I). No live Steam persona was changed,
no game UI was driven, no installed binary was replaced and no live rename or
cross-account overlay acceptance is claimed. Existing opaque game payloads
are not rewritten to guess embedded names. Linux runtime remains unqualified.

## Unilateral friend UDP and copied friend changes (2026-10-16)

The original-game Lab pair exposed a gap hidden by the previous room-first
fixtures: only the joining friend reads the advertiser's publisher record, so
the advertiser had no reverse friend proxy for the first room-zero datagram.
Serving a live private advertisement now grants that authenticated, compatible
friend a reverse UDP path. Allocation remains lazy; source endpoint checks and
directory membership checks are unchanged. Withdrawal and loss of compatible
friend presence revoke the grant. Neither re-publication nor coming online
again resurrects it without a new successful query. An explicit local friend
endpoint is independent authorization. No room or party membership is invented.

Friend callbacks now include copied account/name details for online, removal
and rename transitions. Notification history is independent of list requests,
so an intervening GET cannot swallow an event. Provider offline state overrides
stale matching rich presence. The generic refresh event remains compatible.
Invitation recipient lookup gives an exact canonical account ID priority over
another friend's numeric name; numeric-name lookup remains supported when no
known account matches. Public ABI layouts remain unchanged.

MSVC static/shared Release build `build/preparty-candidate/` passed all 36 CTest
cases (24.88 seconds). Coverage includes unilateral pre-room UDP, withdrawal,
re-publication, offline/online transitions, failed advertisement replies,
withdrawal with queued incoming UDP, late friend startup and rename after GET,
duplicate suppression and exact numeric recipient binding. The cross-process
fixture now uses `.121/.122/.123` loopbacks; an initial `.21` bind collision
failed without stopping another process and prompted that fixture isolation fix.

The extended actual two-child publisher probe failed with the previous DLL
`508a135f...` at the fixed step `unilateral pre-room delivery`, with no directory
room events. With the new candidate, both default and `--party` probes passed:
the default performs bidirectional UDP before any platform party or directory
room; both exercise room transport and actual follower disconnect/seat removal.
The publisher executable remained unchanged at SHA-256
`0fc70159dad030a978f7126e25368fec4e911cc8d418980f7268fe290a027369`.
Candidate DLL SHA-256:
`8623d614894d7ac7781e5e2c99ef67986c612211de968b087ff66faebcd8132e`.

Private evidence: `out/preparty-candidate-build.log`; publisher
`.cache/p2p-probe/preparty-before-fix.log`, `preparty-after-fix-default.log` and
`preparty-after-fix-party.log`. The probe uses only its `.60/.70` publisher and
`.61/.71` game loopbacks, unique hub/database directories and owned child handles.
These are implementation fixtures (I), not original-game, two-account Steam or
WAN acceptance. No active game, Steam client or installed binary was changed.
