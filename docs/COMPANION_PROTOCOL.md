# Local companion pipe, version 1

A companion is an optional child process that implements an application's room
protocol and data, while the library keeps Steam identity, authorization and
transport. This protocol is defined by this project; it is not a Sony or Valve
interface.

## Process and framing

Frames are UTF-8 newline-delimited JSON on inherited stdin/stdout, at most
98,304 bytes including the newline; queues are bounded. Every frame has `v:1`.
All account, lobby and request IDs are decimal strings, except the correlation
`id`, an opaque nonempty string of at most 64 bytes. Unknown operations fail
closed.

The companion logs to stderr and never logs tokens, peer payloads or private
IDs. On Windows stderr is discarded by default; the environment map may set
`OSW_COMPANION_STDERR` to a private UTF-8 log-file path for diagnostics.

The companion exits on stdin EOF. On Windows the parent owns it through a
kill-on-close Job Object. On POSIX the host must leave this child exclusively to
the library for waiting and reaping throughout the context lifetime. Launch
refuses `SIGCHLD=SIG_IGN` and `SA_NOCLDWAIT` without changing the host's signal
disposition. A host that concurrently reaps the child or changes that policy
violates the ownership contract, because PID-based shutdown cannot guarantee a
stable process handle. Linux support has not been tested with a live session.

The library sets `OSW_COMPANION_PROTOCOL=1` in the child's environment.
Application-specific settings belong in the caller's environment map; the
library does not interpret them.

## Owner handshake

The parent first sends:

```text
{v:1,event:"owner",steam_id,online_id,token,title,build,realm,backend:"steam"|"lab",app_id:480}
```

The child must receive it within ten seconds. `backend` is `lab` only for the
library's simulated test backend. `app_id` is a positive uint32 that the library
always sends; it is required in `loopback-v1` mode, and children in other modes
may assume 480 when it is absent. The AppID is part of the platform identity
and namespace.

`token` is an `osw1` envelope whose ticket field is a random 32-byte capability
for this process. It is for local admission only: the exact token and its parsed
owner ID must match. It is never a Steam ticket and never goes to an auth helper
or a peer. `OSW_TOKEN` requests through the C API return the same envelope in
companion mode.

When all its listeners and storage are ready, the child sends
`{v:1,event:"ready"}`, optionally with a `capabilities` array of at most 16
strings of at most 64 bytes. The parent exposes readiness through P2P `status`.

## Endpoint modes

### `virtual-v1`

When the caller enables canonical endpoints, the owner frame adds a `transport`
object with exactly five fields (example values):

```json
{"mode":"virtual-v1","address":"198.18.1.2","port":12345,"game_address":"127.0.0.1","game_port":3074}
```

`address:port` is the immutable canonical identity endpoint and
`game_address:game_port` the configured physical game UDP source. The child must
validate and support this mode, then include `virtual.endpoints.v1` in its ready
`capabilities`; without it the library fails startup, so a child cannot silently
accept a mode it does not implement. Owner frames without `transport` use the
default mode and need no acknowledgement. Capabilities are independent; a child
can acknowledge several in the same array.

In virtual mode `peer.endpoint` still returns only `address` and `port`, but
these are the remote canonical endpoint. Authorization and physical proxy
allocation happen inside the library, and the host translates socket addresses
through the C route API. The child owns any application-specific address
validation and serialization and must preserve embedded peer identity data.

### `loopback-v1`

