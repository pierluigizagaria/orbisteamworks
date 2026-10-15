# Gate 1: anonymous AppID 480 ticket validation

Date: 2026-10-15 (Europe/Rome). Scope: the runtime loader and two standalone
authentication tools. No host, publisher, game, lab executable, account or
network setting was changed. No commit, push or publication was performed.

## Build and execution identity

Both programs ran on the user's same Windows 11 Pro x64 machine, build 26200.
Steam was already running and signed in. The client is account **A**; anonymous
game-server identities are **S1**, **S2**, etc. These are pseudonyms, not SteamIDs.
The server loaded `C:\Program Files (x86)\Steam\steamclient64.dll`; it did not
use SteamCMD or a downloaded redistributable server installation.

| Item | Pin |
| --- | --- |
| User-supplied SDK | Steamworks **1.65**, `Readme.txt`: 23 July 2026 |
| SDK ZIP SHA-256 | `8c42792e09100988e31e3dc069de2eb1bc60702a0445bb37298ba0c54067c202` |
| Valve `steam_api64.dll` SHA-256 | `e6d9bafb9a41e42fba7b21553db49f8719027af3f0deb23ff86cfc44d60e776d` |
| DLL provenance | Local SDK `redistributable_bin/win64`; Authenticode status Valid, signer Valve Corp. |
| Loaded `steamclient64.dll` SHA-256 | `e9045710ec7ee47d7b7488d1c5dda2d933dec7e251e965967a5c48f4df68eb4e` |
| Compiler | MSVC 19.50.35728.0, x64, C++20, Release; Ninja/CMake from installed Visual Studio 18 Community |
| Git identity | New repository with no HEAD commit; uncommitted working tree, as requested |
| Matrix client SHA-256 | `7dfb6390905eabff6539365d7cec4150aa1cdb4d0967db7c5c385416c77cabf8` |
| Matrix server SHA-256 | `63b42602a4be263b3aa3a06479a45a5bed75b65e6f782e46da6e0f42689ac05c` |
| Pre-lifecycle client SHA-256 | `5cb10a7abf28cb95012e4e62299bffdff5bb569ca23aef94b11a02005977aede` |
| Pre-lifecycle server SHA-256 | `bf2589fba543fc2968ce829411bc5b1163cf2985bd702f8360580a7c2d927d61` |
| Pre-lifecycle source-manifest SHA-256 | `56a36a22ad27e95e0861351b40e805e1c5170706483604958dff17cbe60b1c07` |
| Lifecycle client SHA-256 | `4aa151c91b897188bb05e5237432d7eb1602bb297547dc5d20e87117c92fa119` |
| Lifecycle server SHA-256 | `36d60a60f87b054dd439d44b644e2e8a3a2341c670890a2dea85bc79f5f18082` |

The complete private matrix is under
`runtime/auth-spike/matrix-20261014T233141Z/` (UTC directory name). Its
`results.json` masks identities; raw logs and token files remain gitignored.
Private local build/audit helpers are in `out/`. Token files must not be
published, even after cancellation.

After the matrix, the console stop flag was made lock-free atomic because
Windows invokes its console handler on another thread; the test-only API
fixture was moved under `src/steam/testing/`. The final build passes all 11
tests and the SDK ABI audit again. A new live authentication with the final
binaries passes, with validated and owner IDs both equal to A, followed by
clean client shutdown. Evidence and the source-file hash manifest are in
`runtime/auth-spike/final-check/`. An additional private comparison confirms
all seven successful matrix callbacks match their issuer and owner A.

A subsequent lifecycle review added the SDK `SteamShutdown_t` notification.
The client cancels outstanding tickets and exits cleanly when Steam requests
shutdown; a pending ticket completes with explicit error 24. All **13** CTests
and the SDK ABI audit pass. Live evidence is in
`runtime/auth-spike/lifecycle-20261015T090830Z/`: fresh authentication matched
A/owner A, Steam exited normally, and the client received the shutdown event
and exited with code 0. The validator observed later cancellation (6) and
completed its full 90-second watch with code 0. With Steam absent, client
initialization returned code 12 / init result 2 without writing a token.
The installed Steam client was then reopened without a login command or
credentials. No game or system/network setting was changed.

## Evidence and offline checks

