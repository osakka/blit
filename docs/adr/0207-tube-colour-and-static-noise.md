# ADR 0207: The tube's colour controls and static noise are presentation effects

- Status: accepted
- Date: 2026-11-03
- Scope: effects registry, presentation shader, settings parser, effects
  API and OpenAPI
- Builds on: ADR 0187 (presentation effects), ADR 0188 (animated effects),
  ADR 0190 (per-frame randomness), ADR 0204 (zoomed displays draw them)

## Context

The owner wants the rest of cool-retro-term's tube controls:
- **Static noise:** a fine grain over the picture that changes every frame.
  cool-retro-term adds light from a scrolled noise texture, scaled by its
  `staticNoise` slider (default 0.12).
- **Chroma colour:** how much of the picture's own colour survives.
  cool-retro-term mixes between the picture and its luminance, and
  multiplies by the font colour, so at 0 the screen is one phosphor hue.
- **Brightness and contrast:** the set's two knobs, applied to the final
  colour.

Three things in the existing design shape this:
- **One parameter per effect.** Each registry row has one parameter, 0 is
  off, values run 0..maximum. The parser enforces this (a second member is
  `400 invalid_settings_effects`). The API reports one parameter, and the
  Control Panel builds one slider per row from `GET /v1/effects`'s
  `available` list.
- **Two classes.** Local effects are drawn into the scene by both
  compositors, inside each repaint. On GLES that means blended quads:
  scanline rows and corner coverage. Presentation effects are drawn by the
  GLES present pass only, and are reported `cpu compositor` elsewhere.
- **Brightness and contrast have a neutral middle.** They turn both ways,
  and "0 is off" has to mean "unchanged", not "black".

## Options considered

**Class of the colour controls.**
1. **Local.** By ADR 0186's definition they qualify, since a pixel's result
   depends only on the pixel underneath. On the CPU they could be applied
   once over each repainted rectangle, so they would never compound.
   - On GLES the scene is built with blended quads. A blend can multiply by
     a colour of at most 1, but it cannot mix channels (luma), pivot about
     mid-grey (contrast) or brighten above 1. So GLES would need a
     shader read of the scene: a presentation pass in all but name.
   - A local chroma would also leave the presentation pass's own light
     (glow, the hum bar, static noise) in full colour on a "monochrome"
     tube.
   - Rejected: one effect would belong to two classes, and still be wrong.
2. **Presentation.** One branch each in the present shader, after every
   light the pass adds. Chosen. The CPU compositor reports them
   `cpu compositor` and draws nothing, as ADR 0187 says: not imitated.

**Brightness and contrast.**
1. **One effect with two parameters.** Breaks the one-parameter contract
   in the parser, the API's `available` shape and the Control Panel.
   Rejected.
2. **Two effects, 0..200 with 100 neutral.** Breaks "0 is off": 0 would be
   both absent and black, and a slider would jump from neutral at 0 to near
   black at 1. Rejected.
3. **Two effects, signed, -100..100, 0 neutral.** "0 is off" holds exactly.
   The registry gains a `minimum` column (0 for every other row), the
   parser accepts a sign where the row's minimum is below 0, and `available`
   reports `minimum`. Chosen.

**The phosphor colour.**
1. **A second parameter on chroma.** Breaks the one-parameter contract, as
   above. Rejected.
2. **A hue slider.** Fits a slider, but cannot express white, nor
   cool-retro-term's own greens and ambers. Rejected.
3. **Presets by index (1 green, 2 amber, 3 white).** Fits a slider with no
   labels the API can give, and nothing between the presets. Rejected.
4. **Its own row, `phosphor {color: 0..16777215}`, an 0xRRGGBB integer.**
   The parser, settings and OpenAPI take it as they are. 0 is none, which
   is white: no phosphor can be black. Chosen. The parameter's name says it
   is a colour, as `radius_px` says its unit, so a panel can offer a colour
   picker there.

**What phosphor does without chroma.**
- cool-retro-term multiplies the picture by the font colour at every chroma
  setting. The owner's request is "0 = full colour", so here the phosphor
  is only what chroma mixes toward.
