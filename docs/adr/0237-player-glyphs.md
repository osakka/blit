# ADR 0237: player glyphs -- LCD digits, LED meters, sliders, transport, discs

- Status: accepted
- Note: Shipped in 0.3.126; amended in 0.3.127 (keys, big discs), 0.3.136
  (slider styles) and 0.3.137 (scroll bar styles), the sections below.
- Revised by: ADR 0243 (0.3.131): every icon codepoint moved up by
  0x1000 (U+100314-1003D0 in the scope and decision below, and the
  0.3.127 glyphs, are now U+101314-101494), the same layout; the
  amendments from 0.3.136 on are placed in the new range only.
- Date: 2026-11-19
- Scope: Blit's private-use plane-16 glyphs U+100314-1003D0, the shared
  raster's cell geometry, the icon font

## Context

huiamp, the Hibr audio player, wants an instrument's face in a terminal: a
time display, level meters, seek and volume sliders, equalizer faders,
transport buttons and something turning while music plays. Built from text
characters these look like text; a picture per frame costs a frame upload
each time a digit changes. The owner approved a preview (2026-11-19) drawn
exactly as Blit would draw it at the owner's 12x24 cell.

## Options considered

1. Pictures (Kitty graphics) -- any look, but every change is an upload and
   the program owns the scaling.
2. Outline glyphs in the icon font for everything -- digits, meters and
   sliders must join edge to edge between cells, which a fitted outline
   (90% of the advance, centred) never does.
3. Cell geometry for the pieces that join (digits, meters, sliders), drawn
   by the shared raster from the cell's size like the window chrome (ADR
   0210); icons for the shapes that stand alone (transport, discs).

## Decision

Option 3.

- **Cell geometry (105, U+100314-10037C), `src/blit_player_glyphs.h`:**
  coverage in the cell's foreground, so the program picks the colour (a
  green, amber or blue LCD; green, yellow and red LEDs). Unlit LCD segments
  and LEDs are drawn faint (20%), as a real LCD shows them; a track ahead
  of a knob is lighter (38%). Big digits are two cells by two rows
  (`lcd-big-<c>-tl/-tr/-bl/-br`), small digits two cells by one row
  (`lcd-<c>-left/-right`), for 0-9, `minus` and `blank`, with big and small
  colons; LEDs `led-h-0..8` (two a cell) and `led-v-0..8` (four a cell)
  with peak caps; `slider-*` and `fader-*` caps, tracks, filled tracks and
  knobs. Sized from the cell, so a digit's or a meter's pieces meet without
  a seam at any size.
- **Icons (84, U+10037D-1003D0), drawn by `tools/fonts/draw-player-icons.py`:**
  twelve transport buttons (play, pause, stop, prev, next, rewind, forward,
  eject, record, repeat, repeat-one, shuffle), each one cell
  (`transport-<name>`) and two cells (`transport-wide-<name>-left/-right`),
  in the text colour (catalogue `text_glyphs`, `colour: false`); and the
  now-playing discs -- `disc`, `vinyl`, `cassette`, frames 0-7, two cells
  each, in their own colours -- which a player steps through while music
  plays and leaves at frame 0 when stopped.
- Codepoints run on from U+100314, the first free one, and are never
  reused; the catalogue (`GET /v1/fonts/icons`) names each, and
  docs/fonts/special.md is generated from it.

## Consequences

A player draws its whole face as text: a time change is two cells of text,
a meter is a run of cells, and both follow the font size. The icon font
grows from 317 to 401 glyphs. The reference page's deprecation notes are now
generated from the catalogue (they were hand-added to a generated page).
icon_clip_gate now reads a text glyph's columns (it assumed one).

## Evidence

- `player_glyph_gate` at every cell size from 8 to 64 px tall and 12x24,
  through the raster: each big and small digit lights exactly its segments
  and draws the rest faint; an 8's top segment runs unbroken across its two
  cells; colons are two dots; each LED step lights more than the last
  (strictly where an LED is wide enough to show it), 0 none, 8 all; slider
  and fader tracks run edge to edge across cells; every glyph is coverage,
  never a fallback, with ink, in its cell.
- custom_icon_gate (every icon at 57 sizes in every text font, colour ink,
  distinct drawings), icon_clip_gate (no ink lost, two-cell parts joining),
  catalogue_fields_gate (the new parts in the schema's and README's
  vocabulary), blit_icons_rebuild_gate (the font is its sources, byte for
  byte), font_reference_gate: the font suite, 51 pass.

## Amended 2026-11-20 (0.3.127): keys

The owner chose one-row keys from a second preview. A key is `key-left`, the
icon cells, `key-right` on one row. Its three colours are the cell's: the
face is the background (SGR 48), the light edge and the icon the foreground,
the dark edge the underline colour (SGR 58).

