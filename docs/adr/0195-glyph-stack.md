# ADR 0195: Text fonts are discovered; every glyph comes from one shipped stack

- Status: accepted; in use since 0.2.36
- Date: 2026-10-27
- Scope: `blit_raster.c` (glyph resolution), `blit_font.c` (the font list),
  font assets and their layout, packaging, the font gates, the outline
  generator (retired)
- Supersedes: the baked-profile parts of ADRs 0156, 0191 and 0194

## Context

Every Blit outline font is baked: a Nerd-patched TTF into which the
generator re-fits 10,386 icons, splices about 1,500 Noto symbols and Blit's
colour icons, and renames the result. Coverage is therefore a property of
each build, and scaling to more fonts multiplies it:
- **Each font is 2.5–3 MB,** most of it the same icons again.
- **Gaps remain:** overhanging glyphs such as ⟶ draw as `?`, because a
  glyph that would clip falls to the 16 px Terminus bitmap. Fifty-nine
  symbols are uncovered, and Blit JetBrainless borrows Modern's Arabic.
- **Box drawing breaks:** a font's ┌─┬─┐ rarely reaches the cell's edges at
  a fitted size, so lines show gaps. Every serious terminal (kitty, WezTerm,
  Alacritty) draws these itself.
- **The font list is written by hand,** in C, beside the manifest.

The owner asked for a proper set of fonts that all have every glyph, picked
up from a fonts directory, with Blit's additions on any font.

## Decision

**A text font is just a text font.** Any monospaced scalable font
(TTF/OTF) in a fonts directory is a Blit font. It is shipped or added
untouched, and Blit supplies everything else from one stack.

**Glyph resolution, in order, for a one-cell scalar:**
1. **Cell geometry,** drawn by Blit: Braille, Block Elements, Powerline,
   Blit's bar corners, and now **Box Drawing (U+2500–257F)**. Lines join
   cell to cell and line weight follows the cell size.
2. **Blit's colour icons:** one small COLR font built once from
   `icons/catalogue.json`.
3. **The text font,** for everything it has, except Nerd ranges it doesn't
   claim.
4. **Nerd Fonts Symbols Mono** (`SymbolsNerdFontMono-Regular.ttf`, v3.4.0):
   all 10,386 icons, in one file.
5. **Noto Sans Symbols 2, Noto Sans Symbols and Noto Sans Math,** shipped
   whole: arrows, technical, dingbats, symbols and operators.
6. **Text emoji and colour emoji,** as now.
7. **Arabic:** one companion for all fonts, Blit Arabic Modern, by
   decision.
8. **GNU Unifont 16** (OTF, BMP plus upper planes) as the last net, so `?`
   is reserved for codepoints no font has.

**Fitting happens at draw time, once, for every face.**
- **The text font** keeps today's rule: the largest size whose ascent,
  descent and advance fit the cell.
- **A text-font glyph that would overhang** (⟶) is scaled down to fit the
  cell, not dropped.
- **Stack faces (icons and symbols)** use the generator's rule, moved to
  C: fit the glyph's bounds within 90% of the advance and the text font's
  cap height, then centre it. The stack therefore takes its proportions
  from the chosen text font, and an icon sits beside text as it did when
  baked.
- **Unifont** is drawn at integer multiples of its 16 px grid where they
  fit, and scaled otherwise.

**The font list is discovered.**
- **Text fonts** come from `/usr/lib/blit/fonts/text`, the shipped and
  pinned set, and `/usr/local/lib/blit/fonts/text`, the operator's own.
  Pixel fonts (PSF) come from the matching `pixel` directories.
- **A font's id** is its family name in lower-case kebab form, such as
  `jetbrains-mono` or `ibm-plex-mono`. Its title is the family name.
- **Fonts are rejected** if they aren't scalable, aren't fixed-width, or
  have no Unicode charmap. The list reports the reason.
- **The list is cached** by path, size and mtime, so adding a file shows
  up on the next `GET /v1/fonts` without a restart.
- **The C profile table is removed.**
- **A saved id that no longer resolves** falls back to the boot font, as
  in ADR 0194.

**The shipped set** (pinned by sha256 in one manifest, licences beside
them):

| Text font | Source | Licence |
|---|---|---|
| Hack | source-foundry v3.003 | MIT and Bitstream Vera |
| IBM 3270 | rbanffy v3.0.1 | BSD-3-Clause |
| JetBrains Mono (NL) | JetBrains v2.304 | OFL 1.1 |
| Fira Code | tonsky 6.2 | OFL 1.1 |
| IBM Plex Mono | IBM plex-mono 1.1.0 | OFL 1.1 |

Pixel fonts: Blit Terminus 16 and Blit Spleen 16, as now.

