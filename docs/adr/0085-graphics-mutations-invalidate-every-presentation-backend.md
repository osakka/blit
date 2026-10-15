# ADR 0085: Graphics mutations invalidate every presentation backend

- Status: accepted
- Date: 2026-10-15
- Scope: Kitty/Sixel graphics parsing, stale-pixel invalidation, SDL and direct KMS presentation
- Related: [ADR 0036](0036-external-cell-damage-and-graphics-scene-boundary.md),
  [ADR 0047](0047-incremental-damage-restores-under-text-graphics.md),
  [ADR 0060](0060-kitty-placements-source-rectangles-and-delete-targets.md),
  [ADR 0084](0084-kitty-continuations-use-the-current-upload.md)

## Context

Hibr's Kitty image deletion command is a header-only control string:

```text
ESC_Ga=d,d=I,q=2,i=<image-id>ESC\\
```

It has no semicolon and no data payload. Blit previously rejected that valid
form as `missing-payload`, so an image could remain visible after Hibr had
removed it. The result was an overlapping wallpaper, line, or icon that
appeared to survive later terminal updates.

There was also a separate direct-KMS invalidation hole. Graphics placement and
image lifetime can change without changing the terminal cell revision. KMS
therefore needs the graphics-scene identity when deciding whether a view is
unchanged; otherwise an image-only delete or move can expose stale scanout
pixels.

## Decision

1. Accept a Kitty APC with no semicolon only when its parsed action is `a=d`.
   All other Kitty actions still require a payload delimiter, and malformed
   commands remain rejected.
2. Keep the existing graphics-scene signature as the SDL invalidation source.
   Pass the same signature through `BlitKmsSurfaceView` and include it in the
   direct-KMS unchanged-view comparison.
3. Keep image deletion as a full-surface damage event. The renderer must
   repaint the pixels exposed by deletion, not only the terminal cells around
   the cursor.

The parser, canonical graphics state, SDL renderer, and direct-KMS presenter
remain one implementation boundary. Hibr remains unchanged.

## Alternatives rejected

1. Add a semicolon to Hibr's output. This would modify an external dependency
   and hide a Blit protocol-compatibility defect.
2. Treat every no-semicolon Kitty command as valid. That would turn malformed
   uploads into ambiguous state and could silently draw incorrect pixels.
3. Rely only on the terminal surface revision for KMS. Graphics-only moves and
   deletes do not necessarily write cells, so the revision is insufficient.
4. Clear the whole physical output after every terminal update. That avoids
   some trails at the cost of unnecessary work and does not repair the parser
   rejection.

## Verification

`tools/tests/sdl_wallpaper_damage_gate.rb` uses Hibr's exact header-only delete
form and proves the pixel sequence `before=0,0,255`,
`after=0,0,255`, `deleted=0,0,0` while the child session remains alive.
The full renderer suite passes with `RUN_GATES_PASS pass=43 skip=0 fail=0`.
The direct-KMS signature change is TCC-built and covered by the shared view
contract; its physical scanout acceptance still requires the VKMS/QEMU or
hardware KMS gate because the local renderer suite uses SDL's dummy backend.
