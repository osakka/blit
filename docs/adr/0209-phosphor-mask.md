# ADR 0209: The phosphor mask is a presentation effect on physical pixels

- Status: accepted
- Date: 2026-11-03
- Scope: effects registry, presentation shader, KMS present path, effects
  API and OpenAPI
- Builds on: ADR 0186 (the registry), ADR 0187 (presentation effects), ADR
  0204 (zoomed displays), ADR 0207 (the colour controls; one parameter per
  row, a row that only qualifies another)

## Context

The owner wants the RGB phosphor structure of a real tube, as the
well-known CRT shaders draw it (crt-lottes, crt-royale, crt-guest):
- the **aperture grille** of a Trinitron: vertical red, green and blue
  phosphor stripes;
- the **slot mask** of most TVs: the same stripes, cut into short slots,
  the slots of neighbouring triads staggered like bricks;
- the **shadow mask** of a delta-gun tube: round dots in triangles, R G B
  along a row, each row of triads half a triad along from the last.

Each shader multiplies every output pixel's channels by a periodic pattern:
the pixel's own phosphor passes, the other two are dimmed. That darkens
the picture, so most shaders scale it up again.

Four things in Blit shape the design:
- **One parameter per row** (ADR 0207). The parser refuses a second member,
  `available` reports one parameter, and the Control Panel builds one
  control per row. A mask has a type and a strength.
- **"0 is off."** Every row's 0 is "unchanged". A type cannot be off.
- **Zoomed displays** (ADR 0204). The present pass maps each physical
  pixel to the logical pixel it shows, and every effect so far is
  evaluated there: the 100% picture, enlarged.
- **The present pass's order** (ADR 0207): sampling, glow, hum bar, noise,
  flicker, the colour controls, corners, glass scanlines.

## Options considered

**Logical or physical pixels.**
1. **Logical, like every other effect.** At 200% the triad is six pixels
   wide: the mask becomes a coarse pattern of coloured stripes, the
   opposite of a fine structure, and its look changes with the zoom.
   Rejected.
2. **Physical.** A tube's mask is a property of the glass, not of the
   signal: a bigger picture on the same tube lands on the same phosphors.
   The CRT shaders above lay the mask on output pixels for the same
   reason, the finest structure the panel can show. Zoom enlarges the
   picture, and the mask stays at the panel's resolution. Chosen. It is
   the one effect that breaks ADR 0204's "the 100% picture, enlarged", by
   design, and the gate checks that it does.

**Anchored to the pane or to the screen.** Curvature, corners and
scanlines are counted from the pane's edge, since each pane is a tube.
The mask is counted from the frame's top-left pixel: it needs no uniform,
and two panes side by side show one continuous mask, as one sheet of
glass would. A pane's own edge never falls mid-period in a way anyone
could see at 1 px a phosphor. Chosen: the screen.

**Warped or flat.** The pattern is read at the fragment's own pixel, never
at the warped, swayed or jittered point. Curvature, horizontal sync and
jitter move the beam's picture; the glass stays still.

**The parameter shape.**
1. **One row per type, three `amount` rows.** Fits the panel as it is.
   But three can be set at once, and two masks on one tube mean nothing:
   any rule for it (multiply, first wins) would be invented. Rejected.
2. **Encode the type in the amount** (0..100 grille, 101..200 slot…).
   One slider that changes kind halfway. Rejected.
3. **Two rows: `mask {amount}` and `mask_type {kind: 0..2}`.** The strength
   is a slider; the type is an integer enum, as `phosphor {color}` is a
   colour qualifying chroma. "0 is off" means the default: 0 is the
   aperture grille, the most common mask and the one most shaders default
   to. `mask_type` alone draws nothing, as a phosphor without chroma.
   Chosen.

   A `choices` list in `available` would let a panel label the kinds from
   the API. It was left out: it changes the item schema and every gate
   that reads the registry for three labels the panel can show from this
   ADR, as it shows a colour picker for `color`.

**Compensation.**
1. **None.** At full strength white keeps one channel in three: the
   picture falls to a third. Rejected.
2. **A fixed gain,** as crt-lottes's `maskLight 1.5`. Too much for a weak
   mask, too little for a strong one. Rejected.
