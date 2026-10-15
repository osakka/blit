# ADR 0071: Historical evdev diagnostics carry device identity

- Status: accepted
- Date: 2026-10-15
- Scope: `/v1/diagnostics/runtime` physical input evidence

## Context

KVM and USB re-enumeration can change `/dev/input/eventN` node numbers while
Blit is running. The runtime already reports the path and age of the last raw
mouse and keyboard event, but a path is not a stable identity. A live capture
therefore produced an apparently contradictory result: the last event named
`event12`/`event16`, while the current inventory contained different event
nodes. The counters were real; the diagnostic representation did not say that
the path was historical.

## Decision

Keep the existing path and age fields for compatibility, and add these fields
to the same runtime performance object:

- `last_evdev_mouse_device_id`
- `last_evdev_mouse_device_present`
- `last_evdev_keyboard_device_id`
- `last_evdev_keyboard_device_present`

The ID is the input-table resource ID associated with the event source at the
time Blit read the event. The present flag is the table's current availability
for that ID. If the source cannot be resolved because its node has already
been retired, the ID is empty and the flag is false. Clients must treat the
`eventN` path as historical and use the ID/presence pair when correlating a
capture with `/v1/input/devices`.

This is additive within API version `0.1`; it does not alter input ownership,
Hibr delivery, device selection, or the meaning of the existing counters.

## Consequences

Input troubleshooting can distinguish:

1. no raw event during the capture (the age remains unchanged);
2. an event from a currently present device (ID is non-empty and present);
3. an event from a removed KVM/USB generation (empty or not-present source).

The path remains useful for kernel-level correlation, but it is no longer
treated as a resource identity. The OpenAPI contract, runtime implementation,
and regression gates are updated together.