- **P, provider ABI:** inspected the user's SDK headers, then independently
  declared only the needed flat API. A private compile-only SDK comparison
  checks all 26 function signatures (opaque interfaces/enums normalized), six
  payload/identity layouts, relevant member offsets, callback IDs and constants.
  It passes. Production targets have no SDK include path or import library.
- **P, initialization:** `SteamAPI_InitFlat` and
  `SteamInternal_GameServer_Init_V2` are exported C functions in SDK 1.65.
  `SteamGameServer_InitEx` is inline and is not a runtime symbol. The server
  supplies the double-NUL terminated `SteamGameServer015` version check.
- **P, callback ABI:** the SDK packs `CSteamID` itself to alignment 1.
  `ValidateAuthTicketResponse_t` therefore has size 20, alignment 4 and owner
  offset 12 on Windows too. An initial incorrect uint64 alignment was caught by
  the ABI audit and the runtime payload-size guard, then corrected before the
  accepted matrix. The initial attempt was not counted as a successful gate.
- **P/I, loader:** one thread initializes/pumps manual dispatch, copies payloads
  before `FreeLastCallback`, and owns shutdown. Required symbols are resolved
  before initialization. No native C++ callback macros are used.
- **Offline:** 11 CTest cases pass: token contract (20 checks), missing library,
  missing symbol, unavailable client, signed-out client, server-init failure,
  server-logon failure, logon timeout, ticket timeout, ticket rejection, and
  callback-size mismatch. Failure callbacks come from a test-only library
  written here, with no Valve code or network access. Those simulations do not
  qualify real outages or signed-out Steam behavior.
- **Binary inspection:** neither executable's PE import table lists Steam.
  Only Windows/MSVC runtime imports are present. The runtime DLL is installed
  locally under ignored `build/`; it is not a source/distribution dependency.
- **Git hygiene:** SDK, DLL, generated builds, audit objects, tokens, logs and
  `steam_appid.txt` are ignored. No files were staged or committed. The
  `shadps4/`, `shadnet/` and `nextendo-demonware/` repositories were not edited.

The following are **live local acceptance observations**, not PS4 F/C evidence
or guarantees about other machines, accounts, future Steam versions or WAN.
The `osw1` envelope, online-ID syntax and helper policy remain replacement
choices (**I**).

## Live matrix

`Begin` is the immediate result; `Validate` is the asynchronous callback.
Each independent case used a fresh issuer process/ticket. Clients remained
alive while their validators ran, except for the explicit exit case.

| Case | Measured observation |
| --- | --- |
| Fresh ticket, account A | Begin **0 OK**; Validate **0 OK**, SteamID=A and owner=A |
| Same ticket while session active | First validation OK; second Begin **2 DuplicateRequest** |
| Same ticket after EndAuthSession, same server | Begin **0** again; Validate **6 AuthTicketCanceled**; rejected |
| Same ticket in a new server process after the first ends | Begin **0**; Validate **7 AuthTicketInvalidAlreadyUsed**, owner=0; rejected |
| First validation after at least 60 seconds | Begin **0**; Validate **0 OK**, A/owner=A |
| First validation after at least 5 minutes | Begin **0**; Validate **0 OK**, A/owner=A |
| Client CancelAuthTicket, established server watching | Initial Validate **0**; later Validate **6 AuthTicketCanceled** with A/owner=A. Server kept observing until its requested watch ended |
| Cancelled ticket in a new validator | Begin **0**; Validate **7 AuthTicketInvalidAlreadyUsed**, owner=0; rejected |
| Issuer program exits cleanly, Steam stays running | Initial Validate **0**; shutdown cancels its ticket; server receives later Validate **6** and remains alive for its watch |
| Steam application exits | Normal exit observed; issuer receives SteamShutdown and exits 0; validator receives later Validate **6**, remains alive and completes its 90-second watch |
| Wrong SteamID in envelope | Begin **1 InvalidTicket**; rejected |
| Corrupt ticket (last byte flipped) | Begin **1 InvalidTicket**; rejected |
| Ticket bound to validating server S1 | Begin **0**; Validate **0 OK** (positive control) |
| Ticket bound to another live server S1, validated by S2 | Begin **0**; Validate **10 AuthTicketNetworkIdentityFailure**; rejected |
| Token length, bound and unbound, online ID OswTest | **502 ASCII characters**, ticket **234 bytes**; fits the 4096-character limit |
| Missing library | Explicit code 10, verified offline |
| Steam client not running | Real Valve DLL returns init result **2**, explicit code **12 CLIENT_UNAVAILABLE**, no token file |
| Signed out; init/logon failure; timeout | Distinct codes 13/14/15/16 verified with offline fixtures; no account, firewall or network settings were changed |

