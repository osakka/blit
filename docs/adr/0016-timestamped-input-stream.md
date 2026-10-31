# ADR 0016: Timestamp every streamed input event

- Status: accepted

## Context

Interactive terminal games, arcade programs, and emulators need to measure
input-to-presentation latency. Gamepad events already carried a monotonic
receipt timestamp, but keyboard and pointer events did not. That made latency
diagnostics and client-side input prediction inconsistent.

## Decision

Every event on the canonical `input` WebSocket stream carries `time_us`, a
monotonic timestamp captured when Blit receives the event. The timestamp is a
receipt timestamp, not a claim about device hardware time. Clients must use it
for ordering and latency measurement within one Blit process, and must not
compare it across hosts.

The change applies equally to keyboard, pointer motion, pointer buttons,
pointer wheel, and gamepad events. PTY escape-sequence delivery remains the
terminal compatibility path; the streamed event is the API-observable input
path for games and diagnostics.

## Consequences

- Game clients can calculate input age against frame/presentation timestamps.
- Input observability is uniform across device classes.
- The API does not yet promise a hardware-origin timestamp or synchronized
  timestamps across multiple Blit servers.

## Verification

- OpenAPI validation requires `event.time_us`.
- TCC compilation and terminal conformance must pass.
- QEMU input fixtures must continue to pass for keyboard, pointer, and
  gamepad streams.
