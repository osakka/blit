# Original Blit icons

Application authors: see the [full font/glyph developer reference](../../../docs/font-reference.md)
and [generated special-additions table](../../../docs/fonts/special.md) for
category browsing, valid JSON strings, capability discovery and fallbacks.

The canonical mapping is [catalogue.json](catalogue.json), also returned by
authenticated `GET /v1/fonts/icons`. The SVG paths are built once into
`BlitIcons-Regular.ttf` (`tools/fonts/build-blit-icons.py`), which the glyph
stack draws beside every text font (ADR 0195); there is no runtime SVG
renderer. An application still has to write these private-use characters
itself.

| ID | Codepoint | Appearance |
| --- | --- | --- |
| blit | U+100000 | Cyan split beam / golden bolt |
| hibr-pen | U+100001 | Golden nib, turquoise body, blue cap |
| wifi-1 | U+100002 | One active signal bar. **Deprecated (0.3.55):** the bars are the mobile signal; use `cellular-1` |
| wifi-2 | U+100003 | Two active signal bars. **Deprecated (0.3.55):** the bars are the mobile signal; use `cellular-2` |
| wifi-3 | U+100004 | Three active signal bars. **Deprecated (0.3.55):** the bars are the mobile signal; use `cellular-3` |
| wifi-4 | U+100005 | Four active signal bars. **Deprecated (0.3.55):** the bars are the mobile signal; use `cellular-4` |
| bluetooth | U+100006 | Blue Bluetooth symbol |
| battery-0 … battery-4 | U+100009–U+10000D | Upright battery, 0–4 teal segments: on battery |
| battery-0-charging … battery-4-charging | U+10000E–U+100012 | The same, green segments and a yellow bolt: charging |
| battery-ac | U+100013 | Full teal battery with a dark plug: on mains, not charging |
| volume-0 | U+100014 | Speaker with a red cross: muted |
| volume-1 … volume-3 | U+100015–U+100017 | Speaker with 1–3 active waves |
| hui-left, hui-right | U+100018, U+100019 | The letters "hui" in white on a solid teal disc: one badge, two columns (0.3.51; a ring before) |
| power | U+10001A | The power symbol: a teal open ring and a light bar |
| phone-off | U+10001B | Telephone handset, grey outline: the line is off |
| phone-wait | U+10001C | Telephone handset, amber dashed outline: connecting |
| phone-on | U+10001D | Telephone handset, filled teal: on, with a number |
| phone-ring | U+10001E | Filled handset with gold ring waves: a call |
| phone-classic-off … phone-classic-ring | U+10001F–U+100022 | A classic rotary desk phone in the same four states: outline, dashed, filled, filled with ring waves |
| — | U+100023–U+100032 | **Retired** (0.3.5): the large two-column phones, which the owner dropped. Listed under `retired` in the catalogue; never reused, so an old client can't show something else there |
**Since 0.3.8 only folders have colourways** (ADR 0201). Every other file
kind has one look in its own natural colours, in its classic slot. **Since
0.3.9** the blue, green and amber slots of those kinds are retired (listed
under `retired` in the catalogue, never reused); folders (folder, share,
folder-*) keep all four. The ranges below give each kind's eight
codepoints, of which a non-folder kind now uses only the classic two.

