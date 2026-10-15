# ADR 0080: Map Blit panes to Hibr hold-union coordinates

- Status: accepted
- Date: 2026-10-15
- Supersedes: the stock horizontal-only placement of the packaged Blit join launcher

## Context

Blit owns the authoritative logical pane topology. A pane is a complete-cell
rectangle with an `(x, y)` origin in the display's logical cell grid. Hibr's
`hold` module owns one virtual union for a desktop and gives each attached
terminal a rectangle in that union. Hibr's stock `desktop --join` path places
every new client at the current right edge, which is correct for a normal
side-by-side terminal but not for a topology such as one display split into a
top join and a bottom primary.

Treating the join as a mirror would hide this mismatch and would create the
wrong desktop semantics. Treating each pane as a separate Hibr desktop would
also violate the one-owner rule. The two systems therefore need an explicit,
cell-coordinate placement boundary.

## Decision

The Blit server passes the pane's logical row and column to the Blit-owned Hibr
launcher. For a join it also passes the primary pane's logical row and column.
The launcher then:

1. finds the existing primary client in the named Hibr hold session;
2. moves that primary client to Blit's primary-pane origin; and
3. attaches the join client at Blit's join-pane origin with `hold attach -m`.

The Hibr source tree is not modified. The native Hibr PTY remains the only
desktop rendering source, and Blit parses each client's local viewport into its
own pane surface. The launcher falls back to Hibr's normal `--join` behavior
when it is invoked without Blit's topology environment, preserving direct
Hibr usage.

The coordinates are logical cells, not pixels. Blit's pane partitioner has
already removed incomplete edge rows and columns and owns the rounding and
centering decision. Hibr therefore receives exactly the same coordinate system
that Blit uses for hit-testing and presentation.

## Consequences

- A 1x2 stack can place the primary at the bottom and the join above it in one
  Hibr union; the upper pane is a real desktop viewport, not a mirror.
- A 2x1 or 2x2 topology uses the same mechanism without a special horizontal
  case.
- Hibr's primary client remains the desktop's primary display, so its menu,
  wallpaper, input routing, and window-placement semantics stay Hibr-owned.
- A join still cannot write its Blit surface directly; it must render through
  its Hibr PTY.
- The launcher must reject a topology join when the held session has no primary
  client, rather than guessing an offset or starting another desktop.

## Verification

The source gate parses the launcher with the installed Hibr interpreter and
asserts the complete move/attach boundary is present. The API join gate proves
distinct PTY-backed surfaces and pane-hit-tested input. Live acceptance must
additionally show the upper and lower panes as the corresponding stacked parts
of one desktop and record the Hibr `hold clients` coordinates.
