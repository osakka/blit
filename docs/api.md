# Blit API guide

## Native console ownership (candidate0.1.587)

`GET /v1/diagnostics/console` reports Blit's native VT ownership, VT number,
active state, previous VT, actual Linux keyboard mode/read errno, keyboard
muting, registered recovery
process and main-loop watchdog timeout (normally10s, bounded120s during the
privileged package-install exchange). `owner: unmanaged` is not
evidence of SDL isolation; it means this native owner is not managing a VT.
The endpoint is read-only and uses the normal API authentication policy.

Native KMS reserves its own unused text/automatic VT, never the active login
VT, and mutes its kernel keyboard handler
while presenting, and restores it when releasing the VT or stopping. Physical
Ctrl+Alt+F1..F12 switches VTs; API-injected chords do not switch the host.
While away, physical events are discarded, not replayed on return; API input
and PTY/session work remain available. Acquisition requires DRM master and
requests a full repaint. Failure leaves diagnostics available or stops the
server rather than claiming usable presentation.
On exit it returns to the previous VT if its own is still active. Console
descriptor/mute loss stops presentation rather than reporting stale ownership.
This requires privileged Linux VT access; a profile lacking it must remain
API-only with a startup error, not silently share a console keyboard.

The recovery process handles main-process death and an expired heartbeat.
It does not cover a kernel/GPU hang or killing the whole recovery group.
`evdev_exclusive: false` is deliberate: keyboard muting covers all keyboards
without grabbing one of a combined device's separate evdev clients. It does
not prevent a separately running gpm from reading pointer events. SDL daemon
ownership and pointer exclusivity remain117 acceptance work, not implied by
native tests. See [ADR0170](adr/0170-native-console-keyboard-ownership.md).

An SDL startup failure cannot silently enter native KMS without VT ownership;
select the native backend explicitly and restart to acquire it safely before
device-I/O threads start. This does not change a successfully running SDL
renderer or give SDL a competing VT owner.

## Presentation configuration ownership (0.1.563)

Saved `/v1/settings` is the canonical configuration. Its
`document.server.display_backend` takes precedence over the startup fallback
`BLIT_DISPLAY_BACKEND`. Valid backend values are `kms` (native DRM/KMS) and
`sdl`. `kmsdrm` belongs to `SDL_VIDEODRIVER`, not to Blit's backend selector.

Read `effective.presentation_configuration` to distinguish the backend source,
environment validity/override, renderer source and actual SDL video driver.
These fields are derived observations, never a second writable configuration.
The existing effective requested/active fields and restart-required flag tell
clients whether the saved choice has been activated. Native KMS reports the
SDL driver as `not-applicable`; retaining an SDL renderer preference does not
activate SDL. Invalid defaults without a saved backend block display startup,
log `invalid_environment_backend`, and leave the API available for repair.

Change intent through the settings API, save it using the storage API, and
restart presentation. Editing an environment file requires a service restart
to reload it; an in-process restart inherits the original environment.


This is the human-facing guide to the API. The normative contract is always
[`openapi/blit-0.1.yaml`](../openapi/blit-0.1.yaml); this guide explains the
resource model and safe ordering of calls without duplicating every schema.

## Contract-first client rule

Treat `/v1/version` and the OpenAPI document as a compatibility handshake,
not as optional metadata. Before issuing a mutation, a client should:

1. verify the returned `api_version` is supported;
2. discover the capability and revision of the target resource;
3. send the schema-defined body with `If-Match` and an `Idempotency-Key` when
   the operation is retryable; and
4. resynchronize from the authoritative `GET` endpoint after a conflict,
   stream gap, reconnect, or unknown resource event.

The guide gives safe ordering and examples. It is deliberately not a second
schema: field names, enums, status codes, limits, and binary message framing
come only from the OpenAPI document.

## API contract in one page

Blit exposes three kinds of operations:

| Kind | Examples | Client rule |
| --- | --- | --- |
| Discovery | `/version`, `/server`, `/displays`, `/topology` | Read before mutation; treat capabilities as runtime facts. |
| Resource lifecycle | sessions, surfaces, routes, overlays, fonts, effects | Keep returned IDs/revisions; use preconditions and idempotency keys. |
| Observation/control | stream, diagnostics, screenshots, shutdown/restart/update | Observe asynchronous work; use typed problem codes on failure. |

The API is intentionally not a remote shell. Terminal bytes go through the
terminal-session input subresource, display state goes through surfaces and
frames, and lifecycle control goes through explicit control resources. This
keeps input, presentation, and process ownership diagnosable.

### Resource ownership at a glance

Each resource has one canonical owner. Clients orchestrate resources through
the API; they must not recreate an owner in a wrapper, shell script, or second
desktop process.

| Resource | Canonical owner | API responsibility |
| --- | --- | --- |
| API identity and capabilities | Blit server | Discover `/version`, `/server`, and hardware before mutation. |
| Terminal bytes and emulation state | Blit terminal session | Write through the session input subresource; observe surface revisions. |
| Desktop semantics and `desktop --join` | Hibr | Start one primary and attach joins; Blit observes ownership and placement. |
| Cell surface and pane placement | Blit | Commit revisioned cells, map panes, resize PTYs, and route input. |
| Physical scanout and refresh scheduling | Blit renderer/backend | Report output modes, cadence, missed deadlines, and failures. |
| Client event/frame observation | Blit stream | Deliver ordered events and bounded pixel frames; clients resync over HTTP. |

This is an ownership guide, not a second wire contract. The OpenAPI schema
defines the exact fields and operations. Blit never patches Hibr or treats a
successful Hibr command as proof that pixels are already visible.

### HTTP and WebSocket are complementary

HTTP is the authoritative control and repair plane. WebSocket is the ordered
observation plane:

```text
HTTP GET state -> HTTP mutation -> response/revision -> WebSocket observation
                                                \-> HTTP GET repair after gap
```

The stream is not a second database. After a missed sequence, unknown ID, or
reconnect, fetch authoritative state and resume from its revision. Pixel-frame
messages are bounded transport objects, not a replacement for terminal cell
state.

### Required headers and response handling

For JSON mutations send `Content-Type: application/json`. For retryable
mutations send a stable `Idempotency-Key`; for revisioned resources send the
revision using the schema-defined `If-Match` precondition. If authentication
is enabled, requests carry `Authorization: Bearer TOKEN`.

| Outcome | Meaning | Correct action |
| --- | --- | --- |
| `2xx` | Accepted or completed according to that operation | Persist IDs/revisions; observe asynchronous work. |
| `409` | Ownership, revision, or lifecycle conflict | Re-read authoritative state; do not blindly retry the same body. |
| `404` | Unknown route or resource | Verify `/v1/version`, then check the schema and opaque ID. |
| `429` | Bounded queue/back-pressure limit | Apply bounded backoff and inspect runtime counters. |
| `4xx/5xx` problem | Typed failure | Branch on `code`, preserve safe context, and record correlation data. |

Never infer presentation from request latency. A session input receipt means
the PTY write was accepted; a frame is complete only when the surface/frame
revision or stream event confirms it.

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

### The version handshake is mandatory

Every client begins with `GET /v1/version`. The response identifies both the
running server and the API contract:

```json
{"server_version":"0.1.409","api_version":"0.1"}
```

`server_version` answers “which binary is serving this request?”;
`api_version` answers “which schema family may I use?”. A client must reject
an unsupported API version before mutating state. Record both values with the
host, display mode, and time in every incident report.

### Four API planes

| Plane | Endpoints | Meaning |
| --- | --- | --- |
| Discovery | `/version`, `/server`, `/graphics`, `/displays`, `/audio`, `/fonts`, `/topology` | What exists and what it can do now |
| State and control | `/terminal-sessions`, `/surfaces`, `/frames`, `/effects`, `/overlays` | What Blit owns and how it changes |
| Input and observation | `/input/*`, `/stream`, `/diagnostics/*` | What entered, what was rendered, and what was measured |
| Supervision | `/control/shutdown`, `/control/restart`, `/control/update/*` | How an operator stops, restarts, or replaces the service |

These planes are related but not interchangeable. A successful terminal-input
receipt proves that bytes entered the PTY path; it does not prove that a
desktop reacted, that a surface committed, or that a pixel was presented. Use
the corresponding revision, trace, runtime counter, and screenshot to prove
each later boundary.

## Discovery order

Clients should discover before mutating:

1. `GET /v1/version` — identity and compatibility gate.
2. `GET /v1/server` — capabilities and resource links.
3. `GET /v1/graphics`, `/v1/displays`, `/v1/audio` — hardware inventory.
4. `GET /v1/input/devices` and `/v1/input/routes` — input sources and policy.
5. `GET /v1/topology` and `GET /v1/fonts` — logical layout and cell geometry.
6. `GET /v1/diagnostics/observation` — one canonical read-only state dump.
7. `GET /v1/diagnostics/runtime` — readiness, cadence, and bounded logs.

Do not infer capabilities from a screenshot or from a device filename. A
resource is usable only when its API state and capability fields say it is.

## Presentation configuration ownership

`GET /v1/settings` is authoritative for saved presentation intent.
`document.server.display_backend` accepts `kms` (native DRM/KMS, no SDL
rendering) or `sdl`. Saved values override environment startup defaults;
changing `/etc/blit/blit.env` does not erase an existing saved choice.
`BLIT_DISPLAY_BACKEND` uses that same `kms`/`sdl` vocabulary. The different
variable `SDL_VIDEODRIVER=kmsdrm` selects SDL's console driver and does not
select Blit's native backend. `server.renderer` applies only to SDL.

Read `effective.display_backend_requested`, `display_backend_active`,
`renderer_active` and `presentation_restart_required`, and runtime
`render.direct_kms`, `video_driver`, `renderer`. Save via the revisioned
settings API, then request `/v1/control/restart` to apply presentation changes.
Do not maintain competing writable backend preferences in a control panel.
Current561 silently mishandles invalid environment backend names and the UI
does not show source/layers clearly; issue108 tracks correction, not a promise
that invalid values such as `kmsdrm` or `ksmdrm` are accepted.

