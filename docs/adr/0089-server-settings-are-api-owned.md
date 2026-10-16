# ADR 0089: server presentation settings are API-owned state

- Status: accepted
- Date: 2026-10-16
- Scope: persisted server presentation policy, topology defaults, and font selection

## Context

Six operational choices were read only from the environment at boot:
`BLIT_RENDER_INTERVAL_MS`, `BLIT_PANE_LAYOUT`, `BLIT_RENDERER`,
`BLIT_KMS_ASYNC_PAGEFLIP`, `BLIT_EVDEV_MOUSE`, and `BLIT_EXPECT_DESKTOP`.
That made the API-driven display server unable to report or change its own
presentation policy, and a supervised restart discarded a font selection and
scale.

## Options considered

1. Keep the environment authoritative. This is simple, but requires a restart,
   hides state from clients, and makes a release unexpectedly restore defaults.
2. Add separate API resources for every variable. This duplicates ownership,
   especially for pane layout, which already belongs to `/v1/topology`.
3. Store the settings in Blit's existing API-owned settings document, with the
   environment as the boot default only when the document is silent.

## Decision

Option 3. `/v1/settings` owns the persisted document. Its `server` object
stores render cadence, requested renderer, direct-KMS asynchronous pageflip
policy, and the empty-output expectation. Its `font` object stores the
selected profile and tenth-step scale. The environment is read once as the
boot default; a stored value wins, and `DELETE /v1/settings` returns to that
default.

Pane layout is a topology setting, not a second topology implementation.
`PUT /v1/topology` is the live owner and persists its layout choice through the
settings document. A settings replacement or reset that would change an
occupied layout is refused with `topology_in_use`. This keeps pane geometry,
session ownership, and pointer hit-testing on one live path.

Renderer selection is startup-applied. `GET /v1/settings` reports both
`renderer_requested` and `renderer_active`; the active value is the backend in
force for the current process, so a stored request does not pretend to have
switched an existing renderer live.

`BLIT_EVDEV_MOUSE` is retired from the API. Stable `/v1/input/devices`
resources and per-device configuration are the supported input ownership
boundary (ADR 0064); a node-path override would duplicate it and would not be
stable across kernel reprobes. The environment remains a compatibility boot
override for deployments that still need it, but it is not persisted.

Authentication, bind address, command policy, update credentials, and test
seams remain boot-only under ADRs 0065 and 0067.

## Consequences

Clients can back up, restore, inspect, and reset presentation policy through a
single revisioned API resource. Server settings that are live-safe apply
immediately; topology changes preserve the existing occupied-pane conflict;
renderer choice takes effect on the next restart and is explicitly reported.
The environment remains useful for first boot and recovery without competing
with an accepted API setting.

## Evidence

The focused `tools/tests/settings_persistence_gate.rb` proves environment
precedence, API replacement, font persistence, restart persistence, reset to
boot defaults, and the pane-layout conflict boundary. `tools/tests/font_api_gate.rb`
proves tenth-step scaling and its centred complete-cell geometry. The API
schema gate validates the corresponding OpenAPI contract.
