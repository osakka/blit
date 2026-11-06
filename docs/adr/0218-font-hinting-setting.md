# ADR 0218: fonts.hinting: how text glyphs are hinted, one setting, fitted icons exempt

- Status: accepted
- Date: 2026-11-06
- Scope: `src/blit_raster.c`, `src/blit_raster.h`, `src/blit_server.c`,
  `openapi/blit-0.1.yaml`, `docs/api.md`, `docs/font-reference.md`
- Builds on: ADR 0144 (explicit cell pixel size), ADR 0215 (a fitted glyph is
  never trimmed)

## Context

Outline text was always loaded with FreeType's target-normal hinting. At small
cell sizes that is the crispest and the most distorting choice; some owners
want light hinting (vertical only, outline shapes kept) or none. Fitted icons
and symbols (ADR 0215) have their own rules, chosen so a glyph is measured
after rendering and never trimmed, and must not move with a text preference.

## Decision

A setting `fonts.hinting`, one of `full`, `light`, `none`, default `full`.

- It selects the load flags of text glyphs in `draw_outline_face`:
  `full` is `FT_LOAD_TARGET_NORMAL` (what text used, so the default is
  byte-identical), `light` is `FT_LOAD_TARGET_LIGHT`, `none` is
  `FT_LOAD_NO_HINTING`.
- `draw_fitted` (icons, spanning parts, and the symbols that overhang a cell)
  and cell-geometry glyphs ignore it. So do the shaped Arabic companion
  (always unhinted) and the colour emoji bitmaps.
- It travels with the font: `PUT /v1/fonts` accepts `hinting` alone or with
  `font_id` and a size, `active.hinting` reports it, and the settings
  document stores it in `font.hinting`; a stored font without it means
  `full`. Anything else is 400 `invalid_font_hinting` (`invalid_settings_font`
  on the settings door), and changes nothing.
- A change takes the path a size change takes: `apply_font_configuration`
  builds a new raster (with an empty cache) from the requested configuration,
  swaps it, reflows and schedules a render. There is no second path; an
  unchanged configuration is still a no-op.

## Consequences

- Text appearance at small sizes can be chosen without changing the grid.
- `blit_raster_set_hinting` is called once, before the first lookup, like
  `blit_raster_set_arabic`; a raster's cache never mixes modes.

## Evidence

`tools/tests/font_hinting_gate.sh` pins the default raster hash recorded from
the raster before the setting, shows `full` equals the default, none, light
and full differ for text at small sizes, and a fitted icon is identical in all
three. `tools/tests/font_hinting_api_gate.rb` shows each value accepted,
reported, stored and drawn differently, invalid values refused with the
display unchanged, and the choice kept across a restart and reset by
`DELETE /v1/settings`.