Pane IDs are backend-scoped. On a deliberate backend switch, resolve the same
connector/ordinal and update the saved primary reference, rather than leaving
a stale SDL pane ID to fall back to the first native pane. See ADR0138.

## Hibr login manager

`/v1/login` is the single Blit-owned login boundary. It is separate from
ordinary terminal-session creation so there cannot be two login screens or an
ambiguous restart owner.

```sh
# Start the configured Hibr login screen on the first discovered pane.
curl --fail --header 'Content-Type: application/json' \
  --request POST http://HOST:8080/v1/login --data '{}'

# Observe the login process and the authenticated child session.
curl --fail http://HOST:8080/v1/login

# Stop and disable the login manager.
curl --fail --request DELETE http://HOST:8080/v1/login
```

Blit runs its packaged login entrypoint as its root child because Hibr's PAM
flow needs that boundary. The entrypoint loads Hibr's `sys` module and sources
the installed Hibr login script; Hibr's `auth run` then executes the selected
desktop or shell as the authenticated user. Blit never reads the password and
never changes the Hibr source tree. When the login process exits after logout,
Blit retires its PTY/surface and starts a fresh login screen after a bounded
delay. The
`login_manager` marker in the returned terminal session distinguishes this
supervised boundary from an ordinary PTY session.

### Authenticated identity (0.1.538)

The `identity` object in `GET /v1/login` distinguishes the login process from the user selected
by its post-authentication hook. Before authentication it reports
`state: waiting`, reason `awaiting_authenticated_hook`, with null UID/GID/PID
and session kind. The default Blit-owned hook uses a private inherited channel;
Blit verifies the kernel sender credentials and ancestry without an API token
or any public registration endpoint. A verified example is:

```json
{"state":"verified","reason":"kernel_credentials_verified",
 "uid":1000,"gid":1000,"sender_pid":12345,"session_kind":"desktop"}
```

The PID identifies the handoff sender, **not** the live held desktop owner.
Verification is not desktop readiness: the selected session can also be
`shell`. Native owner association is exposed separately by `login.desktop`;
0.1.543 auxiliary reconciliation requires that fresh association before launch.
Destroying/replacing the supervised login discards this identity.
No active login reports `unavailable/no_active_login`; custom session commands
are unchanged and report `unavailable/custom_session_command`. Setup failures
report `unavailable/handoff_setup_failed`. Rejected messages expose null
identity fields and a reason (`root_sender`, `foreign_sender`,
`invalid_message`, `invalid_credentials`, `truncated_message` or
`receive_failed`). A failed handoff does not prevent ordinary desktop/shell
startup; it prevents trusting that login for future automation. See ADR 0125.

### Native desktop association (0.1.539)

The `desktop` object in `GET /v1/login` observes the exact named login client using Hibr's
native `hold list` and `hold clients`, as the verified user. Since0.1.549,
the packaged hook's existing desktop `--name` argument is stable: connector
(otherwise SDL display name) plus one-based row-major pane ordinal, such as
`blit-DP-1-p2`. Auxiliary clients use the same rule, not their role or boot-time
IDs. Long/unsafe selectors receive a bounded readable prefix plus a hash of
the whole selector; duplicate inventory or occupied managed identities refuse
launch. Native duplicate names/suffixes report `native_client_name_in_use`,
never guessed ownership. See ADR0135. A live pane reassignment retains the
client's birth name until its next launch. This is not another desktop instance
or a new settings resource; Hibr owns preferences under the native name.

`state` is `inactive` before verified authentication or for shell selection;
`waiting` when the native owner/client is absent; `ready` when the exact client,
grid, live owner UID and process start-time identity are verified; `error` when
the query fails or its result is invalid. Ready results contain `owner` (PID,
UID, starttime_ticks, union rows/columns), `client` (the exact login client),
and `clients` (all named clients reported by that same native owner, including
signed row/column origins and primary flags). The collection is empty unless
the owner and exact login-client association have both been verified. It is
read-only native state, not a second layout database. Otherwise `owner` and
`client` are null.
`observed_monotonic_us` is the last successful query's time; results expire
after three seconds. `query_in_flight` distinguishes background work from
a current observation.

The observer has a two-second deadline and a one-second retry interval,
without account lookup or Hibr IPC blocking presentation. Its clean environment
contains no API token or inherited input/display/network descriptors. Login
replacement cancels the query and invalidates its generation. No public API
registers a claimed owner PID. The helper never moves, promotes, detaches or
starts a desktop/client. This is association evidence, not automatic joins or
registration in the old external-adapter ownership flags. From0.1.540, a
fresh association safely projects the login's ownership role for native joins
without enabling input projection. See ADRs0126 and0127.
This stage uses the packaged `desktop` name and Hibr's default per-user hold
namespace; custom PAM TMPDIR namespaces are not inferred.

The package enables this behavior with `BLIT_LOGIN_AUTOSTART=1`. The service
must be root-owned for the login endpoint; an unprivileged Blit profile may
run ordinary sessions but must reject login-manager start. Root-path PAM and
logind acceptance still require the Hibr VM/real-host test described by Hibr
issue #79.

## Endpoint map

This table is an orientation aid, not a second schema. The OpenAPI document is
the authority for methods, parameters, required fields, response shapes, and
status codes.

| Area | Endpoints | Purpose |
| --- | --- | --- |
| Identity | `GET /v1/version`, `GET /v1/server` | Compatibility and server capabilities |
| Hardware | `GET /v1/graphics`, `/v1/displays`, `/v1/audio` | Discover renderers, outputs, modes, and sound devices |
| Fonts/topology | `GET/PUT /v1/fonts`, `GET/PUT /v1/topology` | Select logical cell geometry and pane placement |
| Settings | `GET/PUT/DELETE /v1/settings` | Inspect, replace, or reset API-owned server settings |
| Login | `GET/POST/DELETE /v1/login` | Supervise one root-owned Hibr login screen and its authenticated child session |
| Terminals | `GET/POST /v1/terminal-sessions`, `GET/PATCH/DELETE /v1/terminal-sessions/{id}`, `POST /v1/terminal-sessions/{id}/input` | Inventory, create, place, inspect, drive, and stop PTY sessions |
| Hibr | `GET /v1/terminal-sessions/{id}/hibr-owner` | Inspect the primary/join ownership attachment |
| Surfaces | `GET/PUT /v1/surfaces/{id}/cells`, `GET /v1/surfaces/{id}/summary`, `GET /v1/surfaces/{id}/scrollback` | Read and revision-update authoritative terminal state |
| Input | `GET /v1/input/devices`, `GET/POST /v1/input/inject`, `GET/PUT /v1/input/routes` | Discover, diagnose, and route physical or synthetic input |
| Presentation | `POST /v1/frames`, `GET /v1/stream` | Commit coherent frames and consume event/pixel streams |
| Graphics | `GET/PUT /v1/effects`, `POST/DELETE /v1/overlays/{id}` | Configure effects and bounded overlays |
| Diagnostics | `GET /v1/diagnostics`, `/runtime`, `/observation`, `/trace`, `/screenshots/{display_id}` | Inspect aggregate state, timing, events, and evidence captures |
| Control | `POST /v1/control/shutdown`, `/restart` | Stop or restart the supervised server |
| Updates | `POST /v1/control/update`, `GET /status`, `POST /cancel`, `/reset` | Upload and supervise a verified package update |

The schema currently contains 43 path templates. Keep this table synchronized when a
path is added, removed, or renamed; the schema remains the authoritative count.

### Method-level reference

This is the stable mental model for the 40 paths; the OpenAPI file remains the
exact method, parameter, and response reference:

```text
GET    /v1/version                         compatibility handshake
GET    /v1/server                          capabilities and links
GET    /v1/graphics /displays /audio       hardware inventory
GET/PUT /v1/control/shutdown-key           the keyboard stop chord
GET/PATCH /v1/displays/{id}                rotation and brightness
GET/PATCH /v1/audio/cards[/{id}]           a card's mixer
GET/PUT /v1/fonts /topology /effects       revisioned configuration
GET/PUT/DELETE /v1/settings                persisted server and font settings
GET    /v1/terminal-sessions               authoritative live session inventory
POST   /v1/terminal-sessions               create a PTY-backed session
GET/PATCH/DELETE /v1/terminal-sessions/{id} lifecycle and placement
POST   /v1/terminal-sessions/{id}/input    terminal input bytes
DELETE /v1/terminal-sessions/{id}/graphics explicit retained-graphics reset
GET    /v1/terminal-sessions/{id}/hibr-owner Hibr ownership state
GET/PUT /v1/surfaces/{id}/cells            authoritative cell snapshot/update
GET    /v1/surfaces/{id}/summary/scrollback repair and inspection
GET/POST /v1/input/*                       discover, route, or inject input
POST   /v1/frames                          explicit coherent frame boundary
GET    /v1/stream                          events and bounded pixel frames
GET    /v1/diagnostics*                    state, timing, traces, screenshots
GET    /v1/diagnostics/observation         one canonical operational state dump
POST   /v1/control/{shutdown,restart}      orderly service control
POST/GET /v1/control/update*               verified package supervision
POST/DELETE /v1/overlays[/{id}]            bounded graphic overlays
```

A Hibr session's `hibr_attachment.settled` (in `GET /v1/terminal-sessions`)
is false while Blit is still repairing the desktop's startup or attach
layout by nudging its terminal size, and true once that is done. A client
that drives the desktop can wait for it before clicking.

When this map and the schema disagree, the schema wins and the documentation
must be corrected in the same change. A route addition is not complete until
the schema gate, a focused request test, and the verification ledger agree.

## Minimal client workflow

The following is a safe read-only probe. Real clients should use a JSON library
and preserve returned IDs and revisions.

```sh
set -eu
base=${BLIT_URL:-http://127.0.0.1:8080}
auth=()
if [ -n "${BLIT_AUTH_TOKEN:-}" ]; then
  auth=(--header "Authorization: Bearer $BLIT_AUTH_TOKEN")
fi

curl --fail "${auth[@]}" "$base/v1/version"
curl --fail "${auth[@]}" "$base/v1/server"
curl --fail "${auth[@]}" "$base/v1/displays"
curl --fail "${auth[@]}" "$base/v1/topology"
curl --fail "${auth[@]}" "$base/v1/diagnostics/observation"
curl --fail "${auth[@]}" "$base/v1/diagnostics/runtime"
```

