# Original Blit icons

The canonical mapping is [catalogue.json](catalogue.json), also returned by
authenticated `GET /v1/fonts/icons`. The same SVG paths generate both Modern
and Retro; there is no backend-specific artwork or runtime SVG renderer.
Select either font with the existing `PUT /v1/fonts`; this does not make an
application select new private-use characters automatically.

| ID | Codepoint | Appearance |
| --- | --- | --- |
| blit | U+100000 | Cyan split beam / golden bolt |
| hibr-pen | U+100001 | Golden nib, turquoise body, blue cap |
| wifi-1 | U+100002 | One active signal bar |
| wifi-2 | U+100003 | Two active signal bars |
| wifi-3 | U+100004 | Three active signal bars |
| wifi-4 | U+100005 | Four active signal bars |
| bluetooth | U+100006 | Blue Bluetooth symbol |

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

`wifi-bars.svg` supplies one geometry; `signal_level` determines which bars
are active, avoiding four divergent drawings. Colour layers are generated
as COLR v0/CPAL inside the existing TTFs. Source files and original-art MIT
licence are hash-pinned in `../modern-fonts.source.json`; upstream derivative
font licences/notices remain in force. Bluetooth trademark rights are not
granted by the drawing's licence.

Tests: `ruby tools/tests/custom_icon_gate.rb` covers all57 supported heights
and both profiles, premultiplied colour, partial-alpha edges, four distinct
Wi-Fi rasters, themed pen, bounded cache, and the101-scalar versioned Hibr GL
snapshot in both one/two-cell allocations. Snapshot coverage is not coverage
of every Hibr application. `custom_icon_api_gate.rb` checks real local PTY
UTF-8 writes (whole and bytewise), one-cell private advances, the canonical
catalogue/missing/oversized errors, and PTY/API presented-pixel parity.
Native/SDL adapter tests include the seven originals at every supported size.
These are executable local checks, not physical visual acceptance.

[Hibr330](https://git.home.arpa/itdlabs/hibr/issues/330) tracks opt-in menu,
Wi-Fi and Bluetooth selection. No Hibr source was changed here. Until Hibr
selects the private glyphs, an existing Unicode/Nerd indicator is not evidence
that these colour originals are missing from the font.