**Retired:**
- the outline generator's baking: `build-outline-nerd.py`, its symbol
  splicing, and the Nerd-patched sources;
- the baked fonts `BlitModernNerd`, `BlitRetroNerd` and
  `BlitJetBrainlessNerd`;
- `BlitArabicRetro` and its GPL FreeMono sources, since one Arabic
  companion serves all fonts.

The ids `blit-modern`, `blit-retro` and `blit-jetbrainless` become `hack`,
`ibm-3270` and `jetbrains-mono`.

## Consequences

- **Every outline font has every glyph,** and a font dropped into the
  directory does too. The two pixel fonts are the exception (see "Not yet").
- **Coverage is proven once, for the stack,** by one gate run over every
  discovered font.
- **Package size:** five unmodified text fonts of 0.15–0.3 MB each and one
  4 KB icon font replace three baked 2.5–3 MB fonts.
- **More faces are open at once:** about ten. Each glyph is resolved once,
  then cached by the raster's existing cell-keyed cache, so a steady screen
  costs nothing more. The cost of a first-time resolve is measured.
- **The ids change** (no aliases). The owner's machines will fall back to
  the boot font until a new id is chosen. The control panel lists fonts
  from the API, so it needs no change.

## As built (0.2.36)

One release, not two: the stack and discovery landed together, measured as
below. The differences from the decision above:
- **Arabic:** the single companion is Blit Arabic Modern, built from GNU
  FreeMono. It was chosen originally because it covers Hibr's vocalised
  specimen. Blit Arabic Retro, from Kawkab Mono, is retired. FreeMono's GPL
  source offer stays in `sources/`.
- **Iosevka Term was shipped in 0.2.36 and dropped in 0.2.37.** Its TTF is
  11.6 MB, about 40% of the package, for coverage the stack already gives
  and a narrowness the fixed cell cancels. An operator who wants it copies
  it into `/usr/local/lib/blit/fonts/text`. Five text fonts now ship.
- **A text font's cell is its `0` advance,** not its widest glyph. Iosevka's
  few wide glyphs had shrunk its text.
- **Nerd's private-use icons come from Nerd Symbols,** even when the text
  font has its own glyph at that codepoint, so icons are the same in every
  font.
- **Pixel profiles:** the root's PSFs whose cells are narrower than tall.
  The square Unifont PSF stays the wide-cell font and is listed as
  rejected, "a wide-cell font".
- **Box drawing** comes from a table derived from the Unicode character
  names:
  - arms that are light, heavy or double;
  - inner and outer corners for double lines;
  - 2, 3 and 4 dashes, kept solid where a part can't hold a gap;
  - arcs with crisp straight runs;
  - anti-aliased diagonals.
  Until now this range came from the scaled 16 px PSF.

## Evidence (0.2.36)

- **`outline_catalogue_gate`:** all 10,386 Nerd codepoints, at all 57 sizes
  (8–64 px), drawn and not fallback, in each of the six text fonts. That is
  3,552,012 samples, with 0 failures.
- **`custom_icon_gate`,** in every text font at every size:
  - Hibr's 101 UI glyphs in one and two columns;
  - Blit's seven colour icons, premultiplied, with four distinct Wi-Fi
    levels, and the themed pen;
  - ⟶ and Ж ש ก ა ሀ 漢 字, drawn and not fallback.
- **`font_raster_gate`:** U+2500 is a solid line through the cell's centre,
  edge to edge, at every size from 8 to 64 px.
- **`font_discovery_gate` (new):**
  - every shipped text font is listed by its family-derived id, available,
    with the same 11×22 cell at 22 px;
  - the two pixel fonts are listed, and the wide-cell PSF is rejected,
    "a wide-cell font";
  - a monospaced font copied into the local directory appears at once and
    can be selected, and a proportional one is rejected, "not monospaced";
  - a removed font leaves the list.
- **`font_assets_gate`:** every text and stack file and its licence match
  `fonts.source.json`, and `text/` holds nothing unpinned.
- **Font suite:** 36 of 36 pass.
- **Cost:** 11×22 cells, on the build machine, three runs each:

  | | 0.2.35 baked Modern | 0.2.36 Hack plus stack |
  |---|---|---|
  | Raster open (a font switch) | 0.3 ms | 1.6 ms |
  | First ASCII glyph | about 28 µs | about 28 µs |
  | First icon or symbol | about 13 µs | about 19 µs |
  | Fallbacks in that sample | 12 | 0 |

  Every glyph is then cached, so a steady screen costs nothing more.

## Not yet (done in 0.2.43)

**The pixel fonts were not on the stack.** A bitmap profile drew only its
PSF's glyphs, with `?` for icons and symbols. ADR 0199 puts them on it.
