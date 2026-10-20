# ADR0151: colour glyph artwork uses the fitted text em

- Date:2026-10-20
- Status: implemented; package/live visual approval pending
- Tracking: Blit107; follows ADR0147/0150

Modern previously scaled colour artwork to fill the entire allocated cell
rectangle, whereas outline text was fitted to font ascent/descent and advance.
The cell includes line spacing. At22px, the production raster gives H13px
ink height, menu pencil11px and bell20px. The bell fits its legitimate two
cells: this is an optical sizing policy issue, not terminal-width corruption.

Bound colour artwork height by the outline's already-resolved em size,
preserve aspect ratio and centre it inside the unchanged cell rectangle.
Do not alter cell dimensions, font-size settings, Unicode width, hit testing,
emoji source data, outline glyphs or Nerd glyphs. Colour-only contexts with
no outline retain the cell-height bound. Do not add a second user size policy
or a bell-specific codepoint exception. Native and SDL consume one raster.

At Modern22px the bell ink becomes17x18 rather than20x20, while retaining
the same22x22 two-cell allocation. A new regression fails before the change
and passes afterward for this optical bound and unchanged advance. Existing
8..64px raster/blend and backend pixel tests remain required, including the
bell itself. tools/probes/font_glyph_metrics.c reports production-raster ink
bounds without modifying the host; these are not live desktop screenshots.
Operator aesthetic approval remains distinct from geometric acceptance.
