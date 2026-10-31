# ADR 0037: Own Hibr adapter streams by session

- Status: accepted

## Context

The Hibr adapter uses a WebSocket for input events. A session can be deleted
while its adapter process or socket is still unwinding. An unowned stream can
then remain active after its Blit surface is gone, making stale input and stale
desktop state indistinguishable from a rendering defect.

## Decision

The Blit-owned Hibr adapter identifies its stream with `owner_session_id` in
the subscription message. Blit records that owner and closes the stream when
the corresponding session exits or is deleted. Unowned API streams remain
independent and are not closed by terminal-session lifecycle operations.

## Consequences

Session deletion becomes a complete resource boundary: PTY, surface, input
routes, and adapter stream are retired together. This prevents stale Hibr
connections from feeding a later session or keeping the stream count alive.
