# Blit terminal fonts

For application authors: [complete developer reference](../../docs/font-reference.md),
[glyph categories](../../docs/fonts/README.md),
[Blit/Hibr additions](../../docs/fonts/special.md), and
[emoji categories](../../docs/fonts/emoji.md). Tables are generated from these
canonical assets, not an independently maintained glyph registry.

## Text fonts and the glyph stack (ADR 0195)

**Text fonts are discovered.** Every regular, monospaced TTF or OTF in
`text/` is a Blit font, shipped exactly as its upstream releases it. The
same applies to an operator's own fonts in `/usr/local/lib/blit/fonts/text`.
- **Ids:** a font's id is its family name in kebab form, such as
  `jetbrains-mono-nl`.
- **Rejected files** are listed in `GET /v1/fonts` with their reason, for
  example "not monospaced".

| File | Font | Licence |
| --- | --- | --- |
| `text/Hack-Regular.ttf` | Hack 3.003 | `licenses/Hack-LICENSE.md` |
| `text/3270-Regular.ttf` | IBM 3270 3.0.1 | `licenses/IBM3270-LICENSE.txt` |
| `text/JetBrainsMonoNL-Regular.ttf` | JetBrains Mono NL 2.304 | `licenses/JetBrainsMono-OFL.txt` |
| `text/FiraCode-Regular.ttf` | Fira Code 6.2 | `licenses/FiraCode-OFL.txt` |
| `text/IBMPlexMono-Regular.ttf` | IBM Plex Mono 1.1.0 | `licenses/IBMPlexMono-OFL.txt` |

**Every glyph a text font lacks comes from one stack,** in this directory,
fitted to the chosen font when drawn:
1. **Cell geometry,** drawn by the raster: box drawing, block elements,
   Braille, core Powerline and Blit's bar corners.
2. **`BlitIcons-Regular.ttf`:** Blit's seven colour icons, the pen alias
   and the themed text glyphs. Built by `tools/fonts/build-blit-icons.py`
   from `icons/catalogue.json`.
3. **The text font itself.** A glyph that would overhang the cell is
   shrunk to fit, not dropped.
4. **`SymbolsNerdFontMono-Regular.ttf`:** all 10,386 Nerd icons, Nerd
   Fonts v3.4.0. Its private-use icons win over a text font's own.
5. **Noto Sans Symbols 2, Noto Sans Symbols and Noto Sans Math.**
6. **`NotoEmojiText.ttf` and `NotoColorEmoji.ttf`.**
7. **`BlitArabicModern.ttf`:** one Arabic companion for every font.
8. **`Unifont-Regular.otf` and `UnifontUpper-Regular.otf`:** GNU Unifont 16,
   so `?` is left only for codepoints no font has.

Two fitting rules apply, both carried over from the retired outline
generator:
- **Icons** fit within 90% of the advance and the text font's cap height,
  centred.
- **Symbols** keep the text font's size and baseline, shrunk only to fit the
  cell.

Each text font's cell is its `0` advance, so a few extra-wide glyphs don't
shrink its text.

**Provenance:** `fonts.source.json` pins every file by SHA-256, with its
upstream URL and licence. `font_assets_gate.rb` checks it, and that `text/`
holds nothing unpinned. To rebuild Blit's icon font:

```sh
PYTHONPATH=<fontTools4.60.1> python3 tools/fonts/build-blit-icons.py assets/fonts/BlitIcons-Regular.ttf
ruby tools/tests/outline_catalogue_gate.rb   # full Nerd catalogue, every text font, 57 sizes
ruby tools/tests/custom_icon_gate.rb         # Blit icons and Hibr's UI glyphs, every text font
```

Retired in 0.2.36:
- the baked Blit Modern, Retro and JetBrainless Nerd fonts;
- their generator, `build-outline-nerd.py`;
- Blit Arabic Retro.

