# ADR 0200: Kitty images change in place: replace by id, and edit the root frame

- Status: accepted (2026-10-29)
- Date: 2026-10-29
- Scope: `blit_server.c` (the Kitty graphics parser), a new gate
- For: hibr #404, X Screen in pixels

## Context

Hibr's X Screen sends each frame as a whole Kitty image: `a=T,f=24`,
about 0.9 MB at 640×400. To send only what X says changed, it needs to
change an image that is already on screen. Two parts of the Kitty
protocol do that, and Blit had neither:
- **`a=f,r=1`** edits the root frame, a rectangle at a time. Blit refused
  `a=f` outright.
- **`a=t`/`a=T` with the id of a complete image** replaces its data in kitty.
  Blit treated the upload as a continuation of the finished image, refused
  it, and the refusal released the image. It vanished from the screen.

## Decision

A transmission that changes a complete image is staged and lands at once.
- **Staging.** It decodes into the session's staging image, which takes no
  slot in the store. Continuation chunks without `i=` find it, as they find
  any current upload (ADR 0084). Chunking, `o=z` and `f=100` behave as for
  a new image.
- **Landing.** Nothing changes on screen until the last chunk.
  - A **replacement** swaps in the new pixels, size and format, and keeps
    every placement. A placement whose source rectangle would fall outside
    a smaller replacement is refused, and the old pixels stay.
  - An **edit** writes the rectangle into the image. Straight RGB or RGBA,
    RGBA blending by alpha unless `X=1` asks to overwrite. A rectangle
    outside the image is refused, and the image is untouched.
  - Either way the image's generation advances, so the GPU compositor
    uploads the new texture, and its cells are damaged.
- **Only the root frame.** Blit has no animation, so `a=f` without `r`
  (a new frame) or with `r` other than 1 would be pixels nothing draws. It
  is refused with `EINVAL`, as is an edit of an unknown or incomplete image.
- **Success is answered (0.3.3).** Like kitty, a completed `a=t`, `a=T`
  or `a=f`, and a placement made with `a=p`, are answered
  `ESC _Gi=<id>;OK ESC \` unless `q=` quiets it. `q=1` keeps only errors
  and `q=2` keeps nothing. A transmission's quiet level is its opening
  chunk's, so a continuation without `q=` doesn't turn the reply back on.
  Before 0.3.3 only `a=q` was answered. Hibr's probe for edits (`a=t,t=d`
  then `a=f` of a 1×1 image, i=4202) waits for both OKs, so it fell back
  to whole pictures.
- **Placement on replacement.** `a=T` on a replacement places the image
  being replaced, which keeps showing its old pixels until the new ones
  land.

## Consequences

- **X Screen** can send one full image, then only changed rectangles with
  `a=f,r=1`, and no new `a=p`.
- **Re-sending an id is safe.** The image changes instead of vanishing.
- **An edit redraws only what it changed (0.3.2).** Its rectangle is mapped
  through each placement's source rectangle and scale, and only those cells
  are damaged. The image keeps a short history of its edits, as the
  rectangle changed between two generations. The GPU compositor walks back
  from the image's generation to its texture's, and uploads just the union
  of those rectangles. A replacement, a new image, or a gap in the history
  forces a whole upload. `image_partial_uploads` in the compositor
  diagnostics counts the partial ones.

## Evidence (0.3.1)

`kitty_frame_edit_gate` (new) checks a 16×16 image:
- the same-id replacement has exact pixels (CRC32), keeps its placement,
  and the screen shows it;
- a chunked edit, a compressed RGBA edit blending by alpha, and an `X=1`
  overwrite each give exactly the pixels Ruby computes, and the screen
  shows the edit with no new `a=p`;
- an out-of-bounds edit, an edit of an unknown id, and an edit of frame 2
  are each refused (three rejections), and the image is unchanged.
- 0.3.2:
  - an 8×8 edit of a 64×64 image (8×4 cells) damages 1×1 cell, as read
    from the stream's `surface_delta`;
  - on the GPU compositor, `image_partial_uploads` rises.
- 0.3.3: Hibr's probe, byte for byte, gets `i=4202;OK` twice in order, and
  a `q=2` edit after it gets nothing.
