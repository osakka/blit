# ADR 0201: File-manager icons: one drawing per kind, colourways by a primary colour

- Status: accepted; in use since 0.3.4
- Date: 2026-10-29
- Scope: `tools/fonts/draw-file-icons.py` (new), `assets/fonts/icons`, the
  icon catalogue and its API limit, the icon gates, the package gate

## Context

The owner wants a section of the glyph library for Hibr's Files:
two-column colour icons that "look awesome", in the spirit of System 7, in
a few colour variations, and easy to grow. The work is to make them in a
way that stays maintainable: draw the standard ones well and generate the
rest by swapping a primary colour.

## Decision

- **One generator, one drawing per kind.**
  - `draw-file-icons.py` draws each kind once, on a 32-unit grid, in a
    System 7 manner: a black outline, flat fills, and a one-unit highlight
    and shade. The artwork is original.
  - Each layer names a **role**, not a colour: outline, paper, fold, ink,
    primary, light, shade, and fixed accents (red, sky, hill, sun, metal,
    cardboard, led).
- **A colourway is one primary colour.** Its light and shade are derived
  from it in HLS (+0.17 and −0.20 lightness). The four colourways are
  classic (periwinkle, the default), blue, green and amber. **Every kind
  carries the primary somewhere,** so every colourway of every kind is its
  own icon.
- **Twelve kinds to start:** folder, document, text, pdf, image, audio,
  video, archive, code, application, disk, trash. A new kind is one small
  function, and a new colourway is one line.
- **Two columns each,** as ADR 0197: a left/right pair, from one SVG listed
  twice with `columns: 2`. They take U+100033–100092, colourway-major:
  classic's twelve pairs first, then blue, green and amber. Ids are
  `file-<kind>-<colour>-left|right`.
- **The catalogue limit** for `GET /v1/fonts/icons` rises from 8 KiB to
  64 KiB. It is still bounded, but sized for families. The file icons took
  the catalogue to about 17 KiB.
- **The package gate** checks every file under `assets/fonts/icons` in the
  release commit, read from git, instead of a hand-kept list.
- **Hibr names them by kind,** with colour as a separate suffix (its
  request), so Files picks by file kind and a theme or setting picks the
  colour.

- **Kinds added later** take the next free codepoints, a kind at a time,
  four colourways each (left/right pairs). In 0.3.6: home
  (U+100093–10009A), then boxer, markdown, spreadsheet, diagram,
  presentation, font, web, terminal, settings, bundle and link, eight
  codepoints each from U+10009B to U+1000F2. Boxer is a Boxer game without
  its cover art: two boxing gloves. Hibr names each `GL[f<kind>]` plus the
  colour suffixes.
- **Special folders are a folder plus a badge (0.3.7):** a small outlined
  square at the folder's lower right holds an emblem in the primary colour
  (branch, gear, house, shield, monitor, arrow, page, note, frame, film,
  diamond, glove). One folder drawing serves twelve special folders.
- **0.3.7 adds thirty-nine kinds,** U+1000F3–10022A: the twelve folders,
  and rc, history, service, key, network, cron, readme, license, log,
  kernel, boot, c, asm, makefile, patch, json, vector, rom, ebook, exe,
  disc, package, library, lock, database, usb, share. The catalogue then
  passes 64 KiB, so the API limit rises to 256 KiB.

## Revised (0.3.8): colour for folders, natural colours for the rest

The owner found four colourways of everything samey, and too many icons to
make. **Only folders have colourways now** (the plain folder, the twelve
special folders and the share), which Files' Control Panel picks.
- **Every other kind has one look in its own natural colours,** as in System
  7: a red-roofed house, burgundy Boxer gloves, a gold key, a grey disk, a
  green spreadsheet, a blue globe. The generator gives each kind a colour
  in `NATURAL` and draws it once (`file-<kind>.svg`).
- **Labels.** A 3×5 pixel font in the generator writes a format's name on
  an icon: PDF on the PDF icon, and JPG, PNG and GIF on three new picture
  kinds at U+10022B–100230.
- **The changeover keeps every codepoint working:**
  1. Blit 0.3.8 draws the natural icon in all four colour slots of each
     non-folder kind, so Hibr's `_blue`, `_green` and `_amber` names show
     it too.
  2. Hibr names non-folder kinds without a colour.
  3. Blit retires the unused slots, never reused. **Done in 0.3.9**, after
     Hibr 0.99.385: 294 codepoints (the blue, green and amber slots of the
     non-folder kinds) are listed under `retired` in the catalogue and draw
     nothing from Blit's icons.
- `custom_icon_gate` takes one codepoint per distinct drawing from the
  catalogue, and requires those to differ. Since 0.3.9 it also takes the
  live icons and the retired codepoints from the catalogue, not from ranges
  written in the gate.

## Consequences

- Hibr maps `GL[ffolder]`, `GL[fdoc]` and so on, with `_blue`, `_green` and
  `_amber` for the other colourways. Files gets a Large Icons setting.
- Growing the set (spreadsheet, presentation, font, link, network…) is a
  drawing function and a catalogue entry per colourway, with no renderer
  change.

## Evidence (0.3.4)

- The owner approved a preview: 12 kinds × 4 colourways, large and at the
  menu bar's real size.
- Rendered through Blit at 22 px with JetBrains Mono NL: each icon is whole
  across two cells, all at one scale and on one line.
- `custom_icon_gate` (145 originals, every text font, 57 sizes):
  - every half draws in colour;
  - from 16 px, no two whole file icons, kinds or colourways, are alike.
    The first build had text, pdf and image identical across colourways,
    which is why each kind now carries the primary.
- `custom_icon_api_gate`: the range's first and last pairs through a real
  PTY, one column each.
- `font_kms_raster_gate`: the file range on the KMS path.
- `package_artifact_gate`: every icon source in the commit.
