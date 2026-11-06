# ADR 0222: A minimise box, in Chalk and Slate

- Status: accepted
- Date: 2026-11-06
- Scope: `src/blit_window_glyphs.h` (the minimise geometry, beside ADR
  0219's states), `src/blit_control_glyphs.h` (two more pens in the chrome
  pen index), `tools/fonts/draw-window-glyphs.py`, the icon catalogue and
  its README, `docs/font-reference.md`, `docs/fonts/special.md`,
  `window_glyph_gate`, `window_style_gate`, `glyph_edge_sweep_gate`,
  `icon_clip_gate`, `control_glyph_gate`
- Builds on: ADR 0210 (the window chrome), ADR 0211 (Slate from the cell's
  pens), ADR 0219 (pressed boxes: this box is pressed by its rules)
- Release: 0.3.54

## Context

Hibr asked for a minimise box in the title bar beside close, shade and zoom,
in both window styles (Hibr's Flat is Chalk, its Embossed is Slate), with a
pressed form like the other boxes. Its mark has to be told apart at a glance
from shade (a bar across the middle) and zoom (a square in the top-left).

## Options considered

1. A short bar along the bottom of the box: the classic minimise `_`, as
   the window drops to the task bar. Low in the box, where neither shade nor
   zoom draws.
2. A small filled square in the box's bottom-left, a third of it: the window
   shrunk to a corner. Also clear of shade and zoom, but a solid block reads
   heavier than the other marks, and at 11 px it is 3x3, close to zoom's
   inner square in kind.
3. Reuse the shade glyph. No: Hibr shades and minimises as two actions.

Both 1 and 2 were drawn and rendered by the real server, Chalk and Slate
(Pewter), normal and pressed; 1 is chosen. Option 2's geometry is kept out of
the tree.

## Decision

Four glyphs at U+1002CD–1002D0, two a set, Chalk's then Slate's, in the
catalogue's `cell_geometry` section, drawn by `blit_state_roles` with ADR
0219's states; the design script mirrors it.

| Glyph | Chalk | Slate |
| --- | --- | --- |
| minimise | U+1002CD `chalk-minimise` | U+1002CF `slate-light-minimise` |
| minimise, pressed (also hover) | U+1002CE `chalk-minimise-pressed` | U+1002D0 `slate-light-minimise-pressed` |

- **The box** is the close box: the bar without stripes, the band's square
  (11x11 at 11x22) with a 1 px border; Slate's face is the line pen.
- **The mark** is a 1 px bar in the foreground on the bottom row of the mark
  area, centred, the area's width less a quarter of it (rounded down) each
  side. The mark area is the box less its border and `BLIT_WINDOW_PRESS_GAP`
  (1 px): 7x7 at 11 px, so the bar is 5 px on the box's row 8, columns 3–7,
  with a clear row between it and the border.
- **Pressed** follows ADR 0219 exactly. Chalk: the inside solid in the
  foreground and the mark cleared out (the same 5 px). Slate: the mix pen
  inside, the bevel inverted (the foreground pen on the inner top and left,
  the line pen on the inner bottom and right), the mark area 5x5 after the
  bevel and the gap, so the bar is 3 px in the foreground on box row 7.
- **Pens.** Chalk is coverage (`pens: foreground`); Slate is drawn in the
  cell's pens (`pens: cell`), two more entries in the chrome pen index
  (`BLIT_STATE_PENS` is now `BLIT_STATE_SET + BLIT_MINIMISE_SET`). Both are
  `part: title-bar` on `bar`, set as the other boxes.
- **Sizes.** The box is the title bar's box at every size and the bar keeps
  its 1 px height. In the smallest boxes (Slate pressed under 9 px) the mark
  area is 1 or 2 px and the bar is a dot or a stub, as the other pressed
  marks there are.
- U+1002D1 is the next free codepoint.

## Consequences

- Hibr draws minimise as it draws the other boxes and swaps the glyph to
  show it pressed or hovered.
- No existing glyph changes; the catalogue grows by four entries, so its
  revision (and the hash `fonts.source.json` pins) changes.

## Evidence

`window_glyph_gate` holds the four, pixel for pixel, to the design script at
twelve cell sizes, to written-out drawings at 11x22, and to rules at every
size: outside the box the bar's own pixels, the border in the foreground, the
mark where the rule puts it and nothing else inside but the close box's face
or ADR 0219's pressed inside; where the mark area is 3 px or more, the mark is
below the box's middle and the glyph is none of close, shade and zoom (or
their pressed forms). `window_style_gate` draws them through the compositor,
`glyph_edge_sweep_gate` checks their cell edges on both compositors,
`icon_clip_gate` that they reach the cell's edges, `control_glyph_gate` that
U+1002D1 is next free, and `catalogue_fields_gate` the structured fields.