For mutations, first fetch the resource revision, send the revision using the
schema-defined precondition, and supply an `Idempotency-Key` for a retryable
operation. A revision conflict means “resynchronize”: fetch authoritative
state again, reconcile intent, and retry only if the operation is still valid.

For a single deterministic health record, collect these responses together
outside the repository:

```sh
set -eu
base=${BLIT_URL:-http://127.0.0.1:8080}
stamp=$(date -u +%Y%m%dT%H%M%SZ)
mkdir -p "blit-support-$stamp"
for resource in version server graphics displays audio topology fonts diagnostics/runtime 'diagnostics/trace?limit=32'; do
  file=$(printf '%s' "$resource" | tr '/?' '__')
  curl --fail --silent --show-error --connect-timeout 2 \
    "$base/v1/$resource" > "blit-support-$stamp/$file.json"
done
```

Add one screenshot per display using
`GET /v1/diagnostics/screenshots/{display_id}`. Redact bearer and update
credentials, PTY contents, and private host paths before sharing the bundle.
The bundle is evidence for a named run, not a new source of truth.

## Operator runbook: prove the server before touching a desktop

Use this sequence when a physical screen is black, stale, or visually
ambiguous. It is deliberately read-only and uses the API as the source of
truth; do not start a second Blit process or a second Hibr desktop while
diagnosing the first one.

```sh
set -eu
base=${BLIT_URL:-http://127.0.0.1:8080}
auth=()
if [ -n "${BLIT_AUTH_TOKEN:-}" ]; then
  auth=(--header "Authorization: Bearer $BLIT_AUTH_TOKEN")
fi

curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/version"; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/server" | jq '{state,backend,display_count,session_count}'; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/displays" | jq '.displays[] | {id,name,state,width,height,refresh_hz,presenting}'; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/diagnostics/runtime" | jq '{performance,input,frames,events}'; printf '\n'
```

Interpret the result in this order:

1. `/version` proves which binary and API contract answered.
2. `/server` proves whether the service is ready and how many resources it
   believes exist.
3. `/displays` proves discovered outputs and their active modes; it does not
   prove that a desktop has supplied content.
4. `/diagnostics/runtime` separates presentation cadence, producer commits,
   missed deadlines, input dispatch, and recent bounded events.
5. `/diagnostics/screenshots/{display_id}` proves pixels for one output; pair
   it with the surface summary and trace rather than treating it as a full
   diagnosis.

If a desktop is already running, inspect its existing session and Hibr-owner
state before creating anything. A healthy topology has one primary Hibr owner;
an auxiliary `desktop --join` is an attachment to that owner, not another
desktop. If the API reports a stale or failed attachment, repair the resource
through the documented lifecycle and verify that the session/surface/route
counts return to the expected values. Never use a launcher as a second source
of lifecycle state.

For a support bundle, save the JSON responses and a screenshot outside the
repository, redact credentials and PTY contents, and record the exact
`server_version`, host, display mode, workload duration, and timestamp. The
repository accepts the command and interpretation; it does not accept the
capture itself.

## API guarantees and non-guarantees

The API guarantees typed resource state, opaque IDs, revisions, bounded
diagnostics, and explicit lifecycle responses. It does not guarantee that a
successful mutation has already reached physical scanout, that an unchanged
scene will be re-presented continuously, or that a virtual/QEMU result proves
physical input or monitor behavior. Use runtime counters and a screenshot to
distinguish those cases. This distinction is essential when investigating a
slow Hibr producer, a display-mode mismatch, or a damaged scene.

## Minimal session transaction

The following is a complete text-session shape. The exact required and
optional fields are defined by `CreateTerminalSession`; the example uses the
server's ordinary shell policy and an explicit first display. Preserve every
returned opaque ID rather than deriving one locally.

```sh
set -eu
base=${BLIT_URL:-http://127.0.0.1:8080}
key="blit-$(date +%s)-$$"

session=$(curl --fail --silent --show-error \
  --header 'Content-Type: application/json' \
  --header "Idempotency-Key: $key" \
  --data '{"command":"/bin/sh","display_id":"display-1"}' \
  "$base/v1/terminal-sessions")
printf '%s\n' "$session"
session_id=$(printf '%s' "$session" | jq -r '.id')

# Input is a subresource. The parent session route is not an input endpoint.
curl --fail --silent --show-error \
  --header 'Content-Type: application/json' \
  --data '{"data":"printf '\''BLIT API OK\\n'\''\\n","mode":"raw"}' \
  "$base/v1/terminal-sessions/$session_id/input"

curl --fail --silent --show-error "$base/v1/diagnostics/runtime"
curl --fail --silent --show-error -X DELETE \
  "$base/v1/terminal-sessions/$session_id"
curl --fail --silent --show-error "$base/v1/diagnostics"
```

In a production client, replace the shell parsing with a JSON library, use a
stable idempotency key across retries, and verify that cleanup leaves no
session, surface, route, or stream owned by the transaction. A `202` input
receipt means the bounded PTY write path accepted bytes; it does not mean the
terminal has already rendered them. Observe the surface revision or stream
event and use runtime diagnostics to measure presentation.

## Core lifecycle

The normal terminal lifecycle is:

```text
discover -> create session -> place surface -> route input
         -> consume revisions/events -> delete session -> verify cleanup
```

Create a session with `POST /v1/terminal-sessions`. The response identifies
the session, PTY process, surface, display/pane placement, and the
`hibr_desktop` binding. Use `PATCH /v1/terminal-sessions/{id}` for placement
and input policy, `POST /v1/terminal-sessions/{id}/input` for terminal input,
and `DELETE` to stop the process group and release its surface.

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
should keep the returned session IDs. From0.1.540, a login-owned join requires
fresh `GET /v1/login` desktop readiness, not authentication alone. The associated
login projects ownership role `primary`; Hibr's primary-display choice remains
the separate native `login.desktop.client.primary` flag. Omitting `user`
derives the authenticated account in the child, without the service's API
token. A conflicting explicit account returns409
`hibr_desktop_identity_mismatch`; an occupied/missing logical pane returns409
`hibr_join_pane_unavailable`. Delete test sessions at the end and confirm
`/v1/diagnostics` reports zero sessions and surfaces.

Native clients receive input through their own PTYs in local coordinates;
Hibr alone translates it into the current2-D union. Blit does not forward
joined input to the primary or add a horizontal slot offset. An optional
`hibr_join_position: {"row": -67, "column": 0}` requests initial native
placement through the Blit launcher; signed values are bounded to±32767 and
both fields are required. This is a logical-cell example, not a fixed monitor
size or automatic layout policy. It is invalid on non-join and standby
requests. The launcher never moves the primary or reasserts later user layout.

Recognized `--standby`/`-w` is an auxiliary client, not another desktop owner.
Hibr owns its waiting/deliberately-released behavior. Login-owned clients are
retired asynchronously when their authentication generation ends or a fresh
observation proves native owner replacement. Temporary query errors alone do
not trigger destruction. Saved automatic auxiliary policy and its placement/
reconnect reconciliation are not yet implemented by this API increment.

Primary and join lifetimes are intentionally different: the primary may keep
its Blit session running while Hibr's detached owner restarts after the
launcher child exits; a joined client is an attachment, so a dead join is
reported as `failed` and its surface/routes are released. A replacement join
can then be created against the still-running primary.

The intended topology is therefore:

```text
one Blit service
  └─ one primary Hibr desktop owner
       ├─ primary display/pane
       └─ zero or more Hibr desktop --join attachments
            └─ Blit-owned logical pane placement
```

A physical monitor is not itself a Hibr desktop owner, and a logical 1x2 or
2x2 pane split does not imply additional physical connectors. `/v1/displays`
reports physical outputs; `/v1/topology` reports logical placement.

## Displays, topology, and fonts

Since0.1.545, configure independent splits through `PUT /v1/settings`.
Read the full document, edit `display_layouts`, then replace it with its
current `If-Match` revision.

Use0.1.546 or newer: it durably saves non-default legacy migration at startup;
the unreleased0.1.545 candidate required a later settings mutation to save it.

For example:

```json
"display_layouts": {
  "DP-1": {"columns": 1, "rows": 2},
  "HDMI-A-1": {"columns": 1, "rows": 1}
}
```

Each dimension is1–8; columns precede rows, so1x2 is stacked and2x1 is
side-by-side. A missing connector entry means1x1. Disconnected entries remain
saved. Limits are16 configured outputs,64 live panes and16 simultaneous
terminal sessions; these are different resources. Invalid or physically
sub-cell grids return400; removing an occupied pane returns409
`topology_in_use` without changing settings. Detach it explicitly first.
Surviving terminal IDs/processes retain their pane ordinal and receive PTY
resize. Hibr Displays still owns native desktop arrangement.

`GET /v1/topology.outputs[].layout` reports each actual grid. Its top-level
`layout` and `server.pane_layout` are legacy bulk controls, not the per-output
answer. Legacy startup settings migrate for the currently connected outputs
only; a later new monitor does not inherit another output's split.

`PUT /v1/fonts` applies the selected profile and scale to pixels, not only to
metadata. The reported `cell_pixels` dimensions are the physical cell metrics
used by the renderer, PTY resize, pane/display geometry, pointer hit-testing,
and Kitty/Sixel placement. SDL scene textures remain physical-sized output
textures; changing scale does not rely on a backend-specific renderer
transform. A display-attached Hibr session is reflowed to the new display grid
and receives the existing resize/reassert contract. Hibr source is never
modified by this operation. Pixel acceptance is covered by the local font API
gate and should be repeated with the live screenshot endpoint after deployment.

`GET /v1/displays` reports physical outputs, active modes, supported modes,
bounds, and presentation state. `PUT /v1/topology` changes the logical
placement using the schema's revision and idempotency controls. Pane splits are
logical Blit layout resources; they do not create extra physical monitors.

