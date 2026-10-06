# Effect testing

Effects are renderer state, not terminal state. The canonical API is
`PUT /v1/effects`; the initial bounded preset is `scanlines`, with intensity
from `0` through `100`. The SDL3 backend applies it after text composition and
before the software pointer. The direct KMS backend returns
`effect_backend_unsupported` for effect updates because its correctness path
is text-only; it never reports a successful effect that it did not render.

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
`color`, and `opacity` fields. The SDL3 backend composites up to 16 solid
rectangles after terminal text and effects and before the software pointer.
The lifecycle is revisioned through the normal resource response and is
covered by the terminal conformance runner.

Direct KMS is intentionally text-only at this stage. Overlay creation and
deletion there return `overlay_backend_unsupported`; the API never reports a
successful overlay that the KMS path did not render.