| file-<kind>-<colour>-left, -right | U+100033–U+100092 | File-manager icons, two columns wide, in a System 7 spirit: folder, document, text, pdf, image, audio, video, archive, code, application, disk, trash, in classic, blue, green and amber; drawn by `tools/fonts/draw-file-icons.py` (ADR 0201) |
| file-home-<colour>-left, -right | U+100093–U+10009A | Home, the first kind added after the twelve: a house with a roof in the primary colour; classic, blue, green, amber |
| file-<kind>-<colour>-left, -right | U+10009B–U+1000F2 | Eleven more kinds, eight codepoints each in this order: boxer (a Boxer game without its cover: two boxing gloves), markdown, spreadsheet, diagram, presentation, font, web, terminal, settings, bundle (a Hibr bundle), link |
| file-image-jpg/png/gif-left, -right | U+10022B–U+100230 | The picture with its format on a label (JPG orange, PNG blue, GIF violet); one look each (0.3.8) |
| file-<kind>-<colour>-left, -right | U+1000F3–U+10022A | Thirty-nine more, eight codepoints each in this order: special folders with a badge (folder-git, -config, -local, -system, -desktop, -downloads, -documents, -music, -pictures, -videos, -apps, -games), then rc, history, service, key, network, cron, readme, license, log, kernel, boot, c, asm, makefile, patch, json, vector, rom, ebook, exe, disc, package, library, lock, database, usb, share |
| hui-mark-left, hui-mark-right | U+1002C5, U+1002C6 | The hui mark (0.3.51): the monogram H U i in teal, no disc: a full left stem, a crossbar, the lower half joined as a U, a dot for the i; one mark, two columns. U+100231–1002C4 between are window and control glyphs (`cell_geometry`) |
| wiretap-left, wiretap-right | U+1002C7, U+1002C8 | wiretap on (0.3.52), Hibr's network monitor and firewall: a water tap in water blue, side on: a wall stub, the body with its spout curving down, a T handle, and a drop falling from the spout; one icon, two columns |
| wiretap-off-left, wiretap-off-right | U+1002C9, U+1002CA | wiretap off (0.3.52): the same tap with no drop; one icon, two columns |
| wiretap-1, wiretap-1-off | U+1002CB, U+1002CC | wiretap on and off in one column (0.3.53): a bolder tap in the same water blue, the body with its spout curving down and a T handle, with a drop under the spout (on) or without it (off); one drawing, `wiretap-1.svg`, the drop at `data-level` 1 (`signal_level` 1 on, 0 off) |
| wifi-signal-1 … wifi-signal-4 | U+1002D1–U+1002D4 | Wi-Fi (0.3.55): a dot and three concentric arcs in a 90-degree fan opening upward, teal (#41d6bf) up to the level, the rest in the status icons' grey (#7d8796 since 0.3.56): 1 the dot, 2 with the inner arc, 3 with the middle arc, 4 all; one drawing, `wifi-signal.svg` |
| wifi-off | U+1002D5 | Wi-Fi off (0.3.55): the fan all inactive and a red (#f56565) cross at the lower right; `wifi-off.svg`, the same fan. Its monochrome shape is the cross alone |
| cellular-1 … cellular-4 | U+1002D6–U+1002D9 | The mobile signal (0.3.55): 1–4 active bars, `wifi-bars.svg`, the drawing `wifi-1`…`wifi-4` show; the new names of those deprecated ids |
| wifi-signal-wide-1-left, -right … wifi-signal-wide-4-left, -right | U+1002DA–U+1002E1 | Wi-Fi in two columns (0.3.56): the same fan, `wifi-signal.svg` at levels 1–4, drawn across two cells in the square box the two-column icons share, about twice the one-column size; one icon, two columns |
| wifi-off-wide-left, wifi-off-wide-right | U+1002E2, U+1002E3 | Wi-Fi off in two columns (0.3.56): `wifi-off.svg`, the fan unlit and the red cross at the lower right. Its monochrome shape is the cross alone, so in the icon pen the left column is blank |
| volume-wide-0-left, -right … volume-wide-3-left, -right | U+1002E4–U+1002EB | Volume in two columns (0.3.56): 0 is muted, `volume-muted.svg` (the speaker and a red cross), 1–3 `volume.svg` with that many waves lit; one icon, two columns |
| phone-classic-wide-off-left, -right … phone-classic-wide-ring-left, -right | U+1002EC–U+1002F3 | The classic desk telephone in two columns (0.3.57), in its four states off, wait, on and ring: `phone-classic-off.svg`, `-wait.svg`, `-on.svg` and `-ring.svg`, the one-column drawings, about twice the size; one icon, two columns |

The hui badge, the hui mark, the wiretap icons and the two-column Wi-Fi,
volume and classic phone icons each span two columns as two codepoints,
written side by side; each draws its half of the one drawing in `hui.svg`,
`hui-mark.svg`, `wiretap.svg`, `wiretap-off.svg`, `wifi-signal.svg`,
`wifi-off.svg`, `volume.svg`, `volume-muted.svg` or `phone-classic-*.svg`
(ADR 0197). The wide Wi-Fi, volume and classic phone icons are the
one-column drawings, unchanged, at a level or state and two columns:
a spanning icon is fitted by its design box, not its ink, so every level
sits in the same place.
None is a wide character, so no width table changes. `wiretap-1` and
`wiretap-1-off` are one column each, like the battery and phone icons.

U+100007 and U+100008 are the bar corners, drawn as cell geometry (see
`cell_geometry` in the catalogue).

U+100231–U+100276 are the window-chrome glyph sets, also cell geometry (ADR
0210). There are two sets of 35 in one order: Chalk (one colour, the
cell's foreground) and Slate (the cell's foreground, underline colour and
a mix of foreground and background, ADR 0211). U+100277–U+100299, once
Slate dark, are retired and never reused. They
draw title bars, boxes, window and notification frames, dialog borders
and button ends. Blit draws them from the geometry table in
`src/blit_window_glyphs.h`; `tools/fonts/draw-window-glyphs.py` is the
design, writes their catalogue entries, and is what `window_glyph_gate`
holds the raster to.

U+10029A–U+1002AD are the controls (ADR 0212), also cell geometry, 20
glyphs in this order: a checkbox in Chalk then Slate (off, on, mixed), a
compact switch two cells wide in Chalk then Slate (off-left, off-right,
on-left, on-right), and a capsule switch three cells wide (off-left,
off-mid, off-right, on-left, on-mid, on-right). A multi-cell piece is
written side by side, one codepoint per cell. The capsule has no colour of
its own: its track is the foreground (green when on, grey when off, as the
application chooses), its knob the underline colour, its smoothed edge the
mean of the foreground and the background. Blit draws them from
`src/blit_control_glyphs.h` and the window table;
`tools/fonts/draw-control-glyphs.py` is the design and writes their
entries, and `control_glyph_gate` holds the raster to it.

U+1002AE–U+1002BA are the outer-edge frame (ADR 0214), also cell geometry,
thirteen glyphs: the one frame set, `frame-left`, `frame-right`,
`frame-bottom-left`, `frame-bottom`, `frame-bottom-right` (U+1002AE–1002B2),
and the title bar's end cells, `chalk-title-end-left`, `-right` and
`slate-light-title-end-left`, `-right` (U+1002B3–1002B6), then the same
four without stripes for an inactive bar, `chalk-title-end-plain-left`,
`-right` and `slate-light-title-end-plain-left`, `-right` (U+1002B7–1002BA,
0.3.45). A window has one
frame, and its line sits on the outermost edge of each frame cell: the left
cell's left edge, the right cell's right edge, the bottom row's bottom edge
(the bottom corners have two lines). Nothing else in the cell is drawn, so
the cell's own background is the padding: set the frame cells' background to
the window's face. The frame cells are coverage in the foreground, the same
for Chalk and Slate windows. The right-hand cell of the title row is
`*-title-end-right`, not `frame-right`. A title-end cell is the title bar's
end: the outer line, the bar's top line and rule (Slate: its bevel rows
too) and the stripes, which run into the cell and stop (or start) 2 px from
its outer edge, so they join the next cell's. Chalk's are coverage; Slate's
use the cell's pens as `slate-light-stripe` does (background the bar,
foreground the ink, SGR 58 the highlight). A drop shadow (SGR 66) goes in
the cells beyond the frame cells, with inset 0. `tools/fonts/draw-window-glyphs.py`
is the design and writes their entries; `window_glyph_gate` holds the raster
to it. The old frame glyphs and `stripe-first`/`stripe-last` of the window
sets are deprecated by these (see `deprecated` below) and still drawn.

