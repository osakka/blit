# ADR 0236: display mode selection, tried and kept

- Status: accepted
- Note: Gated on the DRM mock; a live mode change on the owner's display is tracked in Gitea #60.
- Date: 2026-11-19
- Scope: `PATCH /v1/displays/{id}` `configuration.mode`, blit_kms output
  rebuild, the settings document's `displays` section (Gitea #60)

## Context

The modes a connector offers were reported but not writable (ADR 0064):
a mode changes the display's pixel size, and so every surface on it, and a
Hibr desktop's cells come from its owner (ADR 0063). Gitea #60 asked for a
resize notification and acknowledgement first, and rollback, because a
controller can blank a screen it cannot see.

Two facts changed that. Native KMS already rebuilds an output at another
size when a monitor is replugged at another resolution: the reconcile
resizes the display's panes and their sessions and moves the topology
revision. And Hibr confirmed (hibr session, 2026-11-19) that a desktop
follows such a resize with no protocol of its own: hold gets the client's
new size, the console refits its grid, the desktop lays out again -- the
path a replug and a resized terminal take, which its suites drive.

## Options considered

1. A resize/ack protocol with the owner before the mode is applied --
   new wire on both sides for what the resize already carries; the ack
   would prove the desktop repainted, not that the monitor shows anything.
2. Set the CRTC's mode from the HTTP handler -- bypasses the output
   lifecycle (buffers, the GPU compositor's surfaces, the zoom scene) that
   hot-plug owns, and duplicates it.
3. A connector's wished mode, honoured wherever an output is brought up,
   applied by rebuilding the output through the hot-plug path; the change
   on trial, reverted by itself unless kept; saved only when kept.

## Decision

Option 3.

- blit_kms keeps a wish per connector name: `blit_kms_wish_mode`. Output
  activation honours it over the preferred mode; the reconcile keeps an
  output only while it has the mode it would be given now.
  `blit_kms_apply_modes` re-runs the reconcile on the last snapshot, so a
  live output whose mode is not its wish is rebuilt as a replug would
  rebuild it. The displays' order is restored (`blit_kms_keep_order`), so
  the first display stays the first even across a failed attempt and its
  revert.
- `configuration.mode` takes `auto`, `WxH` (the highest refresh at that
  size), `WxH@Hz` (within half a hertz) or `keep`. Only a listed mode is
  accepted. Applied, it is on trial: `configuration.mode_trial` names what
  it reverts to and when, and after 15 s (`BLIT_DISPLAY_MODE_TRIAL_MS`)
  without `keep` it reverts by itself, logged `display_mode_reverted`. A
  mode the hardware refuses is put back at once, a 422 saying so.
- A kept mode is saved under the connector name; at start it is wished
  before the outputs first come up, so the start is one modeset at that
  mode. A kept `auto` removes it. A stored mode the connector no longer
  lists leaves it at its preferred mode.
- The display reports the `mode` capability where it has a KMS connector.

## Consequences

Any controller -- the API, a Hibr control-panel row -- can offer the modes
the display lists. A wrong choice costs at most the trial period. Zoom (ADR
0204) applies over the new mode. Both compositors are rebuilt with the
output, as on hot-plug. A refresh-only change at the same size is a mode
like any other. Hibr's two open points -- a window larger than the new
screen pulled back inside it, and a non-primary display's resize in a
multi-display held session -- are being checked on its side (holdpix).

## Evidence

- `display_mode_gate.rb`, the actual server with only libdrm mocked (two
  connectors, more modes, one the "monitor" refuses), on the CPU and the
  GLES compositor:
  - refusals: bad, unlisted, `keep` with nothing on trial;
  - a mode on the second display: bounds and refresh, the modeset at it,
    the order and the other display unchanged, the GLES compositor still
    active, a screenshot at the new size, the session in its pane resized
    and not restarted; not kept, reverted, the session back;
  - zoom 200 kept over a mode;
  - kept: stays past the trial, saved;
  - refused by the monitor: put back, said, order kept;
  - restart: the kept mode, in one modeset per display;
  - `auto` kept: the preferred mode, the setting removed.
- kms_hotplug_gate, kms_hotplug_cycles and display_layout_gate pass with
  the new keep rule.
- Hot-plug (amended 2026-11-19, after 0.3.125): a display unplugged during
  a trial has its wish put back, so a replug comes back at the previous
  mode (display_mode_gate fails on 0.3.125 there); a stored mode applied
  inside the reconcile is recorded only, not rebuilt from within it (both
  displays replugged in one scan pass; not shown to fail before).
- Not yet: a live mode change on the owner's hardware (needs the owner,
  after hibr's window-containment fix).