3. **The mask's mean transmission, divided out.** A colour the gain does
   not clip keeps its average light over one period, at every strength
   and type; strength only redistributes it into the phosphor pattern.
   Chosen. Where the gain clips (bright colours at a strong mask) the
   picture does lose light. That is inherent to an 8-bit display: a
   phosphor cannot be brighter than white. Turning the amount down is the
   control for it.

**Where in the pass.** The mask is at the phosphor plane: everything the
signal and the tube's electronics do (the picture, glow, the hum bar,
noise, flicker, the colour controls) arrives there and is filtered by it.
The bezel's corners and the glass's scanlines are in front of it. So the
mask follows the colour controls and comes before corners and glass
scanlines. Every one of those steps multiplies, but the gain clamps: in
this order the gain never lifts the black of a corner or a scanline gap.

## Decision

**Two registry rows,** presentation class, after `burn_in`:

| Key | Parameter | Range | Title | Animated |
|---|---|---|---|---|
| `mask` | `amount` | 0..100 | CRT mask | no |
| `mask_type` | `kind` | 0..2 | Mask type | no |

**The rule, once, in `blit_effects.h`.** The shader transliterates it, and
the gate compiles the C to predict pixels.
- `blit_effect_mask_strength`: `s = amount / 100`, 0 off.
- `blit_effect_mask_kind`: 0 aperture grille, 1 slot mask, 2 shadow mask
  (`BLIT_EFFECT_MASK_*`).
- `blit_effect_mask_channel(kind, x, y)`: the channel physical pixel
  (x, y) passes, counted from the frame's top-left:

  | Kind | Channel (0 R, 1 G, 2 B; none in a gap) | Period |
  |---|---|---|
  | aperture grille | `x mod 3` | 3 x 1 |
  | slot mask | none where `(y + 2 × (⌊x/3⌋ mod 2)) mod 4 = 3`, else `x mod 3` | 6 x 4 |
  | shadow mask | `⌊((x + 3 × ⌊y/2⌋) mod 6) / 2⌋` | 6 x 4 |

  The slot mask's slots are three rows tall with a dark row between, every
  other triad two rows down. The shadow mask's dots are two pixels wide,
  each pair of rows half a triad (3 px) along: a delta of dots.
- `blit_effect_mask_gain(kind, s)`: `1 / (L + (1 − L)(1 − s))`, where `L`,
  the share of pixels passing a given channel, is 1/3 (a quarter for the
  slot mask, whose gaps pass none). At 100 that is 3, or 4.
- `blit_effect_mask_rgb(set, x, y, factor)`: each channel's factor, `gain`
  where the pixel passes it, `(1 − s) × gain` where it does not; all 1 when
  off.

The pass applies `c = min(c × factor, 1)`.

**Order in the present pass.** ADR 0207's order gains one step:
1. sync, jitter;
2. sample with RGB shift (and burn-in);
3. glow;
4. glowing line;
5. static noise;
6. flicker;
7. chroma, brightness, contrast;
8. **the mask** (new);
9. corners;
10. glass scanlines.

Scanlines drawn in the scene (a pane without curvature) are part of the
picture by then, so the mask's gain scales both rows alike and keeps
their ratio, short of clipping.

**The shader.** One `vec3 mask` uniform: strength, gain, kind. The step is
behind `mask.x > 0.0`. It reads `floor(pixel)`, the frame pixel, so at
100% that is the pixel the content is read at, and on a zoomed output it
is the physical pixel, not the logical one. Its modulo is `wrap(v, n) = v −
n ⌊(v + 0.5) / n⌋`: GLSL's `mod` floors a quotient a GPU may compute by
reciprocal, which can land one period short at an exact multiple.

**The unchanged path.**
- With `mask` unset: no pass for a pane without other presentation
  effects, and `mask.x` is 0 in one that has them, so it computes exactly
  what it did.
- `mask_type` alone takes the pass, which copies the pixels exactly, as a
  phosphor alone does (ADR 0207).

**The CPU compositor** keeps both values, reports each as unavailable
(`cpu compositor`), draws nothing and gets no frame-clock ticks.

## Consequences

- **A tube look in one more place:** `mask 60` on its own is a Trinitron;
  with `mask_type 1` a TV; with `mask_type 2` a monitor's dot pitch.
- **No moiré on the panel.** The mask is laid on the panel's own pixels,
  so it never beats against them. A screenshot scaled by a viewer will
  moiré, as photographs of CRTs do.
