# Gateway testing

## Remote runtime diagnostics

Start Blit on an address reachable by the operator, with authentication
enabled. For example, a system service may set `BLIT_BIND_ADDRESS=0.0.0.0`
and a secret `BLIT_AUTH_TOKEN`; firewall policy should expose the API port only
to the trusted management network. Query the runtime snapshot with:

```sh
curl --fail --silent \
  -H 'Authorization: Bearer YOUR_TOKEN' \
  http://DESKTOP_IP:8080/v1/diagnostics/runtime
```

This endpoint is read-only and reports backend readiness, display/output
counts, refresh scheduling, frame counters, render duration, missed
deadlines, and recent bounded runtime events. Blit may create or stop terminal
sessions through its API, but it cannot start itself after it is dead; daemon
start/stop/restart belongs to the host supervisor (systemd, an init service,
or SSH). Use that supervisor for lifecycle recovery, then use this endpoint to
verify readiness.

For a clean API-driven shutdown while the server is alive, call:

```sh
curl --fail --request POST http://127.0.0.1:8080/v1/control/shutdown
```

The endpoint acknowledges first, then the event loop terminates child process
groups and releases SDL/KMS outputs before exiting. Starting a new server still
belongs to the host supervisor or launch command because a stopped process
cannot serve its own restart request.

## Supervised package updates

The Debian package installs a root-owned `blit-update-helper` behind
`/run/blit/update.sock`. The network update API does not write packages as the
gateway user: it verifies the package name and SHA-256, then delegates
installation to that helper and requests a supervised restart.

The package includes `blit.service`. Installation creates a root-owned update
token and starts the server as the dedicated `blit` account with `video`,
`render`, and `input` supplementary groups. Existing `/etc/blit/blit.env` and
`/etc/blit/update.token` files are preserved on upgrades. The default package
configuration binds to `0.0.0.0:8080` for the trusted test LAN:

```sh
sudo systemctl status blit.service
```

`BLIT_UPDATE_TOKEN_FILE` is preferred for the update token. The file is read
once at startup, trimmed of trailing whitespace, and never logged or returned;
the `blit` service account therefore needs read access (the example uses the
`blit` group). `BLIT_UPDATE_TOKEN` remains supported for protected
environment-file deployments. The update token is deployment configuration and
is not generated, returned, or logged by Blit. Verify:

```sh
curl --fail http://127.0.0.1:8080/v1/version
curl --fail http://127.0.0.1:8080/v1/control/update/status
```

reports the expected server version and `state: "ready"`. Upload only a
package whose SHA-256 was independently recorded:

```sh
sha256sum blit_0.1.223-1_amd64.deb
curl --fail --request PUT \
  --header 'X-Blit-Update-Token: YOUR_UPDATE_TOKEN' \
  --header 'X-Blit-SHA256: RECORDED_SHA256' \
  --header 'Content-Type: application/vnd.debian.binary-package' \
  --data-binary @blit_0.1.223-1_amd64.deb \
  http://127.0.0.1:8080/v1/control/update
```

Poll `/v1/control/update/status` until the supervisor reports the new
version; an interrupted upload or `403` response is not an installation.

For a bounded operator update with preflight checks, use the repository-only
helper. It requires the live update credential explicitly, never substitutes
the API bearer token, and never retries a rejected credential:

```sh
BLIT_UPDATE_TOKEN_FILE=/secure/path/live-update.token \
  BLIT_AUTH_TOKEN_FILE=/secure/path/live-api.token \
  tools/deploy/update-live.sh 192.168.10.54 blit_0.1.223-1_amd64.deb
```

`BLIT_UPDATE_TOKEN` may be used instead of the update-token file. `thisisatest`
is not a built-in token; only the value configured on the running host in
`BLIT_UPDATE_TOKEN` or `BLIT_UPDATE_TOKEN_FILE` is accepted. A `403` means the
credential is wrong and the package was not installed. The helper also checks
package metadata, SHA-256, update readiness, and the final running version.

The idle-timeout gate holds a valid upload open without sending its body and
verifies that the gateway transitions to `update upload idle timeout` instead
of retaining a dead staged transfer indefinitely:

