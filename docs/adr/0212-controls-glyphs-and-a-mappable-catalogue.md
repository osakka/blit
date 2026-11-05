# ADR 0212: The controls glyph set, and a catalogue a client can map dynamically

- Status: accepted
- Date: 2026-11-05
- Scope: `src/blit_control_glyphs.h` (new), `src/blit_sha256.h` (new),
  `blit_window_glyphs.h` (one helper), `blit_raster.c`/`.h`, both
  compositors, `blit_server.c` (the icons endpoint, the capabilities),
  the icon catalogue, `tools/fonts/draw-control-glyphs.py` (new) and
  `draw-window-glyphs.py`, `control_glyph_gate`, `control_style_gate`,
  `catalogue_fields_gate` (new), `custom_icon_api_gate`,
  `terminal_conformance`, OpenAPI
- Builds on: ADR 0191 (cell geometry), ADR 0210 (the window chrome), ADR
  0211 (Slate from the cell's pens)

## Context

Two things came together.

**The owner approved mock-ups of controls** to go with the window chrome:
checkboxes, a compact two-cell switch, and a capsule switch (a round knob
in a pill track, the track's colour chosen by the application: green on,
grey off). They are drawn like the windows: a glyph per cell, Chalk in one
colour and Slate in the cell's pens.

**The owner asked whether glyph ids are published in a way a consumer such
as Hibr can map dynamically.** `GET /v1/fonts/icons` serves the catalogue,
but the pens an application has to set for a cell-geometry glyph (what its
background, foreground and SGR 58 should be) were only in ADR prose, so a
consumer would have to hard-code them. A client that keeps the catalogue
also had no way to tell that it had changed, and no way to switch on which
SGR extensions a server has other than by its version.

## Decision A: the controls

### The set: 20 glyphs, U+10029A–U+1002AD

The brief for this set said 26 glyphs, U+10029A–U+1002B3. Its own ordered
list is 3 + 3 + 4 + 4 + 6 = 20, so that is what is built, in that order.
**The next free codepoint is U+1002AE.**

| Codepoints | Glyphs | Cells |
| --- | --- | --- |
| U+10029A–10029C | `chalk-check-off`, `-on`, `-mixed` | one cell each |
| U+10029D–10029F | `slate-check-off`, `-on`, `-mixed` | one cell each |
| U+1002A0–1002A3 | `chalk-switch-off-left`, `-off-right`, `-on-left`, `-on-right` | two cells: left then right |
| U+1002A4–1002A7 | `slate-switch-off-left`, `-off-right`, `-on-left`, `-on-right` | two cells: left then right |
| U+1002A8–1002AA | `capsule-off-left`, `-off-mid`, `-off-right` | three cells: left, middle, right |
| U+1002AB–1002AD | `capsule-on-left`, `-on-mid`, `-on-right` | three cells: left, middle, right |

A switch or capsule is written side by side, one codepoint per cell (ADR
0197): `off-left` then `off-right`, for example.

### Roles and pens

They use the window glyphs' roles (ADR 0211), so there is one drawing path:
- Chalk is one colour (coverage), drawn in the cell's foreground.
- Slate and the capsule resolve each pixel in the cell it is drawn in, from
  the foreground, SGR 58 (white if unset) and the mean of the foreground
  and background. The raster draws them as three coverage planes, and both
  compositors tint each plane in its pen.
- All of the pen-drawn glyphs share the window glyphs' plane cache: one
  dense index, 35 Slate window glyphs then 13 controls. `blit_chrome_*`
  wrappers in `blit_control_glyphs.h` replace the window-only calls in the
  raster and the compositors. U+100277–100299, retired, still draw nothing.

### The checkbox

One cell: an 11x11 square on the rows 5–15, as the window boxes are. Its
box is the window box's computation (`blit_window_band`, now one helper
used by both), so it lands on the same rows and has the same side at every
cell size.
- **Chalk:** a 1 px outline, nothing inside. Checked: a 1 px X, corner to
  corner of a square 2 px in from the outline. Mixed: a 1 px dash across
  the middle, the X's width.
- **Slate:** an FG outline, a LINE face, and a MIX shade on the bottom and
  right inside edge. Checked: a 2 px FG check mark. Mixed: a heavier, 2 px
  dash centred on the face.

### The compact switch

Two cells, 22x11, on the box's rows: a stadium track (ends round, radius
half the height), with a knob 7 px across, 1 px clear of the ring.
- **Chalk:** off, a 1 px ring and a solid knob on the left. On, a solid
  track with the knob left out of it, on the right.
- **Slate:** a FG ring. Off: the cell's background for a face, a LINE
  highlight along the first row inside the ring (stopping a pixel short of
  the curve), a knob on the left with a FG ring, a LINE face and a MIX shade
  on its last row. On: a MIX track, the same knob on the right.

