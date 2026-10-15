# Gate 1: AppID 480 session-ticket spike

Two console programs share the runtime loader in `src/steam/`. They require a
64-bit C++20 compiler and CMake 3.20 or newer. The normal build neither includes
SDK headers nor links a Steam import library. AppID 480 is fixed. These tools
do not implement the host C API, publisher admission, matchmaking or game UI.

## Prepare and build (Windows)

1. Download Steamworks SDK 1.65 from [Valve](https://partner.steamgames.com/)
   using your own account and accept its terms. Place the ZIP in `out/` and
   extract it to `out/steamworks-1.65/` (containing `sdk/`). Do not add SDK files
   to Git. The ABI subset was checked against that version; older DLLs may lack
   required symbols.
2. Open an **x64 Native Tools Command Prompt** with CMake available, then start
   PowerShell in it. Change to the `orbisteamworks` repository root. Build:

   ```powershell
   cmake -S . -B build -G Ninja -DCMAKE_BUILD_TYPE=Release
   cmake --build build
   ctest --test-dir build --output-on-failure
   ```

3. Install your runtime DLL next to the programs:

   ```powershell
   Copy-Item out/steamworks-1.65/sdk/redistributable_bin/win64/steam_api64.dll build/steam_api64.dll
   ```

   Alternatively pass `--library` with its explicit path to each program.
   There is no search of the current directory or arbitrary PATH entries.
   `SteamAppId=480` is set in each process before initialization; no
   `steam_appid.txt` is necessary. The copy is local and gitignored; do not
   bundle it with sources or executables you distribute.
4. Start Steam yourself and sign in. No tool signs in, asks for credentials,
   creates a key, downloads SteamCMD or starts another game.
5. From the repository root, start the client in one PowerShell window:

   ```powershell
   .\build\osw-auth-client.exe
   ```

   Wait for `Ticket ready`. Keep the client running. It writes
   `runtime/auth-spike/ticket.txt` atomically and prints only the token length,
   ticket byte count and first/last eight hex characters. Its SteamID is printed
   locally. Never paste a token or unredacted log into a report.
6. In a second PowerShell window at the same root:

   ```powershell
   .\build\osw-auth-server.exe
   ```

   Success requires **both** `BeginAuthSession result=0` and
   `ValidateAuthTicketResponse result=0`, with the client SteamID. The server
   logs on anonymously in authentication mode, with dedicated-server status,
   advertising disabled before logon, loopback binding, game port zero and
   query-port shared mode (`65535`). It owns no game/query socket and forwards
   no discovery packets. It must never be listed in the Spacewar browser.

Use Ctrl+C for clean shutdown. The client cancels outstanding tickets. The
validator ends every accepted auth session, logs off and shuts the API down,
including on exceptions. `--stop-file PATH` provides the same graceful stop
for hidden test processes. Remove a prior stop file before reusing its name.
The client also shuts down cleanly on Steam's explicit shutdown notification.
If it arrives while a ticket is pending, the request fails with code 24 and
no token is published. The server keeps observing established auth sessions.
Do not reuse a stale token file when starting the validator before the client.
The default request timeout is 30 seconds; waiting for a token file is bounded
to 60 seconds. The client has **no elapsed-time session cutoff**.

## Test matrix

Run each independent case with a newly started client and a unique `--token`
path. Stop its client after the case. In the examples below `--token` must be
the same on both programs. A later nonzero callback is an observation and does
not terminate an established session. Exit code 21 is expected for deliberate
rejection cases, including the second attempt in the duplicate tests.

| Case | Client flags | Validator flags |
| --- | --- | --- |
| Fresh | `--token runtime/auth-spike/fresh.txt` | `--token runtime/auth-spike/fresh.txt` |
| Active duplicate | `--token runtime/auth-spike/duplicate.txt` | `--token runtime/auth-spike/duplicate.txt --repeat 2 --interval 2` |
| After EndAuthSession | `--token runtime/auth-spike/end.txt` | `--token runtime/auth-spike/end.txt --repeat 2 --interval 2 --end-between` |
| First use after at least 60 s | `--token runtime/auth-spike/age60.txt` | `--token runtime/auth-spike/age60.txt --wait-before 60` |
| First use after at least 5 min | `--token runtime/auth-spike/age300.txt` | `--token runtime/auth-spike/age300.txt --wait-before 300` |
| Cancellation | `--token runtime/auth-spike/cancel.txt --cancel-after 15` | `--token runtime/auth-spike/cancel.txt --watch 45` |
| Issuer process exits | `--token runtime/auth-spike/exit.txt` | `--token runtime/auth-spike/exit.txt --watch 60`; after initial OK, Ctrl+C in the client |
| Steam application exits (manual) | `--token runtime/auth-spike/steam-exit.txt` | `--token runtime/auth-spike/steam-exit.txt --watch 120`; after initial OK, choose Steam > Exit yourself |

`--repeat` counts total calls. `--interval` begins after the previous callback
or immediate rejection (and the one-second drain after `--end-between`). The
validator reads one token once; it never renews it during repeats. To check a
new validator process, run it again on the same token while its client remains
open. Capture immediate and asynchronous results separately.

For wrong identity and corruption, start a fresh client using
`--token runtime/auth-spike/negative-source.txt`. The following PowerShell code
creates two altered files **without displaying their contents**:

```powershell
$tokenParts = [IO.File]::ReadAllText((Join-Path $PWD 'runtime/auth-spike/negative-source.txt')).Trim().Split('.')
$wrongParts = $tokenParts.Clone()
$wrongParts[1] = ([uint64]::Parse($wrongParts[1]) + [uint64]1).ToString()
[IO.File]::WriteAllText((Join-Path $PWD 'runtime/auth-spike/wrong-id.txt'), ($wrongParts -join '.'))
$corruptParts = $tokenParts.Clone()
$lastByte = [Convert]::ToByte($corruptParts[2].Substring($corruptParts[2].Length - 2), 16) -bxor 1
$corruptParts[2] = $corruptParts[2].Substring(0, $corruptParts[2].Length - 2) + $lastByte.ToString('x2')
[IO.File]::WriteAllText((Join-Path $PWD 'runtime/auth-spike/corrupt.txt'), ($corruptParts -join '.'))
.\build\osw-auth-server.exe --token runtime/auth-spike/wrong-id.txt
.\build\osw-auth-server.exe --token runtime/auth-spike/corrupt.txt
```

For binding to the **same** server, use a fresh token path that does not yet
exist. Start the server first:

```powershell
.\build\osw-auth-server.exe --token runtime/auth-spike/bound.txt --ready-file runtime/auth-spike/server-id.txt --token-wait 120
```

Then, in the client window after `Anonymous server` appears:

```powershell
$serverId = [IO.File]::ReadAllText((Join-Path $PWD 'runtime/auth-spike/server-id.txt')).Trim()
.\build\osw-auth-client.exe --token runtime/auth-spike/bound.txt --bind-server $serverId
```

For binding to **another** server, use three windows and fresh filenames:

```powershell
# Window 1: target server stays logged on while waiting for a deliberately absent file.
.\build\osw-auth-server.exe --token runtime/auth-spike/unused-binding-target.txt --ready-file runtime/auth-spike/target-id.txt --token-wait 300
# Window 2: after target-id.txt is ready, issue its bound ticket.
$targetId = [IO.File]::ReadAllText((Join-Path $PWD 'runtime/auth-spike/target-id.txt')).Trim()
.\build\osw-auth-client.exe --token runtime/auth-spike/bound-other.txt --bind-server $targetId
# Window 3: validate with a different anonymous server identity.
.\build\osw-auth-server.exe --token runtime/auth-spike/bound-other.txt
```

Stop the waiting target and client with Ctrl+C. The SDK 1.65 identity argument
is supported by this loader; a positive same-server control is necessary when
interpreting the different-server rejection.

## Failure modes

The application writes a stable `ERROR NAME:` and exits with the listed code.
Opaque SDK initialization diagnostics are deliberately not echoed.

| Code | Name | Meaning |
| --- | --- | --- |
| 10 | LIBRARY_MISSING | No file at the explicit/executable-adjacent path |
| 11 | SYMBOL_MISSING | Required flat export or versioned interface unavailable |
| 12 | CLIENT_UNAVAILABLE | Client init failed; includes numeric init result (2 means no Steam client) |
| 13 | SIGNED_OUT | API initialized but the user is not logged on |
| 14 | SERVER_INIT_FAILED | Game-server initialization failed |
| 15 | SERVER_LOGON_FAILED | Anonymous connection failure callback; numeric Steam result included |
| 16 | TIMEOUT | Named request did not finish by its deadline |
| 17 | TICKET_FAILED | Ticket request or response failed |
| 18 | INVALID_TOKEN | Malformed, oversized, or invalid token/online ID |
| 19 | IO_FAILED | File/environment or other local operation failed |
| 20 | INVALID_ARGUMENTS | Invalid options or explicit stop during a pending wait |
| 21 | AUTH_REJECTED | An immediate/asynchronous initial authentication attempt was refused |
| 22 | CALLBACK_ABI_MISMATCH | Callback payload size differs from the checked ABI |
| 23 | LIBRARY_LOAD_FAILED | File exists but cannot be loaded (dependency/architecture) |
| 24 | STEAM_SHUTDOWN | Steam requested shutdown while the ticket was still pending |

Offline checks are in CTest, including a test-only DLL written in this project
that models failure callbacks. They are not proof of real Steam outages.
Additional manual real-environment checks:

```powershell
.\build\osw-auth-client.exe --library runtime/auth-spike/does-not-exist.dll
# After YOU exit Steam:
.\build\osw-auth-client.exe --timeout 10
```

For an actual anonymous-logon failure, run the validator during an existing
Steam outage or on a separately controlled disconnected test machine, with
`--timeout 10`. Do not change this machine's network/firewall settings for the
spike. A delivered connection-failure callback yields code 15; silence yields
code 16. The deterministic offline CTest cases exercise both outcomes.

## Linux (not tested)

```sh
cmake -S . -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build
ctest --test-dir build --output-on-failure
./build/osw-auth-client --library /absolute/path/to/libsteam_api.so
./build/osw-auth-server --library /absolute/path/to/libsteam_api.so
```

Use Valve's 64-bit Linux runtime and an installed/logged-in Steam client for
the client. A dedicated validator machine requires operator-installed Valve
game-server dependencies, including `steamclient.so`; that deployment has not
been qualified. No SteamCMD download or execution is part of this task.

See [the gate report](../../docs/GATE1_AUTH_SPIKE.md) for measured results,
remaining manual cases and the interpretation of replay/freshness.
