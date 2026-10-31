# ADR 0080: Map Blit panes to Hibr hold-union coordinates

- Status: superseded by ADR 0081
- Note: compatibility projection removed in 0.1.540
- Date: 2026-10-15
- Supersedes: the stock horizontal-only placement of the packaged Blit join launcher
- Superseded by: [ADR 0081](0081-hibr-joins-require-a-2d-union-contract.md)
  and [ADR 0127](0127-native-client-pty-input-and-login-owned-joins.md)

The following is historical rationale, not current implementation guidance.
Hibr now owns the native 2-D union and per-client input rewrite (ADR 0122).
The 0.1.540 source removes the old horizontal slot/primary-input projection;
deployment acceptance is recorded separately in the verification ledger.

## Context

Blit owns the authoritative physical pane topology. A pane is a complete-cell
rectangle with an `(x, y)` origin in the display's logical cell grid. Hibr's
`hold` module owns one virtual union for a desktop and gives each attached
terminal a rectangle in that union. Hibr's stock `desktop --join` path places
every new client at the current right edge. Hibr's desktop implementation also
uses the primary column, but not a primary row, when placing its bar, icons,
and windows. A literal vertical Hibr union therefore puts the desktop bar in
the upper client while the physical primary is lower.

Treating the join as a mirror would hide this mismatch and would create the
wrong desktop semantics. Treating each pane as a separate Hibr desktop would
also violate the one-owner rule. The two systems therefore need an explicit,
cell-coordinate placement boundary.

## Decision

The Blit server passes each pane's deterministic logical Hibr slot to the
Blit-owned launcher. The primary is slot zero and each join occupies the next
horizontal slot in pane-enumeration order, regardless of the physical pane's
row. The launcher then:

1. finds the existing primary client in the named Hibr hold session;
2. reasserts that primary client at logical `(0,0)`; and
3. attaches the join client at its logical horizontal slot with
   `hold attach -m`.

The Hibr source tree is not modified. The native Hibr PTY remains the only
desktop rendering source, and Blit parses each client's local viewport into
its own physical pane surface. The launcher falls back to Hibr's normal
`--join` behavior when it is invoked without Blit's topology environment,
preserving direct Hibr usage.

The coordinates are logical cells, not pixels. Blit's pane partitioner has
already removed incomplete edge rows and columns and owns the rounding and
centering decision. The Hibr slot projection is intentionally separate from
physical `(x,y)` so Hibr's current horizontal desktop contract remains valid.
This is a compatibility projection, not proof that a physically stacked
desktop is correct: input must be translated into the projected union column,
and the Hibr desktop still cannot place its full scene or wallpaper in a true
2-D physical join.

## Consequences

- A 1x2 stack can place the primary at the bottom and the join above it while
  keeping Hibr's primary logical viewport at slot zero; the upper pane is a
  real PTY viewport, not a mirror, but it is not accepted as a complete
  physical desktop until Hibr supports the 2-D contract in ADR 0081.
- A 2x1 or 2x2 topology uses the same deterministic Hibr slot projection
  without a special physical-orientation case.
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
additionally show the upper and lower panes as complete stacked viewports of
one desktop, with physical pointer coordinates matching context-menu/window
coordinates, and record the Hibr `hold clients` coordinates. The current
compatibility projection is not sufficient for that acceptance. Full
spanning-picture acceptance is separately blocked on Hibr's viewport-aware
hold picture propagation, tracked in
[Hibr #238](https://git.home.arpa/itdlabs/hibr/issues/238); the 2-D layout and
input contract is tracked in
[Hibr #240](https://git.home.arpa/itdlabs/hibr/issues/240).
