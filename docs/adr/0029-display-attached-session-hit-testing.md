# ADR-0029: Hit-test display-attached sessions

## Status

Accepted

## Decision

Pointer events target display-attached sessions without requiring a pane. The
hit test uses the same deterministic vertical stacking origin as the SDL
renderer. Pane-attached sessions continue to use pane geometry first.

## Context

The primary Hibr desktop is created with a display target and an explicit
surface size, while auxiliary desktops may use Blit panes. The previous hit
test only searched panes, so the primary desktop rendered correctly but
silently discarded all physical button reports.

## Consequences

Primary and other display-attached sessions receive click, drag, and wheel
reports at their rendered cell coordinates. Pane layouts retain their existing
routing and take precedence.
