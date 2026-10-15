# ADR 0084: Kitty continuation chunks use the current upload

- Status: accepted
- Date: 2026-10-15
- Scope: Kitty graphics parsing, Hibr wallpaper uploads, terminal stream state
- Related: Blit issue #83; [ADR 0005](0005-kitty-graphics-first.md), [ADR 0060](0060-kitty-placements-source-rectangles-and-delete-targets.md)

## Context

The Kitty graphics protocol permits one image payload to span several APC
strings. The opening transmission names the image with `i=<id>`; continuation
chunks carry `q=2,m=1` or `q=2,m=0` and intentionally omit `i`. Hibr uses this
form when its wallpaper exceeds Kitty's per-string payload size.

Blit's parser previously treated an omitted id as image zero. It accepted the
opening chunk, then rejected every continuation as `unsupported-action`,
leaving a visible placement backed by an incomplete image. On the real host
this appeared as a missing or unstable wallpaper and produced repeated
graphics rejections in the diagnostics trace.

## Decision

Blit keeps the current Kitty upload id in the owning terminal session. When a
Kitty graphics string has no action and no image id, Blit resolves it to that
session-local upload. The state is cleared only after the final chunk completes
the image. An id-less continuation without a current upload remains a
protocol error and is rejected.

The state is per PTY session. It is never global, inferred from another
session, or recovered by accepting an arbitrary incomplete image. This keeps
the one-source-of-truth rule: the protocol stream selects the image, and the
renderer consumes only a complete image.

## Alternatives rejected

1. Require `i=` on every chunk. That rejects Hibr's conforming encoder and
   makes wallpaper correctness depend on a producer-specific workaround.
2. Select the first or only incomplete image globally. That can join two
   unrelated terminal streams and corrupt both images under concurrent use.
3. Render each chunk immediately. That exposes partial pixels and violates
   the atomic complete-image presentation boundary.

## Consequences

- Hibr wallpapers and other conforming multi-chunk Kitty images complete
  without a producer change.
- A malformed or orphaned continuation remains visible in the API diagnostics
  as a rejection instead of silently attaching to the wrong image.
- The upload id is small per-session state and has no effect on single-chunk
  images, Kitty placements, sixel, or the public API schema.

## Verification

`tools/tests/graphics_trace_gate.rb` sends Hibr's exact opening and id-less
continuation forms and requires a complete image with `rejected=0`.
`tools/probes/kitty_image_echo.c` uses the same wire form for the TCC/QEMU
probe. The terminal and renderer suites must pass before packaging. The live
acceptance check is the authenticated graphics trace on the supplied host:
the Hibr wallpaper upload must finish with `complete:true` and no new
`unsupported-action` rejections.
