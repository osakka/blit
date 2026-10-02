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
rejected until the grapheme policy is expanded. Font selection, cursor state,
and CRT effects remain later layers.

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
sessions. Input messages identify `session_id` and carry terminal data inside
the event object.
