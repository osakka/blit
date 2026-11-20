# ADR 0240: Bold keeps the shape of a glyph that fills its cell

- Status: accepted
- Date: 2026-11-20
- Scope: `src/blit_raster.c` (`blit_raster_get_styled`,
  `blit_raster_get_cluster_styled`), the raster hooks in `src/blit_font.h`,
  `draw_glyph` and `draw_cell_glyphs` in `src/blit_kms.c`;
  `tools/tests/bold_shape_gate.sh`; Gitea #183

## Context

The owner, with Hack as the text font: the window buttons "are all cut from
the right hand side". Hibr draws the traffic-light buttons as bold U+25CF.
Measured on the owner's screen (0.3.128, Hack 22 px, 11 x 22 cells), each
disc was round on the left and a straight vertical edge on the right.

Bold in Blit is an overstrike: the cell's glyph, then the same glyph one
pixel to the right with its last column dropped so nothing enters the next
cell. Hack draws U+25CF across its whole advance (design box 6-1227 of 1233
units), so at 11 px its ink fills columns 0-10. The overstrike lays the
disc's near-solid column 9 over its soft column 10 and the right edge turns
solid: a D. Fira Code's disc leaves a margin, so its overstrike lands on
empty pixels and the disc stays round. The regular glyph was never clipped:
`draw_outline_face` refuses to clip, and the text font's em is chosen so its
advance fits the cell.

## Decision

- **Bold stays the one-pixel overstrike** for every glyph, so a line keeps
  one bold weight and every glyph with room is drawn exactly as before.
- **A shape whose ink reaches its last column** (coverage of a quarter or
  more anywhere in that column) gets a bold form: the same glyph narrowed
  by area into one column less. The renderer's overstrike then brings it
  back to its own width, round where it was round. Shapes are the arrow to
  symbol blocks, U+2190-2BFF (geometric shapes, dingbats, arrows, technical
  and mathematical symbols), and the bullets U+2022, U+2023 and U+2043.
- **Text keeps the plain overstrike**, letters, digits and punctuation, even
  where they reach their last column (Hack's A, K, T, W, X, w, #, % and @ at
  most sizes): narrowed by a column their stems fall between pixels, since
  FreeType does not hint horizontally, and bold text looked soft. Their
  overstrike does not change their shape.
- **By area, not by outline:** drawing a shape's outline again at the
  narrower width leaned under light hinting (Fira Code's U+25CB, U+25A1);
  a shape has no stems to keep on whole pixels, so area narrowing loses
  nothing.
- **Kept as they are:** cell geometry that is meant to meet its neighbours
  (box drawing, block elements, braille, Powerline), window chrome and bar
  corners, Blit's icons (they span cells), colour glyphs (the overstrike
  skips them), Arabic (its ink may leave the cell), and pixel fonts' own
  glyphs (their overstrike is their bold).
- The raster hooks take the cell's bold (`raster_get`,
  `raster_cluster_get`); `draw_cell_glyphs` asks for the bold form for both
  of its draws. The bold form is cached under its own key (bit 23 of the
  first scalar, above the icon pen's bits 21-22), in the same bounded cache.

## Not chosen

- **Emboldening outlines** (FreeType's `FT_Outline_EmboldenXY`): it changes
  every bold glyph in every outline font, moves hinted stems off whole
  pixels, and would leave box drawing, Arabic and pixel fonts on the
  overstrike -- two bold weights on one line. A heavier bold at large sizes
  is a separate change with its own preview.
- **Blit-drawn traffic-light buttons** for Hibr's default window style: a
  possible polish, not the fix; every bold glyph that fills its cell had the
  same defect.
- **Hibr not bolding its buttons**: the defect is Blit's, for any program.

## Evidence

`tools/tests/bold_shape_gate.sh` draws through `draw_cell_glyphs` for every
shipped text font, cell heights 8-64 and full, light and no hinting, over
ASCII, Latin-1, punctuation and bullets and the shape blocks (arrows,
technical, geometric shapes, miscellaneous symbols, dingbats, symbols and
arrows): 1,191,015 bold cells, 0 failures. Symmetric shapes that reach
their last column (U+25CF, U+25CB, U+25A0 and others, 2,310 cases) keep
matching edges in bold, beyond what the font's own lean gives; no ink
leaves the cell; every other glyph's bold cell is byte for byte the old
overstrike. With the raster's regular glyph handed to bold cells, as
before this ADR, 2,211 shapes fail (Hack U+25CF among them). The sweep
first covered every block up to U+2BFF (2,627,415 cells), which took over
the runner's five minutes on a loaded host; the blocks it dropped (Latin
Extended, mathematical operators, control pictures) hold no shapes.
