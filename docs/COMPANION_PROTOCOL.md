# Local companion pipe, version 1

2026-10-16. Replacement contract (I). UTF-8 newline-delimited JSON on inherited
stdin/stdout. A frame is at most 98,304 bytes including newline; queues are
bounded. Logs use stderr and never contain tokens, peer payloads or private IDs.
On Windows stderr is discarded by default; the environment map may explicitly
set `OSW_COMPANION_STDERR` to a private UTF-8 log-file path for diagnostics.
All account/lobby/request IDs are decimal strings, except correlation `id`,
which is an opaque nonempty string of at most 64 bytes. Every frame has `v:1`.
Unknown operations fail closed. The child exits on stdin EOF; the parent owns
the child in a Windows kill-on-close Job Object. No session-idle timeout exists.
On POSIX the host must leave this child exclusively to the library for waiting
and reaping throughout the context lifetime. Launch refuses `SIGCHLD=SIG_IGN`
and `SA_NOCLDWAIT` without changing the host's signal disposition. A host that
concurrently reaps this child or changes its policy violates that ownership
contract; PID-based shutdown does not provide a stable-process-handle guarantee.
Linux runtime acceptance remains pending.
The library sets generic child `OSW_COMPANION_PROTOCOL=1`. Provider-specific
settings belong in the caller's environment map; for the CTR publisher these
include `CTR_COMPANION=stdio`, `CTR_ADMISSION=local` and
`CTR_MATCHMAKING=steam`. The library does not interpret these settings.

Parent first sends `{v:1,event:"owner",steam_id,online_id,token,title,build,realm,
backend:"steam"|"lab"}`. The child must receive this within ten seconds.
`token` is an `osw1` envelope with a random 32-byte per-process capability in
the ticket field. This is local admission only: exact token plus parsed owner
ID must match. It is never a Steam ticket and never goes to an auth helper or
peer. Native OSW_TOKEN requests in companion mode return that same envelope.
After all listeners and storage are ready the child sends
`{v:1,event:"ready"}`. The parent exposes readiness via P2P `status`.

A child supporting live owner-name changes advertises
`{v:1,event:"ready",capabilities:["owner.name.v1"]}`. Capabilities contain at
most 16 strings of at most 64 bytes. Only after that advertisement may the
parent send `{v:1,event:"owner.name",steam_id,online_id}`: these four fields
are the complete frame. The ID must equal the initial owner and the new name
must be valid UTF-8 of 1–16 bytes without control characters or surrounding
spaces. No token, namespace, account or ownership change is permitted.
The child applies these messages in pipe order, independently of matchmaking.
Authentication and name persistence use the current trusted label. Existing
sessions, PID, subject, database namespace and the exact bootstrap capability
remain unchanged. The name embedded in that capability is its initial label,
not an instruction to revert a later trusted rename. Neither credentials nor
the name-update frame leave the inherited local pipe.

Without this advertised capability the library retains its initial identity
label for that context, rather than sending an event an old child cannot read.
P2P `status.owner_name_updates` exposes support. A caller's explicit online-ID
override remains fixed. Automatic Steam persona changes update the C API
identity and presence; only already committed party memberships are renamed.
Name changes emit identity/session-update events, never artificial join/leave.

Child requests: `{v:1,id,op,...}`. Parent replies:
`{v:1,id,ok:true,result:{...}}` or `{v:1,id,ok:false,error:"code"}`.
Requests complete within 25 seconds; the child should allow 30 seconds.

| op | Request fields | Result |
| --- | --- | --- |
| directory.create | `metadata` object, `capacity` 1..16 (default 8) | room record |
| directory.search | `metadata` object, optional `limit` 1..32 | `rooms` array |
| directory.join | `room_id` | room record |
| directory.leave | `room_id` | empty object |
| directory.update | `room_id`, `metadata` object | room record |
| directory.members | `room_id` | current room record |
| peer.send | `room_id`, `peer`, `payload` standard padded base64 | empty object |
| peer.endpoint | `room_id`, `peer` | `address` IPv4 loopback text, `port` uint16 |
| name.resolve | `peer` | `online_id` or an explicit unavailable error |
| name.lookup | `names` array, at most 500 online IDs | `players:[{steam_id,online_id}]`, at most 100 current in-game friends |
| advertisement.set | `context` ASCII namespace <=32 bytes, `payload` base64 <=4096 decoded bytes; empty clears | empty object |
| advertisement.get | `peer`, `context` | `payload` from that friend's private advertisement, or explicit not-found |
| status | none | `ready`, `backend` |

