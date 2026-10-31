# ADR 0164: cell-fitted core Powerline separators

- Status: accepted
- Date: 2026-10-20
- Tracking: 107

Core Powerline U+E0B0..U+E0B3 joins require cell-edge geometry, not the
cap-height/90%-advance optical fitting used for standalone Nerd icons.
The new red fixture finds blank entire rows in U+E0B0 at 8px before this
change. Do not alter the font's global metrics or stretch all icons.

Generate these four coverage masks in the existing shared raster/cache;
never in individual backends. Solid triangles have an opaque full-height
base column; 8x8 subpixel coverage antialiases the slopes. Thin chevrons use
an inward horizontal stroke of ceil(cell_height/16) pixels. Left/right are
exact reflections, vertically symmetric at odd/even heights. The caller's
allocated cell rectangle remains authoritative, including two-column owners.
The existing foreground/background blend and bounded cache are unchanged.
Protected box/block PSF geometry and other Nerd outlines remain unchanged.
Legacy bitmap profiles without the shared raster are not changed by this ADR.

This is an original bounded geometry implementation, not copied terminal
source. Kitty's maintainer documents why font hinting/fitting alone cannot
guarantee connected cell geometry:
[Kitty discussion 7680](https://github.com/kovidgoyal/kitty/discussions/7680).

Test all 57 sizes, both spans/profiles, full solid-base opacity, thin endpoints,
mirror/vertical symmetry, antialiasing and cache reuse. Backend fixtures also
cover bold overstrike and exact outside-cell guards. This is not a claim that
all Powerline Extras, italic overhang or all font geometry is complete.
No Hibr edits, SSH, package or live deployment during this cycle.
