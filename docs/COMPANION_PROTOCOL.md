# Local companion pipe, version 1

2026-10-16. Replacement contract (I). UTF-8 newline-delimited JSON on inherited
stdin/stdout. A frame is at most 98,304 bytes including newline; queues are
bounded. Logs use stderr and never contain tokens, peer payloads or private IDs.
All account/lobby/request IDs are decimal strings, except correlation `id`,
which is an opaque nonempty string of at most 64 bytes. Every frame has `v:1`.
Unknown operations fail closed. The child exits on stdin EOF; the parent owns
the child in a Windows kill-on-close Job Object. No session-idle timeout exists.

Parent first sends `{v:1,event:"owner",steam_id,online_id,token,title,build,realm,
backend:"steam"|"lab"}`. The child must receive this within ten seconds.
`token` is an `osw1` envelope with a random 32-byte per-process capability in
the ticket field. This is local admission only: exact token plus parsed owner
ID must match. It is never a Steam ticket and never goes to an auth helper or
peer. Native OSW_TOKEN requests in companion mode return that same envelope.
After all listeners and storage are ready the child sends
`{v:1,event:"ready"}`. The parent exposes readiness via P2P `status`.

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
| peer.send | `room_id`, `peer`, `payload` standard padded base64 | empty object |
| peer.endpoint | `room_id`, `peer` | `address` IPv4 loopback text, `port` uint16 |
| name.resolve | `peer` | `online_id` or an explicit unavailable error |
| status | none | `ready`, `backend` |

A room record is `{room_id,owner,members:[decimal ID strings],metadata:{...}}`.
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

Unsolicited parent events:

- `{v:1,event:"peer.message",room_id,sender,payload}`.
- `{v:1,event:"room.changed",room_id,owner,members:[...]}` on membership change.
- `{v:1,event:"room.closed",room_id}` on invalid namespace or lost membership.
- `{v:1,event:"error",error:"code"}` on a transport failure.

The child must invalidate remote peers on departure/closed events. The engine
does not migrate automatically if its owner leaves. The library never forwards
generic publisher requests: only the child defines the narrower room protocol.

The same operations are available to diagnostic hosts through `OSW_P2P` (8):
metadata is the request object; result metadata is the reply result or error.
`events.poll` drains at most 32 diagnostic events. Production hosts need only
the companion config, identity, token and existing platform API operations.
Lab mode is explicitly simulated, selected only by C API v2 configuration.
