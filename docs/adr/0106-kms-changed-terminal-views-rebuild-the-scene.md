# ADR 0106: Changed KMS terminal views rebuild the scene

## Status

Superseded by [ADR 0108](0108-surface-local-kms-damage-boundary.md) —
2026-10-17.

## Decision

When an unsynchronized terminal view changes, direct KMS rebuilds the complete
persistent CPU scene for that output before submitting the page flip. The
change is not presented through a partial damage rectangle.

## Context

The incremental KMS path retained stale pixels when a fast terminal workload
coalesced clear, movement, and replacement writes before one refresh. The
authoritative surface contained the final cells, but the scanout still showed
an earlier moving label. Recreating the surface forced a full redraw and
removed the stale pixels, isolating the defect to incremental scene reuse.

## Consequences

- Terminal movement, erase, scroll, and graphics changes share one atomic
  complete-scene boundary.
- The scene cache remains useful for copying the composed frame into the
  scanout buffer, but terminal updates no longer rely on partial KMS damage.
- Runtime timing must be measured at the display's refresh interval; a frame
  is not accepted merely because the PTY write completed.
- Synchronized DEC 2026 views retain their existing hold semantics and are
  not forced into an early release by another member's update.

## Verification

The live API test updates one lower-pane shell with a rapidly moving colored
label, clears it, and writes `done`. The final surface summary and screenshot
must both contain only the final text; the screenshot must contain no prior
moving label. The KMS composition gate and runtime present counters remain
mandatory before packaging.
