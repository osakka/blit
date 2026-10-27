# ADR 0194: Blit's own fonts only, renamed; JetBrains Mono as Blit JetBrainless

- Status: accepted (2026-10-27; the owner's screen is the last check)
- Date: 2026-10-27
- Scope: `assets/fonts`, the font profile table, packaging, the font gates

## Context

The owner asked for two things:
- A font very like JetBrains Mono, built as a Blit font with all of Blit's
  glyphs.
- A cleanup of the font directory: keep Blit Modern and Retro, rename the
  `hibr-blit` fonts to Blit Terminus 16 and Blit Spleen 16, and remove Uni2
  and Lat15.

What the directory and the package held:
- **Ten font profiles,** six of them console fonts: `uni2-terminus16`,
  `lat15-terminus16`, `uni2-console`, `uni2-blit`, `uni2-fixed16` and
  `lat15-console`. Four of those six pointed at `/usr/share/consolefonts` or
  `/usr/share/blit`, paths Blit never installs.
- **Lat15-Terminus16.psf in every package,** copied from the build bundle's
  stale `fonts` directory. The repository never held it.
- **Hack-Regular.ttf and its licence,** packaged although nothing at runtime
  reads them. Only the emoji audit gate used Hack, as a font that lacks
  emoji. Blit Modern is built from Hack Nerd Font Mono, fetched by hash.
- **Uni2-Terminus16.psf,** which is the build input of the Terminus PSF. It
  is byte-identical to Debian `console-setup-linux` 1.221's copy (sha256
  `002f3314…`).

JetBrains Mono is under the SIL OFL 1.1 with no Reserved Font Name. Nerd
Fonts v3.4.0, the release Blit's outline fonts are already pinned to,
carries it patched with the full icon catalogue.

## Decision

**The profiles are Blit's own fonts:** `blit-terminus16`, `blit-spleen16`,
`blit-modern`, `blit-retro` and `blit-jetbrainless`.
- **The pixel fonts are renamed,** by id and by file:
  `Blit-Hibr-Terminus16.psf` becomes `Blit-Terminus16.psf`, and Spleen
  likewise. The wide-cell `Blit-Hibr-Unifont16.psf` becomes
  `Blit-Unifont16.psf`; it isn't selectable, but it's the same family of
  file.
- **The console profiles are removed,** and so is `Uni2-Terminus16.psf`.
  Its regeneration now reads Debian's copy, pinned by hash in the fonts
  README.
- **Packaging takes fonts from `assets/fonts` only.**
- **Hack-Regular.ttf and its licence are removed.** The emoji audit's
  negative fixture becomes Blit Modern, the font that actually ships.
- **No aliases for the old ids.** A saved selection naming one falls back to
  the boot font, as for any unavailable font. That's the rule: a fresh
  project, no compatibility shims. Both of the owner's machines use
  `blit-modern`.

**Blit JetBrainless** is JetBrains Mono 2.304, the version without
ligatures (Blit draws cells by codepoint, so ligatures could never apply),
from Nerd Fonts v3.4.0's `JetBrainsMonoNLNerdFontMono-Regular.ttf`.
- **Source and licence are pinned by sha256** in `modern-fonts.source.json`:
  `d83b3639…` for the font and `30f0c136…` for the licence.
- **It is built by the same generator,** with a third profile. It gets the
  same fitting of icons and geometric shapes, Blit's colour icons, and the
  Noto symbol blocks (1,535 added).
- **Braille and Block Elements stay cell geometry,** as for every font.
- **Family name:** "Blit JetBrainless Nerd".
- **It shares `blit-modern`'s Arabic companion.**
- **The cell grid is identical to Modern's** at every size, since `size_px`
  sets the cell, so switching fonts never moves the grid.

## Consequences

- The font list shows five Blit fonts, and nothing from the console set.
- The package loses about 330 KB: Hack, Lat15 and Uni2. It gains JetBrainless,
  about 2.9 MB.
- Gates and tools name the new files.

## Evidence

- **The generator reproduces:** JetBrainless builds byte-identically twice
  (`4e95abbf…`). With the profile table change, Modern and Retro still
  rebuild to their shipped hashes.
- **Cell sizes:** JetBrainless is 11×22 at 22 px and 8×16 at 16 px, as
  Modern and Retro are.
- **A specimen** of text, code, symbols, Braille and blocks renders with no
  `?` fallbacks.
- **The font suite passes.** `font_api_gate` now reads the repository's
  fonts; it had relied on the retired console fonts being installed on the
  build machine.
- **`package_artifact_gate`** checks the renamed and new fonts against
  source, and fails if any retired file is shipped.
