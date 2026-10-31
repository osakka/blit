# ADR 0202: local picture frames by descriptor, not through the terminal

- Status: accepted; in use since 0.3.17
- Note: Blit's half in 0.3.17–0.3.19, Hibr's in 0.99.397–0.99.398 (2026-10-30). Reviewed by the Hibr maintainers; their answers and changes are folded in. Measured result under "Result".
- Scope: moving large or frequent pictures (video, a game, a browser page)
  from a program in a Blit session to Blit without encoding them into the
  terminal stream
- Supersedes: [ADR 0182](0182-desktop-video-off-the-pty.md) (desktop video stays on the PTY)
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

## Decision

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
  reads it with `pread` (never mapped, never written), uploads the pixels (or the rectangle) into that
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
    arrives;
  - the reverse, an `a=p` read before its image's first frame (0.3.18):
    while the session has a frame connection, an `a=p` that keeps the
    cursor (`C=1`) and names no image yet is held for two seconds and
    placed when that id's first frame lands; if none comes, it is refused
    `ENOENT` as Kitty would. Sessions without a frame connection, and
    placements that move the cursor, keep Kitty's rule unchanged.
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
  and pictures. Two Hibr decisions: a session's pictures go by descriptor
  only when every display of it can take them (one ssh or plain-terminal
  display puts all of them back on `t=d`, so hold never needs a second
  encoder); and the wallpaper stays on `t=d` (placed rarely and already
  kept).
- **Local only.** A descriptor cannot leave the machine, so the channel
  exists only for a program on the same host as Blit. A program reached
  over ssh, or running anywhere else, has no `BLIT_FRAME_SOCKET` (ssh does
  not forward it), and if it had one it could not reach an abstract socket
  in another kernel, nor pass the token check of a Blit it did reach. It
  sends its pictures through the terminal as before (`t=d`); nothing about
  this channel changes that path.
- **Linux only.** memfd and descriptor passing are Linux; Hibr on macOS
  stays on `t=d`.
- **A login session** (0.3.19). The login screen starts the user's session
  from an empty environment and passes on nothing of Blit's but its
  version (Hibr 0.99.357: the API token is the screen's, not the
  session's). It may pass `BLIT_FRAME_SOCKET` and `BLIT_FRAME_TOKEN`, and
  only those: the frame token reaches nothing but this session's images,
  and only from the signed-in user's uid (or root). Blit checks the
  sender's uid on every frame, not only at the hello, so a connection left
  over from a user who signed out is refused and closed rather than
  reaching the login screen or the next user.
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

  Answered `OK\n`, or `ERROR 0 EACCES\n` (wrong token, or a uid that may
  not send to that session) or `ERROR 0 EINVAL\n` (not a hello), and the
  connection closes. A
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
  codes as Kitty's (`EINVAL`, `ENOENT`, `ENOSPC`, `EFBIG`, `ENOMEM`).
  Accepted frames are not answered. A frame from a uid that may no longer
  send to the session (its user signed out) is answered `ERROR 0 EACCES`
  and the connection closes.
- **Newest wins.** Frames are read as they arrive; of several waiting for
  one image only the newest is applied, the others' descriptors closed
  unread.

## Consequences

- A video frame costs a memfd write in the producer, a descriptor hop
  through hold and one GPU upload in Blit: no base64, no PTY copy, no hold
  copy of the pixels. Measured under "Result".
- Blit gains a second listener. It is local and abstract, so it has no file
  and no file mode: the session token and the peer's uid are what authorise
  a frame, and a frame can only ever reach its own session's images.
- Two picture paths stay in Blit for good: descriptors for a program on this
  host, the terminal for everything else (ssh, another machine, macOS).
- Later, the GLES compositor could import a dma-buf the same way (zero copy
  to the GPU); this record does not decide that.

## Result

The same 60 s YouTube measurement on the owner's machine, a video playing
in a Hibr window across three displays (two 1920x1080, one 2560x1440):

| process | Kitty `a=T` (0.99.395) | one id, `a=f` (0.99.397) | by descriptor (0.99.398, Blit 0.3.19) |
|---|---|---|---|
| Hibr desktop | 69.3% | 32.0% | 34.5% |
| Hibr hold | 22.9% | 25.4% | 2.9% |
| Blit | 21.4% | 25.3% | 9.6% |

The 0.99.397 column composes twice the pixels of the first (all displays,
not one view). By descriptor, each display's session accepted 706 frames
(11.7/s) with none refused, stale or superseded, and no picture byte crossed
a PTY (`graphics_payloads_kitty` +0). What remains is Hibr's desktop
producing the frames.

## Verification

- The same 60 s video measurement, above.
- `tools/tests/frame_descriptor_gate.sh` (the Hibr suite) sends frames by
  descriptor into a placed image and checks the screenshot. It covers: a
  wrong token refused at the hello; unsealed, short, oversized and unknown
  frames refused with their codes; newest wins and an older sequence is
  dropped; a rectangle edit; a frame before its `a=p` drawn when placed; a
  held `a=p` placed when its first frame lands, or refused `ENOENT` after
  two seconds; a frame after its `a=d` refused `ENOENT`.
- Not gated: the uid rules (a wrong uid at the hello, a signed-out user's
  later frames), which need a second uid and so root; they are
  `frame_peer_allowed` in `src/blit_frames.h`. That no name is ever opened
  holds by construction: the frame path calls no `open`, `shm_open` or
  `unlink`.
