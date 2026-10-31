# ADR 0171: native font coverage precedes the wide bitmap

- Status: accepted
- Date: 2026-10-21
- Tracking: [118](https://git.home.arpa/itdlabs/blit/issues/118)
- Refines ADR 0062; preserves ADR 0163 emoji presentation.

## Evidence and decision

The existing native/SDL raster gates called the primary raster directly and
did not supply the production wide font. With the real Unifont loaded, a
two-cell U+2615 coffee symbol takes the bitmap path even when the selected
Modern profile produces intrinsic-colour pixels. The expanded native gate
fails before the correction. This is a concrete colour loss, not proof that
every operator-reported missing icon has the same cause.

Use one `blit_font_cell_source` rule in native KMS and SDL's normal/cursor
selection: when a scalar has a non-fallback native raster, retain the selected
font. Only otherwise choose the existing wide bitmap if it maps that scalar.
Bitmap-only profiles and unsupported CJK retain the wide-font behaviour.
The rule uses the existing bounded raster cache; no new face, width policy,
presentation preference, API setting or renderer is introduced.

## Verification and limits

Production native composition and SDL software/offscreen GLES gates include
the real wide font, U+2615 and the primary outline/colour reference at all
57 cell heights. SDL selection additionally checks that unsupported U+4E2D
still chooses Unifont. Native pixels are exact; existing SDL tolerances remain.
Tests must name their actual backend/profile and must not be described as
physical acceptance. Literal upstream question marks, unknown characters and
missing branded artwork are separate investigation/delivery items in 118.
