# ADR-0047: API-selectable fonts and logical scale

Status: Accepted; scale range superseded by ADR 0074

## Context

Blit owns the terminal cell grid and must expose display appearance through its
versioned API. A font change affects glyph coverage, pixel cell dimensions,
pane geometry, PTY pixel size, Kitty/Sixel placement, and the Hibr attach
contract. Environment variables are useful for bootstrapping but are not a
runtime control plane.

## Decision

Expose `GET /v1/fonts` and `PUT /v1/fonts` in the OpenAPI contract. The
endpoint lists known installed profiles, reports the active profile and
logical cell size, and accepts a bounded scale from 1 through 3. Applying a
configuration is a Blit operation: it reloads the selected font, recreates
SDL glyph atlases, recomputes panes, resizes attached terminal surfaces and
PTYs, and schedules a complete redraw. Hibr code is not changed.

Profile paths are fixed, package-owned allowlisted paths; arbitrary paths are
not accepted through the network API. A missing profile and an invalid scale
are typed API problems.

## Consequences

The scale-2 test doubles an 8x16 font to 16x32 logical pixels and reduces the
number of terminal columns and rows available on a fixed display. Existing
cell content is preserved over the overlap during resize. Font selection is
observable and reversible without restarting Blit.

The left-edge `0`/block observation remains a separate evidence-driven
investigation; this ADR does not hide or rewrite terminal cells to mask it.
