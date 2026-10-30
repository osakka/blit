# ADR 0082: Atomic presentation boundary for one Hibr desktop

- Status: accepted; revised 2026-10-29 (0.3.13), see the end
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

## Revised (0.3.13): one monitor's views, and only views that are drawing

Joined views became independent viewports on their own monitors (ADR 0127),
and a quiet view stopped being a degraded condition: it is a monitor nobody
is drawing on. The barrier still waited for it, a full refresh, on every
frame of the view in use. On the owner's machine, 2026-10-29, in 30 s of
ordinary use with a game in the primary view: 311 barriers, 251 of them with
1 of 3 views dirty, 254 timeouts, and 386 / 403 missed refresh deadlines on
the two monitors.

- **Per output.** A barrier groups the Hibr views on one output. Two
  monitors flip at their own vblanks, so no barrier could show a frame on
  both together; views on different monitors never wait for each other.
  The deadline and the held scene are per output: a held monitor presents
  only its pointer, the others present as usual.
- **Only views that are drawing.** A view that let a barrier time out is
  not waited for again until it draws again. After one second with no view
  on that output drawing -- longer than a pause between keystrokes -- every
  view is waited for again, because a redraw of the whole desktop after a
  pause is the case this barrier exists for.

`hibr_join_frame_barrier_gate.rb` keeps that case: two quiet views redrawn
4 ms apart are presented together. `hibr_frame_barrier_scope_gate.rb`
proves the revision: with one view quiet and the other drawing for 75
frames there is one timeout (0.3.12: 42), and views 4 ms apart on two
monitors open no barrier.

Blit is guessing when a redraw spans views; Hibr's hold layer knows. A
begin/end mark from hold around a redraw of several views would let Blit
drop the guess.

