# P2P transport

The optional P2P extension supplies room discovery, authorization and opaque
peer traffic through the public [C API](C_API.md). It does not define a game's
matchmaking algorithm, packet format or persistence model.

## Ownership

The provider owns Steam identities, canonical endpoint allocation, peer
authorization, room membership and transport. A host owns guest or application
socket adaptation. A companion may implement application room messages and
matchmaking rules through the [companion protocol](COMPANION_PROTOCOL.md).
Each component builds independently; no shared source tree or private runtime
layout is required.

Peers must agree on AppID, title, build, realm and transport version. AppID is
configurable before startup and defaults to 480. Keep application settings that
should not separate players out of the title, build and realm.

## Endpoint modes

| Mode | Consumer behavior |
| --- | --- |
| Default | Uses provider-supplied per-peer local proxy endpoints |
| `virtual-v1` | Uses stable canonical endpoints and the public route API for host socket translation |
| `loopback-v1` | Uses canonical endpoints bound directly by the provider; no host socket translation is required |

Canonical addresses establish stable peer identities before application address
serialization. The provider checks allocation, collisions and peer authority;
it never interprets or rewrites payload contents. Transport mode is explicit,
and incompatible capabilities fail startup instead of silently falling back.

A loopback endpoint uses the reserved `127.64.0.0/10` range and ports 1024–65535.
The local game's physical source must be a specific loopback endpoint outside
that reserved range. Packets from unrelated local sources are rejected.

A supervisor that discovers the physical source after launch can explicitly
defer registration. Transport stays pending until the supervisor binds one
observed endpoint and the companion acknowledges it. Identity and canonical
addresses stay fixed. There is no packet-based source learning or live rebinding.

## Discovery and traffic

Room discovery uses invisible Steam lobbies, separately from platform parties.
Search filters include namespace, room protocol, application filters and capacity.
The provider checks current membership and owner identity, authenticates senders
and bounds messages, queues and rates. Only authorized room members exchange
room traffic. Credentials and private companion state do not belong in lobby
metadata or peer messages.

Application room operations pass over the companion pipe; opaque UDP traffic
uses the peer transport. An application must explicitly allow its room message
schemas. The provider does not forward arbitrary application service requests.
The room ends when its owner leaves; owner migration is not implemented.

## Lifecycle

The library starts a configured companion with private stdin/stdout pipes and
supervises its lifetime. Windows uses a kill-on-close Job Object. EOF and process
failure end the owned session. Individual operations are bounded.

See [host integration](INTEGRATION.md) for setup, and the C API and companion
references for exact schemas, capabilities and ownership rules.
