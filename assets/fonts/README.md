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
| `text/spleen/spleen-{6x12,8x16,12x24,16x32,32x64}.otf` | Spleen 2.2.0, one font of five masters (ADR 0227) | `licenses/Spleen-LICENSE.txt` |

**Every glyph a text font lacks comes from one stack,** in this directory,
fitted to the chosen font when drawn:
1. **Cell geometry,** drawn by the raster: box drawing, block elements,
   Braille, core Powerline and Blit's bar corners.
2. **`BlitIcons-Regular.ttf`:** Blit's original colour icons (Wi-Fi,
   battery, volume, telephones, file-manager icons and the rest), the pen
   alias and the themed text glyphs. Built by `tools/fonts/build-blit-icons.py`
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
alone do not shape flags, skin tones, ZWJ families or VS16 presentation. The
raster does that with HarfBuzz and FreeType as colour clusters, tested against
all 5,244 pinned Emoji 18.0 entries at 16px and representative families at
8..64px; the compositor draws them, explicit API/PTY emoji grouping is
available beside the default scalar layout, and Unicode 18.0
default/VS15/VS16 presentation uses the pinned Noto Emoji 3.002 monochrome
outlines for text presentation. Remaining end-to-end acceptance is internal
tracking 110; the history is in the
[font plan](../../docs/history/font-evolution-plan.md).
Reproduce the complete-corpus raster test with
`ruby tools/tests/emoji_cluster_raster_gate.rb`;
`BLIT_FONT_VALGRIND=1` additionally checks that fixture under Valgrind.
A pixel font keeps every glyph it has, box drawing included, and borrows the
glyph stack for the rest (ADR 0199). Wide characters keep the separate wide
PSF.

## Spleen: one font of five masters (ADR 0227)

`text/spleen/` holds the five OTF masters of Spleen 2.2.0, unmodified, from
the release tarball
`https://github.com/fcambus/spleen/releases/download/2.2.0/spleen-2.2.0.tar.gz`
(sha256 `ec42925c6b56d2138c862b2f97147c872e472f674bf03423417d827a08d69a89`):

| Master | sha256 |
| --- | --- |
| `spleen-6x12.otf` | `c7ab7083f6074949892f48cee940ad3c921a1276c8675d7c7e8fc51879a0236b` |
| `spleen-8x16.otf` | `cadfddb5f4f9a22d7fd879760adfd82dc801993d13d8faf1afaf375c05affe74` |
| `spleen-12x24.otf` | `9e4b0c1a9706ea4ee27815e473b7bcff4aac33e58210a4f74978fbb6feba5d32` |
| `spleen-16x32.otf` | `937b5787414ca4976ee7a1553d244a8d71cf232d2da2e5a173b3cc53ba22deed` |
| `spleen-32x64.otf` | `ae970e820426137d1b0d0bca695d1edfd1c8cd778f964544af3555d21ce3fed5` |

Each master is an outline font of pixel squares, crisp only at its own cell
height and whole multiples of it; the larger ones are redrawn by the author,
not scaled. A directory in `text/` is one font: it is listed once, as
`spleen`, and the master for each size is the largest whose height divides
the cell height, else the largest below it, scaled. Each master's design cell
is read from its own outlines (their pixel grid), not from its name or a
list. Upstream Spleen is BSD-2-Clause; its licence is
`licenses/Spleen-LICENSE.txt`, the tarball's `LICENSE` byte for byte.

**Retired in 0.3.68:** `Blit-Spleen16.psf` (id `blit-spleen16`), the fixed
16 px PSF built from the 8x16 BDF with Terminus fallback glyphs, with its
`Blit-Spleen16.source.json` and its builder and gate. A saved
`blit-spleen16` falls back to the boot font; there is no alias.

## Shared outline and colour rendering policy

Each outline text font (above) is rasterized at native size with grayscale
antialiasing. `NotoColorEmoji.ttf` 2.057 draws supported colour scalars that
the stack's earlier faces don't. Font size8–64px means exact cell height;
the font's em size is fitted inside that cell, not stretched after rendering.
The default font is not changed by installation. Select a font through the
same API or Control Panel as the others. Outline fonts scale smoothly.
Terminus keeps its intentional bitmap pixels for every glyph it has, and
takes icons, symbols and emoji from the glyph stack, fitted to its own H
(ADR 0199). Spleen, a font of masters, is pixel-exact at the sizes its
masters divide and scaled smoothly between them (ADR 0227).

`fonts.source.json` pins every file's SHA-256 and licence.
`licenses/NerdFonts-LICENSE.txt` and `licenses/FontAwesome-LICENSE.txt`
preserve the upstream icon notices, including Fonticons attribution and Font
Awesome licensing. Hack doubles as the emoji audit's negative fixture: a text
font that lacks emoji. Noto remains
unmodified, from upstream release `v2026-09-24-unicode18_0`, its2D CBDT/CBLC
font rather than an unsupported COLRv1 face. It replaced the older Debian
Noto 2.042 asset.

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
runs offline and does not install HarfBuzz. See ADR 0157.

U+270E (`✎`) is Blit's original themed pen silhouette (ADR 0172), from
the icon font. Complete icon outlines are fitted at draw time with 5% side
insets and the text cap-height bound: no cropping and no proportional
advance. Icons take the terminal foreground colour; colour emoji remain a
separate mechanism.

Since 0.1.567, colour artwork is bounded by the fitted text em rather than
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
`unifont_upper`, which is a separate decision recorded in ADR 0062. Outside
the wide cells, plane 1 and beyond come from the glyph stack's
`UnifontUpper-Regular.otf`.

## The Arabic companion

`BlitArabicModern.ttf` (GNU FreeMono 20120503 derivative) is the one Arabic
companion attached to every outline text font (ADR 0195). It preserves source outlines and positioning; native
antialiased glyphs and cell-local mark clusters replace PSF substitution for
the tested Arabic repertoire. There is no second bidi pass: Hibr already
supplies visual forms. The companion shares the primary Latin baseline.
`GET /v1/fonts` identifies the loaded companion (`active.arabic_companion`)
and each profile's `arabic_companion_path`; pixel fonts have none.

Canonical source/output hashes and licences: `arabic-fonts.source.json`.
`tools/fonts/build-arabic-profile.py --profile modern SOURCE OUTPUT`
uses pinned fontTools 4.60.1, Unicode 14.0.0 decompositions and HarfBuzz 6.0.0.
It adds only missing presentation aliases that the source shaper resolves to
one glyph; it does not promise all Arabic forms exist. The face retains the
source notices. FreeMono is GPLv3-or-later with its font
embedding exception retained in the font and `FreeMono-README.txt`;
`FreeMono-LICENSE.txt` is the GPL and `FreeMono-AUTHORS.txt` credits contributors.
The complete upstream preferred-source/build archive is kept in the
repository at `sources/freefont-src-20120503.tar.gz`, pinned by the manifest.
It is not in the runtime package: the separate `blit-font-sources` package
installs it at `/usr/share/doc/blit-font-sources/`, and
`/usr/lib/blit/fonts/FreeMono-SOURCE.txt` in the `blit` package points to it
(ADR 0217); its
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
The backend fixture consumes ink offsets and verifies neighbouring-ink damage,
pane clipping, cursor repaint and mixed Latin/Arabic/colour cache collisions
through the KMS compositor's cell path, under two text fonts. Run
`ruby tools/tests/arabic_backend_gate.rb`. These are finite composition
tests, not physical scanout or typographic acceptance. Physical typography
review remains separate from automated gates.

[Inspect the raster specimen](../../docs/images/blit-arabic-candidates.png).
