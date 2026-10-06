# Gateway testing

## Current acceptance evidence

The TCC-built server has been exercised locally with the SDL dummy backend and
in QEMU with direct DRM/KMS:

- OpenAPI YAML parses as OpenAPI 3 with 13 routes and 30 schemas.
- The WebSocket upgrade on `/v1/stream` returns `101 Switching Protocols`.
- A live `2x1` topology mutation produces two server-owned panes.
- A PTY session, revisioned cell update, and coherent frame commit all return
  their schema-defined success responses.
- Terminal-session `arguments` and bounded string `environment` overrides are
  passed through the canonical PTY launcher and verified in the resulting
  surface output.
- QEMU direct-KMS mode reports a ready backend, KMS display mode, graphics
  capabilities, evdev keyboard/pointer inventory, and a running PTY session.
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

The Hibr VKMS fixture must stage the exact Hibr executable, modules, and
desktop tree being tested. Refresh an existing initramfs tree with:

```sh
tools/probes/stage_hibr_qemu.sh /tmp/hibr-initroot-current /path/to/hibr
```

The staging helper preserves the optional Blit `console-blit.so` provider and
adapter files for provider-specific probes; it does not modify the Hibr
checkout. It does not stage or invoke a Blit shell launcher.

For the isolated input gate, compile the three TCC test helpers into the staged
tree and boot the fixture with `blit.input_only=1` on the kernel command line:

```sh
tcc -std=c99 tools/probes/emit_uinput.c -o /tmp/hibr-initroot-current/blit-stage/emit-uinput
tcc -std=c99 tools/probes/pty_input_echo.c -o /tmp/hibr-initroot-current/blit-stage/pty-input-echo
tcc -std=c99 tools/probes/ws_input_probe.c -o /tmp/hibr-initroot-current/blit-stage/ws-input-probe
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
mkdir -p build/blit
tcc -O2 -std=c99 \
  -Ivendor/SDL3/include \
  -Ibuild/deps/prefix/include \
  -Ibuild/deps/prefix/include/libdrm \
  src/blit_server.c src/blit_kms.c src/blit_font.c \
  -Lbuild/sdl3-kms -lSDL3 \
  -Lbuild/deps/prefix/lib/x86_64-linux-gnu -ldrm -lutil \
  -o build/blit/blit-server
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

The development listener defaults to loopback. Authentication is disabled when
`BLIT_AUTH_TOKEN` is unset. For a network-bound instance, configure the token
and send `Authorization: Bearer <token>` on HTTP and WebSocket upgrades:

```sh
BLIT_BIND_ADDRESS=0.0.0.0 BLIT_AUTH_TOKEN='replace-me' \
  ./build/blit/blit-server 8080
curl --fail -H 'Authorization: Bearer replace-me' \
  http://127.0.0.1:8080/v1/server
```

The repeatable bind/auth gate is:

```sh
tools/tests/network_auth_gate.sh
```

It requires `401` without a token and `200` with the configured bearer token.

TLS termination, token rotation, and per-client authorization remain
deployment responsibilities; do not expose an unauthenticated listener.

When SDL3 reports displays, the gateway creates one borderless SDL window and
renderer per output, positions each window at the reported bounds, and presents
an initial black frame. For a direct-KMS session on a DRM-capable VT, set
`BLIT_DISPLAY_BACKEND=kms`; that path owns DRM connectors and page-flips its
scanout buffers without creating SDL windows.

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

## Exercise a PTY-backed session

The first terminal slice starts the requested command under a real PTY. Keep
this test local because command authorization and remote authentication are
not implemented yet:

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
display IDs are rejected with `display_not_found`; argument/environment policy
is intentionally still pending.

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
colors are rejected. Text is validated as UTF-8 and decoded as one
non-combining code point per cell; malformed UTF-8 and combining marks are
rejected until the grapheme policy is expanded. A cell update may also carry a
bounded `{x,y,visible}` cursor object; session GET responses report the current
cursor, and out-of-bounds cursor positions are rejected. The API accepts only
code points classified as one cell wide; combining and known wide ranges are
rejected until grapheme shaping is added. PTY-driven cursor movement now
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
and seat-level policy remain follow-up work. The 0.1 API supports explicit
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
