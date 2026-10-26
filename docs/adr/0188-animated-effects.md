# ADR 0188: Animated effects re-present a held scene on a frame clock that exists only while one is on

- Status: proposed
- Date: 2026-10-26
- Scope: effects registry, main loop render scheduling, KMS present path,
  GLES presentation pass, diagnostics
- Builds on: ADR 0187 (presentation effects)

## Context

The owner wants cool-retro-term's "glowing line": a brightening that travels
down the screen. Its shader (`terminal_dynamic.frag`, `randomPass`) is:

    fract(smoothstep(-120, 0, y - (H + 120) * fract(time * 0.15)))

in cool-retro-term's virtual pixels. Read precisely:
- It is a broad, soft band, not a line. It ramps from nothing up to its
  brightest at the leading edge, then drops to zero just below it
  (`fract(1.0) = 0`).
- 120 virtual pixels is a third to a half of cool-retro-term's low virtual
  height.
- It sweeps the height plus its own length every 1 / 0.15 ≈ 6.7 s.
- It is added as white light, at most +0.2 at full strength (`glowingLine
  × 0.2`), on the flat picture, before curvature, so it bends with the tube.

It is the first effect that changes with time. Every effect so far changes
a frame only when the screen changes. Blit presents an output only when
something on it changed: "page flips are not a heartbeat" (`blit_kms.c`).
An idle screen costs nothing, and the owner asked that effects add no delay
or cost when they are off.

## Options considered

1. **Recompose the scene on a timer.** Redraws every view each tick and
   rebuilds glow. That is wasteful: nothing in the scene changed.
2. **Keep presenting at the display's refresh while the effect is on.**
   Simple, but 60 Hz of full presents for a band that moves about 3 px per
   frame.
3. **A frame clock that requests held-scene presents at a bounded rate, and
   exists only while an animated effect is on a powered output.** Chosen.

## Decision

**The registry gains animation.** Each spec says whether its effect
animates. `glowing_line: {intensity: 0..100}` is the first, a presentation
effect, with the title "Glowing line". The band and its timing are defined
once in `blit_effects.h`:
- **Band length:** a third of the pane's height.
- **Period:** 1 / 0.15 s, sweeping the pane's height plus one band length.
- **Shape:** brightness rising linearly toward the leading edge, zero below
  it.
- **Strength:** `intensity / 100 × 0.2`, added to each channel and clamped.

The band is placed in content space, at the warped coordinate, so it bends
with the curvature. It is added after glow and before the glass scanlines.

**The phase is computed in C.** The frame clock takes the monotonic time and
computes `fract(seconds × 0.15)` in double precision. Only that 0..1 phase
reaches the shader, as a uniform. A raw time in a float loses precision
after hours of uptime.

**A tick re-presents; it never recomposes.**
- An output with an animated pane that is due is presented with the scene
  held: no view is redrawn and no glow copy is rebuilt.
- Only the scene copy and the presentation pass run, with the new phase.
- In the KMS present path, a new `animation_output_mask` marks such an output
  as changed for that frame, next to the screenshot and pointer masks. The
  rule "an unchanged output is not presented" still holds for everything
  else.
- The pass takes the existing `hold_scene` route: an output that is not yet
  initialised is skipped, and a flip still pending waits, as for the pointer.

**The clock exists only while it is needed.**
- An output's animation is due only while all of these hold:
  - an animated effect is on, and switched on, in at least one of its panes;
  - its compositor draws presentation effects;
  - the output is presenting (powered, not blanked).
- **Rate:** 30 Hz by default (`document.server.effects_animation_hz`,
  10–60). At 30 Hz the band moves about 6 px per frame on a soft ramp. 60 is
  offered only after it is measured on the owner's GPU.
- With no due output there is no tick, no timeout and no present, so an idle
  screen stays free. The main loop's sleep is bounded by the next tick only
  while one is due.
- A tick goes through the existing render schedule (`render_pending`, each
  output's due time and frame timing), so it never presents faster than the
  display, and it coalesces with real changes. A frame that recomposes
  anyway carries the new phase.

**It is unavailable where presentation effects are:** on the CPU
compositor, and without `highp`. It is kept and reported per pane, as in
ADR 0187.

**Cost is visible.** Each output's `effects` diagnostics gain
`animation_hz` (0 when the clock is off) and `animation_frames`, the
presents made only for animation.

## Consequences

- The first source of frames that is not a change. It is bounded by the
  rate, and off whenever no animated effect is on.
- A glowing line on an otherwise idle screen costs the presentation pass,
  30 times a second. ADR 0187 measured about 1.8 ms for both panes on the
  owner's Radeon, so roughly 5% of the GPU at 30 Hz. To be measured.
- Later animated effects (flicker, static noise, horizontal sync) use the
  same clock and phase. Flicker and noise also need a per-frame random seed,
  derived from the frame count in C, never from the clock.

## Evidence (to be met before acceptance)

`presentation_effects_gate` gains these cases, on gles:
- **The band:** with a glowing line on a flat pane, two captures at known
  phases show the band where the registry puts it. Rows behind the leading
  edge are brightened by the shared ramp, and rows ahead of it are not.
- **A tick only re-presents:** during an idle animation, `gpu_frames` and
  `animation_frames` rise at the configured rate, while `drawn_cells` and
  `glow_rebuilds` do not move.
- **No clock when off:** with the line at 0, switched off, or the pane
  switched off, `gpu_frames` stops rising on an idle screen.
- **CPU:** a saved glowing line on a CPU-composed output is reported
  unavailable, and the output presents no ticks.

Plus a measurement on the owner's Radeon of the GPU share at 30 Hz.