U+1002BB–U+1002C4 are the pressed boxes and the grow box (ADR 0219), also
cell geometry, ten glyphs, five a set, Chalk's then Slate's: the title bar's
`close`, `shade` and `zoom` boxes pressed (`chalk-close-pressed`, ...,
`slate-light-zoom-pressed`) and the window's resize corner, `grow` and
`grow-pressed`. Hover draws the pressed glyph. A pressed Chalk box is solid
with its mark cleared out; a pressed Slate box is the mix pen with an inverted
bevel and the mark in the foreground. The grow cell is the window's
bottom-right frame cell (use it in place of `frame-bottom-right`): the frame's
right and bottom lines are the box's own edges, with a `/` hatch inside.
`tools/fonts/draw-window-glyphs.py` is the design and `window_glyph_gate`
holds the raster to it.

U+1002CD–U+1002D0 are the minimise box (ADR 0222), cell geometry beside the
pressed boxes: `chalk-minimise`, `chalk-minimise-pressed`,
`slate-light-minimise`, `slate-light-minimise-pressed`. The mark is a short
bar along the bottom of the box; pressed is drawn as ADR 0219's pressed
boxes are. U+1002D1–U+1002D9 are the Wi-Fi signal, Wi-Fi off and cellular
icons, U+1002DA–U+1002EB the two-column Wi-Fi and volume icons,
U+1002EC–U+1002F3 the two-column classic phones, and U+1002F4 is the next
free codepoint.

