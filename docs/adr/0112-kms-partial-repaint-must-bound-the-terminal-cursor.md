# ADR 0112: Direct-KMS partial repaints must bound the terminal cursor

- Status: accepted
- Date: 2026-10-17
- Scope: direct-KMS terminal-cell composition and retained-scene damage

## Context

The live complaint is stale and bleeding pixels during fast movement. A source
comparison was required before changing another damage rectangle or graphics
protocol rule.

Kitty retains terminal and graphics state, marks its graphics layers dirty on
mutation, rebuilds render data from that retained state, and presents the
resulting frame from its renderer. xterm's `ScrnRefresh` receives an explicit
cell rectangle and repaints only that region. suckless `st` keeps dirty rows
and its draw path renders only those rows into a double-buffered drawable.
These implementations do not allow an unrelated terminal element to write
outside the dirty region.

Blit's SDL renderer already applies the same rule to the terminal cursor: it
draws the cursor only when its row intersects the surface damage rows. The
direct-KMS `draw_view()` path did not. It drew the terminal cursor after the
damaged cell loop without checking either the damaged row or damaged columns.
Thus a partial repaint could mutate pixels outside its declared damage, even
though the scene and page-flip boundary were otherwise retained.

This is independent of Hibr. Hibr supplies terminal state and protocol bytes;
Blit owns the presentation boundary and must keep each renderer's writes
inside its damage contract.

## Decision

Treat the terminal cursor as view-local content. In direct KMS, draw it only
when its row intersects the current damage rectangle and its one- or two-cell
cursor footprint intersects the damaged columns. A full redraw has an
unrestricted damage rectangle and therefore retains the existing behavior.

The KMS composition gate includes a regression case that first establishes a
scene with a known background, then repaints an unrelated cell and asserts
that the cursor cell is unchanged. No Hibr code, API contract, or graphics
protocol behavior changes.

## Consequences

The direct-KMS renderer can no longer introduce a cursor-shaped write outside
the cell damage it is composing. This removes one proven source of artifacts
and keeps KMS aligned with the existing SDL path and the retained/dirty-region
model used by Kitty, xterm, and simple terminals.

This does not yet prove the complete Hibr desktop visually. The corrected
binary must still be deployed to 10.54 and tested with a controlled desktop,
wallpaper, saver movement, pointer movement, and screenshot/diagnostic trace.

## Evidence

- Kitty source: <https://raw.githubusercontent.com/kovidgoyal/kitty/master/kitty/graphics.c>
- Kitty terminal state handling: <https://raw.githubusercontent.com/kovidgoyal/kitty/master/kitty/screen.c>
- xterm explicit cell-region repaint: <https://sources.debian.org/src/xterm/278-4/screen.c>
- suckless `st` dirty-row and double-buffered rendering: <https://git.suckless.org/st/file/st.c.html>
- `tools/tests/kms_compose_gate.sh` passes `KMS_COMPOSE_PASS` with the new
  cursor-boundary regression.
- The TCC-built renderer/terminal suite passes `RUN_GATES_PASS pass=50
  skip=0 fail=0`.
