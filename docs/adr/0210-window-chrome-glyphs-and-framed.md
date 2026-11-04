# ADR 0210: Window chrome glyph sets (Chalk and Slate) are cell geometry, and SGR 51 frames button labels and titles

- Status: accepted
- Date: 2026-11-03
- Scope: `src/blit_window_glyphs.h` (new), `blit_raster.c`, both
  compositors (`blit_kms.c`, `blit_kms_gles.c`), the SGR parser, DECRQSS,
  the cells API and OpenAPI `CellStyle`, the icon catalogue,
  `tools/fonts/draw-window-glyphs.py` (new), `window_glyph_gate` and
  `window_style_gate` (new), `terminal_conformance`
- Builds on: ADR 0191 (cell geometry), ADR 0195 (the glyph stack), ADR 0197
  (one codepoint per column), ADR 0201 (the catalogue grows by families;
  retired codepoints are never reused)

## Context

The owner wants Hibr's windows, notifications and dialogs drawn with
retro Mac-style chrome, from glyph sets of our own drawing:
- **Chalk**, black and white: a pinstriped title bar, a close box, a
  window-shade box and a zoom box, a 1 px frame with a 2 px drop shadow
  (the shadow has since become Hibr's own setting: see the revision of
  2026-11-04).
- **Slate**, in colour: the same geometry with a grey bar, embossed
  stripes, a bevel and white boxes, in a light and a dark set.

Notifications get the frame and a close box. Confirm dialogs get a double
border and rounded buttons, a default button with a thick ring. The owner
approved mock-ups drawn cell by cell at the real 11x22 cell, and asked for
the real glyphs to be drawn more carefully than the mock-ups, the buttons
most of all.

**These are not one bundled setting.** Hibr's appearance settings choose
each part of the chrome separately: Frame, Title Bar, Buttons and Button
placement. Chalk and Slate are values for each of them, so they mix. A
Slate title bar can sit on Hibr's box-drawing frame, and a Chalk title bar
can sit on a Slate frame. Blit provides a glyph set for each part. It
knows nothing of the settings, and nothing here depends on two parts
coming from the same set.

Hibr draws all of it out of terminal cells. Every piece of chrome is a
character in a cell, or an attribute of one. This puts four constraints on
the glyphs:
- **They must line up across cells.** The stripes have to run on from cell
  to cell, the frame lines have to sit on the exact cell edge, and the
  shadow offsets have to be exact.
- **They must be crisp.** That chrome is 1-bit pixel art. A half-lit
  pixel on a stripe reads as a blur.
- **Frame cells are mostly empty.** Where nothing is drawn, the cell's
  background must show: on a held desktop, that is the wallpaper.
- **A button's label and a window's title are text.** A glyph cannot draw
  both the letters and the chrome around them in one cell.

## Options considered

**How to draw the glyphs.**
1. **Outline icons in BlitIcons**, as the file icons are. Every Blit icon
   is fitted to a design box: 80% or 90% of the cell, centred on the cap
   height. A line can never land on the cell's edge that way, and the
   rasteriser antialiases.
2. **Cell geometry in the shared raster**, as the box drawing, blocks,
   Braille and the bar corners are (ADR 0191). The raster draws each
   glyph from the cell's size, so every edge falls on a whole pixel. Both
   compositors and every font get the same pixels.

**How to draw chrome round text** (a button's label, a window's title).
1. **One glyph per text cell.** The chrome is then drawn over a blank,
   and the letters have nowhere to go.
2. **An attribute.** ECMA-48 has one: SGR 51, *framed*, ended by SGR 54. A
   framed label between two cap glyphs is the whole button.

**How a Slate title keeps the bar's bevel.** Every Slate bar cell has
four fixed rows: frame line, highlight, then shade and rule at the bottom.
The title's text cells need those four rows too, or the title looks cut
out of the bar.
1. **The title glyph under the text.** A cell holds one character, so a
   glyph cannot sit under the title's letters.
2. **Colours from SGR.** Plain SGR 51 has one colour (the underline
   colour). Three are needed (K, L and D), and SGR has no slot for the
   other two.
3. **A framing kind for the bar.** Blit sub-parameters of SGR 51 draw the
   bar's own rows in the set's palette: `51:2` for Slate light, `51:4`
   for Slate dark. The colours come from the same palette table the
   glyphs use, through one band function both compositors call.

## Decision

Cell geometry, and SGR 51 with Blit's sub-parameters (option 3 for the
title).

### The glyphs

