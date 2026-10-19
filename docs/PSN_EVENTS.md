# PSN notification stream, version 1

This optional stream maps the provider's Steam-backed platform state onto PSN
notification semantics. It is a versioned orbiSteamworks transport contract,
not a Sony wire protocol or a promise to implement every PSN notification.
It contains no guest pointers, callback functions or host structures. A consumer
may pass these records across its own local transport without remapping them.

## Lifecycle and ownership

Resolve both additive exports from the same provider library:

```c
int32_t osw_enable_psn_events(osw_context *context, uint32_t version);
int32_t osw_poll_psn_event(osw_context *context, char *buffer,
                         size_t capacity, size_t *required);
```

Call `osw_enable_psn_events(context, 1)` before `osw_start`. Other versions return
`OSW_NOT_SUPPORTED`; calls after start or stop return `OSW_INVALID_ARGUMENT`.
Repeating the same enable call before start is harmless. Without opt-in, polling
returns `OSW_NOT_SUPPORTED` and the stream allocates no records. Existing ABI 1
and ABI 2 structures, operations and the `osw_poll_event` queue are unchanged.
Each queue has its own consumer and draining either cannot consume the other.

Polling copies and removes one complete UTF-8 JSON record. `required` includes
the terminating NUL. Pass a null buffer and zero capacity to obtain its size:
`OSW_LIMIT` leaves that record queued. A short buffer also returns `OSW_LIMIT`
without writing it. `OSW_PENDING` means empty. All other failures leave output
storage and `required` unchanged. A null context/required pointer, or a null
buffer with nonzero capacity, returns `OSW_INVALID_ARGUMENT`.

Use one consumer and serialize its size-query/copy pair. A producer may fail
the stream between those calls; handle the copy status rather than assuming
the earlier size query reserved the record. No Steam calls, guest callbacks or
network waits occur while polling. Each record owns the event's names and the
recipient identity captured when enqueued; subsequent renames cannot rewrite
pending records. Normal stop leaves queued records, including sign-out, readable
until destruction. Destroy must not race any API call.

The queue holds at most 512 records and 1 MiB, including NUL bytes; a record is
at most 16 KiB including NUL. A mapped batch, such as the two presence
invalidations, is published atomically. Overflow, an invalid meaningful source
event, or serialization/allocation failure clears the queue and permanently
latches `OSW_UNAVAILABLE`. A lost acceptance is not recoverable from a friend
snapshot, so the consumer must stop its notification integration and recreate
the context; it must not silently continue. Ordinary requests, retained results
and the legacy event queue remain available. Legacy-queue overflow alone does
not fail this independent stream.

## Records

Every record has integer `version: 1` and string `kind`. Account IDs are decimal
**strings**, never JSON numbers. Online IDs are UTF-8, at most 16 bytes. Session
IDs and invitation IDs are copied opaque printable-ASCII strings, bounded to
31 and 60 bytes respectively. They are not names or locally hashed identities.
Provider-generated invitation IDs fit even when every underlying ID/counter
uses all 64 bits. Their representation is opaque and must not be parsed.

### `push`

```json
{
  "version": 1,
  "kind": "push",
  "from": {"accountId": "76561198000000002", "onlineId": "RemoteName"},
  "to": {"accountId": "76561198000000001", "onlineId": "LocalName"},
  "service": null,
  "serviceLabel": 0,
  "dataType": "np:service:presence:onlineStatus",
  "data": "",
  "extData": {}
}
```

`service` is JSON null for the unnamed service. An empty string is distinct and
must not be substituted for null when matching guest filters. `data` is a raw
UTF-8 payload represented as a JSON string: an empty string means zero payload
bytes, not `{}` and not the outer transport document. `extData` is a string-to-
string object. `from` and `to` always exist. The unknown actor of a session
metadata invalidation is `{ "accountId": "0", "onlineId": "" }`; do not
attribute it to the local user or lobby owner. Other events carry the known
source account; a missing source name stays empty.

