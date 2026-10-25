# ADR 0014: Batch SDL glyph geometry for full-screen terminal redraws

## Status

Retired by [ADR 0184](0184-sdl-is-removed.md): the SDL code it describes was removed.

Accepted.

## Context

Hibr wallpapers are represented by ordinary terminal cells. A full-screen
surface can contain tens of thousands of identical glyphs; submitting one
`SDL_RenderTexture` call per cell makes a redraw CPU-bound even when the glyph
atlas is already resident. The canonical cell grid and glyph mapping must
remain unchanged, and direct KMS must continue to use its existing rasterizer.

## Decision

The SDL adapter queues atlas-backed glyph quads and submits them with one
`SDL_RenderGeometry` batch per damaged surface. Vertex colours carry the cell
foreground, so the atlas remains a single white alpha mask. Underlines,
strike-through, cursors, effects, graphics, and fallback glyphs preserve their
existing ordering by flushing the batch before those operations.

The batch is an SDL presentation optimization only. The OpenAPI surface,
terminal parser, PSF mapping, cell widths, direct-KMS path, and Hibr font
remain the single source of truth.

## Verification

- TCC compilation is required.
- VT/UTF-8/Hibr terminal conformance must pass.
- SDL frame and graphics-cache gates must pass.
- A full-screen 240×135-cell workload must report bounded render time and no
  event-loop stall in the active diagnostic window.
- A physical Hibr screenshot must retain readable glyphs and fill the selected
  pane.

