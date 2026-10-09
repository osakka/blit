# ADR 0046: Free panes do not swallow display-attached input

- Status: Accepted
- Date: 2026-10-09

## Context

Topology panes outlive terminal sessions so a layout can be reused. A released
pane therefore has `surface_id: null`. The pointer hit-test used to stop when
it found that free pane, even though no session could receive the event. A
primary Hibr desktop attached directly to a display (without `pane_id`) then
rendered correctly but received no clicks or motion reports whenever a stale
free pane covered the display.

## Decision

Hit-testing first honors an attached session in a matching pane. If the pane
is free, it continues to the display-attached session fallback; a free pane is
layout state, never an input sink. The fallback remains bounded to the
matching output and surface geometry, so pane splits retain precedence.

## Verification

`tools/tests/pointer_target_gate.rb` verifies both the normal pane route and a
released-pane followed by display-attached-session route. Live evidence on
0.1.381 reproduced the failure with `target_session=0`; the fix is included in
0.1.382 and must be revalidated on the live host before release acceptance.
