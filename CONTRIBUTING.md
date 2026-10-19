# Contributing

Keep changes focused on a reusable platform library. Describe the observable
behavior, the integration contract it changes and how it was checked.

## Component boundaries

- Keep the public interface C-compatible and defined in
  `include/orbisteamworks/osw.h`. No C++ object, exception, Steam handle or
  allocation ownership crosses it.
- Confine Steam symbols and API declarations to `src/steam/`.
- Keep guest ABI and socket translation in the host. Game packet formats,
  progression, economy and publisher rules belong to the application/server.
- Integrate through the documented C API or companion protocol. Do not depend
  on sibling repositories, private profiles or another project's build output.
- Preserve unsupported-operation and signed-out failures. Test backends must
  remain explicit and must never replace failed Steam initialization.

## Validation

Use the commands in [Building](docs/BUILDING.md). Add contract tests when a change
affects ownership, async completion, cancellation, teardown, routing or public
behavior. Exercise both static and shared builds when changing the C boundary.

Record the platform, configuration and checks actually run. Keep offline fixture
results separate from live Steam, game, multi-account and network acceptance.
Do not introduce arbitrary session-idle or elapsed-time disconnects.

## Repository contents

Write documentation in English. Keep generated builds in `build/`, local tools
in `out/` and private diagnostic state in `runtime/`; these paths are ignored.
Never include Steamworks SDK files, credentials, tickets, account data, game
assets or source copied from a host or publisher project.

Keep attribution and license notices intact. Changes to the public API or pipe
protocol must update their specification, including compatibility and ownership.
