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
  the span and of the cell's height, centred on the span and on the cell.
  Each cell keeps its own slice, so the halves meet at the seam. The
  raster reads no catalogue.

## Consequences

- An application writes the parts side by side, in order: Hibr writes
  `${GL[huil]}${GL[huir]}`.
- Written alone, or on a terminal without Blit's icon font, each part is
  half a badge. Hibr uses them only in its Blit glyph set, keyed on
  `BLIT_VERSION` 0.2.40 or later.
- The badge is the size of a square in the cell, not cap height, so it
  reads as an emblem beside the menu titles. One-column icons keep their
  rule.

## Evidence (0.2.40)

- `custom_icon_gate`, in every text font at all 57 sizes:
  - each half is one cell, in colour, with antialiased edges;
  - the halves ink the same rows at the seam, give or take one;
  - the ring reaches equally far either side of the seam, within a pixel.
  Shifting a half by 2 px fails the gate (checked by mutation).
- `custom_icon_api_gate`: both halves through a real PTY, one column each,
  with identical pixels by PTY and API.
- Rendered through Blit at 16, 22 and 32 px and inspected: one badge, no
  seam.
