# osw1 token and auth-helper protocol

Protocol version: `osw1`.

This specification describes implemented behavior, so that another private
server can admit players by Steam identity with the same token and helper.
That server does not have to be the Demonware Server. The format is a
replacement design (I). It is not a Sony or Valve format and asserts no PSN
behavior.

Implementation boundaries:

| Role | Source |
| --- | --- |
| Token issuer and codec | `src/core/credential.cpp`, `src/steam/backend.cpp` |
| Auth helper | `server/auth-helper/main.cpp` (`osw-auth-helper`) |
| Consumer | A server that implements the validation, replay and persistence contract below |

The central helper supports AppID 480. The runtime flat-API declarations were
checked against Steamworks SDK 1.65; see [Sources](SOURCES.md).

## Roles and flow

```text
client host + orbiSteamworks          verifier (private server)           osw-auth-helper (child)
  |  GET discovery (authenticated) -->  {"server_id": S, ...}                  |
  |  OSW_TOKEN(server_id=S)                                                    |
  |  GetAuthSessionTicket(bound to S)                                          |
  |  osw1.<id>.<ticket>.<name> ------->  parse, replay check                    |
  |                                      VALIDATE <token> ------------------>  BeginAuthSession
  |                                      <------- OK <user> <owner> <lease>   ValidateAuthTicketResponse_t
  |                                      check user, bind name, record hash
  |  <------------------------ admitted
  |  ... connection closes ...           END <lease> ----------------------->  EndAuthSession
```

## 1. Token

### Syntax

```text
token          = "osw1" "." steam-id "." ticket-hex "." online-id-b64
steam-id       = %x31-39 *19DIGIT        ; canonical decimal, 1 .. 18446744073709551615
ticket-hex     = 1*( 2HEXDIG )           ; even length; issuers emit lowercase
online-id-b64  = 4*22 b64url-char        ; RFC 4648 section 5 alphabet, no padding
b64url-char    = ALPHA / DIGIT / "-" / "_"
```

The token has exactly four fields and three dots. No field contains a dot.

| Field | Rule |
| --- | --- |
| Version | The literal `osw1`. Anything else is refused |
| SteamID64 | Nonzero canonical unsigned 64-bit decimal: no sign, no leading zero, no whitespace. Parse it as an integer, never as floating point |
| Ticket | Hex encoding of the Steam session ticket bytes. Verifiers accept both cases. The canonical identity of a ticket is its **decoded bytes** |
| Online ID | Unpadded base64url of the online ID, which must be canonical: re-encoding the decoded bytes gives the same text, so `=` and nonzero trailing bits are refused. The decoded ID is 1–16 bytes of valid UTF-8, without control characters (U+0000–U+001F, U+007F) and without a leading or trailing space. Issuers derive it from the Steam persona name |

### Size limits

