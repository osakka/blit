# ADR 0145: Selectable retro face with canonical Hibr glyph fallback

- Status: implemented and deployed564; operator aesthetic review separate
- Date: 2026-10-19
- Scope: font assets and profile inventory; Blit#107

## Context

The operator requested both a retro and polished modern colour profile,
without removing the current font. A retro face can reuse the existing PSF
pipeline; inventing another renderer or silently dropping Hibr symbols would
violate the single implementation and coverage requirements.

## Decision

Add optional `blit-hibr-spleen16`, based on pinned Spleen2.2.0 8x16 BDF.
Keep default Terminus first in the profile table. A deterministic builder
merges the exact checked-in Hibr font as fallback. Preserve its box/block
geometry, combining masks and fitted control diamond; refuse changed source
hashes or unexpected BDF metrics. Record source/output/fallback hashes in
`assets/fonts/Blit-Hibr-Spleen16.source.json`. No host-font lookup at build
or runtime. Existing wide-font selection remains unchanged.

Retain Spleen's BSD notice and all fallback notices, including OFL material.
Distribute the combined font under OFL1.1 with component notices retained;
this does not change Blit's code license. Package the actual notices rather
than refer users to uninstalled system font packages. Package tests compare
the new artifact, source manifest and upstream notice with recorded Git source.

## Consequences and limits

The profile has1,575 scalar mappings, retaining1,181 unique mappings from the
existing font;92 printable ASCII masks differ. API selection and explicit
pixel sizing use the same resource/geometry/renderer paths as Terminus.
This is a bitmap face, not a colour font, outline renderer or universal
Unicode claim. Modern antialiased/colour glyphs and sequence support remain
separate pending work. The atlas sampling defect discovered by the new asset
is independently recorded in ADR0146 and must be fixed before shipping.

## Evidence

`retro_font_gate.py` checks output identity, all fallback mappings, protected
bitmaps/nonblank preservation and material ASCII differences. With
`BLIT_SPLEEN_SOURCE` set, it regenerates and compares exact bytes. The pinned
source passes. `sdl_glyph_render_gate.rb` passes the new face on software and
offscreen GLES2 after ADR0146. The expanded grid/font oracle checks115 cells
and14,720 exact pixels against the PSF on both local SDL paths. These are
local server tests, not physical or aesthetic approval. Full suite, package
reproducibility/provenance and physical API profile selection remain pending.

Full runner later reports108pass/7skip/0fail, but the enclosing fingerprint
guard rejects the run because installed Hibr changes257->258. This is not
frozen release acceptance. Repeat against an unchanged installation before
package/live testing. The three pinned-header adapter gates pass separately.
