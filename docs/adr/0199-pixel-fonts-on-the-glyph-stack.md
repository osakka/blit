# ADR 0199: Pixel fonts keep their own glyphs and borrow the glyph stack for the rest

- Status: accepted
- Date: 2026-10-27
- Scope: `blit_raster.c` (`blit_raster_open_pixel`), `blit_server.c` (font
  preparation), a new gate
- Completes: ADR 0195's "Not yet"

## Context

Since 0.2.36, every outline font draws Blit's icons, Nerd icons, symbols
and emoji from one glyph stack. The two pixel fonts, Blit Terminus 16 and
Blit Spleen 16, did not. Choosing either turned the menu bar's battery,
volume, Wi-Fi, hui and power icons, and every Nerd icon, into `?`.

## Options considered

1. **Draw pixel fonts through the raster entirely.** Every glyph would be
   rasterised and cached, changing the pixels the fonts were chosen for,
   and the fast native path would be lost.
2. **A raster that declines what the font has.** The renderer already
   falls back to a font's own drawing when the raster declines a glyph. A
   pixel font's raster declines every glyph its PSF has, and draws only
   the rest from the stack.

## Decision

Option 2: `blit_raster_open_pixel`.
- **What the font has, it draws.** Letters and its own box drawing go
  through the native PSF path, pixel for pixel as before. The 16 px case
  keeps its unscaled fast path.
- **What it lacks comes from the stack,** in the same order as for
  outline fonts:
  - Blit's colour icons (plane 16, first);
  - cell geometry;
  - Nerd Symbols;
  - Noto Symbols 2, Symbols and Math;
  - emoji, as single glyphs and clusters;
  - Unifont, for one-column characters only.
- **Wide characters stay with Blit's wide pixel font**
  (`Blit-Unifont16.psf`), so CJK stays in pixels.
- **What nothing has** is declined too, and the font's own `?` shows.
- **Fitting:** a PSF has no metrics, so they come from its H. The baseline
  is the row under the H, the cap height is the H's ink, and the em is the
  size a text face with that cap height would have, each scaled to the
  cell. Stack glyphs then fit as they do beside an outline font.
- **Degrading:** if the stack can't open (no emoji faces under the font
  root), a pixel font still draws everything it has, as before. Only an
  outline font fails to load without its faces.
- **`blit_raster_open` with no outline is unchanged.** It is still a
  PSF-only raster, which the gates use. Pixel mode is explicit.

## Consequences

- Every shipped font now shows every icon Hibr uses. Of Hibr's 101 UI
  glyphs, each pixel font keeps 74 as its own pixels and takes 27 from the
  stack.
- `GET /v1/fonts` still reports pixel fonts as `bitmap-nearest`, now with
  `colour_glyphs: true`.
- Each glyph now asks the raster first. The raster declines a font glyph
  with one hash lookup, before its cache.

## Evidence (0.2.43)

`pixel_font_stack_gate` (new) runs for both pixel fonts at every size from
8 to 64 px. It checks:
- 'A' and '┌' are declined (the font's own);
- 漢 at two columns is declined (the wide pixel font);
- U+10FFFD is declined (the font's `?`);
- Nerd icons, Powerline, ⏻, ⟶, 😀 and Blit's originals are drawn, not
  fallback, with ink, in colour where they are colour. Spleen keeps its own
  Powerline arrow;
- each of Hibr's 101 UI glyphs is either the font's own or drawn from the
  stack.

The font suite passes, 37 of 37, before the new gate. Terminus at 22 and
32 px and Spleen at 16 px were also rendered through Blit and inspected.