```sh
BLIT_GATE_BINARY=build/blit/blit-server \
  tools/tests/update_idle_timeout_gate.sh
```

## Current acceptance evidence

The TCC-built server has been exercised locally with the SDL dummy backend and
in QEMU with direct DRM/KMS:

- OpenAPI YAML parses as OpenAPI 3.1 with 19 paths and 41 schemas.
- The WebSocket upgrade on `/v1/stream` returns `101 Switching Protocols`.
- The TCC WebSocket probe also delivers a deliberately fragmented, masked
  subscription message and receives `subscription_received`, proving TCP and
  WebSocket frame boundaries do not have to align.
- A live `2x1` topology mutation produces two server-owned panes.
- A PTY session, revisioned cell update, and coherent frame commit all return
  their schema-defined success responses.
- Terminal-session `arguments` and bounded string `environment` overrides are
  passed through the canonical PTY launcher and verified in the resulting
  surface output.
- QEMU direct-KMS mode reports a ready backend, KMS display mode, graphics
  capabilities, evdev keyboard/pointer inventory, and a running PTY session.
- QEMU virtio-GPU mode reports a ready SDL3 KMSDRM backend, a virtio PCI
  graphics card, refresh-aware runtime diagnostics, and a running PTY-backed
  Hibr workload whose surface revision advances with desktop text.
- The terminal conformance runner passes printable text, cursor addressing,
  styles, erase, scrolling, scroll regions, alternate-screen restoration,
  saved cursors, UTF-8, truecolor SGR, and fragmented CSI sequences over the
  public API.
- Two consecutive terminal-conformance runs pass against one server, proving
  explicit session deletion releases bounded session capacity.
- The reproducible two-display VKMS/QEMU fixture passes the logical pane matrix
  (`1x1`, `2x1`, `1x2`, and `2x2`, with expected pane counts of 2, 4, 4, and
  8), restores `1x1`, then starts the real Hibr primary and `desktop --join`
  sessions on distinct displays; both reach `running` and their surface
  revisions advance.
- The same fixture configures and reads back explicit `evdev-keyboard` and
  `evdev-pointer` routes targeting different live Hibr sessions; route IDs,
  device IDs, targets, and enabled state match the OpenAPI contract.
- The fixture creates explicit keyboard and pointer routes to dedicated Hibr
  input-probe sessions and verifies the route records. End-to-end QEMU event
  injection is verified separately by the Blit-only input gate: a TCC-built
  PTY byte sink receives keyboard bytes on the first routed surface and SGR
  mouse bytes on the second routed surface. That gate requires the QEMU kernel
  input fragment in `tools/probes/qemu-kernel.input.config` to be merged with
  `CONFIG_INPUT_EVDEV=y`, `CONFIG_MOUSE_PS2=y`, and `CONFIG_INPUT_UINPUT=y`.
- The ordinary-console Hibr run verifies both terminal surfaces independently:
  the primary and joined surface revisions advance, and the joined surface is
  not a mirror of the primary Blit HTTP surface; Hibr hold fans the terminal
  stream to each Blit PTY client. With current Hibr binaries/modules and the
  fixture's BusyBox `mkdir` applet, Hibr creates its control socket and the
  probe validates `displays`, `move`, `resize`, and `focus`, including moving a
  window to the joined display.

The initial gateway is a TCC-built TCP service. It exposes the discovery
routes defined by the OpenAPI contract and a first WebSocket upgrade path.

HTTP reads are event-loop bounded: the gateway retains incomplete requests per
connection, reads at most the bytes available for that readiness event, and
expires a request that remains incomplete for five seconds. This prevents a
fragmented or slow TCP client from stalling KMS presentation, PTY draining,
input routing, or WebSocket servicing. The regression gate is:

```sh
BLIT_GATE_PORT=19202 BLIT_GATE_TOKEN=blit-network-gate \
  ruby tools/tests/http_fragmentation_gate.rb 19202
```

It holds one request fragmented for one second while requiring an independent
authenticated diagnostics request to complete in under 800 ms.