- **105 codepoints, three sets of 35 in one order.** Chalk is at
  U+100231–100253, Slate light at U+100254–100276 and Slate dark at
  U+100277–100299. U+10029A is the next free codepoint. The order was
  agreed with Hibr: stripe, stripe-first, stripe-last,
  stripe-before-title, stripe-after-title, title, stripe-to-box,
  stripe-from-box, stripe-between, close, shade, zoom, the window frame
  (left, right-top, right, bottom-left, bottom-first, bottom, bottom-right,
  top-left, top, top-right), note-close, the dialog border (top-left, top,
  top-right, left, right, bottom-left, bottom, bottom-right), and the
  button caps (left, right, default-left, default-right).
- **Parts mix across sets.** Each part (the title bar's cells, the frame,
  the dialog border, the button caps) joins only to cells of its own
  part, at the cell edge. Every set uses the same geometry, so a part from
  one set meets a part from another at the same rows and columns. For
  example, every set's title bar has its frame line on row 0, and every
  set's left frame has its line on the cell's last column.
- **The catalogue lists them** in its `cell_geometry` section with the bar
  corners, as `chalk-<name>`, `slate-light-<name>` and `slate-dark-<name>`,
  each with `colour`, `set` and `shape`. `GET /v1/fonts/icons` serves it
  unchanged. The catalogue grows to about 55 KiB, inside its 256 KiB
  bound.
- **Chalk is one colour.** It is a coverage glyph, drawn in the cell's
  foreground, so Hibr's theme colours apply. **Slate is the same geometry
  in its own colours**, as premultiplied RGBA:

  | Role | Light | Dark |
  | --- | --- | --- |
  | frame and outlines (K) | 0 | 8,8,10 |
  | dark grey (D) | 119 | 30,30,34 |
  | highlight (L) | 255 | 112,112,122 |
  | title bar (B) | 214 | 60,60,67 |
  | box face (W) | 255 | 200,200,206 |
  | dialog face (G) | 238 | 52,52,58 |
  | button face (F) | 221 | 78,78,86 |
  | button shade (N) | 153 | 40,40,45 |
  | button ring, dialog outer line (O) | 0 | 168,168,176 |
  | dialog wide line (R) | 119 | 122,122,130 |
  | its middle highlight (H) | 255 | 190,190,198 |

  In light, O, R and H are K, D and L. In dark, the owner found the
  controls' lines too dark to read on the dark faces, so dark has its own:
  - the button ring and the default ring (O) stand about 90 grey levels
    above the 78,78,86 button face, and about 115 above the 52,52,58
    dialog face;
  - the dialog's wide line (R) stands about 70 above the dialog face, with
    its highlight (H) about 140 above.
  The window frame, the boxes and the title bar keep K, D and L. In the
  dark bar, the stripes (30 and 112 on 60) and the boxes (200 with an
  8,8,10 outline) still read.

  In both sets, a pixel that no rule sets is transparent: alpha 0 for
  Slate, coverage 0 for Chalk.
- **One geometry table.** `src/blit_window_glyphs.h` holds every length,
  as `BLIT_WINDOW_*` constants, and the palette. The raster draws from it;
  the design script reads the numbers and the palette from the header and
  draws them again. Both compositors take SGR 51's widths and the title
  bar's rows from the same header. At 11x22 the table gives:
  - **Stripes:** rows 5, 7, 9, 11, 13 and 15. Slate's white row sits
    under each dark one.
  - **The bar:** a frame line at row 0 and a rule at row 21. In Slate, a
    highlight at row 1 and a shade at row 20, across every bar cell and,
    through SGR 51:2 and 51:4, under the title.
  - **Stripe ends:** the stripes stop 2 px short beside a frame or a box,
    stay 2 px clear on both sides between two boxes, and stop 5 px short
    before and after the title.
  - **Boxes:** square, the cell's full width (11x11) at rows 5–15, with a
    1 px outline. The zoom box's inner square is (0,5)–(6,11). The shade
    box has a bar across at row 9.
  - **The window frame:** a 1 px line on the content side of the cell,
    and nothing else (revised 2026-11-04: no shadow).
  - **The dialog border,** from the content out: a 2 px margin of the
    face, a 3 px line (Slate: grey, a highlight down its middle, as the
    approved mock-up draws it), a 1 px gap of the face and a 1 px outer
    line, and nothing beyond it.
  - **Button caps:** a rounded end 2 px in from the outside edge, with a
    radius of 4/11 of the cell height. The ring is 1 px, or 3 px for the
    default button. Its straight top and bottom run to the cell's inner
    edge on rows 0 and 21, or rows 0–2 and 19–21 for the default, where
    the label's SGR 51 lines carry on.

  Corners are tested at pixel centres in half-pixel integers, so C and
  Python agree exactly. No pixel is partial.