The five transport fields have the same shape, with `mode:"loopback-v1"`,
canonical addresses in `127.64.0.0/10` and ports 1024–65535. The ready
acknowledgement must include `loopback.endpoints.v1`; `virtual.endpoints.v1`
alone does not suffice. The physical game source must be a loopback address
outside the reserved range. Proxy sockets bind to the remote canonical endpoint
itself, so hosts need no translation calls. Bind and collision errors never
allocate fallback endpoints. See the [C API](C_API.md#loopback-endpoints-loopback-v1)
for derivation, revocation and AppID isolation.

### Deferred game source

An explicitly deferred loopback owner has exactly this transport shape:

```json
{"mode":"loopback-v1","address":"127.64.1.2","port":12345,"game_pending":true}
```

`game_address` and `game_port` are absent, and a pending descriptor never
authorizes the originally configured source. The child must also acknowledge
`game.endpoint.v1`; otherwise startup fails. The canonical endpoint and the
loopback peer protocol version do not change.

Only a local C API request can start registration. The parent then sends
`{v:1,event:"game.endpoint",id,address,port}`, exactly these five fields. The
child validates the literal loopback source outside `127.64.0.0/10` and the
integer port 1–65535, applies it once, then sends exactly
`{v:1,event:"game.endpoint.bound",id,address,port}` with identical values. The
inherited pipe is the only authority; ownership is never inferred from UDP.

The child must not answer discovery before registration and must answer only
the registered source afterwards. The provider keeps room, peer, advertisement
and carrier operations gated until the acknowledgement; identity and read-only
platform requests remain available, and no room or proxy is created while
pending. The child may apply its local mapping before its acknowledgement
reaches the parent, but no peer routes exist during that interval.

Duplicate, unsolicited or mismatched acknowledgements, invalid registration
frames, pipe loss or an acknowledgement timeout break the context. The parent
bounds the exchange to five seconds and the request deadline. Cancellation after
dispatch is terminal, because the child may already have applied the binding.
Registration cannot be repeated or replaced. P2P `status` adds
`game_endpoint_pending`; `ready` still means that the companion is ready.

## Owner name updates

A child that supports live name changes advertises
`{v:1,event:"ready",capabilities:["owner.name.v1"]}`. Only then may the parent
send `{v:1,event:"owner.name",steam_id,online_id}`, exactly these four fields.
The ID must equal the initial owner, and the new name must be valid UTF-8 of
1–16 bytes without control characters or surrounding spaces. No token,
namespace, account or ownership change is permitted.

The child applies these messages in pipe order, independently of matchmaking,
and uses the current name for authentication labels and name persistence.
Existing sessions, PID, subject, database namespace and the bootstrap capability
remain unchanged; the name embedded in that capability is the initial label and
does not revert a later rename. Neither credentials nor name updates leave the
pipe.

Without this capability the library keeps the initial name for the context.
P2P `status.owner_name_updates` reports support. An explicit online-ID override
from the caller stays fixed. Steam persona changes update the C API identity and
presence, and rename only already committed party memberships; they produce
identity or session-update events, never artificial join or leave events.

## Requests from the child

Child requests are `{v:1,id,op,...}`. Parent replies are
`{v:1,id,ok:true,result:{...}}` or `{v:1,id,ok:false,error:"code"}`. Requests
complete within 25 seconds; the child should allow 30 seconds.

| op | Request fields | Result |
| --- | --- | --- |
| directory.create | `metadata` object, `capacity` 1..16 (default 8) | room record |
| directory.search | `metadata` object, optional `limit` 1..32 | `rooms` array |
| directory.join | `room_id` | room record |
| directory.leave | `room_id` | empty object |
| directory.update | `room_id`, `metadata` object | room record |
| directory.members | `room_id` | current room record |
| peer.send | `room_id`, `peer`, `payload` standard padded base64 | empty object |
| peer.endpoint | `room_id`, `peer` | `address` IPv4 text, `port` uint16; physical loopback in the default mode, canonical in endpoint modes |
| name.resolve | `peer` | `online_id` or an explicit unavailable error |
| name.lookup | `names` array, at most 500 online IDs | `players:[{steam_id,online_id}]`, at most 100 current in-game friends |
| advertisement.set | `context` ASCII namespace <=32 bytes, `payload` base64 <=4096 decoded bytes; empty clears | empty object |
| advertisement.get | `peer`, `context` | `payload` from that friend's private advertisement, or explicit not-found |
| status | none | `ready`, `backend` |

## Rooms

A room record is `{room_id,owner,members:[decimal ID strings],metadata:{...}}`.
Search results for rooms not yet joined have an empty `members` array and use
the advertised owner, because Steam permits owner and member queries only after
joining. Join completion verifies the actual owner against the advertised owner
before the record is accepted; joined records contain the actual members.

Room metadata is public: only `playlist`, `type` and `free_slots` scalar strings
or numbers are accepted, at most 256 encoded bytes. The library adds namespace
fields (`osw/1`, title, build, realm and room protocol `1`) that cannot be
overridden. Never put security IDs, keys, credentials, application files or user
progress in metadata. `search` filters on the exact supplied metadata, excluding
`free_slots`, requests at least one open slot, and widens the distance filter
from Default to Far to Worldwide when no room is found.

The platform-party owner publishes its selected invisible room in member-only
party data. Only members whose game has committed platform membership follow
that selection; provisional invitation joins do not. Create and join wait within
their deadline for the committed followers to join. An owner change closes the
room instead of migrating it, and owner migration is not implemented. Timeout or
cancellation rolls back the room membership and selection it introduced.

An explicit `directory.leave` by a follower suppresses automatic following of
that selected room; retained platform-party membership alone does not undo the
leave. A valid explicit create, search or join request, a different selected
room, or a new party or owner permits following again; no timer re-enables it.
Pending automatic joins are abandoned on an explicit leave, and a later
successful completion is left immediately. A new join to the same room returns
`busy` until the abandoned call completes, so a late callback cannot remove a
newer membership. Pending validations, provider calls and abandoned calls share
a limit of 128, after which admission returns `busy`. If Steam permanently loses
their callbacks, affected rooms stay busy, and exhausting the limit requires a
new library context. An explicit rejoin of the leader's selection resumes
following, including leaving when the leader changes rooms or the local player
leaves the platform party.

## Peer traffic

Messages carry at most 32,768 decoded bytes. Reliable room traffic uses channel
47; game datagrams use channel 48 and at most 65,507 bytes. The library
authenticates the sender by Steam identity and accepts only current room
members; the companion applies its own room-protocol checks. The owner may send
a push on behalf of another player, but never impersonates the authenticated
sender.

`peer.endpoint` allocates a local UDP socket for that remote peer. Incoming
peer datagrams are sent from that socket to the configured `game_bind:game_port`,
and only datagrams from that exact game endpoint go to the corresponding peer.
No network-facing proxy socket is opened.

Unsolicited parent events:

- `{v:1,event:"peer.message",room_id,sender,payload}`.
- `{v:1,event:"room.changed",room_id,owner,members:[...]}` on membership change.
- `{v:1,event:"room.closed",room_id}` on an invalid namespace or lost membership.
- `{v:1,event:"room.selected",room_id,owner,members,metadata}` after a committed
  platform-party follower joins the leader's selected room.
- `{v:1,event:"error",error:"code"}` on a transport failure.

The child must invalidate remote peers on departure and closed events. The
library never forwards generic application requests; only the child defines its
narrower room protocol.

## Private advertisements

Private advertisements let in-game friends exchange connection details before
they share a room. They use reliable channel 49 with an exact
protocol/title/build/realm envelope, and only current in-game friends are
accepted. Eligibility refreshes on friend changes and before local friend
operations. Read-only queries retry the same correlation ID every 500 ms within
five seconds; room mutations are never replayed automatically. Steam session
requests wait for refreshed authorization, and a failed session is reset for a
later attempt. The only operations are query and correlated reply; peers cannot
write another player's advertisement.

Serving a successful query grants that authenticated, compatible friend
permission to start pre-room UDP for the advertisement context. The receiving
library allocates its reverse proxy only when needed; the advertiser need not
look up the friend in return. The grant exists only after a reply for a live
local advertisement has been submitted. Unknown advertisements and invalid
peers grant nothing. Withdrawing an advertisement removes its grants, and losing
the friend's compatible in-game presence revokes them; publishing again
requires a new query. Explicit local friend-endpoint requests are a separate
authorization, and room membership authorizes room transport independently. No
grant creates or commits room or party membership.

`peer.endpoint` also accepts `{peer,scope:"friend"}` before room membership. This
socket accepts only game datagrams from that current in-game friend; room
messages still require membership. When those peers join a common room, the same
socket switches to that room's envelope, which keeps addresses already returned
by friend queries valid and allows the first datagram before a reverse proxy
exists. A friend-scoped socket returns to friend scope after leaving the room; a
room-only socket is removed when its membership ends. The companion chooses
which advertisement contexts it allows and what they contain.

## C API access

The same operations are available to diagnostic hosts through `OSW_P2P` (8):
metadata is the request object; result metadata is the reply result or error.
`events.poll` drains at most 32 diagnostic events. Hosts that use a companion
need only the companion configuration, identity, token and the platform API
operations.