- **Zoom no longer enlarges everything.** A zoomed pane with a mask is not
  2x2 blocks. Gates that assert blocks on zoomed displays must not set it.
- **Bright colours lose light at a strong mask.** Mean-light compensation
  is exact only below the clip: at 100, a grey above a third of white
  (a quarter for the slot mask) cannot keep its light. Brightness, applied
  before the mask, cannot help; a weaker mask can.
- **Cost:** about a dozen ALU operations per pixel (three wraps, a few
  compares, a multiply) in a pass the pane already takes; with no other
  presentation effect, a present quad sampled 1:1. To be measured on the
  owner's Radeon. llvmpipe's spread hides it.
- **API:** `mask` and `mask_type` in `EffectSet` and in `available`, both
  additive.
- **Control Panel** (not changed here; `~/hibr-apps`), from `available` as
  it is:
  - `mask` appears as a "CRT mask" 0..100 slider unchanged;
  - `mask_type` appears as a 0..2 slider. It should be a three-way choice
    for a parameter named `kind` of this row, labelled Aperture grille (0),
    Slot mask (1), Shadow mask (2), with 0 also what "unset" means, shown
    next to the mask's slider since it does nothing alone (as phosphor next
    to chroma);
  - its preview, if it draws one, should note that the mask is at the
    panel's resolution and does not grow with zoom.

## Evidence

`tools/tests/mask_gate.rb`, in the renderer suite. On one 640×384 display
split 1x2, pane 1 shows stripes in two colours, a white block and flat
grey 60. Pane 2 never has effects.

**Both compositors:**
- the two registry rows as the API reports them;
- strict settings: kind 3 or −1, amount 101 or −1, and a second parameter
  are all refused.

**gles:**
- **The rule:** each type, at 100 and at 40, and the slot mask at 70 with
  brightness −30 and contrast 25. Every pixel of pane 1 is the plain
  capture's pixel through `blit_effect_tube_colour` and then
  `blit_effect_mask_rgb` at its physical (x, y), compiled into a probe,
  within 1. That is 1,105,920 pixels.
- **The pattern,** from the capture alone, over the grey:
  - periods of 3×1, 6×4 and 6×4;
  - each pixel at `round(60 × gain)` in the channel the table names and
    `round(60 × (1 − s) × gain)` in the others, all dark in a slot gap.
- **Compensation:** each channel's mean over the grey is 60, within 1, for
  every type and strength.
- **Strength:** 40 and 100 draw different pixels.
- **The unchanged path:**
  - amount 0: no pass, and the pane is byte-identical;
  - `mask_type` alone: the pass runs, and the pane is byte-identical.
- **200%:** for the aperture grille and the shadow mask, every pixel is
  the plain 200% capture's pixel through the rule at its *physical*
  position, within 1. Every 2×2 block is split for the grille, and every
  other row pair for the shadow mask, whose dots are 2×2 (46,080 blocks
  in all).
- **Isolation:** pane 2 is byte-identical, before and after the zoom.

**native:** kept, reported unavailable (`cpu compositor`), undrawn, and no
frames on an idle screen.

The gates that list the registry (`presentation_effects_gate`,
`terminal_conformance`) carry the two new rows.

**Against the previous build (0.3.34, cad49e4):**
- `mask_gate` fails on both backends at the registry (`registry row
  mask`).
- Four shader mutants of this build fail on pixels, not on the registry:
  - **the mask read at `at` instead of `pixel`** (logical, enlarged):
    "200%, kind 0: 81984 pixels off the rule at physical pixels";
  - **no compensation:** "kind 0 at 100: 117760 pixels off the shared
    rule";
  - **the step disabled:** 122,880 pixels off;
  - **the mask before the colour controls:** "with the colour controls:
    122880 pixels off the shared rule".
- `zoomed_effects_gate`'s 100% capture with curvature, RGB shift,
  scanlines and corners (`BLIT_ZOOMED_EFFECTS_SHOT`) is byte-identical
  between 0.3.34 and this change.

`tube_effects_gate`, `burn_in_gate`, `zoomed_effects_gate`,
`presentation_effects_gate`, `pane_effects_gate` and `api_schema_gate` pass.
The renderer suite on gles passes: 76 passed, and 1 skipped for want of
QEMU.

The GPU cost on the owner's Radeon is not measured yet.
