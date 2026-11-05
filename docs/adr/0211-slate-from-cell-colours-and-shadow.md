# ADR 0211: Slate takes its colours from the cell, and the drop shadow is a Blit-drawn cell attribute

- Status: accepted
- Date: 2026-11-05
- Scope: `src/blit_window_glyphs.h`, `blit_raster.c`/`.h`, `blit_font.h`,
  both compositors (`blit_kms.c`, `blit_kms_gles.c`, `blit_kms.h`), the
  cell attributes, the SGR parser, DECRQSS, the cells API and OpenAPI
  `CellStyle`, the icon catalogue, `tools/fonts/draw-window-glyphs.py`,
  `window_glyph_gate`, `window_style_gate`, `terminal_conformance`
- Builds on: ADR 0210 (the window-chrome glyph sets and SGR 51)

## Context

ADR 0210 drew Slate as fixed-colour art in two sets, light and dark. The
owner made two decisions:
- **Slate takes its colours from the cell.** There are no fixed palettes,
  and the owner accepted simpler bevels in return. Hibr colours Slate with
  its own schemes, and one set serves them all.
- **The drop shadow is its own setting** (since 0.3.38 no frame glyph
  draws one). It must combine with any frame: Chalk, Slate, or Hibr's
  box-drawing border.

## Decision A: Slate in the cell's pens

**Each Slate pixel is a role, resolved in the cell it is drawn in.** The
roles are defined once in `src/blit_window_glyphs.h`:

| Role | Colour | Used for |
| --- | --- | --- |
| BG | the cell's background | the bar |
| FG | the cell's foreground | frame lines, outlines, box ink, a dialog's lines, a button's ring |
| LINE | the underline colour (SGR 58), white if unset | the bar's highlight rows, the box face; and the dialog's face (margin and gap) and a button's face, where the application sets SGR 58 to those faces |
| MIX | each channel `(fg + bg) / 2`, rounded down, from the cell's own fg and bg (the default background if the cell has none) | the dark stripes, the bar's shade row, a button's shade |

- **BG is drawn as nothing.** Both compositors paint the cell's
  background before its glyph, so a BG pixel and a transparent pixel look
  the same: the cell's background shows.
- **FG, LINE and MIX are three coverage planes.** The raster draws each
  Slate glyph's three planes as one-colour coverage. It keeps them beside
  its glyph cache (`blit_raster_get_window_plane`, reached by a new
  `raster_plane_get` on `BlitFont`).
  - The CPU compositor draws each plane in its pen.
  - The GLES compositor makes each plane a sprite (the sprite key gains a
    plane) and tints each quad with its pen.
  - `blit_window_pens` resolves the three pens once per cell, and both
    compositors call it. The design script uses the same formula.
- **Pixel fonts** reach the planes the same way. Under Blit Terminus 16
  (8x16), a real server drew all 70 exactly as the design says, Slate in
  its pens (a one-off probe, not a gate).
- **A plain lookup** of a Slate glyph (the block cursor's redraw, or a
  path that knows nothing of pens) gets the union of the three planes,
  drawn in one colour, as Chalk is.
- **Bold and italic** do not apply to window glyphs.

**The roles of each Slate glyph** (U+100254–100276, at 11x22; rows count
from the top):
- **Title bar cells.** These are stripe, stripe-first, stripe-last,
  stripe-before/after-title, stripe-to/from-box and stripe-between.
  - FG: the frame line on row 0 and the rule on row 21.
  - LINE: the highlight on row 1, and the light row under each stripe
    (rows 6–16, even).
  - MIX: the stripes on rows 5–15 (odd), and the shade on row 20.
  - BG: everything else.
- **title:** the same without stripes. FG on rows 0 and 21, LINE on row
  1, MIX on row 20, BG elsewhere.
- **close, shade, zoom:** the title cell plus the box.
  - FG: the box's outline, the shade bar and the zoom square.
  - LINE: the box's face.
- **frame-\*:** FG only, the 1 px line.
- **note-close:** FG outline, LINE face; nothing round it.
- **dialog-\*:** from the content out, only FG and LINE, and nothing
  beyond 7 px (the cell's remaining pixels show its background, the
  wallpaper):
  - LINE: the 2 px margin;
  - FG: the 3 px line, solid;
  - LINE: the 1 px gap;
  - FG: the 1 px outer line.
- **button-left and button-right:**
  - FG: the 1 px ring;
  - LINE: the face inside the ring;
  - MIX: a 1 px shade just inside the ring, round the end only, where the
    edge faces down or right; there is no highlight;
  - nothing: the 2 px inset and the corners outside the ring, where the
    cell's background (the dialog's face) shows.
- **default-button-left and -right:** the same, with a 3 px FG ring.

