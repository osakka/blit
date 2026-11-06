# ADR 0221: The icon pen is a cell attribute, SGR 68

- Status: accepted
- Date: 2026-11-06
- Scope: the cell attributes, the SGR parser, DECRQSS, XTGETTCAP, `GET
  /v1/server` capabilities, the cells API and OpenAPI `CellStyle`,
  `src/blit_raster.c`/`.h` (`blit_raster_get`, the glyph cache key),
  `src/blit_font.h` (`raster_get`), both compositors (`blit_kms.c`,
  `blit_kms_gles.c`), `PUT`/`GET /v1/fonts` and the settings document (the
  removal), `docs/terminal.md`, `docs/api.md`, `docs/font-reference.md`
- Supersedes: ADR 0220's run-time setting ("The pen is the run time's" as a
  setting, and "Plumbing"). Its build-time shape (`<name>.mono`,
  `data-level`, `data-mono`) and `draw_blit_icon`'s drawing stay.

## Context

ADR 0220 made the pen of Blit's colour icons one global setting,
`fonts.icon_colour`. Hibr wants its menu-bar icons monochrome while the
desktop and file manager keep theirs in colour, on the same screen at the
same time. A global setting cannot say that. The owner's condition: it stays
smooth, with no per-frame cost.

## Options considered

1. **Keep the setting, add a second icon range.** Every icon twice; a
   catalogue of copies, and a client that maps two codepoints per icon. No.
2. **A per-cell attribute, drawn by re-rasterising per cell.** A raster
   rebuild or a per-frame tint is the cost the owner ruled out.
3. **A per-cell attribute, and the pen in the glyph cache key (chosen).**
   The raster already draws the colour, black, white and auto forms from the
   build-time shape; which one a cell gets is a cache lookup.

## Decision

**The attribute.** Two bits of the cell attribute word, bits 21–22, above
the shadow (bits 16–20), in `BlitRasterIconPen`'s values: 0 colour (the
default, and what a cell without it is), 1 auto, 2 black, 3 white. SGR 0
resets it. It is a style like framed and shadow, in the same 32-bit word
that scrollback and the alternate screen keep (gated for the shadow, not
separately for the pen), and compared whole by the no-op write check.

**The sequence: SGR 68:v, ended by SGR 69.** Exactly as SGR 66/67 (ADR 0211):
- v is 1 auto, 2 black, 3 white; `68:0` and a bare `68` end it, as `69`
  does; an unknown v leaves the pen alone.
- **Colon form only.** `68;2` is two SGRs: 68 (no value, so off) and dim.
- **Why 68 and 69.** They are the next pair in the gap ADR 0211 found
  unassigned, beside 66/67. ADR 0211's survey (2026-11-05) is the evidence,
  and it covers 66–72: xterm's ctlseqs has nothing from 50 to 89, WezTerm
  only 73–75 from 60 to 89, mintty nothing above 50, and Wikipedia's table
  leaves 66–72 empty. Nothing in this repository lets us check VTE, kitty,
  foot or iTerm2 (ADR 0211 did not read them either), so that is not a
  survey of every terminal. SGR 69 is unrelated to DEC private mode 69
  (DECLRMM, `CSI ? 69 h`), which Blit also implements.
- **DECRQSS** reports `;68:v` when the pen is not colour.
- **Discovery.** `sgr-icon-pen` in `GET /v1/server`'s capabilities, and
  XTGETTCAP answers it as a boolean, as `sgr-shadow` (0.3.46).
- **The cells API** reports `style.icon_pen` (`colour`, `auto`, `black`,
  `white`) on every styled cell and accepts it wherever a style is accepted;
  an unknown name refuses the cell's style, as an unknown shadow does.

**Rendering is a compositing choice, not a raster rebuild.**
- `blit_raster_get` takes the pen per call (and `BlitFont.raster_get` with
  it). For a lone Blit colour icon (plane 16, with COLR layers) the pen is
  OR'd into the cache key's first scalar above bit 20 (no scalar uses those
  bits), so an icon has at most four entries. For every other glyph the pen
  is not looked at and the key is unchanged: text, chrome, controls and
  emoji are cached once whatever pen asks. A cell with no pen costs nothing
  new; a pen-tagged icon cell adds one layer probe per lookup.
