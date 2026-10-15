# ADR 0082: Atomic presentation boundary for one Hibr desktop

- Status: accepted
- Date: 2026-10-15
- Scope: SDL presentation scheduling for native Hibr primary and join PTYs
- Depends on: [ADR 0072](0072-refresh-bound-damage-coalescing.md),
  [ADR 0079](0079-hibr-desktop-uses-the-native-pty-boundary.md), and
  [ADR 0081](0081-hibr-joins-require-a-2d-union-contract.md)

## Context

Blit presents one physical output containing multiple panes. A native Hibr
desktop is one logical scene, but its primary and joined hold clients deliver
their terminal updates through separate PTYs. The updates therefore do not
share a kernel read boundary. Presenting the first dirty viewport immediately
can expose one half of a joined desktop for a frame before the other viewport
has arrived, which appears as a shiver or a mixed frame while the desktop is
redrawing.

This boundary is independent of Hibr's picture protocol. The current live
evidence contains Kitty query/delete traffic but no image upload, so Blit must
not claim to have repaired a missing wallpaper image. Picture propagation and
true 2-D Hibr union semantics remain Hibr-owned dependencies tracked by ADR
0081.

## Options considered

1. Present every PTY update immediately — lowest latency, but permits a
   partial logical desktop frame and reproduces the visible joined redraw
   artifact.
2. Wait indefinitely for every primary/join PTY — visually coherent when all
   clients are healthy, but a quiet or disconnected join would freeze the
   primary desktop.
3. Use a bounded one-refresh group barrier — coalesce the active Hibr members
   until all have contributed damage, then release; if a member is late, release
   at the display refresh deadline.

## Decision

Choose option 3. When a Hibr primary or join surface becomes dirty, Blit opens
one barrier for that logical Hibr desktop. The deadline is one selected output
refresh interval, or 16 ms when no output mode is available. If every active
member becomes dirty before the deadline, the barrier releases immediately.
Otherwise it releases at the deadline and presents the available state.

The barrier is presentation-only:

- each PTY remains the sole source of its own terminal viewport;
- joins are not framebuffer mirrors;
- no cell, Kitty image, or Sixel payload is synthesized;
- a continuous producer cannot extend an open barrier;
- a missing or quiet join cannot stall the primary indefinitely.

## Consequences

Joined Hibr redraws have a bounded additional latency of at most one refresh
interval and no longer present the first member as a standalone frame when the
other member is already in flight. A late or silent member can still produce a
timeout release, which is observable in the diagnostic trace as
`hibr_frame_barrier_timeout`; that is a degraded upstream/session condition,
not an invisible wait.

This does not make a vertically stacked physical layout a supported Hibr 2-D
desktop, and it does not solve Hibr picture propagation. Those remain blocked
until the Hibr contracts in ADR 0081 are available.

## Evidence

- `tools/tests/hibr_join_frame_barrier_gate.rb` sends primary and joined PTY
  redraws in separate reads and verifies that no `render_path` event occurs
  between barrier open and release.
- `ruby tools/tests/hibr_join_frame_barrier_gate.rb` passes with an
  `hibr_frame_barrier_release`.
- Live 0.1.482 diagnostics: SDL renderer `opengles2`, selected refresh 60 Hz,
  target interval 16 ms, no missed deadlines in the current measurement
  window, and zero decoded graphics images.
