# ADR 0109: Hibr terminal default colours cross the API boundary

## Status

Accepted.

## Context

Hibr's wallpaper compositor deliberately leaves some cells unstyled so a
pixel graphic below the text can remain visible. Blit's KMS compositor must
therefore know the session's effective terminal floor; an unstyled cell is not
the same thing as an opaque black cell.

The PTY parser can learn defaults from `DT_INK`/`DT_FACE` supplied in the
child environment or from OSC 10/11. Hibr's `console darkdefault` is a
separate dim/shadow target; it is not the terminal's OSC 10/11 default and
must not be inferred as one.

## Decision

The revisioned `PUT /v1/surfaces/{surface_id}/cells` update accepts optional
`terminal_default_foreground` and `terminal_default_background` fields. They
are session defaults, not per-cell styles. The update is the single atomic
mutation boundary: changing either value invalidates the complete surface.

An API-owned provider may send the actual terminal defaults in that same
update, including a defaults-only update when no cells changed. The
Blit-owned `console-blit` adapter keeps Hibr's `console darkdefault` local for
shadow calculations and does not translate it. Blit exposes the effective and
configured values in
`GET /v1/terminal-sessions/{id}` as `terminal_defaults`.

No Hibr source or installed Hibr desktop file is changed. Native PTY clients
continue to use their existing environment and OSC 10/11 negotiation. The
adapter path has an explicit API contract rather than inferring a theme from
whatever cell colour happens to be most common.

## Consequences

- An API-owned Hibr surface can change theme face colours without a black
  floor or a second, guessed state model.
- A defaults-only update is revisioned and full-damage, so the visible scene
  changes coherently.
- Diagnostics can distinguish an unset default from an explicitly configured
  black default.
- A native PTY Hibr session that never supplies `DT_FACE` or OSC 11 remains
  diagnostically visible as an unconfigured black-floor session; it must not
  be called visually valid until its terminal contract is established.