- **Other cell sizes.** The glyphs are pixel-designed for 11x22, and when
  the font size changes they keep their pixel widths:
  - **Fixed widths:** lines, stripes, outlines, the bevel rows and SGR 51
    stay 1 px. The dialog border's layers stay 2/3/1/1 px, and the
    default ring stays 3 px.
  - **Scaled with the cell:**
    - the box stays square and as wide as the cell;
    - there are six stripes at 22 px, and as many in proportion at other
      heights (eight at 28 px, five at 18), at least one, always 1 px
      apart and centred;
    - a button end's radius stays 4/11 of the height.
  - **Narrow cells:** where a cell is narrower than a piece needs, the
    piece is clipped to the cell. The dialog border needs 7 px, so only a
    cell narrower than that (Hack below 14 px) clips it.
  - **The smallest cell, 4x8 (Hack at 8 px):**
    - two stripes, which run into the bevel;
    - a 3x3 box;
    - button ends square: once a ring is as thick as its end is round, the
      inner radius is 0, and the 3 px default ring fills the 2 px left
      after the inset;
    - the dialog border cut to its inner 4 px.
    The pieces still join; they no longer read as chrome. The largest
    cell, 32x64, keeps the 1 px lines on a big cell, as asked.
- **Every font.** The glyphs come ahead of the icon font in the stack (ADR
  0195). A pixel font has none of them, so it takes them from the stack,
  as it does the bar corners. Checked under Blit Terminus 16: on its 8x16
  cell, all 105 match the design on screen.

### SGR 51 and 54

- **SGR 51** sets *framed*: a line along each framed cell's top edge and
  bottom edge, the cell's full width, 1 px (`BLIT_WINDOW_LINE`), in the
  underline colour (SGR 58) if one is set, else the foreground.
- **Blit's sub-parameters:**
  - **51:3:** the same lines `BLIT_WINDOW_THICK_RING` (3 px) thick. The
    default-button caps match it.
  - **51:2 and 51:4:** a Slate light or Slate dark title bar's top and
    bottom rows: the frame line, with the highlight under it, and the
    shade, with the rule under it. These are drawn in that set's palette
    and match the bar glyphs beside the title row for row. The underline
    colour does not apply.
  Any later SGR 51 replaces the kind.
- **SGR 54** ends framing, of every kind, and so does SGR 0. A framed
  blank still draws its lines.
- **This deviates from ECMA-48.** ECMA-48's *framed* draws a frame on all
  four sides of the text. Blit draws only the top and bottom; the cap
  glyphs and the bar's stripe cells close the ends. Four-sided framing
  would put vertical bars between the letters.
- **Framed is a cell attribute like underline,** in the same 16-bit cell
  attributes (four bits).
  - Scrolling and scrollback keep it (tested).
  - The alternate screen and repaints keep it by construction, because
    they copy or redraw the whole attribute array.
  - Both compositors draw it (tested).
  - DECRQSS reports `51`, `51:3`, `51:2` or `51:4`.
  - The cells API reports and accepts it as `style.framed`: `none`,
    `thin`, `thick`, `slate-light-bar` or `slate-dark-bar`.

## What Hibr needs to know

- **Pick a set per part.** Each setting (Frame, Title Bar, Buttons) names
  a set, and the part's glyphs come from that set's 35. Frame and dialog
  pieces are `frame-*`, `dialog-*` and `note-close`. The title bar is the
  `stripe*`, `title`, `close`, `shade` and `zoom` cells. The buttons are
  the `*button-*` caps.
- **How SGR 51 reaches Blit.** On the PTY path, Hibr writes these itself:
  `ESC[51m`, `ESC[51:3m`, `ESC[51:2m`, `ESC[51:4m`, `ESC[54m` and
  `ESC[58:2::r:g:bm`. The `display` provider path cannot carry framing
  yet:
  - `hibr/console-blit.c` writes cells through the cells API with bold,
    dim, underline and inverse only;
  - Hibr's display ABI (`mods/display.h`, `DP_API_VER` 6) has no framed bit
    and no underline colour.
  Framing there needs a Hibr ABI addition (the framing kind and an
  underline colour). The adapter would then write `style.framed` and
  `style.underline_color`, which the cells API already accepts. This ADR
  changes neither.
