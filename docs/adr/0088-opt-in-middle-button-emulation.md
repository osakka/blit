# ADR 0088: Opt-in deferred middle-button emulation

- Status: accepted

## Context

Some pointers have no physical middle button. The conventional replacement is
left-plus-right: hold the first press briefly, and turn a second press inside
the window into one middle press. A naïve implementation delays the first
press and only checks for the second button when another input event arrives.
That loses the required behavior when the pointer becomes idle, because the
event loop can sleep indefinitely.

## Decision

Blit exposes `middle_button_emulation` as a per-pointer configuration object:

```json
{"enabled":false,"window_ms":150}
```

The feature is disabled by default. `window_ms` is bounded to 50..500 ms and
defaults to 150 ms. A left or right press is pending only on its own device.
The other button within the window emits one middle press; releasing either
button emits one middle release. A pending press whose deadline expires is
emitted as its original button. Motion is never held back.

The main loop clamps its `select()` wait to the earliest pending deadline and
flushes expired presses before processing newly-ready input. This preserves
the event-driven loop while giving the deferred action a measured deadline.
The same path is used by evdev input and API injection with `device_id`, so a
gate can test the behavior without hardware. Configuration changes flush any
old pending press before replacing the policy, preventing a stuck or silently
lost button.

## Consequences

- Controllers must explicitly enable the feature; ordinary clicks are never
  delayed unless a device opts in.
- The pending state is per device, so two pointers cannot complete each
  other's chord.
- The API response always exposes the implemented setting for pointer devices;
  the settings document omits it at its default to remain sparse.
- A 50..500 ms bound makes the timer behavior predictable and prevents a
  controller from creating an unbounded click latency.

## Verification

`tools/tests/pointer_settings_gate.rb` asserts default-off behavior, idle
deadline flushing, motion during a pending press, left-plus-right middle
translation, middle release, and configuration persistence. The API schema,
TCC build, renderer/terminal gate suites, and package artifact gate remain
release gates.
