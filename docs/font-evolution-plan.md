# Retro and modern colour fonts

Tracking: [Blit#107](https://git.home.arpa/itdlabs/blit/issues/107).

## Confirmed requirements and current boundary

Deliver two additional selectable looks: retro and polished modern with
multicolour glyph support. Retain the current font. Prioritise font sizing,
then these profiles, then measured performance. Never modify Hibr.

Font size is not physical display resolution or desktop zoom. Existing
`PUT /v1/fonts` enlarges bitmap glyphs and terminal cells with scale1.0–3.0
in0.1 steps. It resizes PTYs and uses the same geometry for input. Saved
live scale1.2 reports10x19 cells from an8x16 font. It does not rasterize an
outline face at a requested pixel size. Size is currently global.

`BlitFont` and SDL's atlas currently expose one-bit coverage; terminal styles
colour those masks. Another PSF alone cannot add antialiasing or intrinsic
multicolour glyphs. Current BMP wide coverage is not universal Unicode or
emoji-sequence support.

## Implementation sequence

1. Specify native pixel-size selection through the canonical font API.
   Preserve legacy scale callers; reject requests with competing size
   controls. Store one size policy and return resolved cell/glyph metrics.
   Publish schema fields only with implementing handlers and tests.
2. Establish one bounded glyph interface for coverage masks and intrinsic
   colour, with bearings, baseline and advance. Keep PSF loading supported.
   All backends share fallback, width and rasterization policy; drawing
   adapters consume the same glyph data rather than implement text engines.
3. Add both profiles using pinned, licensed assets with explicit repertoires
   and reproducible generation. Review real samples before default changes.
   FreeType is a candidate, not yet a production dependency. Evaluate colour
   format and shaping requirements before selecting emoji assets.
4. Normalize alpha once. Compare exact colour/alpha results over black,
   white and nonuniform backgrounds on native KMS and both SDL paths.
   Terminal foreground tints masks, not intrinsically coloured emoji.
5. Cover combining marks, variation selectors and supported emoji sequences
   explicitly. A single-codepoint demo does not establish sequence support.
   Preserve column widths, box junctions, blocks and Hibr symbols. Document
   unsupported sequences instead of claiming every Unicode character.
6. Bound caches by font identity, native size, glyph/sequence and attributes.
   No unchanged glyph rasterization/upload per frame. Test invalidation on
   size/profile/renderer changes. Failed load/allocation must retain a usable
   prior configuration rather than leave a partially applied font.
7. Gate TCC builds, schema/API/persistence, native-size metrics, right-edge
   clipping, bold/combining/wide cells, colour/alpha and cache reuse. Verify
   split-pane resize and pointer hit agreement. Repeat on10.54 across all
   three backends, preserving backend/layout/login and obtaining visual
   approval before default changes.
8. Resume identical authenticated drag/wallpaper workloads, separating
   input/PTY, composition, uploads and presentation timing. Do not trade
   correctness for a backend-only font fast path.

## Primary references

- [FreeType glyph retrieval](https://freetype.org/freetype2/docs/reference/ft2-glyph_retrieval.html):
  colour loading can return premultiplied BGRA bitmaps.
- [FreeType colour layers](https://freetype.org/freetype2/docs/reference/ft2-layer_management.html):
  COLRv1 is not automatically rendered by the basic colour-load path.
- [Noto Emoji sources and font licenses](https://github.com/googlefonts/noto-emoji):
  candidate colour assets; pin exact format/version/license before shipping.

No new font dependency, asset or API field is delivered by this design note.
