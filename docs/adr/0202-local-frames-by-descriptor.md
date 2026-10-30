# ADR 0202: local picture frames by descriptor, not through the terminal

- Status: accepted; Blit's half implemented in 0.3.17 (2026-10-30), Hibr's
  half (the console writer and hold's descriptor passing) in progress.
  Reviewed by the Hibr maintainers; their answers and changes are folded in.
- Scope: moving large or frequent pictures (video, a game, a browser page)
  from a program in a Blit session to Blit without encoding them into the
  terminal stream
- Depends on: [ADR 0066](0066-the-terminal-names-no-file-blit-opens.md),
  [ADR 0200](0200-kitty-images-change-in-place.md),
  [ADR 0065](0065-the-privilege-and-authentication-model.md)

## Context

Measured on the owner's machine, 2026-10-30, Blit 0.3.16 and Hibr
0.99.395, while a YouTube video played in a Hibr window (60 s, cumulative
CPU of each process):

| process | with the video | same windows, no video |
|---|---|---|
| Hibr desktop | 69.3% | 26.6% |
| Hibr hold | 22.9% | 1.8% |
| Blit | 21.4% | 7.0% |

Each frame is a 924x462 RGB picture sent as a new Kitty image (`a=T`, a new
`i=` every frame, `f=24`, raw, base64 in ~4 KB chunks, the previous image
deleted): about 670 payloads and at least 2.7 MB/s through one view's PTY.
Every frame is converted to text, written to a PTY, copied by `hold` to its
client's PTY, read by Blit, decoded from text, turned into a new image object
and placement, and uploaded whole to the GPU. A third of the bytes are
base64's overhead.

Two steps reduce it:

1. **Within today's protocol** (Hibr's side, Blit supports it since 0.3.1):
   send every frame into one image id -- `a=t` with the same `i=` replaces
   the pixels and keeps the placement, `a=f,r=1` replaces a rectangle and
   Blit uploads only that. That removes the per-frame image and placement
   churn. The text and the copies stay. Sent to Hibr 2026-10-30.
2. **Not through the terminal at all**, which this record is about.

Kitty's own answer is `t=s`: the pixels go in a POSIX shared-memory object
and the escape names it. ADR 0066 refuses it, and still holds: anything that
reaches a session's PTY -- a program's output, `cat` of a file, a log line
-- could make Blit, which may run as root, open and unlink a shared-memory
object by name. The channel is not authenticated, so a name arriving on it
must not be opened.

## Options considered

1. **Kitty `t=s` over the PTY.** Refused by ADR 0066, for the reason above.
2. **The existing pixel-frame WebSocket stream** (ADR 0013, 0166), which is
   authenticated. Costs: it draws frames as an API surface, not as a picture
   inside the terminal's own scene, so a Hibr window over the video, the menu
   bar, and the z-order Kitty placements give (pictures under text, ADR 0200)
   are lost; and frames still cross a socket by copy.
3. **A local frame channel by file descriptor** (this decision). The pixels
   travel as a sealed memfd over an authenticated Unix socket; placement,
   z-order and deletion stay in the terminal stream as Kitty escapes under
   the same image id.

## Decision (proposed)

Option 3.

