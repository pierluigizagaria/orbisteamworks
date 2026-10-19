# Sources and compatibility scope

orbiSteamworks implements a replacement platform provider. Its public C API,
Steam lobby namespaces, `osw1` credential format and companion pipe are project
contracts, not Sony or Valve protocols.

## Steam references

The runtime loader declares only the flat-API subset used by the implementation.
Its declarations were checked against Steamworks SDK 1.65. SDK headers and
runtime libraries are not included in this repository.

- [API overview](https://partner.steamgames.com/doc/sdk/api): initialization and callbacks.
- [ISteamFriends](https://partner.steamgames.com/doc/api/ISteamFriends): identity, friends and invitations.
- [ISteamUser](https://partner.steamgames.com/doc/api/ISteamUser) and
  [authentication](https://partner.steamgames.com/doc/features/auth): client tickets.
- [ISteamGameServer](https://partner.steamgames.com/doc/api/ISteamGameServer): native server ticket validation.
- [ISteamMatchmaking](https://partner.steamgames.com/doc/api/ISteamMatchmaking): lobby metadata, membership and search.
- [Networking Messages](https://partner.steamgames.com/doc/api/ISteamNetworkingMessages): opaque peer messages.
- [Steam Datagram Relay](https://partner.steamgames.com/doc/features/multiplayer/steamdatagramrelay): Steam transport facilities.
- [Overlay guide](https://partner.steamgames.com/doc/features/overlay): overlay integration.
- [Spacewar example](https://partner.steamgames.com/doc/sdk/api/example): AppID 480 reference application.

Steam documentation describes available facilities; it does not establish
compatibility with a particular host or game. The client AppID is configurable;
the separate central authentication helper currently remains fixed to 480.

## Integration references

The public boundary is independent of a host. Its consumers can translate their
own guest APIs without copying host implementations into this library. Relevant
open-source integration surfaces include
[shadPS4](https://github.com/shadps4-emu/shadPS4) and the
[GetLostIn native port](https://github.com/GetLostIn/CTR-NitroFueled-PC).
Their code and licenses remain separate.

[C API](C_API.md), [companion protocol](COMPANION_PROTOCOL.md) and
[token/helper protocol](OSW1_TOKEN_AND_HELPER.md) specify the implemented
contracts. Unsupported operations fail explicitly. Automated fixtures verify
software contracts and do not establish general game or network compatibility.
