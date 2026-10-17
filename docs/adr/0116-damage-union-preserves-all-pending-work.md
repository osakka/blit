# ADR 0116: Damage union preserves all pending work

- Status: accepted
- Date: 2026-10-17
- Scope: canonical terminal damage and direct-KMS composition
- Issue: [Blit #89](https://git.home.arpa/itdlabs/blit/issues/89)

## Evidence

The real surface helpers lost prior damage when a new write moved left or
up: `(60,30)` followed by `(2,1)` produced `(2,1,1x1)` rather than
`(2,1,59x30)`. Point, wide-cell, rectangle, DEC rectangle, and partial-scroll
paths could discard pending cells. Both SDL and direct KMS consume this state.

The production `blit_kms_present()` also failed a memory-backed regression:
a simultaneous pointer move replaced pending terminal bounds with pointer
bounds, left an erased red block visible, failed to draw its replacement, and
then retired the terminal revision. Only the kernel page-flip function was
simulated; the scene and presentation implementation was the real code.

The proposed complete-output redraw rollback was withdrawn before deployment.
It would conceal the accumulator defect and, because pointer work skipped the
changed-view check, would not even repair the concurrent-pointer case.

## Decision

Use one half-open rectangle union in `src/blit_damage.h`, shared by terminal
state and KMS composition. Save old right/bottom bounds before changing the
origin. Every partial mutation unions with outstanding damage, including DEC
rectangles and partial scrolling. Full-surface invalidation still replaces
damage because it contains every visible cell.

Accumulate pointer bounds separately, then union them into the terminal view's
pending bounds. Examine moved views even when pointer/capture work has already
scheduled the output. Retain ADR 0108's partial scene composition and page-flip
ownership rules.

This follows the invariant visible in
[Alacritty's damage bounds](https://github.com/alacritty/alacritty/blob/master/alacritty_terminal/src/term/mod.rs):
the minimum left and maximum right survive every expansion. Kitty's
[line dirty state](https://github.com/kovidgoyal/kitty/blob/master/kitty/line-buf.c)
likewise persists until consumed. These are behavioral references; no external
implementation was copied into Blit.

## Verification

- `surface_damage_gate.sh` exercises real state helpers and was observed red
  before the fix and green afterward for all five failing cases.
- `kms_compose_gate.sh` exercises the production presenter and was observed red
  before the pointer fix. It then compares every pixel of 128 incremental
  frames with freshly composed reference frames, including moving half-blocks,
  Kitty placement movement/deletion, and simultaneous pointer movement.
- Renderer/terminal gates and live screenshots remain distinct evidence.
  Passing this regression does not establish all VT500/graphics conformance,
  physical timing, or complete Hibr desktop acceptance.