- Chosen: phosphor with chroma 0 changes nothing. Excluding it from the
  pass would special-case one row in the "has presentation effects"
  predicate, so the pane still takes the present pass. The pass then copies
  its pixels exactly, since its sampling is 1:1.

**Static noise's grain.**
- Per logical pixel, or on jitter's `cell_width / 6` grid? cool-retro-term
  scales its noise texture with the font.
- Chosen: one grain per logical pixel, the "fine grain" asked for, at one
  hash per pixel, with no interpolation. On a zoomed display a logical
  pixel is an even block, as every effect there is (ADR 0204).

## Decision

**Five registry rows,** all presentation class, after jitter:

| Key | Parameter | Range | Title | Animated |
|---|---|---|---|---|
| `static_noise` | `amount` | 0..100 | Static noise | yes |
| `chroma` | `amount` | 0..100 | Chroma | no |
| `phosphor` | `color` | 0..0xffffff | Phosphor | no |
| `brightness` | `level` | -100..100 | Brightness | no |
| `contrast` | `level` | -100..100 | Contrast | no |

**The rules, once, in `blit_effects.h`.** The shader mirrors them, and the
gates compile the C to predict pixels.
- **Static noise.** `blit_effect_static_noise` gives a strength of
  `amount / 100 × 0.5` (`BLIT_EFFECT_NOISE_GAIN`). Each logical pixel of the
  pane gains white light of `strength × hash(pixel, seed)`, using the
  present shader's existing `fract(sin(…))` hash at its own offset. The
  seed is the frame's `seed` lane of `blit_effect_frame_noise`, so no lane
  is added or reordered and ADR 0190's determinism holds. The grain is grey
  where it is added. It is animated, so it runs on ADR 0188's frame clock:
  30 Hz by default, filling gaps only, and none while off.
- **Colour.** `blit_effect_tube_colour` applies three steps to a colour in
  0..1, in this order:
  - **chroma**, with `a = amount / 100`: `c = mix(c, P × Y(c), a)`. Y is
    the Rec. 709 luma (0.2126, 0.7152, 0.0722) and P is the phosphor,
    white when 0. So 0 is full colour and 100 is monochrome in the
    phosphor's colour;
  - **brightness**: `c × (1 + level / 100)`. -100 is black, 100 double;
  - **contrast**: `0.5 + (c − 0.5) × (1 + level / 100)`, about mid-grey.
    -100 is flat grey, 100 double;

  each clamped to 0..1. Level 0 and chroma 0 skip their step entirely.

**Order in the present pass.** The ADR 0190 order gains two steps:
1. sync, jitter;
2. sample with RGB shift;
3. glow;
4. glowing line;
5. **static noise** (new);
6. flicker;
7. **chroma, brightness, contrast** (new);
8. corners;
9. glass scanlines.

As in cool-retro-term, the noise is light on the picture, so a monochrome
tube colours it, the glow and the hum bar too. The bezel's black corners
and the glass's scanlines come after the colour, so they stay black.

**The unchanged path.**
- Every new step sits behind its own uniform test: `noise.x > 0`,
  `tube.x > 0`, `tube.y != 1`, `tube.z != 1`. A pane without them computes
  exactly what it did before.
- A pane with none of the new effects and no other presentation effect is
  not in the present list at all.
- Sampling is unchanged: the new effects do not switch the scene to linear
  filtering.
- The noise did not get a hash of its own. The existing hash now takes its
  seed as an argument, with the same arithmetic as before.

**Signed values.** Only a row whose `minimum` is below 0 accepts a `-`;
every other row still refuses one. `GET /v1/effects` reports a level as
saved, negative included. Every "is this effect on" test in the registry,
and in the API's reporting, reads `!= 0`.

**Zoomed displays** (ADR 0204). The colour steps are per pixel, and the
noise hashes `floor(at)`, the logical pixel the zoom shows. So every new
effect is the 100% one, enlarged.

**The CPU compositor** keeps the values, reports each as unavailable
(`cpu compositor`), draws nothing, and gets no frame-clock ticks.

