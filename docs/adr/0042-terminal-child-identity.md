# ADR 0042: Explicit terminal child identity

- Status: accepted

## Context

Blit owns KMS, evdev, and the API, but Hibr desktop state and image files belong
to a desktop user. Running Hibr as the `blit` service account or as root makes
the desktop select the wrong configuration or refuse to decode a user's PNG or
JPEG. Setting `HOME` or `USER` in a child environment does not change Unix
identity and is not a valid fix.

## Decision

The Blit gateway service runs as root because it owns the display/input
boundary. `POST /v1/terminal-sessions` accepts an optional `user` field. When
present, the forked child initializes that account's supplementary groups,
primary group, uid, gid, `HOME`, `USER`, and `LOGNAME` before `exec`. The
gateway does not change identity after the child starts.

Omitting `user` preserves the service identity. Deployments must authenticate
the API and restrict `BLIT_COMMAND_ALLOWLIST` before exposing this capability
outside a trusted test LAN.

## Consequences

- Hibr can consume the selected user's desktop configuration and image files.
- KMS and evdev remain owned by one gateway rather than competing desktop
  processes.
- A root service increases the consequence of an API compromise, so the
  unauthenticated LAN configuration is explicitly temporary and not a release
  security posture.
- Child identity is observable and testable through the process behavior, not
  inferred from environment variables.
