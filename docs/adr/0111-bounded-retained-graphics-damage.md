# ADR 0111: Bound retained-graphics damage by old and new coverage

- Status: accepted
- Date: 2026-10-17
- Scope: direct-KMS graphics presentation and retained scene damage

## Context

The direct-KMS presenter already retained a complete CPU scene and preserved
an atomic page-flip boundary, but every Kitty/Sixel/API graphics mutation
forced the entire terminal view to be redrawn. A full-screen wallpaper then
made a small moving image pay for a full output-sized composition on every
update, which is incompatible with the display-refresh and interactive-motion
requirements.

Graphics are not cell-aligned: Kitty supports pixel offsets, source crops,
scaling, multiple placements, and deletion. Repainting only the new rectangle
would leave the old placement behind; repainting only the old rectangle would
omit the new one.

## Options considered

1. Keep repainting the complete view. This is pixel-safe but needlessly
   expensive for small graphics mutations.
2. Repaint only the current graphics rectangle. This leaves stale pixels when
   a placement moves or is deleted.
3. Retain the previous and current graphics bounds, quantize both outward to
   complete cells, repaint their union from the persistent scene, and fall
   back to a complete view when a bound cannot be proven. Chosen.

## Decision

The direct-KMS presenter records one bounded graphics rectangle per presented
surface. On a graphics-signature change it computes the current rectangle
from the shared flattened draw list and unions it with the previous rectangle.
Pixel offsets are rounded outward to cell coordinates and clipped to the
surface. The resulting damage is composed into the persistent scene and
submitted through the existing one-page-flip boundary.

If no valid previous or current rectangle exists for a changed graphics scene,
the presenter uses the existing complete-view rebuild. Surface-origin,
topology, output, effect, and view-count changes retain their existing
full-output behavior. SDL keeps its ping-pong scene policy; this decision does
not create a second graphics implementation or alter Hibr.

## Consequences

Small retained-image movement no longer requires repainting a full wallpaper.
The old and new coverage are both restored before publication, so replacement
and deletion cannot leave trails. The bounded rectangle is conservative: it
may repaint neighboring cells, but it never excludes a pixel that could be
affected by the graphics scene mutation.

The presenter stores a small bounded rectangle table per output surface. The
optimization remains fail-closed and is not used as evidence of a particular
physical frame rate until a live workload measures it.

## Evidence

- `tools/tests/kms_compose_gate.sh` passes `KMS_COMPOSE_PASS full=512` and
  asserts pixel-offset bounds, multi-placement union, equal-z ordering,
  placement replacement, and deletion without stale pixels.
- The TCC-built server passes the renderer suite:
  `RUN_GATES_PASS pass=47 skip=0 fail=0`.
- Kitty placement, graphics trace, SDL graphics-band, and SDL wallpaper-damage
  gates pass against the same TCC build.
