# ADR-0042: Isolate the SDL GLES2 glyph path

## Status

Superseded by the 0.1.396 live geometry acceptance experiment.

## Context

The 0.1.365 live capture showed a split failure: the desktop wallpaper and
menu rendered, but the terminal surface was reduced to coloured dot patterns.
The live renderer reported `opengles2`. API-injected pointer motion changed
only the cursor-sized pixel region and did not create the terminal corruption,
so pointer damage was not the primary cause.

The existing SDL terminal renderer batches glyph quads with
`SDL_RenderGeometry`. Its atlas coordinates are valid for the software test
renderer, but the live GLES2 result is visually corrupt. We need a reversible
experiment that does not alter Hibr and that reports which path produced a
frame.

## Decision

Use batched `SDL_RenderGeometry` atlas draws by default for every SDL
renderer. Keep the immediate `SDL_RenderTexture` path available with
`BLIT_SDL_GLYPH_BATCH=off` as a bounded rollback switch. The diagnostics API
reports `render.glyph_path` as `immediate-texture`, `geometry-batch`, or `kms`.

This is a reversible performance experiment. The live acceptance gate must
prove equivalent glyph output before the default is retained.

## Verification

- Local TCC build succeeds.
- Existing SDL glyph, pointer atomicity, and wallpaper pointer-stress gates
  pass.
- The live acceptance test is: install the candidate, verify
  `render.glyph_path=geometry-batch`, capture the unchanged Hibr desktop, and
  compare glyph legibility and frame timing with the immediate path. If the
  live visual gate fails, set `BLIT_SDL_GLYPH_BATCH=off` and retain the
  evidence; do not change Hibr.
