# ADR 0173: retain visible coverage when shrinking native font fallbacks

- Status: accepted
- Date: 2026-10-21
- Scope: shared Modern/Retro raster fallback
- Tracking: [118](https://git.home.arpa/itdlabs/blit/issues/118)

## Evidence

Strengthening the 101-scalar UI gate from non-replacement to visible ink
revealed empty U+2500 at 8px; after a box-only preservation experiment,
U+2610 checkbox also disappeared. The old point sample misses source row 7
and narrow checkbox edges. A cmap mapping alone is insufficient acceptance.

## Decision

For a native outline profile's PSF fallback, when either cell dimension
shrinks, partition source pixel footprints among destination pixels and
average coverage into the existing grayscale mask. This preserves narrow
strokes/check boxes and shade density without adding another rasterizer.
Keep exact U+2580..U+259F fractional/quadrant block sampling unchanged,
except the density shades U+2591..U+2593. Enlargements and deliberately
bitmap-only contexts retain their existing sampling contract. Normal
outline and colour glyphs and the CJK wide-bitmap selection are unchanged.
The common raster is consumed unchanged by native and both SDL paths.

This is bounded integer source-footprint averaging, not subpixel LCD text,
universal script shaping, a new font-size/scale policy or a wallpaper fix.
The current Modern 22 layout/backend is not changed by this decision.

## Verification

The UI gate requires actual visible ink as well as no substitute for every
one of 101 scalars at 57 sizes, both 1/2-cell allocations and both profiles.
The module gate independently requires U+2500 ink, the known 127 coverage
row at 8px, and exact half-block rows including odd heights. Bitmap-only
coverage remains strictly 0/255. Adapter/reference and unchanged half-block
checks must pass before deployment. Earlier 65-gate results predate this
strengthening and must not be relabelled as a frozen final-artifact run.