### The capsule

Three cells, 33x22, one set, **no outline**. The track is the FG (the
application chooses its colour: green on, grey off), the knob LINE (SGR 58,
white), the smoothed edge pixels MIX (the mean of FG and BG), BG elsewhere.
No glyph carries a colour.
- **Geometry**, in 22nds of the cell's height: the track from 1 px in at
  each end, 18 high (rows 2–19), its ends round (radius 9); the knob 14
  across, 2 px from the track's end, centred.
- **Coverage:** each pixel is sub-sampled 8x8 in integers (positions in
  1/22 of 1/16 of a pixel, so the table's lengths multiply in exactly, in
  C and in Python alike), counting the samples inside the track and the
  knob, of 64.
  - A knob with 48 or more is LINE; 16 or more is MIX, whatever lies under
    it.
  - Else a track with 48 or more is FG; 16 or more is MIX.
  The knob's edge therefore mixes toward the background, not toward the
  track. That is the approved look.

### What happens at other cell sizes

The glyphs are pixel-designed for 11x22. The checkbox and switch stand on
the window boxes' rows and width, so their rules are the windows' (ADR
0210): lines are 1 px; the box is the cell's width or the stripe band
less a line, whichever is smaller, and the switch's track is that high and
two cells wide.
- **Checkbox marks:** the marks are 2 px in from the outline in a box 7 px
  or more across, 1 px in below that; the Slate check and dash are 2 px
  thick from 9 px across, 1 px below. The check's lowest row sits just
  above the shade, and no row is above the first inside the box. At a 5
  px box the X, the dash and the check are still different. **At a 3 px
  box (a 4x8 cell) the interior is one pixel, so *on* and *mixed* draw the
  same single pixel;** both differ from *off*.
- **The switch's knob:** the track's height less a ring and a gap on each
  side (7 at 11 px); at under 3 px, the track less the ring alone, and no
  ring of its own.
- **The capsule scales with the cell's height:** every length is a 22nd of
  it, so the track is 18/22 of the height and the knob 14/22, at any size,
  and its width is the three cells'. At 4x8 the track is 6.5 px high and
  the knob 5 px. The edge pixels are the same rule, so it never turns into
  a rectangle with a dot: every size keeps its track, knob and mixed edge
  (checked at all 12 sizes).

### Pens Hibr sets

| Piece | Background | Foreground | SGR 58 |
| --- | --- | --- | --- |
| Chalk checkbox, switch | the panel | ink | unset |
| Slate checkbox | the panel | ink | the box's face (white in Pewter) |
| Slate switch | the panel | ink | the knob's face, and the track's highlight (white in Pewter) |
| Capsule | the panel | the track: green on, grey off | the knob (white) |

A switch's or capsule's cells all take the same three. The Slate shade and
the capsule's edge are the mean of the foreground and the panel, so they
follow any scheme.

**One consequence of the owner's choice:** in a dark scheme (a light
foreground on a dark panel) the mean is lighter than a dark face, so a
Slate checkbox's "shade" reads as a highlight edge. In Pewter it is darker
than the white face. A scheme that wants the shade darker sets the
checkbox face (SGR 58) lighter than the mean.

## Decision B: a catalogue a client can map dynamically

### Structured pen roles on every `cell_geometry` entry

All 92 entries (the two bar corners, the 70 window glyphs and the 20
controls) carry five fields, generated from the same geometry tables as the
glyphs (`draw-window-glyphs.py` and `draw-control-glyphs.py`; each has a
`--check` that fails if the catalogue is not what its tables give):

| Field | Words |
| --- | --- |
| `part` | `bar-corner`, `title-bar`, `note-close`, `frame`, `dialog-border`, `button-cap`, `checkbox`, `switch`, `capsule` |
| `cells` | `{span, position}`: the piece's width in cells (1 to 3), and this glyph's place in it |
| `background` | `bar`, `panel`, `wallpaper`, `dialog-face`, `button-face` |
| `foreground` | `ink`, `track`, `bar` |
| `line` | `none`, `highlight`, `box-face`, `dialog-face`, `button-face`, `knob` |

Alongside the existing `pens` (`foreground` or `cell`) and `set` (`chalk`,
`slate-light`, `slate`, `capsule`). The vocabulary is documented in
`assets/fonts/icons/README.md` and typed in OpenAPI as enums
(`CellGeometryGlyph`), and `catalogue_fields_gate` holds the three to one
another.
- **`set` for the controls is `slate`.** The window glyphs keep
  `slate-light` in their ids and `set`, because renaming them would break
  lookups made before Slate dark was retired (ADR 0211).
- The OpenAPI `FontIconCatalogue` schema did not describe `retired` or
  `cell_geometry` at all; it now types both, and the icons' `columns` and
  `column`.

### A revision a client can compare

