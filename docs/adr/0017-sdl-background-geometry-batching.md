# ADR 0017: Batch SDL terminal background geometry

## Status

Retired by [ADR 0184](0184-sdl-is-removed.md): the SDL code it describes was removed.

Accepted.

## Context

Hibr themes can encode wallpaper as a truecolor background on many terminal
cells. The SDL renderer previously submitted one `SDL_RenderFillRect` call per
colored cell. On a full 240×135 display this made wallpaper redraws consume the
frame budget even when glyphs were already atlas-backed.

## Decision

The SDL adapter performs a background pre-pass for each damaged surface,
queues opaque colored cell rectangles as `SDL_RenderGeometry` quads, submits
the batch once, and then draws the glyph layer. Inverse cells use the same
effective foreground/background calculation as the canonical cell renderer.
Allocation failure falls back to the existing per-rectangle operation; it
never changes cell colors or suppresses a background.

The batching is an SDL presentation optimization only. Canonical terminal
state, damage, direct-KMS rendering, font mapping, and the OpenAPI contract
remain unchanged. Runtime diagnostics expose `background_batches` and
`background_quads` so a physical wallpaper workload can prove the path is
active.

## Verification

- TCC compilation and full terminal conformance must pass.
- `sdl_background_batch_gate.rb` must verify a red truecolor cell in an SDL
  screenshot and a nonzero batch/quads counter.
- Physical Hibr diagnostics must report the counters during a wallpaper
  redraw, with the display's refresh and missed-deadline fields retained.
