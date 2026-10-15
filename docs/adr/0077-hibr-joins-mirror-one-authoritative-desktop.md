# ADR 0077: Hibr joins mirror one authoritative desktop

- Status: accepted
- Date: 2026-10-15
- Scope: Hibr `desktop --join`, surface ownership, graphics, and input routing

## Context

Hibr's `console-blit` adapter publishes the primary desktop's cells and
Kitty/Sixel graphics through Blit's API. A `desktop --join` process has a
separate Blit surface, but it is not a second desktop state owner. Treating
that surface as an independent external owner leaves the joined pane blank:
the Hibr hold stream is not the rendering source for the API-backed adapter,
and it cannot safely compete with the primary's cell and graphics state.

The product requirement is one Hibr desktop spanning auxiliary panes. The
primary and every join must therefore share one rendering authority while
retaining independent pane geometry and hit testing.

## Options considered

1. Parse Hibr's joined PTY stream into each joined surface. Rejected: this
   creates a second terminal-state path, cannot reproduce API-owned graphics
   reliably, and reopens the owned-surface collision closed by ADRs 0057 and
   0063.
2. Make each join an independent external surface. Rejected: it permits
   multiple writers and is the blank joined-surface behavior observed on the
   live integration.
3. Mirror the primary surface in Blit and canonicalize joined input to the
   primary owner. Chosen.

## Decision

- A Hibr `desktop --join` session is forced to `surface_mode: mirror`.
- The primary Hibr session remains the only surface writer. Blit copies its
  committed cells, cursor, style, and dimensions into the joined surface at
  the presentation boundary. A mirror is read-only through the public cell
  mutation route and returns `409 mirror_surface_read_only` for direct writes.
- Graphics are not duplicated. The renderer selects the primary session's
  graphics record when composing a joined surface, so Kitty/Sixel images and
  their placements are visible in the joined pane without a second image
  ownership model.
- Pointer and keyboard events hit-test against the joined pane but publish
  the primary session ID to the Hibr input stream. The trace retains both
  `target_session` and `input_session` so routing is observable.
- Blit does not modify Hibr source or ask Hibr to serialize its desktop into a
  second ANSI stream. Pane geometry and placement remain Blit's authority;
  desktop/window semantics remain Hibr's authority.

## Consequences

This closes the first join gate: the joined pane can show the primary desktop
and its graphics, and context-menu input can reach the same Hibr desktop
owner. It also gives the API an unambiguous ownership model: one source,
multiple presentations.

The current mirror is a presentation copy of the primary viewport. True
union-canvas window placement across independently sized panes remains a
separate acceptance item; it must add an explicit coordinate transform rather
than silently changing this ownership rule.

## Verification

`tools/tests/hibr_join_mirror_gate.rb` proves the mode, committed-cell mirror,
read-only enforcement, and joined-pane-to-primary input routing using the TCC
server and a no-Hibr fixture. Physical wallpaper, context-menu, and
multi-monitor acceptance remain pending until the exact package is deployed
and observed on the authorized host.
