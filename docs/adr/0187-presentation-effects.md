# ADR 0187: Curvature and glow are presentation effects, drawn as the GPU presents

- Status: accepted (2026-10-26, after the measurement below)
- Date: 2026-10-26
- Scope: effects registry, GLES compositor, pointer input, effects API and
  settings, diagnostics
- Builds on: ADR 0186 (pane effects), ADR 0181 (GLES compositor)

## Context

ADR 0186 added local effects: corners and scanlines, where a pixel's result
depends only on its position and the pixel underneath. The owner wants the
rest of the cool-retro-term look, starting with screen curvature and glow.
Neither is local:
- Curvature moves geometry: a screen pixel shows content from somewhere else.
- Glow reads neighbours: a pixel brightens with what is around it.

ADR 0186 deferred this class to "an offscreen per-pane target and a full-pane
pass, on the GPU only". The effects must stay light. Most panes have none,
and those must cost nothing.

What exists already decides most of the design:
- The GLES compositor draws every view into a persistent per-output offscreen
  texture, the scene, repainting only damage.
- Every presented frame then copies the whole scene to the scanout buffer in
  one quad (`blit_gles_scene_draw`).
- The pointer and API overlays are drawn after that copy, in screen space.

The scene is the offscreen target ADR 0186 asked for, and the copy is a
full-pane pass that already runs.

## Options considered

1. **A new per-pane offscreen target and pass.** Duplicates the scene, adds
   memory and a pass per pane. Rejected: the scene already holds every pane.
2. **More branches in the compose shader.** That shader runs for every glyph
   and image fragment. A curvature branch there taxes all drawing, effect or
   not. Rejected.
3. **A second program for the scene-to-screen copy, applied only to panes
   with presentation effects.** Chosen.

For glow's blurred source:
- **Mipmaps of the scene.** Rejected: the context is GLES 2
  (`EGL_CONTEXT_CLIENT_VERSION 2`), where a non-power-of-two texture cannot
  be mipmapped without `GL_OES_texture_npot`. It would also bleed across
  panes.
- **A two-pass separable Gaussian blur at full size.** The smoothest result,
  but the most GPU work. Kept as a possible later refinement.
- **A small downsampled copy per pane, built by Blit's own draw calls.**
  Chosen: plain GLES 2, cheap, and kept within each pane.

## Decision

**The registry gains a class.** Each `blit_effects.h` spec gets a class:
- `local`: corners and scanlines (ADR 0186), applied inside each view's
  repaint into the scene, by both compositors.
- `presentation`: curvature and glow, applied when the scene is presented,
  by the GLES compositor only.

The local loops skip presentation ids, and the CPU compositor never draws
them. The registry stays the one source of truth: settings, the API, both
compositors and pointer input all read it.

**Parameters.** In `document.effects`, beside the local ones:
- `curvature: {amount: 0..100}`
- `glow: {intensity: 0..100}`

Both are validated by the same parser. 0 is off.

**Curvature geometry**, defined once in the registry as a pure C function and
mirrored exactly in the shader. In pane-normalised coordinates `q` in
[-1, 1]², with `k = amount / 400` and `r² = q·q`, the screen point `q` shows
the content at:

    s = q · (1 + k·r²) / (1 + k)

What this means:
- The centre is unchanged.
- The middle of each edge maps exactly to the edge, so the content still fills
  the pane.
- Content bulges outward, more toward the corners.
- Where `s` falls outside the pane, the pixel is black. So each edge becomes
  an arc that touches the pane's edge only at its midpoint, and the black is
  widest at the corners. This is the shape of a tube.
- At 100, a corner point (`r² = 2`) shows content from 1.2 times its distance
  from the centre. Along the diagonal, the outer 11% or so of the
  half-diagonal is black.

The panel shows the amount as a percentage.

**Glow.** Its source is a small blurred copy of each glowing pane:
- Each glowing pane has its own half- and quarter-size textures, so glow
  never reads another pane.
- The copy is made by repeated halving, to half size and then to a quarter. Each step is one linear-filtered draw at exactly half
  size, so every output texel is a true 2×2 box of its source. A single 4×
  step would sample only some of the source pixels, and a thin glyph stroke
  could fall between samples and get no glow.
