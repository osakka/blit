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
| wifi-1 | U+100002 | One active signal bar |
| wifi-2 | U+100003 | Two active signal bars |
| wifi-3 | U+100004 | Three active signal bars |
| wifi-4 | U+100005 | Four active signal bars |
| bluetooth | U+100006 | Blue Bluetooth symbol |
| battery-0 … battery-4 | U+100009–U+10000D | Upright battery, 0–4 teal segments: on battery |
| battery-0-charging … battery-4-charging | U+10000E–U+100012 | The same, green segments and a yellow bolt: charging |
| battery-ac | U+100013 | Full teal battery with a dark plug: on mains, not charging |
| volume-0 | U+100014 | Speaker with a red cross: muted |
| volume-1 … volume-3 | U+100015–U+100017 | Speaker with 1–3 active waves |
| hui-left, hui-right | U+100018, U+100019 | The letters "hui" in a teal ring: one badge, two columns |

The hui badge spans two columns as two codepoints, written side by side;
each draws its half of the one drawing in `hui.svg` (ADR 0197). Neither
is a wide character, so no width table changes.

U+100007 and U+100008 are the bar corners, drawn as cell geometry (see
`cell_geometry` in the catalogue).

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

`wifi-bars.svg`, `battery.svg`, `battery-charging.svg` and `volume.svg` each
supply one geometry; `signal_level` determines which parts are active
(`data-level` at or below it), avoiding divergent drawings per level. One
icon says the whole state: the charging bolt and the mains plug are inside
the battery, not a second glyph beside it. Colour layers are COLR v0/CPAL.
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