**The Pewter scheme** reproduces 0.3.38's Slate light for the bar, boxes
and frame, except on MIX pixels. The dialog and the buttons were redrawn
(below).

| Cells | Background | Foreground | SGR 58 |
| --- | --- | --- | --- |
| title bar and its glyphs, title text | 214,214,214 | 0,0,0 | 255,255,255, or unset |
| dialog border cells (`dialog-*`) | the wallpaper (default) | 0,0,0 | 238,238,238, the dialog's face |
| dialog content cells | 238,238,238 | 0,0,0 (text) | unset |
| button caps | 238,238,238, the dialog's face | 0,0,0 | 221,221,221, the button's face |
| button labels | 221,221,221 | 0,0,0 (text) | 0,0,0, for SGR 51's outline |
| frame cells | the wallpaper (default) | 0,0,0 | unset |

- **Differences from 0.3.38's Slate light in the bar, boxes and frame:**
  MIX pixels only. The stripes and the shade were 119 and are now
  (0 + 214) / 2 = 107. `window_glyph_gate` holds the 23 glyphs ADR 0211
  leaves alone, at 11x22, against 0.3.38's own Slate light reference (a
  fixture made by 0.3.38's design script). The 528 pixels that differ are
  all MIX.
- **The dialog and the buttons are redrawn** (owner direction, 2026-11-05),
  to remove the halo and the square corners the first version of this ADR
  had:
  - **Dialog frame cells keep the wallpaper as their background.** The
    margin and the gap are the line pen, with SGR 58 set to the dialog's
    face. The 3 px line is solid foreground, so there is no highlight and
    no grey: no pixel of any fixed colour. Nothing is drawn beyond the
    border's 7 px, so the wallpaper shows right up to it, with no halo.
  - **Button caps sit on the dialog's face.** The face inside the ring is
    the line pen, with SGR 58 set to the button's face. The 2 px inset
    and the corners outside the ring are the cap's background, the
    dialog's face, so a button is round on a flat dialog. The highlight is
    gone, and the shade (MIX) stays on the edges that face down or right:
    (0 + 238) / 2 = 119 in Pewter, where 0.3.38's was 153.
  - **Chalk's halo cannot go the same way within one colour.** Chalk's
    margin and gap are transparent, so they show the cell's background. A
    cell cannot show the dialog's face there and the wallpaper beyond the
    border, so Chalk's frame cells still take the face as background. The
    way out is the Slate dialog glyphs, which are now only foreground and
    line pen: in black on white, with SGR 58 white, they draw Chalk's look
    with no halo (`chalkpens_dialog_2x.png`).
- **A dark scheme** recolours the same set. For example: bar 60,60,67,
  foreground 225, SGR 58 112 (MIX 142,142,146). A dark dialog (face
  52,52,58, buttons 78,78,86, foreground 225): the border cells' SGR 58 is
  52,52,58; the caps' background is 52,52,58 and their SGR 58 78,78,86;
  a cap's shade is (138,138,141).