- **The channel.** Blit listens on an abstract-namespace Unix socket (no
  file, so no directory permissions stand between a session's user and it)
  and tells each session its name in `BLIT_FRAME_SOCKET`, with a random
  per-session secret in `BLIT_FRAME_TOKEN`. A client connects, sends a
  hello with that token, and Blit checks the token names a live session and
  that the peer's uid (`SO_PEERCRED`) may send that session frames: the
  signed-in user for a login session, the account a session was started
  as otherwise, or root. Everything the client sends after that applies to
  that session only. (`BLIT_SESSION_TOKEN` is not used: it is built from
  the start time and the session number and is not a secret.)
- **A frame.** A fixed binary header -- image id, a sequence number, width,
  height, format 24 or 32, stride, and for an edit the rectangle `x y w h`
  -- and one memfd passed with `SCM_RIGHTS`. Blit requires the memfd sealed
  against shrinking, growing and writing (`F_SEAL_SHRINK | F_SEAL_GROW |
  F_SEAL_WRITE`; the producer has to unmap its writable mapping before
  `F_SEAL_WRITE` will succeed), checks its size covers `stride x height`,
  maps it read-only, uploads the pixels (or the rectangle) into that
  session's image `i=`, and closes it. No name is opened, created or
  unlinked; ADR 0066's invariant is untouched.
- **Two channels, ordered by a sequence number.** Creation, placement and
  deletion travel on the PTY and pixels on the socket, with no ordering
  between the two. So an image is either a Kitty image or a *descriptor
  image*, never both:
  - a descriptor image's pixels only ever travel on the socket, its first
    frame included; the PTY carries only `a=p`, `a=d` and `z=` for it;
  - each frame carries a sequence number that rises per id, and Blit drops
    a frame that is not newer than the one it holds;
  - a frame for a deleted id gets `ENOENT` and is dropped; producers never
    reuse an id;
  - a frame for an id not yet placed is kept and drawn when the `a=p`
    arrives.
- **Through hold** (Hibr's design, from their review). Unheld, the desktop's
  console connects to `BLIT_FRAME_SOCKET` itself: one hop. Held, two hops:
  the desktop opens a frame connection to its own hold server (uid checked
  with `SO_PEERCRED`), and the server passes the same descriptor to each
  client whose view holds the picture and whose terminal speaks kitty -- one
  window across two views goes to both. The server keeps the last memfd per
  image so a client that attaches later gets the current frame, and drops it
  on delete. Ids are the ones each client sees. A client not in a Blit
  session (an ssh attach, a plain terminal) never receives a descriptor and
  stays on `t=d`.
- **The producer** is Hibr's console (`cn_image`, through which every
  picture reaches the terminal), so one writer covers video, pages, games
  and pictures.
- **Linux only.** memfd and descriptor passing are Linux; Hibr on macOS
  stays on `t=d`.
- **Offered, not assumed.** A session gets `BLIT_FRAME_SOCKET` only when
  the channel is up; a client without it, or refused at the hello, uses
  `t=d`. The Kitty `a=q` probe is unchanged.
- **Bounds.** The same limits as a Kitty image (4096x4096, 64 MiB), with
  the per-session image count raised from 8 to 16 for both: a desktop can
  hold a wallpaper, a video, a browser page and two previews at once, and
  hold keeps one more during a replacement. One frame in flight per image: a newer frame for the same id
  replaces one not yet uploaded, so a slow GPU drops frames instead of
  queueing them. Errors are a one-line reply on the socket, with the same
  codes as Kitty (`EINVAL`, `ENOENT`, `ENOSPC`, `EFBIG`).

## Wire format (version 1)

Blit is the server, so it states the bytes. Little-endian, fixed offsets,
one message per `recvmsg` (`SOCK_SEQPACKET`).

- **Socket:** abstract `blit-frames-<port>` (`sun_path` starts with a NUL),
  given as `BLIT_FRAME_SOCKET=@blit-frames-<port>`; `<port>` is the API's.
- **Hello**, the first message, no descriptors:

  | offset | size | field |
  |---|---|---|
  | 0 | 8 | magic `BLITFRM1` |
  | 8 | 4 | version, `1` |
  | 12 | 4 | token length, 1–64 |
  | 16 | n | `BLIT_FRAME_TOKEN` |

  Answered `OK\n`, or `ERROR EACCES\n` and the connection closes. A
  frame before a successful hello closes the connection.
- **Frame**, 48 bytes and exactly one memfd in `SCM_RIGHTS`:

  | offset | size | field |
  |---|---|---|
  | 0 | 4 | magic `FRM1` |
  | 4 | 4 | image id, ≥ 1 |
  | 8 | 8 | sequence, rising per image id |
  | 16 | 4 | image width |
  | 20 | 4 | image height |
  | 24 | 4 | format, `24` (RGB) or `32` (RGBA) |
  | 28 | 4 | stride, bytes per row in the memfd |
  | 32 | 4 | x |
  | 36 | 4 | y |
  | 40 | 4 | w |
  | 44 | 4 | h |

  `w = h = 0` is the whole image: the memfd holds `width x height` rows of
  `stride` bytes. Otherwise it holds the `w x h` rectangle at `x, y`, which
  must lie inside the image. An image's first frame is a whole one; a
  whole frame with a new width or height resizes it, placements kept.
- **Replies** only on error, one line: `ERROR <image id> <code>\n`, the
  codes as Kitty's (`EINVAL`, `ENOENT`, `ENOSPC`, `EFBIG`). Accepted frames
  are not answered.
- **Newest wins.** Frames are read as they arrive; of several waiting for
  one image only the newest is applied, the others' descriptors closed
  unread.

## Consequences

- A video frame costs a memfd write in the producer, a descriptor hop
  through hold and one GPU upload in Blit: no base64, no PTY copy, no hold
  copy of the pixels. The expected result is most of the 69% + 23% + 21%
  above; it is to be measured, not claimed.
- Blit gains a second listener. It is local, mode 0660, group `blit`; the
  session token and peer uid are what authorise a frame, and a frame can
  only ever reach its own session's images.
- Hibr's work: write frames to a memfd and send them by descriptor where
  `BLIT_FRAME_SOCKET` is set, and pass descriptors through hold. Until then
  step 1 above applies.
- Later, the GLES compositor could import a dma-buf the same way (zero copy
  to the GPU); this record does not decide that.

## Evidence to gather

- The same 60 s video measurement on the owner's machine before and after,
  per process, with the same windows open.
- A gate: a client sends frames by descriptor into a placed image and the
  screenshot shows them; a wrong token, a wrong uid, an unsealed or short
  memfd and an oversized frame are each refused with their code; a frame
  before its `a=p` is drawn when placed; a frame after its `a=d` gets
  `ENOENT`; an older sequence number is dropped; nothing under `/dev/shm`
  is opened.