Pane bounds are terminal-cell rectangles, but Blit derives them by partitioning
the physical output first and quantizing each physical pane region separately.
This prevents a terminal row from crossing a physical panel boundary. For a
1920x2160 output at 48px cells, a 1x2 split therefore reports two 80x22 panes
inside 1920x1080 regions; the 24px remainder in each region is centred and is
not painted as a partial row. The enclosing output may consequently have one
more complete row than the sum of its pane rows.

Each topology pane publishes `pixel_region` and a pane-local `cell_grid`. The
pane grid is the authoritative physical placement for rendering and pointer
hit-testing; the pane's terminal-cell `bounds` remain the authoritative Hibr
and PTY dimensions. Each topology output also publishes its enclosing
`cell_grid`, which describes the complete-cell display rectangle and its
remainder. Clients must use these reported values rather than reconstructing
positions from font names, display modes, or hard-coded halves. See ADRs 0075
and 0076 in docs/adr.

### Display settings: rotation, brightness, and what power costs

A display's `configuration` used to be `{}`, and that was a decision rather
than an omission (ADR 0064): selecting a mode changes the render output size
and therefore every surface on the display, and a Hibr desktop's cells come
from its owner (ADR 0063), so applying one needs a resize protocol with that
owner. Issue #60 is that protocol. `mode` is still refused, with
`invalid_display_property`.

What is left is the settings that resize nothing, and `capabilities` says
which of them this backend and this hardware can actually perform. It is
derived, not declared: `configuration` offers exactly what `capabilities`
claims, and a PATCH accepts exactly that. A property that takes a value and
does nothing is worse than its absence, which is the reasoning the empty
object used.

```text
mode-report   always
rotation      the SDL path. Blit's own render transform.
brightness    only where a backlight is attributed to the display.
```

**`rotation`** is `0`, `90`, `180` or `270`: the angle, clockwise, by which
Blit rotates the rendered image before presenting it. At 90, what Blit drew at
the top-left of the logical rectangle appears at the **top-right of the
panel**. The contract is stated as a corner on purpose, because every enum
that could stand in for it means something different -- DRM's `ROTATE_90` is
anticlockwise and RandR calls the same transform "right".

Rotation moves the display's rectangle, not its mode:

```text
bounds            the logical rectangle: 1920x2160 becomes 2160x1920 at 90
modes             physical, unchanged
active_mode       physical, unchanged
state.physical    the mode the connector drives
```

`bounds` is the logical one because that is what a rotated monitor occupies on
a desk, and because the cell grid, the pointer hit test and the topology
placements then all work in one coordinate space. `xrandr` reports the same
split. `GET /v1/topology` carries the same rectangle and the same rotation.

A rotation is refused with **409 `display_rotation_in_use`** while a surface is
present on the display. Exchanging width and height exchanges the cell grid,
and a surface's owner is the one that decides its size -- so this is issue
#60's problem again, and the refusal is a conflict rather than a prohibition:
remove the sessions and the same request succeeds. A rotation is persisted
(ADR 0067) against the display's **name**, which on the SDL KMSDRM backend is
its connector (`DP-1`); setting it back to `0` removes the stored entry rather
than leaving a zero behind.

**`brightness_percent`** reads `actual_brightness` -- what the panel is doing
-- and writes `brightness` -- what was requested. A driver that ramps will
report the old value until it arrives, which is why the two are not conflated.
It is not persisted: `systemd-backlight` already restores it across a boot, and
two things restoring one setting is how they end up fighting. A backlight is
attributed to a display only when the host has **exactly one backlight and
exactly one display**: sysfs does not say which panel a backlight drives, and a
control that dims the wrong screen is worse than one that is absent. Where none
is attributed, the write is refused with 422
`display_brightness_unsupported`.

**Power is reported and not settable.** `state.power` carries the connector's
DPMS state where a DRM connector was matched to the display, so an operator can
see that a panel is asleep -- but `power` is deliberately not in
`capabilities`, because nothing can set it yet. Two measurements on the live
host say why:

```text
/sys/class/drm/card0-DP-1/dpms   mode 444; a root write returns EACCES
/dev/dri/card0                   one open fd, held by blit through SDL
```

So sysfs is not an avenue on any backend -- that is the kernel, not SDL -- and
setting the property needs DRM master, which belongs to SDL's KMSDRM backend.
SDL publishes that master fd per window, which is the route the rest of #69
takes. `/sys/class/graphics/fb0/blank` is not an alternative: fbdev emulation
is one device for the whole card, so it could not express "turn off this one
display" even if it worked behind a master.

On this host there is **no backlight at all** (`/sys/class/backlight` exists and
is empty), so `brightness` is absent there and that is the answer rather than a
gap. The code path is real and gated locally against a fabricated backlight
under `BLIT_BACKLIGHT_ROOT`.

Two limits are worth knowing rather than discovering:

- Whether the **unprivileged profile** (ADR 0065) can write
  `/sys/class/backlight/*/brightness` is not measured. The attribute is
  typically `root:video` with a udev rule granting the seat's user, the gate
  runs against fabricated files, and the live host runs as root with no
  backlight. It is the same unmeasured-group caveat as the ALSA control device
  in #68, and it belongs to an operator who switches profiles on hardware that
  has a panel.
- On a **multi-display** host, rotating one display changes its width, so a
  topology placement computed against the old width may no longer tile. Blit
  does not recompute the placement for you: re-`PUT /v1/topology` after a
  rotation. Both machines this was measured on have a single output, so that
  is a stated consequence rather than a tested one.

### Explicit emoji cell layout (offline candidate578)

`PUT /v1/surfaces/{id}/cells` accepts optional `text_layout`:

- `scalar` (default): existing Unicode15.1 scalar widths and zero-width
  attachments. Existing positioned Hibr providers are unchanged.
- `emoji`: group pinned Emoji18.0 sequences within each cell's `text` value.
  Completed multi-scalar sequences occupy two columns; singletons/ordinary
  text keep existing widths. Never join across positioned cells or requests.

For example, `{"base_revision":7,"text_layout":"emoji","cells":[{"x":0,"y":0,"text":"👩‍💻X"}]}`
places the emoji at0, its continuation at1 and X at2. Use Modern/Retro for the
shared colour-cluster raster. Snapshots return the complete sequence on its
anchor. Overwriting either half clears the previous wide ownership. A wide
cluster that cannot fit the right edge is clipped whole, following existing
API clipping semantics (the request can still be acknowledged). Invalid
layout types/values return400 `invalid_text_layout` before cell mutation.
This cell-update policy does not alter the owning session's PTY policy.

Offline candidate579 also accepts `text_layout` on
`POST /v1/terminal-sessions`, for example
`{"command":"/bin/cat","text_layout":"emoji"}`. It is fixed for the lifetime
of that session and returned by session observations; PATCH returns400
`immutable_text_layout`. Default/omitted `scalar` preserves existing Hibr
widths. `emoji` uses the same pinned sequence trie across PTY reads, supports
late widening and whole-cluster REP/wrap. With autowrap disabled or a single
available column, allocation stays one column rather than spilling outside
the viewport. Controls and successful positioned updates terminate an active
extension; incomplete UTF-8 bytes do not. This is not DEC2027/general Unicode
grapheme support.

Candidate582 fixes resize ownership: overlapping whole cells survive; a wide
cell cut by the new right edge is removed whole, including its retained
anchor. Both active and saved alternate buffers are normalized. Growing the
grid cannot resurrect clipped attachments or link IDs. A resize terminates
streaming emoji-extension eligibility, without resetting the terminal or its
last printable REP value. This is grid clipping, not paragraph reflow.

Offline candidate580 uses pinned Unicode18.0 presentation data: default-emoji
scalars and standardized VS16 pairs select colour; standardized VS15 pairs
select monochrome outlines. Modern/Retro share a real monochrome Noto Emoji
fallback when their selected outline lacks a text glyph. Presentation never
changes the owner's allocated columns. These are shared-raster semantics,
not a claim of complete terminal or physical-display acceptance. Existing
scalar layout remains the default; grouping still requires explicit opt-in.

`GET /v1/fonts` lists profiles and the active cell size. `PUT /v1/fonts` can
select `font_id`, legacy `scale` from 1.0 through 3.0 in 0.1 increments,
or `size_px` (integer8..64, exact cell height). Select a profile together with
either size control, never both size controls. Profile-only selection retains
the active size policy. `active.sizing` identifies `scale` or `pixels`;
`active.size_px` and `active.cell_pixels` report the actual result. Returned
scale under pixel mode is derived and need not be a valid legacy scale write.
For example, `{"size_px":23}` selects12x23 cells for the8x16 font. This is
font/cell sizing, not a display mode change. Existing bitmap profiles are
sampled at this size. Font release0.1.564 adds `blit-modern`, rasterizing Hack outline text
at a fitted native size, with antialiased coverage and Noto intrinsic-colour
emoji. `blit-hibr-spleen16` is the additional pixel-art retro profile; both
remain selectable alongside Terminus. Select smooth text, for example, with
`{"font_id":"blit-modern","size_px":23}`. Pixel-art fallback characters are
still pixel-art: adding these profiles does not implement universal Unicode,
complex-script/ligature shaping or ZWJ/variation-selector emoji sequences.
Candidate0.1.562 contained this work but was not deployed; config-only
maintenance0.1.563 intentionally excludes it. Discover available profiles
through `GET /v1/fonts`, not a version-number comparison alone.

Candidate0.1.573 adds `blit-retro`, an antialiased 3270 Mono derivative, and
expands both outline profiles to the full pinned Nerd Fonts3.4.0 catalogue.
Select with `{"font_id":"blit-retro","size_px":24}`; use the same existing
settings save/load contract for persistence. The font inventory is canonical
for availability. Neither a font change nor `colour_glyphs:true` implies full
emoji-sequence shaping; see issue110 and [font scope](../assets/fonts/README.md).
Candidate0.1.574 upgrades the one shared colour asset to Noto2D2.057 and pins
the Emoji18.0 acceptance corpus. The API still reports colour-face availability,
not complete emoji-cluster conformance; the scalar-only renderer is not changed
by that asset update. No additional sizing or browser-zoom API is introduced.

