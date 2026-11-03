# ADR 0208: Burn-in keeps a per-pane history that fades in whole levels, on the frame clock only while it fades

- Status: accepted
- Date: 2026-11-03
- Scope: effects registry, GLES compositor (a history texture per burning
  pane, the presentation shader's sampling), KMS present path, frame clock,
  effects API, OpenAPI and diagnostics
- Builds on: ADR 0187 (presentation effects; its "Out of scope" named
  burn-in and its per-pane history), ADR 0188 (frame clock), ADR 0204
  (zoomed displays), ADR 0207 (the registry's shape)

## Context

The owner wants cool-retro-term's burn-in: a phosphor's persistence. What
was lit fades out over time instead of vanishing, so moving or changing
text leaves a decaying trail.

cool-retro-term keeps a burn texture. Each frame it takes the previous
one, makes it darker by an amount proportional to the time since the last
update, and keeps the brighter of that and the new picture. The fade is
linear: its slider (0..1) sets a fade time from about 0.16 s to 1.6 s. It
also keeps the previous frame's luminance in the texture's alpha, and
fades less where the picture was lit. That approximates something exact:
light still on the screen has not started to fade yet.

Four things in Blit shape the design:
- **The present pass** (ADR 0187) already draws each pane with
  presentation effects from the scene, per pane, in logical coordinates
  (ADR 0204). A history is one more texture the pass can read.
- **Blit presents only on change** (ADR 0188). A fade is change without
  content, so it needs the frame clock, and an idle screen must stay free.
- **The textures are 8 bits a channel.** A fade multiplied each frame
  rounds back up: 1/255 × 0.98 is 1/255 again, and a trail never reaches
  black.
- **GLES 2 cannot read the texture it draws into.** Updating a history in
  place is either a blend, or a second texture to ping-pong.

## Options considered

**The fade.**
1. **Exponential, `history × decay(dt)`.** It has to be floored by hand in
   8 bits, or it sticks short of black. Its end is `τ ln 255`, a long dim
   tail that keeps the clock running. Rejected.
2. **Linear, in whole levels: `history − levels / 255`, levels carried
   exactly.** This is cool-retro-term's own shape. Whole levels subtract
   exactly in 8 bits, and black is reached at a stated time. Chosen.

**Updating the history.**
1. **A burn program and two textures per pane, ping-ponged.** Plain
   GLES 2, but twice the memory and a third program. Rejected.
2. **Blending into one texture.** A reverse-subtract blend (`dst − src`,
   GLES 2) with a grey of `levels / 255` fades it, clamped at 0 by the
   unorm target. A max blend (`GL_MAX_EXT`, `GL_EXT_blend_minmax`) with
   the scene's rectangle keeps the brighter. Both use the existing compose
   program. Chosen. `GL_EXT_blend_minmax` is exposed by every Mesa driver.
   Where it is missing, burn-in alone is reported unavailable (`no blend
   minmax`) and the other presentation effects are unaffected.

**When the history is updated.**
1. **After the scene is recomposed: `max(new picture, history − dt)`.**
   The picture just replaced was lit until this frame, but in this order
   it has already lost the time since the previous frame. After a hold,
   that is the whole hold: a block held for ten seconds would vanish
   instead of fading. Rejected.
2. **Before the scene is recomposed: `max(history − levels, the picture
   shown until now)`.** This is exact: what the pane showed fades from
   full at the frame it changed. cool-retro-term's alpha trick
   approximates the same rule. Chosen.

**What the pass reads.**
1. **The history alone**, which already includes the scene. A missed
   update would freeze stale content into the pane. Rejected.
2. **The brighter of the scene and the history**, at every sample (the
   centre and RGB shift's taps). The current picture always shows, and a
   history can only add a trail. Chosen.

**The frame clock.**
1. **Mark the row `animated`.** The clock would run forever on any pane
   with burn-in. Rejected.
2. **Not animated. The compositor reports per output whether its last
   frame left light to fade, and the clock runs only while one has.**
   Chosen.

## Decision

**A registry row,** presentation class, after contrast:

| Key | Parameter | Range | Title | Animated |
|---|---|---|---|---|
| `burn_in` | `amount` | 0..100 | Burn-in | no |

**The rule, once, in `blit_effects.h`.**
- `blit_effect_burn_in_us`: the fade time, `160 ms + 1440 ms × amount /
  100` (1.6 s at 100). 0 is off.
- `blit_effect_burn_in_levels(fade_us, elapsed_us, &carry)`: the whole
  levels of 1/255 to subtract for `elapsed_us`. The rest of
  `elapsed × 255` is carried, so over any run of frames the levels add up
  to `elapsed × 255 / fade`, rounded down or one more, at any frame rate.
  A gap of a whole fade time or more is 255.
- A pixel's trail is `v − levels since it stopped being shown`, down to 0.
  It is exactly black one fade time after the change.

**The history** (`blit_gles.c`): one RGBA8 texture per burning pane,
`width × height` of its logical rectangle in the scene, rows bottom-up like
glow's copy. Each scene holds up to 8. Each history keeps the caller's
book-keeping with it: the time faded to, the carry, and the levels faded
since the pane last changed.

**Each presented frame** (`kms_gles_compose`), for each burning pane:
1. **Before the scene is recomposed:** the history fades to now. It loses
   `blit_effect_burn_in_levels` levels (reverse-subtract), then keeps the
   brighter of itself and the scene as it still is (max). Once 255 levels
   have faded since the pane last changed, the history is the scene, so
   this step draws nothing and only advances its time.
2. **The scene is recomposed** as before.
3. **In the present list:**
   - A pane without a history gets one as a plain copy of the new scene,
     with nothing to fade.
   - A pane redrawn this frame (the same `changed` test glow uses) restarts
     its count at 0.
4. **The present shader** samples `max(scene, history)` wherever it reads
   the picture. The history's filter follows the scene's: nearest at 1:1,
   linear under a warp, shift, sync or jitter.
5. **After the pass,** every history no pane claimed this frame is freed.

**Order in the pass.** Burn-in is in the sampling, before every other
step: it is the picture. Curvature, RGB shift, sync and jitter move the
trail with the content. The hum bar, noise, flicker and the tube's colour
light it and colour it. Corners and glass scanlines cover it. This is
cool-retro-term's order: burn-in on the content, the tube after. Glow is
built from the scene, as before, so the trail does not bloom. Building it
from the history would rebuild glow on every frame of a fade.

**The frame clock.** The compositor reports per output
(`blit_kms_burn_in_fading`) whether any history has not yet faded 255
levels since its pane changed. `output_animated` treats such an output as
animated, under ADR 0188's other conditions: presenting, VT held, ticks
only filling gaps. The last tick falls at most one fade time plus one
period after the last change. Then the clock stops, and an idle screen
presents nothing.

**Resets.**
- **A changed rectangle** (layout, resize, zoom, mode, pane moved): the old
  rectangle's history is unclaimed and freed. The new one starts as a copy
  of the new scene, so nothing from the old layout or another pane can
  fade into it.
- **A zoom change** closes the scene, and its histories with it.
- **The same rectangle showing other content** (a pane swap with equal
  rectangles, a session replaced) fades what was there, as glass would.

**Off.** With burn-in unset:
- no history texture exists;
- no fade draw is made;
- the shader's `burn` uniform is 0 and its sampling arithmetic is
  unchanged;
- a pane with no other presentation effect is not in the present list at
  all.

Turned off, a history is freed on the next frame of that output.

**Zoomed displays** (ADR 0204): the history is logical and is read at the
logical pixel the zoom shows. The trail is the 100% trail, in 2×2 blocks
at 200%.

**The CPU compositor** keeps the value, reports `burn_in` unavailable
(`cpu compositor`), draws nothing and gets no ticks.

**Diagnostics** (each output's `effects`):
- `burn_in_panes`: histories held;
- `burn_in_faded`: the levels the freshest trail has faded since its
  pane's change, 255 when nothing fades;
- `animation_hz`: non-zero while a trail fades.

## Consequences

- **GPU memory:** 4 bytes per logical pixel per burning pane, one texture,
  only while the effect is on. A 1920×1080 pane is 8.3 MB; at 200% zoom, a
  quarter of that.
- **Cost while fading:** two blended full-pane quads into the history per
  frame (subtract, max), and one more texture read per sample in the
  pass, two more with RGB shift. Frames come from the clock at up to
  30 Hz.
- **Cost once faded:** no history draw, but still the extra reads in the
  pass. To be measured on the owner's Radeon.
- **A frequently changing pane keeps the clock busy.** A clock in the top
  bar, or htop, changing every second under burn-in 100 keeps the frame
  clock running, since each change starts a 1.6 s fade. That is the effect
  working: a fade needs frames. With nothing changing, nothing runs.
- **A full redraw** (everything drawn) counts as a change of every pane. It
  buys one fade time of ticks, even when nothing visible fades. This is
  bounded.
- **API:** `burn_in` in `EffectSet` and in `available` (`animated:
  false`). `unavailable` may say `no blend minmax`, and the diagnostics
  gain two fields. All are additive.
- **Control Panel** (not changed here; `~/hibr-apps`): its sliders come
  from `available`, so a "Burn-in" 0..100 slider appears unchanged. It
  could show the fade time beside the slider, 0.16 s + 1.44 s ×
  amount/100, as `animated: false` would otherwise suggest a static
  effect. It should also show `no blend minmax` like the other reasons.

## Evidence

`tools/tests/burn_in_gate.rb`, in the renderer suite. On one 640×384
display split 1x2, a white 10×3-cell block moves between two places in one
write.

**gles:**
- **Switched on:** the pane is byte-identical to the plain capture, the
  pass and one history exist, and an idle screen presents at most 2 frames
  in 1.5 s.
- **The fade**, after the block was held for seconds. Each capture shows
  the old place as exactly one grey, non-increasing. Its fade is:
  - the C rule (compiled into a probe) for the time since the input,
    within the latency to the screen;
  - and the `burn_in_faded` the diagnostics report on either side of the
    capture, within 1;
  - over 46 to 95 captures per run, 30 to 68 of them mid-grey.
- **The end:** black after 1.6 s, the pane byte-identical to the plain
  capture, `animation_hz` 0 and no frames when idle.
- **Uncaptured fade:** 29 to 35 animation frames over 1.6 s (the floor is a
  third of 30 Hz), then none.
- **A trail laid during a fade,** at a 10 Hz clock, three times: it starts
  from full, matching the reported level within 1.
- **Amount 30:** a trail a third of the way in, gone by 0.592 s.
- **Layout 1x2 → 2x1 mid-fade:** no grey in the new pane, and nothing
  fading.
- **200%:** every 2×2 block is one colour, and the trail is exactly the
  100% trail's pixels enlarged.
- **Off:** no pass, no history, no trail.
- **Isolation:** pane 2 is byte-identical.

**native:** kept, reported unavailable, undrawn, no frames.

The gates that list the registry (`presentation_effects_gate`,
`terminal_conformance`) carry the new row.

**Against the previous build (0.3.32, ff63e9a):**
- `burn_in_gate` fails on both backends at the registry row.
- Three mutants of this build fail on its pixel and clock checks:
  - the shader not reading the history: "no gradual fade";
  - the clock not run while fading: "the frame clock did not run while
    fading";
  - the fade done after the scene is recomposed (option 1 above): "a trail
    laid during a fade faded 17..22, reported 0..4".
- `zoomed_effects_gate`'s 100% capture with curvature, RGB shift,
  scanlines and corners (`BLIT_ZOOMED_EFFECTS_SHOT`), which reads the
  picture through the shader's new `pick`, is byte-identical between
  0.3.32 and this change.

The renderer suite on gles passes (75 passed, 1 skipped for want of QEMU).

Measured on the owner's mini PC (Radeon, 0.3.33, 2026-11-03): the DP-1
pane holding the Hibr desktop, already drawing curvature, glow, RGB shift,
scanlines, corners, the glowing line, flicker, horizontal sync and jitter
(so its frame clock runs at 30 Hz regardless); `gpu_total_us / gpu_frames`
over 15 s each, ordinary desktop use with a terminal updating, the owner's
settings restored after:

| Burn-in | GPU per frame | Frames in 15 s | `burn_in_panes` |
| --- | --- | --- | --- |
| off | 1.782 ms | 493 | 0 |
| 50 | 2.242 ms | 496 | 1 |
| 100 | 2.298 ms | 496 | 1 |
| off again | 1.747 ms | 497 | 0 |

Burn-in costs about 0.5 ms of GPU time per frame on this pane (a 1920x1080
history), and adds no frames where the clock already runs. The pass stays
near ADR 0187's 2 ms budget. A pane without other animated effects, burning
idle, is still to be measured.

A first attempt measured nothing: the effects saved for DP-1 pane 1 apply
to no view, because the desktop is one session in pane 2. `GET /v1/effects`
does not say so (follow-up).
