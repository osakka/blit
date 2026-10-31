# ADR-0076: Center the complete-cell display grid

- Status: accepted

## Context

Display modes are measured in pixels, while terminal panes and PTYs are
measured in whole cells. Font scaling therefore often leaves pixels that are
too small to contain another complete cell. Previously the cell grid started
at the display origin, which made the unused remainder one-sided: the first
cell was aligned but the last edge contained an unexplained strip. Renderer
origins, pane origins, and pointer hit-testing could then disagree about the
usable rectangle.

## Decision

Blit computes one `BlitCellGeometry` per display from the active cell metrics:

    columns = floor(display_width / cell_width)
    rows    = floor(display_height / cell_height)
    origin_x = floor((display_width - columns * cell_width) / 2)
    origin_y = floor((display_height - rows * cell_height) / 2)

Only the complete-cell rectangle is part of the terminal grid. Pane bounds
partition that grid, and every surface origin and pointer hit-test includes
the same centred origin. The API publishes the result as `outputs[].cell_grid`
so clients can distinguish intentional pixel margins from a rendering fault.

This does not stretch, crop, or invent partial cells. It also does not alter
Hibr's display arrangement: Hibr remains the authority for `--join` placement
and desktop semantics, while Blit remains the authority for physical pixels,
cell geometry, and pane mapping.

## Verification

The cell geometry gate must cover a scale that leaves both horizontal and
vertical remainders, assert the published grid and centred origins, and prove
that a surface is painted at those origins. The existing odd-grid pane gate
must continue to prove contiguous whole-cell partitions. Live acceptance
must compare `/v1/topology` with a screenshot and pointer hit-test evidence.

## Consequences

At exact multiples there is no visible change. At other sizes the unused
pixels are balanced around the grid, and the first and last complete cells
remain equally placed. API clients must use `cell_grid.origin_*` when mapping
pixel coordinates; they must not reconstruct an origin from a hard-coded
inset or from unrounded font metrics.
