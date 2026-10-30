# ADR 0202: local picture frames by descriptor, not through the terminal

- Status: proposed (2026-10-30), for review with the Hibr maintainers
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

- **The channel.** Blit listens on a Unix socket, `/run/blit/frames.sock`,
  and tells each session where with `BLIT_FRAME_SOCKET`. A client connects,
  sends a hello with its `BLIT_SESSION_TOKEN`, and Blit checks the token
  names a live session and that the peer's uid (`SO_PEERCRED`) is that
  session's user. Everything the client sends after that applies to that
  session only.
- **A frame.** A fixed binary header (image id, width, height, format 24 or
  32, stride, and for an edit the rectangle `x y w h`) and one memfd passed
  with `SCM_RIGHTS`. Blit requires the memfd sealed against shrinking and
  writing (`F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE`), checks its size
  covers `stride x height`, maps it read-only, uploads the pixels (or the
  rectangle) into that session's Kitty image `i=`, and closes it. No name
  is opened, created or unlinked; ADR 0066's invariant is untouched.
- **Everything else stays Kitty.** The client places, moves, layers and
  deletes the image with ordinary escapes on its PTY (`a=p`, `a=d`, `z=`).
  A frame for an image the session has not placed is stored and drawn when
  placed, as a transmission is today.
- **Through hold.** The Hibr desktop runs behind `hold`; the program in
  the Blit session is hold's client. The desktop passes the memfd to the
  client over hold's own local socket and the client forwards it to Blit
  with its session token. A client that is not in a Blit session (an ssh
  attach, a plain terminal) keeps `t=d`.
- **Offered, not assumed.** A session gets `BLIT_FRAME_SOCKET` only when
  the channel is up; a client without it, or refused at the hello, uses
  `t=d`. The Kitty `a=q` probe is unchanged.
- **Bounds.** The same limits as a Kitty image (4096x4096, 64 MiB, 8 images
  per session). One frame in flight per image: a newer frame for the same id
  replaces one not yet uploaded, so a slow GPU drops frames instead of
  queueing them. Errors are a one-line reply on the socket, with the same
  codes as Kitty (`EINVAL`, `ENOENT`, `ENOSPC`, `EFBIG`).

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
  memfd and an oversized frame are each refused with their code; nothing
  under `/dev/shm` is opened.
