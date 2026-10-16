# ADR 0087: Scroll terminal graphics with their owning rows

## Context

Blit kept graphics at absolute pixel coordinates after a terminal scroll. The
text therefore moved while an image stayed behind, which is visible as image
and text drift. This affected the later `CSI S`/`CSI T` and scroll-region paths,
not only the scroll that a sixel image can cause while it is being placed.

Kitty and sixel graphics placed at the cursor are part of the terminal page;
API pixel frames are independent scene objects. A single absolute-coordinate
rule cannot represent both contracts.

## Decision

Graphics records carry an explicit `scroll_anchored` bit. Kitty placements and
sixel images set it; API pixel frames leave it clear. The terminal engine moves
anchored placements for index, reverse-index, `CSI S`/`CSI T`, and
insert/delete-line operations. The destination and source rectangle are
clipped together at the active page boundary. Graphics that leave the page are
kept only as bounded data and are hidden; Blit does not provide graphics
scrollback.

## Consequences

- Cursor/page graphics remain aligned with the text that placed them.
- API-owned frames do not move merely because a terminal session scrolls.
- Scroll-region and line-edit operations use the same rule as ordinary index
  movement, including vertical clipping.
- Renderer backends consume the same updated placement state, so SDL3 and KMS
  cannot disagree about the image origin.

## Verification

- `tools/tests/sixel_decoder_gate.rb` proves a placed sixel image follows a
  later `CSI 1 S`.
- `tools/tests/kitty_placement_gate.rb` proves a cursor-placed Kitty image
  follows a later `CSI 1 S`.
- The terminal and renderer suites remain required before release.

## Rejected alternative

Recomputing image positions from the current cursor was rejected because the
cursor is independent of previously placed images and cannot represent
numbered Kitty placements or a scroll region. Moving every graphics object was
rejected because API pixel frames are deliberately absolute scene content.
