# ADR 0033: Reassert external Hibr sessions after PTY attach

Status: Accepted

## Context

Blit creates Hibr sessions through a PTY and correctly supplies the initial
window size.  That is not sufficient for an already composed desktop: Hibr's
documented redraw path is a PTY resize notification followed by
`console reassert` and a complete layout redraw.  Without an attach-time
notification, the external surface can contain only startup damage until an
unrelated user action happens.  Injecting Escape is incorrect because Escape
has desktop meaning.

## Decision

When a terminal session first becomes active, Blit schedules bounded delayed
attach reasserts. It reapplies the canonical PTY geometry and sends SIGWINCH
to the PTY foreground process group, with a process fallback. Hibr's hold
layer can retain Blit's exported identity on an owner while launching the
desktop in a descendant group, so Blit also exports a unique per-server,
per-session token and follows only that bounded descendant tree. The optional
Blit-owned `console-blit` provider may additionally register its live owner
PID through the session-scoped token endpoint; this removes `/proc` discovery
timing from that provider path. The delay lets the child finish startup and
open the desktop before Hibr handles the resize notification. Blit owns this
behavior; Hibr source is not modified.

The launcher process is not the lifetime owner for Hibr desktop sessions:
`hold new -d` can exit after handing the desktop to its detached owner. Blit
therefore keeps an Hibr session in the running state after launcher exit,
continues the bounded owner transaction after the launcher PTY closes, and
reports the owner match and registration state in `hibr_attachment`.

If the canonical geometry is already installed when the held desktop is found,
Blit performs one bounded PTY geometry transition to a one-row-shorter size and
immediately restores the canonical size. This is a notification transaction,
not a surface resize: it forces a real SIGWINCH path when the kernel suppresses
same-size `TIOCSWINSZ`, while the surface remains at its authoritative geometry.

## Consequences

Hibr sessions deterministically enter their normal full redraw path on attach
or restart without signalling stale sessions that reused a numeric ID. No user
input is synthesized, and ordinary terminal sessions use the same PTY contract.
The retries are bounded and stop after the desktop owner has been reasserted.
The transition is scoped to sessions whose token-bearing Hibr descendants have
been found. Match counts remain process counts; diagnostic telemetry does not
treat the forced notification as an extra Hibr process.
