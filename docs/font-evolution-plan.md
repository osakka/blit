# Retro and modern colour fonts

Tracking: [Blit#107](https://git.home.arpa/itdlabs/blit/issues/107).

## Current offline candidate573: smooth Retro and full Nerd catalogue

ADR0156 adds `blit-retro` (3270 Mono derivative) and expands Modern (Hack Mono)
to all10,386 distinct codepoints in the pinned Nerd Fonts3.4.0 catalogue.
Both use the same existing antialiased FreeType renderer and colour-scalar
fallback, one generator and one provenance manifest. Default/bitmap profiles
are unchanged. No display, live configuration or Hibr change is required.

Run `ruby tools/fonts/preview.rb` for the actual production-raster specimen.
The catalogue gate checks592,002 glyph/size cases per face, including deliberate
empty `cod-blank`; partial-alpha, colour, API/restart and backend composition
tests remain separate. See [font assets](../assets/fonts/README.md) for exact
sources, licences, reproducible commands and scope. These are local checks,
not a claim of physical visual acceptance. The host remains572.

"All emoji" is not completed: scalar fonts do not add sequence shaping.
Flags, ZWJ, skin tones, keycaps and presentation selectors are issue110.
Powerline seamless-edge geometry is also not proved by icon coverage.

The dated rollout sections below are historical evidence, not current
repertoire restrictions or instructions to redeploy an earlier release.

## Deployed567: colour artwork proportions

ADR0151 follows the measured bell report:20px-tall ink in a22px cell versus
13px-tall H. The shared colour raster now fits within the text em, not line
spacing. Bell ink becomes18px tall while keeping two-cell advance and22px
global size. No text/Nerd/backend change. Source-raster optical-bound gate
is red before/green after; physical visual approval is not yet obtained.
Font suite15/0/0 and expanded1026-sample backend pixel gates pass; package
rebuild byte-identical and API deployment/version567 verified. Native KMS,
Modern22px and saved settings237 unchanged; login awaits authentication.

## Deployed566: Hibr Nerd coverage and readable start-menu pencil

Implemented ADR0150's font-only approach. Modern retains Hack Unicode and
1475 Font Awesome mappings from pinned Nerd Fonts3.4.0 Hack Mono. This
explicitly covers Hibr's Nerd glyph set, not every Nerd family. Its existing
menu pencil U+270E uses the nf-fa-pencil outline, so no Hibr edits are needed.
Complete icons fit within fixed advance and cap-height without clipping;
production gate1140 samples and all15 font-suite checks pass, plus969 pixel
samples on each native/SDL software/GLES adapter. Package reproducibility and
API deployment/version566 verified on10.54; Modern22px/native KMS/settings237
retained. Login awaits authentication and operator aesthetic approval.
Colour bell sizing has not changed in this release.

## Historical566 planning: Modern icon coverage and visual metrics

Operator accepted565 physical mouse motion ("It's perfect!"). Sequence is
now Modern glyph/size work, then SDL104; retain the native backend and current
layout while validating fonts. Do not reinterpret font size as screen scale.

Read-only inspection of installed Hibr confirms its unicode/ascii/nerd
glyph selector in wm/glyphs.hibr. Notification styles are separate:
wm/bar.hibr's dt_bellglyph maps bell directly to U+1F514. Selecting Nerd does
not itself replace this bell. Modern currently bundles Hack plus Noto colour
emoji, not a complete Nerd outline repertoire.

The shared draw_colour path fits each colour bitmap to the available cell
rectangle; outline text instead uses fitted font ascent/descent and baseline.
This is a verified policy difference, not yet a measured diagnosis of the
operator's oversized bell. Distinguish ink bounds/optical size from terminal
advance: text remains fixed-grid, some Unicode symbols legitimately use two
cells, and visually different widths are not themselves a defect.

1. Extract Hibr's emitted glyph repertoire read-only; measure actual bell,
   menu pen, notification/restart and Nerd symbols at current22px and adjacent
   odd/even sizes. Record source face, fallback, columns and ink bounds.
2. Add pinned/licensed single-cell Nerd icon coverage to Modern, keeping Hack
   text, colour emoji and canonical block geometry. Upstream Nerd Font Mono
   is the single-cell variant; avoid proportional/overhanging icon assets.
3. Introduce failing tests for demonstrated clipping, incorrect advance or
   inconsistent visual metrics before changing the shared raster policy.
   Verify native/SDL software/GLES consume identical resolved glyph metrics.
4. Preview on10.54 through the API, preserving/restoring size and layout;
   obtain visual approval, commit/package/deploy verified changes, then SDL104.

Nerd icons are normally foreground-coloured masks, not multicolour emoji.
Do not promise universal Unicode coverage, suppress legitimate wide cells,
or impose even-only sizes based on the unconfirmed wallpaper observation.
Reference: https://github.com/ryanoasis/nerd-fonts (Mono variant and licensing).

## 2026-10-19 release564 deployed

Native KMS recovery/configuration issue108 is resolved and operator-confirmed.
Candidate564 combines the existing shared raster/font work with that config
fix; it does not switch the working native backend. Final archive source is
da7e32d (packaged guide clarifies564 versus config-only563); server binary is
unchanged from7db553e and reproducibility passes. Full exact-binary regression
112pass/3skip/0fail and seven offscreen GLES checks pass. API deployment on10.54
is verified; native KMS and physical layout/non-font settings are unchanged.
Both profiles pass live17/19/23px geometry/capture checks. Real native menu
glyph F shows11 intermediate coverage levels on Modern versus none on Retro.
Modern is selected; a later external size change to22px was preserved.
User confirmed restarting Hibr during previews; do not attribute observed
session replacement to Blit. Operator aesthetic/whole-desktop resize review
and the separate pointer-performance phase remain open.

The bounded live probe supports `BLIT_FONT_ID=blit-modern` or
`BLIT_FONT_ID=blit-hibr-spleen16` with `BLIT_FONT_SIZES=17,19,23` and existing
`BLIT_LIVE`, `BLIT_AUTH_TOKEN`, `BLIT_EVIDENCE_DIR` controls. It captures every
output, checks complete-cell grids and unchanged backend/physical pane regions,
then restores the original profile AND size policy. It refuses to overwrite a
concurrent font change. `host_font_probe_gate.rb` runs this actual probe against
an isolated server for both profiles, restoration and unavailable-profile
refusal. Captures/geometry are not universal glyph or desktop acceptance.

After font rollout, return to pointer issue101: the operator reports less
choppiness when YouTube is foreground. This is a recorded clue, not a timing
measurement or root-cause finding. Keep font and pointer changes separable.

## Current implementation: release0.1.564 (initial candidate562)

Both new profiles are integrated: Spleen retro (`blit-hibr-spleen16`) and
smooth Hack/Noto colour (`blit-modern`). Original Terminus remains default.
The shared FreeType rasterizer is linked into the server and consumed by
native KMS, SDL software and SDL/OpenGLES2. Exact cell sizing, per-context
bounded caching, premultiplied-alpha handling and transactional profile/size
replacement are implemented. Production adapters pass513 glyph samples each
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

Full frozen regression, reproducible package/identity, API deployment and
representative physical capture/metrics pass for564 as recorded above.
Operator aesthetic/whole-desktop resize review remains separate. Neither a
module test nor a local offscreen GL test is physical scanout approval.
Protected box/block geometry,
missing symbols and wide BMP fallback remain bitmap. Scalar colour support
is implemented; ZWJ/variation-sequence shaping, ligatures and complex-script
shaping are not. No universal glyph or smooth-fallback claim is made.

## Confirmed requirements and current boundary

Deliver two additional selectable looks: retro and polished modern with
multicolour glyph support. Retain the current font. Prioritise font sizing,
then these profiles, then measured performance. Never modify Hibr.

Font size is not physical display resolution or desktop zoom. Existing
`PUT /v1/fonts` supports explicit cell height `size_px`8..64 or legacy
scale1.0–3.0 in0.1 steps. Bitmap profiles resample their one-bit masks;
Modern instead rasterizes an outline at the fitted native size. PTY sizing
and input use the same resolved cell geometry. Size is currently global.
Control Panel0.1.7 exposes font/profile and pixel size through this same API;
the selected profile, not a second panel preference, determines rasterization.

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
Candidate562 itself was not deployed;564 is the first production font rollout.
Production dependency and licensed assets are declared; do not interpret the
config-only563 maintenance release as carrying the new font profiles.

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

## Historical source/rasterizer feasibility evidence (2026-10-19)

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

At initial feasibility, sources were reviewed without system installation:

| Input | Pinned identity | SHA256 |
| --- | --- | --- |
| Spleen8x16 |2.2.0, commit0493c34e22791824767c618fed42c434b477662c | `4a3d97ee61a8c86a7525d8c723cb8a14081f395cd2feb4227ba5e3baf0629bae` |
| Hack-Regular.ttf | Debian fonts-hack3.003-3 | `9f67c8a8cd28498525639437d6fac588c452e8572945c790db7641cc7907a69b` |
| NotoColorEmoji.ttf | Debian fonts-noto-color-emoji2.042-0+deb12u1 | `e5899ed38b8ed83e08bd3ac5de09791e9d19d288333a796de1d35ad17396f1ec` |

Licenses read: Spleen BSD2-clause; Hack Expat/Bitstream Vera; Noto font OFL1.1
(other project files have other licensing). Actual license notices now ship
with the canonical assets in assets/fonts; no universal coverage claim follows.
The original temporary download directory is not a release/build input.

Reproduce the isolated capability check using canonical headers/assets:

```sh
mkdir -p build/tests
tcc -std=c99 \
  -Ibuild/deps/prefix/include/freetype2 \
  tools/probes/font_source_capabilities.c \
  /lib/x86_64-linux-gnu/libfreetype.so.6 \
  -o build/tests/font-source-capabilities
build/tests/font-source-capabilities \
  assets/fonts/Hack-Regular.ttf assets/fonts/NotoColorEmoji.ttf
```

This probe is not linked into production; no font dependency/default changes
are made by running it. Shared glyph fitting/rasterization is now implemented
under ADR0147; complex shaping remains outside the delivered scalar contract.
