# ADR 0035: Clear external surfaces when their PTY exits

- Status: accepted

## Context

An API-launched Hibr process can be killed outside Blit's DELETE endpoint.  In
that case the session becomes `failed`, but its last external surface remained
attached to the SDL output and continued to be presented.  The physical screen
therefore showed a stale Hibr desktop after Hibr had exited.

## Decision

When Blit reaps a session process, it clears the active surface, closes its
logical frame, hides its cursor, disables input routes targeting that session,
and schedules one full blank presentation.  The failed session record remains
available for diagnostics.

## Consequences

Killing Hibr can no longer leave stale desktop pixels or routes active.  The
cleanup applies to externally terminated sessions as well as normal process
exit and does not modify Hibr.
