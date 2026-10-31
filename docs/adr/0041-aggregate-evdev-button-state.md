# ADR 0041: Aggregate button state across evdev mice

- Status: accepted

## Context

Blit intentionally opens every usable relative evdev pointer so multiple
physical devices remain available. A single global button bitmask cannot
represent that input: releasing a button on one device can incorrectly release
the logical button while another device still holds it. That produces lost
drag state and inconsistent terminal mouse reports.

## Decision

Maintain a button mask for each opened evdev mouse and derive the logical state
as their bitwise aggregate. Dispatch a logical transition only when the
aggregate changes. Device removal releases only buttons no remaining device
holds. Duplicate transitions are counted as
`evdev_mouse_button_state_coalesced` for diagnostics.

## Consequences

Multiple mice share one predictable logical pointer without losing held-button
state. The QEMU uinput fixture holds the same button across two devices and
requires two coalesced transitions. Hibr and terminal protocol handling remain
unchanged.