`GET /v1/fonts/icons` returns `revision`: the SHA-256 of the catalogue file
as packaged, which is the hash `fonts.source.json` pins. It is the first
member of the body and the `ETag` (quoted). A request with `If-None-Match`
naming the current revision (or `*`) is answered 304 with the ETag.
- The server hashes the file at the time of the request, so the revision
  is always the file's, even if a package replaced the file without the
  generator running. There is no SHA-256 elsewhere in the server (updates
  use `sha256sum`), so `blit_sha256.h` adds one, tested against
  `sha256sum`.
- **`Cache-Control` is `no-cache`** for this endpoint (it was `no-store`
  for every response): the catalogue may be kept and must be asked about,
  which is what an ETag is for.
- The catalogue is 60.8 KB with the controls (46.9 KB before this
  release), 61 KB served. The API's cap is 256 KiB; the gate fails above
  255.

### Capability flags

`GET /v1/server` lists `sgr-framed`, `sgr-framed-bar` and `sgr-shadow` in
`capabilities`, so a client switches on a flag, not a version:
- `sgr-framed`: SGR 51, with Blit's 51:3 (thick) and 51:5 (a bottom rule),
  and 54 to end it (ADR 0210);
- `sgr-framed-bar`: 51:2, a Slate title bar's rows in the cell's pens, with
  51:4 its alias (ADR 0211);
- `sgr-shadow`: SGR 66 and 67, a drop shadow drawn under the cell's glyph
  (ADR 0211).

## Evidence

- **`control_glyph_gate`:**
  - The catalogue lists all 20 in order, with sets and pens; nothing is
    mapped at or past U+1002AE.
  - Every glyph equals the design pixel for pixel at 11x22 and at eleven
    other sizes, 4x8 to 32x64. Chalk is coverage; Slate and the capsule are
    role maps read from their planes, which the harness requires to be
    disjoint and together the one-colour lookup. Chalk has no planes.
  - The mock-ups' 11x22 pixels hold, written out in the gate: the square,
    the X, the dash, the shade, the check, the track and knob rows and
    positions, the capsule's extent (x 1–31, y 2–19), and the on knob the
    off knob mirrored.
  - The three states differ; a switch's and the capsule's cells join at
    every size; the capsule keeps its three pens and its knob inside its
    track, which stands on 2/22 to 20/22 of the height, at every size.
- **`control_style_gate`,** a real server, native and gles: all 20 glyphs
  with SGR 58 unset (white) and set, every pixel as the design says. The
  capsule in green and in grey, on a light and a dark panel: every pixel is
  the foreground, SGR 58, their mean or the background, and the same pixels
  are track, knob and edge in each; a few are written out from the geometry
  (the knob's centre, a track pixel clear of it, the corners).
- **`catalogue_fields_gate`:** all 92 entries have all five fields, from
  the vocabulary, coherent (multi-cell pieces are consecutive codepoints
  from position 0; a switch spans 2 and a capsule 3; only Chalk is
  one-colour); the vocabulary equals the OpenAPI enums and is documented
  and used; both generators' `--check` pass; the size and the pin.
- **`custom_icon_api_gate`:** the revision is the file's SHA-256 and the
  first member; the ETag is that, quoted; `Cache-Control: no-cache`; a
  current `If-None-Match` (quoted, weak, `*`, or in a list) is 304 with no
  body; a stale one gets the catalogue; and changing the catalogue changes
  the revision and the ETag and turns the old one into a 200.
- **`terminal_conformance`:** the server lists the three flags once each.
  **`api_schema_gate`:** the typed contract, the headers and the 304.
- **Against the 0.3.39 server and raster:**
  - `control_style_gate` fails: "U+10029A on row 1 pixel 3,3 is [255, 200,
    0], the design says [40, 80, 160]".
  - `control_glyph_gate` fails: "U+10029A is not a whole coverage cell at
    11x22".
  - `custom_icon_api_gate` fails: "the revision is not the SHA-256 of the
    catalogue file".
  - `terminal_conformance` fails: "server capabilities lack sgr-framed,
    sgr-framed-bar, sgr-shadow".
- **Renders:** the real server drew the checkboxes and switches in Chalk,
  Slate in Pewter and Slate in a dark scheme, and the capsule off and on,
  green and grey, light and dark, with 8x close-ups; and Pewter and the
  capsule at 8x16, 14x28 and 32x64.

## Considered and not done

- **A revision field inside the catalogue file.** The generator would
  hash its own output, and a package that replaced the file would carry a
  stale one. The server's hash of the bytes cannot drift.
- **Every font-size-independent box,** so the checkbox is always 11x11.
  The window boxes scale with the cell; the controls stand with them.
- **Anti-aliased alpha for the capsule.** Planes stay 0 or 255 and the
  smoothing is a pen (the mix), so the same path serves both compositors
  and any colour.
