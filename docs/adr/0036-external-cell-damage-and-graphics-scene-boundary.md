# ADR 0036: Separate external cell damage from graphics-scene invalidation

Status: Accepted

## Context

Hibr external surfaces have an atomic logical frame boundary: deferred cell
chunks must not be presented until the commit chunk arrives. The previous
policy rebuilt the complete SDL scene for every committed external cell frame.
On a desktop, pointer motion can cause Hibr to commit a small hover update for
each motion report. Rebuilding a full 1920x2160 wallpaper scene for each such
update made pointer presentation visibly choppy and prevented the pointer
scene cache from being used.

## Decision

Keep the committed external cells in the canonical surface and apply their
tracked damage rectangle to the cached SDL scene texture. The renderer still
does not consume deferred chunks because `frame_open` suppresses rendering
until commit. Rebuild the complete scene only when the graphics scene changes:
an image is added, replaced, moved, hidden, deleted, or otherwise changes its
visibility/content, or when the output is being initialized or its topology
changes.

The graphics signature is output-specific and includes image identity,
generation, revision, visibility, placement, and z-order. Pointer-only frames
continue to composite the software pointer over the committed scene texture.

## Consequences

External cell commits preserve atomicity while using incremental damage. Image
state changes retain the correctness-first full rebuild required to remove
stale wallpaper pixels. Diagnostics expose `external_changed`,
`graphics_changed`, and `pointer-cache` so a regression is distinguishable
from an input-device or refresh failure. The behavior is covered by the TCC
`EXTERNAL_POINTER_COMPOSITION_PASS` gate.
