# Contributing

Keep changes focused on a reusable platform library. Describe the observable
behavior, the integration contract it changes and how it was checked. The
project name is written **orbiSteamworks**; the repository and library are
`orbisteamworks`.

## Component boundaries

- Keep the public interface C-compatible and defined in
  `include/orbisteamworks/osw.h`. No C++ object, exception, Steam handle or
  allocation ownership crosses it.
- Keep existing ABI layouts and defaults. New behavior is opt-in, negotiated
  through explicit capabilities; incompatible configurations fail instead of
  falling back silently.
- Confine Steam symbols and API declarations to `src/steam/`. Core code carries
  no Steam, guest or host types.
- Keep guest ABI and socket translation in the host. Game packet formats,
  address formats, progression, economy and server rules belong to the
  application or its companion.
- Integrate through the documented C API or companion protocol. Do not depend
  on sibling repositories, private profiles or another project's build output.
- Preserve unsupported-operation and signed-out failures. Never fabricate
  identity, tokens or successful operations. Test backends must remain explicit
  and must never replace failed Steam initialization.
- Bound individual requests, but do not add idle or elapsed-time limits to
  established sessions.

## Validation

Use the commands in [Building](docs/BUILDING.md). Add contract tests when a change
affects ownership, async completion, cancellation, teardown, routing or public
behavior. Exercise both static and shared builds when changing the C boundary.

Record the platform, configuration and checks actually run. Report automated
tests separately from checks with live Steam accounts, games or several machines,
and do not infer Linux or macOS support from Windows results.

## Repository contents

Write documentation in English. Keep generated builds in `build/`, local tools
in `out/` and private diagnostic state in `runtime/`; these paths are ignored.
Never include Steamworks SDK files, credentials, tickets, account data, game
assets or source copied from a host or server project. Use public documentation
and lawfully obtained material only. When the declared Steam flat-API subset
changes, record the SDK version it was checked against in
[Sources](docs/SOURCES.md).

Keep attribution and license notices intact. Changes to the public API or pipe
protocol must update their specification, including compatibility and ownership.