`active.rasterization` is `bitmap-nearest` or `native-antialiased`;
`active.colour_glyphs` reports the colour face, and `active.raster_cache`
reports bounded cached bytes and render count. `glyphs` and `mappings` retain
their PSF identity meaning (the canonical fallback for the modern profile),
not a total Unicode coverage claim for the outline/emoji faces. Profiles
report their rasterization and availability. Installation keeps Terminus as
default. Profile/size changes validate the rasterizer and replacement SDL
atlases before retiring active state. Native KMS and both SDL renderers share
the same coverage/colour source, fallback policy and cell sizing.

Candidate0.1.588 attaches a required Arabic companion to canonical
`blit-retro` and `blit-modern` profiles. `profiles[].arabic_companion_path`
identifies the packaged asset; `active.arabic_companion` reports the actually
loaded `path`, `family`, `baseline_px` and `shaping: "visual-cell-marks"`.
It is null for profiles without a companion. Availability includes the
companion file; a failed font/size replacement preserves the active font.
Arabic shares the Latin baseline and retains ink extending beyond a cell.
Hibr supplies display-order presentation forms; Blit positions cell-local
marks without applying a second bidi pass. Tested core letters, lam-alef,
diacritics and Arabic-Indic digits `٠١٢٣٤٥٦٧٨٩` are a finite scope, not a
promise of every Arabic/Quranic form. Uncovered scalars use the shared,
explicitly flagged PSF fallback. Custom noncanonical font paths do not infer
an Arabic companion. Primary `glyphs`/`mappings` still describe PSF fallback,
not total companion coverage. Font size remains controlled by `size_px`.

Integer JSON values such as `2` remain valid and mean `2.0`. Blit recalculates
pane geometry and resizes attached PTYs. Because a framebuffer cell cannot be
fractional, the physical width and height use one shared nearest-pixel rule;
for example, an 8x16 font at scale 1.1 is a 9x18 cell. This is a display
geometry operation; it does not change Hibr source or terminal protocol state.

For example, apply a modest size increase without restarting the server:

```sh
curl --fail-with-body -X PUT "$BLIT_URL/v1/fonts" \
  -H "Authorization: Bearer $BLIT_AUTH_TOKEN" \
  -H 'Content-Type: application/json' --data '{"size_px":23}'
```

Read back `active.scale` and `active.cell_pixels`: rounded cell dimensions,
not fractional physical pixels, determine the grid. An8x16 source becomes
10x19 at1.2. The live font API queues automatic saving; poll settings storage
for a clean receipt. To edit font sizing as part of a complete configuration,
read `GET /v1/settings`,
update its complete `document.font` with the desired profile/scale and
`PUT /v1/settings` using the returned revision in `If-Match`. Preserve the
other document sections and require `storage.state == "clean"`. A successful
live font response alone is not confirmation of a durable settings save.

When saving pixel sizing, set `document.font` to `{"font_id":"…","size_px":23}`
and remove the legacy `scale` key. Saving both returns400 and preserves the
previous durable configuration. A legacy scale write likewise replaces pixel
mode rather than multiplying it. See ADR0144.

Text foreground/background colours are terminal styles, independent of font
selection. The current monochrome PSF pipeline does not provide multicolour
emoji merely by selecting a profile with more Unicode mappings.

### API-owned server settings

`GET /v1/settings` is the authoritative view of persisted presentation
settings. The response has three relevant parts: `document` is the sparse
stored document, `effective` is what this process is using now, and
`revision` is the optimistic-concurrency value for replacement. The document
contains `font` (`font_id`, `scale`) and `server` values for
`render_interval_ms`, `renderer`, `kms_async_pageflip`, and `expect_desktop`.
The environment variables with the corresponding names are boot defaults only
when the document is silent. A stored renderer request is applied at the next
start; `effective.renderer_requested` and `effective.renderer_active` make
that distinction explicit.

Per-output pane layout is owned by `document.display_layouts`, applied live by
settings replacement. Legacy `PUT /v1/topology` applies one1x1/1x2/2x1/2x2
grid to the connected outputs in bulk and refuses occupied layout changes.
Prefer explicit connector-keyed layouts for independent monitors.

Replace the complete document with `If-Match` (omitted only for an initial
write):

```sh
curl --fail --request PUT \
  --header 'Content-Type: application/json' \
  --header 'If-Match: REVISION' \
  --data '{"document":{"version":1,"input_devices":{},"audio_devices":{},"font":{"font_id":"blit-hibr-terminus16","scale":1.1},"server":{"render_interval_ms":16,"pane_layout":"1x2","renderer":"software","kms_async_pageflip":false,"expect_desktop":true}}}' \
  http://HOST:8080/v1/settings
```

`DELETE /v1/settings` clears the document and restores boot defaults. It does
not alter authentication, bind address, command allowlists, update
credentials, or test seams. `BLIT_EVDEV_MOUSE` is a compatibility boot
override only; stable input devices and their API configuration are the
supported input boundary. See ADR 0089 and ADR 0067.

#### Login startup policy (0.1.537)

`document.login` moves login placement into the same API-owned store:

```json
{"enabled":true,"preferred_pane_id":"pane-display-1-2","fallback":"wait"}
```

Add that object to the complete document read from `GET /v1/settings`; replace
with `PUT /v1/settings` and its revision in `If-Match`. Do not send this fragment
as the whole document: replacement would reset unrelated settings. The saved
policy is normalized to explicit values. Missing fields use boot defaults;
`preferred_pane_id:null` requests automatic selection. `fallback` is `wait`
or `first_available`. A missing preferred pane is valid intent, retained until
available; fallback assignment never overwrites it. Current IDs are scoped to
the backend topology, not portable monitor identities.

This section is **startup-applied**. `effective.login` and
`effective.login_restart_required` show the distinction. Saving must not kill
an authenticated desktop. `/v1/login` remains the live start/move/stop resource
and does not implicitly save its operations. Its `preferred_pane_id` is
selection intent; `pane_id` is assignment. This does not implement joins,
hotplug failover, or automatic primary migration; see ADR 0124 and issue #99.

`document.auxiliary_displays` is the saved auxiliary intent: an array of
unique `{output, pane_ordinal}` bindings. `output` is the stable connector
name; `pane_ordinal` is one-based row-major within that output's configured
pane grid. Missing outputs are valid dormant intent. Saving this field does
not move or reconnect a current desktop client. Blit's standby
reconciler applies initial placement once; Hibr Displays owns subsequent
session moves. The control panel must keep live Apply separate from durable
Save as default. Since 0.1.543, saved auxiliaries are loaded as the live policy
at boot. Later settings replacements affect the next boot, while the current
policy is controlled by the separate Apply operation below.

#### Live auxiliary displays (0.1.543)

Direct KMS0.1.544 reconciles connected outputs without restarting the service.
A read-only worker observes DRM uevents, coalesces bursts for100ms and probes
periodically every five seconds as a fallback. The main thread owns output
creation/retirement and publishes changed display inventory and topology
revisions. Unchanged output resources and primary PTYs are retained. When
the last output disappears, the API remains available and PTYs keep their
pane intent; primary migration is not automatic. Unknown outputs are not
automatically added to auxiliary policy. Since0.1.545, new outputs use their
connector-keyed layout or1x1 when unconfigured. Font selection/size remain
global. The0.1.544 global-split inheritance is superseded by ADR0131.

`diagnostics.render.hotplug` (also in runtime/observation snapshots) reports
`supported`, `observer` (`uevent+periodic`, `periodic`, `unavailable`),
`inventory_revision`, worker `scans`, `last_scan_ms`, and `error`. A failed
probe preserves the last inventory; a failed activation/teardown is reported
without destroying unrelated outputs. Failed teardown quarantines resources
until a subsequent retry can disable scanout. Unknown connector status also
retains its last-known output instead of inventing a disconnection; inspect
`hotplug.error` before treating retained inventory as freshly verified presence.
New connections may remain
unavailable if output slots or compatible CRTCs are exhausted. Support targets
the selected DRM device, not arbitrary multi-GPU ownership.

The existing auxiliary controller resolves saved/live connector bindings
against this inventory. A removed managed auxiliary is retired; on return a
new standby can be provisioned after fresh authenticated native readiness.
Its initial placement is verified once. Surviving clients' user placement is
not reasserted. Unmanaged/primary PTYs are not terminated by output loss.

Display discovery in the current SDL implementation occurs at server startup.
It does not handle live SDL display-added/removed events by rebuilding output
resources and topology. Consequently an auxiliary waiting for a connector
does not prove that plugging in a monitor will make that connector discoverable
without restarting the service. API and Control Panel refreshes report the
server inventory; they are not hardware rescans. Startup-detected monitors can
be selected explicitly through Apply and saved as defaults. New monitors are
not automatically added to the requested auxiliary set.

`GET /v1/auxiliary-displays` returns the live policy revision, operation ID,
source (`startup` or `apply`), and per-binding progress. Each binding reports
its output selector, pane ordinal, resolved pane ID, managed session ID,
native client name, state/reason, and initial relative row/column offsets with
a `confirmed` flag. Connector names come from display inventory; a backend
without connectors uses its display name. An ambiguous name conflicts.

`POST /v1/auxiliary-displays/apply` replaces live intent without saving it:

```json
{"bindings":[{"output":"DP-1","pane_ordinal":1}],"retry_stopped":false}
```

The POST returns 202 with that same resource, not a completed-join promise.
Poll GET for `joined`. Optional If-Match checks the live policy revision;
Idempotency-Key protects transport retries. Identical intent is a no-op.
`retry_stopped: true` explicitly retries failed/exited clients. An empty
bindings array retires only the managed auxiliaries, preserving the desktop
owner and any independently created client.

The controller waits for authentication, fresh native desktop association,
available outputs and panes; occupied panes conflict. It starts a named Hibr
`--standby` as the authenticated user, waits for native attachment, then applies
and confirms initial placement once. Later user moves stay authoritative. A
standby exit reports stopped until explicit retry or a new desktop generation.
Logout/verified owner replacement retires its generation's managed clients.
Unverified or expired queries never prove logout. After a previously joined
client disappears while its process lives, `waiting_join` with reason
`native_client_absent_release_unknown` reports the observation; the supported
native interface does not establish whether it was deliberately released.
Hibr's standby owns reattachment. See ADR 0128 for limits and acceptance.

