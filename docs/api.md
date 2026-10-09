# Blit API guide

This is the human-facing guide to the API. The normative contract is always
[`openapi/blit-0.1.yaml`](../openapi/blit-0.1.yaml); this guide explains the
resource model and safe ordering of calls without duplicating every schema.

## Endpoint and authentication

The default development endpoint is `http://HOST:8080`. All public resources
are versioned under `/v1`; `/v1/version` is the first request a client should
make and returns both `server_version` and `api_version`.

When `BLIT_AUTH_TOKEN` or `BLIT_AUTH_TOKEN_FILE` is configured, send:

```sh
curl --fail --header 'Authorization: Bearer TOKEN' \
  http://HOST:8080/v1/server
```

The test-LAN package may bind to `0.0.0.0`, but that is not an authentication
or firewall policy. Use a restricted management network and a real token. The
update credential is separate from the API bearer token and is accepted only
by the package-update headers.

## Discovery order

Clients should discover before mutating:

1. `GET /v1/version` — identity and compatibility gate.
2. `GET /v1/server` — capabilities and resource links.
3. `GET /v1/graphics`, `/v1/displays`, `/v1/audio` — hardware inventory.
4. `GET /v1/input/devices` and `/v1/input/routes` — input sources and policy.
5. `GET /v1/topology` and `GET /v1/fonts` — logical layout and cell geometry.
6. `GET /v1/diagnostics/runtime` — readiness, cadence, and bounded logs.

Do not infer capabilities from a screenshot or from a device filename. A
resource is usable only when its API state and capability fields say it is.

## Core lifecycle

The normal terminal lifecycle is:

```text
discover -> create session -> place surface -> route input
         -> consume revisions/events -> delete session -> verify cleanup
```

Create a session with `POST /v1/terminal-sessions`. The response identifies
the session, PTY process, surface, display/pane placement, and the
`hibr_desktop` binding. Use `PATCH /v1/terminal-sessions/{id}` for placement
and input policy, `POST` on the same resource for terminal input, and
`DELETE` to stop the process group and release its surface.

All mutable resources expose revisions. Send `If-Match` when a client has a
known revision; a stale mutation is a typed conflict, not an implicit merge.
Use `Idempotency-Key` when retrying a mutation whose duplicate application
would be harmful.

## Hibr desktop orchestration

Hibr remains the owner of desktop semantics. Blit owns display placement,
surfaces, input routing, lifecycle observation, and the API contract. Create
one primary Hibr desktop session first; auxiliary sessions use the same Hibr
held-session name and `--join`. Blit rejects an orphan join and a duplicate
primary with `409` problem responses. The server therefore has one explicit
Hibr owner at a time rather than accidentally spawning competing desktops.

The API does not rewrite Hibr configuration or patch Hibr source. A client
should keep the returned session IDs and inspect `hibr_desktop.role` before
starting auxiliary joins. Delete all Blit sessions at the end and confirm
`/v1/diagnostics` reports zero sessions and surfaces.

Primary and join lifetimes are intentionally different: the primary may keep
its Blit session running while Hibr's detached owner restarts after the
launcher child exits; a joined client is an attachment, so a dead join is
reported as `failed` and its surface/routes are released. A replacement join
can then be created against the still-running primary.

## Displays, topology, and fonts

`GET /v1/displays` reports physical outputs, active modes, supported modes,
bounds, and presentation state. `PUT /v1/topology` changes the logical
placement using the schema's revision and idempotency controls. Pane splits are
logical Blit layout resources; they do not create extra physical monitors.

`GET /v1/fonts` lists profiles and the active cell size. `PUT /v1/fonts` can
select `font_id`, `scale` from 1 through 3, or both. Blit recalculates pane
geometry and resizes attached PTYs. This is a display geometry operation; it
does not change Hibr source or terminal protocol state.

## Terminal surfaces and atomic updates

The surface cell snapshot is authoritative. Use `GET /v1/surfaces/{id}/summary`
for cheap health checks and `GET /v1/surfaces/{id}/cells` for repair or full
state. Send revisioned updates to the same cells route. The surface response
contains the resulting revision and commit receipt; do not treat a partially
received stream message or a screenshot as a committed frame.

`/v1/frames` accepts an explicit coherent-frame request. The WebSocket stream
can carry surface deltas, terminal output, frame events, diagnostics, and
bounded pixel frames. Pixel frames use `pixel_frame_begin` metadata followed by
binary data and a final completion message. Back-pressure and dropped-event
counters are visible in runtime diagnostics.

## Input

Physical keyboards, pointers, and gamepads are discovered through
`/v1/input/devices`. Bind a source to a session with
`PUT /v1/input/routes/{route_id}`. `POST /v1/input/inject` is deliberately
synthetic and is useful for deterministic diagnostics; it does not claim that
a physical device works. Mouse button state is aggregated across opened
evdev pointers, and the input trace identifies the source and dispatch path.

## Diagnostics and screenshots

Use `GET /v1/diagnostics` for current health/resource state and
`GET /v1/diagnostics/runtime` for counters. The runtime response includes
present calls, measured frame/render intervals, missed deadlines, input
dispatch counters, terminal bytes, surface/frame commits, queue pressure, and
a bounded event log. `GET /v1/diagnostics/trace?limit=32` provides correlated
input, surface, and render-path events.

Capture one display with:

```sh
curl --fail --output display.bmp \
  http://HOST:8080/v1/diagnostics/screenshots/display-1
```

Use `?cached=1` to inspect the already-composed scene without forcing a fresh
redraw. A screenshot is evidence of pixels only; pair it with runtime
counters, surface snapshots, and trace events before diagnosing a boundary.

## Effects, overlays, and control

`GET/PUT /v1/effects` manages bounded named effects such as `scanlines`.
`POST/DELETE /v1/overlays` manages explicit graphic overlays. Effects and
overlays are downstream of terminal state and cannot silently replace it.

`POST /v1/control/shutdown` is an asynchronous clean stop. It terminates
managed child process groups and releases outputs; it cannot restart itself.
Use the host supervisor to start it again. `/v1/control/restart` requests an
orderly in-process restart. Package installation uses the separate
`/v1/control/update` flow and must be verified through
`/v1/control/update/status` and `/v1/version`.

## Errors and compatibility

Failures use `application/problem+json` with a stable `code`, HTTP status,
title, detail, and structured context where applicable. Clients must branch on
the code, not parse human prose. Common codes include `route_not_found`,
`invalid_request`, `stale_revision`, `unsupported_capability`,
`hibr_desktop_exists`, and `hibr_desktop_owner_missing`.

Unknown response fields should be ignored. Resource IDs are opaque. Events and
snapshots carry revisions/sequences so clients can detect gaps and resync from
the authoritative GET endpoint. A schema or semantic breaking change requires
a new API version and ADR; changing a renderer implementation does not.

## Safe client checklist

- Pin and verify `api_version` before use.
- Discover capabilities and output readiness.
- Keep resource IDs and revisions; use idempotency keys on retries.
- Treat `409`, `429`, renderer failures, and unavailable outputs as recoverable
  typed states where the problem code permits.
- Resync authoritative state after a stream gap.
- Delete temporary sessions, routes, and overlays in cleanup paths.
- Do not log bearer tokens, update tokens, package bytes, or private paths.
