# ADR-0075: Pane grid partitions own every terminal row and column

Status: Accepted

## Context

The topology API describes pane bounds in terminal cells, while SDL presents
the enclosing display in physical pixels. The display's pixel dimensions are
first quantized by the current physical cell metrics (ADR 0073). A long
display can therefore have an odd number of terminal rows: at scale 3, a
1920x2160 display with 8x16 base cells has 80x45 cells.

The previous split calculated one floored pane size and reused it for every
row and column. A 45-row 1x2 layout consequently made two 22-row panes and
left one unowned terminal row at the bottom. That appeared as a black band and
made the visible split look vertically displaced.

## Decision

Blit partitions the quantized terminal grid by integer boundaries:

    start(i) = floor(total * i / parts)
    end(i)   = floor(total * (i + 1) / parts)
    extent   = end(i) - start(i)

Pane origins use start; pane widths and heights use extent. This makes the
panes contiguous, gives every terminal row and column to exactly one pane,
and assigns an unavoidable odd-cell remainder to the final row or column.
For the live 80x45 1x2 example the result is a 80x22 top pane and a 80x23
bottom pane at y=22. The bottom pane remains the explicit primary target; the
top pane is the explicit desktop --join target.

The API continues to report terminal-cell bounds, not fractional pixels. A
client that needs exact physical centering must use the reported cell metrics
and accept that an odd terminal grid cannot have two equal integer heights.
Blit does not create a second rounding rule or silently leave a pixel/cell
band outside the topology.

## Verification

tools/tests/pane_split_geometry_gate.rb sets an odd-height cell grid, requests
a 1x2 layout, and proves that the pane boundaries are contiguous, the final
pane owns the remainder, and the original topology is restored. The live API
check must report the same invariant before a joined desktop is accepted as
visually complete. Hibr source is not changed.

## Consequences

The two panes can differ by one terminal row or column when the grid is odd;
that is intentional and visible in /v1/topology. There is no black terminal
band. Existing pane attachments still require the normal topology lifecycle:
change the layout before attaching sessions, or stop/detach sessions before a
layout change.
