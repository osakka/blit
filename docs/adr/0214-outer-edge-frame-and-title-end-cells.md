# ADR 0214: One frame, its line on the outer edge; the title bar's end cells

- Status: accepted
- Date: 2026-11-05
- Scope: `src/blit_window_glyphs.h` (the frame and title-end geometry, one
  bar helper), `src/blit_control_glyphs.h` (the chrome wrappers),
  `tools/fonts/draw-window-glyphs.py`, the icon catalogue, OpenAPI
  (`CellGeometryGlyph`), `assets/fonts/icons/README.md`,
  `window_glyph_gate`, `window_style_gate`, `catalogue_fields_gate`,
  `control_glyph_gate`, `font_reference_gate`, `api_schema_gate`,
  `custom_icon_gate`, `documentation_gate`
- Builds on: ADR 0210 (the window chrome), ADR 0211 (Slate from the cell's
  pens, the shadow attribute), ADR 0212 (the catalogue a client can map)
- Number: 0213 is the graphics-ledger record on the main line; if two
  records collide on merge, renumber this one.

## Context

The window frame glyphs of ADR 0210 draw a one-pixel line against the
content and leave the rest of the cell transparent, so the window's edge is
the line and a cell of wallpaper-coloured padding beside it. The owner
decided differently: **a window has one frame, and its line sits on the
outermost edge of each frame cell.** The left frame cell's line is its left
edge, the right cell's its right edge, the bottom row's its bottom edge.
Everything else in the cell is transparent, so the cell's own background is
the padding: Hibr sets the frame cells' background to the window's face and
the face runs right out to the line.

Two further observations settled the shape of the change:

- Chalk's and Slate's frame glyphs were identical (a 1 px foreground line,
  in coverage), so two sets of them were one set drawn twice.
- The title bar's two end cells are the frame cells of the bar row. Their
  look belongs to the title bar's styles, Chalk's and Slate's: the stripes
  have to run into them, which a frame cell that is only a line cannot do.

## Decision

### Nine new glyphs, U+1002AE–1002B6

The next free codepoint is **U+1002B7**.

| Codepoint | Id | Where it goes | Pens |
| --- | --- | --- | --- |
| U+1002AE | `frame-left` | left of every body row | fg = the line |
| U+1002AF | `frame-right` | right of every body row | fg = the line |
| U+1002B0 | `frame-bottom-left` | bottom-left corner: left and bottom lines | fg = the line |
| U+1002B1 | `frame-bottom` | the bottom row | fg = the line |
| U+1002B2 | `frame-bottom-right` | bottom-right corner: right and bottom lines | fg = the line |
| U+1002B3 | `chalk-title-end-left` | the title row's left end cell | fg = the ink |
| U+1002B4 | `chalk-title-end-right` | the title row's right end cell | fg = the ink |
| U+1002B5 | `slate-light-title-end-left` | the title row's left end cell | bg = the bar, fg = the ink, SGR 58 = the highlight |
| U+1002B6 | `slate-light-title-end-right` | the title row's right end cell | as above |

- **The frame cells** (`set: frame`, `pens: foreground`) are coverage in
  the cell's foreground and nothing else: the line on the outermost edge
  (one pixel at every size), no other ink. An application sets their
  background to the window's face. There is one set: it serves Chalk and
  Slate windows alike. The right-hand cell of the title row is **not**
  `frame-right`; it is a title-end glyph.
