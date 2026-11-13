# ADR 0234: Shake to find the pointer

- Status: proposed (0.3.102; accepted once the owner has shaken it on the
  mini PC)
- Date: 2026-11-13
- Scope: the pointer (`src/blit_pointer_shake.h`, `src/blit_pointer_art.h`,
  `src/blit_kms.c`, `src/blit_server.c`), `document.pointer` in
  `PUT /v1/settings`, the Blit Mouse pane

## Context

The owner asked for the pointer to grow when the mouse is shaken, so it
is easy to find on a large or busy display: "grow larger the more we move
it aggressively and shrink as such ... make it a really cool user
experience, and feel very interactive". It must be a setting, tunable in
the Blit Mouse pane, never dictated.

Blit draws its own pointer from vector art at 24–64 px. Until now a size
change rebuilt the art and repainted every output: fine for a settings
change, not for a pointer whose size changes every frame while shaken.

## Options considered

1. **Grow on speed.** Simple, but a fast straight sweep across the display
   is not a search for the pointer, and would grow it on every long move.
2. **Count direction reversals in a window, grow in fixed steps.** Tells a
   shake from a sweep, but the size jumps, and a harder shake looks no
   different from a gentle one.
3. **Path length over the box it covers, scale continuous.** Over the last
   750 ms, the length of the pointer's path against the diagonal of the
   box it covered. A sweep, however fast, has a ratio near 1; a shake goes
   back and forth over a small box, and the ratio rises with how hard it is
   shaken. The scale follows the ratio continuously, so the harder the
   shake the bigger, and it falls on its own as the history ages out.

## Decision

Option 3.

**The detector** (`blit_pointer_shake.h`, pure, fed positions and times):

- Only turning points are kept: a point continuing the last segment's way
  on both axes (1 px either way counts as any way) replaces the last one.
  At most 64 points in the 750 ms window.
- A box under 60 px of diagonal is a jitter, not a shake: ratio 0.
- The threshold is `4.5 − 0.3 × sensitivity` (sensitivity 1–10: 1 needs a
  hard shake, 10 a light one). Above it the target scale rises linearly over
  a ratio of 3 to its most, `shake_max_px / size_px`.
- The drawn scale eases toward the target: quickly growing (60 ms), slowly
  shrinking (300 ms). Once the drawn size can no longer differ from the
  base by 2 px, it is at rest.
- Drawn sizes are in 4 px steps, so a level's art is reused.

**What feeds it:** relative motion from a hand on a device (evdev, or
relative motion injected through the API). A warp (the API's absolute
position, a zoom), a held button (a drag is not a search), a locked,
confined or hidden pointer each reset the history instead.

**Cost:** nothing while nobody shakes. A tick is asked of the loop only
while the pointer is grown or growing (every 8 ms). The art at the largest
size is built once while nothing has been drawn for a second, so the first
shake finds it ready; each size is area-resampled from it (about 1.5 ms at
the largest). Sizes up to 64 px are still rasterized exactly as before
(byte-identical); above 64 px the rasterizer samples finely only near edges.
The direct-KMS path repaints only the old and new pointer rectangles when
the size changes, not the whole output.

**Settings** (`document.pointer`, `effective.pointer`):
`shake_to_find` (default true), `shake_max_px` 64–256 (default 160),
`shake_sensitivity` 1–10 (default 5). `effective.pointer_shown` reports the
size drawn now (`size_px`), `shake_scale` and `shake_sizes` (how many times
the drawn size has changed).

## Consequences

- On by default: the owner asked for it, and it costs nothing at rest. The
  Blit Mouse pane turns it off and tunes it.
- `BLIT_POINTER_MAX` is 264, so the pointer's art and the KMS pointer
  background are about 280 KB each instead of 20 KB.
- The GLES renderer already re-creates the pointer texture on a size
  change; it does so at most once per 4 px step while shaken.
- A program's own pointer shapes (OSC 22) grow too: they take the
  display's size and so its shake.

## Evidence

- `pointer_shake_gate.sh` (tcc, and under ASan/UBSan): a fast straight
  sweep, slow wandering and a jittering mouse do not grow it; a moderate
  shake grows it, a harder one more; never past the most, in 4 px steps;
  it eases back (still grown 400 ms after, back to 1 and idle later);
  switched off it does not grow; a reset forgets the history.
- `pointer_settings_gate.rb` `a-shaken-pointer-grows-and-settles`: the
  on a running Blit, relative motion shaken through the API grows
  `pointer_shown.size_px` in 4 px steps within 160 px while a fast
  straight sweep does not, it settles back within 2.5 s, switched off
  through `PUT /v1/settings` a shake does nothing, and a most of 300 px is
  refused with 400.
- Not yet: the owner's hand on the mini PC.
