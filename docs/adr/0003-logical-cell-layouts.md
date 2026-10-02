# ADR 0003: Logical cell-based pane layouts

- Status: accepted
- Date: 2026-10-02

## Context

Hibr desktop sessions are text terminals, but Blit may span displays, split a
display into two or four panes, and change font size or display mode. A layout
contract based directly on pixels would couple Hibr to SDL geometry and make a
font change an application-level event.

## Decision

Blit layouts are expressed in logical terminal cells. A layout policy defines
the arrangement of panes, such as a two-column or two-by-two grid. Blit uses
the active display mode and font profile to calculate the available cell grid,
derives each pane's cell bounds, assigns opaque pane identifiers, and resizes
the attached Hibr session.

Pane identifiers and split geometry are Blit-owned. Hibr discovers them from
the topology API; it does not invent names or calculate physical placement.

Font metrics and physical pixel bounds remain Blit rendering concerns. They
are not part of Hibr's placement contract.

## Consequences

- Hibr receives stable cell dimensions and does not need SDL or pixel
  knowledge.
- Font or display-mode changes recalculate pane dimensions and emit ordinary
  session resize events.
- Pane policies can preserve proportions across displays with different
  resolutions and font profiles.
- Exact cell bounds are derived state and must be reported by Blit with the
  active layout revision.

## Rejected alternative

Pixel-defined panes were rejected because they make text sizing nondeterministic
and force Hibr to duplicate display and font-metric knowledge.
