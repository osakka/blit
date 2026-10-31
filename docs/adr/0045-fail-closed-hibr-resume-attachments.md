# ADR 0045: Fail closed on unresolved Hibr resume attachments

- Status: accepted

## Context

`desktop --resume` attaches to an already-held Hibr session. The API-created
PTY can be correctly sized while the actual Hibr desktop remains attached to
another, stale PTY. Reporting the API session as `running` in that condition
produces a misleading partial or blank display and makes later input and
resize symptoms look like renderer failures.

## Decision

For a Hibr `desktop` invocation containing `--resume` (or `-r`), Blit keeps
the existing bounded owner-discovery/reassert window. If all attempts find no
token-bearing Hibr owner, Blit terminates only the API-launched attach client,
marks that API session `failed`, retires its surface, and emits the structured
`hibr_attach_unresolved` diagnostic. The existing held Hibr processes are not
signalled, restarted, or modified.

The session API reports `hibr_attachment` with the requested mode, reassert
attempt count, owner-match count, and unresolved state. Fresh primary and
`desktop --join` launches retain the existing behavior.

## Consequences

An ambiguous resume fails visibly and safely instead of presenting stale
content. Recovery remains explicit: the caller must inspect Hibr's own held
session lifecycle, then start a fresh primary or resume the authoritative
held client. Blit remains standalone and does not modify Hibr source or its
state files.
