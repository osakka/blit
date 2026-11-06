# ADR 0219: Pressed boxes and a grow box, in Chalk and Slate

- Status: accepted
- Date: 2026-11-06
- Scope: `src/blit_window_glyphs.h` (the state geometry),
  `src/blit_control_glyphs.h` (the chrome wrappers, one more pen group),
  `tools/fonts/draw-window-glyphs.py`, the icon catalogue and its README,
  `docs/font-reference.md`, `docs/fonts/special.md`, `window_glyph_gate`,
  `window_style_gate`, `glyph_edge_sweep_gate`, `icon_clip_gate`,
  `control_glyph_gate`
- Builds on: ADR 0210 (the window chrome), ADR 0211 (Slate from the cell's
  pens), ADR 0214 (the frame's line on the outer edge of its cells)
- Number: 0218 and release 0.3.47 are the hinting work's; if two records
  collide on merge, renumber this one.

## Context

The title bar's close, shade and zoom boxes had one drawing each, and the
window had no resize corner. A box that does not answer the pointer feels
dead, and a window with no grow box cannot show where to drag. The owner
approved mock-ups ("Perfection") for both sets. The mock-ups were
approximations from a simplified model; this record fixes the pixels from
the real geometry tables.

## Options considered

1. Separate hover and pressed glyphs. Twice the glyphs and no design asked
   for the difference: rejected.
2. Draw the state in the application with SGR attributes (reverse video).
   Reverse swaps the pens, but the mark must be cleared out of a solid box
   and Slate's bevel must invert: pens alone cannot do either.
3. New glyphs, drawn as cell geometry like the rest. Chosen.

## Decision

Ten glyphs at U+1002BB–1002C4, five a set, Chalk's then Slate's, in the
catalogue's `cell_geometry` section and drawn from the table in
`src/blit_window_glyphs.h` (`blit_state_roles`); the design script mirrors it.

| Order | Glyph | Chalk | Slate |
| --- | --- | --- | --- |
| 0 | close, pressed | U+1002BB | U+1002C0 |
| 1 | shade, pressed | U+1002BC | U+1002C1 |
| 2 | zoom, pressed | U+1002BD | U+1002C2 |
| 3 | grow | U+1002BE | U+1002C3 |
| 4 | grow, pressed | U+1002BF | U+1002C4 |

Ids are `chalk-close-pressed`, `slate-light-close-pressed` and so on (the
`slate-light` prefix and set keep the name the window glyphs have), and
`chalk-grow`, `chalk-grow-pressed`, `slate-light-grow`,
`slate-light-grow-pressed`.

- **Hover is pressed.** One set of states, no separate hover glyphs.
- **Chalk pressed.** The box (the band's square, 11x11 at 11x22) solid in the
  foreground and the mark cleared out (transparent: the cell's background,
  the bar, shows, which is white on a Chalk window). The mark area is the box
  less its border and `BLIT_WINDOW_PRESS_GAP` (1 px): 7x7 at 11 px. Close: an
  X on its diagonals. Zoom: a 1 px outline one pixel short of the area, so
  6x6 in its top-left. Shade: three bars on the box's rows 3, 5 and 7 (the
  middle row and two either side).
- **Slate pressed.** The mix pen inside the border, the unpressed bevel
  inverted: the foreground pen on the inner top and left edges, the line pen
  on the inner bottom and right (the foreground keeps the two corners where
  they meet), and the mark in the foreground pen in the area that is left
  after the bevel and the gap, 5x5 at 11 px: close an X, zoom a 4x4 outline in
  its top-left, shade two bars on box rows 4 and 6.
- **The grow cell** is the window's bottom-right frame cell (ADR 0214) with
  the grow box: a square the size of the title bar's box standing on the
  cell's bottom-right, 11x11 on rows 11–21 at 11x22. Its right and bottom
  edges are the frame's own outer lines (the cell's last column, the full
  height, and its last row), its top and left edges lines of its own, and
  inside is a `/` hatch: the pixel at (x, y) from the box's top-left is a
  hatch line where (x + y) mod `BLIT_WINDOW_GROW_PERIOD` (4) is 0.
  Chalk: foreground lines. Slate: each a mix-pen line with a line-pen (SGR
  58, white) line beside it on the next phase. **Pressed:** Chalk the inside
  solid with the hatch cleared out; Slate the mix pen with a foreground
  hatch. The cell is used in place of `frame-bottom-right`.
- **Pens.** Chalk is coverage in the foreground (`pens: foreground`); Slate is
  drawn in the cell's pens (`pens: cell`), with five more entries in the
  chrome pen index (`BLIT_STATE_PENS`). The boxes are `part: title-bar` on
  `bar`; the grow cells `part: frame` on `panel`.
- **Sizes.** The box is the title bar's box at every cell size, the marks
  and the hatch keep their pixel widths, and a mark that does not fit a small
  box is not drawn.
- **Source of truth.** One geometry table in the header (the new
  `BLIT_STATE_*`, `BLIT_WINDOW_PRESS_GAP` and `BLIT_WINDOW_GROW_PERIOD`
  defines), read by the design script; the catalogue is generated from it.
  U+1002C5 is the next free codepoint.

## Consequences

- Hibr can draw the pressed state by swapping the glyph, and the resize
  corner by using the grow cell for the frame's bottom-right.
- No existing glyph changes; the catalogue grows by ten entries, so its
  revision (and the hash `fonts.source.json` pins) changes.
- The frame's outer lines are the grow box's edges, so a window with a grow
  box has no gap or doubled line at its corner.

## Evidence

`window_glyph_gate` holds all ten, pixel for pixel, to the design script at
twelve cell sizes, to written-out drawings at 11x22 and to rules at every
size: a pressed Chalk box is the foreground solid with only its mark clear
(the bar around it the `close`, `shade` or `zoom` glyph's own), a pressed
Slate box uses only the three role pens, and the grow box's right and bottom
edges coincide with the frame's bottom-right line pixels, with the hatch on a
4 px period and nothing outside the box but the frame's line.
`window_style_gate` draws them through the compositor, `glyph_edge_sweep_gate`
checks every cell's edges on both compositors, `icon_clip_gate` that they
reach the cell's edges, `control_glyph_gate` that U+1002C5 is next free, and
`catalogue_fields_gate` the structured fields.
