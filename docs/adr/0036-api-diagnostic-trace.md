# ADR 0036: Expose a bounded API diagnostic trace

Status: Accepted

## Context

Counters and screenshots show symptoms but cannot correlate a physical input
event with its route, Hibr surface commit, and renderer path.  That made live
pointer artifacts and missing clicks difficult to distinguish from Hibr
behavior.

## Decision

Blit exposes `GET /v1/diagnostics/trace`, a bounded chronological ring of
timestamped events for keyboard keys, pointer motion/buttons, surface
presentation modes, and cached versus full render paths.  The trace is
diagnostic-only and is owned by Blit; it does not alter input, rendering, or
Hibr.

The HTTP representation defaults to the 32 newest matching events and accepts
an optional `limit=1..128` query parameter. This is deliberately smaller than
the in-memory ring by default so a diagnostic request cannot create an
unnecessarily large response on the single-threaded gateway.

## Consequences

An operator can correlate the real display experience with API evidence and
capture a deterministic incident bundle. The ring is bounded so tracing
cannot grow memory without limit, and the response is independently bounded so
observability cannot materially interfere with the live display.
