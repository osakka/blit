# ADR-0075: Pane grids are quantized inside physical pane regions

- Status: accepted; in use since 0.1.536

## Context

The topology API describes pane bounds in terminal cells, while SDL presents
the enclosing display in physical pixels. A split is a physical operation: a
1x2 layout on a 1920x2160 output has two 1920x1080 physical regions. At scale
3, a 48px cell does not divide 1080px, even though it does divide the whole
2160px output into 45 rows.

The previous split quantized the whole output first and then partitioned its
45-row grid. The resulting 22+23 panes put the first row of the lower pane at
1056px, 24px before the physical hinge at 1080px. A menu row could therefore
be visible on both physical panels.

## Decision

Blit partitions physical pixels first and quantizes each physical pane region
independently:

    region_start(i) = floor(pixel_total * i / parts)
    region_end(i)   = floor(pixel_total * (i + 1) / parts)
    pane_grid       = complete_cells(region_start..region_end)

The complete-cell rectangle is centred inside each physical region. For the
1920x2160, 48px-cell 1x2 example the result is two 80x22 panes, each with
24px of vertical remainder. The top pane's grid ends before the hinge and the
bottom pane's grid starts after it; no terminal row crosses the hinge. The
unused remainder is intentional and is never painted as a partial cell.

Pane `bounds` remain logical terminal-cell bounds for Hibr and PTY sizing.
The API additionally publishes `pixel_region` and a pane-local `cell_grid`,
which are the authoritative physical placement values for rendering and
input. A layout can consequently have fewer total pane rows than the
enclosing output's cell grid: the omitted row is the impossible whole-cell
row that would straddle the physical split.

## Verification

`tools/tests/pane_split_geometry_gate.rb` requests a 1x2 layout whose physical
regions have an odd cell remainder and proves that each pane's grid is
complete, centred, and contained in its physical region. The live API check
must compare pane `pixel_region` and `cell_grid` values with a screenshot
before a joined desktop is accepted as visually complete. Hibr source is not
changed.

## Consequences

The two panes can have the same terminal dimensions even when the enclosing
output has an odd cell count; the physical remainder belongs to the pane
regions, not to a partial terminal row. The intentional pixel margins are
visible in `/v1/topology` and do not represent a renderer fault. Existing pane
attachments still require the normal topology lifecycle: change the layout
before attaching sessions, or stop/detach sessions before a layout change.
