# ADR 0032: Redraw the complete SDL scene for external Hibr commits

Status: Superseded by ADR-0036

## Context

External Hibr frames are atomic at the surface boundary, but the SDL renderer
was still allowed to consume their damage rectangle incrementally from the
cached scene texture. A previous glyph or background could therefore survive
outside the current damage region and appear to bleed into the next frame.

## Decision

This decision was intentionally conservative but made every external Hibr
cell commit expensive. It is superseded by ADR-0036. The original full-scene
rule remains the reference for diagnosing stale graphics, but is no longer
the normal cell-rendering policy.

## Consequences

The full-scene cost was bounded by the logical display size, but it prevented
the pointer cache from being used when Hibr committed hover/motion updates.
