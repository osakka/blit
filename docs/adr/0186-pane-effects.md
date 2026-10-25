# ADR 0186: Effects are per pane, local, and drawn by both compositors

- Status: accepted (the owner's decision, 2026-10-25)
- Date: 2026-10-25
- Scope: effects API and settings, both KMS compositors, diagnostics
- Replaces: the global `/v1/effects` scanlines effect

## Context

The owner wants effects in the spirit of cool-retro-term, per display or pane,
starting with rounded corners on each pane with a settable radius. There will
be many effects, and they must work together and stay light.

What existed was one global effect: `scanlines` with an intensity, set through
`PUT /v1/effects`. It had three problems:
- It was the same on every display.
- Only the CPU compositor drew it. Turning it on made an output fall back
  from the GPU ("a display effect is on"), undoing ADR 0181's gain.
- It was not saved.

It was also not idempotent. It darkened every second row of the whole output
each frame. The CPU compositor repaints only damage, so rows outside the
damage were darkened again on every frame.

## Options considered

1. **Add corners next to the global effect.** This keeps a second,
   incompatible model and the CPU fallback, and makes per-pane effects a
   later rewrite.
2. **A full effect graph with user-ordered stages.** More than any planned
   effect needs. User-chosen ordering multiplies what has to be tested.
3. **Per-pane effects in one registry, local effects applied inside each
   view's repaint by both compositors.** This is the chosen option.

## Decision

**One registry.** `src/blit_effects.h` names each effect, its one parameter,
its bounds and its place in the order. It holds the pixel rule as pure
functions of position and parameters: scanline alpha, and corner coverage
derived from the title-bar corners' 8×8 sampling. The settings parser, the
API and both compositors all use it. Adding an effect is a registry row, a
parameter, and one branch in each compositor.

**Per pane, kept in settings.** `document.effects` is keyed like
`display_layouts`, by stable connector and pane ordinal:
`{"DP-1": {"1": {"corners": {"radius_px": 24}, "scanlines": {"intensity": 40}}}}`.

- It is validated strictly (`400 invalid_settings_effects`), applied live and
  persisted.
- An invalid stored section is logged and ignored at startup.
- Entries for absent outputs or panes are kept.
- `GET /v1/effects` reports each live pane's effective set (radius clamped)
  and the available effects. It is read-only; a write is
  `405 effects_read_only`, so there is one place effects are set.

**Local effects only, applied where the view repaints.** Every effect here is
local: a pixel's result depends only on its position in the pane and on the
pixel underneath. Each compositor applies a pane's effects last in that
view's repaint, over exactly the rectangle it repainted:
- the CPU at the end of `draw_view`;
- the GPU at the end of `kms_gles_view`, recorded into the output's scene.

The pixels underneath were just repainted opaquely, so an effect is applied
once per pixel whatever the damage, and costs nothing outside damage. The GPU
draws corners as four coverage quads from one 2r×2r texture built by the
shared function, and scanlines as one alpha row each. The CPU blend rounds as
the GPU's does.

**Geometry and order.**
- Effects are measured from the pane's pixel region. Its 1–5 px margins
  outside the cell grid are black on every full repaint in both compositors,
  so a corner's arc meets black.
- A radius is clamped to half the pane's shorter side when drawn and
  reported.
- Scanline parity is counted from the pane's top.
- The order is fixed by the registry: scanlines, then corners, because
  corners define the edge.
- Pictures inside the pane are under the effects. The pointer and API
  overlays are above them.

**Cost is visible.** Each output's render diagnostics carry an `effects`
object: CPU milliseconds for the last and worst frame, and GPU quads for the
last frame.

## Consequences

- With effects on, the GPU keeps composing; the CPU-fallback reason "a
  display effect is on" is gone.
- The old global scanlines are removed with no compatibility path (a fresh
  project). Scanlines are now a per-pane effect.
- **Not in this change:** effects that sample neighbours or move geometry
  (curvature, glow/bloom, chromatic shift, burn-in), and animated ones
  (flicker, noise). They need:
  - an offscreen per-pane target and a full-pane pass, on the GPU only;
  - on the CPU, to be reported as unavailable, not imitated;
  - for animated effects, a frame clock that is off by default and
    rate-limited.

  Local effects never assume they are the last thing drawn into the scanout
  buffer, so that class can be added beneath them.

## Evidence

`tools/tests/pane_effects_gate.rb` runs on `native` and `gles`, with one
display split 1x2 and each pane painted one colour. It checks:
- placement on pane 1's own corners;
- 7,680 pixels against the shared rule: exact on the CPU, within 1 on the
  GPU;
- no pixel changed by two seconds of redraws over the corner cells;
- the GPU composing the frame itself;
- the cost reported, and under 5 ms of CPU for the pane's full repaint;
- strict settings, the read-only endpoint, clamping, and persistence across a
  restart.

`kms_compose_gate`'s held-frame test now carries a pane scanline effect.
