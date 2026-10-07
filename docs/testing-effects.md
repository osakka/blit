# Effect testing

Effects are renderer state, not terminal state. The canonical API is
`PUT /v1/effects`; the initial bounded preset is `scanlines`, with intensity
from `0` through `100`. The SDL3 and direct KMS backends apply it after text
composition and before the software pointer. The response identifies the
active renderer as `sdl3` or `drm-kms`.

With a TCC-built server using the dummy SDL backend:

```sh
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=build/sdl3-kms \
  build/blit-desktop-current/bin/blit-server 8080
curl --fail http://127.0.0.1:8080/v1/effects
curl --fail -X PUT http://127.0.0.1:8080/v1/effects \
  -H 'Content-Type: application/json' \
  --data '{"revision":1,"enabled":true,"preset":"scanlines","intensity":40}'
```

The terminal conformance runner verifies effect revisioning and stale-update
rejection. The SDL capture gate additionally compares disabled and enabled
frames; both captures must be valid 1024×768 BMPs and their payload hashes must
differ. This proves the preset reaches pixels without making screenshot output
the canonical state.

## Bounded graphic overlays

`/v1/overlays` is the schema-first rectangle overlay resource. An overlay is
attached to one known display and has logical `x`, `y`, `width`, `height`,
`color`, and `opacity` fields. The SDL3 and direct KMS backends composite up
to 16 solid rectangles after terminal text and effects and before the software
pointer.
The lifecycle is revisioned through the normal resource response and is
covered by the terminal conformance runner.

The direct KMS capture gate creates an overlay on `display-47` and verifies
its blue pixel in the captured PPM. It also enables scanlines and verifies
that a populated row is followed by a suppressed row. This checks that the
API state reaches KMS pixels rather than only being accepted by the API.

The SDL screenshot overlay gate checks the corresponding failure mode reported
on physical displays: a red rectangle is captured at the top-left, deleted
through the API, and captured again. The second BMP must not retain the red
pixel:

```sh
BLIT_GATE_BINARY=build/blit/blit-server \
  tools/tests/screenshot_overlay_gate.sh
```