The SDL3 adapter obtains the complete fullscreen mode list when the backend
provides it and reports the current/native active mode through the same
`/v1/displays` contract as direct KMS. If SDL cannot provide a mode list, the
reported single entry is the observed current mode, not an invented maximum.
SDL renderers are explicitly configured for vsync and `/v1/diagnostics`
reports `presentation_sync` as `sdl-vsync`; unsupported SDL synchronization is
reported as `scheduler-only` rather than being presented as a hardware timing
guarantee.

Writes use the same bounded policy: HTTP and WebSocket sends are nonblocking.
If a receiver cannot accept the next bounded write, the HTTP response is
closed or the WebSocket stream is removed. Blit does not sleep in the gateway
loop waiting for a remote reader. The network gate also opens a small receive
buffer, requests a large surface snapshot without reading it, and verifies
that an independent diagnostics request completes in under 800 ms.

The Hibr VKMS fixture must stage the exact Hibr executable, modules, and
desktop tree being tested. Refresh an existing initramfs tree with:

```sh
tools/probes/stage_hibr_qemu.sh /tmp/hibr-initroot-current /path/to/hibr
```

The staging helper replaces and version-checks the canonical Blit binary on
every run, then stages the optional Blit `console-blit.so` provider and
adapter files for provider-specific probes. It does not modify the Hibr
checkout. It also rebuilds the repository-owned HTTP, PTY, WebSocket, and
uinput probe helpers with TCC, preventing stale binaries from entering the
initramfs. It does not stage or invoke a Blit shell launcher.

For the isolated input gate, use the staging helper, then boot the fixture with
`blit.input_only=1` on the kernel command line:

```sh
tools/probes/stage_hibr_qemu.sh /tmp/hibr-initroot-current /path/to/hibr
# Add `blit.input_only=1` to the QEMU kernel command line. The gate checks
# PTY delivery while the network input stream also carries key releases,
# repeats, pointer buttons, and wheel events.
# For a bounded run in automation, add `blit.input_wait=8`; the default wait
# remains 30 seconds. The fixture also enables application keypad mode and
# verifies that KP1 reaches the PTY as the VT/SS3 sequence `ESC O q`.
# For the full held-desktop pane matrix, add `blit.hibr_panes=1`; this starts
# one primary and three `desktop --join` clients across a Blit-owned `2x2`
# pane layout and requires four attached surfaces.
```

## Build

From the repository root:

```sh
tools/build-tcc.sh build/blit/blit-server
```

## Run locally

The dummy video driver is useful for exercising the network contract without
owning a physical DRM device:

For an SDL-path framebuffer capture and render-cost trace, create a writable
directory and set the diagnostic variables before starting the server:

```sh
mkdir -p /tmp/blit-sdl-capture
BLIT_SDL_SCREENSHOT_DIR=/tmp/blit-sdl-capture \
BLIT_SDL_SCREENSHOT_AFTER=2 \
BLIT_SDL_TRACE_TIMING=1 \
SDL_VIDEODRIVER=dummy \
./build/blit/blit-server 8080
```

The server writes one BMP per SDL output after the requested frame and logs
`render_ms` for each composed frame. This is an opt-in diagnostic path; it is
not required by the installed runtime and is never enabled by default.

```sh
SDL_VIDEODRIVER=dummy \
LD_LIBRARY_PATH=build/sdl3-kms \
  ./build/blit/blit-server 8080
```

The installed listener defaults to all IPv4 interfaces (`0.0.0.0`) so a client
on the management network can reach it. Authentication is disabled when
`BLIT_AUTH_TOKEN` is unset; set a token and apply a firewall policy before
exposing the API. Send `Authorization: Bearer <token>` on HTTP and WebSocket
upgrades:

```sh
BLIT_AUTH_TOKEN='replace-me' \
  ./build/blit/blit-server 8080
curl --fail -H 'Authorization: Bearer replace-me' \
  http://127.0.0.1:8080/v1/server
```

The repeatable bind/auth gate is:

```sh
tools/tests/network_auth_gate.sh
```

