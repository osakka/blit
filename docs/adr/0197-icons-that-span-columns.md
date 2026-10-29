# ADR 0197: An icon that spans columns is one codepoint per column

- Status: accepted (2026-10-27; the owner's screen is the last check)
- Date: 2026-10-27
- Scope: `assets/fonts/icons` (catalogue and artwork), the icon generator,
  `blit_raster.c` (`draw_fitted`), the icon gates

## Context

The owner wants a "hui" mark, the letters in a circle, as an alternative to
the pen on Hibr's menu bar. A circle needs a square, and a cell is about
half as wide as it is tall, so the badge needs two columns. At 22 px a
one-cell badge would leave each letter about 4 px.

## Options considered

1. **One wide private-use character.** It is clean as a character, but
   cell widths come from a width table Blit generates from Hibr's, which is
   itself generated from the Unicode database. Both would need a Blit-only
   entry, in two repositories, for one logo.
2. **One codepoint per column,** each one cell wide, each drawing its slice
   of one badge. No width table changes, and the same mechanism serves any
   future wide icon, such as a large Files folder.

## Decision

Option 2.
- **Catalogue:** each part is its own entry, with `columns` (the span) and
  `column` (its place). The hui badge is `hui-left` at U+100018 and
  `hui-right` at U+100019, from `hui.svg`. The codepoints were agreed with
  Hibr.
- **Generator:** every part carries the whole outline, laid across the
  span and shifted left by its column. A multi-column icon must be wider
  than one icon (90% of an advance), or the build fails.
- **Raster:** for Blit's icon face, ink wider than 90% of an advance gives
  the span, and its centre gives the column. A spanning icon fills 95% of
  the span and 80% of the cell's height (from 0.2.41; 95% in 0.2.40),
  centred on the span and on half the cap height like any icon, and kept
  inside the cell.
  Each cell keeps its own slice, so the halves meet at the seam. The
  raster reads no catalogue.

## As built, revised (0.3.4)

The file icons (ADR 0201) broke the first version's two rules.
- **The span was read from the ink's width** ("wider than one icon"). A
  document page, narrow on its square canvas, would read as one column.
  The span is now the part's **advance**: the generator gives a part
  `600 × columns`, and the raster divides by the icon font's own column
  (the advance of glyph 0). The column is where the ink's centre falls, and
  the generator refuses a part whose centre isn't within its column's
  share.
- **The fit was to the ink,** so a tall page and a wide folder came out at
  different scales. Every spanning icon is now fitted by its **design box**,
  the icon font's cap height (700 units) at 80% of the cell. It is centred
  on half the text's cap height, and shrunk only if its ink would not fit
  the span. A family shares one scale and one line. The hui badge, whose
  ink fills its design box, is unchanged.

## Consequences

- An application writes the parts side by side, in order: Hibr writes
  `${GL[huil]}${GL[huir]}`.
- **The same drawing can come in both widths.** The large phones (0.3.0)
  are the one-column phone SVGs listed again with `columns: 2`, so there
  is one drawing per phone.
- Written alone, or on a terminal without Blit's icon font, each part is
  half a badge. Hibr uses them only in its Blit glyph set, keyed on
  `BLIT_VERSION` 0.2.40 or later.
- **Size:** the owner chose 80% of the cell's height (0.2.41) over 95%
  (0.2.40, too big beside the menu titles) and over cap height (13 px at
  22 px, with letters of about 4 px). It stands a little taller than the
  capitals, as text with its descenders does. One-column icons keep their
  rule.

## Evidence (0.2.40)

- `custom_icon_gate`, in every text font at all 57 sizes:
  - each half is one cell, in colour, with antialiased edges;
  - the halves ink the same rows at the seam, give or take one;
  - the ring reaches equally far either side of the seam, within a pixel;
  - the badge is at least 80% of the cell tall, so it is not clipped
    (0.2.41).
  Shifting a half by 2 px fails the gate (checked by mutation).
- `custom_icon_api_gate`: both halves through a real PTY, one column each,
  with identical pixels by PTY and API.
- Rendered through Blit at 16, 22 and 32 px and inspected: one badge, no
  seam.