- The copy is rebuilt only when the scene changed this frame, and only for
  panes with glow above zero.

The shader softens the quarter-size copy with nine taps (1-2-1 weights), so
glow spreads about 8 px around bright text, not 2. It adds `intensity / 100
× G` times that blurred colour at the same warped coordinate, then clamps to
white. The taps run only on glowing panes. G is a gain fixed in the registry.
The result is a soft, slightly blocky bloom. A separable Gaussian is a later
option, if the owner wants it smoother.

**The present pass.**
- If no pane on an output has a presentation effect, `blit_gles_scene_draw`
  runs exactly as today: one quad, no new cost.
- Otherwise, panes without presentation effects keep the plain copy. Each
  pane with them is drawn with the new `present_program`, over its own pixel
  region.
- `blit_gles.c` owns the second program. `gles_flush` and the batch state
  handle the switch between programs.
- The shader does the following:
  1. Normalises within the pane rectangle.
  2. Applies the warp.
  3. Paints black outside [0,1]².
  4. Otherwise samples the scene inside that pane's own rectangle, so it never
     reads another pane. The scene's rows are bottom-up, and the lookup flips
     them.
  5. Applies the pane's scanlines, if it has curvature (see below).
  6. Adds glow.

**Filtering.** The scene texture is `GL_NEAREST`, which is right for today's
1:1 copy. Under curvature the mapping is no longer 1:1, and nearest sampling
gives jagged glyph edges. So a curved pane is sampled with `GL_LINEAR`. The
glow downsample draws are linear too. GLES 2 has no sampler objects, so the
scene texture's filter is set with `glTexParameteri` before those draws and
put back to `GL_NEAREST` before the plain copy.

**What the warp carries, and where scanlines go.** Corners are in the scene,
so they bend with the content: the corner arc follows the curve.

Scanlines cannot stay in the scene for a curved pane. They have a 2 px
period:
- Nearest sampling through a warp would double or skip rows, giving moiré
  bands.
- Linear sampling would average them to a flat grey, and the effect would
  vanish.

So there is one rule, decided by one predicate: **if curvature is drawn for
a pane, that pane's scanlines are drawn by the present shader in screen
space**, using the registry's `blit_effect_scanline_alpha` on the screen row
counted from the pane's top. The scene skips them for that pane. Otherwise,
including on the CPU compositor where curvature is unavailable, scanlines
stay in the scene, exactly as in ADR 0186.

This is the order cool-retro-term uses: the scanlines sit on the glass, not
on the picture. The predicate is the same one that decides whether curvature
is drawn, so the two can never both draw scanlines or both skip them.

The pointer and API overlays are drawn after the present pass, in screen
space, unwarped. Screenshots read back after the present pass, so they show
the effects.

**Pointer input follows the warp.** A click must reach the content the user
sees under the pointer. Where `blit_server.c` turns a display pixel into a
cell, the pixel is first mapped through the same registry warp, in that
pane's normalised space, before the division by the cell size. Only the
forward warp is needed, never its inverse. A click in the black margin
clamps to the nearest edge cell, as the existing inset rule already does.

**Precision.** The warp needs `highp` floats in the fragment shader.
Curvature at 2560 px in `mediump` (10-bit mantissa) is visibly wrong.
`GL_FRAGMENT_PRECISION_HIGH` is visible only inside a shader, so Blit checks
at start with `glGetShaderPrecisionFormat(GL_FRAGMENT_SHADER, GL_HIGH_FLOAT)`.
A zero range or precision means no `highp`. Presentation effects are then
unavailable on that output, with the reason `no highp fragment precision`.

**Where it is unavailable, it is said, not imitated.** On a CPU-composed
output, or without `highp`, presentation effects are not drawn. The saved
values are kept, and `GET /v1/effects` reports them under each pane's
`unavailable`, for example `{"curvature": "cpu compositor"}`. Local effects
still draw there. This keeps ADR 0186's rule that entries are kept, not
dropped.

