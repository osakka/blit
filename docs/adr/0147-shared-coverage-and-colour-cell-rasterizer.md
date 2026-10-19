# ADR0147: shared coverage and colour cell rasterization

- Date: 2026-10-19
- Status: shared module implemented/tested; production adapters not integrated
- Tracking: [Blit#107](https://git.home.arpa/itdlabs/blit/issues/107)

## Context

Pixel size controls are implemented, but the deployed bitmap renderer still
stretches one-bit glyphs. Arbitrary resizing cannot produce native-size smooth
text. Another PSF profile does not fulfill the requested modern colour font.

## Decision

Use one FreeType-backed `BlitRaster` in `src/blit_raster.c`, with immutable
profile/size identity per object. Return cell-sized8-bit coverage or explicit
premultiplied RGBA, never a boolean mask for outline glyphs. The fallback PSF
must outlive the rasterizer. Face creation is transactional: construct a
replacement before releasing the prior context. This is a single-owner API.

Fit hinted ascent, descent and advance inside the exact requested cell, not
an assumed em height. Centre the baseline within spare vertical space. Do not
silently crop overhanging glyphs: use the canonical bitmap fallback. Protect
box drawing/block geometry from outline-font seams. Preserve an intentional
nearest-sampled PSF mode for pixel-art retro profiles.

Normal text selects the outline before a colour face, so digits do not become
emoji merely because a colour font covers them. A colour bitmap strike is
aspect-fitted and area-averaged in premultiplied space, then converted from
BGRA to RGBA once. Mask glyphs receive terminal foreground; intrinsic colour
does not. The module provides the canonical integer RGB source-over helper.

Bound the direct-mapped cache to128 entries and4MiB. Cache keys include scalar
and1/2-column span; profile and size belong to the owning context. Borrowed
results expire on the next lookup or close. Backend atlases must copy data
and invalidate on context replacement; no repeated FreeType work per frame.

This initial interface handles individual scalars, not shaped sequences.
Variation selectors, emoji sequences, combining placement and style-specific
faces require explicit integration before claiming comprehensive support.
Only scalable monospaced outline faces and fixed-strike BGRA colour faces are
accepted here. No SVG/COLRv1/general-font-format claim is made.

## Verification and remaining acceptance

`tools/tests/font_raster_gate.sh` compiles the real module with TCC. It checks
every integer cell height8..64, partial coverage, metrics, opaque full-block
coverage, intrinsic colour/premultiplication, exact blend vectors over black
and white, bounds, cache reuse/pressure and failed-load survival. Valgrind
checks memory errors/leaks. Inputs are explicit reviewed Hack/Noto assets;
provenance is in `docs/font-evolution-plan.md`.

The module is not linked into the server yet; no runtime dependency/default,
public profile or live configuration changes in this checkpoint. Next wire
canonical profile ownership and size changes, then native KMS and both SDL
adapters, including atlas alpha representation. Compare pixels, style/Unicode
fixtures, resize/hit geometry and caches before packaging and physical review.
Module success is not presentation acceptance.

## Primary references

- [FreeType glyph retrieval](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html)
- [FreeType sizing and scaling](https://freetype.org/freetype2/docs/reference/ft2-sizing_and_scaling.html)
