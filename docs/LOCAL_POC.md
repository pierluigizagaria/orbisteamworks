# Local shadPS4 proof of concept

This is a local, uncommitted PoC for the existing Windows workspace, not a
release. The user deferred distribution and native-port work. The emulator
is built at `../shadps4/build/shadps4.exe`; the publisher candidate is at
`../nextendo-demonware/.cache/steam-poc/demonware-server.exe`. Valve's library
is supplied locally by the user and is not part of this project.

## Prepared configuration

The private operator is `../shadps4/out/tools/steam_poc.py`. `prepare` writes
candidate settings under profile A's `steam-poc/` directory; it does not
activate them. `game` backs up A's current `config.json` and
`host_overrides.json`, activates the candidate and launches the existing
executable through the standard `-g` and `-f false` options. It opens a direct
game window. Saves, installed game bytes and the other profiles are retained.
`restore` restores those settings after A is closed. On 2026-10-15 the operator
activated A after making that backup and launched the first Steam PoC process.
It loaded both Steam runtime modules. No game UI input was sent. Check process
state before launching another instance. The user subsequently confirmed the
Online menu; platform-party creation, matchmaking search and gameplay remain
separate acceptance steps.

The configured online ID is `OrbiPlayerA`. It is a PoC name, not the Steam
persona name. Change `steam_online_id` in the candidate config before the
first game admission if a different name is desired: the publisher binds
the first admitted name to the Steam account. The normal publisher has a
fresh, separate `runtime/steam-poc/data/ctrnf.db`; prior ledgers are not migrated.
Realm is `ctrnf-poc-normal`, title `5760`, build `1.21`, AppID `480`.

The new publisher binds loopback only: HTTPS 31543, dashboard 31593, lobby TCP
3074 and NAT UDP 3074. Its operator refuses occupied ports and does not stop
other services. The existing local TLS certificate and signing material are
referenced in place. Host discovery verifies the certificate using the
configured local CA. The existing guest HTTPS setting is retained separately.
Guest DNS and HTTP host overrides route to loopback for this local-only PoC.
The existing signature/routing XML patches remain unchanged.

## Human-operated acceptance

Keep Steam running and signed in. Close profile A and any old normal publisher
using 127.0.0.1:3074. Run these private launchers:

1. `../shadps4/out/tools/start-steam-publisher.cmd`.
2. After it reports the helper ready, `../shadps4/out/tools/start-steam-game.cmd`.
3. Enter CTR's Online menu manually. Record sign-in, publisher admission,
   platform session creation and any visible error. Close and repeat the
   login/session lifecycle once with a fresh ticket.

The publisher has no elapsed-time or menu-idle shutdown. Stop it with Ctrl+C
when finished. After closing the game,
`../shadps4/out/tools/restore-pre-steam-profile.cmd` restores A's earlier
provider settings. Do not use the older paired shadNet launcher during this
single-account test: B remains configured for the earlier provider.
This command restores profile settings only. The former installed emulator is
preserved at `../shadps4/build/backups/before-steam-20261015T095941Z/shadps4.exe`.
Returning to the previously accepted shadNet lab also requires that executable;
close all emulator profiles before replacing the shared executable. The PoC's
default-build check is compilation evidence, not renewed shadNet game acceptance.

Only one Steam account/machine is available. No two-account race, friend-page
acceptance, real invite/accept, grouped search or LAN/WAN acceptance has been
performed. The native port remains paused. Those tests require independent
Steam clients and human gameplay; fixtures do not substitute for them.

## Verified boundaries and remaining limits

- Gate 1 native Steam authentication passed, including the recorded negative
  and lifecycle cases.
- Twenty-seven offline suites cover ownership, cancellation, namespaces, binary
  payloads and platform behavior. A separate C translation unit compiles the
  public header. Two simulated platform instances transfer the captured-size
  image before explicit guest membership.
- Windows first-chance observation also covers expected validation, loader,
  initialization, ticket and callback-ABI failures. Failed ticket issuance
  remains retryable; fatal backend errors finish pending work. These checks use
  a synthetic API library and do not replace live Steam or game evidence.
- Real C API private-session create/read/update/delete passed with account A.
  The image readback was local; remote payload visibility is not established.
- A real 4,096-byte lobby message containing NULs round-tripped on account A.
- A C API-issued bound ticket passed the native helper, SQLite, verified local
  HTTPS discovery, signed publisher auth and publisher-ticket handoff.
- Both Steam-enabled and default-disabled emulator configurations build.
  The final Steam-enabled executable also passes CLI startup with `--help`.
  Compilation and CLI startup are not guest ABI/game acceptance.
- The full publisher test suite, vet and executable build pass.
- The actual publisher executable starts with the prepared PoC configuration;
  its HTTPS certificate and anonymous helper readiness were checked locally.
- The first actual emulator process loaded `steam_api64.dll` and
  `steamclient64.dll` and reached guest NP initialization. Manual Online reentry
  attributed its recurring exception to multipart validation. The owned Toolkit
  excludes the closing delimiter from its submitted length; the library now
  accepts a complete final part ending exactly at its declared length. The
  synthetic regression failed before and passed after this correction.
- That actual game process passed Steam-backed HTTPS authentication and the
  original encrypted Demonware lobby handshake. Its publisher identity matches
  Steam; the game subsequently registered its peer data. After the user closed
  the game, the corrected executable was installed with backup and profile A
  relaunched. The new process created a platform party and registered a publisher
  search at 12:42:51 UTC. Manual cancellation, main-menu reentry and another
  search at 12:44:39 UTC passed. The user reported the search screen without
  errors; no C++ exception was observed in the corrected session.

Unknown WebAPI routes fail explicitly. The PoC does not yet qualify pagination,
remote-user session enumeration, nonempty invitation messages/data or the wider
PS4 platform surface. It implements no Steam race transport: peer UDP remains
the game's existing transport. Guest NPAuth, the publisher handshake and the
single-player party/search/reentry path have now run successfully. Friends,
invitations, grouped search and an actual race still require paired acceptance.
The original host's handling of first-chance C++ exceptions remains unchanged.
The newer error-handling build passes all 27 offline suites and the host
build/CLI check. It was installed after the user closed the game, with backup
under `build/backups/before-expected-errors-20261015T132941Z/`. Fresh game
authentication, party creation and publisher search passed at 13:31:01 UTC,
with human confirmation and zero C++ exceptions during bounded observation.
The observers detached while game and publisher stayed running. Manual
cancel/main-menu reentry on this exact build subsequently passed: the user
corrected an initial error selection and confirmed success, the publisher
registered fresh searches at 14:53:38 and 14:53:53 UTC, and a read-only snapshot
confirmed a valid party and ready registration with empty selected error lists.
The game log contains no unhandled exception; the earlier debugger observation
does not cover this later reentry continuously.
The successful flow does not qualify every negative-input/error path.

No commits, pushes, issues, PR changes or public service deployment were made.
Private logs, SDK files, credentials, keys and generated binaries stay ignored.