| Provider transition | Service | Data type | Payload and extensions |
| --- | --- | --- | --- |
| Compatible friend enters, leaves, or changes online ID | null | `np:service:presence:onlineStatus` | Empty payload and extensions |
| Same transition, second invalidation | `inGamePresence` | `np:service:presence:gameTitleInfo` | Empty payload and extensions |
| Committed session member joins | `sessionInvitation` | `np:service:session:game:join:to:member` | Empty payload; session and accounts |
| Committed member leaves | `sessionInvitation` | `np:service:session:game:remove:to:member` | Empty payload; session and accounts |
| Session metadata or member name changes | `sessionInvitation` | `np:service:session:game:update:to:member` | Empty payload; session and known accounts |
| Invitation received and namespace validated | `sessionInvitation` | `np:service:invitation` | Invitation JSON payload; invitation, session and accounts |

Session extensions contain `sessionId`, `toAccountId` and, when known,
`fromAccountId`. An invitation also contains `invitationId`. Its payload string
encodes an object with `invitationId`, `sessionId` and
`fromUser: { "accountId": "...", "onlineId": "..." }`. It does not fabricate
a message, expiration or delivery guarantee.

Friend visibility means **compatible and currently in game**, using the same
title/build/realm and provider protocol as the friend-list API. Losing that
visibility is not evidence that the Steam friendship was removed. Consequently
the stream emits presence invalidations, never friendship-added/removed events.
The consumer refreshes the friend/presence snapshot through the ordinary API.
Generic legacy friend refresh events (`account_id=0`) do not identify a user and
produce no push; actual changes have their own per-friend events. Unchanged
snapshots produce no duplicate invalidations.

### `invitationAccepted`

```json
{
  "version": 1,
  "kind": "invitationAccepted",
  "from": {"accountId": "76561198000000002", "onlineId": "RemoteName"},
  "to": {"accountId": "76561198000000001", "onlineId": "LocalName"},
  "sessionId": "109775244000000001",
  "invitationId": "109775244000000001-76561198000000002-1"
}
```

This is a distinct accepted-invitation action for the host's system-event path.
It is emitted only after the existing provider acceptance flow has validated
the namespace and prepared the target session data. A Steam invitation receipt,
an overlay open/close and provisional lobby entry do not imply acceptance.
Acceptance does not commit game-party membership: the application's membership
operation remains necessary. Duplicate acceptance remains idempotent. This
extension does not add cold-launch invitation handling or an in-game dialog.

### `identity`

Fields are `version`, `kind: "identity"`, `signedIn` (boolean), `accountId`
(decimal string) and `onlineId` (string). Sign-out uses `accountId: "0"` and an
empty online ID. This snapshot lets a host dispatch its own NP state callbacks.
An account change still ends the context; it is never adopted in place.

## Host responsibilities and evidence

The host owns guest subscription/filter matching, local-user mapping, callback
registration lifetime, PS4 structures and memory, and dispatch on its own
callback pump. Only an accepted-invitation record belongs on its accepted
SystemService path. Ordinary push records must be delivered to matching guest
registrations with the documented service, type, payload and extension fields.
Transport errors must be visible to the integrating application. The library
never enters guest code or changes host configuration.

The names and payload distinctions above follow the public replacement
implementation surfaces in [shadNet](https://github.com/shadps4-emu/shadNet)
(`client_session.cpp`, `webapi_routes_session.cpp`) and
[shadPS4](https://github.com/shadps4-emu/shadPS4) (`np_handler_web_api.cpp`).
They are compatibility evidence, not a Sony SDK specification. The mapper was
implemented independently in this MIT library; no host/server implementation
was copied. Valve's [join callback](https://partner.steamgames.com/doc/api/ISteamFriends#GameLobbyJoinRequested_t)
and [lobby invitation API](https://partner.steamgames.com/doc/api/ISteamMatchmaking#InviteUserToLobby)
describe the underlying Steam actions. Offline tests exercise both this public
stream and real core friend/invitation transitions using fixtures. They do not
establish game, live Steam or multi-PC acceptance.
