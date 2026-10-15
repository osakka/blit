# ADR 0081: Physical Hibr joins require a 2-D union contract

- Status: accepted
- Date: 2026-10-15
- Scope: physical pane joins, Hibr desktop geometry, input coordinates, and
  joined wallpaper
- Depends on: [ADR 0079](0079-hibr-desktop-uses-the-native-pty-boundary.md)
- Supersedes: the vertical-join acceptance portion of
  [ADR 0080](0080-map-blit-panes-to-hibr-hold-union-coordinates.md)

## Context

Blit owns physical pane placement and presents each native Hibr PTY viewport
in its pane. Hibr owns the desktop scene and the hold union. Hibr's current
desktop layout reads the primary column but not the primary row, and its hold
picture rule does not emit a spanning Kitty/Sixel image to a client whose
rectangle contains only part of that image. Blit 0.1.481 therefore produced a
visually populated primary below an incomplete joined viewport above it.

The compatibility workaround projected physically stacked panes into a
horizontal Hibr union. That made the primary bar remain on the physical
primary, but it changed the logical coordinate space. Before the corresponding
input translation was added, a click in the upper pane was routed to the
primary with pane-local coordinates, so the pointer and Hibr context-menu
location disagreed. Even after that translation, the projection cannot make a
horizontal desktop scene become a correct 2-D physical desktop.

## Decision

- Blit remains standalone and does not modify Hibr source.
- Blit may retain the horizontal projection as an explicitly diagnostic and
  compatibility mode. Every pointer motion, button, wheel event, and related
  diagnostic must use the same projected Hibr union coordinate transform.
- A vertical or 2x2 physical Hibr join is not product-accepted until Hibr
  provides a true 2-D union contract for client rectangles, desktop layout,
  input rewriting, and clipped picture propagation.
- The required Hibr-side work is tracked in
  [Hibr #240](https://git.home.arpa/itdlabs/hibr/issues/240). Picture-specific
  propagation remains tracked in
  [Hibr #238](https://git.home.arpa/itdlabs/hibr/issues/238).
- Blit acceptance requires screenshots and deterministic cell/input evidence;
  a distinct PTY or a passing API route alone is insufficient.

## Acceptance criteria

For a 1x2 stack with the primary physically below the join, and then a 2x2
layout:

1. `hold clients` reports stable 2-D rectangles with the expected row and
   column offsets.
2. The complete Hibr desktop scene, including wallpaper and Kitty/Sixel
   placements, is clipped into every client rectangle without mirroring.
3. Pointer motion, left/middle/right buttons, wheel, window movement, and
   context menus use the same physical-to-Hibr coordinate transform.
4. A fresh screenshot, per-client cell snapshot, and diagnostic input trace
   agree at one frame boundary.
5. Reconnect and resize preserve the same geometry without stale pixels or
   duplicate desktop owners.

Until all five hold, the release ledger must call the result blocked rather
than “working” or “accepted”.
