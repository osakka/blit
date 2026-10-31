# ADR 0156: smooth Retro and Modern share the pinned Nerd repertoire

- Status: superseded by ADR 0195
- Date: 2026-10-20
- Tracking: [107](https://git.home.arpa/itdlabs/blit/issues/107), emoji clusters [110](https://git.home.arpa/itdlabs/blit/issues/110)
- Colour-asset pin superseded by ADR 0157 in candidate 574; profile and shared-raster decisions unchanged.
- Supersedes ADR 0150's Font-Awesome-only repertoire restriction, not its shared rendering boundary.

## Decision

Add optional `blit-retro`, a renamed derivative of Nerd Fonts v3.4.0 3270 Mono,
for rounded, mainframe-inspired antialiased text. Expand existing `blit-modern`
(Hack Mono) to the same complete pinned catalogue: 10,386 unique codepoints.
This is a Blit-tailored derivative, not a claim to have designed a new typeface.
Keep the source Unicode repertoires, pinned licence notices and existing
Terminus/Spleen bitmap profiles. Do not change the installed/default selection.

One offline generator, `tools/fonts/build-outline-nerd.py`, checks source and
catalogue hashes and the exact fontTools version. Fit complete icon outlines
inside each face's cap height and 90% advance, retaining the readable U+270E
pencil alias. Preserve source timestamps and deterministically rename families.
Nerd icons receive terminal foreground colours; they are not multicolour fonts.
Powerline glyphs currently use this optical icon fit; seamless cell-edge
separator geometry is not established by catalogue coverage alone.

Both profiles use the existing shared FreeType raster, cache, PSF geometry
fallback and Noto Colour Emoji 2.042 fallback. Do not add another renderer,
font-size API, backend-specific font asset or Hibr change. Keep exact 8..64px
cell heights, not enlarged bitmap sampling or a second screen-scale policy.
The existing API inventory exposes the new profile to dynamic clients.

Colour-font updates are deliberately separate from this cycle. Scalar cmap
coverage is not sequence shaping: flags, ZWJ families, skin tones, keycaps and
VS15/VS16 colour selection remain issue 110. No "all emoji" promise. Protected
box/block and missing-glyph fallbacks remain bitmap and cannot be described
as entirely smooth universal Unicode.

## Verification

The catalogue gate passes every codepoint through the production raster at
every integer height 8..64 for both profiles: 592,002 cases each. Require native
outline resolution, unchanged cell allocation and visible ink; specifically
require `cod-blank` U+EC03 to remain empty. Separate existing raster gates check
partial alpha, colour premultiplication, exact geometry, cache and load failure.
Retain Modern's 18px bell em bound at 22px; Retro's reviewed metrics use 20px.

API tests select both faces at all 57 sizes and restore Retro 23px from canonical
saved settings after process restart. Missing colour assets fail transactionally.
Native-KMS/mock, SDL-software and offscreen GLES compare production composition
at all 57 sizes. These are offline adapter tests, not physical-display acceptance.
The specimen generator uses the actual shared raster/compositor, not a design
mockup; visual aesthetics still require operator review.