| Item | Limit | Reason |
| --- | --- | --- |
| Whole token | 1–4096 ASCII characters | Fits the 4097-byte PS4 `OrbisNpIdToken` buffer with its NUL. The Demonware envelope reader accepts at most 4096 |
| Ticket bytes | Issuer: at most `floor((4096 − 7 − len(steam-id) − len(online-id-b64)) / 2)`, and at most 2048 (the issuer's buffer) | The token limit dominates: at most 2042 bytes, and 2025 bytes for a 17-digit SteamID with a 16-character online ID |
| Ticket bytes at the helper | 1–2048 | `BeginAuthSession` input bound |
| Online ID | 1–16 bytes; 2–22 encoded | The PS4 online-ID field holds 16 bytes |
| Measured | 234-byte ticket, 502-character token | Gate 1, online ID `OswTest`, bound and unbound |

### Meaning

- **SteamID64** is a claim. It is authenticated only when the helper validates
  the ticket and reports the same SteamID.
- **The ticket** comes from `ISteamUser::GetAuthSessionTicket`. orbiSteamworks
  always binds it to the helper's game-server SteamID: a
  `SteamNetworkingIdentity` of type SteamID, taken from the `server_id`
  request field. A ticket bound to another server fails validation with
  result 10 (`AuthTicketNetworkIdentityFailure`, measured).
- **The online ID is not authenticated by Steam.** The verifier decides whether
  it may be used, through its own binding (section 3).
- **The token is a bearer credential.** It has no timestamp, audience field,
  nonce or signature beyond Steam's ticket. Never log, store or forward a
  token. Persist only the ticket hash.
- **Freshness is not established.** Unused tickets still validated after 60
  seconds and after 5 minutes. Do not claim a 120-second ticket lifetime.
  Replay protection relies on Steam's own refusal and on the verifier's
  persistent hash set.

### Issuing a token (orbiSteamworks)

A host submits `OSW_TOKEN` with JSON metadata:
- `client_id`: 1–128 characters, no NUL;
- `scope`: one of `openid`, `openid openid:duid`, `openid,openid:duid`;
- `server_id`: the helper's server SteamID as a canonical nonzero decimal
  string.

`client_id` and `scope` are checked but not encoded in the token.

- **One request at a time.** A context has at most one pending token request;
  a second request returns `OSW_LIMIT`.
- **A new token cancels the previous ticket** (`CancelAuthTicket`). A verifier
  can then see a later "ticket cancelled" callback (result 6) for an earlier
  session. The helper ignores it, and an admitted player stays connected.
- **Bounded size.** A ticket that would make the token exceed 4096 characters
  completes with `OSW_LIMIT`.
- **No token when signed out.** Without Steam, or while signed out, the request
  fails. It never returns a token.

### Discovering the helper identity

The client must learn `server_id` through a channel that authenticates the
server. Otherwise a man in the middle obtains a ticket bound to its own game
server.

Demonware serves `GET /orbi/steam/v1` on its existing HTTPS listener:

```json
{"protocol":"osw1","app_id":"480","title_id":"5760","realm":"<realm>","server_id":"<decimal>"}
```

- The response carries `Cache-Control: no-store`.
- Other methods return 405. The path returns 404 when admission is not `steam`.
- The anonymous server identity changes when the helper restarts. Clients
  fetch it for each token and do not cache it.
- The shadPS4 host checks the server certificate against a configured CA, and
  checks `title_id` and `realm` against its configuration, before requesting a
  token.

Another server may expose the same document (I). It should use its own
`title_id` and `realm` namespace, keep `protocol` = `osw1` and `app_id` = `480`,
and serve the document over TLS that clients verify.

### Test vectors

Both reference parsers (C++ `osw::core::parse_token`, Go `steam.ParseToken`)
give these results. SteamID `12345` is a synthetic syntax example, not an
account.

| # | Token (or construction) | Result |
| --- | --- | --- |
| 1 | `osw1.12345.00ff.T3N3VGVzdA` | Accept: 12345, `OswTest`, ticket `00 ff`, SHA-256 `06eb7d6a…9389b8` |
| 2 | `osw1.12345.00FF.T3N3VGVzdA` | Accept: same bytes and **same hash** as #1 |
| 3 | `osw1.18446744073709551615.01.QWJj` | Accept: maximum uint64, `Abc` |
| 4 | `osw1.12345.00ff.QTIzNDU2Nzg5MDEyMzQ1Ng` | Accept: 16-character `A234567890123456` |
| 5 | 2037-byte ticket, total 4096 characters | Accept |
| 6 | Same with one more byte, 4098 characters | Reject: too long |
| 7 | `osw2.12345.00ff.T3N3VGVzdA` | Reject: version |
| 8 | `osw1.012345.00ff.T3N3VGVzdA` | Reject: leading zero |
| 9 | `osw1.0.00ff.T3N3VGVzdA` | Reject: zero SteamID |
| 10 | `osw1.18446744073709551616.00ff.T3N3VGVzdA` | Reject: overflow |
| 11 | `osw1.+12345.00ff.T3N3VGVzdA` | Reject: sign |
| 12 | `osw1.12345..T3N3VGVzdA` | Reject: empty ticket |
| 13 | `osw1.12345.0ff.T3N3VGVzdA` | Reject: odd hex length |
| 14 | `osw1.12345.00fg.T3N3VGVzdA` | Reject: non-hex |
| 15 | `osw1.12345.00ff.T3N3VGVzdA==` | Reject: padding |
| 16 | `osw1.12345.00ff.T3N3VGVzdB` | Reject: noncanonical base64url |
| 17 | `osw1.12345.00ff.MXN3` (`1sw`) | Accept: an online ID may start with a digit |
| 18 | `osw1.12345.00ff.T3c` (`Ow`) | Accept: two-byte online ID |
| 19 | `osw1.12345.00ff.QTIzNDU2Nzg5MDEyMzQ1Njc` | Reject: online ID of 17 characters |
| 20 | `osw1.12345.00ff.T3N3IFRlc3Q` (`Osw Test`) | Accept: space inside an online ID |
| 20a | `osw1.12345.00ff.UGnDqSBaLg` (`Pié Z.`) | Accept: non-ASCII UTF-8 |
| 20b | `osw1.12345.00ff.IE9zdw` (` Osw`) | Reject: leading space |
| 20c | `osw1.12345.00ff.T3N3AQ` (`Osw` + U+0001) | Reject: control character |
| 20d | `osw1.12345.00ff._w` (byte `ff`) | Reject: invalid UTF-8 |
| 21 | `osw1.12345.00ff.T3N3+GVzdA` | Reject: standard-base64 character |
| 22 | `osw1.12345.00ff.T3N3VGVzdA.x` | Reject: five fields |
| 23 | `osw1.12345.00ff` | Reject: three fields |
| 24 | empty string | Reject |

Full SHA-256 of `00 ff`:
`06eb7d6a69ee19e5fbdf749018d3d2abfa04bcbd1365db312eb86dc7169389b8`.

## 2. Auth-helper process protocol

### Process

```text
osw-auth-helper [--library PATH] [--timeout SECONDS]
```

- **Library.** `--library` names the Steam API library the operator supplies
  (`steam_api64.dll` or `libsteam_api.so`). The default is the file next to
  the helper executable.
- **Timeout.** `--timeout` is an integer from 1 to 60 (default 30). It bounds
  the anonymous logon and each validation.
- **AppID.** The helper sets the process `SteamAppId` to 480 itself.
- **Steam setup.** It initializes a game server bound to 127.0.0.1 with game
  port 0 and the shared query port, in authentication mode 2 (no VAC). It turns
  advertising off before `LogOnAnonymous`.
  - It never appears in Spacewar's server browser and opens no listening
    socket.
  - It needs no Steam account, password or Web API key.
- **Parent only.** Only the parent process talks to the helper, through
  inherited pipes. On Windows, stdin must be a pipe or the helper exits.
  - The helper is not a network service. Never expose it through a socket.
- **stdout carries the protocol only.** The helper redirects library and
  Steam output to stderr.
- **stderr carries diagnostics.** They can contain account-specific
  information, so the parent discards them or keeps them private. Demonware
  discards them.
- **Shutdown.** EOF on stdin, SIGINT or SIGTERM ends every active
  authentication session, logs off, shuts Steam down and exits with status 0.

### Framing

- **Lines.** Each line is ASCII and ends with LF. The helper also accepts
  CRLF input.
- **Length.** A line holds at most 8191 bytes before its LF. The parent's
  reader should accept 8192.
- **Fields.** Fields are separated by TAB, with no quoting or escaping. Every
  line starts with the field `OSW1`.
- **Sequence.** Every request carries a sequence number: a canonical nonzero
  decimal uint64 that strictly increases. Gaps are allowed; Demonware uses
  1, 2, 3 and so on.
- **Ordering.** Responses repeat the request sequence and can arrive out of
  order. A `VALIDATE` result waits for a Steam callback, while `END` and
  immediate errors return at once. The parent matches responses by sequence.

### Messages

Arrows show direction. `→` is a TAB in the examples.

| Direction | Line | Meaning |
| --- | --- | --- |
| helper → parent | `OSW1→READY→<server SteamID>` | Sent once, after anonymous logon. This is the `server_id` that clients bind tickets to |
| parent → helper | `OSW1→<seq>→VALIDATE→<osw1 token>` | Validate the token |
| helper → parent | `OSW1→RESULT→<seq>→OK→<user SteamID>→<owner SteamID>→<lease>` | The validation callback succeeded with a nonzero owner. A lease now holds the native session |
| helper → parent | `OSW1→RESULT→<seq>→ERROR→<reason>` | Validation refused (reasons below) |
| parent → helper | `OSW1→<seq>→END→<lease>` | End the session of this lease |
| helper → parent | `OSW1→RESULT→<seq>→ENDED` | Done. This is idempotent: unknown or already ended leases also return `ENDED` |

- **Leases.** A lease is a positive integer, increasing within one helper
  process and starting at 1. It has meaning only on that pipe.
- **No session deadline.** Active leases have no elapsed-time deadline. Later
  Steam callbacks, such as a cancelled ticket or a Steam logoff, go to stderr
  as diagnostics and never end a lease.
- **Owner.** `owner` is the license owner that Steam reports. It can differ
  from `user`.

### Errors

| Reason | When |
| --- | --- |
| `INVALID_TOKEN` | The token fails the section 1 parser |
| `UNAVAILABLE` | 64 validations are already pending, or the game server is not logged on |
| `BUSY` | This SteamID already has a pending validation or an active lease. Steam allows one session per SteamID; the client can retry after the old lease ends |
| `REJECTED` | `BeginAuthSession` returned nonzero, or the callback reported failure or owner 0. Replays, foreign or corrupt tickets and tickets bound to another server end here |
| `TIMEOUT` | No matching callback within `--timeout`. The helper ends the native session first |

Some conditions are **fatal**. The helper ends its sessions and exits nonzero:
- a line without exactly four fields;
- a missing `OSW1` prefix;
- a non-canonical or non-increasing sequence;
- an unknown operation, or a malformed `END` lease;
- an overlong line;
- a pipe I/O failure.

Exit statuses:

| Exit | Meaning | When |
| --- | --- | --- |
| 0 | Clean shutdown | EOF or signal |
| 10 | Steam API library missing | Before `READY` |
| 11 | Required Steam symbol missing | Before `READY` |
| 14 | Game-server initialization failed | Before `READY` |
| 15 | Anonymous logon failed | Before `READY` |
| 16 | Logon timed out | Before `READY` |
| 22 | Steam callback ABI mismatch | Any time |
| 23 | Steam library could not be loaded | Before `READY` |
| 18 | Malformed sequence or lease number | After `READY` |
| 19 | Pipe I/O failure | Any time |
| 20 | Invalid arguments, stdin not a pipe, or a framing violation: wrong field count, missing prefix, non-increasing sequence, unknown operation, overlong line | Any time |

### Parent obligations, as Demonware implements them

- **Startup.** Start the helper with `--timeout 30`, with a hidden window on
  Windows and stderr discarded. Wait at most 40 seconds for `READY`.
- **Deadlines.** Give each validation a 35-second deadline, longer than the
  helper's 30, so the helper normally reports `TIMEOUT` first. Give each `END`
  a 5-second deadline.
- **Parent-side timeout.** If the parent's deadline expires, it kills the
  helper. The abandoned validation might still hold a native session. New
  admissions are then refused until the operator restarts the server.
  - Automatic helper restart is not implemented (I).
- **Protocol violations** fail the helper generation. These are unexpected
  output, an unknown sequence and malformed numbers.
- **Helper loss** never disconnects players who were already admitted.

## 3. Verifier rules

Demonware applies the following steps. Every failure refuses admission with
the same generic error, so clients learn no detail.

1. **Extract the token** from the game's transport. For CTR this is
   `ReadPSNEnvelope`: `title_id` `5760`, `auth_task` `46`, an `iv_seed`
   decimal, `extra_data` → `token` → `id_token`, and a body of at most 16 KiB.
   Other games or servers use their own transport.
2. **Parse the token** as in section 1. Compute `hash = SHA-256(decoded ticket bytes)`.
3. **Replay check:** refuse if `hash` was ever consumed. This runs before the
   helper is contacted.
4. **Validate:** send `VALIDATE` and wait for at most 35 seconds. Require
   `OK` with a nonzero lease.
5. **Take the lease.** From this point every refusal sends `END` for it.
6. **Check identity.** Require the helper's `user` to equal the token's
   SteamID and `owner` to be nonzero. Demonware does not require owner to
   equal user. A stricter server may.
7. **Record atomically**, in one database transaction:
   - If the SteamID has no binding, bind it to the online ID. Names are unique
     case-insensitively through a lowercase key across SteamIDs.
   - If a binding exists with a different string, the same SteamID is
     renaming itself (for example after a Steam persona change): update the
     binding, the name used by friend lookups and the player's leaderboard
     rows. A name bound to another SteamID, in any letter case, fails the
     transaction.
   - Insert `hash` into the consumed set. A duplicate fails the transaction.
8. **Admit** with:

   ```text
   PID = SteamID64, Username = online ID, Sub = "steam:<SteamID64>", Kind = "steam"
   ```

Replay rules:
- **Retention.** Consumed hashes have no expiry. Steam tickets carry no
  verifiable issue time, so a time-to-live would add no freshness guarantee.
- **Hashing.** Hash the decoded bytes, never the hex text.
- **Shared store.** Servers that share one identity namespace must share the
  consumed set and the binding table.
- **Defense in depth.** Persistent replay rejection does not depend on Steam
  rejecting a previously consumed ticket in a later validation session.

Lease lifecycle:
- **Expiry.** An admission is held until the game claims it. In Demonware
  that happens when the lobby connection presents its publisher ticket.
  Unclaimed admissions expire with the 120-second publisher-ticket handoff
  window. This window is a Demonware detail, not a Steam ticket lifetime.
- **Single claim.** Each admission can be claimed once.
- **Release.** Closing the claimed connection sends `END`. Authentication or
  response-write failures release the admission too.
- **No cutoffs.** There is no elapsed-time or idle cutoff for established
  connections.

Instance separation:
- **Trust ID.** Demonware binds the instance database to
  `ctrnf:steam:480:5760:v1:<realm>`. `<realm>` holds 1–64 letters, digits,
  `_` or `-`.
- **No migration.** Steam bindings never mix with shadNet or guest data.

## 4. Adopting osw1 in another server

1. **Run the helper as a child process.** Build `osw-auth-helper` from
   orbiSteamworks (MIT). Run one helper per server process, behind private
   pipes. The operator supplies the Steam game-server libraries; see
   [QUICKSTART.md](../QUICKSTART.md).
2. **Publish `server_id`** through an authenticated discovery document with
   your own title and realm.
3. **Implement section 3:** the persistent consumed-hash set, the
   first-admission name binding in the same transaction, the user check and
   lease ending.
4. **Handle the lease states.** Treat `BUSY` as temporary. Treat a parent-side
   timeout as a helper failure.
5. **Use the online ID only after the binding check.** Use SteamID64 as the
   account key everywhere, losslessly.
6. **Accept the AppID 480 limits.** Players appear as "Playing Spacewar".
   Its application namespace is shared; the project-specific title and realm
   checks remain required.