The session lifecycle gate starts a child that ignores `SIGTERM`, verifies that
API deletion escalates to a bounded `SIGKILL`, and immediately checks that the
diagnostics endpoint remains responsive:

```sh
BLIT_GATE_BINARY=build/blit/blit-server \
  tools/tests/session_lifecycle_gate.sh
```

The shutdown gate verifies that `POST /v1/control/shutdown` acknowledges the
request and the Blit process exits cleanly:

```sh
BLIT_GATE_BINARY=build/blit/blit-server \
  tools/tests/shutdown_gate.sh
```

It requires `401` without a token and `200` with the configured bearer token.

TLS termination, token rotation, and per-client authorization remain
deployment responsibilities; do not expose an unauthenticated listener.

When SDL3 reports displays, the gateway creates one borderless SDL window and
renderer per output, positions each window at the reported bounds, and presents
an initial black frame. For a direct-KMS session on a DRM-capable VT, set
`BLIT_DISPLAY_BACKEND=kms`; that path owns DRM connectors and page-flips its
scanout buffers without creating SDL windows.

SDL output uses SDL's automatic renderer selection by default so a physical
host can use its accelerated renderer. Set `BLIT_RENDERER=software` only for a
deterministic software/VKMS fixture. The selected renderer is reported by
`GET /v1/diagnostics/runtime` as `snapshot.render.renderer`.

## Exercise discovery

```sh
curl --fail http://127.0.0.1:8080/v1/server
curl --fail http://127.0.0.1:8080/v1/displays
curl --fail http://127.0.0.1:8080/v1/topology
curl --fail http://127.0.0.1:8080/v1/graphics
curl --fail http://127.0.0.1:8080/v1/audio
  curl --fail http://127.0.0.1:8080/v1/input/devices
curl --fail http://127.0.0.1:8080/v1/diagnostics
```

The audio inventory is sourced from usable ALSA PCM nodes under `/dev/snd`;
the API returns an empty device list when no playback or capture PCM exists.
It does not infer audio capability from the presence of the directory alone.

## Exercise a PTY-backed session

The first terminal slice starts the requested command under a real PTY. Keep
this test local unless the deployment has configured its command policy and
`BLIT_AUTH_TOKEN`; bearer authentication is enforced whenever that token is
set.

For a network deployment, set an exact, comma-separated executable allowlist:

```sh
BLIT_AUTH_TOKEN=change-me \
BLIT_COMMAND_ALLOWLIST=/usr/bin/hibr,/bin/sh \
blit 8080
```

The allowlist compares the complete requested path. Blit does not invoke a
shell or perform `PATH` lookup, and a denied executable returns `403` with
problem code `command_not_allowed`. When the variable is unset, command
policy is open for local development and deterministic tests; production
network deployments should set it explicitly. Hibr's installed executable
can therefore be allowed directly while Hibr continues to own `desktop` and
`desktop --join`.

```sh
curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/bin/sh","surface":{"width":80,"height":24}}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

Use the returned numeric session id with `GET` or `PATCH` to inspect or update
its display placement. Pass an enumerated `display_id` at creation time to
route that session's surface to one output:

```sh
curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/bin/sh","display_id":"display-2","surface":{"width":80,"height":24}}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

An omitted `display_id` targets the first output for compatibility. Unknown
display IDs are rejected with `display_not_found`; arguments and environment
are passed only after the executable path passes the configured policy. The nested `surface`
object and its `width`/`height` members are required by the OpenAPI contract;
omitting them returns the typed `session_create_failed` problem.

## Discover and attach to Blit panes

Blit derives logical panes internally and exposes their opaque identifiers from
topology discovery. A session attached to a pane is automatically sized to the
pane's cell bounds:

```sh
curl --fail http://127.0.0.1:8080/v1/topology

curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"command":"/opt/bin/hibr-desktop","pane_id":"pane-display-1-1","surface":{"width":80,"height":24}}' \
  http://127.0.0.1:8080/v1/terminal-sessions
```

The pane identifier and dimensions in the topology response are authoritative;
clients must not derive pane names or pixel geometry. For development, the
initial grid can be selected with `BLIT_PANE_LAYOUT=1x1`, `2x1`, `1x2`, or
`2x2`.