**Switches bypass, they do not dim.** The owner asked for effects to be
switchable, with no cost while off:
- `document.effects.enabled` is a master switch, and each pane's entry may
  carry `"enabled": false`. Both default to on.
- Off keeps the values, so on brings them back exactly.
- Off is decided in one place: the server's per-pane effect set is empty for
  a switched-off pane. The compositors then take exactly the path of a pane
  that never had effects: no local pass, and no presentation pass. With every
  pane off, the frame is today's plain copy.
- `GET /v1/effects` reports both switches, and an empty set for a
  switched-off pane.

**Cost is bounded and visible.**
- Budget: the present pass, glow rebuild included, at most 2 ms of GPU time
  at 2560x1440 on the owner's Radeon. Evidence: the existing per-output
  `gpu_last_ms` and `gpu_max_ms`.
- Each output's `effects` diagnostics gain `present_quads` and
  `glow_rebuilds`.

**Out of scope.**
- Chromatic shift and burn-in come later on the same pass. Burn-in also needs
  a persistent per-pane history texture.
- Animated effects (flicker, noise) need a frame clock: off by default, and
  rate-limited. They get their own ADR.

## Consequences

- The common case, with no presentation effects, has identical code and cost
  to today.
- The first effects that work on the GPU only. The API says so per pane, and
  a settings document carries them across compositors unchanged.
- Pointer input gains one pure function call per event on panes with
  curvature.
- **Deployment.** This is more per-frame GPU work than anything shipped so
  far. It is not deployed while #146 (the 21→22 px stall on the real GPU) is
  being reproduced, so a stall can be attributed to one change or the other.
- **Control Panel.** The Blit Effects pane applies changes live: a slider
  when it is let go, a switch at once. It writes the server's current
  settings with only `effects` replaced, so nothing another pane has staged
  is saved with it. Its sliders come from `GET /v1/effects`'s list, so
  Curvature and Glow appear with this release. On a pane where they are
  unavailable, it shows the reason.

## Evidence

To be met before this ADR is accepted. `tools/tests/pane_effects_gate.rb`
gains these cases, on `gles`:
- **Warp:** a pane painted with alternating 8 px bands. The readback must
  match the shared C warp: within one band edge at the boundaries, exact
  elsewhere, and black outside the tube.
- **Scanlines under curvature:** a pane with curvature and scanlines over a
  flat colour. In every 16-row window, close to half the rows must be dark,
  so there is no moiré and no flattening. The scene for that pane must hold
  no scanline darkening, so they are drawn exactly once.
- **Pointer:** motion injected at the screen position of a known cell's
  warped centre must report that cell. One injection in the black margin must
  report the nearest edge cell.
- **Glow:** one bright cell on black. Its neighbours must be brighter than
  without glow, a far pixel unchanged, and a pixel across the pane boundary
  unchanged.
- **Isolation:** on a 1x2 split, effects on pane 1 and none on pane 2. Pane
  2's pixels must be byte-identical to a run with no effects.
- **Unchanged path:** with no presentation effects, the frame's draw count
  matches today's.
- **CPU:** a saved curvature on a CPU-composed output persists, and
  `/v1/effects` reports it unavailable.
- **Cost:** the GPU milliseconds are reported. The budget is checked on the
  owner's GPU, not llvmpipe.

### Measured on the owner's Radeon (0.2.29, 2026-10-26)

DP-1 at 1920×2160, split 1×2. Both panes have curvature, glow, scanlines and
corners. GPU time per frame, averaged over 10 s each, A/B with the master
switch:

| State | GPU per frame | Present panes | Glow rebuilds |
|---|---|---|---|
| Effects on | 2.31 ms | 2 | 207 in 274 frames |
| Effects off | 0.49 ms | 0 | 0 |
| On again | 2.23 ms | 2 | 152 in 371 frames |

- The presentation effects cost about 1.8 ms per frame, glow rebuilds
  included. That is within the 2 ms budget, on more pixels than the
  2560×1440 it was stated for.
- Switched off, the frame costs what a frame with no effects costs. So the
  switches bypass the effects, they don't dim them.
- Glow rebuilds track the panes' own changes: the clock, htop and the
  cursor. They no longer follow changes elsewhere on the output (0.2.29).

