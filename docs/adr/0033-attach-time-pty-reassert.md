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
per-session token and follows only that bounded descendant tree. The delay
lets the child finish startup and open the desktop before Hibr handles the
resize notification. Blit owns this behavior; Hibr source is not modified.

## Consequences

Hibr sessions deterministically enter their normal full redraw path on attach
or restart without signalling stale sessions that reused a numeric ID. No user
input is synthesized, and ordinary terminal sessions use the same PTY contract.
The retries are bounded and stop after the desktop owner has been reasserted.
