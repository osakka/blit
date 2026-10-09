# ADR 0055: The Hibr adapter authenticates with an injected API token

- Status: accepted
- Date: 2026-10-09
- Scope: network authentication, Hibr adapter, terminal session environment

## Context

Authentication is a global gate: `handle_http_request` checks the bearer token
before dispatching any route, so a configured `BLIT_AUTH_TOKEN` protects
`POST /v1/terminal-sessions/{id}/hibr-owner` and the `/v1/stream` upgrade
along with everything else.

The `console-blit` adapter cannot satisfy that gate. It sends exactly one
header pair of its own, the WebSocket upgrade, and no `Authorization` header
anywhere. The `token` it already carries is `BLIT_SESSION_TOKEN`, the Hibr
owner handshake secret from ADR 0051; it travels in a JSON body and answers
"which process owns this session", not "may this caller use the API".

The result is that the two supported configurations are mutually exclusive.
Measured on the live host: with a token configured, the packaged entrypoint
reports `hibr_resize_discovery ... matches=0` for all 32 attempts and then
`hibr_attach_unresolved`, and the session is retired with zero printable
cells. Without a token, the same launch reaches 32,400 cells. An operator who
follows the hardening advice loses the desktop, and the symptom names Hibr
rather than authentication, so the obvious diagnosis is the wrong one.

Every existing live Hibr result in the verification ledger was therefore
gathered against an unauthenticated server, because no other kind of run was
possible.

## Options considered

1. **Accept `BLIT_SESSION_TOKEN` as an API credential.** No new secret and no
   injection. It conflates two different questions in one value, so a
   compromised owner token would become an API credential, and the
   session-scoped handshake would have to be checked before route dispatch
   where the session is not yet known. Rejected.

2. **Require the operator to configure the token in both places.** No code
   change. It makes a working desktop depend on two values agreeing, with a
   failure mode that is silent and identical to the current one. Rejected.

3. **Special-case the packaged Hibr entrypoint and inject only for it.**
   Narrowest exposure, but it makes "which commands receive a credential" an
   implicit policy keyed on a path, invisible in the API and untestable from
   outside. Rejected.

4. **Inject the API token into the launched child's environment and have the
   adapter present it.** Chosen.

## Decision

When `BLIT_AUTH_TOKEN` is configured, Blit sets `BLIT_API_TOKEN` in the
environment of every PTY child it launches. The adapter reads it and sends
`Authorization: Bearer <token>` on its HTTP requests and on the `/v1/stream`
upgrade. When no token is configured the variable is absent and the adapter
sends no header, which keeps the unauthenticated path byte-identical.

`BLIT_API_TOKEN` and `BLIT_SESSION_TOKEN` remain distinct and keep their
separate meanings. The API token authenticates a caller to the gateway; the
session token proves owner identity for one session.

Injection happens with the other integration defaults, before the API's own
`environment` entries are applied. Those entries are applied last with
overwrite, so a client that does not want the credential in a particular
child can set `BLIT_API_TOKEN` to an empty value in the create request.

## Consequences

Authentication and a running Hibr desktop become compatible for the first
time, which makes the Milestone 6 hardening item reachable and lets live Hibr
evidence be gathered against an authenticated server.

The cost is explicit and is the reason this ADR exists: **every program Blit
launches sees the API bearer token in its environment**, not only the Hibr
adapter. This does not widen access for API callers, who must already hold
the token to create a session at all, but it does expose the credential to
the launched program and to anything that can read that process's
environment. A shell session started through the API can print it.

That is accepted here because the alternative designs are worse in ways that
are harder to see, and because the exposure is bounded by an existing
capability: `POST /v1/terminal-sessions` already runs arbitrary commands, so a
caller holding the token can already act with the service's full authority.
The successor design is a per-session credential, scoped to that session's own
routes and expiring with it, which would remove the shared secret from child
environments entirely. It is deliberately not built here; it is a larger
change to the authentication model and belongs in its own cycle.

The token is not logged, not returned by any endpoint, and not recorded in the
diagnostic trace. The child `environment` map is parse-only input and is never
echoed in a session response.

## Evidence

- `tools/tests/network_auth_gate.sh` starts a server with a token configured
  and asserts that an authenticated session's child receives a non-empty
  `BLIT_API_TOKEN` while the unauthenticated configuration does not set it.
  The check reports presence only and never prints the value.
- The adapter path requires Hibr and is therefore proven on the live host or
  in the QEMU fixture, not by a local gate: with `BLIT_AUTH_TOKEN` set, the
  packaged `blit-session.hibr` entrypoint must reach `owner_registered: true`
  and a non-zero printable-cell count. The release manifest records that run.
