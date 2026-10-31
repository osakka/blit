# ADR 0190: Flicker, horizontal sync and jitter: per-frame randomness from C, on the frame clock

- Status: accepted
- Date: 2026-10-26
- Scope: effects registry, presentation shader, frame clock
- Builds on: ADR 0187 (presentation effects), ADR 0188 (animated effects)

## Context

The owner asked for cool-retro-term's moving effects, "the real ones". Its
RGB shift is static: what moves the fringes is the picture under them. Read
from its shaders (`terminal_dynamic.vert` and `.frag`):

- **Noise:** once per frame, the vertex shader reads one texel of a noise
  texture at `(fract(t / 2.048), fract(t / 1048.576))`, giving two random
  values, r and g, for the whole frame.
- **Flicker:** `brightness = 1 + (g − 0.5) × flickering`, multiplied into
  the final colour. The whole screen brightens or dims a little, at random,
  every frame.
- **Horizontal sync:**
  - `strength = mix(0.05, 0.35, hsync)` and `randval = strength − r`.
  - `scale = max(randval, 0) × strength × hsync` and
    `freq = mix(4, 40, g)`.
  - Then `x += sin((y + t) × freq) × scale`, in normalised coordinates.
  - On a random share of frames (r < strength: 5–35%), the picture sways
    sideways in a sine wave of random frequency; on the rest it is still.
- **Jitter:**
  - Every pixel's sample point moves by
    `(noise.ba − 0.5) × jitterDisplacement × j`, where
    `jitterDisplacement = (0.007 j, 0.002 j)` (QML). So it scales with
    **j²**, at most ±0.35% of the width (±6.7 px on 1920) and ±0.1% of the
    height.
  - The noise is a 512² texture, repeated and bilinear-filtered
    (`smooth: true`), sampled at `scaleNoiseSize × coords + time offset`.
    One texel spans about `1.33 × windowScaling × fontScaling` pixels, so
    the displacement is spatially smooth over a couple of pixels and
    re-randomised every frame. It is not per-pixel static.
- **cool-retro-term's defaults:** jitter 0.2, horizontal sync 0.08,
  flicker 0.1, glowing line 0.2.

All three change every frame, so they need ADR 0188's frame clock.

## Decision

**Three registry rows:** presentation class, animated, each with one 0..100
parameter mapped as cool-retro-term maps its 0..1 slider:
- `flicker {intensity}`: flickering = intensity / 100.
- `horizontal_sync {amount}`: hsync = amount / 100. The sway's largest
  amplitude is `strength² × hsync`: 12% of the pane's width at 100 (about
  235 px on 1920), and 0.15% at cool-retro-term's default 8.
- `jitter {amount}`: j = amount / 100, displacing by j² as cool-retro-term
  does. At amount 20, its default, that is about 0.3 px: subtle by design.

**The randomness is computed in C, once per frame,** from a hash of the
output's presented-frame counter, never from the clock, as ADR 0188
requires. The mix lives in `blit_effects.h`, so a gate's C check and the
shader's inputs agree. cool-retro-term's own r and g come from walking one
row of its noise texture, looping every 2.048 s; a frame hash stands in for
that. That
gives the two values r and g for the frame. From them, C computes what the
shader needs:
- the flicker factor;
- the sync wave's `scale` and `freq`, and its phase `fmod(t × freq, 2π)`, so
  no large time value reaches a float;
- a jitter seed.

So:
- the shader adds no per-frame noise lookup;
- flicker and sync are one multiply and one `sin` per pixel;
- the effects are deterministic for a given frame number, so gates can check
  them.

**Jitter's noise is value noise in the shader.** A hash gives two lanes at
the corners of a grid whose spacing is `cell_width / 6` pixels (cool-
retro-term's 1.33 × font scale, with 8 px as its base cell), and is
bilinearly interpolated between them. That is four hashes per pixel, only in
jittered panes. The hash is `fract(sin(dot(cell, k) + seed) × 43758.5453)`,
with `seed` kept small (a fraction) so that `sin` keeps its precision. Jitter's displacement is fractional, so a jittered pane
is sampled with linear filtering, as curvature and RGB shift are.

**Order in the presentation pass,** after the curvature warp gives the
content position:
1. Horizontal sync moves it.
2. Jitter moves it.
3. The scene is sampled there, with RGB shift.
4. Glow is added, sampled at the same displaced position, so the bloom
   follows the picture.
5. The glowing line is added.
6. Flicker multiplies.
7. Glass scanlines.

This is cool-retro-term's order: its dynamic pass distorts and brightens,
then its static pass curves and shifts colour. Every sample stays clamped to
the pane's own rectangle.

**What does not move:**
- **Pointer input** follows curvature only. Sync and jitter are transient,
  and a click aimed at moving text should reach where the text rests.
- **Corners follow the curvature, not the sync.** Corners are in the scene,
  so sampling a displaced position would slide them sideways during a sync
  event, by up to 12% of the width. In cool-retro-term the frame belongs to
  the static pass and does not move. One predicate, as for scanlines under
  curvature: when a pane has horizontal sync or jitter, its corners are not
  drawn in the scene. The presentation pass applies them, using the shared
  coverage rule at the *undisplaced* warped position, so they bend with the
  tube and stay still during the sway. The rule is evaluated only inside
  the corner squares.

**Cost:** these three add a hash and a `sin` per pixel in panes that have
them, and frames at the clock's rate. Off, or switched off, they cost
nothing, as ADR 0188 already guarantees.

## Consequences

- A pane with any of the three presents at the clock's rate (30 Hz by
  default) for as long as they are on.
- Flicker at 30 Hz is slower than cool-retro-term's per-refresh flicker.
  The rate setting can raise it only to 30, until a higher ceiling is
  measured.
- Burn-in and static noise remain unbuilt. Static noise would reuse jitter's
  hash; burn-in needs per-pane history (ADR 0187).

## Evidence (to be met before acceptance)

`presentation_effects_gate`, on gles:
- **Flicker:** over a dark flat pane (no clipping at ±50%), screenshots
  spaced beyond one 33 ms frame differ in brightness, by one factor across
  the whole pane, within `1 ± intensity/200`.
- **Horizontal sync:** over vertical stripes at amount 100, some frames
  displace rows sinusoidally, up to the stated amplitude, with the clamp
  smearing at the pane's edge. Frames without an event are the plain
  picture. The corners do not move.
- **Jitter:** over stripes, edge pixels change between frames while the
  stripes' mean colour holds, and no sample leaves the pane.
- **Free when off:** as ADR 0188.
- **Determinism:** the C hash gives the same r and g for the same frame
  number. A unit check covers it.

## Implementation (0.2.32)

- **Registry:**
  - the rows `flicker`, `horizontal_sync` and `jitter`;
  - `blit_effect_frame_noise`: splitmix64 of the composed-frame count, three
    lanes;
  - `blit_effect_flicker`, `blit_effect_horizontal_sync` (scale, frequency,
    and phase reduced mod 2π) and `blit_effect_jitter` (j²);
  - `blit_effect_corners_presented`.
- **Shader:**
  - sync: one `sin`;
  - jitter: value noise, four hashes at a `cell_width / 6` px grid,
    bilinear;
  - flicker: one multiply;
  - corners: the shared 8×8 coverage rule at the undisplaced position,
    evaluated only in the corner squares.
- **Filtering:** linear when a pane is curved, shifted, swaying or
  jittered.

Measured evidence on the owner's GPU is still to come; the gates above pass
on gles, and on native the effects are reported unavailable.

