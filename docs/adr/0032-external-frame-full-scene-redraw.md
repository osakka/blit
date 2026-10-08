# ADR 0032: Redraw the complete SDL scene for external Hibr commits

Status: Accepted

## Context

External Hibr frames are atomic at the surface boundary, but the SDL renderer
was still allowed to consume their damage rectangle incrementally from the
cached scene texture. A previous glyph or background could therefore survive
outside the current damage region and appear to bleed into the next frame.

## Decision

Whenever an external surface has committed damage, Blit rebuilds the complete
SDL scene for that output from the canonical surface state before presenting.
Ordinary terminal surfaces retain incremental damage rendering. The external
Hibr path is correctness-first because its frame boundary already guarantees a
complete logical frame.

## Consequences

External Hibr presentation cannot retain stale pixels across commits. The
additional work is bounded by the logical display size and is measured through
the existing render diagnostics; it does not alter the canonical surface or
create a second renderer.
