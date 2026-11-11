# Blit API guide

This is the human-facing guide to the API. The normative contract is always
[`openapi/blit-0.1.yaml`](../openapi/blit-0.1.yaml); this guide explains the
resource model and safe ordering of calls without duplicating every schema.

## Native console ownership

`GET /v1/diagnostics/console` reports Blit's native VT ownership: the VT
number, whether it is active, the previous VT, the Linux keyboard mode (and
the errno if reading it failed), keyboard muting, whether a recovery process
is registered, and the main-loop watchdog timeout (normally 10 s, 120 s
during the privileged package-install exchange). `owner: unmanaged` means
Blit is not managing a VT. The endpoint is read-only and uses the normal API
authentication.

Native KMS reserves its own unused VT, never the active login VT, and mutes
its kernel keyboard handler while presenting. It restores the handler when it
releases the VT or stops. Physical Ctrl+Alt+F1..F12 switches VTs;
API-injected chords do not switch the host. While Blit's VT is not active,
physical events are discarded, not replayed on return; API input and
PTY/session work remain available. Acquiring the VT requires DRM master and
requests a full repaint. A failure leaves diagnostics available or stops the
server rather than claiming usable presentation. On exit Blit returns to the
previous VT if its own is still active. Losing the console descriptor or the
mute stops presentation rather than reporting stale ownership. This requires
privileged Linux VT access; a profile lacking it stays API-only with a
startup error, and never silently shares a console keyboard.

The recovery process handles main-process death and an expired heartbeat. It
does not cover a kernel or GPU hang, or the whole recovery group being
killed. `evdev_exclusive: false` is deliberate: keyboard muting covers all
keyboards without grabbing one of a combined device's separate evdev clients.
It does not stop a separately running gpm from reading pointer events;
pointer exclusivity is still open (issue 117). See
[ADR 0170](adr/0170-native-console-keyboard-ownership.md).

