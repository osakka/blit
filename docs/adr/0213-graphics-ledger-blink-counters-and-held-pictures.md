# ADR 0213: The graphics ledger, blink counters, and pictures held through a synchronized update

- Status: accepted
- Date: 2026-11-05
- Scope: `blit_server.c` (the ledger, the blink detector, Kitty deletes,
  mode 2026, the counters and `GET /v1/diagnostics/graphics-ledger`),
  `blit_graphics.h` (the `retired` image), `blit_kms.c`, `blit_kms_gles.c`,
  `blit_kms.h` (what the compositors report), `graphics_blink_gate`,
  OpenAPI
- Builds on: ADR 0060 (placements and delete targets), ADR 0134 (non-visual
  bytes do not paint), ADR 0200 (images change in place), ADR 0082 (the Hibr
  frame barrier)

## Context

On the owner's mini PC (GLES compositor, a Hibr desktop), entering desktop
full screen on a Boxer game made the display blink five or six times over two
to three seconds. Boxer's own output was one `a=T` with two `a=d` in the first
frame and nothing that clears the screen. Hibr's `hold` had dropped and
re-sent the wallpaper at the same moment.

Nothing could say what Blit presented. The trace ring is 128 entries shared
with pointer events, and there was no counter for "a picture that was on
screen vanished for a frame and came back". A flicker could not be measured
after it had happened.

## Decision A: an instrument

- **The graphics ledger.** A ring of 512 picture events, a struct appended in
  O(1) (no allocation, no lock, no formatting until read), never shared with
  another trace. An entry has a sequence, a time, the session, the output's
  presented-frame count, a kind (`transmit`, `place`, `delete`, `free`,
  `reset`, `blink`, `barrier`), the image and placement ids, the placement
  rectangle in cells and pixels, the payload bytes, how many frames a blink
  lasted, and a reason (the delete letter, `evicted-unshown`,
  `frame-abandoned`, `a=T,replace`, a barrier's `hold`, `release`,
  `timeout`). `GET /v1/diagnostics/graphics-ledger?since=<sequence>&limit=`
  returns the entries after `since`, oldest first, with the cursor for the
  next read and how many the ring lost; it also carries the per-output
  presented-frame counters the entries are stamped from.
- **Counters** in the runtime diagnostics' `performance`:
  `graphics_placements`, `graphics_deletes`, `graphics_images_freed`,
  `graphics_upload_bytes` (picture bytes uploaded to GPU textures; the CPU
  compositor draws from the image store and uploads none),
  `graphics_bytes_landed` (picture bytes of completed transmissions),
  `graphics_full_repaints` (frames an output composed whole),
  `graphics_blink_frames`, `graphics_blink_events`, `graphics_barrier_holds`,
  `graphics_barrier_timeouts` and `graphics_synchronized_live_draws`.

### What a blink is

A *presented frame* is one that a compositor submitted for an output (a held,
deferred or unchanged attempt is not one). For each surface, after each
presented frame that drew it, the detector compares what was drawn with what
the surface showed in the frame before:

- A picture shown before and **absent now** is *vanished*. Absent means no
  placement of that **image id** is drawn, and no other image's placement
  covers at least half of its rectangle. Matching by id, not rectangle,
  means a window that moves is not a vanish; matching a covering successor
  means a picture replaced by a new id in the same frame is not one either.
- A vanished picture is watched for **8 presented frames**. If the same image
  id is drawn again, or a successor covering at least half of its rectangle
  is, that is one **blink event**, and the frames it was absent are added to
  `graphics_blink_frames`. The ledger gets a `blink` entry with the frames
  gone and a reason: what last deleted or freed that image, then `;returned`
  or `;successor=<id>`.
- A picture that is not back within 8 presented frames was **deleted for
  good**, and is not counted. So a screen intentionally cleared, an `a=d`
  followed by a different image elsewhere, or a program that stops drawing a
  picture count nothing; and when something does blink, the ledger's delete
  that precedes it says whether the program meant it.
- Granularity is the image id. Deleting one of an image's several placements
  while another is drawn is not a vanish. A gap longer than 8 frames is not
  seen as a blink either (it is a long absence, readable from the ledger).
  The detector sees what a compositor drew for each surface; it does not
  read pixels.
- A synchronized view that a whole-output redraw draws anyway (below) is shown
  to the detector as it was drawn: from its live pictures.

## Decision B: pictures a synchronized update is holding stay until it ends

### What the owner's blink was

Hibr brackets each frame in `CSI ? 2026 h ... l`, and the frame is `a=d,d=I`
of the old picture's id, then `a=T` of a new id in 4096-byte chunks. Blit
holds a synchronized view: both compositors skip it, so its last presented
pixels stay. **Except on a whole-output redraw**, which draws every view
from its live state (the code said "one tear rather than a hole"). The live
state of a view in the middle of Hibr's frame has the old wallpaper already
freed by `d=I` and the new one still incomplete -- a hole, for the frame.

