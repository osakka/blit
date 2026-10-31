# ADR 0020: Reject unresolved fallback glyphs

- Status: accepted

## Context

The previous font manifest only rejected blank glyphs. A fallback font can
return a visible `.notdef` box for an unsupported scalar, so the manifest could
pass while Hibr's halfwidth Katakana, Arabic presentation forms, and symbols
were visibly wrong. This is especially damaging in a terminal because the
replacement box looks like valid UI content.

## Decision

Font generation uses an ordered build-time fallback set: the Terminus PSF
remains primary, followed by DejaVu Sans Mono, Droid Sans Fallback, Noto Arabic
families, Noto Symbols, and Noto Sans. A candidate bitmap is accepted only if
it is nonblank and differs from that font's `.notdef` bitmap. Arabic
presentation forms may fall back through Unicode NFKC to their canonical scalar
when the advertised presentation-form outline is blank. Four symbols absent
from the available monochrome fonts have explicit deterministic 8×16
pictograms. Unresolved required scalars fail generation.

The checked-in PSF remains the only runtime dependency; these fonts are
build-time inputs and are not installed or loaded by Blit at runtime.

## Verification

- The manifest gate checks exact box and special glyph bytes.
- Generation fails for unresolved or `.notdef` fallback output.
- Required glyph duplicate analysis must not contain the previous large
  `.notdef` alias group.
- Full terminal and SDL/KMS gates must pass after regeneration.
