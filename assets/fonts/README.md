# Blit terminal fonts

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
modern antialiased/colour profile is separate, still pending work.

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