The split can also be changed through the topology API before sessions are
attached:

```sh
curl --fail --request PUT \
  --header 'Content-Type: application/json' \
  --data '{"layout":{"columns":2,"rows":2}}' \
  http://127.0.0.1:8080/v1/topology
```

Blit rejects layout changes while a pane is occupied with
`topology_in_use`; detach or stop joined sessions before changing the grid.

Resize a running terminal through the same session resource. The gateway
updates the PTY window size, preserves the overlapping cells on both the main
and alternate grids, blanks cells outside the new bounds, clamps the cursor,
resets the scroll region to the full surface, clears deferred wrapping,
increments `surface_revision`, and emits a surface-delta event:

```sh
curl --fail --request PATCH \
  --header 'Content-Type: application/json' \
  --data '{"surface":{"width":120,"height":40}}' \
  http://127.0.0.1:8080/v1/terminal-sessions/1
```

Dimensions outside the bounded surface policy are rejected with
`invalid_surface_resize`.

Main-screen lines that scroll off the top are retained in a bounded 1000-line
ring and can be inspected without disturbing the live surface:

```sh
curl --fail \
  http://127.0.0.1:8080/v1/surfaces/surface-1/scrollback
```

The response is oldest-first and reports the current `view` position. Set the
view through the same resource to render history on the display; `view: 0`
returns to the live screen:

```sh
curl --fail --request PATCH \
  --header 'Content-Type: application/json' \
  --data '{"view":3}' \
  http://127.0.0.1:8080/v1/surfaces/surface-1/scrollback
```

Output from an alternate screen is never added to this history. Selection and
copy remain separate terminal-view work.

## Apply cells and present a frame

The created session owns a surface whose initial revision is `1`. Cell updates
must name that revision; successful updates return the next revision. A frame
request then references the surface and its current revision:

```sh
curl --fail --request PUT \
  --header 'Content-Type: application/json' \
  --data '{"base_revision":1,"cells":[{"x":0,"y":0,"text":"hello"}]}' \
  http://127.0.0.1:8080/v1/surfaces/surface-1/cells

curl --fail --request POST \
  --header 'Content-Type: application/json' \
  --data '{"base_revision":2,"surface_ids":["surface-1"]}' \
  http://127.0.0.1:8080/v1/frames
```

Stale cell revisions are rejected with `409 Conflict`. Cell mutations update
authoritative state; frame receipts record acceptance and trigger the SDL3
output adapter from an immutable surface/revision plan. The first frame for an
output is a full draw; subsequent single-surface frames use the captured dirty
rows, while multi-surface frames conservatively redraw the output. The renderer
uses deterministic vertical stacking for multiple surfaces on one output and
SDL3's built-in bitmap text primitive. Cell styles accept six-digit foreground
and background colors plus bold, dim, underline, and inverse flags; invalid
colors are rejected. Text is validated as UTF-8 and decoded into logical
cells; malformed UTF-8 and zero-width sequences without a preceding base
glyph are rejected. Wide glyphs occupy two physical columns, and combining
marks, joiners, and variation selectors remain attached to their base cell.
A cell update may also carry a
bounded `{x,y,visible}` cursor object; session GET responses report the current
cursor, and out-of-bounds cursor positions are rejected. The API accepts only
valid logical cell sequences. PTY-driven cursor movement now
updates the canonical surface for printable UTF-8 output, CR/LF,
backspace, tab, and the core ANSI cursor/erase commands `A`, `B`, `C`, `D`,
`H`, `f`, `J`, and `K`. Output reaching the bottom row scrolls the surface by
one row and marks the full surface dirty. Selection and blink timing remain
later layers. Unsupported PTY widths become a visible replacement cell so
cursor positioning remains deterministic.

Session responses expose `surface_revision` separately from the session
configuration `revision`. Clients should use that value as `base_revision` for
cell updates. A `409 revision_conflict` response also carries the authoritative
current revision in `Problem.revision`, so clients can retry without another
discovery request when PTY output wins the race. The same revision-bearing
conflict applies when a frame references stale surfaces.

