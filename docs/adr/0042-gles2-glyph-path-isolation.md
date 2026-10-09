# ADR-0042: Isolate the SDL GLES2 glyph path

## Status

Accepted for live diagnosis; the visual gate remains open until the real
display is captured with the corrected path.

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

Use immediate `SDL_RenderTexture` atlas draws by default for SDL's `opengles2`
renderer. Keep the geometry path available with
`BLIT_SDL_GLYPH_BATCH=on`; `BLIT_SDL_GLYPH_BATCH=off` forces the immediate path
on every SDL renderer. The diagnostics API reports `render.glyph_path` as
`immediate-texture`, `geometry-batch`, or `kms`.

This is an isolation and correctness decision, not a claim that the geometry
path is permanently rejected. It can be re-enabled only after a real GLES2
pixel-oracle test proves equivalent glyph output.

## Verification

- Local TCC build succeeds.
- Existing SDL glyph, pointer atomicity, and wallpaper pointer-stress gates
  pass.
- The live acceptance test is: install the package, verify
  `/v1/version=0.1.366`, verify `render.glyph_path=immediate-texture`, capture
  the display, and visually confirm the terminal glyphs are legible. If that
  fails, retain the capture and revert the path experiment rather than
  changing Hibr.
