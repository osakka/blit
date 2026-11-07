# ADR 0224: A full-screen box and a sticky box, in Chalk and Slate

- Status: accepted
- Date: 2026-11-07
- Scope: `src/blit_window_glyphs.h` (the two boxes' geometry and marks,
  beside ADR 0222's minimise box), `src/blit_control_glyphs.h` (five more
  pens in the chrome pen index), `tools/fonts/draw-window-glyphs.py`, the
  icon catalogue and its README, `docs/font-reference.md`,
  `docs/fonts/special.md`, `window_glyph_gate`, `window_style_gate`,
  `glyph_edge_sweep_gate`, `icon_clip_gate`, `control_glyph_gate`
- Builds on: ADR 0210 (the window chrome), ADR 0211 (Slate from the cell's
  pens), ADR 0219 (pressed boxes), ADR 0222 (the minimise box: these are
  drawn as it is)
- Release: 0.3.59

## Context

Hibr asked (#484) for two more title-bar boxes in both window styles (its
Flat is Chalk, its Embossed is Slate): full screen and sticky (the window on
every desktop), each matching the close, minimise, shade and zoom boxes and
their pressed forms exactly. Sticky is a toggle, so it also needs a third
form: on, the window is sticky and nobody is pressing the box. Each mark has
to read apart at a glance from close (an X), shade (bars across the middle),
zoom (a square in the top-left) and minimise (a bar along the bottom), and
from each other.

## Options considered

Two drawings each, built into the header as compile-time alternatives
(`BLIT_WINDOW_FULLSCREEN_MARK`, `BLIT_WINDOW_STICKY_MARK`, 1 or 2) and
rendered by the real server, Chalk and Slate (Pewter), normal, pressed and
on:

Full screen:

1. Four corner brackets, one in each corner of the mark area: the "expand
   to the edges" sign. Nothing in the middle and nothing along the middle of
   the sides, so it is not zoom's square outline, and it has no diagonal, so
   it is not close.
2. A diagonal two-headed arrow from bottom-left to top-right. Reads as
   "expand", but its diagonal is half of close's X; at 11 px the heads are
   2 px arms and the box is closer to close than the brackets are.

Sticky:

1. A pin: a round head about half the area across on a short stem, centred.
   The usual "pin to all desktops" sign; nothing else in the title bar is a
   vertical shape.
2. A small filled dot, centred (3x3 at 11 px). Clear of every other mark,
   but it says little, and its on form (a bigger dot) is only a weight away
   from it.

1 and 1 are chosen and are the header's default; the alternatives stay in
`blit_box_mark` behind the two defines (the catalogue's shape text and the
gate's 11x22 drawings are candidate 1's).

For sticky on: the unpressed box with the mark **bold**, the mark and every
pixel beside it (left, right, above, below) within the mark area. Other
forms were weighed: a solid inside with the mark cut out is the pressed
form; a thicker border or an inner ring takes the 1 px gap the mark needs
at 11 px; a mix-pen centre exists only in Slate. Bold works in both sets
the same way, keeps the face of the unpressed box (so on and pressed differ
by the face, and Chalk on and pressed are as far apart as a white and a
black box), and reads as "set" by weight: the pin's head becomes a 5 px
disc and its stem 3 px.

## Decision

Ten glyphs, in the catalogue's `cell_geometry` section, drawn by
`blit_state_roles` beside ADR 0222's minimise box; the design script mirrors
it.

| Glyph | Chalk | Slate |
| --- | --- | --- |
| full screen | U+10030A `chalk-fullscreen` | U+10030C `slate-light-fullscreen` |
| full screen, pressed (also hover) | U+10030B `chalk-fullscreen-pressed` | U+10030D `slate-light-fullscreen-pressed` |
| sticky | U+10030E `chalk-sticky` | U+100311 `slate-light-sticky` |
| sticky, pressed (also hover) | U+10030F `chalk-sticky-pressed` | U+100312 `slate-light-sticky-pressed` |
| sticky, on | U+100310 `chalk-sticky-on` | U+100313 `slate-light-sticky-on` |

- **The box** is the minimise box: the close box, the bar without stripes,
  the band's square (11x11 at 11x22) with a 1 px border; Slate's face is the
  line pen. The mark area is the box less its border and
  `BLIT_WINDOW_PRESS_GAP`: 7x7 at 11 px.
- **Full screen** (`blit_box_mark`, drawing 1): in an m x m mark area, four
  L brackets in its corners, each two arms of a = (m + 1) / 3 px (at least
  1) along the area's edges, the corner pixel included. At 11 px, arms of
  2 px with 3 px clear between brackets.
- **Sticky** (drawing 1): a pin. A head d = ((m - 1) / 2) | 1 px across
  (odd, so it centres), its corner pixels clear from 5 px up (round); under
  it a 1 px stem (d + 1) / 2 px long; head and stem centred in the area
  together. At 11 px a 3x3 head on box rows 3–5, columns 4–6, the stem on
  column 5, rows 6–7.
- **Sticky on:** the unpressed sticky box, the pin bold as above.
- **Pressed** follows ADR 0219 exactly. Chalk: the inside solid and the mark
  cut out. Slate: the mix pen inside, the bevel inverted, the mark in the
  foreground in the 5x5 area left.
- **Pens.** Chalk is coverage (`pens: foreground`); Slate is drawn in the
  cell's pens (`pens: cell`), five more entries in the chrome pen index
  (`BLIT_STATE_PENS` is now the state, minimise, full-screen and sticky
  sets). All are `part: title-bar` on `bar`.
- **Sizes.** The box is the title bar's box at every size; the marks scale
  with the mark area by the rules above. Where the area is 1 or 2 px (the
  smallest boxes, Slate pressed) the marks are a dot or a stub, as the
  other pressed marks there are.
- U+100314 is the next free codepoint.

## Consequences

- Hibr draws full screen and sticky as it draws the other boxes, swaps to
  the pressed glyph while pressed or hovered, and to sticky on while the
  window is sticky.
- No existing glyph changes; the catalogue grows by ten entries, so its
  hash in `fonts.source.json` changes.
- Choosing the other drawing later is the define and the gate's literal
  drawings and the catalogue's shape text, not new code. The switch is the
  value written in the header, not a build flag: the design script and
  `window_glyph_gate` read that line, so a `-D` alone would set the raster
  apart from them.

## Evidence

`window_glyph_gate` holds the ten, pixel for pixel, to the design script at
twelve cell sizes, to written-out drawings at 11x22, and to rules at every
size: outside the box the bar's own pixels, the border in the foreground,
the mark where `blit_box_mark` puts it (bold for on) and nothing else inside
but the close box's face or ADR 0219's pressed inside; where the mark area
is 3 px or more, each glyph is none of close, shade, zoom and minimise (in
the same state), not the other new box, and each state is apart from the
others. `window_style_gate` draws them through the compositor,
`glyph_edge_sweep_gate` checks their cell edges on both compositors,
`icon_clip_gate` that they reach the cell's edges, `control_glyph_gate` that
U+100314 is next free, and `catalogue_fields_gate` the structured fields.
