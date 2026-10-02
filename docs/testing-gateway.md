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
its display placement. The current slice accepts the command and surface
dimensions; argument/environment policy is intentionally still pending.

Unsupported inventories are returned as empty arrays until their native
backends are implemented. Display discovery is sourced from SDL3.

## Exercise the stream upgrade

The current slice accepts a WebSocket opening handshake and sends one
diagnostic text frame before closing the connection:

```sh
curl --include --no-buffer \
  -H 'Connection: Upgrade' \
  -H 'Upgrade: websocket' \
  -H 'Sec-WebSocket-Version: 13' \
  -H 'Sec-WebSocket-Key: dGhlIHNhbXBsZSBub25jZQ==' \
  http://127.0.0.1:8080/v1/stream
```

The persistent stream, subscriptions, event fan-out, and terminal/session
commands are the next gateway implementation slice. They must be added to the
same contract and runtime rather than introduced as a parallel protocol.
