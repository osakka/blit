# ADR 0220: fonts.icon_colour: Blit's colour icons in their own colours, solid black or solid white

- Status: accepted
- Date: 2026-11-06
- Scope: `src/blit_raster.c`, `src/blit_raster.h`, `src/blit_server.c`,
  `tools/fonts/build-blit-icons.py`, `assets/fonts/BlitIcons-Regular.ttf`,
  the icon catalogue (`mono`), `openapi/blit-0.1.yaml`, `docs/api.md`,
  `docs/font-reference.md`
- Builds on: ADR 0195 (the glyph stack and the icon font), ADR 0197 (icons
  spanning columns), ADR 0218 (`fonts.hinting`, whose plumbing this reuses)

## Context

Blit's colour icons (the 249 catalogue icons from U+100000, COLR layers in
`BlitIcons-Regular.ttf`) are drawn in their own colours. A bar that is light
or dark, or a theme that wants one ink, needs the same glyph set in a single
colour: solid black for a light bar, solid white for a dark one. The owner
wants it as a setting, not as copies of the glyphs. The `hui` badge is the letters
in a teal ring; as a monochrome mark it should be a solid disc
with the letters cut out, the Hibr logo: black disc and white letters on a
light bar, white disc and black letters on a dark one. A plain tint of the
colour badge cannot do that: its disc is a ring, empty in the middle.

## Decision

A setting `fonts.icon_colour`, one of `colour`, `black`, `white`, default
`colour`.

- **Where.** The tint is applied once, in `draw_blit_icon`, the one place a
  colour icon is drawn. The icon is fitted and rendered exactly as before;
  only its colour is replaced. Premultiplied, black is rgb 0 with the icon's
  alpha, and white is rgb equal to the alpha, so every pixel keeps its
  coverage. `colour` does not touch the pixels, so the default is
  byte-identical (the gate pins a hash recorded from the pre-change raster).
- **What it covers.** Colour icons only. The Chalk and Slate window glyphs,
  controls and bar corners are cell geometry or role art and never reach
  `draw_blit_icon`; text, Noto and emoji do not either.
- **The badge.** The catalogue marks an icon `"mono": "disc-knockout"`
  (today `hui-left` and `hui-right`). The SVG stays the single source:
  `build-blit-icons.py` takes the first path's first subpath as the disc, fills
  it solid, and knocks every other path out of it, producing one extra glyph
  `<name>.mono`, in no cmap, as two COLR layers: a white disc, black marks.
  The build refuses a marked icon whose disc is not its outermost shape, so
  the badge is fitted by the same box in every mode and nothing moves. The
  catalogue pin in `fonts.source.json` changes with the field, and the icon
  font is rebuilt (252 codepoints as before; the colour output is unchanged).
- **How the raster knows.** There is no list of badges in C. For a colour icon
  in a mono mode the raster asks the font for `<glyph name>.mono`; if the glyph
  exists it is drawn, through the same fitting code, instead of the colour
  glyph. Its white disc and black marks are the white pen as drawn and the
  black pen inverted, with the disc's alpha. An icon without the glyph is a
  plain coverage tint.
- **Plumbing, as `fonts.hinting`.** `PUT /v1/fonts` accepts `icon_colour`
  alone or with the other controls; `active.icon_colour` reports it; the
  settings document stores it in `font.icon_colour` and a stored font without
  it means `colour`; anything else is 400 `invalid_font_icon_colour`
  (`invalid_settings_font` on the settings door) and changes nothing;
  `DELETE /v1/settings` restores `colour`. A change goes through
  `apply_font_configuration`, which builds a fresh raster with an empty cache,
  swaps it, reflows and redraws; an unchanged configuration is a no-op. The
  word parser is shared with hinting.

## Consequences

- One glyph set serves light and dark bars; no copies of the artwork.
- The icon font gains two glyphs (the `hui` mono parts) and the catalogue one
  optional field, `mono`, documented in the OpenAPI `FontIcon` schema.
- A future badge only needs the field and an SVG whose first path is its disc.
- Black and white are exactly black and exactly white. A theme that wants
  another single ink is not served by this setting.

## Evidence

`tools/tests/font_icon_colour_gate.sh` pins the default raster hash recorded
before the setting, shows `colour` equals the default, black and white differ
from it and from each other, keep every icon's alpha, are exactly black and
exactly white for plain icons, make `hui` a solid disc (more covered pixels
than the colour badge) whose shades are each other's inverse, and leave a
window glyph, text and a colour emoji identical in all three modes.
`tools/tests/font_icon_colour_api_gate.rb` shows each value accepted,
reported, stored and drawn differently, invalid values refused with the
display unchanged, and the choice kept across a restart and reset by
`DELETE /v1/settings`.
