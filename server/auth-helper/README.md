# Anonymous Steam authentication helper

The complete token format, protocol and verifier rules for other servers are
in [docs/OSW1_TOKEN_AND_HELPER.md](../../docs/OSW1_TOKEN_AND_HELPER.md).

Built as `osw-auth-helper` by the normal CMake build. It runs on the publisher
machine, loads the operator-supplied Steam API library and logs on anonymously
for AppID 480. It opens no HTTP listener and does not advertise a game server.

A publisher starts it with redirected stdin/stdout. Only the parent process owns
these pipes; they are not a network authentication endpoint. EOF ends all
outstanding sessions, logs off and shuts down. Steam diagnostics go to stderr,
which the publisher discards to avoid forwarding account-specific output.

Each ASCII protocol line is less than 8192 bytes. Fields use tabs; no quoting.
Every request has a strictly increasing positive decimal uint64 sequence.

```text
helper -> OSW1 READY <server-SteamID>
parent -> OSW1 <sequence> VALIDATE <osw1-token>
helper -> OSW1 RESULT <sequence> OK <user-SteamID> <owner-SteamID> <lease>
helper -> OSW1 RESULT <sequence> ERROR <reason>
parent -> OSW1 <sequence> END <lease>
helper -> OSW1 RESULT <sequence> ENDED
```

Spaces above denote tabs. `END` is idempotent for unknown/already-ended leases.
Leases are local opaque integers; they have no authority outside this pipe.
Validation returns OK only after a matching successful native callback with a
nonzero owner. One pending or active authentication per SteamID is allowed by
the native interface. The helper accepts at most 64 pending validations, each
with a deadline (default 30 seconds, `--timeout` in 1–60). Active sessions have
no elapsed-time deadline. Later callbacks are diagnostics and never end a lease.

Reasons are `INVALID_TOKEN`, `BUSY`, `UNAVAILABLE`, `REJECTED`, `TIMEOUT`.
Malformed framing or a decreasing request sequence closes the helper. Fatal
initialization errors are nonzero process exits. The parent fails admission on
unexpected output, pipe loss, timeout or process exit. It does not disconnect
already-admitted publisher players when the helper fails.

The helper does not own publisher persistence or name bindings. The integrating
publisher owns ticket replay prevention and verified identity/name bindings;
the [protocol specification](../../docs/OSW1_TOKEN_AND_HELPER.md) defines the
verifier's responsibilities. A new admission requires a new ticket.
There is no claimed 120-second native Steam ticket lifetime. The publisher's
120-second ticket handoff deadline ends only *unclaimed* native leases;
claimed leases end when their owning lobby connection closes.

The helper and diagnostic tools share the token codec in
`src/core/credential.cpp`. No host or publisher implementation is required to
build the helper.
