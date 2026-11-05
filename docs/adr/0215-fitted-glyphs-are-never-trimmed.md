# ADR 0215: A fitted glyph is never trimmed

- Status: accepted; in use since 0.3.41
- Date: 2026-11-05
- Scope: `blit_raster.c` (`draw_fitted`), `tools/tests/icon_clip_gate.rb`,
  `tools/tests/glyph_edge_sweep_gate.rb`
- Revises: ADR 0195 and ADR 0197 (the fitting step only)

## Context

The owner saw icons and buttons clipped on the right when the font size
grew, after 0.2.23 (centring), 0.3.4 (spanning icons by advance and design
box) and ADR 0197 had each "fixed" it. Each fix adjusted a prediction;
nothing swept the glyphs across sizes and checked the ink.

`draw_fitted` predicts a glyph's size from its unscaled outline, chooses a
scale, renders with hinting, then copied only the pixels inside the cell and
dropped the rest, silently. The sweep (`icon_clip_gate`, every catalogue icon
and the glyph classes the desktop draws, 8 to 64 px, the cell Blit derives
for each size and the owner's 11x22) found that the prediction and the render
disagree in three ways:

1. **A colour glyph is drawn from its COLR layers; the prediction used the
   base glyph.** For `wifi-1` the base outline is x 30..131 of 1000 units and
   the layers x 30..536. The bitmap landed about 40% past the right edge, at
   every size including 11x22. Wi-Fi 1-3 and volume 1-2 were affected.
2. **Hinting moves a glyph by a pixel** from where its outline says: one row
   at the top or bottom of accented capitals, U+25AF, and Noto symbols at any
   size; one column at the right of Nerd icons below 20 px, where the
   percentage margins leave under a pixel.
3. **The two halves of a spanning icon are hinted and phased separately**, so
   they disagree by a pixel in most icons at most sizes (a seam), though the
   icon's ink never leaves the span at the owner's sizes.

Not a cause, checked and refuted: the spanning icon's `fit * advance`
exceeding the cell. The placement centres the whole icon on its span whatever
that slack's sign, and the fit is capped by its ink's width, so a spanning
icon keeps 3 px or more per side at 40 px and 5 px at 64 px.

The window buttons, blocks, box lines and powerline are exact cell geometry,
not fitted; the raster and both compositors (CPU and GLES) draw them edge to
edge at every size 22 to 64 px (`glyph_edge_sweep_gate`).

## Decision

- The fitted box is the union of a colour glyph's layers when it is drawn in
  colour, the glyph's outline otherwise.
- A spanning icon is rendered unhinted, at a scale where one advance is a
  whole number of 26.6 steps and from an origin on that grid, so every part
  has the same sub-pixel phase and the parts join to the pixel. Every other
  fitted glyph gets light hinting (vertical only), which leaves its
  horizontal extent where its outline says and changes its look least.
- The bitmap is measured after the render. One that does not fit its cell
  (for a spanning part, the span's outer edges) is fitted again smaller, up to
  three times; one that fits but sits askew is slid in by whole pixels (not a
  spanning part); one that still does not fit is declined, so the caller shows
  the visible fallback, as `draw_outline_face` does. A glyph is never trimmed
  silently.
- The percentage margins stay: unhinted or lightly hinted, they leave the
  ink inside the cell, and they keep the look at the owner's size.

## Consequences

- Wi-Fi and volume icons now show their whole drawing, fitted to the cell.
- Most fitted icons change slightly in anti-aliasing at every size (a pixel's
  edge weight), the light hinting least; the before and after sheets in the
  release notes show it.
- A future glyph that cannot be fitted shows the fallback instead of a cut.
- A setting for hinting of the text outline (`none`, `light`, `full`) would
  not affect fitted icons, which are fixed by this decision.

## Evidence

- `icon_clip_gate`: every catalogue icon, the Hibr UI glyphs, Nerd, Noto and
  Unifont samples and Hack's overhanging arrows, at 8 to 64 px, in two text
  fonts and a pixel font by default (all seven with
  `BLIT_ICON_CLIP_FONTS=all`), none trimmed, none falling back, and the two
  halves of every spanning icon identical to the pixel one cell apart. It
  failed on 0.3.40 (about 16,000 glyph-sizes at the default sample) and
  passes now.
- `glyph_edge_sweep_gate`: the real server with the CPU and the GLES
  compositor at 22, 24, 32, 40, 48, 56 and 64 px; a scene of blocks, box
  lines, powerline, the Chalk and Slate boxes and the button dots across the
  pane; every cell's pixels match the raster's own coverage, 4.58 million
  pixels.
