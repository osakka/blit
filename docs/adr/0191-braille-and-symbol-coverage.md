# ADR 0191: Braille is cell geometry; missing symbol blocks come from reviewed Noto sources

- Status: proposed
- Date: 2026-10-26
- Scope: raster cell geometry, the outline font generator and its manifest,
  both outline profiles (modern, retro)

## Context

On the owner's machine, btop's graphs and some of Claude Code's and Codex's
symbols show as `?`. The bytes are right: btop sends real Braille (U+28C0,
U+2809, U+28FF and more, read from the surface's cells). Blit draws `?`
because its fonts do not have the glyphs.
- **Blit Modern Nerd** (Hack Nerd Font Mono with Blit's icons) has, by
  cmap:
  - Braille: none of 256;
  - Miscellaneous Technical: 224 of 256 missing (⏺ ⎿ ⏵ ⏎ ⌘ ⌥);
  - Dingbats: 137 of 192 missing (✻ ✶ ✳ ✢ ✓ ✔ ✗);
  - Miscellaneous Symbols: 252 of 256 missing (⚠);
  - Mathematical Operators: 79 of 256 missing (⋮);
  - Symbols for Legacy Computing: none of 256.
  Geometric Shapes, Block Elements and Arrows are complete.
- **The fallback path:** a codepoint missing from the outline font falls
  back to the PSF base (Terminus, 930 glyphs), and from there to its `?`.
  The Unifont PSF serves only wide cells: one PSF holds one cell size, and
  Unifont's is 16×16. So it cannot fill narrow cells.

## Options considered

1. **A runtime fallback chain of system fonts.** The look would depend on
   what is installed, glyphs would not be fitted to the cell, and the font
   would no longer be one reviewed artefact. Rejected.
2. **Scale Unifont's bitmaps.** Blurry at 22 px, and it holds narrow glyphs
   only as half of a 16×16 file. Rejected.
3. **Braille as geometry, plus reviewed outline sources for the symbol
   blocks.** Chosen.

## Decision

**Braille (U+2800–28FF) is cell geometry,** drawn by the raster like the
Powerline separators and Blit's bar corners. It is added to
`protected_geometry`, so no outline font can claim it.
- **Dots:** 2 columns × 4 rows. Bits 0–2 are dots 1–3 down the left column,
  bits 3–5 are dots 4–6 down the right, and bits 6 and 7 are dots 7 and 8 at
  the bottom left and right.
- **Positions:** column centres at ¼ and ¾ of the cell width, row centres at
  (0.5, 1.5, 2.5, 3.5) / 4 of the height.
- **Shape:** round dots, radius about 0.19 of a quarter of the cell height,
  never touching: at least one pixel of gap at a 16 px cell. Anti-aliased by
  8×8 sampling in sixteenths, as the corners are.

Dots placed exactly on the cell grid are why terminals draw Braille
themselves: a font's dots rarely line up, and graphs then show gaps.

**The symbol blocks come from Noto,** under the SIL Open Font Licence, one
consistent design. Each source is pinned by SHA-256 in
`modern-fonts.source.json`, as Hack and the emoji fonts are, with its
licence in `assets/fonts`. Sources, in order of preference:
1. **Noto Sans Symbols 2:** Miscellaneous Technical, Dingbats,
   Miscellaneous Symbols, Legacy Computing. Includes ⏺ ⏵ ✻ ✶ ✳ ✓ ✗ ⚠.
2. **Noto Sans Symbols:** what Symbols 2 lacks there, such as ⎿ and ⎇.
3. **Noto Sans Math:** Mathematical Operators, such as ⋮ and ⧉.
4. **Noto Sans Mono:** the remainder, such as ※.

In the listed blocks, only 59 rarely used codepoints remain uncovered by
these four.

**Only codepoints Hack lacks are taken, and nothing of Hack changes.**
Added glyphs are fitted as the generator fits Nerd icons: the shared optical
cap-height and side inset, centred in the advance. Then ⏺ or ✻ sits in the
cell like the icons, not at text scale. Blit Retro Nerd takes the same
codepoints from the same sources, so the two profiles cover the same set.

## Consequences

- btop's graphs, and Claude Code's and Codex's markers and spinners, draw as
  intended.
- The fonts grow by the added outlines, a few hundred kilobytes; the release
  receipts carry their new hashes.
- Codepoints still uncovered keep falling back to `?`, a visible signal
  rather than a blank.

## Evidence (to be met before acceptance)

- **A C raster gate for Braille,** at 16 and 22 px:
  - U+2800 is blank; U+28FF lights all eight dot centres;
  - U+2809 and U+28C0, btop's, light exactly their dots;
  - the gaps between dots are dark;
  - no U+28xx falls through to `?`.
- **A font gate:** the rebuilt profiles cover the listed key codepoints, and
  every glyph Hack already had is unchanged, outline for outline.
- **On the owner's screen:** btop's graphs, and Claude Code's ⏺, ⎿ and
  spinner.
