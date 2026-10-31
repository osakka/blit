# ADR 0185: outline glyphs are centred at sub-pixel precision; shapes get the icon fit

- Status: accepted
- Date: 2026-10-25
- Scope: shared raster (`draw_outline_face`), outline font generator
- Amends: ADR 0150 and 0156 (the icon fit's scope)

## Context

With Modern at 32 px, the owner saw Hibr's window-button dot `●` and Nerd
icons as "clipped on the right". The font was not the cause. Blit's own
raster showed two compounding faults.

**Placement.** `draw_outline_face` centred `ceil(advance)` in the cell. The
outline advance is fractional: 15.66 px in a 16 px cell at em 26, and
10.84 px in an 11 px cell at em 18. The integer centring is therefore 0,
and all the slack lands on the right. Measured in blit's raster at 16×32:
- `●` had a dense left edge column (`.+#%%#+.`) and a faint right one
  (`:=++=:`);
- the home icon U+F015 had a soft left edge and a hard right edge.

On the live screen, the red button's right edge column was 8 px tall
against 6 px on the left.

**Shape fit.** ADR 0150/0156 fit the Nerd icons to 90% of the advance and to
cap height; their ink measures 30..572 of 602. Hack's Geometric Shapes block
is outside the catalogue and was drawn edge to edge (2..600 of 602, sitting
below the cell's middle). Hibr uses that block for its standalone symbols:
- the window buttons (`●`, `■`, `□`, `◉`);
- the scroll marks (`▲`, `▼`, `▶`);
- the folder, disk and trash marks (`▤`, `▥`, `▽`, `▼`);
- the diamond `◆`.

## Options considered

1. **Snap symbol ink to a whole number of pixels in the raster.** This
   needs a symbol-specific path, plus a second, runtime list of what counts
   as a symbol. ADR 0150 rules both out.
2. **Fit the shapes in the font only.** The dot would then match the icons,
   and the icons showed the same one-sided placement.
3. **Centre the unrounded advance in the raster, and fit the shapes in the
   font generator.** This is the chosen option.

## Decision

**Raster.** `draw_outline_face` takes the unhinted advance from
`FT_Get_Advance` (16.16). It centres that advance in the cell at 26.6
precision and applies the fraction as a horizontal `FT_Set_Transform`
delta before scan conversion, the same way `draw_arabic` already places
glyphs. The vertical delta is 0, so vertical hinting is unchanged.

This applies to every outline glyph, letters included, because there is
one placement rule. Letters move by half the slack: under 0.1 px at 22 px
and 0.17 px at 32 px. A letter's two stems now match, where the left one
used to be the sharper.

The never-clip check is unchanged for glyphs with ink. A blank glyph (the
space, or Codicons' `cod-blank` U+EC03) has nothing to clip. At some large
sizes its advance is a fraction wider than the cell, which puts its origin
at -1/64 px. It is therefore drawn as blank and never sent to the bitmap
fallback: the catalogue and Arabic gates caught that regression.

**Font.** `tools/fonts/build-outline-nerd.py` applies the existing icon fit
(90% advance, cap height, centred) to the Geometric Shapes block
U+25A0..U+25FF as well. The exception is the corner triangles U+25E2..25E5,
U+25F8..25FA and U+25FF: these are cell-corner geometry (Hibr's resize grip
`◢`) and keep touching their edges.

The rule is the same for both profiles:
- **Modern:** 88 glyphs change.
- **Retro:** 27 glyphs change. Its shapes already had side margins, so
  most of them are only recentred on cap height.

Only those glyphs differ from the previous build. The cmap is unchanged.

## Consequences

- At 16×32 and 11×22, the dot and the icons have equal left and right edge
  columns.
- The shapes sit centred on cap height, beside the icons. Hack's had sat
  below the cell's middle.
- The pinned font hashes change in `modern-fonts.source.json` and
  `docs/fonts/coverage.json`. Both faces rebuild byte for byte from the
  pinned sources with fontTools 4.60.1.

## Evidence

- Blit's own raster, dumped at 16×32 and 11×22 before and after:
  `U+25CF`, `U+F015` and `M` have symmetric edge columns afterwards.
- The font, raster and catalogue gates, and the native and GLES suites, in
  the 0.2.23 entry of `docs/verification-status.md`.
- On the host: the live red button's left and right edge columns are
  re-measured after the deploy.