Scope matters: this is **not all Unicode or all emoji sequences**. Font files
alone do not shape flags, skin tones, ZWJ families or VS16 presentation; that
shared-renderer acceptance remains internal tracking110; see the
[font plan](../../docs/font-evolution-plan.md).
Candidate576 adds an internal HarfBuzz/FreeType colour-cluster raster, tested
against all5,244 pinned Emoji18.0 entries at16px and representative families
at8..64px. The specimen includes its output, not a claim of terminal support.
Candidate577 adds compositor consumption, tested with representative sequence
families in native KMS and both SDL paths. Candidates578/579 add explicit
API/PTY emoji grouping while preserving default scalar layout. Candidate580
adds Unicode18.0 default/VS15/VS16 presentation and pinned Noto Emoji3.002
monochrome outlines; full end-to-end/live acceptance remains open.
Reproduce the complete-corpus raster test with
`ruby tools/tests/emoji_cluster_raster_gate.rb`;
`BLIT_FONT_VALGRIND=1` additionally checks that fixture under Valgrind.
A pixel font keeps every glyph it has, box drawing included, and borrows the
glyph stack for the rest (ADR 0199). Wide characters keep the separate wide
PSF.

## Retro: Blit Hibr Spleen16

`Blit-Spleen16.psf` is an optional8x16 retro face based on Spleen2.2.0
(profile `blit-spleen16`). Default Terminus is unchanged. It provides
1,575 scalar mappings, retaining all1,181 unique scalar mappings from the
existing Hibr font, with92 visibly different printable ASCII masks.
Box/block geometry, combining marks and the fitted control diamond retain
their canonical bitmaps. Other absent symbols use the checked-in Hibr font
as build-time fallback; no extra runtime renderer is added.

Upstream Spleen is BSD2-clause. The combined font contains OFL-covered
fallback material and is distributed under OFL1.1 with the upstream and
fallback notices retained in `licenses/` (Terminus, DejaVu, Noto and Droid).
The respective notices remain authoritative. Build provenance, exact source
commit and hashes are in `Blit-Spleen16.source.json`. Regenerate using
the pinned upstream BDF, not arbitrary host fonts:

```sh
python3 tools/fonts/build-retro-font.py /path/to/spleen-8x16.bdf \
  assets/fonts/Blit-Spleen16.psf
BLIT_SPLEEN_SOURCE=/path/to/spleen-8x16.bdf python3 tools/tests/retro_font_gate.py
```

Pixel size changes use `PUT /v1/fonts` with `size_px`; this bitmap face does
not become an outline font or intrinsic-colour font by resizing it. The
modern antialiased/colour profile is separate.

## Shared outline and colour rendering policy

Each outline text font (above) is rasterized at native size with grayscale
antialiasing. `NotoColorEmoji.ttf` 2.057 draws supported colour scalars that
the stack's earlier faces don't. Font size8–64px means exact cell height;
the font's em size is fitted inside that cell, not stretched after rendering.
The default font is not changed by installation. Select a font through the
same API or Control Panel as the others. Outline fonts scale smoothly.
Spleen and Terminus keep their intentional bitmap pixels for every glyph
they have, and take icons, symbols and emoji from the glyph stack, fitted to
their own H (ADR 0199).

`fonts.source.json` pins every file's SHA-256 and licence.
`licenses/NerdFonts-LICENSE.txt` and `licenses/FontAwesome-LICENSE.txt`
preserve the upstream icon notices, including Fonticons attribution and Font
Awesome licensing. Hack doubles as the emoji audit's negative fixture: a text
font that lacks emoji. Noto remains
unmodified, from upstream release `v2026-09-24-unicode18_0`, its2D CBDT/CBLC
font rather than an unsupported COLRv1 face. This replaced the older Debian
Noto2.042 asset in candidate574; see the deployment ledger for current status.

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

U+270E (`✎`) is Blit's original themed pen silhouette (ADR 0172), from
the icon font. Complete icon outlines are fitted at draw time with 5% side
insets and the text cap-height bound: no cropping and no proportional
advance. Icons take the terminal foreground colour; colour emoji remain a
separate mechanism.

From567, colour artwork is bounded by the fitted text em rather than
the full cell height (which includes line spacing). Its aspect ratio and
one/two-cell allocation remain unchanged. This is a shared raster policy,
not a bell-specific substitution or another font-size setting; see ADR0151.

