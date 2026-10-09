# ADR 0051: Fail closed when a Hibr desktop owner is not registered

Status: Accepted

## Context

Every API-launched Hibr desktop client receives a private Blit session token.
The Blit console adapter posts that token and its PID to the session's
`hibr-owner` endpoint. The registration is the authoritative boundary between
the Hibr desktop process and the Blit surface; bounded `/proc` discovery is
only a compatibility fallback for resize/reassert signaling.

A fresh primary that never registered an owner could previously remain
`running` after the bounded reassert window. Its PTY might still produce cell
updates, making a missing adapter, stale provider, or wrong Hibr installation
look like a renderer or font defect.

## Decision

All API-launched Hibr desktop clients—primary, `--join`, and `--resume`—must
register an owner or be matched to a token-bearing Hibr process during the
bounded startup window. If neither happens after the maximum attempts, Blit
marks the session `failed`, emits `hibr_attach_unresolved`, retires its
surface, and disables its input routes. Blit terminates only the API-launched
client process group; it does not signal or modify unrelated Hibr state.

The public session response continues to expose `hibr_attachment` with the
registration and bounded-discovery counters. A `running` Hibr session now
means that the integration boundary is observable, not merely that a child
process exists.

## Consequences

Misconfigured or stale Hibr launches fail early and diagnostically instead of
rendering partial content. Valid adapter-backed primary and join clients are
unchanged. The rule is implemented entirely in Blit and does not require a
change to Hibr source or desktop scripts.
