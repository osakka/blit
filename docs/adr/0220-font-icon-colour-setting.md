# ADR 0220: fonts.icon_colour: Blit's colour icons in colour, or as a monochrome shape that says state

- Status: accepted
- Note: the run-time setting (`fonts.icon_colour`, its plumbing and the
  global pen) is superseded by ADR 0221, in 0.3.49: the icon pen is a cell
  attribute, SGR 68. The build-time shape and its drawing stay.
- Date: 2026-11-06
- Scope: `src/blit_raster.c`, `src/blit_raster.h`, `src/blit_server.c`,
  `tools/fonts/build-blit-icons.py`, `assets/fonts/BlitIcons-Regular.ttf`,
  the SVGs `battery*.svg`, `wifi-bars.svg` and `hui.svg`,
  `openapi/blit-0.1.yaml`, `docs/api.md`, `docs/font-reference.md`
- Builds on: ADR 0195 (the glyph stack and the icon font), ADR 0197 (icons
  spanning columns), ADR 0218 (`fonts.hinting`, whose plumbing this reuses)

## Context

Blit's colour icons (the 249 catalogue icons from U+100000, COLR layers in
`BlitIcons-Regular.ttf`) are drawn in their own colours. A bar that is light or
dark needs the same glyph set in one ink. A first version tinted the colour
raster at run time, and it was wrong: a tint of the colour raster can only give
the union silhouette, so every level of a family had the same outline. battery-0
to battery-4 were pixel-identical, charging and draining differed by 1.3%, Wi-Fi
and volume showed all their bars at every level. In one ink, state has to be
carried by shape, and the shape cannot be recovered at run time from the colour
pixels. So the shape is decided where the artwork is, at build time, and the
run time only chooses the pen.

## Decision

A setting `fonts.icon_colour`, one of `colour`, `black`, `white`, `auto`;
default `colour`.

- **The shape is built from the SVG.** `build-blit-icons.py` gives an icon one
  extra glyph, `<name>.mono`, in no cmap, when its SVG has levels or marks.
  - It is the union of the *active* paths: a path whose `data-level` is above
    the icon's `signal_level` is left out, so a level is carried by what is
    drawn (battery bands, Wi-Fi bars, volume waves).
  - `data-mono="knockout"` on a path cuts its shapes out of the rest. They are
    added against the nonzero winding of the ink, so inside the ink they are
    holes and outside it they are solid: a charging bolt is a hole in the fill,
    and solid on the part of the battery that is empty, so it reads at every
    level. Knockout shapes are simple, without holes of their own.
  - `data-mono="disc"` on a path draws only its first subpath: a ring is a
    solid disc. With the hui letters as a knockout, hui is a solid disc with its
    letters cut out.
  - An icon with none of these gets no extra glyph: its colour glyph's coverage
    is its monochrome shape, exactly what a plain tint of it was. There is no
    per-icon list in C and no catalogue field: the SVG is the only source, and
    the raster asks the font whether `<glyph name>.mono` exists.
- **The pen is the run time's.** In `draw_blit_icon`, in `colour` nothing
  changes (byte-identical). In the other three the shape is rendered through the
  same fitting code, placed by the colour glyph's box, so a level never moves
  between states. `black` is rgb 0 and `white` rgb 255, hard, with the shape's
  coverage as alpha; `auto` is a coverage glyph, drawn in the cell's foreground,
  with the cell's background showing through the knockouts. `auto` makes Blit's
  icons behave exactly like the text and Nerd glyphs beside them in a bar:
  there is no theme detection, and light and dark follow from the cell's pens.
  In `black` and `white` the knockouts are transparent too, so it is the bar
  that shows through, not a second ink.
- **Scope.** Colour icons only. Chalk, Slate, controls and bar corners are cell
  geometry or role art and never reach `draw_blit_icon`; text, Noto and emoji
  do not either.
- **State families, as the artwork now says them.** battery-N is a frame, a
  terminal and N bands (N=4 is a solid fill, N=0 an empty frame); battery-N-charging
  the same bands and a bolt; battery-ac a solid fill with a plug cut out;
  `wifi-bars.svg` and `volume.svg` show only the bars and waves that are on
  (volume keeps its speaker as its base, wifi-1 is one taller bar). Their
  colour looks changed a little to make them readable at one ink: the battery
  frame is thinner (1.0 unit) with a smaller terminal, the bands fill the frame,
  the bolt and plug are larger, and the Wi-Fi bars are taller. Colour defaults of
  every other icon are unchanged.
- **Plumbing, as `fonts.hinting`.** `PUT /v1/fonts` accepts `icon_colour` alone
  or with the other controls; `active.icon_colour` reports it; the settings
  document stores it in `font.icon_colour` and a stored font without it means
  `colour`; anything else is 400 `invalid_font_icon_colour`
  (`invalid_settings_font` on the settings door) and changes nothing;
  `DELETE /v1/settings` restores `colour`. A change goes through
  `apply_font_configuration`: a fresh raster with an empty cache, swapped,
  reflowed and redrawn; an unchanged configuration is a no-op.

## Consequences

- One glyph set serves light and dark bars; no copies of the artwork. Seventeen
  small glyphs are added to the icon font (+2.5 KB).
- A future icon needs only `data-level` and `data-mono` in its SVG.
- `black` and `white` are exactly black and white; a theme that wants another
  single ink uses `auto` and sets the cell's foreground.

## Evidence

`tools/tests/font_icon_shape_gate.sh` renders the state families at 22 px (an
11x22 cell) and 16 px (8x16) in the black pen and requires every pair in a
family to differ by at least 15% (22 px) and 10% (16 px) as 50%-alpha masks,
`|A xor B| / |A or B|`: battery 0-4, charging 0-4, battery-N against
battery-N-charging, battery-ac against all ten batteries, Wi-Fi 1-4 and volume
0-3. It failed on the run-time tint (78 pairs below the minimum) and passes now.
`tools/tests/font_icon_colour_gate.sh` pins the default raster hash, recorded
before the setting, of the icons whose artwork was not touched; shows `colour`
equals the default; shows black, white and auto have one shape, exactly black,
exactly white and coverage; that an icon with no levels or knockouts keeps its
colour coverage exactly; that hui is a solid disc with its letters cut out; and
that a window glyph, text and a colour emoji are identical in all four modes.
`tools/tests/font_icon_colour_api_gate.rb` shows each value accepted, reported,
stored and drawn differently, invalid values refused with the display unchanged,
and the choice kept across a restart and reset by `DELETE /v1/settings`.