- **Title text cells** go on the bar's background, framed to match the
  Title Bar setting's set:
  - **Chalk:** SGR 51 with SGR 58 in the ink colour, which draws the
    frame line and the rule;
  - **Slate light:** SGR 51:2;
  - **Slate dark:** SGR 51:4.
  `stripe-before-title` and `stripe-after-title` stand either side.
- **Button labels** go on the button's face background with SGR 51, or
  51:3 for the default, with SGR 58 in the ring colour: black for Chalk
  and Slate light, 168,168,176 for Slate dark. The caps go on either
  side, with the dialog's face as their background. Without SGR 58 the
  lines take the text colour, which is wrong for Slate dark's light text.
- **Slate's button bevel** (light at the top and left, shade at the bottom
  and right) is drawn only round the two ends. The label cells between
  them are flat, because the buttons' SGR 51 draws only the outline.
- **A Chalk dialog's frame cells take the dialog's face as their
  background.** Chalk is one colour, so the border's margin and gap can
  only be the cell's background. The cells' area outside the border shows
  the face too: a white halo round the dialog. The
  owner chose this. Slate paints its own face there, so its frame cells
  keep the wallpaper.
- **Shadow is Hibr's.** No frame or dialog glyph draws a shadow (revised
  2026-11-04); Hibr's Shadow setting draws one, or none.
- **The Slate dialog and button face colours** must match the content
  cells: 238 and 221 in light, 52,52,58 and 78,78,86 in dark. The dark
  set's dialog and button faces are not in any mock-up; they were chosen
  from the dark palette.

## Rejected

- **A Chalk dialog on the desktop.** Leaving a Chalk dialog's frame cells
  on the desktop puts wallpaper behind everything outside the border. But
  it puts wallpaper in the border's margin and gap too, so the double line
  no longer reads as one border. The owner compared it with the
  face-background render and rejected it.
- **A Slate title framed with plain SGR 51.** The frame line and rule ran
  under the title, but the highlight and shade rows did not. The owner
  saw the title area as cut out of the bar.
- **A bar without a bevel.** It was considered and reversed. The bevel
  stays across every bar cell, the title included.

## Deviations from the mock-ups

- **Chalk takes Slate's geometry** (owner direction): full-width 11x11
  boxes, the to-box, from-box and between cells, and the 5 px title gap.
  The first Chalk mock-up's 9x10 boxes and 4 px title gap are gone.
- **No drop shadow in the glyphs** (owner direction, 2026-11-04): the
  mock-ups drew one into the frame; it is now Hibr's setting.
- **The button outlines sit on the cell's top and bottom rows (0 and 21),**
  where SGR 51's lines are. The mock-up drew them at rows 1 and 20. The
  default button is a 3 px ring in the same box, not a ring around a
  normal button's outline.
- **The Slate dialog's highlight runs down the middle of the 3 px line,**
  as the approved mock-up draws it. The brief described it on the line's
  inner edge.
- **Slate dark's controls are lighter** than any mock-up (owner
  direction): see O, R and H above.

## Evidence

- **`window_glyph_gate`** checks the glyphs through the production raster:
  - **The catalogue:** it lists all 105 in order with their names, as cell
    geometry, clear of the icons and every retired codepoint.
  - **The design:** every glyph equals the design script's, pixel for
    pixel, at 11x22 and at 4x8, 5x10, 6x12, 8x16, 9x18, 10x20, 12x24,
    14x28, 16x32, 22x44 and 32x64. Those run from the smallest to the
    largest cell Hack gives across the 8–64 px slider.
  - **Formats:** Chalk is coverage and Slate is RGBA in its own colours,
    in the raster's byte order; no pixel is partial.
  - **The approved mock-up at 11x22, written into the gate:** stripe and
    bevel rows, the 11x11 box at rows 5–15, the zoom square, the shade
    bar, the insets, the dialog's layers, and the caps' 1 px and 3 px seam
    rows.
  - **Footprints at every size, in every set:** each frame piece inks
    exactly its 1 px line and each dialog piece exactly its 7 px border,
    nothing else. The gate writes these out itself, not from the design.
  - **Slate dark's contrast:** the button ring, the default ring and the
    dialog's lines each stand at least 60 grey levels above their face. With
    the old dark values this fails: "Slate dark's button ring [8, 8, 10]
    does not read on its face".
  - **Seams at every size:** frame lines run edge to edge, the stripes in
    neighbouring cells sit on the same rows, and lines stay 1 px.
