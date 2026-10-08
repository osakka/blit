# ADR-0011: Event-driven input-device discovery

## Context

Blit owns the presentation and input event loop. A periodic synchronous scan
of `/dev/input/event*` ran in that loop. On the authorized host, the scan
could block for approximately one second, producing the observed one-frame-
per-second behavior even though rendering and `select()` themselves were
fast.

## Decision

Use a nonblocking inotify watch on `/dev/input` to request device discovery
only when devices are created, removed, moved, or have relevant attributes
changed. Device reconnect and loss handling remains in the single gateway
owner, but discovery is no longer performed on every presentation tick.

## Consequences

- Presentation scheduling remains available at the display's refresh cadence
  while input topology is unchanged.
- Device loss and reconnect remain observable through the existing diagnostics
  and input-device events.
- An input route's `enabled` field is operator configuration and survives a
  temporary device loss; the route becomes effective again when that device
  class reconnects. Session lifecycle remains authoritative: a route targeting
  a stopped or failed PTY is disabled with that session.
- The implementation depends on the Linux `/dev/input` topology used by the
  direct-KMS deployment; non-Linux backends must provide an equivalent
  event-driven discovery mechanism.
- The fix is measurable through `last_evdev_scan_ms`, `max_evdev_scan_ms`,
  loop-gap counters, and the presentation-rate window in
  `/v1/diagnostics/runtime`.

## Verification

The exact 0.1.268 TCC binary passed the Hibr VKMS input fixture with keypad
soak, multi-mouse, stream, gamepad, device-loss/reconnect, and render-soak
markers. Hibr source was not changed.