**Slate dark is retired.** U+100277–100299 are listed under `retired` in
the catalogue and never reused, and they draw nothing of their own (the
PSF's fallback). Its palette and the O, R and H control roles, which
existed only for it, are gone. The catalogue lists 70 window glyphs, and
each now says `colour: false` and `pens`: `foreground` for Chalk, `cell`
for Slate. The ids keep their names (`slate-light-*`) so lookups by id
keep working.

**The title bar framing follows the pens.** SGR 51:2 draws the bar's four
rows from the same pens: FG frame line, LINE highlight, MIX shade, FG rule.
51:4, once Slate dark's, is now redundant:
- the parser stores it as 51:2, so an old Hibr still draws a title;
- DECRQSS reports `51:2`;
- the cells API reports `bar`, and still accepts `slate-light-bar` and
  `slate-dark-bar` on input, as aliases of `bar`.

## Decision B: SGR 66, a drop shadow drawn by Blit

**A shadow is a cell attribute.** Blit draws it after the cell's
background and before its glyph, so it sits under whatever the cell holds.
It is clipped to the cell, like the underline.

**Kinds.** Hibr sets the shadow on a window's right column and on the row
under it. In a w×h cell, with t the width (1 or 2 px) and i the inset
(1 by default, or 0):

| Kind | Name | Pixels |
| --- | --- | --- |
| 1 | right | x ∈ [i, i+t), every row |
| 2 | right-start (a window's first row) | x ∈ [i, i+t), from row 2 down |
| 3 | under | every column, y ∈ [i, i+t) |
| 4 | under-start (a window's first cell) | x from 2−i, y ∈ [i, i+t) |
| 5 | corner | where the strips meet: [i, i+t)×[0, i+t) and [0, i+t)×[i, i+t) |

**Inset 1 (the default) is for Blit's frame glyphs.** The shadow goes on
the frame cells themselves (frame-right-top, frame-right,
frame-bottom-first, frame-bottom, frame-bottom-right), and its strips
start 1 px in (`BLIT_WINDOW_LINE`), just outside the frame's line. A
frame glyph with a 2 px shadow then draws exactly what the frame glyphs
drew before 0.3.38.

**Inset 0 (flush) is for a frame that fills its own cells.** Hibr's
box-drawing border sits on the window's face colour, so the window's
edge is its cells' outer edge. The shadow then goes on the next column
and row, with its strips at the cell's edge.
- Inset 1 there would leave a 1 px gap of wallpaper; the first render
  showed it, which is why inset exists.
- The bottom row starts under the window's first cell (its left frame
  cell), from 2 px in.

- **The offsets.** right-start begins 2 px down from the window's top
  (`BLIT_WINDOW_SHADOW_OFFSET`). under-start begins 2 px from the
  window's left edge.
  - Inset 1: that edge is the left frame glyph's line, 1 px into the
    previous cell, so the strip starts 1 px into its own cell. This keeps
    0.3.37's approved pixels.
  - Inset 0: the edge is the cell's own, so the strip starts 2 px in, as
    the brief asks.
- **The corner** with inset 1 leaves (0,0) empty, where a frame's corner
  pixel sits; flush, it is the t×t square.

**Colour:** the underline colour (SGR 58) if set, else the foreground.
Hibr colours it from its scheme.

**The sequence: SGR 66:kind:width:inset, ended by SGR 67.**
- **The parameters:**
  - kind is 1–5, as in the table above;
  - width is 1 or 2, and 2 if it is left out;
  - inset is 1 or 0, and 1 if it is left out;
  - `66` with no kind, or `66:0`, ends the shadow, as `67` does;
  - an unknown kind, width or inset leaves the pen alone;
  - SGR 0 ends it.
- **Colon form only.** `66;1;2` would be three SGRs: 66 (no kind, so
  off), bold and dim.
- **Why 66 and 67:**
  - The block ends at 65 (60–65 are the ideogram attributes), and the
    next assignments we found are 73–75, superscript and subscript.
  - We read these on 2026-11-05, and none assigns 66 or 67:
    - xterm's ctlseqs, whose SGR list has nothing from 50 to 89;
    - WezTerm's escape-sequence reference, which has 73–75 and nothing
      else from 60 to 89;
    - mintty's CtrlSeqs page, which lists no SGR above 50;
    - Wikipedia's SGR table, which leaves 56–57, 66–72, 76–89 and 98–99
      empty, and says it is not exhaustive.
  - We did not read VTE's, kitty's, foot's or iTerm2's tables, nor the
    ECMA-48 text itself.
  - 66 sits next to ECMA-48's own block, and pairing it with 67 follows
    ECMA's own pairs (51 and 54, 53 and 55).
  - A shadow is not framing, so it does not share 51.
  - A terminal that does not know 66 should drop the whole colon-form
    parameter, as unknown SGRs are; we have not tested that on others.
- **DECRQSS** reports `;66:kind:width`, with `:0` after it when the shadow
  is flush.
- **The cells API** reports and accepts `style.shadow`: `none`, or the
  kind's name and width, with `:0` when flush. For example `right:2`,
  `under-start:1:0`. These are in OpenAPI `CellStyle`.

**The cell attributes are 32 bits now.** They were 16 bits: bits 0–10 were
the styles, bit 11 marked framing, and bits 12–14 held its kind. The
shadow needs five bits, so they are widened, not squeezed into bit 15.
- **Where:**
  - the surface and alternate-screen arrays, scrollback lines and the
    session's pen;
  - DECCRA's row copy and DECCARA's masks;
  - the compositors' view, which shares the surface's array by pointer;
  - the five C gates that build a view of their own. A 16-bit array there
    still compiles, with only a pointer warning, and then reads two cells
    as one: `font_kms_raster` failed that way until they were widened.
- **The shadow's bits:** its kind is bits 16–18, its width bit 19 and its
  inset bit 20.
- **Gated:** `terminal_conformance` puts a shadow and framing on a cell,
  passes it through the alternate screen and into scrollback, and reads
  both back. It also writes a shadow through the cells API and reads it
  back.

## What Hibr needs to know

- **Slate cells:** set the cell's background, foreground and SGR 58 from
  the scheme. Pewter is in the table above.
  - SGR 58 unset means white highlights.
  - **A dialog's border cells** keep the default (wallpaper) background,
    with SGR 58 set to the dialog's face. Without it, the margin and gap
    are white.
  - **Button caps** have the dialog's face as background and SGR 58 set
    to the button's face. Without SGR 58 the face is white.
  - **Button labels** have the button's face as background, and SGR 51
    with SGR 58 as the outline's colour, as before.
  - Title text: the bar's colours, with SGR 51:2.
  - Stop using U+100277–100299 and SGR 51:4. Both still parse, but the
    codepoints draw the fallback `?`.
- **Shadow:** `ESC[66:<kind>:<width>:<inset>m` on the cells of a window's
  right column and bottom row, then `ESC[67m`.
  - The column's first row is kind 2, the rest kind 1.
  - The bottom row's first cell is kind 4, the rest kind 3.
  - The cell where they meet is kind 5.
  - With Chalk or Slate frames, inset 1, set on the frame cells:
    - right column: frame-right-top (kind 2), then frame-right (kind 1);
    - bottom row: frame-bottom-first (kind 4), frame-bottom (kind 3),
      frame-bottom-right (kind 5).
  - With a box-drawing frame on the window's face, inset 0, set on the
    cells beyond it:
    - the column right of the window, from its first row (kind 2) down;
    - the row under it, from the left frame's column (kind 4) to the
      right frame's (kind 3);
    - kind 5 at the corner beyond both.
  - Set its colour with SGR 58 if it isn't the foreground; in a light
    scheme whose frame ink is light, set it explicitly.

## Revision 2026-11-05 (later): no halo, round buttons on a flat face

The first version of this decision gave the dialog's margin and gap the
cell's background, so a Slate dialog's frame cells had to carry the
dialog's face, which showed as a halo of face round the border, and its
caps' square corners showed the button's face. The owner chose instead:
the dialog's frame cells keep the wallpaper and the glyph paints the face
with the line pen (SGR 58 the face); the wide line is solid foreground;
button caps sit on the dialog's face and paint the button's face with the
line pen. The roles, the Pewter table and the Hibr notes above are the
result; BG is now only the bar's.

## Evidence

- **`window_glyph_gate`:**
  - **The catalogue:** 70 glyphs with their names and pens, and Slate dark
    retired.
  - **Slate's planes:** disjoint, together equal to its one-colour lookup,
    and pixel-exact against the design at 12 cell sizes.
  - **Retired:** Slate dark draws nothing of its own, and has no planes.
  - **Pewter:** the 23 glyphs left alone equal 0.3.38's except on MIX
    pixels (528).
  - **The dialog:** every piece is only FG and LINE pixels, the margin and
    gap LINE, the lines FG, nothing beyond 7 px (written out in the
    gate).
  - **The caps:** the inset and the corners outside the ring are clear,
    the face is solid LINE from the ring to the seam, and the shade (MIX)
    is only down and right (in the left cap, the lower half).
  - Its earlier checks (footprints, seams, the 11x22 mock-up pixels) are
    unchanged.
- **`window_style_gate`,** with a real server on native and gles, at
  11x22:
  - **Every Slate pixel in the cell's pens.** Slate is printed with and
    without SGR 58, and every pixel matches: FG orange, LINE white or the
    SGR 58 colour, MIX the per-channel half.
  - **The dialog on the wallpaper:** every pixel of its eight border
    cells is the foreground, the face (SGR 58) or the wallpaper, and
    everything beyond the 7 px border is the wallpaper itself: no halo,
    no fixed colour.
  - **The caps on the dialog's face:** the corners and the 2 px inset
    are the dialog's face, the face inside is the button's, the shade is
    half the foreground and half the dialog's face, and only down and
    right.
  - **Titles:** the title glyph and text framed 51:2, and 51:4, have the
    same four rows (0, 255, 107 and 0 in Pewter).
  - **Shadows:** all twenty kind, width and inset combinations are
    exactly their strips (written out in the gate), with nothing after SGR
    67. A shadow
    is drawn in the SGR 58 colour when set, and is gone after SGR 0. A
    Chalk frame-right over a red right:2 shadow keeps its line on top.
- **`terminal_conformance`:**
  - SGR 66 kinds, widths and insets; 67, 66:0 and 0 end it; an unknown
    kind or inset leaves it.
  - The alternate screen and scrollback keep it, and the cells API writes
    and reads it.
  - DECRQSS reports 66 and the 51:4 → 51:2 alias.
- **Against the 0.3.38 server:**
  - `window_style_gate` fails: "the surface does not hold SGR 66:1:1:1
    then 67 (nil)".
  - `terminal_conformance` fails at the bar's name: "expected bar, bar,
    thin; got slate-light-bar, slate-dark-bar, thin".
  - `window_glyph_gate`'s harness needs the plane lookup, which 0.3.38
    does not have.
