# Effect testing

Pane effects (ADR 0186, 0187) are renderer state, not terminal state. They are
kept per pane in the settings document, keyed by output name and pane ordinal,
and applied live:

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

Presentation effects and the switches (ADR 0187; the animated ones, ADR 0188
and 0190) are checked by `tools/tests/presentation_effects_gate.rb`, also on
one display split 1x2. Both gates are in the renderer suite.

On `gles` it checks:
- **warp:** pane 1 is captured plain, then with curvature 100. Each sampled
  warped pixel is predicted from the plain capture with the shared warp and
  the shader's linear sampling, wherever the 2×2 texels it reads are one
  colour (more than 5,000 must be compared); outside the tube it must be
  black;
- **glass scanlines:** with curvature and scanlines over a flat colour, every
  odd screen row is darkened by the shared rule and every even row is not.
  So they are drawn once, with no moiré and no flattening;
- **glow:** one white cell on black brightens a pixel 4 px away, leaves a
  far pixel black, and never reaches pane 2;
- **RGB shift:** vertical stripes three cells wide, with amount 50 on 8 px
  cells (samples 2 px apart). Every pixel matches the three-tap rule applied
  to the plain capture; more than a thousand pixels fringe; and with
  curvature too, the tube's outside stays black;
- **flicker:** over a dark grey pane, every frame has one brightness across
  the pane, within ±50% at 100, and it varies between frames;
- **horizontal sync:** over stripes at 100, of 20 frames some sway and some
  are plain, within 2 of the still picture; the rounded corners never move;
- **jitter:** stripe edges shimmer between frames while the pane's mean
  colour holds;
- **noise:** a small C probe checks that each frame's values are a pure
  function of its number;
- **isolation:** pane 2 is unchanged by pane 1's effects;
- **pointer:** a real SGR mouse report from a session in the curved pane
  names the cell the warp shows under the pointer, including the clamp in the
  black margin;
- **the plain path:** with no presentation effects, `present_panes` is 0.

On both compositors it checks the switches: master or pane switched off
draws exactly what no effects draws, costs nothing, keeps the values and is
reported by the API; a switch that is not a boolean is refused. On `native`
it also checks that saved curvature is kept and reported unavailable
(`cpu compositor`), while the local effects still draw.

```sh
ruby tools/tests/presentation_effects_gate.rb
BLIT_GATE_BACKEND=gles ruby tools/tests/presentation_effects_gate.rb
```

Effects on a zoomed display (ADR 0204) are checked by
`tools/tests/zoomed_effects_gate.rb`, in the renderer suite. Two mock
outputs: display-1, 1024x768, zoomed to 200%, and display-2, 512x384 at
100% -- display-1's logical size. Both run the same session (vertical
stripes three cells wide, drawn again on SIGWINCH) with the same effects.
On `gles` it checks:
- **same, enlarged:** with curvature, RGB shift, scanlines and corners on
  both, every physical pixel (x, y) of display-1 is display-2's
  (x / 2, y / 2), within 2. The effects are drawn, in logical coordinates,
  exactly as at 100%;
- **drawn:** the tube's outside is black in display-1's corners, and stripe
  edges in the middle row fringe;
- **pixel zoom:** every 2x2 block of display-1 is one colour, exactly;
- **animated:** with the glowing line, the frame clock ticks on the zoomed
  display, the band moves between captures, and every capture is still
  2x2 blocks;
- **pointer:** a real SGR mouse report from the curved, zoomed pane names
  the cell the warp shows at the pointer's logical position;
- **diagnostics:** `presentation_unavailable` is empty on the zoomed
  display, and the pass drew the pane;
- **100% unchanged:** back at 100%, the frame with the static effects is
  byte-identical to the one captured before zooming.

On `native` it checks that presentation effects stay unavailable
(`cpu compositor`) at 200%. `BLIT_ZOOMED_EFFECTS_SHOT=<path>` keeps the
100% capture with effects, for comparing two builds byte for byte.

```sh
BLIT_GATE_BACKEND=gles ruby tools/tests/zoomed_effects_gate.rb
```

## Bounded graphic overlays

`/v1/overlays` is the schema-first rectangle overlay resource. An overlay is
attached to one known display and has logical `x`, `y`, `width`, `height`,
`color`, and `opacity` fields. The KMS compositor (GLES or CPU) composites up
to 16 solid rectangles after terminal text and effects and before the software
pointer.
The lifecycle is revisioned through the normal resource response and is
covered by the terminal conformance runner.

The QEMU direct KMS capture probe (`tools/probes/qemu_kms_capture_init.sh`,
see [KMS testing](testing-kms.md)) creates an overlay on `display-47` and
verifies its blue pixel in the captured PPM. This checks that the API state
reaches KMS pixels rather than only being accepted by the API.

The screenshot overlay gate checks the corresponding failure mode reported
on physical displays, on the mock KMS display: a red rectangle is captured at
the top-left, deleted through the API, and captured again. The second BMP must
not retain the red pixel:

```sh
BLIT_GATE_BINARY=build/blit/blit-server \
  tools/tests/screenshot_overlay_gate.sh
```
