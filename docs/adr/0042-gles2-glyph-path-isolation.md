# ADR-0042: Isolate the SDL GLES2 glyph path

- Status: retired in 0.2.22 (ADR 0184)
- Note: accepted for 0.1.396 after the live geometry acceptance experiment.

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

This is a reversible performance decision. The live acceptance gate proved
equivalent glyph output before the default was retained.

## Verification

- Local TCC build succeeds.
- Existing SDL glyph, pointer atomicity, and wallpaper pointer-stress gates
  pass.
- The 0.1.396 live acceptance test verified `render.glyph_path=geometry-batch`
  on OpenGL ES2, captured the unchanged Hibr desktop with legible glyphs, and
  recorded zero missed deadlines. `BLIT_SDL_GLYPH_BATCH=off` remains the
  bounded rollback switch; Hibr was not changed.