#### Save acknowledgement and failure

`persisted` means persistence is enabled, not that all current changes reached
disk. Inspect `storage.state` (`clean`, `pending`, `error`, `disabled`),
`storage.saved_revision` and `storage.error`. Device changes may coalesce for
500 ms. A successful settings replacement has a clean receipt from the same
serializer as GET, with `effective` plus `applied`/`refused` details.

The writer syncs file contents and the parent directory around atomic rename.
Replacement no longer deletes the old file first. A forced save failure
returns HTTP 503 `settings_write_failed`, not success; changes may already
have applied in memory. Read settings and resolve the error before retrying.
Failure after rename but before directory sync means durability is unconfirmed,
not that the old file is guaranteed still present. Failed background writes
retry at the coalescing interval, not every event-loop turn.

## Terminal surfaces and atomic updates

The surface cell snapshot is authoritative. Use `GET /v1/surfaces/{id}/summary`
for cheap health checks and `GET /v1/surfaces/{id}/cells` for repair or full
state. The summary distinguishes styled cells from cells containing actual
codepoints and printable codepoints; this is useful when diagnosing a blank
joined desktop whose surface exists but has not supplied glyphs. Send
revisioned updates to the same cells route. The surface response contains the
resulting revision and commit receipt; do not treat a partially received
stream message or a screenshot as a committed frame.

An API-owned terminal provider may include `terminal_default_foreground` and
`terminal_default_background` in that same update. These are session defaults,
not cell styles; changing either one invalidates the complete surface. Inspect
the effective and configured values through `GET /v1/terminal-sessions/{id}`
under `terminal_defaults`.

`/v1/frames` accepts an explicit coherent-frame request. The WebSocket stream
can carry surface deltas, terminal output, frame events, diagnostics, and
bounded pixel frames. Pixel frames use `pixel_frame_begin` metadata followed by
binary data and a final completion message. Back-pressure and dropped-event
counters are visible in runtime diagnostics.

Since0.1.583, `pixel_frame_begin` accepts paired `placement_width` and
`placement_height` (integer1..65535) in terminal pixels. `width`/`height`
remain source dimensions and alone determine RGB/RGBA payload length. The
destination starts at `x`,`y`; it is not a Chrome zoom setting. Omitting both
placement fields preserves source-sized placement for older producers. A
partial pair, non-integer, zero or out-of-range size is rejected as
`pixel_frame_error`/`invalid_begin` before changing the current image.
Blit's display adapter converts the callback's requested destination cells
using the current cell metrics and sends this pair. Ship the matching server
and adapter together; older servers do not implement these placement fields.

The HTTP surface state and the stream are complementary. HTTP GETs are the
repair path; the stream is the low-latency observation path. A client must
resync after a sequence gap, unknown resource ID, reconnect, or rejected frame
commit. Never reconstruct a complete surface solely from deltas received
before a disconnect.

## Input

### Negotiated keyboard reporting (candidate0.1.585)

`GET /v1/terminal-sessions/{id}` exposes
`terminal_capabilities.keyboard`: `protocol: kitty`, `supported_flags:11`,
current `active_flags` and saved `stack_depth` (0..16). These are observations,
not a second API setting: the program owns negotiation through its PTY.
`CSI ? u` queries; `CSI > flags u` pushes; `CSI < count u` pops;
`CSI = flags ; mode u` replaces/sets/clears bits with modes1/2/3.
Main/alternate buffers and sessions keep independent state. Unsupported
flags4/16 are masked out; only1/2/8 are implemented.

API key injection already uses the physical keyboard dispatch. For example,
send `{"kind":"key","code":125,"value":1}` then value0 for left Super;
right Super is126. A program requesting all-key reporting receives its
canonical key event, not literal text. Super chords require press-Super,
press-key,release-key,release-Super. Repeat/release encoding additionally
requires flag2. Without negotiated all-key reporting, a lone modifier remains
silent. Input stream events include logical `super` after remapping; the
existing input-device modifier role `meta` means the evdev Super role here.
An external browser that cannot capture an OS-reserved key may offer this
explicit action; the server cannot intercept that client's operating system.
Do not confuse terminal raw-byte support with Hibr326's pending console/hold
integration. See ADR0169 for supported scope and acceptance limits.

Physical keyboards, pointers, and gamepads are discovered through
`/v1/input/devices`. Bind a source to a session with
`PUT /v1/input/routes/{route_id}`. `POST /v1/input/inject` is deliberately
synthetic and is useful for deterministic diagnostics; it does not claim that
a physical device works. Mouse button state is aggregated across opened
evdev pointers, and the input trace identifies the source and dispatch path.

Terminal-input `data` is a JSON string. Blit decodes JSON escapes before the
PTY write, so a client may represent an ESC byte as `\\u001b`; the receipt's
`bytes_requested` counts decoded bytes, not the characters used to encode the
JSON request. This is important for API clients sending VT controls and is
covered by the terminal conformance suite.

The normal physical-input transaction is:

```text
GET /v1/input/devices
  -> identify the canonical device_id and capabilities
GET /v1/input/routes
  -> inspect existing ownership; do not duplicate a route
PUT /v1/input/routes/{route_id}
  -> set target_session_id and enabled=true
GET /v1/diagnostics/runtime
GET /v1/diagnostics/trace?kind=pointer_motion&limit=32
  -> prove dispatch and target ownership
```

Use `kind=pointer_button` and `kind=pointer_wheel` for those acceptance
checks. An API-injected event proves the downstream route and dispatch path, but
only a trace entry identifying an evdev source proves physical-device
delivery. There must be one deliberate pointer owner for a
display-attached session; competing routes are a configuration error.

A streamed keyboard press or autorepeat carries optional `event.text` after
Blit has applied that source device's keymap, Shift, CapsLock, and AltGr. A
consumer must use it for printable input rather than reconstructing a US
layout from `event.code`. The evdev code remains the source of truth for named
non-text keys such as Escape, arrows, and function keys. Releases omit
`text`, as do modifier and other non-printing keys.

### Audio: a card's mixer

A mixer belongs to the **card**, not to a PCM, and the API says so rather
than leaving it to be discovered. ADR 0064's audio id is per PCM because that
is what an application opens, and the live host has four HDMI outputs on one
card — so a mixer reported per PCM would be four copies of one thing, and a
write through one would leave the other three stale.

```text
GET  /v1/audio/cards                -> every card and its control elements
GET  /v1/audio/cards/{card_id}      -> one card
PATCH /v1/audio/cards/{card_id}     -> volume_percent, mute, or controls
GET  /v1/audio/{device_id}          -> card_controls points at the card
```

Every PCM carries `card_controls`, and every card carries
`shares_controls_with`: the ids of the devices whose PCMs share that mixer.
Show four HDMI outputs with four independent volume sliders and they will
appear to move together, because they are one slider.

**`volume_percent` and `mute` resolve by a rule, and are absent when it names
nothing.** The rule is: the element named exactly `Master Playback Volume` if
there is one; else the only `* Playback Volume` if there is exactly one; else
nothing. The live host's six cards exercise every branch — one has `Master`,
one has only `PCM Playback Volume`, one has both `Mic Playback Volume` and
`Speaker Playback Volume` and no `Master`, and three have no playback volume
at all. For the ambiguous one, `volume_percent` is **absent** and a write is
refused with `audio_control_ambiguous`: guessing would adjust the wrong
hardware. Name the element under `controls` instead.

`mute` is the inverse of the playback switch, because an ALSA switch is on
when audible.

**A percentage is converted against the element's own range, and what comes
back is the read-back.** ALSA ranges are whatever the hardware says: the
host's cards report `0..255`, `0..87`, `0..63`, `0..36`, `0..32` and `0..9216`
for various elements. So:

```text
PATCH /v1/audio/cards/PCH { "configuration": { "volume_percent": 50 } }
  -> Master Playback Volume values [44, 44], volume_percent 51
```

50% of `0..87` is 44, and 44 of `0..87` is 51%. That is arithmetic, not a bug:
88 values cannot distinguish 101 percentages. Read the response rather than
assuming the request took. What the rounding does guarantee is that the cycle
*settles*: read a value, show a percentage, write it back, and you land where
you started — so a slider may snap once and never drifts after.

**Named writes take the element's own raw value**, not a percentage, because
an enumerated element has values rather than a magnitude. Each element reports
its `minimum`, `maximum`, `step`, `channels` and `writable`; a read-only
element is refused rather than silently ignored.

**Control values are not persisted.** They are the card's own state and the
card keeps them, so storing and re-applying them would make Blit fight
`alsactl` for ownership of something that already survives a reboot. The
`default` flag on a PCM *is* Blit's own and *is* persisted (ADR 0067).

`state.control_accessible` is a separate fact from a PCM's `accessible`:
`/dev/snd/controlC*` is `root:audio` while the PCM nodes are not, so a server
without the `audio` group sees PCMs and no mixers. The packaged unit grants
it; the unprivileged profile (ADR 0065) grants it too.

Format and sample-rate *selection* is deliberately not here: it is a PCM
`hw_params` question rather than a mixer one, and it would mean Blit opening a
PCM, which it does not do.

### Pointer and gamepad settings

#### Pointer appearance (Blit0.1.549+)

Read `GET /v1/settings` → `effective.pointer` for active, default-filled
appearance. Edit `document.pointer` and submit the **complete** settings
document using `PUT /v1/settings`, `{"document": ...}`, with the settings
revision in `If-Match`. This applies live and saves through the existing atomic
writer. Inspect its storage receipt; stale writes return409 and invalid pointer
objects return400 `invalid_settings_pointer` before mutation.

```json
{"shape":"arrow","palette":"pearl","size_px":32,"shadow":true}
```

