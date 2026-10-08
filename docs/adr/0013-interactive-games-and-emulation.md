# ADR 0013: Interactive games and emulation are first-class workloads

- Status: accepted
- Date: 2026-10-08

## Context

Blit is intended to host Hibr sessions, including terminal-native games and
frontends for arcade and console emulation. Correct snapshots alone are not
enough: input must be delivered promptly, presentation must remain smooth,
and terminal grammar and fonts must not corrupt the game display.

Kitty graphics and sixel are useful compatibility paths for bounded images,
but they are not the canonical high-rate frame transport for an emulator.

## Decision

Games have two paths:

1. Terminal-native games and text-mode emulator frontends use the canonical
   PTY, VT500/UTF-8 terminal state, input routing, and text renderer.
2. Pixel-oriented arcade and emulator workloads use a bounded API-owned
   graphic canvas/frame path. It shares Blit's output scheduler, input routes,
   diagnostics, and display placement, but does not encode every frame as
   terminal cells or require Kitty/sixel parsing.

Both paths remain controlled through the versioned API and use the same output
and input ownership model. There is no second display implementation and no
special Hibr launcher or shell wrapper.

## Acceptance requirements

- Outputs present at their selected mode's refresh rate unless an explicit
  lower cap is requested and reported.
- Input is timestamped from device receipt through routing and presentation,
  with bounded queue depth and observable dropped-event counts.
- Terminal games pass alternate-screen, cursor, colour, UTF-8, wide/combining
  glyph, mouse-reporting, keyboard, and resize tests without corruption.
- The pixel path provides frame pacing, damage or buffer reuse, back-pressure,
  and a diagnostic frame counter; it must not block the API thread on a slow
  client.
- One-output and multi-output placement are tested, including independent
  refresh rates and output loss/reconnect.
- An arcade/emulation release claim requires an end-to-end fixture that
  injects input, presents frames, and proves stable timing. API success or a
  screenshot alone does not count.

## Consequences

Terminal fidelity and pixel throughput are measured separately while sharing
the same scheduler and public diagnostics. Kitty/sixel remain supported for
compatibility and overlays, but are not treated as the emulator transport.
The pixel canvas/frame schema must be designed before claiming arcade support.