- The ends, U+1003D1-1003D4 (`key-left`, `key-right`, `key-pressed-left`,
  `key-pressed-right`), are cell geometry in the cell's pens like the Slate
  glyphs: light along the top and the outer side, dark along the bottom
  (and the right end's side), the dark edge taking the corners it meets;
  pressed is the reverse. Part `key`, set `key`, line `shade`.
- The icon cells carry SGR 51:6 (raised) or 51:7 (pressed): the same 2 px
  bevel across them as two framing bands, so the ends meet them edge for
  edge; a 51:7 cell's glyph is drawn 1 px lower, so a pressed key's icon
  goes down with it. The framing field's free values took them; 51:4 and
  51:5 were already Slate's synonym and the note's rule (hibr noticed).
- window_style_gate draws a raised and a pressed key on the CPU and GLES
  compositors: the bands' colours and widths, each end's sides and corners,
  and the pressed `H` exactly 1 px below the raised one.

## Amended 2026-11-20 (0.3.127): big discs, icons over rows

The owner approved bigger now-playing discs. The preview drew them about 38 px
across at the 12 x 24 cell, which is four cells by two rows; a 2 x 2 block (24
px wide) could not hold that, so they are 4 x 2: `<kind>-big-<f>-r<row>c<col>`,
U+1003D5 + 64 kind + 8 f + 4 row + col (disc, vinyl, cassette; frames 0-7),
U+1003D5-100494, the same drawings as the two-cell discs.

Icons could span columns (ADR 0197), not rows. Now an icon can span both:
the catalogue's `rows` and `row`, and a private 'BLrw' table in the icon font
(glyph id, rows, row, written by build-blit-icons.py) that the raster reads
when it opens the font. Such a part is fitted to all its rows (80% of their
height, as a one-row icon is to one) and placed by its row, an inner top or
bottom edge being its neighbour row's, as an inner side is its neighbour
column's. custom_icon_gate checks a multi-row icon's ink and colour whole (a
corner part may be empty at a small cell); icon_clip_gate allows ink toward a
neighbour row. Rendered at 12 x 24, the parts join without a seam.

## Amended 2026-11-22 (0.3.136): slider styles

The owner approved two more looks for the slider and the fader (2026-11-22,
from a preview drawn as Blit draws them at the owner's 12x24 cell), for a
Slider Style choice in Hibr's Control Panel. They are cell geometry like the
grooved knob's (`slider-*`, `fader-*`): coverage in the cell's foreground,
sized from the cell, no pens (`src/blit_slider_glyphs.h`, drawn through
`blit_chrome_glyph`/`blit_chrome_draw`).

- **Circle** (U+101495-10149E): a round knob on a thin line. The line is
  2 px at 24 (1 at 16, 3 at 40), the track ahead of the knob lighter
  (38%, as the current style's); the caps end the line round at the
  cell's middle; the knob is a disc the cell's width, at most half the
  row, antialiased.
- **Groove** (U+10149F-1014A8): a channel a quarter of the row tall,
  hollow ahead of the knob (its two edges) and solid behind it, and a
  square thumb standing over it; every piece is full coverage on whole
  pixels. The channel's ends line up with the thumb's edges at either end
  of its travel.

A style is a base codepoint and the current style's offsets, so a program
swaps the base and the cells line up one for one:

| | slider base | fader base |
| --- | --- | --- |
| current (grooved knob) | U+101373 | U+101378 |
| circle | U+101495 | U+10149A |
| groove | U+10149F | U+1014A4 |

| offset | slider | fader |
| --- | --- | --- |
| +0 | `cap-left` | `cap-top` |
| +1 | `track` (ahead of the knob) | `track` (above the knob) |
| +2 | `track-filled` (behind the knob) | `track-filled` (below the knob) |
| +3 | `knob` | `knob` |
| +4 | `cap-right` | `cap-bottom` |

The ids are `slider-<style>-<piece>` and `fader-<style>-<piece>` (for
example `slider-circle-knob`, `fader-groove-cap-bottom`), part `slider` or
`fader`, `background` panel, `foreground` ink, `line` none. A slider is the
left cap, the filled track up to the knob, the knob, the track after it and
the right cap, on the row's centre line; a fader the same top to bottom,
filled from the bottom. **The knob's cell holds the knob alone**, never a
piece of track: a program pens the knob in a colour of its own, and at 0%
and 100% the knob takes an end's cell. Every piece's centre line is within
half a pixel of the current style's, so switching styles never moves the
line.

The two styles take the next free codepoints after the big discs; the next
free is U+1014A9. U+101495 was one past the last glyph that moved in ADR
0243's transition, whose old range was written to U+100495 and so would
have drawn `slider-circle-cap-left` there; that range now ends at the last
glyph that moved, U+100494 (ADR 0243). The icon font does not change: cell
geometry is drawn by the raster, not the font.

- `player_glyph_gate` holds the 20 at every cell size from 8 to 64 px tall
  and 12x24, through the raster: coverage, never a fallback, with ink; a
  slider's cap, filled track and track and a fader's join edge to edge
  across cells with the same profile; the circle's knob is exactly its
  disc and the groove's one solid square over the channel; each piece's
  centre line within half a pixel of the current style's; U+1014A9 draws
  nothing. Built without the styles' hook it fails.

## Amended 2026-11-23 (0.3.137): scroll bar styles

The owner approved three scroll bar styles (2026-11-23, from a preview
drawn as Blit draws them at the owner's 12x24 cell, composed cell for cell
as Hibr's `dt_scrollbar` and `dt_hscrollbar` compose a bar), for a Scroll
Bar Style choice in Hibr's Control Panel beside the Slider Style. They are
cell geometry like the slider styles: coverage in the cell's foreground,
sized from the cell, no pens (`src/blit_scrollbar_glyphs.h`, drawn through
`blit_chrome_glyph`/`blit_chrome_draw`), part `scrollbar`, `background`
panel, `foreground` ink, `line` none.

- **Classic** (U+1014A9-1014B8): a track as thick as the cell is wide,
  two edge lines and a fine checker between them, closed by a solid line
  at the caps; the thumb a hollow box on the edges, with three grip bars
  across its middle where they fit (always from a 12 px wide cell). Whole
  pixels, no antialiasing: it stays crisp at 8x16. The checker carries on
  across cells at any length: its runs along the bar are an even number a
  cell, so its phase turns over at every seam. They are unit-long wherever
  the cell is a whole number of unit pairs long (8x16, 12x24 and 20x40,
  as previewed, are unchanged). A checker of identical cells cannot be
  perfect at any other length, so there the odd pixels go to runs toward
  the cell's middle: at an odd length with a unit of 1 (5x9, 11x22, and
  the vertical bar at every odd height) one run in the middle of each cell
  is a pixel longer, and the seams look like any other step.
- **Modern** (U+1014B9-1014C8): a capsule thumb about half the cell's
  width (an even number of pixels, 6 at 12), round ends antialiased, on a
  faint track (28% coverage) of the same band.
- **Simple** (U+1014C9-1014D8): the track is the light box-drawing line,
  **pixel for pixel Blit's U+2500 (horizontal) and U+2502 (vertical)**, so
  a bar drawn with it looks as one drawn with box drawing, the look Hibr
  has today; the thumb a solid rectangle centred on it.

Why three: classic is the crisp, period look that matches Chalk and Slate;
modern is the quiet overlay look most desktops use now; simple keeps
today's unicode bar and only adds a proper thumb and ends.

A style is a base codepoint and the same offsets, horizontal then
vertical, so a program swaps the base:

| style | horizontal base | vertical base |
| --- | --- | --- |
| classic | U+1014A9 | U+1014B1 |
| modern | U+1014B9 | U+1014C1 |
| simple | U+1014C9 | U+1014D1 |

| offset | horizontal | vertical |
| --- | --- | --- |
| +0 | `cap-left` | `cap-top` |
| +1 | `track` | `track` |
| +2 | `thumb` (one cell) | `thumb` |
| +3 | `thumb-start` (a long thumb's left end) | `thumb-start` (its top) |
| +4 | `thumb-mid` | `thumb-mid` |
| +5 | `thumb-grip` (a long thumb's middle cell) | `thumb-grip` |
| +6 | `thumb-end` (its right end) | `thumb-end` (its bottom) |
| +7 | `cap-right` | `cap-bottom` |

The ids are `scrollbar-<style>-<h|v>-<piece>` (for example
`scrollbar-classic-v-thumb`, `scrollbar-simple-h-cap-right`). A bar is the
track in every cell, a cap in the first and the last, and the thumb over
the cells where the view is: `thumb` in one cell, or `thumb-start`,
`thumb-mid` (with `thumb-grip` in the middle cell) and `thumb-end` for a
thumb sized to the view. **The thumb's cell holds the thumb alone**, never
a piece of track: a program pens the thumb in a colour of its own (Hibr
pens the track idle and the thumb active), and the thumb takes an end's
cell at the top and the bottom of its travel. So a cap ends where the
thumb's end does in that cell. A horizontal bar is as thick as a vertical
one (both from the cell's width), on the row's middle.

The styles take the next free codepoints after the slider styles; the next
free is U+1014D9 (the slider amendment's U+1014A9 check moved there). They
have no old codepoint (ADR 0243). The icon font does not change.

- `scrollbar_glyph_gate` holds the 48 at every cell size from 8 to 64 px
  tall and 12x24, through the raster: coverage, never a fallback, with
  ink; the track and caps join edge to edge (classic: the edges equal and
  the checker's phase turning over at every seam, no run across a seam
  longer or shorter than one inside a cell, its first square anchored);
  a cap's first (last) inked line is the thumb's; the thumb alone (the
  classic box hollow but for its grip bars, the modern capsule in the
  band and clear of the cell's ends, the simple one solid rectangle);
  the long thumb's pieces join; simple's track equals U+2500/U+2502; a
  horizontal bar as thick as a vertical one; U+1014D9 draws nothing.
  Built without the hook it fails, and it catches a checker phase off by
  one, a gap in a cap, a thumb with track in it, a short long-thumb piece
  and a simple track a pixel off box drawing.
