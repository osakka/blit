# Retro and modern colour fonts

Tracking: [Blit#107](https://git.home.arpa/itdlabs/blit/issues/107).

## Current implementation: candidate0.1.562

Both new profiles are integrated: Spleen retro (`blit-hibr-spleen16`) and
smooth Hack/Noto colour (`blit-modern`). Original Terminus remains default.
The shared FreeType rasterizer is linked into the server and consumed by
native KMS, SDL software and SDL/OpenGLES2. Exact cell sizing, per-context
bounded caching, premultiplied-alpha handling and transactional profile/size
replacement are implemented. Production adapters pass399 glyph samples each
across all57 sizes8..64; native KMS is exact, SDL allows2 levels per channel.
SDL atlas-collision tests pass. API tests pass all sizes, size-policy retention,
canonical save/restart and missing-font rollback. Actual installed Hibr native
Control Panel slider/+1/-1 tests pass with modern rasterization selected;
captures live in `build/font-modern-panel-evidence/`. No Hibr changes.

FreeType is now a production dependency (Debian libfreetype6>=2.12.1).
Pinned source fonts and complete license notices are checked in; asset gates
validate their hashes. Build headers are explicit, not downloaded silently.
`BLIT_FONT_ROOT` relocates only bundled profile paths for isolated tests;
normal installations use `/usr/lib/blit/fonts`.

The release gate remains full frozen regression, reproducible package/identity,
API deployment, physical capture/metrics and operator aesthetic review. Live
is still561 until that gate completes. Neither a module test nor a local
offscreen GL test is physical scanout approval. Protected box/block geometry,
missing symbols and wide BMP fallback remain bitmap. Scalar colour support
is implemented; ZWJ/variation-sequence shaping, ligatures and complex-script
shaping are not. No universal glyph or smooth-fallback claim is made.

## Confirmed requirements and current boundary

Deliver two additional selectable looks: retro and polished modern with
multicolour glyph support. Retain the current font. Prioritise font sizing,
then these profiles, then measured performance. Never modify Hibr.

Font size is not physical display resolution or desktop zoom. Existing
`PUT /v1/fonts` supports explicit cell height `size_px`8..64 or legacy
scale1.0–3.0 in0.1 steps. The current deployed renderer still resamples bitmap
glyphs. It resizes PTYs and uses the same geometry for input, but does not
rasterize an outline face at a requested pixel size. Size is currently global.
Control Panel0.1.6 exposes pixel size; it does not itself change rasterization.

Bitmap `BlitFont` masks remain supported; the modern profile adds shared
coverage/colour rasters instead of stretching those masks. Another PSF alone
cannot add antialiasing or intrinsic multicolour glyphs. Current BMP wide
coverage is not universal Unicode or emoji-sequence support.

## Implementation sequence

1. Specify explicit pixel-size selection through the canonical font API.
   Preserve legacy scale callers; reject requests with competing size
   controls. Store one size policy and return resolved cell/glyph metrics.
   Publish schema fields only with implementing handlers and tests.
2. Establish one bounded glyph interface for coverage masks and intrinsic
   colour, with bearings, baseline and advance. Keep PSF loading supported.
   All backends share fallback, width and rasterization policy; drawing
   adapters consume the same glyph data rather than implement text engines.
3. Add both profiles using pinned, licensed assets with explicit repertoires
   and reproducible generation. Review real samples before default changes.
   FreeType is the selected production rasterizer (ADR0147). Evaluate colour
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

Release561 implements explicit cell pixel height and legacy-policy
compatibility (ADR0144), with full regression and bounded physical SDL/GLES2
sizing/capture checks passing. Candidate562 adds the optional Spleen retro
profile (ADR0145), plus integrated native outline/colour rendering (ADR0147).
Full release verification is running; production dependency and assets are
declared, but no562 deployment has been accepted yet.

## Historical shared-module checkpoint (superseded by integration above)

ADR0147 and `src/blit_raster.c` now implement the common cell-raster contract:
native-size FreeType grayscale coverage, premultiplied colour RGBA, explicit
PSF fallback/protected box/block geometry, exact cell-height fitting and a
bounded128-entry/4MiB cache. A shared RGB source-over helper separates mask
tinting from intrinsic colour. Context replacement is transactional.

`tools/tests/font_raster_gate.sh` checks every integer size8..64 with reviewed
Hack/Noto inputs, including partial alpha, full-block coverage, colour/alpha,
blend vectors, invalid input, cache pressure/reuse and failed-load survival.
The complete local font suite passes9/0skip/0fail with that gate enabled.
Valgrind reports zero errors and no leaks across all57 sizes. The module does
not invoke Hibr. Installed Hibr changed0.99.258 to0.99.259/ABI16 during this
work, so these local checks are not a frozen full-release acceptance run.
This is module verification, not backend/desktop acceptance. The server does
not call or link this module yet; there is no new deployed smooth profile.

Enable the module gate using `BLIT_GATE_OUTLINE`, `BLIT_GATE_COLOUR`,
`BLIT_FREETYPE_INCLUDE` (directory containing `ft2build.h`) and optional
`BLIT_FREETYPE_LIBRARY` (default Linux amd64 system soname path), then run
`sh tools/run-gates.sh font`. Missing candidate inputs are an explicit skip,
not a pass. Direct invocation accepts PSF/outline/colour paths as three args.

At that checkpoint, remaining work was: pin/package production FreeType and source assets;
attach raster ownership/invalidation to canonical font profiles and pixel
sizing; connect native KMS plus SDL immediate/batched/atlas rendering to the
same coverage/colour data; cover styles, combining/variation/sequence policy,
alpha and metrics with cross-backend pixel oracles; run frozen full gates;
package and physically review10.54 without changing its layout/backend by
surprise. Neither profile quality nor performance is accepted from a module
test alone. Existing production rendering is unchanged by this checkpoint.

## Source/rasterizer feasibility evidence (2026-10-19)

`tools/probes/font_source_capabilities.c` is an isolated TCC/FreeType source
probe, not an alternate terminal renderer. It verifies grayscale partial
coverage for Hack's A at17/23/32em pixels, and a genuinely coloured U+1F600
bitmap from Noto, including the premultiplied-BGRA channel invariant.
FreeType2.12.1 passes. This does not test font fallback, full sequences,
Blit blending/atlas/cache behavior or presentation.

Important measured distinction: requested em23 gives line height27,
ascender22 and descender-6 for this Hack build. The font layer must fit
ascent/descent/bearings to the exact requested terminal-cell size. Naively
using requested em pixels as cell height would recreate clipping. Noto's
selected bitmap strike is136x128, with10,279 intrinsically coloured pixels;
it needs deliberate cell fitting, not an assumed requested-size strike.

Candidate sources reviewed without system installation or redistribution:

| Input | Pinned identity | SHA256 |
| --- | --- | --- |
| Spleen8x16 |2.2.0, commit0493c34e22791824767c618fed42c434b477662c | `4a3d97ee61a8c86a7525d8c723cb8a14081f395cd2feb4227ba5e3baf0629bae` |
| Hack-Regular.ttf | Debian fonts-hack3.003-3 | `9f67c8a8cd28498525639437d6fac588c452e8572945c790db7641cc7907a69b` |
| NotoColorEmoji.ttf | Debian fonts-noto-color-emoji2.042-0+deb12u1 | `e5899ed38b8ed83e08bd3ac5de09791e9d19d288333a796de1d35ad17396f1ec` |

Licenses read: Spleen BSD2-clause; Hack Expat/Bitstream Vera; Noto font OFL1.1
(other project files have other licensing). Include actual license notices
with eventual distributed assets; no universal coverage claim follows.
Downloaded source inputs are currently under `/tmp/blit-font-sources.kyJ1xr/`.

Reproduce the capability check against those extracted headers/assets:

```sh
BLIT_FONT_SOURCE_ROOT=/tmp/blit-font-sources.kyJ1xr
tcc -std=c99 \
  -I"$BLIT_FONT_SOURCE_ROOT/extracted/usr/include/freetype2" \
  tools/probes/font_source_capabilities.c \
  /lib/x86_64-linux-gnu/libfreetype.so.6 \
  -o "$BLIT_FONT_SOURCE_ROOT/font-source-capabilities"
"$BLIT_FONT_SOURCE_ROOT/font-source-capabilities" \
  "$BLIT_FONT_SOURCE_ROOT/extracted/usr/share/fonts/truetype/hack/Hack-Regular.ttf" \
  "$BLIT_FONT_SOURCE_ROOT/extracted/usr/share/fonts/truetype/noto/NotoColorEmoji.ttf"
```

This probe is not linked into production; no font dependency/default changes
are made by running it. Choosing the shared glyph interface, fitting and
shaping contract remains the next implementation step after pixel sizing.
