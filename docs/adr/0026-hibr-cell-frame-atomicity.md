# ADR 0026: Commit external Hibr cell frames atomically

Status: Accepted

## Context

The Hibr `console-blit` provider writes a full logical redraw through bounded
HTTP requests. Presenting each request immediately exposes intermediate
wallpaper, menu, and window states. This appeared on live displays as a saver
or wallpaper briefly overlapping the desktop.

## Decision

`PUT /v1/surfaces/{surface_id}/cells` accepts `presentation` with the values
`immediate`, `defer`, and `commit`. Deferred updates advance the canonical
surface revision and damage state but are excluded from rendering. The commit
update applies the final bounded chunk and makes the complete surface eligible
for presentation. The Hibr adapter sends `defer` for every non-final chunk and
`commit` for the final chunk.

## Consequences

The visible surface changes at a logical Hibr flush boundary instead of at an
HTTP chunk boundary. The canonical API snapshot may contain a deferred frame
before commit, which is intentional and observable. A client that begins a
frame must eventually commit it; destroying its session releases the surface
and therefore cannot leave a permanent transaction behind.

The existing `immediate` default preserves compatibility for ordinary API
clients. This decision covers cell surfaces; pixel-stream presentation remains
governed by its existing frame protocol.