Session creation, cell mutations, and frame commits accept the documented
`Idempotency-Key` header. Within the bounded in-memory replay window, an
identical request returns its original response without spawning another PTY
or applying state again; reusing a key with a different payload returns
`idempotency_conflict`.

Unsupported inventories are returned as empty arrays until their native
backends are implemented. Display discovery is sourced from SDL3.

SDL3 mouse clicks, drags, and wheel events are routed to the Blit pane under
the pointer and encoded as SGR mouse reports for the owning PTY. A primary
button press focuses that pane; keyboard input then follows the focused live
session until the API changes focus. Hibr's console mouse mode controls
whether the joined desktop consumes those reports. Pointer capture, touch,
and seat-level policy remain follow-up work. SDL windows and evdev devices are
normalized to the same virtual-desktop coordinate space; only the final SDL
warp uses per-window local coordinates. This prevents a pointer on an
auxiliary monitor from being hit-tested or clicked against the primary
monitor. The 0.1 API supports explicit
`evdev-keyboard` and `evdev-pointer` routes through `/v1/input/routes/{id}`;
an explicit route takes precedence over focus or pointer hit-testing for its
device source. Enabling a route disables any other enabled route for that
device, preventing duplicate delivery. The Blit-only QEMU gate verifies both
route records and end-to-end virtual-evdev delivery. SGR button presses use
button codes 0–2 and releases use the same code with the `m` terminator;
legacy VT reports use the protocol's button-3 release code and 32-byte offset.
Evdev Shift, Alt, and Ctrl state contributes the standard SGR/VT modifier bits
to button, wheel, and drag reports.

When a routed PTY exits, Blit disables the route during lifecycle reaping but
retains its `target_session_id` for diagnostics and replay-safe reconciliation.
Re-enabling that route against the stopped session returns the typed
`input_route_target_stopped` conflict; a new live session must be selected.

The evdev service also removes a keyboard or pointer when its descriptor
reports EOF, `ENODEV`, or another terminal read error. It closes and compacts
the active descriptor set, disables the aggregate route only when its last
device disappears, and reports `input_device_missing` with zero active devices
through `/v1/diagnostics`. The device-loss path does not silently reattach a
replacement device; clients must observe `/v1/input/devices` and configure a
new route when hardware returns. The QEMU input gate exercises descriptor
removal with `blit.input_loss=1`; QEMU's surviving baseline devices verify
that aggregate routes remain enabled until the final device disappears, and
the fixture recreates the synthetic devices to verify bounded re-enumeration.
The same run subscribes the WebSocket probe to diagnostics and requires
sequenced `input_device_lost` and `input_device_reconnected` events.

## Exercise the stream upgrade

The current slice accepts a WebSocket opening handshake, keeps the connection
open, and sends an initial diagnostic text frame. HTTP surface mutations are
then broadcast to connected streams:

```sh
curl --include --no-buffer \
  -H 'Connection: Upgrade' \
  -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  http://127.0.0.1:8080/v1/stream
```

Clients may send a text WebSocket message to receive a subscription diagnostic
acknowledgement, for example a `frames`-only subscription. Event fan-out is
filtered by subscription, and client text frames are unmasked. Ping frames
receive pong responses; surface deltas and frame receipts are the currently
implemented event types, along with `terminal_output` events from PTY-backed
sessions. A `surface_delta` includes the current surface revision, cursor, and
bounded damage rectangle. Input messages identify `session_id` and carry
terminal data inside the event object. Broadcast surface, terminal-output, and
frame events carry a monotonic server `sequence`; clients can detect missed
events when a reconnect produces a gap. Reconnect with
`/v1/stream?since=<last-sequence>` to replay the bounded retained history. If
the cursor is older than retention, the stream emits `replay_gap`; clients
must resynchronize from the HTTP resources.

The canonical resynchronization endpoint is:

```sh
curl --fail http://127.0.0.1:8080/v1/surfaces/surface-1/cells
```

It returns the current surface revision, cursor, non-empty cells, Unicode text,
and cell styles.
