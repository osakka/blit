# ADR0174: Arabic companion faces and cell-local mark positioning

Status: experimental, not enabled in server profiles pending acceptance (2026-10-21).
Tracks Blit119 under118.

## Evidence and decision

The two Latin/Nerd faces lack42 audited Arabic-range scalars and ten Arabic-Indic
digits U+0660..0669. Legacy PSF substitution is not native Arabic typography.
Hibr's `uni vis` already emits display-order presentation forms, including a
single-cell lam-alef. Blit must preserve those forms and column allocations,
not run a second bidi algorithm or alter Hibr.

Evaluate separate, reproducibly derived Arabic companions: Kawkab Mono0.501
for Retro and GNU FreeMono20120503 for Modern. Source hashes, family names,
versions and alias-generation dependencies live in arabic-fonts.source.json.
Only absent presentation mappings that the source HarfBuzz shaper resolves
to one glyph are materialized offline. Existing mappings and positioning
tables are preserved. Cascadia Mono2407.24 was rejected: lam-alef shaping
returns multiple glyphs, incompatible with Hibr's one-cell presentation form.
Vazir Code1.1.2 fails the same contract. DejaVu Sans Mono2.37 lacks U+0670
superscript alef from the vocalized religious specimen and was rejected.
FreeMono covers all captured specimen scalars; that is not exhaustive Arabic
coverage. Its GPLv3+font embedding exception and preferred source accompany
the experimental derived face, separately from Kawkab's OFL1.1.

Use the existing shared raster/cache/compositor, with an explicit companion
attachment before first use. All glyphs in a face use one cell-size transform
and baseline; never shrink individual letters to hide clipped ink. The
nominal Arabic advance fits one terminal cell. Cell-local base+mark clusters
use HarfBuzz GPOS; disable contextual joining substitutions because the base
form is already supplied. This is not paragraph shaping or implicit bidi.
Ink extents are independent of terminal advance: measure first, allocate a
bounded canvas and return ink offsets. No clipped letter is accepted. Existing
backends do not yet consume nonzero offsets or expand damage for neighbouring
ink, so companion attachment stays disabled in server profiles. Enabling it
before both composition and damage handling are updated would be incorrect.
Unsupported clusters fail rather than being advertised as supported.

## Acceptance and limits

First implement an opt-in module fixture and inspect its real raster output.
Do not wire an unaccepted candidate into live/server profile selection.
Require native visible ink for base letters, presentation forms, digits and
representative vowel-mark clusters at every supported height, geometry/cache
checks, mark positioning against independently obtained HarfBuzz positions,
and visually inspected words. Joining continuity and religious/Quranic
coverage require explicit evidence, not a cmap count or generic emoji gate.
Separate local module, terminal/backend, and physical aesthetic acceptance.

Adding companion assets alone does not implement the feature. No package or
live claim until runtime selection, inventory and artifact verification agree.
Retain upstream licensing and acknowledge remaining coverage gaps.

## Alternatives and references

Merging Arabic into the Latin face would change Latin line metrics and risk
discarding Arabic positioning tables. Per-glyph fit breaks cursive continuity.
Blindly overlaying independent marks omits GPOS anchors. Replacing Hibr's text
order violates the existing terminal boundary.

- [HarfBuzz shaping operations](https://harfbuzz.github.io/shaping-operations.html)
- [HarfBuzz buffer properties](https://harfbuzz.github.io/setting-buffer-properties.html)
- [FreeType glyph retrieval](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html)
- [Kawkab Mono](https://github.com/aiaf/kawkab-mono)
- [GNU FreeFont Arabic](https://www.gnu.org/software/freefont/ranges/Arabic.html)