A whole-output redraw is what a full-screen change causes: the number of
views changes, a pane's origin moves, the effects or overlays change. They
come in a burst for two or three seconds while panes resize, and each one
paints a frame without the wallpaper. Five or six of them are the owner's
five or six blinks. In steady state nothing redraws the whole output, which
is why only the transition blinked.

It was reproduced by `graphics_blink_gate` on both compositors before the fix:
Hibr's frame sent over several writes slower than the display's frames, with a
view appearing and going in the middle, blinked (`graphics_blink_events` 1,
`graphics_blink_frames` 6, `graphics_synchronized_live_draws` 6, the ledger
reading `delete-d=I;successor=2`). Hibr's own bytes were right: the delete and
the whole new picture are inside one synchronized update. The hole is
Blit's.

The frame barrier is not the cause by itself. It decides whether an output
renders at all (it held for one display interval and timed out as designed),
and it cannot draw a synchronized view. It is measured here, though: a
replace that lands within the barrier's interval shows no gap, and one slower
than the interval times out and presents what is there, which the ledger
records as `barrier` `hold` and `timeout` and
`graphics_barrier_timeouts` counts.

### The fix

A delete by capital letter (`d=I`, `d=A`, ...) that arrives while a session is
in synchronized output, of a complete image that is drawn, **keeps the image
and its placements and marks it retired**: nothing can find it by id, so the
program sees the delete (a new transmission of the same id is a new image),
but its pixels stay in the store and are drawn by any redraw. When the update
ends (mode 2026 off, a terminal reset, `ESC c`) the retired images are
freed, their cells are damaged, and the `free` is in the ledger with the
reason `delete-d=I,held`. A full store gives up retired images before any
other. The old picture is thus on screen until the frame that replaces it, and
the frame that ends the update shows the new one.

- A lowercase delete in a synchronized update (`d=i`, `d=a`) keeps the data
  and drops the placements as before; Hibr never sends it, and holding a
  placement but not an image would need the pixels shared. A program that
  sends it, then a whole-output redraw mid-update, can still show a gap.
- The API's graphics reset (`DELETE .../graphics`) frees retired images at once, so recovery works from a wedged update.
- A synchronized update that never ends holds its retired images as it holds
  its cells: until the program, a reset or the session ends.
- Outside a synchronized update nothing changes: a delete is at once, and a
  program that deletes in one write and sends the new picture in later ones
  blinks, as the gate proves the instrument sees.

## Consequences

- The owner's blink is measurable afterwards: read
  `performance.graphics_blink_events` and `graphics_blink_frames` before and
  after one Alt+Enter, and the ledger since a cursor.
- The ledger is cheap enough to be always on: an append is a `memset` and a
  few stores; the detector runs once per presented frame per surface over at
  most 144 draws.
- A whole-output redraw still draws a synchronized view's *cells* live; only
  its pictures are held. The text tear the code accepted stays.

## Evidence

`graphics_blink_gate` (new, in the renderer suite, on both compositors):
- **split** -- a delete in one write and the new picture across several
  later ones, unbracketed: blink events counted, the ledger showing the
  delete, frames, the place and the last chunk in that order, with the
  rectangle and the bytes, for `d=I` with a new id and for `d=i` and `d=a`
  with the same id again;
- **atomic** -- the delete and a whole new picture in one write: zero;
- **synchronized** -- Hibr's frame, slowly, over a 2026 batch: zero;
- **synchronized with a whole redraw** -- the same while a view appears and
  goes three times: zero, with `graphics_synchronized_live_draws` proving the
  redraw reached the held view. **Against the 0.3.40 server this fails** with
  one blink event, six blink frames and six live draws, on both compositors;
- **barrier** -- a Hibr desktop pair: a replace landing whole inside the
  barrier shows no blink and the ledger has the `hold`; a replace slower than
  the barrier's interval blinks, and the barrier's `timeout` is in the ledger
  and in `graphics_barrier_timeouts`.

## Considered and not done

- **A longer barrier timeout.** The barrier is not the cause, and a longer
  wait would delay every frame of every Hibr desktop.
- **Holding the cells too** (a snapshot of the held view for a redraw). The
  pictures are what blinked, and a snapshot of every synchronized surface
  costs memory on every 2026 program.
- **Queueing graphics commands until the update ends.** A same-id delete then
  transmit would delete the new image, and a multi-megabyte frame would be
  buffered.
- **Counting a gap longer than 8 frames as a blink.** It cannot be told from
  a deletion; the ledger shows both.