Nonzero exit 21 in rejection cases is expected. Later revocation events do not
turn an initially successful watched session into an automatic disconnect.
No artificial lifetime or idle cutoff is proposed for real game sessions.

## Non-advertising and process lifecycle

The server calls `SetAdvertiseServerActive(false)` before `LogOnAnonymous`,
sets dedicated mode, uses authentication mode 2 (without VAC), binds to
127.0.0.1 and passes query-port-shared `65535` with game port zero. SDK 1.65
documents advertising as false initially and shared mode as opening no query
socket. The tool never creates a game listener or forwards discovery packets.

A live host endpoint inspection while three validators were running found
only their ephemeral UDP endpoints on **127.0.0.1** and no TCP listening socket.
No public listener/query port was observed. Spacewar browser enumeration was
not used as a separate check; the guarantee here is the explicit API setup,
absence of a query socket and observed local bindings.

Normal completion, rejection and exceptions unwind the same owner: outstanding
auth sessions end, anonymous server logs off, then API shutdown runs. Client
stop cancels tickets. Test orchestration used stop files for clean client
shutdown. Some Valve shutdown logs include a work-queue diagnostic; the
processes exit normally and session cleanup is explicitly requested first.

## Interpretation and admission requirements

1. **Wait for the matching callback.** Immediate Begin OK is provisional.
   Replay, cancellation and identity-binding rejection can all arrive later.
   Admit only after Validate OK for the expected SteamID, with a valid owner.
2. **Issue one new ticket per admission.** An established session is keyed by
   SteamID in this API. Duplicate active authentication is refused; future
   helper connection ownership must handle reconnect/overlap explicitly.
3. **Replay was refused in the tested cases.** This is evidence for this setup,
   not a universal replay-retention duration. Use canonical ticket bytes for a
   helper-side consumed-ticket hash cache and coordinate it across any helper
   instances. Upper/lowercase hex variants must map to the same bytes.
4. **Do not assume a 120-second Steam ticket lifetime.** Unused tickets at
   60 seconds and five minutes both succeed. The `osw1` envelope contains no authenticated issue
   time. A helper request deadline is not proof of ticket freshness. A replay
   cache with an arbitrary 120-second TTL does not, by itself, establish the
   freshness rule in the design. That contract must be settled before publisher
   integration; client-supplied timestamps are not a solution.
5. **Server binding works in this setup.** Use the intended helper's identity
   for ticket issuance when the future discovery contract is defined. Anonymous
   server identities can change on restart. Binding does not establish the
   age of a ticket or bind an online ID: the publisher still owns that binding.
6. **Later events are observations.** Cancellation/logoff notifications must not
   become arbitrary player disconnects; fresh credentials are for new admissions.

## Reproduce and remaining manual cases

See [the complete tool instructions](../tools/auth-spike/README.md) for exact
build/run commands, repeats, age tests, corruption, binding, missing-library
and Steam-exit procedures. Remaining optional environment qualification:

- Run the real client with Steam signed out, and observe an actual
  anonymous-logon failure in a controlled environment. Offline errors already
  have deterministic coverage; no firewall or system setting was changed.
- Linux, a server without a local Steam client, another account, host NPAuth,
  publisher admission, multiplayer and WAN are separate, unqualified work.

Official provider references (P): [ISteamUser](https://partner.steamgames.com/doc/api/ISteamUser),
[ISteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer),
[API overview](https://partner.steamgames.com/doc/sdk/api). Local SDK ABI and
live results, rather than generic authentication assumptions, support this gate.

## Verdict

**PASS** for gate 1 on this Windows/SDK/account setup. Runtime-loaded client
ticket issuance, anonymous AppID 480 validation, identity, duplicate/end-session
behavior, cancellation and age are observed. The matrix also verifies negative
identity/corruption and positive/negative server binding. No alternative
authentication route or Web API key was introduced. Remaining manual cases and
the freshness contract are explicitly listed above; no later gate is qualified.
