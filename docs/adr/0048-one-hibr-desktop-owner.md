# ADR-0048: One primary Hibr desktop owner

Status: Accepted

## Context

Blit exposes a generic PTY session API, while the Hibr desktop is a held
logical session with one primary owner and optional `desktop --join` clients.
Without an explicit boundary, two valid API requests could launch two primary
desktop owners, or a join could start before its owner. That produces exactly
the duplicated, blinking, and confusing desktop state the integration is
intended to prevent.

## Decision

Blit classifies the Hibr desktop entrypoints (`desktop`, `hibr desktop`, and
the packaged `blit-session.hibr` invocation) and extracts the Hibr held-session
name, defaulting to `blit`. It enforces these rules at session creation:

1. only one primary owner may be running for a held-session name;
2. a `--join` client is accepted only while that primary owner is running;
3. a duplicate primary returns `409 hibr_desktop_exists`;
4. an orphan join returns `409 hibr_desktop_owner_missing`;
5. ordinary non-Hibr terminal sessions remain unrestricted by this policy.

The API reports `hibr_desktop.role` (`none`, `primary`, or `join`) and the
held-session name on every terminal-session response. Hibr remains unchanged
and remains the owner of desktop state; Blit only enforces the process-boundary
invariant.

## Consequences

The multi-display topology may still have multiple PTYs and Hibr processes,
but they are provably one logical desktop: one primary plus explicit joins.
Tests must use separate isolated server instances or explicit join clients,
not multiple primary invocations. The bounded API argument buffer is cleared
before every parse so an earlier `--join` cannot leak into a later request.
