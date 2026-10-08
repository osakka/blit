# ADR-0023: Bounded stream service for refresh fairness

- Status: accepted
- Date: 2026-10-08

## Context

The API streaming plane carries graphics frames for games and emulators. A
fast client can place several complete WebSocket messages in one kernel read.
Draining that read without returning to the main loop lets the client replace
the newest image repeatedly while delaying SDL/KMS presentation, PTY service,
and input dispatch. Returning after a bounded batch introduces a second edge:
messages left in Blit's user-space receive buffer must be serviced even when
the kernel socket is no longer readable.

## Decision

`service_stream()` processes one complete WebSocket message per service turn.
Fragmented messages remain in the stream receive state and continue on a later
turn. The main loop checks for buffered stream data before selecting and
services it outside the `ready > 0` branch. An `EAGAIN` from `recv()` therefore
parses already-buffered bytes instead of discarding the service turn.

The renderer remains refresh-aware and presents the newest complete image;
intermediate frames may be coalesced when the producer exceeds the display's
mode. This is the required transport behavior for a networked display server:
the API remains responsive, presentation is not starved by ingress, and the
client receives no false guarantee that every accepted frame was scanned out.

## Consequences

- A continuously producing client cannot monopolize the single gateway loop.
- PTY output, evdev input, diagnostics, and presentation receive service time
  between graphics messages.
- Buffered WebSocket data is drained without a busy loop or dependence on a
  new kernel readiness edge.
- Accepted-frame counts and presented-frame counts remain intentionally
  distinct in diagnostics.

## Verification

The TCC-built 0.1.281 binary passes the local paced pixel gate with 30 frames
accepted and 29 rendered. The authorized physical 1920×2160@60 host passes the
same no-ACK paced gate with 30 frames accepted, 24 rendered, zero pixel-frame
errors, and zero missed refresh deadlines. The full terminal conformance gate
also passes; Hibr source remains unchanged.
