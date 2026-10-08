# ADR 0031: Make Hibr `console flush` the atomic frame boundary

Status: Accepted

## Context

The Blit-owned Hibr display adapter previously flushed its pending queue when
the queue reached its capacity. That silently committed a partial desktop
redraw before Hibr called `console flush`. A wallpaper or menu clear could
therefore become visible as an incomplete frame.

## Decision

`console flush` is the only external Hibr cell-frame commit. Queue pressure
must never call the transport flush routine. The adapter uses a bounded queue
of 262144 cell mutations, enough for the supported 240x135 logical surface
including repeated writes within one redraw. If that bound is ever exceeded,
the operation fails explicitly and the current frame remains uncommitted.

The adapter continues to send `presentation: defer` for bounded HTTP chunks and
`presentation: commit` only on the final chunk of the Hibr flush. Blit’s
canonical surface and renderer remain the sole state owner.

## Consequences

Intermediate clear/redraw states cannot be committed by buffer management.
Memory use is bounded and failure is visible to Hibr instead of corrupting the
display. The terminal PTY path remains available for ordinary terminal clients;
Hibr desktop integration must use the external adapter surface mode so its
explicit flush boundary is preserved.
