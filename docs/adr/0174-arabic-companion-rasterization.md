# ADR0174: Arabic companion faces and cell-local mark positioning

Status: experimental raster/backend implementation; server profile activation
and remaining acceptance pending (2026-10-21).
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
bounded canvas and return ink offsets. No clipped letter is accepted. KMS
and SDL now consume offsets and expand damage/source regions in backend
fixtures. Companion attachment stays disabled in server profiles until the
remaining integration and acceptance work is complete.
Unsupported clusters fail rather than being advertised as supported.

### Backend implementation cycle

Carry bounded maximum canvas and ink margins with the attached font. SDL
atlas tiles must fit the canvas, use actual atlas dimensions for UVs, and
place the ink at its offset without stretching it back into the cell. KMS
uses the same offsets and a view/damage clip. Keep existing zero-margin
profiles on their existing fast paths.

For incremental updates distinguish the write rectangle from glyph sources:
expand dirty cells by the bounded old/new ink halo, restore every layer in
that write rectangle, then also sample neighbouring source cells whose ink
can enter it. Clip those source glyphs to the write rectangle. Simply widening
damage and drawing whole glyphs would reblend unchanged pixels or erase ink
from a neighbouring cell. Share this geometry between the adapters; do not
fall back to full-screen repainting as a substitute for damage correctness.
Respect pane boundaries. Require incremental-vs-clean-frame pixel comparison
and guard pixels before server profile selection is enabled.

SDL uses power-of-two atlas extents with16×8 bounded tiles for overhanging
contexts (up to32MiB/output at64px), keeping existing contexts at1024×1024.
An arbitrary416×208 atlas reproducibly dropped an ink pixel at8px in SDL
software; binary-exact UV boundaries fix that without changing tolerance.
Texture creation remains fallible; future profile activation must validate
the replacement on every output before swapping. No hardware limit override.
Block cursors repaint all intersecting source ink clipped to the cursor cell,
so adjacent marks survive and outside ink keeps its normal colour. SDL normal
and cursor repaint use one shared cell-ink dispatch.

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