Shapes: `arrow`, `hand`, `precision`. Palettes: `pearl`, `obsidian`, `neon`,
`amber`. Nominal size24–64 integer physical pixels; up to six additional pixels
hold antialiasing/shadow. Defaults are arrow/pearl/24/shadow-on. Missing fields
use defaults; remove the object to reset. Shape-specific hotspots preserve
pointer hit-testing. The same cached vector source drives SDL and direct KMS;
size is independent of font scale. Motion does not rerasterize the artwork.
SDL rotates the shape with its output; direct KMS's existing rotation
capability/refusal contract is unchanged.

Catalogue `control-panel/blit`0.1.5 registers **Blit Mouse** alongside Blit.
Appearance and stable-ID device controls share its staged settings draft and
revision-checked Save. Hibr's separate Mouse pane still owns window behaviour.
Selecting a different pointer is not a performance or input-latency guarantee.

For direct-KMS pointer stalls, inspect
`GET /v1/diagnostics/runtime` → `snapshot.render.outputs[].kms_phases`.
`draw_ms`, `copy_ms` and `submit_ms` separate the last frame's CPU scene work,
scene-to-scanout copy and kernel submission. `drawn_cells` counts requested
terminal damage across views (overlaps may be counted twice), not pointer
pixels. SDL reports zeros. These are last-frame values, not latency quantiles;
sample repeatedly and correlate with CPU, loop gaps and physical input counts.

| property | what it is |
|---|---|
| `sensitivity_percent` | the linear multiplier, unchanged by anything below |
| `natural_scroll` | inverts the wheel direction |
| `buttons` | a sparse map from evdev code to the button it produces |
| `scroll_step` | how many reports one wheel notch produces |
| `acceleration` | `{"profile":"flat"}` or `{"profile":"adaptive"}` |
| `tap_to_click` | a touchpad only: a quick touch is a click (one finger left, two right); off by default |
| `deadzone_percent` | gamepad axes: what reads as zero |
| `invert_axes` | gamepad axes: which have their sign flipped |

**`buttons` refuses to let two codes hold one button.** Left-handed is
`{"272":"right","273":"left"}`. Mapping only `272` to right is refused, and
the refusal names both codes: `273` still produces right by default, and the
terminal mouse report holds one state per button — so pressing both and
releasing one would report released while the other was still down. Map the
other one too, or map it to `none`. A code that is neither one of the three a
pointer always has nor named in the map is not a button at all, which is why
a pointer that also reports keyboard keys does not dispatch letters as clicks.

**`acceleration: flat` is the default and is what this always did.**
`sensitivity_percent` means exactly what it meant before the property
existed. `adaptive` multiplies it again by a factor that rises with the size
of the delta: untouched at or below 2 counts, tripled at or above 20, linear
between.

```
delta   flat (100%)   adaptive (100%)
    2             2                 2
   11            11                22
   20            20                60
```

The curve is defined on the magnitude of one event's delta rather than a
speed in counts per second. That is a deliberate trade: it makes the curve
exact and therefore testable, at the cost of depending on the device's report
rate — a 1000 Hz mouse sends small deltas for the hand speed that makes a
125 Hz mouse send large ones, so `adaptive` feels different on different
hardware. The numbers live in `src/blit_input_pointer.h`.

`middle_button_emulation` is opt-in per pointer device. It reports and accepts
`{"enabled":false,"window_ms":150}` by default; `window_ms` is 50 through
500 ms. When enabled, a left or right press is held for that window. If the
other button arrives first, Blit emits one middle press; when either physical
button is released, it emits one middle release. If the window expires, the
original press is emitted as itself. Motion continues to be dispatched while a
press is pending, and the event-loop wait is clamped to the earliest pending
deadline so an idle server still flushes it. The setting is absent from the
persistent document at its default and is restored through the same settings
path as the other per-device properties.

```text
PATCH /v1/input/devices/{device_id}
  { "configuration": {
      "middle_button_emulation": { "enabled": true, "window_ms": 150 }
  } }
```

```text
PATCH /v1/input/devices/{device_id}
  { "configuration": { "buttons": { "272": "right", "273": "left" },
                       "scroll_step": 3,
                       "acceleration": { "profile": "adaptive" } } }
```

An empty `buttons` object clears the map. The gamepad properties are refused
on a pointer and the pointer properties on a keyboard, by the same rule as
everything else: a device of no known kind accepts anything, a device of a
known kind refuses what contradicts it.

### Keyboard settings

A keyboard's writable properties, all under `configuration` and all written
with `PATCH /v1/input/devices/{device_id}` (ADR 0067 stores them, so they
survive a release):

| property | what it is |
|---|---|
| `repeat_delay_ms` | how long a key is held before it repeats |
| `repeat_rate_hz` | how many repeats a second |
| `repeat_period_ms` | read-only; the period the kernel actually holds |
| `caps_lock_boot`, `num_lock_boot` | the lock states to apply when the device opens |
| `modifiers` | a sparse map from evdev code to the role that code plays |
| `keymap` | a sparse map from evdev code to the characters it types |

**Repeat is the kernel's.** Blit sets it with `EVIOCSREP` on the descriptor it
already holds and reads it back with `EVIOCGREP`, so what is reported is what
the hardware is doing and not what Blit last asked for. That distinction is
observable, because the kernel holds a period in whole milliseconds while a
person adjusts a rate in hertz: 30 Hz is a 33 ms period and reads back as 30,
but 7 Hz is 142.857 ms, which the kernel keeps as 142. Read the values back
rather than assuming the request took.

Repeat applies only where the device's `capabilities` include `repeat`, which
is EV_REP. A device the kernel does not autorepeat for has no rate to set, so
asking is a `422` rather than a stored number that changes nothing — hide the
control for such a device rather than offering it.

**`modifiers` is not `keymap`, and they compose in one order.** The remap
decides what a code *is*; the keymap decides what it *types*. A code with a
role never reaches the keymap, which is why Caps-Lock-as-Control is a remap
and could not have been a keymap entry — a modifier is not a character. The
roles are `none`, `shift`, `control`, `alt`, `altgr`, `meta`, `caps_lock` and
`num_lock`; `none` swallows the key, which is how "disable this key" is said.

A remapped code does not *become* the code it was mapped to. The held set
stays keyed by the physical code, so holding a real Control and releasing a
remapped Caps Lock leaves Control held — which it would not if the remap
rewrote the code, since the two would then share one held bit.

```text
PATCH /v1/input/devices/{device_id}
  { "configuration": { "repeat_delay_ms": 250, "repeat_rate_hz": 30,
                       "num_lock_boot": true,
                       "modifiers": { "58": "control", "125": "none" } } }
  -> read repeat_period_ms and repeat_rate_hz back from the response
```

An empty `modifiers` object clears the map. Every property is per device: two
keyboards on one server can carry different layouts, different repeat rates
and different remaps, which is what makes per-device routing usable.

## Original font icon discovery

`GET /v1/fonts/icons` returns the canonical packaged versioned catalogue for
Modern/Retro: seven original intrinsic-colour icons, their hexadecimal scalar
mappings, one-cell allocation, four Wi-Fi signal levels and standard text
fallback additions. Authentication is the same as other read endpoints. A
missing/unreadable/oversized catalogue returns503
`font_icon_catalogue_unavailable`; this request never changes the selected
font or the screen. See [the icon contract](../assets/fonts/icons/README.md)
and [full developer glyph reference](font-reference.md) for generated category
tables, semantic IDs, JSON encodings, width/colour rules and safe fallbacks.

`GET /v1/fonts` remains authoritative for the active profile and exact pixel
size; use `PUT /v1/fonts` to select Modern/Retro. Bitmap-only profiles do not
gain these colour glyphs. Clients must select the documented characters;
existing ASCII/Unicode/Nerd UI modes do not automatically become multicolour.
Do not equate the active `glyphs`/`mappings` PSF fallback counters with the
repertoire of the attached outline/emoji faces.

## Diagnostics and screenshots

`GET /v1/diagnostics/observation` is the canonical incident snapshot. It is
authenticated and read-only, and returns the version, server posture,
backend/resource diagnostics, runtime counters and logs, graphics/displays,
audio, input devices/routes, topology, fonts, settings, login manager,
terminal sessions, effects, and overlays in one response. It is assembled
during one event-loop turn from the same serializers as the focused endpoints;
the `consistency` field says `single_event_loop_turn` and
`captured_monotonic_us` identifies when the capture began. It intentionally
does not include terminal cell contents or framebuffer bytes: pair it with a
surface snapshot and `GET /v1/diagnostics/screenshots/{display_id}` when pixel
evidence is required.

For SDL presentation, `diagnostics.render.video_driver` is the exact SDL video
driver selected at startup (for example `kmsdrm`); `direct_kms` is `true` only
for Blit's separate direct DRM/KMS backend. `renderer` is the SDL renderer
(for example `opengles2`), not the video driver.

From candidate568, SDL output initialization records these codes in the
existing bounded runtime log; details identify the requested display:

| Code | Failed boundary |
| --- | --- |
| `sdl_output_window_failed` | Window properties/allocation |
| `sdl_output_binding_failed` | Actual window display differs from requested display |
| `sdl_output_fullscreen_failed` | Fullscreen activation |
| `sdl_output_renderer_failed` | Renderer creation |

An explicit `sdl_kms_fallback` means native KMS took over, not that SDL
succeeded. Read the actual backend and inventory independently. If startup
blocks before the listener exists, these API endpoints cannot service recovery;
independent startup rollback is tracked in Blit109. Candidate568's mocked-
display tests do not establish physical connector discovery or scanout.

Each `render.outputs[]` entry's `present_calls`, `last_present_ms` and
`max_present_ms` belongs to that output's resource lifetime. SDL values count
and time `SDL_RenderPresent` calls; direct KMS counts submitted frames.
An idle output does not inherit another output's counts or timing. Runtime
performance totals remain aggregate counters. SDL call completion does not
prove physical scanout; direct-KMS flip/vblank evidence remains in
`kms_scanout`.