- The cluster path (`blit_raster_get_cluster`, a base with marks) never
  reaches `draw_blit_icon` (an icon is a lone scalar there), so it takes no
  pen.
- The compositors pass each cell's pen bits. They already branch per glyph
  on its format (coverage drawn in the cell's foreground, premultiplied RGBA
  as it is), so auto takes the cell's foreground and black and white draw
  as they are, with no new drawing code.
- **The GLES compositor caches sprites by its own key**, which had no
  attribute bits (framed and shadow are drawn as rectangles and never reach
  a sprite). The key gains the pen, for plane-16 cells without marks only,
  or a black icon would reuse the colour sprite.
- **GLES sprites read their format from the raster.** A sprite is made by
  drawing the cell white over black and over white; premultiplied white ink
  (the white pen) looks exactly like coverage in those two renders and was
  tinted by the cell's foreground. Where the raster draws the cell's glyph,
  its own format now decides; the two renders decide the rest. This is asked
  once, when the sprite is made. It also makes an all-grey colour glyph on
  GLES match the CPU compositor, which was the reference already.

**A pen change is cell damage only.** The attribute word is compared whole
when a cell is written (`terminal_cell_matches`), so a pen flip changes and
damages exactly the cells whose pen changed: no reflow, no font
reconfiguration, no cache flush.

**The global setting is removed, with no shim.** `icon_colour` is gone from
`PUT`/`GET /v1/fonts`, the settings document's `font` object (written and
validated), `blit_raster_set_icon_colour` and the raster's field, OpenAPI,
`docs/api.md` and `docs/font-reference.md`; `font_icon_colour_api_gate.rb`
is deleted. A settings document saved by 0.3.48 holds `font.icon_colour`;
the font object's parsers read only their own keys, so it loads, the key is
ignored, and it is not written back. `PUT /v1/fonts` with only
`icon_colour` is `font_configuration_empty`.

## Consequences

- Hibr sets SGR 68 on its menu-bar icon cells and leaves the desktop and the
  file manager alone; each cell is drawn as it says.
- A client that set `fonts.icon_colour` gets 400 on that request and should
  switch on `sgr-icon-pen` instead.
- Bits 23–31 of the attribute word remain free.

## Evidence

- `tools/tests/terminal_conformance.rb` (terminal): 68:1/2/3, an unknown
  value, 69, 68:0, a bare 68 and SGR 0; the pen beside bold and the shadow,
  each ended alone; DECRQSS `;68:2` and nothing after 69; XTGETTCAP
  `sgr-icon-pen`; the capability flag.
- `tools/tests/icon_pen_gate.rb` (renderer, run on both compositors): a row
  of text, four colour icons and text, rewritten with the icons in black
  inside a synchronized update, damages exactly the four icon cells
  (`2,1 4x1`), and back to colour the same; the topology revision, cell grid,
  surface size and active font are unchanged; the raster renders at most one
  glyph a new icon variant and none on the way back; the icons are pixel for
  pixel what they were. One icon in colour, auto, black and white and auto
  under a second foreground: black is black, white white, auto the cell's
  foreground, all one shape, colour none of them. A letter and a box-drawing
  glyph in each pen are identical to pen 0.
- `tools/tests/font_icon_colour_gate.sh` (font): pen 0 is still the
  pre-ADR-0220 raster hash; one raster serves an icon's four pens with four
  renders and then only hits; text, chrome and emoji are drawn once whatever
  the pen. `font_icon_shape_gate.sh` passes the black pen per call.
- `tools/tests/font_icon_colour_settings_gate.rb` (font): a document with
  `font.icon_colour: "black"` loads hack at 22 px, is not reported back by
  `GET /v1/fonts` or `GET /v1/settings`, draws the icon as on a document
  without it, and is dropped when the document is next written. On 0.3.48
  this gate fails five checks.
- The `timing` suite on 0.3.48 and on this change, back to back on one
  machine (CPU compositor): `frame_rate_gate` 62.485 Hz and 62.556 Hz;
  `terminal_soak_gate` (120 s, 4 sessions) 14160 and 14112 snapshots, 1288
  and 1283 resizes; `performance_baseline_gate` and
  `pointer_cadence_observation_gate` pass on both. The differences are run
  to run noise.
