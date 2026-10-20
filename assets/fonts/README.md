# Blit terminal fonts

## Smooth Blit Retro and Blit Modern

`blit-retro` is the new smooth, rounded mainframe-inspired profile, based on
pinned Nerd Fonts3.4.0 3270 Mono. `blit-modern` is the clean Hack Mono companion.
Both are Blit-tailored renamed derivatives, not original typeface designs.
Both retain the complete pinned Nerd catalogue: **10,386 distinct codepoints**,
plus their source Unicode repertoires. Native grayscale antialiasing is used
at each requested size; neither profile enlarges a one-bit bitmap for text.
Nerd icons take terminal colours; supported emoji scalars use the shared,
shared Noto Colour Emoji2.057 face for intrinsic multicolour artwork.

![Actual shared-raster specimen: Blit Retro and Modern](../../docs/images/blit-fonts.png)

Select with `PUT /v1/fonts`, for example
`{"font_id":"blit-retro","size_px":24}`. The existing font inventory and
Control Panel use the same profiles. Sizes8..64px specify exact cell height,
not an em height or whole-display scale. Default Terminus and the selectable
pixel-art Spleen remain unchanged. This source change has not been deployed.

`modern-fonts.source.json` is the single provenance manifest for **both**
outline profiles, their shared colour fallback, catalogue and licence files.
See `licenses/nerd-v3.4.0/README.md` for icon-family attribution and sources.
Generate both with the same tool and hash-checked upstream inputs:

```sh
PYTHONPATH=<fontTools4.60.1> python3 tools/fonts/build-outline-nerd.py \
  --profile retro /path/to/3270NerdFontMono-Regular.ttf assets/fonts/BlitRetroNerd-Regular.ttf
PYTHONPATH=<fontTools4.60.1> python3 tools/fonts/build-outline-nerd.py \
  --profile modern /path/to/HackNerdFontMono-Regular.ttf assets/fonts/BlitModernNerd-Regular.ttf
ruby tools/fonts/preview.rb
ruby tools/tests/outline_catalogue_gate.rb
```

The preview is `build/font-preview/blit-fonts.png`, generated with Blit's real
production raster and compositor without SDL, Hibr or a connected display.
Complete icons fit within cap height and90% advance; Powerline separators
currently use optical fitting, not a guaranteed seamless-edge geometry policy.
Every catalogue codepoint is raster-tested at all57 integer heights for both
profiles; `cod-blank` intentionally has no ink. See ADR0156.

