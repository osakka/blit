# ADR 0018: Expose bounded stream back-pressure telemetry

## Status

Accepted.

## Context

The network stream is part of the game, input, and diagnostics control plane.
Blit must never block its single gateway/render loop on a slow TCP reader. The
implementation therefore uses nonblocking WebSocket sends and removes a
stream when its next bounded event cannot be accepted. Its receive side has a
bounded, observable buffer for fragmented frames and pixel data.

## Decision

`/v1/diagnostics/runtime` reports:

- `stream_queue_bytes`: currently buffered WebSocket receive bytes;
- `stream_queue_peak_bytes`: maximum receive-buffer occupancy since startup;
- `stream_dropped_events`: broadcast event deliveries rejected by a stream;
- `stream_backpressure_disconnects`: streams removed because a nonblocking
  send encountered a full or failed socket.

There is intentionally no unbounded outgoing queue. A slow client is isolated
by disconnecting it, and the diagnostic counters make that behavior visible.

## Consequences

Clients can distinguish an idle stream from a congested or discarded stream.
Pixel-frame producers must use the accepted/receipt protocol and may reconnect
after a back-pressure disconnect. The counters are process-local and reset on
restart.

## Verification

- The OpenAPI runtime schema requires all four counters.
- The network fragmentation/back-pressure gate and TCC build must pass.
- Pixel-frame and input-stream fixtures must continue to pass without blocking
  the render loop.