- **The title-end cells** are the bar row's end cells in each title-bar
  style. Each is the bar glyph (top line on row 0, rule on the last row,
  Slate's bevel: a highlight under the line and a shade over the rule, in
  the same roles as the existing Slate bar glyphs) **plus the outer line**
  down its outer edge, and **the stripes run into the cell**. They start
  (left cell) or stop (right cell) `BLIT_WINDOW_STRIPE_INSET` (2) px from
  the cell's outer edge: the line and one clear pixel. Chalk's are
  coverage; Slate's use the roles and pens of `slate-light-stripe` (BG the
  bar, FG the lines, LINE the highlight and the stripes' light row, MIX the
  shade and dark stripes). The stripe rows are those of the neighbouring
  `stripe` cell, so the stripes join seamlessly at every cell size.

  The brief said the stripes stop "2 px clear of the outer line"; the
  approved mock-up (`mock_frame.py`'s `outer` mode) starts them at x = 2,
  i.e. 2 px from the edge, one clear pixel beside the 1 px line. That is
  what is built (`BLIT_WINDOW_STRIPE_INSET`, the constant the other stripe
  cells already use for the gap beside a box).
- `chalk-title-end-*` has the ids and set of Chalk; `slate-light-title-end-*`
  keeps the `slate-light-` prefix that the existing Slate ids have.
- The geometry is one table: `blit_frame_roles` in
  `src/blit_window_glyphs.h` shares `wg_fill`, `blit_window_band` and the new
  `wg_bar` (the bar's rows, which `blit_window_roles` now also uses) with the
  rest of the window code. The raster's pen planes and both compositors take
  the two Slate title ends through the existing `blit_chrome_*` wrappers
  (their pen indices follow the controls'; `BLIT_CHROME_PENS` is 50). No
  compositor code changes.
- The catalogue entries are generated by `draw-window-glyphs.py`, which
  owns U+1002AE–1002B6 as well as the window sets. In the structured fields
  the frame cells are `part: frame`, `background: panel` (the window's
  face; `panel` already meant what a control or note sits on),
  `foreground: ink`, `line: none`; the title ends are `part: title-bar`,
  `background: bar`, `foreground: ink`, `line: none` (Chalk) or `highlight`
  (Slate). The set enum gains `frame`.

### The shadow is unchanged

SGR 66 draws a drop shadow under any glyph. With the line on the outer edge
of the frame cell, the strips go in the cells **beyond** the frame cells,
with inset 0 (`66:kind:width:0`): the right strip begins at the pixel next
to the frame's line, the under strip on the pixel row under the bottom line,
the under-start cell 2 px in, the corner as the flush corner. The default
inset (1) is for the old frame glyphs, whose line sat 1 px from the cell's
edge, and is unchanged.

### Deprecation, not retirement

Hibr 0.99.443 draws windows with the current frame glyphs and
`stripe-first` / `stripe-last`, so Blit keeps drawing them unchanged. The
catalogue marks them, as data:

- `deprecated: true` and `replaced_by` (a list of ids), typed in OpenAPI
  (`CellGeometryGlyph`), documented in the icon README and held by
  `catalogue_fields_gate`;
- the 18 entries are `chalk-` and `slate-light-` `frame-left`,
  `frame-right-top`, `frame-right`, `frame-bottom-left`,
  `frame-bottom-first`, `frame-bottom`, `frame-bottom-right`,
  `stripe-first` and `stripe-last`;
- `frame-left` is replaced by `frame-left` (a body row) and the style's
  `title-end-left` (the title row), `frame-right-top` by `title-end-right`,
  `stripe-first` / `-last` by the title ends, the bottom pieces by their
  new namesakes;
- the dialog, note and frame-without-a-title-bar glyphs (`frame-top-*`)
  are untouched, and not deprecated.

A later release retires the deprecated entries (their codepoints are then
never reused).

## Evidence

- **`window_glyph_gate`** (fails on 0.3.40, which has no U+1002AE): every
  new glyph equals the design at the 12 cell sizes; every frame cell has
  its line on the outermost edge and no ink elsewhere; Chalk's title end's
  ink equals Slate's foreground plus its mix in the stripe rows; the stripes
  start 2 px from the outer edge; and the column that meets the next cell
  equals that cell's stripe column, row for row, at every size, in both
  styles. At 11x22 the pixels are written out.
- **`window_style_gate`**, native and GLES: a real server draws the nine
  glyphs, then a Slate window with a 1 px shadow and a Chalk window with a
  2 px shadow, each cell compared with the design or the plain face, the
  stripes continuous from the end cells, and the shadow beginning at the pixel
  next to the frame (inset 0).
- **`catalogue_fields_gate`**: `deprecated` and `replaced_by` are typed,
  documented and exactly on the 18 old entries; the replacements exist and
  are not themselves deprecated.
- **`font_reference_gate`, `api_schema_gate`, `custom_icon_gate`,
  `documentation_gate`**: the reference lists the deprecations, the schema
  types them, the new codepoints keep clear of the icons and every retired
  range, and this record and its index entry exist.

## Considered and not done

- **Two frame sets (Chalk's and Slate's).** They were identical; one set.
- **Frame cells that carry their own padding.** The cell's background
  already does, in the window's face, in one place (Hibr's setting).
- **Retiring the old glyphs now.** Hibr has not moved; Blit would break
  its windows.
- **Moving the shadow into the glyphs.** It stays SGR 66 (ADR 0211).
