# ADR 0138: Select the presentation backend through saved settings

- Status: implemented; physical comparison pending
- Superseded by [ADR 0184](0184-sdl-is-removed.md): SDL is removed; native KMS is the only presentation path.
- Date: 2026-10-19
- Issue: Blit104; related ADRs0089,0137

## Decision

The existing revisioned `/v1/settings` document owns `server.display_backend`,
an optional enum `kms` or `sdl`. Missing values use the startup
`BLIT_DISPLAY_BACKEND` default; saved values win. `BLIT_SETTINGS=ignore` remains
the recovery override. Reset removes the saved preference and restores the
startup default. No service environment edits or second configuration store
are needed to select the three comparison paths.

Saving a selection never destroys current outputs or sessions. The existing
supervised `/v1/control/restart` applies it at the next process startup. SDL's
renderer remains selected by `server.renderer`; the bare-console video driver
remains a deployment default. Effective settings report requested backend,
actual backend identity and `presentation_restart_required` (also true for a
changed explicit SDL renderer). A reported active backend may be unavailable;
runtime health remains authoritative for readiness. Existing observable direct
KMS fallback is retained; a fallback differing from the saved request keeps
the restart flag true rather than claiming the requested selection succeeded.

Invalid backend values/types return400 before settings/state mutation. The
settings document is saved durably through the existing atomic writer. Display
layouts, font settings and login-owned joins use the same saved document and
existing startup reconciliation. Backend-scoped transient display/pane IDs
must not be treated as portable connector identity.

## Alternatives and verification

Editing systemd/environment through a terminal session duplicates configuration
ownership. In-process DRM/SDL handoff destroys live resources and introduces
thread/context and login lifetime hazards. Both are rejected for this delivery.

The API gate verifies deferred apply, invalid input rejection, durable save,
startup precedence, reset and API survival when the selected KMS device is
absent. Software/GLES composition gates remain separate. Physical switching
requires a bounded comparison window and restoration of the saved baseline.
