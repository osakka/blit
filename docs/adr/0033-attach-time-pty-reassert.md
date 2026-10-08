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

When an external session first becomes active, Blit schedules one delayed
attach reassert.  It reapplies the canonical PTY geometry and sends SIGWINCH
to the PTY foreground process group, with a process fallback.  The delay lets
the child finish its startup and open the desktop before Hibr handles the
resize notification.  Blit owns this adapter behavior; Hibr source is not
modified.

## Consequences

External Hibr sessions deterministically enter their normal full redraw path on
attach or restart.  No user input is synthesized, and ordinary terminal
sessions are unaffected.  The operation is one-shot per session activation.
