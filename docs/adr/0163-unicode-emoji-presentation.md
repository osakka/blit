# ADR0163: Unicode presentation data and real monochrome emoji outlines

- Date: 2026-10-20
- Status: accepted; offline implementation, live acceptance pending
- Tracking:107/110

Pin Unicode18.0 emoji-data and standardized variation sequences beside the
existing corpus. Generate presentation lookup data from those files, not
handwritten ranges or font cmap guesses. Default Emoji_Presentation selects
colour ahead of outline coverage; ordinary text/geometry/PUA policy remains.
Standardized VS16 pairs select colour; VS15 pairs select monochrome text.
Keep the caller's allocated columns unchanged.

Prefer the selected text outline for VS15; where absent, use pinned Noto Emoji
3.002 monochrome outlines (Google Fonts commit8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5,
default weight400). All371 variation bases exist in this face. It is a shared
fallback, not another selectable profile or raster engine. Use existing
FreeType coverage rendering at fitted cell sizes; do not derive silhouettes
from colour bitmaps. Check actual hinted ink bounds as well as advance;
reduce the fallback's em and re-rasterize when ink overhangs. Never clip it.
Colour-enabled contexts require the sibling text asset;
missing/invalid assets fail construction/transactional font changes.

One cache/colour compositor remains authoritative. Test all371 standard
selector pairs, all1228 default-colour scalars, both spans/profiles and
8..64px sizes; preserve normal combining overlays. This decision does not
claim arbitrary nonstandard selectors or general Unicode grapheme support.
No Hibr changes, live writes or package deployment during local verification.

Sources: https://www.unicode.org/Public/18.0.0/ucd/emoji/ and
https://github.com/google/fonts/tree/8b0a1d0f5983c89bc2b93f1b5fb55f9e252744b5/ofl/notoemoji .
