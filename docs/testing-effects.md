# Effect testing

Pane effects (ADR 0186) are renderer state, not terminal state. They are kept
per pane in the settings document, keyed by output name and pane ordinal, and
applied live:

```json
"effects": {
  "DP-1": {
    "1": {"corners": {"radius_px": 24}},
    "2": {"corners": {"radius_px": 24}, "scanlines": {"intensity": 40}}
  }
}
```

`GET /v1/effects` reports what each live pane draws: the available effects with
their parameter and maximum, and each pane's set, with a corner radius clamped
to half the pane's shorter side. It is read-only; any write is
`405 effects_read_only`. Each output's render diagnostics carry an `effects`
object: CPU time for the last and worst frame, and GPU quads for the last one.

`tools/tests/pane_effects_gate.rb` splits one mock display 1x2 and paints each
pane one colour. It runs on both compositors (`BLIT_GATE_BACKEND=native` and
`gles`) and checks:
- placement: the pane's own four corners only;
- every pixel of the corner squares and two rows against the shared rule in
  `src/blit_effects.h` (exact on the CPU, within 1 on the GPU);
- idempotence: two seconds of redraws over the corner cells change no pixel;
- that the GPU composes the frame itself with effects on;
- strict settings, the read-only endpoint, radius clamping, and persistence
  across a restart.

```sh
ruby tools/tests/pane_effects_gate.rb
BLIT_GATE_BACKEND=gles ruby tools/tests/pane_effects_gate.rb
```

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