Scope matters: this is **not all Unicode or all emoji sequences**. Font files
alone do not shape flags, skin tones, ZWJ families or VS16 presentation; that
shared-renderer work is [issue110](https://git.home.arpa/itdlabs/blit/issues/110).
Missing scalars and protected box/block geometry retain pixel-art PSF fallback;
wide BMP characters retain the separate wide PSF. No Hibr code is changed.

## Retro: Blit Hibr Spleen16

`Blit-Hibr-Spleen16.psf` is an optional8x16 retro face based on Spleen2.2.0
(profile `blit-hibr-spleen16`). Default Terminus is unchanged. It provides
1,575 scalar mappings, retaining all1,181 unique scalar mappings from the
existing Hibr font, with92 visibly different printable ASCII masks.
Box/block geometry, combining marks and the fitted control diamond retain
their canonical bitmaps. Other absent symbols use the checked-in Hibr font
as build-time fallback; no extra runtime renderer is added.

Upstream Spleen is BSD2-clause. The combined font contains OFL-covered
fallback material and is distributed under OFL1.1 with the upstream and
fallback notices retained in `licenses/` (Terminus, DejaVu, Noto and Droid).
The respective notices remain authoritative. Build provenance, exact source
commit and hashes are in `Blit-Hibr-Spleen16.source.json`. Regenerate using
the pinned upstream BDF, not arbitrary host fonts:

```sh
python3 tools/fonts/build-retro-font.py /path/to/spleen-8x16.bdf \
  assets/fonts/Blit-Hibr-Spleen16.psf
BLIT_SPLEEN_SOURCE=/path/to/spleen-8x16.bdf python3 tools/tests/retro_font_gate.py
```

Pixel size changes use `PUT /v1/fonts` with `size_px`; this bitmap face does
not become an outline font or intrinsic-colour font by resizing it. The
modern antialiased/colour profile is separate.

## Shared outline and colour rendering policy

Profile `blit-modern` uses `BlitModernNerd-Regular.ttf`, a renamed, reproducibly
generated derivative of Nerd Fonts3.4.0 Hack Mono, for native-size
antialiased monospaced text and `NotoColorEmoji.ttf`2.057 for supported colour
scalars absent from the outline face. Font size8–64px means exact cell height;
the font's em size is fitted inside that cell, not stretched after rendering.
The default font is not changed by installation. Select this profile through
the same API/Control Panel as the others. The modern face scales smoothly;
Spleen/Terminus retain intentional bitmap pixels.

`modern-fonts.source.json` pins source package versions, font SHA256s and
license SHA256s. `licenses/HackNerd-LICENSE.txt`, `licenses/NerdFonts-LICENSE.txt`
and `licenses/FontAwesome-LICENSE.txt` preserve the upstream notices, including
Fonticons attribution and Font Awesome licensing. The original Hack face stays
as a legacy provenance reference/negative emoji-audit fixture, not a second Modern runtime profile or
an input to the full-Nerd generator. Noto remains
unmodified, from upstream release `v2026-09-24-unicode18_0`, its2D CBDT/CBLC
font rather than an unsupported COLRv1 face. This replaces the older Debian
Noto2.042 asset in candidate574; it is not deployed to the host.

The complete Unicode Emoji18.0 test corpus and licence are pinned in
`assets/unicode/emoji-18.0`. The independent offline font-shaping audit is:

```sh
ruby tools/tests/emoji_corpus_gate.rb
python3 tools/fonts/audit-emoji.py --require-complete > build/emoji-font-audit.json
```

The audit reports its HarfBuzz library version/hash and exact font hash.
All5,244 entries shape to one nonzero glyph in Noto2.057 with HarfBuzz6.0.0;
Noto2.042 fails174 RGI entries and10 minimally-qualified variants. These are
**font-shaping** results, not a production renderer implementation, colour
bitmap validation or permission to claim complete emoji support. The audit
does not install HarfBuzz or add it to the production binary. See ADR0157.

Before ADR0156, Modern retained only1475 Font Awesome mappings; that restriction
is superseded by the complete pinned Nerd catalogue above.
U+270E (`✎`), Hibr's existing menu character, uses the
Font Awesome pencil outline without changing Hibr or terminal codepoints.
Complete icon outlines are fitted with5% side insets and the text cap-height
bound; no cropping or proportional advance. Icons take the terminal foreground
colour; colour emoji remain a separate mechanism. Generate with
the single `tools/fonts/build-outline-nerd.py` generator shown above.
The generator checks the pinned source hash and never downloads implicitly.

From567, colour artwork is bounded by Modern's fitted text em rather than
the full cell height (which includes line spacing). Its aspect ratio and
one/two-cell allocation remain unchanged. This is a shared raster policy,
not a bell-specific substitution or another font-size setting; see ADR0151.

Protected box/block geometry and missing scalars use the existing PSF
fallback; those fallback characters remain pixel-art. Wide BMP characters
retain the separate wide PSF. This is not universal Unicode, script shaping,
ligature or emoji-sequence support. Explicit variation-selector/ZWJ shaping
is not added by these font files. See ADR0147 for the renderer boundary.

## Default: Blit Hibr Terminus16

`Blit-Hibr-Terminus16.psf` is the default runtime font. It preserves the bundled
Terminus 8×16 glyphs and adds the missing Hibr Unicode desktop glyphs at the
same cell size. The added glyphs are rasterized from DejaVu Sans Mono, which is
licensed under the permissive DejaVu font license; see the system font package
for the complete license text.

Fallback outlines are rendered completely, then the largest native raster
size fitting8x16 is selected. Negative bearings and overwide glyphs must not
be cropped by an8px canvas. Control diamonds use deterministic symmetric
geometry with space for bold. `tools/tests/font_fit_gate.py` checks complete
fallback masks and diamond symmetry; the manifest gate checks coverage.

Regenerate it with:

```sh
tools/build-hibr-font.py assets/fonts/Uni2-Terminus16.psf \
  assets/fonts/Blit-Hibr-Terminus16.psf \
  /usr/share/fonts/truetype/dejavu/DejaVuSansMono.ttf \
  /usr/share/fonts/truetype/droid/DroidSansFallbackFull.ttf \
  /usr/share/fonts/truetype/noto/NotoSansArabic-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoNaskhArabic-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoSansSymbols2-Regular.ttf \
  /usr/share/fonts/truetype/noto/NotoSans-Regular.ttf
```

The generated PSF is checked in so runtime installations have no font-tool or
font-package dependency. The manifest includes Hibr glyphs plus common
terminal shading symbols and combining marks; it does not claim universal
Unicode or CJK shaping in an 8×16 cell. The build uses ordered fallback fonts
and fails if a required scalar resolves to a fallback `.notdef` bitmap. Required box-drawing characters are
generated as dedicated deterministic terminal geometry, rather than inherited
from potentially aliased fallback-font bitmaps.

## The wide font

`Blit-Hibr-Unifont16.psf` is the **16×16** font used for cells a wide
character occupies. It is a second artifact rather than an extension of the
8×16 font because PSF2 carries one cell size per file, and a wide character
needs a bitmap twice as wide — a two-cell character drawn from an 8×16 bitmap
is a stretched narrow glyph, which is what the primary font can offer and why
CJK, Kana and Hangul previously rendered as `?`
([ADR 0062](../../docs/adr/0062-a-second-font-for-wide-cells.md)).

49,899 glyphs, covering every wide block of the BMP completely: CJK Unified
(20,992), CJK Extension A, CJK Compatibility, Hangul Syllables (11,172),
Hangul Jamo, Hiragana, Katakana, CJK Symbols and Fullwidth forms.

Source: **GNU Unifont 15.1.05**, the only freely licensed bitmap font with
complete coverage at this cell size. Unifont is dual licensed under the SIL
Open Font License 1.1 and GPLv2+ with the GNU Font Embedding Exception; this
artifact is redistributed under the **OFL 1.1**. Copyright © 1998–2024 Roman
Czyborra, Paul Hardy, Qianqian Fang, Andrew Miller, Johnnie Weaver, David
Corbett, Nils Moskopp, Rebecca Bettencourt, Ho-Seok Ee et al.

Regenerate it with:

```sh
tools/fonts/make-wide-psf.py /usr/share/unifont/unifont.bdf \
  assets/fonts/Blit-Hibr-Unifont16.psf
```

Only glyphs the BDF marks `DWIDTH 16` are taken; a narrow glyph belongs in the
primary font. Codepoints outside the BMP are **not** covered, because
`unifont.bdf` is a BMP font — emoji live in plane 1 and need Unifont's
`unifont_upper`, which is a separate decision recorded in ADR 0062.
