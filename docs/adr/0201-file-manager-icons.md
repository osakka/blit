# ADR 0201: File-manager icons: one drawing per kind, colourways by a primary colour

- Status: accepted (2026-10-29; the owner approved the preview)
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