`GET /v1/diagnostics/processes` lists up to 16 processes of the logged-in
owner (Blit's own user before login), busiest first by cumulative CPU
(`pid`, `ppid`, `comm`, `cpu_ms`, `threads`, `rss_kib`). Diff two samples to
see what is using the CPU now.

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
| Physical scanout and refresh scheduling | Blit native KMS compositor | Report output modes, cadence, missed deadlines, and failures. |
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
| `503` | A bounded capacity is full (`http_capacity`, `stream_capacity`, `input_route_capacity`) or a dependency is unavailable | Apply bounded backoff and inspect runtime counters. |
| `4xx/5xx` problem | Typed failure | Branch on `code`, preserve safe context, and record correlation data. |

Never infer presentation from request latency. A session input receipt means
the PTY write was accepted; a frame is complete only when the surface/frame
revision or stream event confirms it.

## Endpoint and authentication

The default endpoint is `http://HOST:8080`. All public resources
are versioned under `/v1`; `/v1/version` is the first request a client should
make and returns both `server_version` and `api_version`.

When `BLIT_AUTH_TOKEN` is configured (the package generates one in
`/etc/blit/blit.env`), send:

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
{"server_version":"0.3.20","api_version":"0.1"}
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
`GET /v1/server`'s `capabilities` is the list to switch on, not the
version. Among its flags, the terminal's SGR extensions: `sgr-framed` (SGR
51 framed, with Blit's 51:3 thick and 51:5 rule, ended by 54),
`sgr-framed-bar` (51:2, a Slate title bar's rows in the cell's pens; 51:4
is its alias), `sgr-shadow` (66 and 67, a drop shadow under the cell's
glyph) and `sgr-icon-pen` (68:v and 69, the pen of Blit's colour icons in
that cell; the cells API's `style.icon_pen`). See
[the terminal reference](terminal.md#sgr).

## Presentation configuration ownership

Saved `/v1/settings` is the canonical configuration. Native KMS is the only
presentation backend (ADR 0184): `document.server.display_backend` and the
startup fallback `BLIT_DISPLAY_BACKEND` accept only `kms`. Any other saved
value is a 400 `invalid_settings_server`. An invalid environment value logs
`invalid_environment_backend` and, without a saved backend, leaves the API
available with no presentation, for repair.

`effective.presentation_configuration` reports where the backend choice came
from and whether the environment value is valid. These are derived
observations, never a second writable configuration.
`effective.display_backend_active` is `drm-kms`, or `unavailable` when no DRM
device could be opened (Blit then serves its API with no outputs and says why
in `/v1/diagnostics`). Do not keep competing writable backend preferences in
a control panel.

Change intent through the settings API, which saves it. Saving leaves
current displays and sessions running; a setting read at start (such as
`kms_compositor`) needs `POST /v1/control/restart`. Confirm storage is
`clean` before restarting, then verify `/v1/version` and each output's
`compositor` in `/v1/diagnostics`. A restart recreates sessions through the
login and auxiliary policy. Resetting settings restores the boot defaults,
which likewise need a restart to reach a start-time setting. Editing an
environment file needs a service restart to reload it; an in-process restart
inherits the original environment.

### Compositor

`document.server.kms_compositor` (ADR 0181) chooses how the native backend
composes a frame: `gles` (the default) or `cpu`. It is read when the backend
opens, so a change applies at the next start (`POST /v1/control/restart`).
`effective.kms_compositor` is the saved choice and `BLIT_KMS_COMPOSITOR` the
boot default. Any other value is a 400 `invalid_settings_server`. A GLES
failure at start or while running drops that output to the CPU compositor and
keeps presenting.

In `/v1/diagnostics`, each `render.outputs[].compositor` says what an output
actually uses:

- `active` (`cpu` or `gles`) and `requested`;
- `reason` when GLES was asked for and not used (no EGL/GBM, no scanout
  config, a lost context), and `renderer` (GL renderer and version);
- `gles_frames` and `gles_uploaded_rows` (rows sent to the GPU; unchanged
  rows are not resent);
- `gpu_composed_frames`, `cpu_composed_frames` (frames the CPU had to compose
  instead) and `cpu_reason` (why the last one was not GPU-composed);
- GPU time per GPU-composed frame: `gpu_timed`, `gpu_last_ms`, `gpu_max_ms`,
  `gpu_total_us`, `gpu_frames`, measured with `GL_EXT_disjoint_timer_query`
  when the driver has it;
- the last frame's `frame_quads`, `frame_draw_calls` and `upload_ms`.

Each output's `flip` object (`last_ms`, `max_ms`, `total_us`, `count`) times
page flips from submission to their completion event, for either compositor.

A GLES start is guarded. Blit creates `<settings path>.gles-starting` before
opening GLES and removes it once every GLES output has drawn and flipped
frames for 30 seconds. A marker still present at the next start means that
start never became healthy (a crash included): that run uses the CPU
compositor with `compositor.reason` `previous GLES start did not complete`,
logs `gles_start_guard`, and clears the marker, so a deliberate restart tries
GLES again. The saved setting is never rewritten.

### Frame timing

`document.server.kms_frame_timing` chooses when a native KMS frame is
composed. `immediate` (the default) composes as soon as the previous flip
completes. `late` composes each output's frame a lead before its next vblank,
predicted from the kernel's vblank timestamps, so input that arrives during
the refresh is shown at that vblank rather than the next. It applies live,
without a restart; `BLIT_KMS_FRAME_TIMING` sets the boot default.

The lead starts at 4 ms. It grows by 1 ms (up to half a refresh) when a frame
misses its vblank -- a flip completing two or more vblanks after the CRTC's
vblank number at submission, or the loop busy through the compose slot --
and shrinks by 0.25 ms (down to 2 ms) after 240 flips on time. Each output's
`flip` object reports `timing`, `lead_us`, `on_time`, `late`, `slot_misses`
and `submit_phase_ms`: the on-time and late flips counted by how many whole
milliseconds after the previous vblank each was submitted (17 buckets, the
last 16 ms and over). Late flips gathered in the last buckets were submitted
too close to the vblank. See ADR 0183.

`render.presentation_sync` in `/v1/diagnostics` names how frames reach the
panel: `drm-page-flip`, or `drm-page-flip-async` when
`server.kms_async_pageflip` is on.

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

### Authenticated identity

The `identity` object in `GET /v1/login` tells the login process apart from
the user its post-authentication hook selected. Before authentication it
reports `state: waiting`, reason `awaiting_authenticated_hook`, with null
UID/GID/PID and session kind. The default Blit-owned hook uses a private
inherited channel; Blit verifies the kernel sender credentials and ancestry
without an API token or any public registration endpoint. A verified example:

```json
{"state":"verified","reason":"kernel_credentials_verified",
 "uid":1000,"gid":1000,"sender_pid":12345,"session_kind":"desktop"}
```

The PID identifies the handoff sender, **not** the live held desktop owner.
Verification is not desktop readiness: the selected session can also be
`shell`. Native owner association is reported separately by `login.desktop`,
and auxiliary reconciliation requires a fresh association before launch.
Destroying or replacing the supervised login discards this identity.

No active login reports `unavailable/no_active_login`; a custom session
command reports `unavailable/custom_session_command`, and a setup failure
`unavailable/handoff_setup_failed`. Rejected messages expose null identity
fields and a reason (`root_sender`, `foreign_sender`, `invalid_message`,
`invalid_credentials`, `truncated_message` or `receive_failed`). A failed
handoff does not prevent ordinary desktop or shell startup; it prevents
trusting that login for later automation. See ADR 0125.

### Native desktop association

The `desktop` object in `GET /v1/login` observes the exact named login client
through Hibr's native `hold list` and `hold clients`, run as the verified
user. The packaged hook names the desktop client by connector (otherwise the
display name) plus one-based row-major pane ordinal, such as `blit-DP-1-p2`.
Auxiliary clients use the same rule, not their role or boot-time IDs. A long
or unsafe selector becomes a bounded readable prefix plus a hash of the whole
selector; duplicate inventory or an occupied managed identity refuses launch.
A duplicate native name reports `native_client_name_in_use`, never guessed
ownership (ADR 0135). A live pane reassignment keeps the client's birth name
until its next launch. This is not another desktop instance or a new
settings resource; Hibr owns preferences under the native name.

`state` is `inactive` before verified authentication or for a shell
selection; `waiting` when the native owner or client is absent; `ready` when
the exact client, grid, live owner UID and process start-time identity are
verified; `error` when the query fails or its result is invalid. A ready
result contains `owner` (PID, UID, `starttime_ticks`, union rows/columns),
`client` (the exact login client), and `clients` (all named clients the same
native owner reports, with signed row/column origins and primary flags).
Otherwise `owner` and `client` are null and `clients` is empty. This is
read-only native state, not a second layout database.
`observed_monotonic_us` is the last successful query's time; results expire
after three seconds. `query_in_flight` tells background work apart from a
current observation.

The observer has a two-second deadline and a one-second retry interval, and
neither account lookup nor Hibr IPC blocks presentation. Its clean
environment carries no API token and no inherited input, display or network
descriptors. Replacing the login cancels the query and invalidates its
generation. The helper never moves, promotes, detaches or starts a desktop
or client. A fresh association projects the login's ownership role for
native joins without enabling input projection (ADRs 0126 and 0127). The
observer uses the packaged `desktop` name and Hibr's default per-user hold
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
| Hardware | `GET /v1/graphics`, `GET /v1/displays`, `GET/PATCH /v1/displays/{id}`, `GET /v1/audio`, `GET/PATCH /v1/audio/{id}`, `GET /v1/audio/cards`, `GET/PATCH /v1/audio/cards/{id}` | Discover and configure the GPU, outputs, sound devices and mixers |
| Fonts/topology | `GET/PUT /v1/fonts`, `GET /v1/fonts/icons`, `GET/PUT /v1/topology` | Select the font and cell size, discover Blit icons, place panes |
| Settings | `GET/PUT/DELETE /v1/settings` | Inspect, replace, or reset API-owned server settings |
| Login | `GET/POST/DELETE /v1/login` | Supervise one root-owned Hibr login screen and its authenticated child session |
| Auxiliary displays | `GET/PUT /v1/auxiliary-displays`, `POST /v1/auxiliary-displays/apply` | Start and place Hibr on the other monitors |
| Terminals | `GET/POST /v1/terminal-sessions`, `GET/PATCH/DELETE /v1/terminal-sessions/{id}`, `POST /v1/terminal-sessions/{id}/input`, `DELETE .../graphics`, `POST/GET/DELETE .../output-capture` | Inventory, create, place, inspect, drive, and stop PTY sessions; reset their images; capture their output |
| Hibr | `POST /v1/terminal-sessions/{id}/hibr-owner` | Internal: the Blit-launched Hibr adapter registers its PID for resize reassertion |
| Surfaces | `GET/PUT /v1/surfaces/{id}/cells`, `GET /v1/surfaces/{id}/summary`, `GET/PATCH /v1/surfaces/{id}/scrollback` | Read and revision-update authoritative terminal state; scroll the view |
| Input | `GET /v1/input/devices`, `GET/PATCH /v1/input/devices/{id}`, `POST /v1/input/inject`, `GET /v1/input/routes`, `PUT/DELETE /v1/input/routes/{id}` | Discover, configure, and route physical or synthetic input |
| Presentation | `POST /v1/frames`, `GET /v1/stream` | Commit coherent frames and consume event/pixel streams |
| Graphics | `GET /v1/effects`, `GET/POST /v1/overlays`, `DELETE /v1/overlays/{id}` | Report pane effects; manage bounded overlays |
| Diagnostics | `GET /v1/diagnostics`, `/runtime`, `/console`, `/processes`, `/observation`, `/trace`, `/graphics-ledger`, `/screenshots/{display_id}` | Inspect aggregate state, timing, the console, busy processes, events, picture events and blinks, and evidence captures |
| Control | `POST /v1/control/shutdown`, `/restart`, `GET/PUT /v1/control/shutdown-key` | Stop or restart the server; program the keyboard chord that stops it |
| Updates | `PUT /v1/control/update`, `GET .../status`, `POST .../cancel`, `POST .../reset` | Upload and supervise a verified package update |

The schema contains 50 path templates (`api_schema_gate.rb` checks the
count). Keep this table synchronized when a path is added, removed, or
renamed; the schema remains the authoritative count.

### Method-level reference

This is the stable mental model for the paths; the OpenAPI file remains the
exact method, parameter, and response reference:

```text
GET    /v1/version                         compatibility handshake
GET    /v1/server                          capabilities and links
GET    /v1/graphics /displays /audio       hardware inventory
GET/PUT /v1/control/shutdown-key           the keyboard stop chord
GET/PATCH /v1/displays/{id}                rotation 0 only, brightness
GET/PATCH /v1/audio/{id}                   a PCM's default flag
GET/PATCH /v1/audio/cards[/{id}]           a card's mixer
GET/PUT /v1/fonts /topology                revisioned configuration
GET    /v1/effects                         pane effects (set in settings)
GET/PUT/DELETE /v1/settings                persisted server and font settings
GET    /v1/terminal-sessions               authoritative live session inventory
POST   /v1/terminal-sessions               create a PTY-backed session
GET/PATCH/DELETE /v1/terminal-sessions/{id} lifecycle and placement
POST   /v1/terminal-sessions/{id}/input    terminal input bytes
DELETE /v1/terminal-sessions/{id}/graphics explicit retained-graphics reset
POST   /v1/terminal-sessions/{id}/hibr-owner internal Hibr owner registration
GET/PUT /v1/surfaces/{id}/cells            authoritative cell snapshot/update
GET    /v1/surfaces/{id}/summary           cheap health check
GET/PATCH /v1/surfaces/{id}/scrollback     history and scrolled view
GET/PATCH /v1/input/devices[/{id}]         discover and configure devices
POST   /v1/input/inject                    synthetic input
GET    /v1/input/routes, PUT/DELETE /{id}  input routing
POST   /v1/frames                          explicit coherent frame boundary
GET    /v1/stream                          events and bounded pixel frames
GET    /v1/diagnostics*                    state, timing, traces, screenshots
GET    /v1/diagnostics/observation         one canonical operational state dump
POST   /v1/control/{shutdown,restart}      orderly service control
PUT    /v1/control/update, GET .../status  verified package supervision
GET/POST /v1/overlays, DELETE /{id}        bounded graphic overlays
```

A Hibr session's `hibr_attachment.settled` (in `GET /v1/terminal-sessions`)
is false while Blit is still repairing the desktop's startup or attach
layout by nudging its terminal size, and true once that is done. A client
that drives the desktop can wait for it before clicking.

A session's `pointer_shape` is its OSC 22 request: `requested` is the shape
name on top of its stack as the program sent it (or `null`), and `depth` the
stack's size. It is cleared by RIS, a soft reset, and the program exiting.

A session's `mouse_reporting` says what its program asked for: `mode`
(1000, 1002, 1003 or 0), `sgr` for the SGR encoding (1006) and `pixels`
for SGR-Pixels (1016), where a report carries the pointer's position in
pixels of the surface, one-based, instead of its cell. Under 1016 motion is
held to one report a frame (a fixed 16.7 ms), the newest
position kept, and a button report writes any held motion first.
`pointer_lock` is true while the session holds the pointer (mode ?7701), and
`pointer_lock_refusals` counts the requests refused because the session
lacked focus or had ended, so you can see which session a program asked on.

**Pointer lock, mode ?7701 (ADR 0198).** For games that turn with the
mouse.
- **Asking:** a program asks first, with DECRQM (`CSI ? 7701 $ p`). Blit
  answers `CSI ? 7701 ; 2 $ y` (settable, off) or `; 1` (on).
- **Engaging:** `CSI ? 7701 h` engages only while the session has Blit's
  focus. Otherwise it is refused, with `CSI ? 7701 ; 2 $ y`.
- **While locked:**
  - the pointer stays where it was, hidden;
  - the program is sent raw mouse counts (no sensitivity or acceleration)
    as `CSI < b ; dx ; dy M`, summed and at most one report a frame, with
    `b` = 35 plus modifiers, or 32 + button while one is held;
  - buttons and the wheel come as `CSI < b ; 0 ; 0 M/m`;
  - absolute motion is ignored;
  - no other session hears any of it.
- **Release:**
  - The program gives the lock back with `CSI ? 7701 l`, and is sent no
    reply.
  - Blit takes it back when focus moves to another session, the program
    exits or the session closes, on RIS or a soft reset, and when the
    program leaves its input unread for two seconds (it is stuck). Each
    time, Blit sends an unsolicited `CSI ? 7701 ; 2 $ y`.
  - The pointer reappears where it was locked.
- **No release key in Blit.** The desktop's own shortcut releases it.
- **Counters:** `pointer_lock_reports` and `pointer_lock_releases` in
  `GET /v1/diagnostics/runtime`.

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
  "$base/v1/server" | jq '{display_count,display_backend}'; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/displays" | jq '.displays[] | {id,name,active_mode,presentation_state}'; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/diagnostics" | jq '{health,resources,errors,warnings}'; printf '\n'
curl --fail --silent --show-error --connect-timeout 2 "${auth[@]}" \
  "$base/v1/diagnostics/runtime" | jq '.performance | {frame_rate_hz,present_calls,missed_deadlines,surface_commit_hz,input_events}'; printf '\n'
```

Interpret the result in this order:

1. `/version` proves which binary and API contract answered.
2. `/server` shows whether the display backend is ready and how many
   displays it found.
3. `/displays` proves discovered outputs and their active modes; it does not
   prove that a desktop has supplied content.
4. `/diagnostics` gives health, resource counts, and the errors and warnings
   a status check should act on.
5. `/diagnostics/runtime` separates presentation cadence, producer commits,
   missed deadlines, input dispatch, and recent bounded events.
6. `/diagnostics/screenshots/{display_id}` proves pixels for one output; pair
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
should keep the returned session IDs. A login-owned join requires fresh
`GET /v1/login` desktop readiness, not authentication alone. The associated
login projects ownership role `primary`; Hibr's primary-display choice is
the separate native `login.desktop.client.primary` flag. Omitting `user`
derives the authenticated account in the child, without the service's API
token. A conflicting explicit account returns 409
`hibr_desktop_identity_mismatch`; an occupied or missing logical pane returns
409 `hibr_join_pane_unavailable`. Delete test sessions at the end and confirm
`/v1/diagnostics` reports zero sessions and surfaces.

Native clients receive input through their own PTYs in local coordinates;
Hibr alone translates it into the current 2-D union. Blit does not forward
joined input to the primary or add a horizontal slot offset. An optional
`hibr_join_position: {"row": -67, "column": 0}` requests initial native
placement through the Blit launcher; signed values are bounded to ±32767 and
both fields are required. This is a logical-cell example, not a fixed monitor
size or automatic layout policy. It is invalid on non-join and standby
requests. The launcher never moves the primary or reasserts later user layout.

A recognized `--standby`/`-w` is an auxiliary client, not another desktop
owner. Hibr owns its waiting and deliberately-released behaviour. Login-owned
clients are retired asynchronously when their authentication generation ends
or a fresh observation proves native owner replacement. Temporary query
errors alone do not trigger destruction. Saved auxiliary policy is described
under "Live auxiliary displays" below.

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

Configure each output's pane split through `PUT /v1/settings`: read the full
document, edit `display_layouts`, then replace it with its current
`If-Match` revision. For example:

```json
"display_layouts": {
  "DP-1": {"columns": 1, "rows": 2},
  "HDMI-A-1": {"columns": 1, "rows": 1}
}
```

Each dimension is 1–8; columns precede rows, so 1x2 is stacked and 2x1 is
side-by-side. A missing connector entry means 1x1. Disconnected entries stay
saved. The limits are 16 configured outputs, 64 live panes and 16
simultaneous terminal sessions; these are different resources. Invalid or
physically sub-cell grids return 400; removing an occupied pane returns 409
`topology_in_use` without changing settings. Detach it explicitly first.
Surviving terminal IDs/processes retain their pane ordinal and receive PTY
resize. Hibr Displays still owns native desktop arrangement.

### Pane effects

`document.effects` gives each pane its effects (ADR 0186, 0187). It is keyed
like `display_layouts`, by stable connector, then by pane ordinal. Changes
apply live, with no restart, and are saved:

```json
"effects": {
  "enabled": true,
  "DP-1": {
    "1": {"corners": {"radius_px": 24}, "curvature": {"amount": 40}},
    "2": {"enabled": false, "corners": {"radius_px": 24},
          "scanlines": {"intensity": 40}, "glow": {"intensity": 30}}
  }
}
```

| Effect | Class | Parameter | Range | What it draws |
|---|---|---|---|---|
| `scanlines` | local | `intensity` | 0–100 | Every second pixel row from the pane's top, darkened by that percentage |
| `corners` | local | `radius_px` | 0–1024 | Each corner of the pane cut to a quarter circle in black, anti-aliased |
| `curvature` | presentation | `amount` | 0–100 | The pane bulges like a tube; its edges become arcs, black beyond them |
| `glow` | presentation | `intensity` | 0–100 | A soft bloom around bright content, from a blurred copy of the pane |
| `rgb_shift` | presentation | `amount` | 0–100 | An old set's colour fringe: each channel leans to one side, up to half a cell apart |
| `glowing_line` | presentation, animated | `intensity` | 0–100 | "Hum bar": a soft band of light sweeping down the pane every 6.7 s (cool-retro-term's glowing line) |
| `flicker` | presentation, animated | `intensity` | 0–100 | The whole pane brightens or dims a little, at random, each frame (up to ±50% at 100) |
| `horizontal_sync` | presentation, animated | `amount` | 0–100 | On a random share of frames the picture sways sideways in a sine wave (up to 12% of the width at 100) |
| `jitter` | presentation, animated | `amount` | 0–100 | A fine shimmer of the sample points; grows with the square of the amount |
| `static_noise` | presentation, animated | `amount` | 0–100 | Grey grain over every pixel, fresh each frame, up to half of white at 100 (ADR 0207) |
| `chroma` | presentation | `amount` | 0–100 | Colour toward the phosphor's: 0 full colour, 100 monochrome in the phosphor colour |
| `phosphor` | presentation | `color` | 0–16777215 | The colour chroma tints toward, as 0xRRGGBB; 0 is none (white) |
| `brightness` | presentation | `level` | −100–100 | The picture times 1 + level/100; 0 neutral, −100 black |
| `contrast` | presentation | `level` | −100–100 | About mid-grey, times 1 + level/100; 0 neutral, −100 flat grey |
| `burn_in` | presentation | `amount` | 0–100 | Phosphor persistence: what the pane stops showing fades to black over 0.16–1.6 s (ADR 0208) |
| `mask` | presentation | `amount` | 0–100 | A CRT's phosphor mask on the display's physical pixels: each passes one of R, G, B and dims the others to 1 − amount/100, lifted back by the mask's mean (ADR 0209) |
| `mask_type` | presentation | `kind` | 0–2 | The mask's pattern: 0 aperture grille (default), 1 slot mask, 2 shadow mask; drawn only through `mask` |

- **Order:** effects are applied in a fixed order (scanlines, then corners),
  over the pane's pixel region, beneath the pointer and API overlays.
  Pictures inside the pane are under them.
- **Clamping:** a radius beyond half the pane's shorter side is drawn, and
  reported, at that half.
- **Refusals:** an unknown output character, a pane ordinal outside 1–64, an
  unknown effect or parameter, and a value out of range are
  `400 invalid_settings_effects`.
- **Absent panes:** entries for absent outputs or panes are kept and apply
  when they appear.
- **Switches:** `effects.enabled: false` turns every effect off, and a
  pane's own `"enabled": false` turns its effects off. The values are kept.
  Switched off, the compositors take exactly the path of a pane with no
  effects, so nothing is spent on them. `GET /v1/effects` reports `enabled`
  for the master and for each pane, and a switched-off pane's `effects` as
  empty.
- **Compositors:** both apply local effects, and the GPU keeps composing.
  Presentation effects are drawn only by the GLES compositor, as it presents
  the frame (ADR 0187). Elsewhere they are kept, not drawn, and reported per
  pane under `unavailable`, with the reason (`cpu compositor`, `no highp
  fragment precision`). When a pane's curvature is drawn, its scanlines are
  drawn on the glass, in screen space, rather than bent with the picture.
- **Animation (ADR 0188):** an effect marked `animated` is driven by a frame
  clock. The clock runs only while such an effect is on, switched on, and
  drawable on a presenting display. Its rate is per output, from
  `document.server.effects_animation_hz`:
  - `"auto"` (the default, also when the key is absent): the output's own
    refresh, the mode's rate refined by the measured one when they agree,
    60 when neither is known;
  - a number from 10 to 240: a ceiling the display can keep. The output
    ticks every Nth vblank, N = ceil(refresh / setting), so its rate is the
    largest at or below the setting that divides the refresh into whole
    vblanks: 30 on 60 Hz is 30, 50 on 60 Hz is 30, 25 on 60 Hz is 20, 120 on
    144 Hz is 72, and 240 on 60 Hz is 60.

  Each tick re-presents the frame without redrawing anything, just after a
  vblank. A tick only fills a gap: an output that presented within the last
  three quarters of its period, or is inside a quiet window or a Hibr frame
  barrier, is not ticked, since real frames carry the animation themselves.
  With no animated effect on, an idle screen presents nothing.
  Diagnostics: `effects.animation_hz` (the output's rate, 0 when off),
  `animation_frames` and `animation_phase`; `effective.effects_animation_hz`
  in `GET /v1/settings` is the setting in use (`"auto"` or the number).
- **Names:** `GET /v1/effects` gives each effect a `title` for people
  ("RGB shift") beside its key.
- **Moving effects (ADR 0190):** flicker, horizontal sync and jitter take
  their randomness from the frame number, so each frame is reproducible. A
  pane with horizontal sync or jitter has its corners drawn by the
  presentation pass, so they stay still while the picture sways. Pointer
  input follows curvature only, never the sway.
- **The tube's colour (ADR 0207):** chroma, brightness and contrast are
  applied in that order, after every light the pass adds (glow, the hum
  bar, static noise, flicker), so a monochrome tube colours them too, and
  before the corners and glass scanlines. Brightness and contrast are the
  only signed parameters; `GET /v1/effects` gives each effect's `minimum`
  beside its `maximum`. A phosphor with chroma at 0 changes nothing.
- **Burn-in (ADR 0208):** each burning pane keeps a history texture of its
  logical rectangle. Every presented frame, before the scene changes, the
  history loses `elapsed × 255 / fade` whole levels (the remainder carried)
  and takes the brighter of itself and the picture still shown; the pass
  reads the brighter of scene and history wherever it samples, so the
  trail is warped, shifted and coloured like the content. The fade is
  0.16 s + 1.44 s × amount/100, linear, to exactly black. It is not
  `animated`: the frame clock runs only while a trail is fading, then
  stops. A change of the pane's rectangle (layout, resize, zoom, mode)
  starts a fresh history with nothing fading; switched off, the texture is
  freed. It needs `GL_EXT_blend_minmax`; without it, it is reported
  unavailable as `no blend minmax`. Glow is built from the scene, so the
  trail does not bloom.
- **Pointer:** under curvature a click reaches the cell the screen shows at
  that point; a click in the black margin reaches the nearest edge cell.
- **Cost:** each output's render diagnostics report it:
  - `effects.cpu_last_ms`, `cpu_max_ms` and `gpu_quads` (local effects);
  - `present_panes`, the panes the presentation pass drew last frame (0 when
    no pane has presentation effects: the plain path);
  - `glow_rebuilds`, blurred copies rebuilt, only when a frame changed;
  - `burn_in_panes`, burn-in histories held, and `burn_in_faded`, the levels
    the freshest trail has faded (255 when nothing fades);
  - `presentation_unavailable`, empty when the output can draw them.

`GET /v1/topology.outputs[].layout` reports each actual grid. Its top-level
`layout` and `server.pane_layout` are legacy bulk controls, not the per-output
answer. Legacy startup settings migrate for the currently connected outputs
only; a later new monitor does not inherit another output's split.

`PUT /v1/fonts` applies the selected profile and size to pixels, not only to
metadata. The reported `cell_pixels` dimensions are the physical cell metrics
used by the compositor, PTY resize, pane/display geometry, pointer
hit-testing, and Kitty/Sixel placement; a size change does not rely on a
scaling transform. A display-attached Hibr session is reflowed to the new
display grid and receives the existing resize/reassert contract. Hibr source
is never modified by this operation. Pixel acceptance is covered by the local
font API gate and should be repeated with the live screenshot endpoint after
deployment.

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

### Display settings: rotation, brightness, zoom, and what power costs

A display's mode is not writable (ADR 0064): selecting a mode changes the
render output size and therefore every surface on the display, and a Hibr
desktop's cells come from its owner (ADR 0063), so applying one needs a
resize protocol with that owner (issue #60). A PATCH naming `mode` is a 400
`invalid_display_property`.

What is writable is the settings that leave the mode alone, and
`capabilities` says which of them this hardware can actually perform. It is derived, not
declared: `configuration` offers exactly what `capabilities` claims, and a
PATCH accepts exactly that. A property that takes a value and does nothing
is worse than its absence.

```text
mode-report   always
brightness    only where a backlight is attributed to the display.
zoom          every KMS display (ADR 0204).
```

**`rotation`** is not a capability: native KMS composes straight into the
connector's framebuffer and has no rotation transform. A PATCH with
`rotation: 0` is accepted, since that is what every display is; `90`, `180`
or `270` is a 422 `display_rotation_unsupported`, and any other value a 400
`invalid_display_rotation`.

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

**`zoom`** is pixel zoom (ADR 0204): the whole display, pictures included,
magnified as an image. It is a percentage, a multiple of 25 from 100 (none)
through 400; anything else is a 400 `invalid_display_zoom`. A zoomed
display's logical size, its `bounds`, is the mode times 100 / zoom rounded
down (2560x1440 at 150% is 1706x960); panes, cell grids, the topology, hit
testing and the pointer's position all work in it, and its sessions are
resized to it as a font change resizes them, that display's only. The
winsize is logical too: `ws_xpixel` is the columns times the unchanged cell
width (rows likewise). The image is composed at the logical size and
enlarged nearest-neighbour: physical (x, y) shows logical (x·100/zoom,
y·100/zoom), the last logical row and column repeated where the mode is not
a whole multiple. The pointer and the performance panel are drawn after the
enlargement at their normal size, relative pointer motion is physical
(the pointer crosses a zoomed display as fast as any other), and
screenshots are physical. Pane effects are drawn on a zoomed display as at
100%, in logical pixels, and enlarged with everything else: on GLES the
presentation effects (curvature, glow and the rest) included. A zoom other
than 100 is persisted in the settings
document as `displays.<connector>.zoom`, keyed by `state.connector` (`DP-1`,
not the display's `name`; `name` only where there is no connector), so it
follows the monitor and is applied before the first frame. An entry under
the `name`, as 0.3.26 wrote it, is read and moved on the next save.

**Power is reported and not settable.** `state.power` carries the connector's
DPMS state where a DRM connector was matched to the display, so an operator can
see that a panel is asleep -- but `power` is deliberately not in
`capabilities`, because nothing can set it yet. Two measurements on the live
host say why:

```text
/sys/class/drm/card0-DP-1/dpms   mode 444; a root write returns EACCES
/dev/dri/card0                   one open fd, held by blit (DRM master)
```

So sysfs is not an avenue -- that is the kernel -- and setting the property
needs DRM master, which Blit holds; that is the route issue #69 takes.
`/sys/class/graphics/fb0/blank` is not an alternative: fbdev emulation is one
device for the whole card, so it could not express "turn off this one
display" even if it worked behind a master.

On this host there is **no backlight at all** (`/sys/class/backlight` exists and
is empty), so `brightness` is absent there and that is the answer rather than a
gap. The code path is real and gated locally against a fabricated backlight
under `BLIT_BACKLIGHT_ROOT`.

One limit is worth knowing rather than discovering: whether the **unprivileged
profile** (ADR 0065) can write `/sys/class/backlight/*/brightness` is not
measured. The attribute is typically `root:video` with a udev rule granting the
seat's user, the gate runs against fabricated files, and the live host runs as
root with no backlight. It is the same unmeasured-group caveat as the ALSA
control device in #68, and it belongs to an operator who switches profiles on
hardware that has a panel.

### Explicit emoji cell layout

`PUT /v1/surfaces/{id}/cells` accepts an optional `text_layout`:

- `scalar` (default): Unicode 15.1 scalar widths and zero-width attachments.
  Positioned Hibr providers are unchanged.
- `emoji`: group pinned Emoji 18.0 sequences within each cell's `text` value.
  A completed multi-scalar sequence occupies two columns; singletons and
  ordinary text keep their widths. Grouping never joins across positioned
  cells or requests.

For example,
`{"base_revision":7,"text_layout":"emoji","cells":[{"x":0,"y":0,"text":"👩‍💻X"}]}`
places the emoji at 0, its continuation at 1 and X at 2. Snapshots return the
complete sequence on its anchor. Overwriting either half clears the previous
wide ownership. A wide cluster that cannot fit the right edge is clipped
whole, following the API's usual clipping (the request can still be
acknowledged). An invalid layout returns 400 `invalid_text_layout` before any
cell changes. This cell-update policy does not alter the owning session's
PTY policy.

`POST /v1/terminal-sessions` also accepts `text_layout`, for example
`{"command":"/bin/cat","text_layout":"emoji"}`. It is fixed for the lifetime
of the session and returned by session observations; a PATCH returns 400
`immutable_text_layout`. The default, `scalar`, keeps Hibr's widths. `emoji`
uses the same pinned sequence trie across PTY reads, and supports late
widening and whole-cluster REP and wrap. With autowrap disabled or a single
available column, allocation stays one column rather than spilling outside
the viewport. Controls and successful positioned updates end an active
extension; incomplete UTF-8 bytes do not. This is not DEC mode 2027 or
general Unicode grapheme support.

On resize, overlapping whole cells survive; a wide cell cut by the new right
edge is removed whole, including its retained anchor. Both the active and the
saved alternate buffer are normalized. Growing the grid cannot resurrect
clipped attachments or link IDs. A resize ends streaming emoji-extension
eligibility, without resetting the terminal or its last printable REP value.
This is grid clipping, not paragraph reflow.

Presentation follows pinned Unicode 18.0 data: default-emoji scalars and
standardized VS16 pairs select colour; standardized VS15 pairs select a
monochrome outline. Outline fonts share a monochrome Noto Emoji fallback for
text presentation the font itself lacks. Presentation never changes the
owner's allocated columns.

### Fonts

`GET /v1/fonts` lists profiles and the active cell size. `PUT /v1/fonts` can
select `font_id`, legacy `scale` from 1.0 through 3.0 in 0.1 increments, or
`size_px` (integer 8..64, exact cell height). Select a profile together with
either size control, never both size controls. Profile-only selection keeps
the active size policy. `active.sizing` is `scale` or `pixels`;
`active.size_px` and `active.cell_pixels` report the actual result. A scale
returned under pixel sizing is derived and need not be a valid scale write.
For example, `{"size_px":23}` selects 12x23 cells for the 8x16 font. This is
font and cell sizing, not a display mode change. Outline fonts
(`rasterization: "native-antialiased"`) are drawn at a fitted native size.
Pixel fonts (`"bitmap-nearest"`) keep their own pixels for every glyph they
have and take the rest from the glyph stack, so both kinds report
`colour_glyphs: true` (ADR 0199). Select smooth text, for example, with
`{"font_id":"hack","size_px":23}`.

`hinting` (`full`, `light` or `none`; default `full`) sets how FreeType
hints text glyphs (ADR 0218): `full` is the target-normal hinting text has
always used, `light` the light autohinter target, `none` no hinting. It can
be sent alone or with the other controls, `active.hinting` reports it, and
anything else is a 400 `invalid_font_hinting`. A change rebuilds the glyph
cache and redraws, as a size change does. Fitted icons and symbols and cell
geometry ignore it. It is stored in `document.font.hinting`; a document
without it means `full`.

The pen of Blit's colour icons is not a font setting: it is a cell
attribute, SGR 68 (ADR 0221, which replaced 0.3.48's `icon_colour`; see
[the terminal reference](terminal.md#sgr)). A stored document that still
holds `font.icon_colour` loads; the key is ignored and not written back.

Fonts are discovered (ADR 0195):

- **What `GET /v1/fonts` lists:** every regular, monospaced TTF or OTF in
  the fonts root's `text/` folder or `/usr/local/lib/blit/fonts/text`, plus
  the narrow-cell PSFs in the root. A folder inside `text/` is one font of
  masters (ADR 0227), listed once: see below.
- **Each profile** has an `id` (its family name in kebab form, for example
  `jetbrains-mono-nl`), a `title`, a `source` (`shipped` or `local`), a
  `path`, `available` and `rasterization`.
- **`rejected`** lists the files that aren't fonts Blit can use, each with
  a `reason`.
- **The list is read fresh on each request,** so a font copied in appears
  without a restart.
- **Shipped fonts:** `hack`, `ibm-3270`, `jetbrains-mono-nl`, `fira-code`,
  `ibm-plex-mono`, `spleen` and `blit-terminus16`. Without a saved
  selection Blit boots with `blit-terminus16`.
- **Glyph coverage:** every outline font draws icons, symbols, box drawing,
  Braille, blocks, Arabic, emoji and Unifont's coverage from Blit's glyph
  stack, fitted to that font.
- **Retired ids:** `blit-modern`, `blit-retro`, `blit-jetbrainless`, the
  console fonts, and `blit-spleen16` (0.3.68, replaced by `spleen`, ADR
  0227). A saved selection naming one falls back to the boot font; there
  are no aliases.

A font of masters (ADR 0227) is a folder in `text/` whose font files are
pixel fonts drawn as outlines, each crisp at its own cell height and its
whole multiples. Spleen is one: `text/spleen/` holds Spleen 2.2.0's 6x12,
8x16, 12x24, 16x32 and 32x64 masters, the larger ones redrawn by its author
rather than scaled.

- **Listed once:** its `id` and `title` come from the words its masters'
  family names share (`Spleen`), its `path` is the folder, and
  `profiles[].masters` lists each master's design cell, smallest first:
  `["6x12","8x16","12x24","16x32","32x64"]`. A master is never a font of
  its own; one that is not on a pixel grid, or repeats another's height,
  is in `rejected` with its reason.
- **One master per size.** For a cell height H: the largest master whose
  height divides H (crisp); else the largest master below H (scaled); else
  the smallest. 16 px is 8x16, 48 px is 12x24, 20 px is 8x16 scaled.
  `size_px` and `scale` changes pick again; a display's zoom does not, since
  it enlarges the composed image (ADR 0204).
- **`active.master`** names the master in use (`"8x16"`) and
  **`active.master_fit`** is `crisp` or `scaled`; both are null for any
  other font.
- **Crisp is exact:** every design pixel is a whole square of device pixels,
  fully on or off, and the hinting setting doesn't apply (FreeType's
  hinter moves whole glyphs a pixel at some multiples). Scaled sizes are
  drawn as any outline font, with the hinting setting.
- **Coverage:** what a master lacks (6x12 has no Latin Extended-A, for
  one) comes from the glyph stack, as for any text font.

Discover available profiles through `GET /v1/fonts`, not a version
comparison. `colour_glyphs: true` does not imply full emoji-sequence
shaping; see issue 110 and [font scope](../assets/fonts/README.md).

`active.rasterization` is `bitmap-nearest` or `native-antialiased`;
`active.colour_glyphs` reports the colour face, and `active.raster_cache`
reports bounded cached bytes and render count. `glyphs` and `mappings`
describe the loaded primary font file (its identity), not the coverage of
the glyph stack behind it. A profile or size change validates the rasterizer
before retiring the active state. Both native KMS compositors (CPU and GLES)
share the same coverage and colour source, fallback policy and cell sizing.

Every outline profile carries the Arabic companion `BlitArabicModern.ttf`:
`profiles[].arabic_companion_path` names it, and `active.arabic_companion`
reports the loaded `path`, `family`, `baseline_px` and
`shaping: "visual-cell-marks"`. Both are null for a pixel font. Availability
includes the companion file; a failed font or size replacement keeps the
active font. Arabic shares the Latin baseline and keeps ink that extends
beyond a cell. Hibr supplies display-order presentation forms; Blit
positions cell-local marks without applying a second bidi pass. Tested core
letters, lam-alef, diacritics and Arabic-Indic digits `٠١٢٣٤٥٦٧٨٩` are a
finite scope, not a promise of every Arabic or Quranic form.

A `scale` given as an integer, such as `2`, means `2.0`. A size change
recalculates pane geometry and resizes attached PTYs. Because a framebuffer
cell cannot be fractional, the physical width and height use one shared
nearest-pixel rule; for example, an 8x16 font at scale 1.1 is a 9x18 cell.
This is a display geometry operation; it does not change Hibr source or
terminal protocol state.

For example, apply a modest size increase without restarting the server:

```sh
curl --fail-with-body -X PUT "$BLIT_URL/v1/fonts" \
  -H "Authorization: Bearer $BLIT_AUTH_TOKEN" \
  -H 'Content-Type: application/json' --data '{"size_px":23}'
```

Read back `active.size_px` and `active.cell_pixels`: rounded cell
dimensions, not fractional physical pixels, determine the grid. An 8x16
source becomes 10x19 at scale 1.2. `PUT /v1/fonts` queues an automatic save;
poll settings storage for a clean receipt. To edit font sizing as part of a
complete configuration, read `GET /v1/settings`, update its complete
`document.font` with the desired profile and size, and `PUT /v1/settings`
with the returned revision in `If-Match`. Preserve the other document
sections and require `storage.state == "clean"`. A successful live font
response alone does not confirm a durable save.

When saving pixel sizing, set `document.font` to
`{"font_id":"…","size_px":23}` and remove the legacy `scale` key. Saving
both returns 400 and keeps the previous durable configuration. A scale write
likewise replaces pixel sizing rather than multiplying it. See ADR 0144.

Text foreground and background colours are terminal styles, independent of
font selection.

### API-owned server settings

`GET /v1/settings` is the authoritative view of persisted presentation
settings. The response has three relevant parts: `document` is the sparse
stored document, `effective` is what this process is using now, and
`revision` is the optimistic-concurrency value for replacement. It moves when
the document's content changes, live or by request, and not when the writer
later puts it on disk (that is `storage.saved_revision` catching up), so an
`If-Match` fails only when there is something new to read (issue #150). The document
holds `font` (`font_id` with `scale` or `size_px`, and `hinting`) and `server` values:
`render_interval_ms` (1–100), `pane_layout`, `display_backend`,
`kms_compositor`, `kms_frame_timing`, `kms_async_pageflip`,
`effects_animation_hz` (`"auto"` or 10–240; see "Animation"),
`graphics_session_budget_mib`,
`graphics_server_budget_mib`, `graphics_texture_budget_mib` (1–65536; see
"Picture memory"), `restart_after_update`, `expect_desktop` and
`shutdown_key`. The other
sections are `display_layouts`, `effects`, `pointer`, `login`,
`auxiliary_displays`, `input_devices`, `audio_devices` and `displays`.
Environment variables such as `BLIT_RENDER_INTERVAL_MS`, `BLIT_PANE_LAYOUT`,
`BLIT_KMS_COMPOSITOR`, `BLIT_KMS_FRAME_TIMING`, `BLIT_KMS_ASYNC_PAGEFLIP`,
`BLIT_EXPECT_DESKTOP`, `BLIT_PSF_FONT` and `BLIT_FONT_SCALE` are boot
defaults, used only when the document is silent.

Per-output pane layout is owned by `document.display_layouts`, applied live
by settings replacement. Legacy `PUT /v1/topology` applies one 1x1, 1x2, 2x1
or 2x2 grid to the connected outputs in bulk and refuses occupied layout
changes.
Prefer explicit connector-keyed layouts for independent monitors.

Replace the complete document with `If-Match` (omitted only for an initial
write):

```sh
curl --fail --request PUT \
  --header 'Content-Type: application/json' \
  --header 'If-Match: REVISION' \
  --data '{"document":{"version":1,"input_devices":{},"audio_devices":{},"font":{"font_id":"blit-terminus16","scale":1.1},"server":{"render_interval_ms":16,"pane_layout":"1x2","kms_compositor":"gles","kms_async_pageflip":false,"expect_desktop":true}}}' \
  http://HOST:8080/v1/settings
```

`DELETE /v1/settings` clears the document and restores boot defaults. It does
not alter authentication, bind address, command allowlists, update
credentials, or test seams. `BLIT_EVDEV_MOUSE` is a compatibility boot
override only; stable input devices and their API configuration are the
supported input boundary. See ADR 0089 and ADR 0067.

#### Login startup policy

`document.login` moves login placement into the same API-owned store:

```json
{"enabled":true,"preferred_pane_id":"pane-display-1-2","fallback":"wait"}
```

Add that object to the complete document read from `GET /v1/settings`; replace
with `PUT /v1/settings` and its revision in `If-Match`. Do not send this
fragment as the whole document: replacement would reset unrelated settings.
The saved policy is normalized to explicit values. Missing fields use boot
defaults; `preferred_pane_id:null` requests automatic selection. `fallback` is
`wait` or `first_available`. A missing preferred pane is valid intent,
retained until available; fallback assignment never overwrites it. Current IDs
are scoped to the backend topology, not portable monitor identities.

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
Save as default. Saved auxiliaries are loaded as the live policy at boot.
Later settings replacements affect the next boot; the current policy is
controlled by the separate Apply operation below.

#### Display hotplug

Native KMS reconciles connected outputs without restarting the service. A
read-only worker observes DRM uevents, coalesces bursts for 100 ms and probes
every five seconds as a fallback. The main thread owns output creation and
retirement and publishes changed display inventory and topology revisions.
Unchanged output resources and primary PTYs are kept. When the last output
disappears, the API stays available and PTYs keep their pane intent; primary
migration is not automatic. A new output uses its connector-keyed layout, or
1x1 when unconfigured (ADR 0131). Font selection and size are global.

`diagnostics.render.hotplug` (also in the runtime and observation snapshots)
reports `supported`, `observer` (`uevent+periodic`, `periodic`,
`unavailable`), `inventory_revision`, worker `scans`, `last_scan_ms`, and
`error`. A failed probe keeps the last inventory; a failed activation or
teardown is reported without destroying unrelated outputs. A failed teardown
quarantines resources until a later retry can disable scanout. An unknown
connector status also keeps its last-known output instead of inventing a
disconnection; inspect `hotplug.error` before treating retained inventory as
freshly verified presence. A new connection may stay unavailable if output
slots or compatible CRTCs are exhausted. Support targets the selected DRM
device, not multi-GPU ownership.

#### Live auxiliary displays

The auxiliary controller resolves saved and live connector bindings against
the display inventory. A removed managed auxiliary is retired; on its return
a new standby can be provisioned after fresh authenticated native readiness.
Its initial placement is verified once. Surviving clients' user placement is
not reasserted. Unmanaged and primary PTYs are not terminated by output loss.
New monitors are not added to the auxiliary set automatically; select them
explicitly through Apply and save them as defaults.

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

The disk work runs on its own writer thread (ADR 0189), so a slow or busy
disk never stalls drawing, input or the console watchdog. A `PUT` or `DELETE
/v1/settings` is answered once its change is durable, with the same receipt
as before. While a save is in flight, `GET /v1/settings` reports `pending`
with the previous `saved_revision`. A request whose save is not confirmed
within 30 s is answered 503 `settings_write_failed`; the save may still
complete, so read the storage state.
The writer syncs file contents and the parent directory around atomic rename.
Replacement never deletes the old file first. A forced save failure
returns HTTP 503 `settings_write_failed`, not success; changes may already
have applied in memory. Read settings and resolve the error before retrying.
Failure after rename but before directory sync means durability is unconfirmed,
not that the old file is guaranteed still present. Failed background writes
retry at the coalescing interval, not every event-loop turn.

### Output capture

`POST /v1/terminal-sessions/{id}/output-capture` records a session's raw PTY
output for diagnosis: the bytes exactly as the program wrote them, before
the terminal parser, for up to 120 s and at most 2 MiB. `GET` returns them
as base64, with each read's byte offset and time since arming. `DELETE`
frees the capture, as does closing the session. The cell snapshot shows the
result; the capture shows the sequence that produced it, such as a clear
arriving while a background colour was set. It can contain anything the
program printed, so it requires the API token like every endpoint, and is
only recorded while armed.

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

`pixel_frame_begin` accepts paired `placement_width` and `placement_height`
(integer 1..65535) in terminal pixels. `width`/`height`
remain source dimensions and alone determine RGB/RGBA payload length. The
destination starts at `x`,`y`; it is not a Chrome zoom setting. Omitting both
placement fields preserves source-sized placement for older producers. A
partial pair, non-integer, zero or out-of-range size is rejected as
`pixel_frame_error`/`invalid_begin` before changing the current image.
A picture past the session's ceiling or its picture budget is refused with
`"status": 409` and the code `image_store_full` or `image_budget` ("Picture
memory").
Blit's display adapter converts the callback's requested destination cells
using the current cell metrics and sends this pair.

The HTTP surface state and the stream are complementary. HTTP GETs are the
repair path; the stream is the low-latency observation path. A client must
resync after a sequence gap, unknown resource ID, reconnect, or rejected frame
commit. Never reconstruct a complete surface solely from deltas received
before a disconnect.

## Input

### Negotiated keyboard reporting

`GET /v1/terminal-sessions/{id}` exposes `terminal_capabilities.keyboard`:
`protocol: kitty`, `supported_flags: 11`, the current `active_flags` and the
saved `stack_depth` (0..16). These are observations, not a second API
setting: the program owns negotiation through its PTY. `CSI ? u` queries;
`CSI > flags u` pushes; `CSI < count u` pops; `CSI = flags ; mode u`
replaces, sets or clears bits with modes 1, 2 and 3. The main and alternate
buffers, and each session, keep independent state. Flags 4 and 16 are masked
out; only 1, 2 and 8 are implemented.

API key injection uses the physical keyboard dispatch. For example, send
`{"kind":"key","code":125,"value":1}` then value 0 for left Super; right
Super is 126. A program requesting all-key reporting receives its canonical
key event, not literal text. A Super chord is press Super, press key,
release key, release Super. Repeat and release encoding also require flag 2.
Without negotiated all-key reporting, a lone modifier stays silent. Input
stream events include logical `super` after remapping; the input-device
modifier role `meta` is the evdev Super role here. An external browser that
cannot capture an OS-reserved key may offer this as an explicit action; the
server cannot intercept that client's operating system. See ADR 0169 for
scope and acceptance limits.

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

#### Pointer appearance

Read `GET /v1/settings` → `effective.pointer` for active, default-filled
appearance. Edit `document.pointer` and submit the **complete** settings
document using `PUT /v1/settings`, `{"document": ...}`, with the settings
revision in `If-Match`. This applies live and saves through the atomic
writer. Inspect its storage receipt; stale writes return 409 and invalid
pointer objects return 400 `invalid_settings_pointer` before any change.

```json
{"shape":"arrow","palette":"pearl","size_px":32,"shadow":true}
```

Shapes: `arrow`, `hand`, `precision`. Palettes: `pearl`, `obsidian`, `neon`,
`amber`. Nominal size 24–64 integer physical pixels; up to six additional
pixels hold antialiasing and shadow. Defaults are arrow/pearl/24/shadow-on.
Missing fields use defaults; remove the object to reset. Shape-specific
hotspots preserve pointer hit-testing. One cached vector source drives the
pointer; size is independent of font scale. Motion does not rerasterize the
artwork.

A program may ask for another shape by OSC 22 (below); the settings stay the
display's own pointer and are never changed by it. `effective.pointer_shown`
reports what is drawn now: `{"shape":"text","hidden":false}`, where `shape`
is one of the API shapes or a program shape (`text`, `wait`, `move`,
`ew-resize`, `ns-resize`, `nwse-resize`, `nesw-resize`, `not-allowed`) and
`hidden` is true while the program under the pointer asked for `none`.
Program shapes take this palette, size and shadow; the API cannot select them
as the display's own pointer.

The Hibr control panel's `control-panel/blit` catalogue registers **Blit
Mouse** alongside Blit. Appearance and stable-ID device controls share its
staged settings draft and revision-checked Save. Hibr's separate Mouse pane
still owns window behaviour. Selecting a different pointer is not a
performance or input-latency guarantee.

For direct-KMS pointer stalls, inspect
`GET /v1/diagnostics/runtime` → `snapshot.render.outputs[].kms_phases`.
`draw_ms`, `copy_ms` and `submit_ms` separate the last frame's CPU scene work,
scene-to-scanout copy and kernel submission. `drawn_cells` counts requested
terminal damage across views (overlaps may be counted twice), not pointer
pixels. These are last-frame values, not latency quantiles;
sample repeatedly and correlate with CPU, loop gaps and physical input counts.

| property | what it is |
|---|---|
| `sensitivity_percent` | the linear multiplier, unchanged by anything below |
| `natural_scroll` | inverts the wheel direction |
| `buttons` | a sparse map from evdev code to the button it produces |
| `scroll_step` | how many reports one wheel notch produces |
| `acceleration` | `{"profile":"flat"}` or `{"profile":"adaptive"}` |
| `tap_to_click` | a touchpad only: a quick touch is a click (one finger left, two right); off by default |
| `tap_and_drag` | a touchpad only, with tapping on: a tap holds the left button for 180 ms, and a touch in that time drags it until lifted; tap, tap is a double click; on by default (ADR 0193) |
| `disable_while_typing` | a touchpad only: for 500 ms after each key typed (not modifiers) the pad does not move, scroll or tap; pressing it still clicks; on by default (ADR 0193) |
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

### Keyboard map (xkb)

`GET /v1/input/keymap` returns a keyboard as a complete xkb keymap, the text
libxkbcommon compiles (`xkb_keymap_new_from_string`,
`XKB_KEYMAP_FORMAT_TEXT_V1`), `Content-Type: text/plain`. It is how
blit-compositor gives its Wayland programs Blit's keyboard (ADR 0228; "Program
windows" below), and it is generated from Blit's own tables, not from a
layout name: each key's four levels (plain, Shift, AltGr, Shift+AltGr) are
what Blit itself types with them -- the built-in US table under the device's
`keymap` overrides, CapsLock inverting Shift for a lower-case letter only --
and a code with a `modifiers` role is that modifier (`control` is
`Control_L`, `altgr` is `ISO_Level3_Shift`, `none` is no key). Keycodes are
evdev codes plus 8; Shift, Lock, Control, Mod1 (Alt), Mod2 (Num Lock), Mod4
(Super) and Mod5 (AltGr) are the modifiers, and the indicators are "Caps
Lock" and "Num Lock", lit while Lock and Num Lock are locked (X programs
read them; Xwayland 24.1 crashes under a keymap with none). A level of more than one
character is its first character, since an xkb level is one keysym; the
keypad is its Num Lock-on keysyms.

The keyboard is `?device=<id>` when given (404 `input_device_not_found` for
none such), else the keyboard whose key came last -- the one whose keys
reach the window -- while it is there and enabled, else the first enabled
device Blit reads as a keyboard, else none, which is the built-in table
alone; `X-Blit-Keymap-Device` names it (`builtin`). A key pressed on
another keyboard makes it the default from that key on. The `ETag` is the
SHA-256 of the text, quoted, so it changes exactly when the keymap does;
`If-None-Match` with it is answered 304 with no body.

Both answers, 200 and 304, carry that keyboard's repeat, as the kernel
holds it ("Keyboard settings": the read-back, one source):
`X-Blit-Repeat-Rate`, repeats a second (from `repeat_period_ms`, as
`repeat_rate_hz` reports it), and `X-Blit-Repeat-Delay`, milliseconds
(`repeat_delay_ms`); `0` and `0` for a keyboard the kernel does not
autorepeat (no `repeat` capability), and for none. Repeat is not part of the
keymap's text, so a change of repeat alone keeps the ETag: a client asking
with `If-None-Match` reads the new repeat from the 304. These are what
`wl_keyboard.repeat_info` carries (rate 0: no repeat).

blit-compositor is sent this same keymap and repeat (one source) by
`INPUT keymap` on its input routes ("Input straight from Blit") whenever it
may have changed -- a keyboard's settings, a keyboard enabled, disabled,
plugged or gone, or the default moving to another keyboard -- so it needs
neither this endpoint nor a token; a client of the event stream sees the
same changes as `device_changed`.

```text
GET /v1/input/keymap
  -> 200 text/plain  ETag: "<sha-256>"  X-Blit-Keymap-Device: <id>
     X-Blit-Repeat-Rate: 30  X-Blit-Repeat-Delay: 250
     xkb_keymap {
     xkb_keycodes "blit" { minimum = 8; maximum = 255; <K1> = 9; ... };
     ...
     key <K30> { type = "ALPHABETIC", [ a, A ] };
     ...
```

## Original font icon discovery

`GET /v1/fonts/icons` returns the packaged, versioned catalogue of Blit's own
colour icons (status icons, the hui badge, telephones, file-manager icons):
each icon's id, codepoint, how many columns it spans, and the codepoints
retired and never reused. Its `cell_geometry` section lists the glyphs
Blit draws as cell geometry: the bar corners, the 70 window-chrome glyphs
(ADR 0210), the 20 controls (ADR 0212), the 13 outer-edge frame glyphs
(ADR 0214), the 10 pressed-box and grow glyphs (ADR 0219), the 4
minimise boxes (ADR 0222) and the 10 full-screen and sticky boxes
(ADR 0224), each with `colour`, `set` (`chalk`, `slate-light`, `slate`,
`capsule` or `frame`) and `pens` (`foreground`, or `cell` for Slate and the
capsule, ADR 0211). Slate dark's codepoints are under `retired`. An entry a
later glyph replaces carries `deprecated: true` and `replaced_by` (the ids
that replace it): Blit still draws it, unchanged, until a release retires
it, so a client moves at its own pace (ADR 0214). An `icons` entry can carry
the same two fields: since 0.3.55 `wifi-1`..`wifi-4` (the bars, the mobile
signal) are deprecated, `replaced_by` `cellular-1`..`cellular-4`; Wi-Fi is
`wifi-signal-1`..`wifi-signal-4` and `wifi-off`.

**Every cell-geometry entry says which pens to set**, as data, so a client
maps the glyphs without a table of its own: `part` (what it is a piece
of), `cells` (`{span, position}`: how many cells the piece spans and this
one's place in it), `background` (the cell's background should be the
title bar's, a panel, the wallpaper, a dialog's face or a button's),
`foreground` (ink, a capsule's track, or the bar's colour) and `line`
(what SGR 58 is: nothing, a highlight, a box's, dialog's or button's
face, or a knob). The words are in
[the icon contract](../assets/fonts/icons/README.md) and typed in OpenAPI.

**The catalogue has a revision.** The body's first member, `revision`, is
the SHA-256 of the catalogue file as packaged (the hash `fonts.source.json`
pins), and the response's `ETag` is the same value, quoted. A client that
keeps the catalogue compares it, or sends it back as `If-None-Match` and is
answered 304 with no body while it is current; the response is
`Cache-Control: no-cache`, so it may be kept and must be asked about. The
catalogue is about 61 KB; the endpoint's limit is 256 KiB. Authentication
is the same as other read endpoints. A missing, unreadable or oversized catalogue returns 503
`font_icon_catalogue_unavailable`; this request never changes the selected
font or the screen. See [the icon contract](../assets/fonts/icons/README.md)
and [full developer glyph reference](font-reference.md) for generated category
tables, semantic IDs, JSON encodings, width/colour rules and safe fallbacks.

`GET /v1/fonts` remains authoritative for the active profile and exact pixel
size; use `PUT /v1/fonts` to select a font. The icons sit in the glyph stack
under every font, pixel fonts included (ADRs 0195 and 0199). Clients must
send the documented characters; ASCII, Unicode or Nerd UI modes do not
become multicolour on their own. The active `glyphs` and `mappings` counts
describe the primary font file, not the glyph stack's repertoire.

## Picture memory

A session's pictures -- Kitty and sixel images, API pixel frames and frames
by descriptor alike -- are bounded by memory, not by count
([ADR 0226](adr/0226-pictures-bounded-by-memory-not-count.md)). Each session
has a picture budget, the bytes of decoded pixels Blit keeps for it: CPU
copies, sixel and Kitty scratch, and the GPU textures made from them (a
picture with both is charged for both). A dmabuf frame is the producer's
memory and is charged nothing. The budgets are settings in the document's
`server` section, in MiB, applied live:

| Setting | Default |
|---|---|
| `graphics_session_budget_mib` | 256 per session |
| `graphics_server_budget_mib` | a quarter of physical memory, at most 2048, across sessions |
| `graphics_texture_budget_mib` | 512, the GPU compositor's texture cache |

A picture is shown when it is placed and the placement is not deleted
(the default placement included), when a placement is held for its next
frame (an `a=p` that came before the picture's first frame by descriptor),
or when it holds an input route on the frame channel. When a new picture
would pass a budget, Blit first frees pictures that are not shown, least
recently shown first: the session's own, then, for the server budget, any
session's. A shown picture is never freed. If freeing every unshown picture
would still not make room, nothing is freed and the picture is refused: Kitty answers
`ENOSPC`, a frame by descriptor is refused `ENOSPC` (`frame-budget`), and a
`pixel_frame_begin` gets `pixel_frame_error` with `image_budget` and
`"status": 409`. A program that scrolls through many pictures deletes the
placements it scrolls away (`a=d,d=i` keeps the data) so they can be freed.

The counts are ceilings against a runaway program, far above any desktop:
4096 pictures and 16384 placements (every picture's together) per session.
Past the picture ceiling an unshown picture is freed, least recently shown
first, as a full store always was; with every picture on screen the new one
is refused (`ENOSPC`, `frame-store-full`, or `image_store_full` with
`"status": 409`). A new placement past the placement ceiling is refused
`ENOSPC`. The frame channel's input routes follow pictures: one per picture,
up to the picture ceiling per connection.

The GPU compositor keeps every picture's texture it draws in a cache bounded
by `graphics_texture_budget_mib`, recycled least recently drawn first and
never within the frame that drew it, so nothing on screen is uploaded again
while it stays there (`graphics_upload_bytes` in `/v1/diagnostics/runtime`
stays flat). XTSMGRAPHICS answers as before.

`/v1/diagnostics` `graphics` reports the ceilings (`max_images`,
`max_placements`), the per-picture bounds (`max_width`, `max_height`,
`max_bytes`), `budget` (`session_bytes`, `server_bytes`, `texture_bytes`)
and `used` (`server_bytes`, all sessions' pictures; `texture_bytes`, the
GPU cache). Each session's `graphics_memory` reports its pictures' `bytes`
and `budget`, its `images` and `placements`, the pictures `freed` by reason
(`unshown` at the ceiling, `budget` over a budget) and those `refused` by
reason (`ceiling`, `budget`).

## Picture frames by descriptor

A program running in a Blit session can send an image's pixels without
encoding them into the terminal ([ADR 0202](adr/0202-local-frames-by-descriptor.md)).
Its environment carries `BLIT_FRAME_SOCKET` (an abstract Unix socket,
`@blit-frames-<port>`) and `BLIT_FRAME_TOKEN` (a random secret for that
session). It connects (`SOCK_SEQPACKET`), says hello with the token, and
sends frames: a 48-byte header and one memfd sealed against shrinking,
growing and writing. The image is placed, layered and deleted with ordinary
Kitty escapes on the session's terminal, under the same id. The byte layout
and rules are in the ADR's "Wire format".

The channel has one version, 5, and one grammar for its lines ("One grammar,
version 5" under "Program windows"): every line that concerns a picture is
`<WORD> <verb> <id> [arguments]`, the picture id always the third word. The
hello, written `v5 <token>` here, is the ADR's binary hello with version 5.
It is answered `OK ...` (below); or refused, and the connection closed,
with `ERROR 0 EACCES` (a wrong token, or a uid that may not send to that
session) or `ERROR 0 EINVAL` (not a hello, or a hello of any version but
5). A refused frame is answered `FRAME error <id> <code>`.

The channel is local only. A program reached over ssh, or running on another
machine, has no `BLIT_FRAME_SOCKET` and could not reach it if it had: it
sends its pictures through the terminal as Kitty images, exactly as before.
A client that finds the variables missing, cannot connect, or is refused at
the hello should fall back the same way. Each session reports
`frames.accepted`, `refused`, `stale`, `superseded` and `last_error`; the
server reports `graphics.frames` in `/v1/diagnostics`.

A client may also be sent `HUD1 on` or `HUD1 off` lines, and may send STA1
status lines for the performance panel (a malformed one is answered
`FRAME error 0 EINVAL`); see "Performance panel".

A client may send frames whose pixels are already in
GPU memory: `FRM3`, a 72-byte header with the DRM fourcc, the format
modifier and up to four planes, their dmabuf descriptors (and optionally an
acquire fence) in `SCM_RIGHTS` ([ADR 0223](adr/0223-frames-by-dmabuf.md),
"Wire format, version 3"). Its hello is answered
`OK dmabuf=1 fence=<0|1> formats=<list> window=0` while the GPU compositor composes
every output and can import them -- the list names each importable format and its modifiers,
`XR24/0/100000000000001,AR24/0,...` (fourcc, then `/` and each modifier in
hex) -- and `OK dmabuf=0 window=0` otherwise (the CPU compositor on any output, or
EGL without `EGL_EXT_image_dma_buf_import`). `AR24` and `AB24` are
premultiplied by alpha; a producer with straight alpha premultiplies or
sends by memfd, and `XR24`/`XB24` (opaque) suit games and video. Blit imports the planes as a texture,
never maps them, and answers `FRAME release <id> <sequence>` when it no longer
needs a frame's buffer: once a newer frame of the image has been drawn, or
at once for a frame superseded, stale or never shown, and when the image
is deleted. A frame it cannot import is refused `FRAME error <id> ENOTSUP` and
should be sent again by memfd; a malformed one is `EINVAL`. Each session's
`frames.dmabuf` counts frames accepted, released and refused by reason
(`unavailable`, `format`, `import`, `invalid`), and an image says
`"dmabuf": true` while it shows one and `frame_us` when its newest frame
was accepted (monotonic microseconds; with `/v1/diagnostics/runtime`'s
`performance.last_inject_us`, the time `POST /v1/input/inject` last ran,
it measures input-to-frame latency, ADR 0228 phase 0); `/v1/diagnostics` reports
`graphics.frames.dmabuf`: whether it is offered (or why not), the formats,
whether fences are waited on, the totals, `fences_waited` and the same
counts per session.

A client may also ask Blit to send the physical keyboard and mouse
to blit-compositor's input socket for one of its windows, so that the
program in it gets input as a compositor expects it rather than as
terminal escapes ([ADR 0228](adr/0228-blit-is-the-compositor-for-programs.md);
the lines Blit writes there are "Program windows", "Input straight from
Blit", below). An INPUT line with any other verb is answered
`INPUT error 0 EINVAL`.

Every INPUT line names the image its route is for, so one connection can
carry the routes of several pictures (a desktop multiplexes its windows'
pictures over one connection per display). Each request is one message; a
trailing newline is optional.

- `INPUT sock <image-id> <path>` names the socket (an absolute path to a
  Unix stream socket owned by the client's own uid, which Blit checks
  against the file's owner and, once connected, the listening peer's
  credentials) and the image whose picture is the window. The image need
  not have a frame yet; the pointer maps once it has a frame and a
  placement. The path is the rest of the line, one word. Answered
  `INPUT ok <image-id>`, or `INPUT error <image-id> <code>` (the id is 0
  when the line names none or cannot be read): `EINVAL` (malformed; image
  id 0, a deleted image or a Kitty image; `INPUT on` or `INPUT off` for an
  image with no route), `EACCES` (the socket is not the client's),
  `ENOENT` (no such socket, or it refused the connection), `EBUSY` (this
  image already has a route on this connection, or the connection already
  has 4096, the picture ceiling of a session, `graphics.max_images`),
  `ENOMEM` (Blit could not allocate the route). If the compositor closes
  the socket later, or it cannot take a line, Blit sends `INPUT error
  <image-id> ENOENT` then and that route ends; the connection's other
  routes are untouched. A route ends with its image or its connection.
- `INPUT on <image-id>` and `INPUT off <image-id>` say whether that
  picture's window has the keyboard. A route starts off. At most one route
  is on per session: `INPUT on` turns off any other route of the same
  session, on any of its connections, releasing what it held first, so two
  windows never both get the keys even if an `off` is lost. A route is live
  -- it gets the keys -- only while it is on, its connection's session is
  Blit's focused session, the console is Blit's and its image exists; the
  desktop, which alone knows which of its windows has focus, sends these.
  The pointer -- motion, presses, the wheel -- goes to the on route in the
  session under the pointer (from 0.3.83; before, only to the live one),
  so a window shown on a display other than the focused session's, or
  over the seam between two, takes the pointer where it is; a button's
  release goes to the route it was pressed through, so a drag goes on
  across the seam, and a route lets go of its buttons when its session is
  neither focused nor under the pointer.
- `INPUT hold <image-id> relative|confine` says what the route does while
  its session holds pointer lock (`?7701`); it may be sent any time after
  `INPUT sock`, and neither takes nor gives back the lock, which stays the
  desktop's (ADR 0198). A new route is `relative`. Answered
  `INPUT ok <image-id>`, or `INPUT error <image-id> EINVAL` for an unknown
  mode, an image with no route on this connection, or a malformed line (id
  0 when it has no readable one).
  - `relative`: Blit's pointer is hidden and still, and the route is sent
    relative motion, the raw counts.
  - `confine`: while the route is live and its picture is drawn on the
    output the pointer is on, Blit's pointer stays shown with its usual
    shape and moves as an unlocked pointer does, kept inside the picture's
    drawn rectangle (its topmost placement), and the route is sent that
    point as an absolute position, mapped as below, so the compositor's
    pointer and the drawn one are the same point; it is sent no relative
    motion. Buttons and the wheel go to the route as always, and the
    session gets the lock's reports as it does under `relative`. When the
    picture is not drawn, or the pointer is on another output, the route
    behaves as `relative`, so a hold never strands the pointer. When the
    lock ends (`?7701l`, focus, the console, the image or the route gone),
    the pointer stays where it was, inside the picture.
- While live, Blit sends evdev key codes as they happen, press and release
  (the compositor makes its own repeats); buttons and scroll; the pointer
  as an absolute position, in the pixels of the image's latest frame,
  while it is over the image's drawn rectangle, wherever and however large
  the picture is placed; and, while the session holds pointer lock
  (`?7701`), relative motion instead, or under `INPUT hold ... confine` the
  confined pointer's position. Keys still reach the session as
  before, so the desktop's own keys (Release Pointer, Leave Full Screen)
  keep working.
- When a route stops being live (`INPUT off`, another route turned on, the
  session losing focus, Blit giving up the console, the image deleted, the
  connection closed) Blit first releases every key and button it pressed
  there. Each session reports `frames.input`: `events` (keys, buttons,
  scroll notches and motions sent, all routes of the session), `dropped`
  (lines a route's socket could not take), `last_error` (an INPUT code, or
  `sock-disconnected`/`sock-full`), and `routes`, one per route: its
  `image`, `state` (`off`, `on` while on but not live, or `live`: the
  keys'), `hold` (`relative` or `confine`), and `pointer`, the last absolute pointer for diagnosing its
  mapping: `here` (whether the pointer is the route's now: on, in the
  session under the pointer), `surface` (where it was on the session's surface, in pixels),
  `draw` (the picture's drawn rectangle it was mapped through, x, y, width,
  height), `over` (the picture it was over: the route's own or a member's,
  0 before any), `sent` (the position sent to the compositor), and `miss`
  (empty once a position was sent; `other-session`, `no-image` or
  `outside-picture` when the last pointer sent nothing; `none-yet`); and
  `members`, the route's member pictures (`INPUT member`, "Program
  windows"), each `{"image","x","y"}`.

## Program windows (draft, ADR 0228 phase 1)

`blit-compositor` implements the compositor's
side from 0.3.71 (ADR 0228 phase 1: milestone A, `wl_shm` windows as FRM1;
milestone B, the window states, `WINDOW output`, dmabuf as FRM3, pacing,
the keymap and input; milestone C, 0.3.72, pointer lock and confinement,
Blit's key repeat and the keymap re-read on Blit's notice; and, from
0.3.73, `--display`, programs that survive a desktop restart and `WINDOW
quit 0`), and Blit its
`window=0` answer, `GET /v1/input/keymap`, `INPUT sock`, `INPUT member`
and `INPUT keymap`; Hibr implements the desktop's side. `blit-compositor`
is Blit's Wayland compositor for programs and, from phase 2 (milestone A
in 0.3.78, milestone C in 0.3.81: "X programs"
below), the X window manager
of the Xwayland it runs for X programs. It composites nothing: each window
is one picture by descriptor.
It speaks the frame channel above as a producer, with these additions.

- **Who starts it.** The desktop, once per desktop session, with the
  `BLIT_FRAME_SOCKET`/`BLIT_FRAME_TOKEN` of the socket the desktop serves
  (Hibr's `frames serve`), so a held desktop's windows reach every display
  through `hold`. It connects to whatever those variables name, as any
  producer does. It creates `$XDG_RUNTIME_DIR/<name>`, a Wayland
  socket owned by the desktop's user, and says its name (below); the
  desktop puts it in every program's environment as `WAYLAND_DISPLAY`,
  and the X display of its Xwayland, when there is one, as `DISPLAY`.
  The desktop may pick the name, `blit-compositor --display <name>`, so it
  can export `WAYLAND_DISPLAY` before the compositor answers; without it
  the compositor takes the first free `blit-<n>`. A name is printable
  ASCII without a space or a slash (it is one file in `$XDG_RUNTIME_DIR`
  and one word on the `WINDOW display` line), at most 31 bytes, and short
  enough that `$XDG_RUNTIME_DIR/<name>-input-<id>` (a window's input
  socket, below, with the longest id) fits in 107 bytes. A name that is not one, or that another compositor
  holds, is refused before the channel is dialed: a line on standard error
  and exit status 2. `WINDOW display` says the name either way.
- **Hello.** `v5 <token>` (the frame channel's one version, below, with
  the `WINDOW` lines). A peer that answers the hello without
  `window=1` in its `OK` line does not speak `WINDOW`; the compositor then
  shows nothing and says so in its log. Blit itself, reached directly with
  no desktop between, answers `window=0`: placing windows is the desktop's.
  The compositor's first line after `OK` is `WINDOW display <name>
  <x-display>`, on every connection (a reconnect too: "Programs survive a
  desktop restart"): `<name>` the Wayland display, `<x-display>` the X
  display of its Xwayland, `:<n>`, or `-` when X is off (no Xwayland 23.1
  or later on `PATH`, no free X display, or Xwayland given up after dying
  at once three times: "X programs" below). The desktop exports both:
  `WAYLAND_DISPLAY=<name>`, and `DISPLAY=:<n>` unless it is `-`.
- **One grammar, version 5** (Blit 0.3.74 and Hibr 0.99.482, which ship
  together: the `blit` package Breaks `hibr (<< 0.99.482)` and Hibr's
  Breaks `blit (<< 0.3.74)`, so there are no compatibility forms). Every
  line on the frame channel that concerns a picture is
  `<WORD> <verb> <id> [arguments]`, the picture id always the third word:
  - Blit to a producer: `FRAME release <id> <seq>`, `FRAME error <id>
    <code>`, `INPUT ok <id>`, `INPUT error <id> <code>`;
  - a producer to Blit: `INPUT sock <id> <path>`,
    `INPUT on|off <id>`, `INPUT hold <id> relative|confine`, `INPUT member
    <id> <toplevel-id> <x> <y>`;
  - on an input route's socket: `INPUT key|ptr|rel|btn|wheel|frame|keymap
    <id> ...`;
  - between the compositor and the desktop: every `WINDOW <verb> <id> ...`
    line above and below, and `WINDOW ok <id>`, `WINDOW error <id> <code>`.
  Further ids (a popup's parent, a member's top-level) are arguments after
  it. A line that concerns no one picture says 0 there: `FRAME error 0
  EINVAL` (a malformed STA1, or a message too short to name its picture),
  `FRAME error 0 EACCES` (the session's user signed out; the connection then
  closes), `INPUT error 0 <code>` and `WINDOW error 0 <code>` (a line with
  no readable id, or `WINDOW display <name> <x-display>`, which names the
  displays, not a picture), and `WINDOW quit 0` (the whole compositor). The hello and its
  answer are not picture lines and keep their shapes: `OK dmabuf=0
  window=0`, `OK dmabuf=1 fence=<0|1> formats=<list> window=0`, `ERROR 0
  EACCES`, `ERROR 0 EINVAL`; Blit refuses a hello of version 1 to 4 (or
  any but 5) `ERROR 0 EINVAL` and closes the connection, the refusal every
  version reads. `HUD1 on|off` names no picture, and the binary `FRM1`,
  `FRM3` and `STA1` messages are unchanged. A relay rewrites ids with one
  rule: the third word of a picture's line.
- **One id space.** A window's id is its picture id: its `FRM1` and `FRM3`
  frames, `FRAME release`s and `INPUT` routes all use it.
- **Frames.** A `wl_shm` buffer is copied into one `FRM1` and given back to
  the program at once. A `zwp_linux_dmabuf_v1` buffer is one `FRM3` of the
  program's own planes, no copy, given back at Blit's `FRAME release` for it (or
  when its window goes: `WINDOW gone` follows only after every buffer of
  the id is given back, and Blit's later `FRAME release`s for them are stale).
  `zwp_linux_dmabuf_v1` (version 4, default feedback in one tranche) is
  offered only when the hello was answered `dmabuf=1` with a `formats=`
  list, and offers exactly those formats and modifiers. A frame refused
  `FRAME error <id> ENOTSUP` is sent again as an `FRM1` copy when its one linear
  plane can be mapped, as is every later dmabuf frame of the connection;
  any other refusal, or one that cannot be mapped, gives the buffer back
  unshown and says so in the compositor's log. The feedback's main device
  is the first DRM render node: the hello does not name Blit's GPU.
- **Pacing.** A program's frame callback is answered when Blit has taken
  the frame -- an `FRM1` when it is sent; an `FRM3` at once when it is the
  only frame of the window Blit holds, else at Blit's `FRAME release` of the
  frame before it, so a program is never more than one frame ahead of
  Blit -- and at most once per refresh of the window's `WINDOW output`.
  `wp_presentation` feedback is `presented` at the same moment
  (`CLOCK_MONOTONIC`, that output's refresh, no flags: the compositor does
  not see Blit's vblank), and `discarded` for a frame superseded before
  Blit took it or never sent.
- **Compositor to desktop**, one event a line, UTF-8:
  - `WINDOW new <id> <parent|0>`: a top-level; parent is its transient-for.
  - `WINDOW title <id> <text>`, `WINDOW appid <id> <text>`.
  - `WINDOW limits <id> <minW> <minH> <maxW> <maxH>`: pixels, 0 for none.
  - `WINDOW fullscreen <id> on|off`: the program's own request.
  - `WINDOW popup <id> <parent> <x> <y>`: a popup or menu, its own picture,
    at an offset in the parent's surface pixels (not on-screen pixels: the
    desktop may show the parent scaled); the desktop keeps it there when the
    parent moves, and may draw it past the parent's window. Sent again for
    the same id when the program moves the popup (`xdg_popup.reposition`,
    or an X menu moved by its program; from 0.3.82): the desktop moves the
    popup it has, from its next frame.
  - `WINDOW sub <id> <parent> <x> <y> <w> <h> above|below`: a subsurface
    (`wl_subsurface`), its own picture, as a popup's is, with no `WINDOW
    new`: owned, moved, hidden and closed with its top-level, never in the
    window list or the focus. `<parent>` is always the top-level: a tree of
    subsurfaces is flattened, `<x> <y>` the sum of the positions down to it
    in the top-level's surface pixels (negative allowed), `<w> <h>` its
    committed buffer's size, `above` or `below` the top-level's own
    picture. Sent when it first has a buffer (and every subsurface above it
    in the tree is mapped), and again, for all of that top-level's
    subsurfaces, bottom to top, whenever any is new or its offset, size or
    place in the order changes; positions and order are applied at the
    parent's commit, a synchronized subsurface's frame only then too.
    `WINDOW gone <id>` when it is unmapped (a NULL buffer, its
    `wl_subsurface` destroyed, a parent unmapped or gone). Its frames are
    `FRM1`/`FRM3` under `<id>`, released like any picture's. The desktop
    places it 1:1 at its offset (Blit's and kitty's `X=`/`Y=` cell offset
    gives the exact pixel), clipped to the window body, and sends Blit
    `INPUT member <id> <parent> <x> <y>` as for a popup; the compositor
    hit-tests subsurfaces by their input regions (an empty one, as Firefox
    sets on its content, gives the pointer to the parent). A subsurface of
    a popup is not shown in this release (logged).
  - `WINDOW decor <id> server|client`: who draws the frame
    (`zxdg_decoration`). `client` (a program that insists on its own, such
    as GTK's client-side decorations) gets a bare window from the desktop,
    never a frame around a frame.
  - `WINDOW geometry <id> <x> <y> <w> <h>`: the window itself inside its
    buffer, in its surface pixels (`xdg_surface.set_window_geometry`): a
    program that draws its own frame draws its shadow outside it. Sent at
    the commit that changes it and when the window is announced again; no
    line means the whole buffer. The desktop crops the picture to it with
    a source rectangle (Blit maps the pointer through it) and sizes the
    window body by it: `WINDOW size` is this rectangle's size.
  - `WINDOW move <id>`, `WINDOW resize <id>
    top|bottom|left|right|top-left|top-right|bottom-left|bottom-right`: the
    program's own frame asks the desktop to move or resize it
    (`xdg_toplevel.move/resize`). Sent only while a pointer button the
    compositor delivered to that window is down, the request naming that
    press's serial; anything else is ignored and logged. The desktop has
    seen the press itself (Blit reports every press, drag and release to
    the terminal as well as to a live route), starts its own drag at the
    pointer, and sends `INPUT off <id>` so that the program's button is
    released and it gets no motion during the drag, `INPUT on <id>` after.
  - `WINDOW menu <id> <x> <y>`: the window menu, asked for at x, y in its
    surface pixels (`xdg_toplevel.show_window_menu`, the same press rule).
  - `WINDOW appmenu <id> <service> <path>`: the global menu (ADR 0229). The
    program serves its menus over the session bus
    (`com.canonical.dbusmenu`: Qt and KDE programs, GTK 3 with the appmenu
    module) and says where with `org_kde_kwin_appmenu`: a D-Bus service
    name (`:1.42`, `org.kde.app`) and an object path (`/MenuBar/1`). Sent
    when the program sets it, when the window is announced again, and as
    `WINDOW appmenu <id> - -` when the program releases it. Names that are
    not D-Bus syntax are ignored. The desktop reads the menus and shows the
    focused window's in its bar; Firefox and GTK 4 programs send none and
    keep their own.
  - `WINDOW cursor <id> <name>`: the pointer shape the program asks for over
    that window, in Blit's own OSC 22 vocabulary -- one source of cursors,
    Blit's pointer art: a CSS cursor name (`default`, `text`, `pointer`,
    `wait`, the `*-resize` names, ...) as the program named it through
    `wp_cursor_shape_v1`, or `none` to hide the pointer (`wl_pointer.
    set_cursor` with no surface, as games do); for an X program, the name X
    gives its cursor, in the same vocabulary ("X programs", below). A
    program's own cursor image (`set_cursor` with a surface) is never
    drawn: it is `default`, the display's own pointer. Sent for the
    top-level whose surface has the pointer, once per change, and again
    when the window is announced again.
    The desktop relays the name unchanged as that window's OSC 22 shape (a
    name Blit has no art for shows the display's own pointer), so Blit and
    `hold` gain nothing new for it.
  - `WINDOW maximize <id> on|off`, `WINDOW minimize <id>`: the program's
    requests; the desktop decides and answers with `WINDOW size`. xdg has
    no restore event: a minimized window stops being drawn, and its next
    `WINDOW size` comes when the desktop restores it.
  - `WINDOW parent <id> <parent|0>`: a transient parent set or cleared after
    `WINDOW new`.
  - `WINDOW hide <id>`: the program unmapped the window; the id stays, and
    its next frame shows it again where the desktop kept it.
  - `WINDOW gone <id>`: sent after every buffer of that id is released;
    no frame follows it for that id.
  `title`, `appid`, `limits`, `cursor` and `decor` may come again at any
  time. The compositor strips control characters, newline included, from
  titles and app ids and bounds them to 256 bytes.
  A line the desktop does not know is answered `WINDOW error <id> EINVAL`.
- **Desktop to compositor:**
  - `WINDOW size <id> <w> <h> [fullscreen] [maximized] [activated]
    [tiled-left] [tiled-right] [tiled-top] [tiled-bottom]`:
    pixels, with the xdg states the desktop has given the window (so a
    window it zooms on its own still tells the program it is maximized). The
    compositor configures exactly this size; the frame that follows carries
    the size the program committed in its header. `0 0` means the program
    chooses (an xdg configure of 0,0): dialogs and fixed-size programs pick
    their own size and the desktop sizes its window to the first frame.
    The state words are a set (any order, each at most once); the tiled ones say which edges
    the desktop has put the window against (xdg_toplevel's tiled states):
    a program that draws its own frame drops its shadow there.
    A program's initial commit is answered at once, as xdg-shell requires
    and as programs rely on (vkcube presents after one roundtrip): with the
    desktop's size if one has come, else 0,0. So the first frame of a new
    window is often the program's own size, and the desktop's first `WINDOW
    size` is an ordinary reconfigure. The same holds for a program started
    while no desktop is connected.
  - `WINDOW output <id> <w> <h> <mHz>`: the display the window is on (its
    mode, from `/v1/displays`), sent when the window opens and whenever it
    moves to another display; the compositor presents it to the program as
    its `wl_output`: one `wl_output` per distinct mode, which the window's
    surface enters (and leaves the one before). Until the first, a window
    is on a 1920x1080 60 Hz output.
  - `WINDOW close <id>`: `xdg_toplevel.close` (in phase 2,
    `WM_DELETE_WINDOW`); what to do if the program lingers is the desktop's.
    For a popup it is `xdg_popup.popup_done` -- the desktop sends it for a
    click outside a grabbing popup; an X popup closes itself ("X
    programs", below).
  - `WINDOW quit 0`: the person quit the desktop; the compositor ends
    every program at once and exits, status 0, without reconnecting. Only
    id 0 (the whole compositor) is defined; any other is not a `quit`
    line.
  - `INPUT on|off|hold` as above; focus is `INPUT on`, so there is no
    separate focus line. `INPUT on` for one window implies nothing for
    another: the desktop sends each `off` itself.
  - **One input route per top-level.** A popup's picture joins its
    top-level's route with `INPUT member <popup-id> <toplevel-id> <x> <y>`
    (the popup's offset in the top-level's surface pixels, the same numbers
    as `WINDOW popup`), sent to Blit by the desktop. Blit maps the pointer
    over a member picture into the top-level's surface coordinates, so the
    compositor sees one coordinate space per top-level and hit-tests its
    own popups; keyboard focus stays the top-level's. The member ends with
    the popup's picture (deleted, or gone after it was shown), or with the
    top-level's route. Answers follow the other INPUT requests, with the
    popup's id: `INPUT ok <popup-id>`, or `INPUT error <popup-id> <code>`
    (id 0 when the line has no readable one): `EINVAL` for a malformed line
    (an offset is a decimal, optionally negative, at most 32767 either
    way), a top-level with no route on this connection, the popup the
    top-level itself, or a popup that is a deleted or Kitty image; `EBUSY`
    for a popup with a route of its own on this connection, or a route with
    16 members already. Sent again, it moves the member (a popup
    repositioned); a member of another route on the connection moves to
    this one. Where draws of the top-level and its members overlap, the
    topmost wins.
- **Sizes are the program's to honour.** A Wayland client is told a size
  and commits the buffer it draws; most draw exactly that size, but a
  client with a fixed size, or one mid-resize, commits another. The
  compositor never scales or crops: it sends what was committed, and
  `WINDOW limits` says in advance what a client will not go below or above.
  The desktop shows a mismatch as it shows any picture, scaled to fit with
  its shape kept.
- **Pixels, scale 1.** The desktop computes a window's size from its cells
  and the cell size of the display it is on; a window moved to a display
  with a different cell size gets a new `WINDOW size`.
- **Programs survive a desktop restart** (agreed with Hibr 2026-11-10;
  in blit-compositor from 0.3.73). The desktop
  starts blit-compositor detached (its own session, owned by init), records
  its pid and display (`--display`, above), and serves the frame socket
  under a name and token fixed for the desktop session, so a Restart
  Desktop (an exec) neither ends the compositor nor changes its
  environment, and the restarted desktop starts no second one.
  - **The channel lost.** When the channel closes or errors, the
    compositor keeps every program running and its windows unshown, with
    the Wayland socket and each window's input socket open. Blit's input routes to
    the compositor end with the desktop's channel (their connections to
    the input sockets close, each releasing what it held). An active
    pointer lock or confinement ends as at the desktop's `WINDOW lock <id>
    off` (a oneshot one is then spent); every one still wanted is asked
    for again on reconnect.
  - **Meanwhile.** Programs may go on committing: the compositor keeps
    only each window's newest frame -- for `wl_shm`, the copied pixels; for
    a dmabuf, the newest `wl_buffer`, every older one given back at once,
    so a program does not stall on a full pool. Frame callbacks are still
    answered at the window's output refresh (the pacing above, as if each
    frame were taken), so programs do not freeze and a game or video keeps
    running unseen; `wp_presentation` feedback is `discarded`, since
    nothing is shown. A program that connects meanwhile gets its windows,
    announced when the desktop is back.
  - **Reconnecting.** To the same `BLIT_FRAME_SOCKET` and
    `BLIT_FRAME_TOKEN`, first after 50 ms, then at intervals doubling to
    one second, for at most 60 seconds from the loss; the event loop never
    waits on it (a hello not answered within 3 seconds is dropped and
    dialed again). An answer without `window=1` (Blit itself, reached
    directly) is let go and the next attempt follows.
    `BLIT_COMPOSITOR_RECONNECT_MS` shortens the 60 seconds, in
    milliseconds, for gates only: it is not a setting.
  - **On reconnect** it says hello again (version 5), then `WINDOW
    display`, and announces every window again with its id and its last
    state: each top-level, in the order they were created, `WINDOW new <id>
    <parent>` (`0`, and `WINDOW parent` after every top-level, for a parent
    created after it), `title`, `appid`, `limits`, `decor`, `fullscreen
    on`, `maximize on` and `INPUT sock`, and `WINDOW hide` for one its
    program had unmapped (`minimize` is a request, not a state, and is not
    repeated); then each popup, `WINDOW popup` with its offset; then each
    top-level's subsurfaces, `WINDOW sub` bottom to top with their ids
    kept; then each shown window's, popup's and subsurface's newest frame (`FRM3` of the kept buffer, or `FRM1`);
    then `WINDOW lock` for each pointer lock or confinement a program still
    holds. Popups' `INPUT member` lines are the desktop's to send again, as
    for new windows. The desktop answers as for new windows (`WINDOW
    size`, `WINDOW output`, `INPUT on`); the compositor configures no window
    until its new `WINDOW size` arrives, so until then a program keeps its
    size, and each window stays on its output until the desktop's `WINDOW
    output`. The dmabuf formats offered to programs stay the first
    hello's: a desktop that answers others, or no `dmabuf=1`, is sent
    copies (`FRM1`).
  - **Ending.** If no desktop answers with `window=1` within the 60
    seconds, the compositor says so in its log, ends its programs (their
    connections closed) and exits, status 1. When the person quits
    the desktop, the desktop sends `WINDOW quit 0` (id 0: the whole
    compositor) and the compositor ends its programs at once, exit status
    0, with no reconnect.
- **Windows nobody launched through the desktop** -- a program started
  from a terminal with `WAYLAND_DISPLAY` set -- still produce `WINDOW new`,
  and the desktop opens a window for them.
- **X programs** (ADR 0228 phase 2: milestone A, X programs in windows
  with keys and mouse, from 0.3.78; milestone C, their menus, dialogs,
  states, move and resize and cursors, from 0.3.81).
  The compositor runs Xwayland (23.1 or later, upstream's X server,
  `Recommends` of its package) on the first free X display `:<n>`, said on
  `WINDOW display`, and is its window manager. An X top-level window is a
  window as a Wayland one is, in the same id space, on the same lines:
  `WINDOW new <id> 0`, `WINDOW title` (its `_NET_WM_NAME`, else
  `WM_NAME`), `WINDOW appid` (the class of its `WM_CLASS`), `WINDOW decor
  <id> server` (X programs leave their frame to the window manager: the
  desktop draws it) or `client` when its `_MOTIF_WM_HINTS` say no
  decorations (it draws its own frame or none, as Steam does; said again
  when the property changes, from 0.3.80), `INPUT sock`, and each frame of its Xwayland surface
  as a frame of the picture (`FRM1`; `FRM3` when the peer imports dmabufs
  and Xwayland draws with them). The desktop's `WINDOW size` resizes the
  X window to exactly that size (`0 0` leaves it to the program) and,
  with `activated`, gives it X's input focus (and `_NET_ACTIVE_WINDOW`);
  the keyboard's first key from its route does so too. `WINDOW close`
  asks the program to close it (`WM_DELETE_WINDOW`), or ends the program
  when it takes no such request. The window's unmapping or destruction
  is `WINDOW gone`; mapped again it is a new window with a new id. Keys
  and the pointer come on its input socket as for any window.
  From milestone C (0.3.81) each of an X
  program's requests is the line its Wayland equivalent produces:
  - `WM_TRANSIENT_FOR` is the window's parent, `WINDOW new <id> <parent>`
    (and `WINDOW parent` when it changes), as `xdg_toplevel.set_parent`.
  - `WM_NORMAL_HINTS`' minimum and maximum size are `WINDOW limits` (its
    base size is the minimum when it gives no minimum, as ICCCM says), as
    `set_min_size` and `set_max_size`.
  - `_NET_WM_STATE` full screen and maximized are `WINDOW fullscreen` and
    `WINDOW maximize`, as `set_fullscreen` and `set_maximized`: the
    property the program set before mapping the window, and its requests
    after (add, remove, toggle; maximized is vertical and horizontal
    both). The desktop's `WINDOW size` states come back as the window's
    `_NET_WM_STATE` (full screen, maximized, `activated` as focused).
  - `_NET_WM_MOVERESIZE` (a program's own title bar or edge, as Steam's)
    is `WINDOW move` or `WINDOW resize <edges>`, as `xdg_toplevel.move`
    and `resize`, under the same rule: only while a press the compositor
    delivered to that window is down (the button the message names; X has
    no serial). Its keyboard move and size are ignored and logged.
  - `WM_CHANGE_STATE` to Iconic is `WINDOW minimize`.
  - X's cursor is `WINDOW cursor <id> <name>` by the name X programs give
    their cursors (XFixes; libXcursor names every themed cursor, `left_ptr`,
    `xterm`, `hand2`, or the CSS names GTK asks for), when Blit's pointer
    vocabulary has it -- its OSC 22 names take the X names as aliases --
    else `default`; `none` while X hides it.
  - An override-redirect window (an X menu, dropdown or tooltip) is a
    `WINDOW popup <id> <parent> <x> <y>`, as an `xdg_popup` is, and the
    desktop joins it to its top-level's route with `INPUT member` as for
    any popup. Its parent is the top-level its `WM_TRANSIENT_FOR` names;
    else the one under its origin in X space (a popup there first: a
    submenu over its menu); else the X top-level the pointer is in. Its
    offset is its place relative to that top-level in X space, where
    every X top-level keeps its own position (the program's, only kept
    inside the X screen, the largest `WINDOW output`, since X confines its
    pointer to it; never moved after that): X space is a virtual layout,
    and the desktop places the pictures. A popup the program moves after
    it is shown is `WINDOW popup` again at its new offset, as a Wayland
    popup's reposition is (0.3.82). One whose origin is in no X
    window and that names none is not shown (logged). The pointer over
    the part of a top-level larger than the X screen reaches X at the
    screen's edge.
  - X menus take their input as X programs expect: the keys and the
    pointer of their top-level's route reach the menu through its X
    grabs, and a press anywhere in the program's windows outside the menu
    reaches the menu, which closes itself. X has no `popup_done`, and the
    window manager does not unmap another program's override-redirect
    window: `WINDOW close` for an X popup is noted in the log and nothing
    more. When the desktop takes the window's focus (its `WINDOW size`
    without `activated`), X's focus and `_NET_ACTIVE_WINDOW` leave it,
    and X toolkits close their menus on that. A press outside every X
    window does not reach X at all.
  - From milestone D (0.3.84), X's CLIPBOARD and PRIMARY are the
    clipboard's two selections, on the `CLIP` lines below with nothing
    new: an X program is one more owner and reader, as a Wayland program
    is, and X as a whole is "the program with the keyboard" while an X
    window has X's focus (which follows the keyboard) -- any X client
    then, as xclip run from an xterm or Wine's clipboard connection. Its
    copy (its X ownership) is `CLIP offer` of
    `text/plain;charset=utf-8` when it has a text target (`UTF8_STRING`,
    `text/plain;charset=utf-8`, `STRING` -- its Latin-1 made UTF-8 --,
    `TEXT`, `text/plain`), with `x-kde-passwordManagerHint` when it has
    that target; an ownership while no X window has the focus is not the
    clipboard's (logged; X keeps it until the selection next changes).
    The desktop's offer or a Wayland program's selection is owned in X by
    the window manager, which answers only while an X window has X's
    focus: `TARGETS`, `UTF8_STRING`, `text/plain;charset=utf-8`,
    `TEXT`, `STRING` (Latin-1, `?` for what it has not), the hint, and a
    Wayland program's other types, read from the owner as a Wayland
    paste is (`CLIP read` with a pipe, for the desktop's). From 64 KiB a
    transfer goes INCR, both ways. A transfer that makes no progress for
    3 s ends: an X paste not yet answered gets nothing (property None),
    one under way ends short with what came; the reader's pipe closes.
    When the desktop clears a selection (`CLIP clear`), X's owner goes at
    once (a paste already under way is the desktop's pipe to finish).
  Not yet: `_NET_ACTIVE_WINDOW` requests and `_NET_WM_WINDOW_TYPE` (noted
  once in the compositor's log), and X drag and drop. When Xwayland dies,
  its windows are `WINDOW gone`, an X program's selection is `CLIP clear`,
  and it is started again on the same display; its X programs end with
  it.
  `blit-compositor --x11 off` runs no Xwayland (Wayland programs only,
  `WINDOW display <name> -`, the log saying why); the desktop starts it so
  from its X11 Programs setting (`blit-compositor --version` 0.3.81 or
  later takes the option; an older one refuses any unknown option with
  status 2). `BLIT_COMPOSITOR_XWAYLAND`, for gates only, names another
  Xwayland.
- **Input straight from Blit.** blit-compositor takes its windows' input
  from Blit directly, not through the desktop's relay. It listens on an
  input socket of its own for each top-level (`SOCK_STREAM`) and asks once
  per top-level, through the channel, `INPUT sock <id> <path>` ("Picture
  frames by descriptor", above): Blit connects to `path` (an absolute path
  to a Unix socket owned by the client's uid), and the answers,
  `INPUT on|off|hold|member`, the live rule and the release of everything
  held when a route stops being live are as described there. A desktop
  between relays the request and replays it on reconnect, and carries no
  input.
  While the route is live, Blit writes to that socket, in the picture's own
  surface pixels (a member's position mapped into its top-level's, as
  `INPUT member` defines):
  ```
  INPUT key   <id> <evdev-code> press|release
  INPUT ptr   <id> <x> <y>             absolute
  INPUT rel   <id> <dx> <dy>           under a relative hold
  INPUT btn   <id> <BTN_code> press|release
  INPUT wheel <id> <dx> <dy>           notches
  INPUT frame <id> <monotonic-us>      ends one batch of events
  INPUT keymap <id> <revision> <size> <rate> <delay>
                                       the keymap (a memfd passed with it)
  ```
  Keys are raw evdev codes; the compositor derives modifiers and characters
  with the keymap below, so there is one source. `ptr` is in pixels of the
  picture's latest frame, a decimal with two fractional digits (over a
  member, the top-level's pixels, negative where a popup reaches past the
  top-level's left or top); `rel` the
  raw counts; `wheel` whole notches, positive right and down (as Wayland
  counts them); each event is one batch, its `frame` after it. Input
  is never dropped silently: if the socket cannot take a line (full, or its
  reader gone), Blit counts it in `frames.input` `dropped`, forgets what
  that route held -- the compositor releases every key and button of a
  route whose socket ends -- and ends the route with `INPUT error <id>
  ENOENT`, as when the compositor closes the socket itself
  (`last_error` `sock-disconnected`, or `sock-full`).
  `INPUT keymap` is no event: no `frame` follows it, it is not counted in
  `events`, and it may come between batches. It carries the keymap of the
  keyboard whose keys reach the window -- what `GET /v1/input/keymap`
  serves by default ("Keyboard map (xkb)"), the same bytes -- as a sealed
  memfd passed with the line (SCM_RIGHTS) of `size` bytes, the xkb text and
  a NUL, and that keyboard's repeat, `rate` per second after `delay` ms (0
  0: no repeat). `revision` only grows while Blit runs. Blit writes it on a
  route when the route is live and has not been sent the current revision:
  at once when the revision changes (a keyboard's settings, a keyboard
  enabled, disabled, plugged or gone, or a key from another keyboard,
  written before that key), and when the route goes live after a change,
  its first line. So the key that follows is read with the new keymap. The
  compositor compiles it, sends its clients the new keymap and, when the
  repeat changes, `repeat_info`; the same bytes as the keymap it has
  change only the repeat (compared by bytes: a restarted Blit counts
  revisions afresh). Until a route's first keymap its clients are sent
  `no_keymap`, and keys still reach them.
  A window's input socket has at most one route in a session: Blit ends a
  route to the same socket file (by device and inode, on any channel of
  the same session) when a new `INPUT sock` opens one -- what it held
  released first, its channel told `INPUT error <id> ENOENT`, `last_error`
  `superseded` -- so a route left behind when a desktop asks again under a
  new picture id cannot linger. Routes in other sessions stay: a desktop
  gives each display's session its own route to a window, and only the
  session the pointer or keyboard is in delivers, so a window over the
  seam between two displays takes input on both halves. In `frames.input` each
  route says `picture`: `present` when its picture is in the route's
  session, `absent` when it has not come yet or is drawn in another
  display's session (keys still reach a live route; the pointer cannot).
  A top-level's input socket is `$XDG_RUNTIME_DIR/<display>-input-<id>`
  (mode 0600, made at its first `WINDOW new`, removed when it goes), and
  the compositor sends `INPUT sock` for it right after its `WINDOW new`
  hints. The socket, not the line, names the window: a desktop relays
  `INPUT sock` to Blit under its own id for the picture (Hibr's are not
  the compositor's), and the `<id>` on Blit's lines is that one, which the
  compositor does not read. It gives the keyboard to the window whose route sends
  a line, and takes it away when another window's route sends one, when
  the desktop's `WINDOW size` for it drops `activated`, or when its route's
  socket ends (releasing what was down); a popup of the top-level under
  the pointer gets the pointer in its own pixels, and the surface a button
  went down on keeps it until the last button is up. Clients are told the
  key repeat Blit's keymap answer carries (`X-Blit-Repeat-Rate`,
  `X-Blit-Repeat-Delay`: the active keyboard's), and again when it
  changes; none (rate 0) without Blit's answer.
- **Relative pointer.** `zwp_relative_pointer_manager_v1` is offered: the
  `INPUT rel` lines of a route under Blit's relative hold (the desktop's
  `?7701` with `INPUT hold <id> relative`) go to the relative pointers of
  the client that has the pointer, the raw counts as both the accelerated
  and the unaccelerated motion.
- **Pointer lock** (*agreed with Hibr 2026-11-10; blit-compositor offers
  `zwp_pointer_constraints_v1` version 1 from 0.3.72*). The lock is the desktop's
  (ADR 0198), so the compositor asks for it rather than taking it. The
  desktop takes a hold only within its gesture window after a click or key
  in that window (Hibr's `DT_PTRGESTMS`, 2 s): a program asking on its own,
  with no gesture, is answered `WINDOW lock <id> off` until the person
  clicks. The desktop takes it through `dt_pointerlock` on the display
  showing the window, with the route's `INPUT hold <id> relative|confine`;
  Release Pointer and focus loss always end it with `off`.
  - compositor to desktop, `WINDOW lock <id> relative|confine|off`: the
    program locked the pointer (`zwp_pointer_constraints_v1.lock_pointer`:
    `relative`), confined it (`confine_pointer`: `confine`), or let it go
    (`off`, its constraint destroyed or its window gone). A lock's region
    and a confinement's region are not carried: Blit confines to the
    picture's drawn rectangle.
  - desktop to compositor, `WINDOW lock <id> on|off`: the desktop engaged
    its hold for the window (`?7701`, after `INPUT hold <id> relative` or
    `confine`), or the hold ended -- on its Release Pointer chord, a focus
    change, or the program's `off`. The compositor sends `locked` or
    `confined` on `on`, and `unlocked` or `unconfined` on `off` (a
    `oneshot` constraint is then dead, a `persistent` one waits for the
    next `on`).
  - Relative motion while locked is the existing `INPUT rel`; a confined
    pointer's positions are the existing `INPUT ptr` (Blit keeps the
    pointer inside the picture under `confine`). While a lock is active
    the compositor sends the program no absolute motion, whatever `INPUT
    ptr` arrives.
  - The id is the top-level's: a constraint on a popup's surface is asked
    for with its top-level's id, as the route is the top-level's. A
    constraint is asked for once, when it is made (or when its window is
    announced, if later); a oneshot constraint deactivated by `off` is
    spent, so destroying it afterwards sends nothing, and a program that
    wants the lock again makes a new constraint, which asks again.
- **The keyboard map** is generated from Blit's own keyboard tables (its
  evdev-to-character maps and modifier roles) -- one source -- and comes on
  the input route itself: `INPUT keymap` with a sealed memfd of the xkb
  text (`xkb_v1`, the bytes `GET /v1/input/keymap` serves) and the repeat,
  which the compositor compiles with libxkbcommon and hands to clients. A
  change of Blit's keyboard settings, or keys from another keyboard, reach
  the windows at once. The compositor needs no API, no port and no token
  for it (the desktop session has none of Blit's: the login screen keeps
  its own, ADR 0228), and with several displays each route's Blit is the
  one whose keys it carries. Without a keymap its clients are sent
  `no_keymap`. The desktop never touches it.
- **The clipboard** (ADR 0229): Hibr's clipboard is the one truth; the
  compositor offers `wl_data_device_manager` v3 (Ctrl-C/V) and
  `zwp_primary_selection_device_manager_v1` (middle click) and keeps only
  which side owns each selection, never the bytes. Lines, both ways:
  ```
  CLIP offer <seq> clipboard|primary [<type> ...]   the selection is now this offer
  CLIP read <seq> <type>                            with a pipe's write end: the owner writes, closes
  CLIP clear <seq>                                  the offer is withdrawn
  ```
  `<seq>` is the sender's own count. "A program" is a Wayland program or,
  from milestone D, an X program (X programs, above). A program's selection (set by the
  program with the keyboard) reaches the desktop as `text/plain;charset=utf-8`
  when it has any text type (`text/plain;charset=utf-8`, `UTF8_STRING`,
  `text/plain`, `STRING`, `TEXT`), with no types otherwise, plus
  `x-kde-passwordManagerHint` when the program marked it secret; the
  desktop's `CLIP read` in the text type is answered from the program's best
  text type, through the pipe passed with the line. The desktop's offer is
  shown to programs with all five text types (and the hint), and a
  program's paste becomes `CLIP read <seq> text/plain;charset=utf-8` with
  the program's pipe, which the desktop writes and closes. A secret (the
  hint: the Vault's offers carry it) enters no history, and the desktop's
  `CLIP clear` when its timeout clears it ends it on the program side too.
  A program's source going sends `CLIP clear`. Between programs a paste goes
  straight from the source. The program with the keyboard is offered each
  selection when it gets the keyboard and when one changes. The desktop's
  offers lapse with its channel; a program's selection is offered again,
  under a new `<seq>`, after the re-announcement. A pipe either way may
  close short: an X program that stops sending, or stops taking, for 3 s
  is let go, and the pipe carries no error, so the desktop reads a short
  text as it would a whole one (or its write to an X paste fails). Drag
  and drop is not here yet: a drag is cancelled at once (ADR 0229's later
  row).

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

Each `render.outputs[]` entry's `present_calls`, `last_present_ms` and
`max_present_ms` belongs to that output's resource lifetime and counts
submitted frames. An idle output does not inherit another output's counts or
timing. Runtime performance totals remain aggregate counters. Submission does
not prove physical scanout; flip and vblank evidence is in `kms_scanout`.

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

### Measuring a picture that blinks

A picture that is on screen, absent from a few presented frames, and back is
a *blink* ([ADR 0213](adr/0213-graphics-ledger-blink-counters-and-held-pictures.md)).
`GET /v1/diagnostics/runtime` counts them in `performance`:
`graphics_blink_events` (each vanish-and-return) and `graphics_blink_frames`
(the presented frames they were absent), beside `graphics_placements`,
`graphics_deletes`, `graphics_images_freed`, `graphics_upload_bytes`
(GPU texture uploads), `graphics_bytes_landed`, `graphics_full_repaints`,
`graphics_barrier_holds`, `graphics_barrier_timeouts` and
`graphics_synchronized_live_draws`. `GET /v1/diagnostics/graphics-ledger`
says what happened around one: a ring of the last 512 picture events
(`transmit`, `place`, `delete`, `free`, `reset`, `blink`, `barrier`), each
stamped with the output's presented-frame count, the image and placement,
the rectangle in cells and pixels, the bytes and a reason. It is its own
ring, not the trace's, so a burst of pointer events cannot evict it. Read it
incrementally with `since`:

```sh
before=$(curl -s "$base/v1/diagnostics/runtime" | jq '.performance | {graphics_blink_events,graphics_blink_frames,graphics_barrier_timeouts}')
cursor=$(curl -s "$base/v1/diagnostics/graphics-ledger?limit=1" | jq .sequence)
# ... do the thing that blinks ...
curl -s "$base/v1/diagnostics/runtime" | jq '.performance | {graphics_blink_events,graphics_blink_frames,graphics_barrier_timeouts}'
curl -s "$base/v1/diagnostics/graphics-ledger?since=$cursor" | jq '.events[] | {sequence,kind,frame,image_id,reason,frames,cells}'
```

The response's `next` is the cursor for the following read and `lost`
counts events the ring overwrote since yours. A `blink` event's `reason` is
what last deleted the image, then `;returned` or `;successor=<id>`.

Capture one display with:

```sh
curl --fail --output display.bmp \
  http://HOST:8080/v1/diagnostics/screenshots/display-1
```

The capture is a BMP of the next presented frame; taking it never forces a
redraw (`?cached=1` is accepted and has no further effect). A screenshot is
evidence of pixels only; pair it with runtime
counters, surface snapshots, and trace events before diagnosing a boundary.

For a performance report, capture `/v1/version`, `/v1/displays`, and
`/v1/diagnostics/runtime` at the start and end of the same bounded workload.
Compare `present_calls`, `frame_rate_hz`, `surface_commit_hz`, frame/render
intervals, missed deadlines, queue pressure, and input dispatch counters. Do
not report a frame rate from a screenshot timestamp or HTTP request latency.

Each `diagnostics.render.outputs[]` entry includes `kms_scanout`. On direct
KMS, `present_calls` counts submitted frames while
`kms_scanout.flip_completions` counts completed page-flip events. The
`vblank_sequence`, `last_vblank_timestamp_us`, `vblank_span_count`, and
`vblank_span_us` fields expose the kernel's output timing; its
`observed_refresh_hz` is the vblank rate, not the rate of changed content.
`available` is false, and the counters zero, for an output with no KMS state.
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
surface commit, scheduler, compositor, KMS submission, or physical scanout is
the limiting boundary.

## Performance panel

Blit can draw a small panel of its own counters over the panes ("stats for
nerds", [ADR 0203](adr/0203-performance-panel.md)). It is set in the
settings document's `hud` object and changed live with `PUT /v1/settings`:

```json
"hud": {"enabled": true, "display_id": "all", "corner": "top-right",
        "hz": 2, "opacity": 100, "sections": ["blit", "hibr", "graph"]}
```

Those are the defaults, except `enabled`, which defaults to `BLIT_HUD=1` at
start. `display_id` is a display or `all`; `hz` (1–10) is the most updates
a second. `opacity` (10–100, percent) lets what is under the panel show
through its background and its graph's fill; the text, border, bars and
lines stay opaque, so it still reads. Both compositors blend the same way,
to the same pixels, and a translucent panel does not darken with time. An unknown or repeated member, or a value out of range, is
`400 invalid_settings_hud`. `settings.effective.hud` is what is in force.

Each display's panel is about that display, one idea a line:

```text
display-102  2560x1440  60 Hz
draws   58/s   late 2/s   missed 0/s
why     effects 49  video 9
draw    12.3 ms  (max 20.9 in 10 s)
flip    0.8 ms  (max 2.1 in 10 s)
gpu     AMD Radeon Graphics (radeonsi)
video   9/s  by descriptor  (kitty 0  sixel 0)
```

- **draws** are the frames presented to that output in the last second,
  less the panel's own; **late** and **missed** are late flips and missed
  deadlines in that second. **why** splits the draws by the first reason
  that applies: an animated pane effect, a picture (Kitty, Sixel or a frame
  by descriptor), the pointer alone, changed text (cells), and anything
  else, such as a full redraw.
- **draw** and **flip** are the last frame's draw time and submit-to-flip
  time, with the worst in the last 10 s. **gpu** is the GLES renderer,
  shortened; on the CPU compositor the line says why.
- **video** is the pictures the display's sessions sent in the last second;
  `refused N/s` follows when any were refused.
- **graph** (section `graph`): the last 60 s, one bar a second, newest at
  the right: that second's worst frame time on the output, against a line at
  the refresh interval. Bars over it are red.
- **blit**, once, on the first panel shown: the worst loop gap in 10 s,
  input (`idle`, or the last and worst dispatch time), the glyph cache's hit
  rate, and errors in 10 s (`none`, or the nonzero counts). While an
  installed update waits for a restart, one more line:
  `update  0.3.27 installed, at login screen`, or `restart to apply` under
  `manual` (see "Restart after an update").
- **hibr** (section `hibr`): the lines the session's frame client sends as
  STA1 (the frame socket, ADR 0202 and ADR 0203), with their age, while
  younger than 5 s. The session reports them as `status`.

Every figure is in `/v1/diagnostics`: an output's under
`render.outputs[].recent` (with the raw `picture_frames` and
`pointer_frames`), Blit's under `hud.recent`. They are kept whether or not
the panel is on.

The panel uses the boot PSF whatever font is chosen, is identical on both
compositors, and is not an overlay (it takes no `/v1/overlays` slot). It is
rebuilt only while on; a line or the graph is redrawn only when it changed,
and the output presented only when a pixel did. A frame presented only for
it is counted in the output's `hud_frames` (and in `present_calls`), never
in `frame_rate_hz` or the panel's draws. Off, it costs nothing.
`/v1/diagnostics` reports `hud`: the settings, `frames`, `updates`, the
update cost (`last_update_ms`, `mean_update_ms`, `max_update_ms`) and each
panel with its place and lines.

## Effects, overlays, and control

Pane effects are set in the settings document's `effects` (see "Pane effects"
under settings). `GET /v1/effects` reports what each live pane draws, and is
read-only. `POST/DELETE /v1/overlays` manages explicit graphic overlays.
Effects and overlays are downstream of terminal state and cannot silently
replace it.

`POST /v1/control/shutdown` is an asynchronous clean stop. It terminates
managed child process groups and releases outputs; it cannot restart itself.
Use the host supervisor to start it again. `/v1/control/restart` requests an
orderly restart of the exact executable currently serving the API, preserving
the listen port, or, when that file was replaced on disk by an update, of the
new one at its path; this also works for a TCC-built development fixture
without silently switching to another installed binary. Package installation uses the
separate `/v1/control/update` flow and must be verified through
`/v1/control/update/status` and `/v1/version`.

### Restart after an update

The package never restarts Blit (ADR 0008), and an update through the API
restarts it as before. A package installed another way -- an `apt upgrade`
typed into a terminal inside Blit -- leaves the old Blit running, so Blit
checks every 5 s whether the binary it was started from was replaced and
reads the installed version from `/usr/lib/blit/VERSION`
([ADR 0205](adr/0205-restart-after-update.md)). A restart is pending when
that version is not its own. `document.server.restart_after_update`
decides what happens:

- `at-login-screen` (the default): Blit restarts itself, as
  `POST /v1/control/restart` does, at the first safe moment: the login
  manager is running and waiting for a sign-in (its identity `waiting`; a
  verified, rejected or unavailable identity counts as signed in), no other
  terminal session runs, no input has come for 10 s, and no API update is
  in progress. It does so at most once for an installed version.
- `manual`: Blit only reports it.

`/v1/diagnostics` reports `update`: `running_version`, `installed_version`,
`executable_replaced`, `restart_pending`, `restart_after_update` and, while
pending, `waiting_for`, what the safe moment still lacks (`login-screen`,
`signed-in`, `user-session`, `input`, `update`, `restart-attempted`).

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

Failures use `application/problem+json` with `type`, `title`, `status`, a
stable `code` and a prose `detail`. Clients must branch on the code, not
parse the detail. Common codes include `route_not_found`,
`method_not_allowed`, `revision_conflict`, `idempotency_conflict`,
`invalid_if_match`, `hibr_desktop_exists`, and `hibr_desktop_owner_missing`.

Log the code, status, request correlation data, and safe context. Do not log
bearer tokens, update credentials, PTY contents, or complete binary payloads.
A `404` for an unknown route is not evidence that the server is down; verify
`/v1/version` and then check the requested path against the schema.

Unknown response fields should be ignored. Resource IDs are opaque. Events and
snapshots carry revisions/sequences so clients can detect gaps and resync from
the authoritative GET endpoint. A schema or semantic breaking change requires
a new API version and ADR; changing a compositor implementation does not.

### Recovery table

| Observation | First check | Correct response |
| --- | --- | --- |
| Connection refused | supervisor/process and `/v1/version` | Start or repair the service; do not start a second server on the same port |
| `404 route_not_found` | API version and OpenAPI path | Correct the route; this is not evidence that rendering failed |
| `409` ownership/revision | resource `GET` and the session's `hibr_desktop` | Resynchronize, then retry with the current revision or repair the stale owner |
| black or stale display | `/displays`, runtime, screenshot, surface summary | Separate output discovery, producer commits, presentation cadence, and pixels |
| motion but no click | routes plus `pointer_button` trace | Verify source, button capability, target session, and aggregate state |
| duplicate desktop | diagnostics and each session's `hibr_desktop` role | Retire the stale attachment and keep one primary owner |
| slow scene updates | `surface_commit_hz` versus `frame_rate_hz` | Fix the producer boundary; do not label producer cadence as display refresh |

## Safe client checklist

- Pin and verify `api_version` before use.
- Discover capabilities and output readiness.
- Keep resource IDs and revisions; use idempotency keys on retries.
- Treat `409`, `503`, compositor failures, and unavailable outputs as
  recoverable typed states where the problem code permits.
- Resync authoritative state after a stream gap.
- Delete temporary sessions, routes, and overlays in cleanup paths.
- Do not log bearer tokens, update tokens, package bytes, or private paths.
