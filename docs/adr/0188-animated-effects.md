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

**A tick re-presents; it never recomposes.** It does not force a held
scene; it only makes its outputs due and changed:
- **Changed:** in the KMS present path, a new `animation_output_mask` marks
  such an output as changed for that frame, next to the screenshot and
  pointer masks. The rule "an unchanged output is not presented" still holds
  for everything else.
- **Nothing is redrawn:** with no view changed, GLES composition redraws no
  view, and (since 0.2.29) rebuilds no glow copy. Only the scene copy and
  the presentation pass run, with the new phase. A frame that recomposes
  anyway carries the new phase too.
- **Quiet windows:** inside a terminal quiet window or a Hibr frame barrier,
  the main loop limits presents to the outputs the pointer dirtied
  (`output_mask &= pointer_dirty_outputs`). Outputs due for animation are
  OR'd into that filter, so the band keeps moving. The frame is then a held
  scene, as for the pointer.
- **Pacing:** an output that is not yet initialised is skipped, and a flip
  still pending waits, as for the pointer.

**The clock exists only while it is needed.**
- An output's animation is due only while all of these hold:
  - an animated effect is on, and switched on, in at least one of its panes;
  - its compositor draws presentation effects;
  - the output is presenting (powered, not blanked).
- **Rate:** 30 Hz by default (`document.server.effects_animation_hz`,
  10–30 in this release). At 30 Hz the band moves about 6 px per frame on a
  soft ramp. A higher ceiling waits for a measurement on the owner's GPU.
- With no due output there is no tick and no present, so an idle screen
  stays free.
- **The tick owns its own cadence.** After any present,
  `advance_render_schedule` makes an output due again one refresh later, and
  `render_work_remains` would keep `render_pending` set while anything is
  outstanding. So animation is not "work": `render_pending` stays 0 between
  ticks. The frame clock alone raises it, and sets the output's due time, at
  its own rate. Otherwise the loop would present every refresh: 60 Hz, not
  30. The present itself still goes through the render schedule and frame
  timing, so it is aligned to the vblank and coalesces with real changes.
- **No new timeout.** With nothing pending, the main loop already wakes
  every 10 ms (`render_wait_ms`). The clock is checked on each pass, so a
  tick lands within 10 ms of its time, on a 33 ms period.
- **"Presenting" is three facts the runtime already has:**
  - the display's DPMS `power_state` is on, or was never read;
  - its KMS `presentation_state` is `BLIT_KMS_PRESENTATION_PRESENTING`;
  - Blit holds the VT (`blit_vt_active`).
  A display that is off, failed, recovering or switched away gets no ticks.

**It is unavailable where presentation effects are:** on the CPU
compositor, and without `highp`. It is kept and reported per pane, as in
ADR 0187.

**Cost is visible.** Each output's `effects` diagnostics gain:
- `animation_hz`: 0 when the clock is off;
- `animation_frames`: the presents made only for animation;
- `animation_phase`: the phase of the last present, which also makes the
  band's position checkable.

## Consequences

- The first source of frames that is not a change. It is bounded by the
  rate, and off whenever no animated effect is on.
- A glowing line on an otherwise idle screen costs the presentation pass,
  30 times a second. ADR 0187 measured about 1.8 ms for both panes on the
  owner's Radeon, so roughly 5% of the GPU at 30 Hz. To be measured.
- The band does not bloom. In cool-retro-term the glowing line is added
  before bloom, so it glows. Here glow is sampled from the scene's blurred
  copy and the band is added after it. That is deliberate: it is what keeps
  a tick free of glow rebuilds. It is not a bug to "fix".
- Later animated effects (flicker, static noise, horizontal sync) use the
  same clock and phase. Flicker and noise also need a per-frame random seed,
  derived from the frame count in C, never from the clock.

## Evidence (to be met before acceptance)

`presentation_effects_gate` gains these cases, on gles:
- **The band's shape:** in one capture of a flat pane with a glowing line,
  the rows brighter than the flat colour form one contiguous run, about a
  third of the pane tall. Brightness rises monotonically toward the run's
  lower (leading) edge and is zero below it. The run's position matches the
  `animation_phase` the diagnostics report for that frame.
- **Direction:** a second capture a moment later has the run lower down,
  wrapping only past the bottom.
- **A tick only re-presents:** during an idle animation, `gpu_frames` and
  `animation_frames` rise at the configured rate, while `drawn_cells` and
  `glow_rebuilds` do not move.
- **No clock when off:** with the line at 0, switched off, or the pane
  switched off, `gpu_frames` stops rising on an idle screen.
- **CPU:** a saved glowing line on a CPU-composed output is reported
  unavailable, and the output presents no ticks.

Plus a measurement on the owner's Radeon of the GPU share at 30 Hz.
