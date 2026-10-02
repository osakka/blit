# Gateway testing

The initial gateway is a TCC-built TCP service. It exposes the discovery
routes defined by the OpenAPI contract and a first WebSocket upgrade path.

## Build

From the repository root:

```sh
mkdir -p build/blit
tcc -std=c99 \
  -Ivendor/SDL3/include \
  src/blit_server.c \
  -Lbuild/sdl3-kms -lSDL3 -lutil \
  -o build/blit/blit-server
```

## Run locally

The dummy video driver is useful for exercising the network contract without
owning a physical DRM device:

```sh
SDL_VIDEODRIVER=dummy \
LD_LIBRARY_PATH=build/sdl3-kms \
  ./build/blit/blit-server 8080
```

The development listener defaults to loopback. The gateway has no TLS or
authentication yet; do not set `BLIT_BIND_ADDRESS` to a non-loopback address.

When SDL3 reports displays, the gateway creates one borderless SDL window and
renderer per output, positions each window at the reported bounds, and presents
an initial black frame. Renderer initialization failures remain in the server
diagnostic state; they are not hidden by a second rendering path.

## Exercise discovery

```sh
curl --fail http://127.0.0.1:8080/v1/server
curl --fail http://127.0.0.1:8080/v1/displays
curl --fail http://127.0.0.1:8080/v1/topology
curl --fail http://127.0.0.1:8080/v1/graphics
curl --fail http://127.0.0.1:8080/v1/audio
  curl --fail http://127.0.0.1:8080/v1/input/devices
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
