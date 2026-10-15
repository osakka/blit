# ADR-0073: Keep font scaling in the physical cell-metric boundary

Status: Accepted

## Context

ADR 0047 makes font profile and logical scale runtime API controls. The first
implementation reported the scaled cell size and resized pane-attached PTYs,
but the SDL renderer still used a renderer-wide scale transform while its
cached scene and display-attached Hibr surface remained physical-sized. On the
live GLES2 path this allowed the pointer to change size while terminal glyphs
remained at the base 8x16 raster size. API metadata was therefore able to say
that scale 3 was active while the screenshot disproved it.

## Decision

There is one shared cell-metric boundary. `runtime_terminal_cell_width()` and
`runtime_terminal_cell_height()` produce the physical pixel dimensions used by
SDL drawing, direct KMS drawing, surface origins, pointer hit-testing, PTY
pixel sizes, and display-session reflow. SDL renderers remain at a 1.0
transform because scene textures are physical-sized output textures. A
display-attached Hibr session without an explicit pane is resized to the
display's current grid when the font scale changes. Hibr source is not changed.

The software pointer is positioned in physical output coordinates and its
arrow dimensions follow the selected scale. This keeps pointer location,
terminal cells, and input hit-testing in the same coordinate system.

## Verification

`tools/tests/font_api_gate.rb` must prove both API state and pixels at scale 1
and scale 2, including a display-attached surface. The SDL glyph, render-state,
cell-paint, pointer-geometry, and full renderer suites must remain green.

## Consequences

Changing the font scale changes visible glyph and cell size rather than merely
changing a cursor transform. Existing cell content is preserved over the
overlap during reflow, and the smaller display grid is sent to the PTY/Hibr
owner through the existing resize boundary. Rotation and screenshot capture
continue to operate on physical scene textures.
