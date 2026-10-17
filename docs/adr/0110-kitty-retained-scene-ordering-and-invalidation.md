# ADR 0110: Kitty-retained scene ordering and invalidation

- Status: accepted
- Date: 2026-10-17
- Scope: Kitty graphics composition, scene signatures, SDL and direct KMS
  presentation

## Context

Kitty graphics are a retained scene, not a sequence of pixels that can be
painted in parser or allocation order. Equal-z references need a stable
ordering when image slots are allocated differently, and replacing or deleting
a placement must invalidate the old coverage as well as the new coverage.
Otherwise a moving image can leave trails, or SDL and direct KMS can disagree
about which overlapping image is visible.

Kitty's reference behavior sorts visible references by z, image id, and
placement/reference id. Its graphics layer is marked dirty for mutations,
including placement movement and deletion, before the next presentation.

## Options considered

1. Preserve array-discovery order and rely on current allocation behavior. This
   is not deterministic and cannot define equal-z overlap across sessions or
   protocol traces.
2. Sort only by z. This makes bands correct but leaves equal-z overlap
   dependent on image-slot history.
3. Flatten every visible placement and sort by `(z, image id, placement id)`,
   while making the scene signature include every render-affecting field.
   Chosen.

## Decision

`blit_graphics_collect()` is the sole renderer-neutral draw-list constructor.
It resolves default and numbered placements, then orders them ascending by z,
image id, and placement id. Both SDL and direct KMS consume this same list;
neither renderer may walk image slots independently.

The graphics scene signature includes image dimensions, format, opacity,
protocol, scroll anchoring, payload generation/revision, default placement,
and all active numbered-placement fields. A change to any field that can alter
the composed result is therefore a scene change and reaches the existing
atomic presentation boundary. The current safe policy remains a full view
rebuild for graphics-scene changes; local dirty-region optimization is a
separate decision and requires pixel proof first.

## Consequences

Equal-z composition is deterministic and independent of allocation order.
Numbered placement replacement and deletion cannot be mistaken for a no-op by
the scene cache. This adds a small bounded sort/signature cost, which is
acceptable because the graphics store is bounded and correctness is more
important than speculative damage reduction.

This does not claim complete Kitty protocol conformance. File/shared-memory
transfers, animation, and other explicitly refused protocol features remain
outside the implemented contract and must continue to fail observably.

## Evidence

- `tools/tests/kms_compose_gate.sh` passes `KMS_COMPOSE_PASS` and asserts
  equal-z image-id ordering, numbered placement replacement, and deletion
  without stale pixels.
- `tools/tests/kitty_placement_gate.rb` passes against the TCC-built server,
  covering crops, offsets, numbered placements, scrolling, and deletion.
- `tools/tests/graphics_trace_gate.rb`, `sdl_graphics_band_gate.rb`, and
  `sdl_wallpaper_damage_gate.rb` pass against the same TCC build.
