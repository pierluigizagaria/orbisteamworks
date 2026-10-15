# orbiSteamworks quickstart

orbiSteamworks lets a PS4 game running in a supporting host use Steam for
identity, friends, platform sessions, invitations and ID tokens. A private
publisher server then admits players by SteamID. The first target is CTR
Nitro-Fueled (CUSA13795 1.21) in a shadPS4 fork, with the Demonware Server as
publisher.

There is no release. The only host is a local shadPS4 fork that you build from
source with `ENABLE_ORBISTEAMWORKS=ON`. Evidence, gates and lab history are in
[docs/](docs/); this page covers setup only.

## AppID 480 limits

orbiSteamworks runs as Spacewar (AppID 480), permanently. There is no other
AppID.

- Your Steam friends see you "Playing Spacewar", not CTR.
- Invitations work only while the game is running. Accepting one with the game
  closed launches Spacewar.
- Lobbies share Spacewar's namespace with every other project that uses 480.
  orbiSteamworks ignores lobbies from other projects, titles, builds and realms.
- Valve tolerates this use but does not support it, and may change it.
- It suits private groups and small communities. It is not a public product.

## Players

You need:
- **Steam,** installed, running and signed in. Steam allows one account per
  PC, so each player needs a separate PC.
- **Valve's Steam API library, which you supply yourself.** No orbiSteamworks
  build contains Steamworks files.
- **A host with the Steam provider.**

1. **Get the Steam API library.** Download the Steamworks SDK from Valve at
   <https://partner.steamgames.com/> and accept Valve's terms. Copy
   `redistributable_bin/win64/steam_api64.dll` (Windows) or
   `redistributable_bin/linux64/libsteam_api.so` (Linux) next to the host
   executable, or set its path in `steam_library`. Tested: SDK 1.65, Windows x64.
2. **Choose your online ID:** 3–16 characters, a letter first, then letters,
   digits, `_` or `-`.
   - Your first login to a server binds this name to your SteamID permanently,
     including letter case.
   - Nobody else on that server can take it, in any letter case.
   - Your Steam persona name is not used.
3. **Configure the host.** In the shadPS4 fork's network settings:

   | Setting | Value |
   | --- | --- |
   | `np_provider` | `steam` |
   | `steam_online_id` | your online ID |
   | `steam_library` | path to the Steam API library (empty: next to the executable) |
   | `steam_title`, `steam_build` | `5760`, `1.21` for CTR |
   | `steam_realm` | the realm your operator gives you |
   | `steam_publisher` | the operator's `https://` publisher address |
   | `steam_publisher_ca` | the operator's CA certificate, if it is not publicly trusted |

4. **Start Steam first, then the game.**
   - If the Steam library or Steam itself is missing, the provider stays signed
     out and the host log states why. Nothing pretends to sign in.
   - Only Steam friends seen running orbiSteamworks with the same title and
     realm appear in the game.

## Operators

You run the Demonware Server
([demonware-server](https://github.com/pierluigizagaria/demonware-server))
with Steam admission. It starts the orbiSteamworks auth helper, which logs on
to Steam as an anonymous game server and validates player tickets. You need no
Steam Web API key, account password or public query port.

1. **Build the helper.** Build `osw-auth-helper` from this repository with
   CMake and C++20. It needs the nlohmann/json headers (`OSW_JSON_INCLUDE_DIR`)
   and no Steamworks SDK.
2. **Put the Steam libraries next to the helper.**
   - Copy Valve's `steam_api64.dll` or `libsteam_api.so` from the SDK, as in
     player step 1, or point `CTR_STEAM_LIBRARY` at it.
   - The helper also needs Valve's game-server runtime (on Windows,
     `steamclient64.dll` with its companions). You can take it from a Steam
     client installation on the same host, or from Valve's "Steamworks SDK
     Redist" downloaded anonymously with SteamCMD:

     ```text
     steamcmd +login anonymous +force_install_dir <dir> +app_update 1007 validate +quit
     ```

   - **Qualification:** only the Steam client installation has been tested
     (Windows). The SteamCMD layout, and any Linux helper deployment, are not
     yet qualified.
3. **Configure the server** (see `docs/STEAM_ADMISSION.md` and `example.env`
   there):

   ```text
   CTR_ADMISSION=steam
   CTR_STEAM_HELPER=<absolute path to osw-auth-helper>
   CTR_STEAM_REALM=<1-64 letters, digits, _ or ->
   CTR_STEAM_LIBRARY=<optional; default is next to the helper>
   DATA_DIR=<a fresh directory>
   ```

   - Leave all `SHADNET_*` settings empty. Existing shadNet or guest databases
     are refused, not migrated.
   - Run the normal and unlock-all policies as separate instances, each with
     its own realm and data directory.
4. **Publish the address.** Give players the HTTPS publisher address, your
   realm and, for a private CA, its certificate. Clients fetch the helper
   identity from `GET /orbi/steam/v1` and refuse a mismatched title or realm.
5. **Keep the helper healthy.**
   - If the helper is down or times out, new admissions are refused, and
     players already connected stay connected.
   - Restart the server to recover the helper.
   - Players' names and consumed ticket hashes persist in `DATA_DIR`.

For other private servers that want to accept the same Steam identity, the
token and helper protocol are specified in
[docs/OSW1_TOKEN_AND_HELPER.md](docs/OSW1_TOKEN_AND_HELPER.md).