## Consequences

- A tube look in one place: a green or amber monochrome terminal is
  `chroma 100` with a phosphor; a washed-out set is `contrast -30`.
- **API:** `available` gains `minimum`, an additive field. A client that
  ignores it offers 0..100 for brightness and contrast: they still work,
  but can only be raised.
- **Control Panel** (not changed here; `~/hibr-apps`), to show these well:
  - a slider from each row's `minimum` to `maximum`, centred at 0 where
    the minimum is below 0;
  - a colour picker for a parameter named `color`, with green, amber and
    white presets, writing the 0xRRGGBB integer, and 0 for "none" (white);
  - phosphor shown with chroma, since it does nothing without it.
- **Cost:** a pane with the colour controls only costs a present quad,
  sampled 1:1, plus about ten ALU operations per pixel. Static noise adds
  one hash per pixel and the frame clock's presents, as jitter does.
  These are to be measured on the owner's Radeon. llvmpipe's frame-to-frame
  spread hides any difference.
- cool-retro-term's own grain is font-scaled and smoothed; this one is per
  logical pixel and unsmoothed. Moving to jitter's grid would be one
  shader line, should the owner want it coarser.

## Evidence

`tools/tests/tube_effects_gate.rb`, in the renderer suite:
- **Both compositors:**
  - the registry rows as the API reports them, minimum included;
  - strict settings: out-of-range values, a sign on an unsigned row, and a
    second parameter are refused;
  - negative levels are kept and reported as written.
- **gles, colour:** over three-cell stripes, every pixel of pane 1 is
  predicted from the plain capture by `blit_effect_tube_colour`, compiled
  into a probe, within 1. That is 860,160 pixels over seven sets: chroma
  100 in green, which makes the stripes monochrome in the phosphor; chroma
  50 in amber; brightness -40 and 60; contrast -50 and 70; and all four
  together.
- **gles, the unchanged path:** at 0 the effects draw no pass and the pane
  is byte-identical; a phosphor alone changes nothing.
- **gles, static noise:**
  - grey grain within 0..129 over a flat grey, with mean lift 64 ± 8;
  - over 90% of pixels change between frames;
  - the clock ticks;
  - under chroma 100 the grain is amber;
  - off, the plain pane returns and an idle screen presents nothing.
- **gles, isolation:** pane 2 is byte-identical.
- **native:** kept, reported unavailable, undrawn, and no frames.

`zoomed_effects_gate.rb` (gles) also checks, at 200%:
- with chroma, a phosphor, brightness and contrast on top of the static
  effects, display-1 is display-2 enlarged, within 2;
- static noise changes between frames in whole 2x2 blocks.

The gates that list the registry (`presentation_effects_gate`,
`terminal_conformance`) carry the new rows.

**Against the previous build (0.3.31):**
- `tube_effects_gate` fails on both backends at the registry
  (`static_noise` absent), and the zoomed gate fails saving the colour
  controls.
- Two mutants of this build are caught by the pixel checks, not the
  registry:
  - with the chroma step disabled in the shader, 122,880 pixels are off
    the shared rule;
  - with the noise step disabled, one grain level per frame is found.
- `zoomed_effects_gate`'s 100% capture with curvature, RGB shift,
  scanlines and corners (`BLIT_ZOOMED_EFFECTS_SHOT`) is byte-identical
  between 0.3.31 and this change.

Measured on the owner's mini PC (Radeon, 0.3.32, 2026-11-03): DP-1's top
pane already drawing curvature, glow, RGB shift, the glowing line,
flicker, horizontal sync and jitter; `gpu_total_us / gpu_frames` over 15 s
(about 494 frames) each, the owner's settings restored after:

| Added to the pane | GPU per frame |
| --- | --- |
| nothing | 1.754 ms |
| static noise 30 | 1.702 ms |
| chroma 70, amber phosphor | 1.755 ms |
| brightness 20, contrast 20 | 1.727 ms |
| all five | 1.727 ms |

The differences are within run-to-run noise: the new steps cost nothing
measurable on top of the pass they join.