- **`window_style_gate`** runs a real server on both compositors (native
  and `BLIT_GATE_BACKEND=gles`) with Hack at 22 px, an 11x22 cell:
  - **Every glyph:** a session prints all 105 in an orange foreground on a
    blue background. All 25,410 pixels of those cells are what the design
    says: Chalk in the foreground, Slate in its own colours, the
    background wherever a glyph is transparent.
  - **SGR 51:** it draws 1 px top and bottom lines, 51:3 draws 3 px ones,
    and the underline colour is used when set. SGR 54 and SGR 0 end them,
    and a framed blank still draws.
  - **Titles:** text framed 51:2 and 51:4 has the frame line, highlight,
    shade and rule rows of the title glyph beside it, pixel for pixel.
  - **Buttons:** cap, framed label, cap is one unbroken outline, normal
    and default.
- **The pixel font:** under Blit Terminus 16 (8x16), a real server drew
  all 105 matching the design pixel for pixel (a one-off probe, not a
  gate).
- **`terminal_conformance`:** SGR 51 thin, 51:3 thick, 51:2 and 51:4 the
  Slate bars, 54 and 0 end them, a framed cell keeps its attribute in
  scrollback, and DECRQSS reports `51`, `51:3` and `51:4`.
- **Without the change, these fail:**
  - **The new gates:** on the server built from 0.3.35,
    `window_style_gate` fails with "the surface does not hold SGR 51 /
    51:3 / 54 as written". Without the raster branch, `window_glyph_gate`
    fails at its first codepoint: "U+100231 is not a whole cell at 11x22".
  - **`terminal_conformance`:** it fails at `framed-sgr`, where the framed
    cell has no style.
  - **Byte order:** the first build stored Slate's colours in BGRA order.
    `window_style_gate` caught it, on Slate dark's (8, 8, 10), and
    `window_glyph_gate` now checks byte order itself.
- **Owner renders,** drawn by the real server for the owner's approval:
  - the Chalk, Slate light (active and inactive) and Slate dark windows;
  - their notifications;
  - confirm dialogs with a normal and a default button;
  - a Slate title bar on Hibr's box-drawing frame, and a Chalk title bar
    on a Slate frame;
  - the Slate title before and after 51:2.
  They are at 11x22 and at 14x28, shown at 2x, with 6x close-ups of the
  title bar ends, the title and the buttons.

## Revision 2026-11-04: 51:5, the rule under a note's strip

The approved note has a black rule under its coloured strip, and 0.3.36
drew nothing there: plain 51 on the strip's cells would also draw a top
line directly under the frame-top row's line, a doubled 2 px line. Blit
adds `51:5`, a rule along the cell's bottom edge only, 1 px, in the
underline colour or the foreground, drawn by the same band function as
every other kind. Hibr puts it on the strip's cells.

The framing kinds are now one 3-bit field beside the FRAMED bit (0 thin,
1 thick, 2 and 3 the Slate bars, 4 the rule), mapped to the SGR
sub-parameters in one table, so the parser, DECRQSS, the cells API
(`framed: "rule"`) and both compositors read the same values. An unknown
sub-parameter is plain 51. `terminal_conformance` (framed-rule-sgr,
decrqss-sgr-framed-rule) and `window_style_gate` (the bottom line only,
both compositors) cover it.

## Revision 2026-11-04: no shadow in the frame glyphs

The owner made the drop shadow a setting of its own, Hibr's Shadow, so it
can be on or off, and mixed with any frame. The frame and dialog glyphs
therefore stop drawing it, in all three sets:
- **The frame pieces** each draw only their 1 px line against the content.
  `frame-right-top` now draws as `frame-right`, and `frame-bottom-first`
  as `frame-bottom`. Both keep their codepoints, so Hibr's layout need not
  change; `frame-bottom-right` is its corner pixel alone.
- **The dialog pieces** each draw only the border's four layers (margin,
  wide line, gap, outer line). Nothing lies beyond the outer line.
- **One change in the table:** the shadow constants
  (`BLIT_WINDOW_SHADOW`, `BLIT_WINDOW_SHADOW_OFFSET`) and Slate's shadow
  role are gone from `src/blit_window_glyphs.h`, because nothing uses them.
  The design script reads the table, so it changed with it. The catalogue
  shapes no longer mention a shadow.
- **A Blit-drawn shadow may follow, in its own ADR.** It would be an
  attribute drawn by the compositor rather than glyph pixels, so that any
  frame, in any set, can cast one.

`window_glyph_gate` now holds every frame and dialog piece, in every set
and at all 12 sizes, to exactly its line or border: no other pixel may be
set. Against the 0.3.37 drawing it fails at the first: "frame-right-top:
no line against the content, or a shadow beside it". `window_style_gate`
compares every glyph cell with the design on both compositors, so it
covers the change on screen.
