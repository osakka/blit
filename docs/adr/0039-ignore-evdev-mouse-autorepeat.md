# ADR 0039: Ignore evdev mouse-button autorepeat

Status: Accepted

## Context

Linux `EV_KEY` events use value `0` for release, `1` for press, and `2` for
autorepeat. Blit previously treated every nonzero mouse-button value as a new
press. A device or input stack that emits autorepeat could therefore produce
duplicate terminal clicks or leave the aggregate button state inconsistent.

## Decision

The raw evdev mouse path accepts only values `0` and `1` for `BTN_LEFT`,
`BTN_MIDDLE`, and `BTN_RIGHT`. Value `2` and any other noncanonical value is
ignored and counted in `evdev_mouse_button_repeats_ignored` diagnostics. API
injection and SDL fallback semantics are unchanged.

## Consequences

Mouse-button presses and releases retain their protocol meaning across all
evdev devices, and repeat handling is observable in the runtime diagnostics.
The QEMU uinput fixture emits a value-2 event and requires the counter to
increase. This fixes duplicate-click input without modifying Hibr or terminal
protocol code.