To select a presentation path, read `GET /v1/settings`, preserve its complete
`document`, set `document.server.display_backend` to `kms` or `sdl`, and save
it with `PUT /v1/settings` and the current revision in `If-Match`. For SDL,
`document.server.renderer` selects `software` or `opengles2` on the supported
bare-console paths. The saved backend overrides the service's boot default.
The response reports `effective.display_backend_requested`,
`effective.display_backend_active` and `effective.presentation_restart_required`.
Saving leaves current displays/sessions running. Confirm storage is `clean`
before `POST /v1/control/restart`; verify `/v1/version` and the observation's
backend/video-driver/renderer identity afterward. Restart recreates sessions
through the existing login/auxiliary policy. A missing KMS device leaves the
API reachable for recovery and reports not-ready rather than claiming an
output exists. Resetting settings restores the boot preference and likewise
requires restart when it differs from the active path.

```sh
curl --fail --header "Authorization: Bearer $BLIT_AUTH_TOKEN" \
  http://HOST:8080/v1/diagnostics/observation > observation.json

# A compact operator summary, retaining the full observation on disk.
jq '{version, server: {version: .server.server_version,
   backend: .server.display_backend, listener: .server.listener},
   health: .diagnostics.health, resources: .diagnostics.resources,
   render: .diagnostics.render, performance: .runtime.performance,
   sessions: .sessions.count, displays: .displays.count}' observation.json
```

Use `GET /v1/diagnostics` for current health/resource state and
`GET /v1/diagnostics/runtime` for counters. The runtime response includes
present calls, measured frame/render intervals, surface-producer cadence,
missed deadlines, input
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

For a performance report, capture `/v1/version`, `/v1/displays`, and
`/v1/diagnostics/runtime` at the start and end of the same bounded workload.
Compare `present_calls`, `frame_rate_hz`, `surface_commit_hz`, frame/render intervals, missed deadlines, queue
pressure, and input dispatch counters. Do not report a frame rate from a
screenshot timestamp or HTTP request latency.

Each `diagnostics.render.outputs[]` entry includes `kms_scanout`. On direct
KMS, `present_calls` counts submitted frames while
`kms_scanout.flip_completions` counts completed page-flip events. The
`vblank_sequence`, `last_vblank_timestamp_us`, `vblank_span_count`, and
`vblank_span_us` fields expose the kernel's output timing; its
`observed_refresh_hz` is the vblank rate, not the rate of changed content.
`available` is false and the KMS-only counters are zero for SDL renderers.
The top-level `frame_rate_hz` remains an overall Blit composition rate; it is
not a per-display scanout measurement. Use deltas of per-output completion
counters over a measured interval to evaluate delivered frames per display.
For pointer diagnosis, `pointer_change_requests` counts KMS present attempts
where the old or new pointer footprint intersects that output;
`pointer_frame_deferred` counts such attempts deferred for an uninitialized
output or pending prior flip; and `pointer_frame_submissions` counts submitted
frames containing a changed pointer position. A request may be counted again
while another output's flip is pending, so compare these counters with their
matching deferral/submission deltas instead of treating requests as unique
physical input events.
`GET /v1/diagnostics/runtime` also exposes
`performance.pointer_position_changes`: the count of accepted pointer-state
transitions after physical or injected input processing. Compare its delta with
raw `evdev_mouse_motion_events`, then with each output's pointer request,
deferral and submission deltas to isolate input processing from presentation.

For a reproducible performance report, include the exact `server_version`,
display mode/refresh, workload duration, `present_calls`, measured frame or
render interval, missed deadlines, input dispatch counters, terminal bytes,
queue pressure, and the trace window used. “The screen looked slow” is a
useful observation, but it is not enough to identify whether the producer,
surface commit, scheduler, renderer, KMS submission, or physical scanout is
the limiting boundary.

## Effects, overlays, and control

### Bounded presentation trials (0.1.569)

From572, `/v1/diagnostics` and observation `diagnostics.render.sdl_kms_mode`
report `legacy`/`atomic` for SDL KMS, or null for native/other drivers.
Pinned SDL3.4.16 defaults to its documented legacy-KMS path because secondary
atomic plane initialization is missing (ADR0155). Explicit environment
atomic=1 is refused with a backend diagnostic, not silently overridden. This
does not claim SDL timing, pixels, connector mode selection or hotplug parity.

`GET /v1/control/presentation-trial` reads the privileged helper's trial state.
`POST` accepts only `{"renderer":"opengles2","timeout_seconds":60}` (or
`software`, integer timeout30..120). It requires normal API authentication,
a clean native-KMS baseline, no authenticated desktop or other active non-login
terminal session, and the current settings revision in `If-Match`. The response
is202 with an opaque trial ID; startup happens asynchronously.

Before startup, the helper arms an independent systemd recovery timer. The
same service temporarily reads a frozen settings snapshot, with login disabled;
the normal saved document is never overwritten. During the trial, configuration
mutations are refused and `/v1/settings` explicitly reports `persisted:false`,
storage `trial-read-only`, and provenance `trial-snapshot`. Diagnostic surface,
terminal-session and input-injection fixtures remain available.

`DELETE /v1/control/presentation-trial` with `{"id":"<current32hexid>"}`
requests early restoration. Its202 body is the last observed state, not proof
of completion. Stale IDs receive409. Expiry also restores the normal service
without requiring the Blit listener or event loop. A `restored` helper state
means the normal restart was queued: verify `/v1/version`, actual backend,
output inventory and settings before calling the baseline restored. Updates
are refused while recovery remains active. Failures stay explicit; this is
not a guarantee of recovery from kernel/GPU D-state or system-manager failure.

Verification scope: local helper/dispatcher fault tests use mocked systemd.
On10.54, actual timer expiry restored the identical saved native configuration
after both software and GLES startup crashes. This proves recovery from those
observed failures, not recovery from every kernel/GPU hang or SDL correctness.
The API-only startup/recovery oracle is
`tools/probes/host_presentation_trial.rb`; it records recovery and SDL startup
as separate verdicts. See ADR0153.

`GET/PUT /v1/effects` manages bounded named effects such as `scanlines`.
`POST/DELETE /v1/overlays` manages explicit graphic overlays. Effects and
overlays are downstream of terminal state and cannot silently replace it.

`POST /v1/control/shutdown` is an asynchronous clean stop. It terminates
managed child process groups and releases outputs; it cannot restart itself.
Use the host supervisor to start it again. `/v1/control/restart` requests an
orderly restart of the exact executable currently serving the API, preserving
the listen port; this also works for a TCC-built development fixture without
silently switching to another installed binary. Package installation uses the separate
`/v1/control/update` flow and must be verified through
`/v1/control/update/status` and `/v1/version`.

## Stopping the server from the keyboard

`GET`/`PUT /v1/control/shutdown-key` programs a chord that stops the server,
defaulting to **Ctrl+Alt+Backspace** and **enabled**. It exists for the case
an operator actually hits: the screen is wrong or input is not reaching
anything, and the only other control channel is this API, which may be exactly
what they cannot get to.

```json
{ "enabled": true, "code": 14, "modifiers": ["control", "alt"] }
```

`code` is the Linux evdev key code; 14 is Backspace. Fields omitted from a
`PUT` keep their current value, and the chord is persisted (ADR 0067) so one
moved off a combination the desktop itself uses survives the next release.

What it does and does not do, because a safety valve that misfires is worse
than none:

- It performs the same orderly stop as `POST /v1/control/shutdown` and exits
  **0**, so a unit with `Restart=on-failure` leaves it stopped rather than
  bringing it straight back.
- It fires on **press only** — never on release, never on autorepeat.
- Every modifier it names must be held **on the keyboard the key came from**.
  Control held on one keyboard says nothing about a key pressed on another,
  and a chord that pooled them would fire on stray combinations. Holding
  *extra* modifiers still fires it.
- `meta` is refused with 400: it is not derived per keyboard, so a chord
  naming it could never match, and accepting one would leave an operator
  believing they had a working chord.
- `enabled: false` turns it off entirely, which is what a kiosk wants.

## Errors and compatibility

Failures use `application/problem+json` with a stable `code`, HTTP status,
title, detail, and structured context where applicable. Clients must branch on
the code, not parse human prose. Common codes include `route_not_found`,
`invalid_request`, `stale_revision`, `unsupported_capability`,
`hibr_desktop_exists`, and `hibr_desktop_owner_missing`.

The problem response has `type`, `title`, `status`, and stable `code`; many
failures also include machine-readable context. Log the code, status, request
correlation data, and safe context. Do not log bearer tokens, update
credentials, PTY contents, or complete binary payloads. A `404` for an unknown
route is not evidence that the server is down; verify `/v1/version` and then
check the requested path against the schema.

Unknown response fields should be ignored. Resource IDs are opaque. Events and
snapshots carry revisions/sequences so clients can detect gaps and resync from
the authoritative GET endpoint. A schema or semantic breaking change requires
a new API version and ADR; changing a renderer implementation does not.

### Recovery table

| Observation | First check | Correct response |
| --- | --- | --- |
| Connection refused | supervisor/process and `/v1/version` | Start or repair the service; do not start a second server on the same port |
| `404 route_not_found` | API version and OpenAPI path | Correct the route; this is not evidence that rendering failed |
| `409` ownership/revision | resource `GET` and `hibr-owner` | Resynchronize, then retry with the current revision or repair the stale owner |
| black or stale display | `/displays`, runtime, screenshot, surface summary | Separate output discovery, producer commits, renderer cadence, and pixels |
| motion but no click | routes plus `pointer_button` trace | Verify source, button capability, target session, and aggregate state |
| duplicate desktop | diagnostics and Hibr-owner resources | Retire the stale attachment and keep one primary owner |
| slow scene updates | `surface_commit_hz` versus `frame_rate_hz` | Fix the producer boundary; do not label producer cadence as display refresh |

## Safe client checklist

- Pin and verify `api_version` before use.
- Discover capabilities and output readiness.
- Keep resource IDs and revisions; use idempotency keys on retries.
- Treat `409`, `429`, renderer failures, and unavailable outputs as recoverable
  typed states where the problem code permits.
- Resync authoritative state after a stream gap.
- Delete temporary sessions, routes, and overlays in cleanup paths.
- Do not log bearer tokens, update tokens, package bytes, or private paths.