Box drawing and block elements are cell geometry. A codepoint no face in the
stack has falls back to the PSF's `?`. Wide BMP characters keep the separate
wide PSF where no outline face draws them. This is not universal Unicode, script shaping,
ligature or emoji-sequence support. Explicit variation-selector/ZWJ shaping
is not added by these font files. See ADR0147 for the renderer boundary.

## Default: Blit Hibr Terminus16

`Blit-Terminus16.psf` is the default runtime font. It preserves the bundled
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
# Uni2-Terminus16 from Debian console-setup-linux 1.221, sha256
# 002f331400ea0257f894a6b81fafa7a0ab8ba8b816149b81487043dbf2d37e1a
zcat /usr/share/consolefonts/Uni2-Terminus16.psf.gz > /tmp/Uni2-Terminus16.psf
tools/build-hibr-font.py /tmp/Uni2-Terminus16.psf \
  assets/fonts/Blit-Terminus16.psf \
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

`Blit-Unifont16.psf` is the **16×16** font used for cells a wide
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
  assets/fonts/Blit-Unifont16.psf
```

Only glyphs the BDF marks `DWIDTH 16` are taken; a narrow glyph belongs in the
primary font. Codepoints outside the BMP are **not** covered, because
`unifont.bdf` is a BMP font — emoji live in plane 1 and need Unifont's
`unifont_upper`, which is a separate decision recorded in ADR 0062.
## Arabic companions (119)

`BlitArabicRetro.ttf` (Kawkab Mono0.501 derivative) and
`BlitArabicModern.ttf` (GNU FreeMono20120503 derivative) are required companions
of canonical Retro/Modern profiles in candidate0.1.588.
They preserve source outlines and positioning; native antialiased glyphs and
cell-local mark clusters replace PSF substitution for the tested Arabic
repertoire. There is no second bidi pass: Hibr already supplies visual forms.
The companion shares the primary Latin baseline. GET/v1/fonts identifies the
loaded companion; missing files reject replacement atomically. Arbitrary
custom font paths do not automatically receive a companion.

Canonical source/output hashes and licences: `arabic-fonts.source.json`.
`tools/fonts/build-arabic-profile.py --profile retro|modern SOURCE OUTPUT`
uses pinned fontTools4.60.1, Unicode14.0.0 decompositions and HarfBuzz6.0.0.
It adds only missing presentation aliases that the source shaper resolves to
one glyph; it does not promise all Arabic forms exist. Both faces retain the
source notices. Kawkab is OFL1.1. FreeMono is GPLv3-or-later with its font
embedding exception retained in the font and `FreeMono-README.txt`;
`FreeMono-LICENSE.txt` is the GPL and `FreeMono-AUTHORS.txt` credits contributors.
The complete upstream preferred-source/build archive is included at
`sources/freefont-src-20120503.tar.gz`, pinned by the manifest; its
`sfd/FreeMono.sfd`, FontForge generation scripts and build instructions are
the source of the upstream outlines. Blit's offline derivative generator is
the source of the naming/cmap modifications, not a new font-outline design.

`arabic-specimens.json` records actual read-only Hibr0.99.277/ABI16 visual
output, including `حِبْر`, vocalized Bismillah, lam-alef variants and
Arabic-Indic/Hindi digits `٠١٢٣٤٥٦٧٨٩` (U+0660..0669), alongside ASCII digits.
It is a finite corpus, not exhaustive Quranic/Unicode coverage.

Run `ruby tools/tests/arabic_raster_gate.rb` or the normal font gate suite.
Generate a real raster preview with
`ruby tools/fonts/preview.rb --arabic build/font-preview/blit-arabic.png`.
Backend fixtures now consume ink offsets and verify neighbouring-ink damage,
pane clipping, cursor repaint and mixed Latin/Arabic/colour cache collisions
through KMS mock, SDL software and offscreen GLES. Run
`ruby tools/tests/arabic_backend_gate.rb kms|software|opengles2` (one mode).
These are finite composition tests, not physical scanout or typographic
acceptance. Physical typography review remains separate from automated gates.

[Inspect the actual candidate raster specimen](../../docs/images/blit-arabic-candidates.png).