A room record is `{room_id,owner,members:[decimal ID strings],metadata:{...}}`.
Search results for rooms not yet joined have an empty `members` array and use
the advertised owner. Steam permits actual owner/member queries only after
joining. Join completion verifies the actual owner against that advertised owner
before the record is accepted; joined records contain the actual members.
Metadata is public: only `playlist`, `type`, `free_slots` scalar strings/numbers
are accepted, <=256 encoded bytes. Library-added namespace fields are
`osw/1`, title, build, realm, room protocol `1`; they cannot be overridden.
No security IDs/keys, credentials, publisher files or user progress are allowed.
`search` filters exact supplied metadata, excluding `free_slots`, and requests
at least one open slot; it widens Default, Far, Worldwide when empty.

Messages carry <=32,768 decoded bytes. Reliable room traffic uses channel 47;
race datagrams use channel 48 and <=65,507 bytes. The library authenticates the
sender using provider identity and only accepts current room members. The
publisher additionally checks its room protocol allow-list and claimed PID.
The owner may send a push for another player, but never impersonates the
authenticated sender. `peer.endpoint` allocates a unique local UDP socket for
that remote peer. Incoming peer race packets are sent FROM that socket TO the
configured local `game_bind:game_port`. Only datagrams from that exact game
endpoint are sent to the corresponding remote peer. No network-facing proxy
socket is opened.
The simulated UDP carrier reserves another 20 bytes for its local headers and
therefore accepts game datagrams up to 65,487 bytes; gameplay packets tested so
far are much smaller. This is a lab transport limit, not a Steam measurement.

Unsolicited parent events:

- `{v:1,event:"peer.message",room_id,sender,payload}`.
- `{v:1,event:"room.changed",room_id,owner,members:[...]}` on membership change.
- `{v:1,event:"room.closed",room_id}` on invalid namespace or lost membership.
- `{v:1,event:"room.selected",room_id,owner,members,metadata}` after a committed
  platform-party follower joins the leader's selected room.
- `{v:1,event:"error",error:"code"}` on a transport failure.

The child must invalidate remote peers on departure/closed events. The engine
does not migrate automatically if its owner leaves. The library never forwards
generic publisher requests: only the child defines the narrower room protocol.

The platform-party owner publishes its selected invisible room in member-only
party data. Only members whose game has committed platform membership follow
that selection; provisional invitation joins do not qualify. Create/join waits
within its existing deadline for the committed followers to join. Owner change
closes the room, rather than migrating its publisher engine silently.
Timeout or cancellation rolls back the introduced room membership and selection.
An explicit `directory.leave` by a follower suppresses automatic following of
that same selected room. Retained platform-party membership alone does not
undo the leave. A valid explicit create/search/join request, a different selected
room, or a new party/owner context permits following again. There is no timer
that silently re-enables membership. Pending automatic joins are abandoned on
an explicit leave and any later successful completion is left immediately.
A new join to the same room returns `busy` until that abandoned provider call
completes and is cleaned up, so a late callback cannot remove a newer membership.
Pending validations, provider calls and abandoned calls share a limit of 128;
admission returns `busy` at that limit. Abandoned calls are never forgotten by
a timer. If the provider permanently loses their callbacks, affected rooms stay
busy, and exhausting the shared limit requires a new library context to recover.
An explicit rejoin to the leader's selection resumes following, including leave
when the leader changes rooms or the local player leaves the platform party.

Private advertisements use dedicated reliable channel 49 with an exact
protocol/title/build/realm envelope. Only current in-game friends are accepted.
Eligibility refreshes on friend changes and before local friend operations.
Read-only queries retry the same correlation ID every 500 ms within five seconds;
room mutations are never automatically replayed. Steam session requests wait
for refreshed authorization, and a failed session is reset for a later attempt.
The only operations are query and correlated reply; peers cannot write another
player's advertisement. `peer.endpoint` also accepts `{peer,scope:"friend"}`
before room membership, enabling the game's friend-advertisement address fields.
This explicitly allocated endpoint accepts only race datagrams from that current
in-game friend; room messages still require room membership. The publisher
chooses an allow-listed advertisement context and never exports local economy.
When those peers join a common room, the same socket automatically uses that
room's envelope. This preserves addresses already returned by friend queries
and permits the follower's first race packet before a reverse proxy exists.
An explicitly friend-scoped socket can return to friend scope after room leave;
a room-only socket is removed when its membership ends.

The same operations are available to diagnostic hosts through `OSW_P2P` (8):
metadata is the request object; result metadata is the reply result or error.
`events.poll` drains at most 32 diagnostic events. Production hosts need only
the companion config, identity, token and existing platform API operations.
Lab mode is explicitly simulated, selected only by C API v2 configuration.
