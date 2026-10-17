# ADR 0123: centred grids and complete font rasters

- Status: accepted
- Date: 2026-10-17
- Issues: [Blit #97](https://git.home.arpa/itdlabs/blit/issues/97),
  [Blit #98](https://git.home.arpa/itdlabs/blit/issues/98)

## Evidence

The live 0.1.535 1920px-wide pane contains 240 cells of 8px each. Its
authoritative centred grid begins at x0, but presentation adds another4px,
placing the right edge at1924 and clipping4px. The API geometry regression
fails before correction. The same surplus offset affects rendering/input.

The font builder rasterizes 16px TTF symbols of10px outline width into an
8px canvas. The screen's diamond and17 Control Panel symbols match the
packaged bitmap exactly: the clipping happens before SDL receives the font.
The old diamond bitmap fails the complete/symmetric regression.

## Decision

Keep the existing complete-cell partition/centering as the only owner of
placement. Remove the extra presentation inset on both renderers and input,
keeping `surface_inset_px` as a compatible diagnostic field with value0.
Do not reserve another border, shrink PTY dimensions, alter font scale, or
move Hibr's logical clients. Existing240x67 joined viewports stay240x67.

Rasterize fallback outlines completely, including negative bearings, at the
largest native point size whose ink fits8x16. Centre that ink horizontally
and retain the terminal baseline, without cropping or nonuniform squeezing.
Keep valid Terminus glyphs and deterministic block/box geometry. A7px
symmetric diamond has a spare column for bold's overstrike; both regular and
bold forms are tested. The PSF remains generated from this one builder.

## Verification and limits

The actual API edge regression is red before/green after. Independent complete
font-mask comparisons cover12 fallback symbols; diamond regular/bold symmetry
and existing manifest coverage are separate assertions. Renderer, terminal,
font, geometry and pointer gates must pass before live deployment. The live
check must verify all pixels in the last physical column and selected glyphs
against their regenerated bitmap, not just confirm that the server starts.

No Hibr source changes. A supervised update restarts the service; preserve
SDL GL and the lower login pane, then attach the native upper join only after
authentication. This does not silently implement the lifecycle work in #96.
