# ADR0150: Modern Hibr Nerd coverage belongs in the font asset

- Date:2026-10-19
- Status: deployed in566; operator visual approval pending
- Tracking: Blit107; follows ADR0147

Hibr emits18 distinct Font Awesome private-use scalars for its Nerd glyph set,
while its start menu emits U+270E independently of that setting. Current Hack
does not outline-render these symbols; Modern falls back to pixel art.

Generate a renamed Blit Modern Nerd face from pinned Nerd Fonts3.4.0 Hack Mono
using fontTools4.60.1. Retain Hack Unicode and the1475 Font Awesome mappings,
not other icon packs with unrelated licensing/review boundaries. Preserve
the source notices and Fonticons attribution. Keep the original Hack source
only as a reproducible-build input. Modern has one runtime outline face.

Use the same pencil outline at U+270E and nf-fa-pencil U+F040. This is a
font-design mapping of equivalent pencil symbols, not terminal remapping,
PUA substitution of arbitrary text, or a Hibr code change. The source Mono
pencil has a negative sidebearing and can fail the rasterizer's no-clipping
check. Fit complete icon outlines to90% advance width and text cap-height,
preserving aspect ratio, centring and fixed advance; never crop them.
No special-case second icon renderer or backend-specific implementation.

Keep existing colour emoji and protected box/block geometry. Do not alter
global font size, cell widths, wallpaper or backend settings. The oversized
bell report is separate: this change does not resize colour emoji.

The new production-raster gate fails on the former Hack face at U+270E and
passes1140 Hibr/pencil samples across8..64px with no fallback, nonempty ink
and exact single-cell dimensions. Native and both SDL adapters additionally
exercise these outlines against the same raster. Reproducible font output
and licensed asset identities are required before packaging. This establishes
coverage and geometry, not subjective visual approval or all Nerd families.