### What an application sets for a cell-geometry glyph

Every `cell_geometry` entry carries, besides its `id`, `codepoint`, `colour`
(always false), `shape`, and for the window and control sets `set` and
`pens`, fields an application reads to set the cell's pens from data. They
are generated from the same geometry tables as the glyphs, and
`catalogue_fields_gate` checks them against these words and the OpenAPI
schema (`CellGeometryGlyph`):

| Field | Values and meaning |
| --- | --- |
| `pens` | `foreground`: drawn in the cell's foreground alone (Chalk). `cell`: drawn in its foreground, underline colour (SGR 58; white if unset) and the mean of foreground and background (Slate, the capsule). The bar corners have neither this nor `set`. |
| `set` | `chalk`, `slate-light` (the window glyphs; the id prefix and set keep the name from before Slate dark was retired), `slate` (Slate's controls), `capsule`, `frame` (the one frame set, drawn in the foreground alone like Chalk, ADR 0214). |
| `part` | What the glyph is a piece of: `bar-corner`, `title-bar` (stripes, title, close, shade, zoom), `note-close`, `frame`, `dialog-border`, `button-cap`, `checkbox`, `switch`, `capsule`. |
| `cells` | `{span, position}`: how many cells the whole piece spans (1 to 3), written side by side, and this glyph's place in it from 0. |
| `background` | What the cell's background should be: `bar` (the title bar's colour), `panel` (what a control or a note sits on, and the window's face, which a frame cell's background is), `wallpaper` (the default background), `dialog-face`, `button-face`. |
| `foreground` | `ink` (lines and outlines), `track` (the capsule's track, in the colour the application chooses), `bar` (the corners' fill: the bar's colour). |
| `line` | What SGR 58 is: `none` (Chalk, frames, corners), `highlight` (the bar's highlight, and a window box's face), `box-face` (a note's close box or a checkbox), `dialog-face`, `button-face`, `knob` (a switch's or the capsule's knob, and a switch track's highlight). Unset, it is white. |
| `deprecated` | `true`, present only on a glyph that a later one replaces (ADR 0214): Blit still draws it, unchanged, until a release retires it. An application moves to `replaced_by`. |
| `replaced_by` | With `deprecated`: the ids that replace it, each an entry in this section that is not itself deprecated. The old frame glyphs (`chalk-` and `slate-light-` `frame-left`, `frame-right-top`, `frame-right`, `frame-bottom-left`, `frame-bottom-first`, `frame-bottom`, `frame-bottom-right`) and `stripe-first`/`stripe-last` are deprecated: the one frame set takes the body and bottom rows, the style's `title-end` glyphs the title row. |

`GET /v1/fonts/icons` also returns `revision`, the SHA-256 of the catalogue
file as packaged (the hash `fonts.source.json` pins), and sends it as the
`ETag`. A client that keeps the catalogue compares the revision, or sends it
in `If-None-Match` and is answered 304 while it is current.

These explicitly selected colour glyphs occupy one terminal cell. Plane16
private-use mappings are a Blit contract, not Unicode or Nerd assignments;
other fonts may assign them differently. A JSON string can use the character
itself or surrogate pairs (for example U+100000 is `"\uDBC0\uDC00"`). Do not
encode a supplementary scalar as a six-digit JSON `\u` escape.

U+270E is a monochrome/themed alias of the original pen silhouette. It retains
writing-pencil semantics; it is not the intrinsic-colour glyph. Standard
U+16D2 Berkanan and U+21CC harpoons have text outlines added only where absent
from the upstream profile. The former keeps its runic B shape; the separate
Bluetooth artwork does not redefine that Unicode character. Nerd glyphs and
Unicode emoji retain their existing meanings/presentation rules.

`wifi-bars.svg`, `wifi-signal.svg`, `battery.svg`, `battery-charging.svg`, `volume.svg` and `wiretap-1.svg` each
supply one geometry; `signal_level` determines which parts are active
(`data-level` at or below it), avoiding divergent drawings per level. An
inactive part is drawn in its `data-inactive-fill`; `#rrggbbaa` with alpha
00 draws it nowhere yet keeps it in the icon's box, so `wiretap-1-off` sits
exactly where `wiretap-1` does, its drop clear (a one-column icon is fitted
by its box). `wifi-signal.svg` carries `wifi-off`'s cross the same way, at
`data-level` 5 (never lit) and `#f5656500`, so the fan sits in one box in all
five states. One
icon says the whole state: the charging bolt and the mains plug are inside
the battery, not a second glyph beside it. Colour layers are COLR v0/CPAL.
Each icon has a monochrome shape too, for the icon pen (SGR 68, ADR 0220, 0221),
decided when the font is built, from its SVG: a path whose `data-level` is
above the icon's `signal_level` is left out (so a battery shows its bands, Wi-Fi
and volume their active bars and waves); `data-mono="knockout"` on a path cuts
its shapes out of the rest (the charging bolt, the mains plug, the hui
letters); `data-mono="disc"` draws a path's first subpath only, so a ring
is its solid outer shape (the hui disc, solid in colour too since 0.3.51).
An icon with none of these has no extra glyph: its colour coverage is its
shape (the hui mark, the two-column wiretap icons: on and off differ by the drop,
held apart by `font_icon_shape_gate`). The shapes live in the icon font as `<glyph>.mono`.
Source files and the original-art MIT licence are hash-pinned in
`../fonts.source.json`. Bluetooth trademark rights are not
granted by the drawing's licence.

Tests: `ruby tools/tests/custom_icon_gate.rb` covers all57 supported heights
in every shipped text font, premultiplied colour, partial-alpha edges,
distinct rasters for every Wi-Fi, battery and volume level, themed pen, bounded cache, and the101-scalar versioned Hibr GL
snapshot in both one/two-cell allocations. Snapshot coverage is not coverage
of every Hibr application. `custom_icon_api_gate.rb` checks real local PTY
UTF-8 writes (whole and bytewise), one-cell private advances, the canonical
catalogue/missing/oversized errors, and PTY/API presented-pixel parity.
The KMS raster gate includes every original at every supported size.
These are executable local checks, not physical visual acceptance.

Internal Hibr tracking330 covers opt-in menu,
Wi-Fi and Bluetooth selection. No Hibr source was changed here. Until Hibr
selects the private glyphs, an existing Unicode/Nerd indicator is not evidence
that these colour originals are missing from the font.
