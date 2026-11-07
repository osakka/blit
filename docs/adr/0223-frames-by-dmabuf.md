# ADR 0223: frames by dmabuf, imported by the GPU compositor without a copy

- Status: proposed
- Date: 2026-11-07
- Scope: the local frame channel (ADR 0202) and the GPU compositor
  (ADR 0181): letting a program hand Blit a picture that is already in GPU
  memory, so it is drawn without being read back, copied or uploaded
- Depends on: [ADR 0202](0202-local-frames-by-descriptor.md) (the channel,
  its authentication and its ordering rules, unchanged),
  [ADR 0181](0181-gles-compositor-inside-native-kms.md) (the GPU compositor),
  [ADR 0066](0066-the-terminal-names-no-file-blit-opens.md) (no names)

## Context

Blit already uses GBM and dmabufs for its own output: the GPU compositor
draws each frame with OpenGL ES into a GBM surface, and that buffer becomes
the scanout buffer through KMS (`kms_gles_lock_scanout`, `src/blit_kms.c`).
Nothing on the way from Blit's GPU to the screen is copied by the CPU.

Pictures from other programs are not handled that way. Video, a browser page
and a game's screen reach Blit as pixels in system memory: since 0.3.19 by
descriptor (ADR 0202, a sealed memfd over an authenticated Unix socket), and
before that, or off the local machine, as Kitty images through the terminal.
For each frame Blit `pread`s the memfd and uploads the pixels into a GL
texture. `EGL_LINUX_DMA_BUF_EXT` is not used anywhere in the source.

ADR 0202's measurement (60 s of YouTube in a Hibr window, owner's machine)
shows what that path costs now:

| process | by descriptor (Blit 0.3.19, Hibr 0.99.398) |
|---|---|
| Hibr desktop | 34.5% |
| Hibr hold | 2.9% |
| Blit | 9.6% |

Two consequences shape this record:

1. **Blit's own share is already small.** What dmabuf import removes on
   Blit's side is the `pread`, the upload and the texture's memory bandwidth:
   a part of 9.6%, more at higher resolutions and frame rates (a 1080p60
   video is about 500 MB/s of RGBA uploads).
2. **The bigger cost is the producer's**, and dmabuf only helps a producer
   whose frame is already on the GPU. A producer that draws on the CPU still
   has to put the pixels somewhere, and a memfd is as good as anything for
   that.

So the value of this work depends on which producers can hand over GPU
buffers:

| producer | where its frame is | dmabuf helps |
|---|---|---|
| Browser (WPE WebKit) with GPU compositing | GPU; WPE's FDO backend exports dmabufs | yes: no readback, no copy, no upload |
| Video decoded in hardware (VA-API) | GPU, as NV12/P010 surfaces exported as dmabufs | yes, and the YUV-to-RGB conversion moves to the GPU too |
| Video decoded in software (the current YouTube path) | CPU | no; stays on memfd |
| Boxer (DOSBox-style software renderer) | CPU | no; stays on memfd |
| Kitty images, wallpaper, previews | CPU, rare | no |

## Options considered

1. **Leave it.** Blit's share of a video is under 10%; the remaining cost is
   the producers'. Fails the owner's aim of the fastest path for GPU
   producers, and keeps an avoidable readback in the browser.
2. **A new channel for dmabufs.** Duplicates ADR 0202's authentication,
   session binding, ordering and placement rules. Two channels to keep in
   step: rejected (one source of truth).
3. **Extend ADR 0202's channel with a dmabuf frame** (this proposal). Same
   socket, hello, token, uid check, image ids, sequence numbers, placement by
   Kitty escapes and newest-wins rule; a new frame message carries dmabuf
   planes instead of a memfd.

## Decision (proposed)

Option 3, as wire format version 3 of the frame channel.

- **Offered, not assumed.** In the hello's answer Blit says whether it can
  import (`OK dmabuf=1 formats=<n>\n`) only when the GPU compositor is active
  and EGL has `EGL_EXT_image_dma_buf_import` (and `_modifiers` for tiled
  buffers). A producer that does not see it keeps sending memfds; nothing
  changes for it.
- **The frame.** A fixed header like ADR 0202's (image id, sequence,
  width, height) plus the DRM fourcc, the format modifier, the plane count
  (1 to 4), and per plane an offset and a pitch. The planes' descriptors,
  and optionally one acquire fence (a `sync_file`), travel in
  `SCM_RIGHTS`. No rectangle edits: a GPU frame is whole.
- **Formats.** RGB first (`XRGB8888`, `ARGB8888`, `XBGR8888`, `ABGR8888`),
  then YUV (`NV12`, `P010`) through an external texture
  (`GL_TEXTURE_EXTERNAL_OES`), so a hardware-decoded video never becomes RGB
  on the CPU. Blit lists the fourcc and modifier pairs it can import
  (`eglQueryDmaBufFormatsEXT`, `eglQueryDmaBufModifiersEXT`) in the hello
  answer's version 3 extension and in `GET /v1/diagnostics`, so a producer
  allocates something Blit can read.
- **Import.** Blit creates an `EGLImage` from the planes
  (`EGL_LINUX_DMA_BUF_EXT`), binds it to the image's texture and draws it as
  any picture: placement, z-order, clipping and effects unchanged. If the
  import fails (another GPU, a format or modifier it cannot read, a bad
  size), the frame is refused `ERROR <id> ENOTSUP` and the producer falls
  back to memfd for that image. A buffer from a different DRM device than
  Blit's is refused rather than copied.
- **Synchronisation.** A frame may carry an acquire fence; Blit waits on it
  in the GPU (`EGL_ANDROID_native_fence_sync` / `eglWaitSyncKHR`), never on
  the CPU. Without a fence Blit relies on the kernel's implicit
  synchronisation of the dmabuf.
- **Release.** Blit keeps a buffer until a newer frame for that image has
  been presented, then answers `RELEASE <id> <sequence>\n` so the producer
  can reuse the buffer (a producer keeps a small pool, normally three). This
  is the only new reply; errors stay one line as in ADR 0202.
- **Bounds.** ADR 0202's limits stay (4096x4096, 16 images per session, one
  frame in flight per image, newest wins). Blit never maps a dmabuf; it
  checks each plane's size with `lseek(fd, 0, SEEK_END)` against offset and
  pitch before importing.
- **The CPU compositor.** It does not advertise `dmabuf=1`. If the
  compositor falls back from GPU to CPU while dmabuf images are live, Blit
  answers their next frames `ENOTSUP`, and producers resend those images by
  memfd. Blit does not read dmabufs back on the CPU.
- **Security.** Unchanged from ADR 0202: no names are opened, created or
  unlinked; only authenticated sessions can send; a dmabuf is a descriptor,
  passed as a memfd is. The GPU importing a buffer it does not own is the
  driver's business; a producer can only show its own session's pictures.
- **Producers, in order of value.** (1) The Browser engine with GPU
  compositing: WPE exports its frames as dmabufs. (2) Hardware-decoded video
  (VA-API to `NV12` dmabufs). Software video, Boxer and Kitty images stay on
  memfd, by design.

## Wire format, version 3

Added in Blit 0.3.60. Everything of ADR 0202's versions 1 and 2 holds
(socket, hello, token, uid checks, `FRM1`, `HUD1`, `STA1`, newest wins);
version 3 adds one frame message and one reply. Little-endian, fixed
offsets, one message per `recvmsg` (`SOCK_SEQPACKET`).

- **Hello**: as ADR 0202's, with version `3`. Answered, when the GPU
  compositor composes at least one output and EGL has
  `EGL_EXT_image_dma_buf_import` (and GL `GL_OES_EGL_image`):

  `OK dmabuf=1 fence=<0|1> formats=<list>\n`

  `fence=1` when an acquire fence is waited on in the GPU
  (`EGL_ANDROID_native_fence_sync` and `EGL_KHR_wait_sync`). `<list>` is one
  token: per importable format its four-character DRM fourcc, then `/` and
  each modifier it can be imported with as a 2D texture, in lowercase hex
  without `0x`; formats separated by commas, at most 16 modifiers each:
  `XR24/0/100000000000001,AR24/0,XB24/0,AB24/0`. Without
  `EGL_EXT_image_dma_buf_import_modifiers` the only modifier is `0`
  (`LINEAR`). Otherwise the answer is `OK dmabuf=0\n`, and the client
  sends memfds. Version 1 and 2 hellos are still answered `OK\n`.
- **Frame** (`FRM3`), 72 bytes, from a version 3 client only:

  | offset | size | field |
  |---|---|---|
  | 0 | 4 | magic `FRM3` |
  | 4 | 4 | image id, ≥ 1 |
  | 8 | 8 | sequence, rising per image id (shared with `FRM1`) |
  | 16 | 4 | width, 1–4096 |
  | 20 | 4 | height, 1–4096 |
  | 24 | 4 | DRM fourcc (`XR24`, `AR24`, `XB24`, `AB24`) |
  | 28 | 4 | plane count, 1–4 |
  | 32 | 8 | format modifier (`0` is `LINEAR`) |
  | 40 + 8·i | 4 | plane i offset, i = 0..3 |
  | 44 + 8·i | 4 | plane i pitch; planes past the count are 0 and 0 |

  `SCM_RIGHTS` carries exactly one descriptor per plane, in plane order, and
  optionally one more, the acquire fence (a `sync_file`): plane count or
  plane count + 1 descriptors. A frame is always whole; there are no
  rectangle edits.
- **Checks, in order.** Malformed -- wrong length, a version 1 or 2 sender,
  a descriptor count that fits neither rule, image id 0, width or height
  0, plane count outside 1–4, a plane field set past the count -- is
  `EINVAL`; over 4096 either way `EFBIG`. Then, if the compositor cannot
  import now, or the fourcc and modifier pair is not in the advertised
  list, `ENOTSUP`. Each plane's size is read with `lseek(fd, 0, SEEK_END)`
  (never mapped): plane 0 must hold `offset + pitch·(height−1) +
  4·width` bytes with `pitch ≥ 4·width`, any other plane (a modifier's
  auxiliary plane) must start inside its buffer; otherwise `EINVAL`. The
  image rules of ADR 0202 follow (a Kitty image `EINVAL`, a deleted id
  `ENOENT`, a full store `ENOSPC`). Finally the import:
  `eglCreateImageKHR(EGL_LINUX_DMA_BUF_EXT)` with the planes (and the
  modifier, when EGL takes one) and `glEGLImageTargetTexture2DOES` on the
  image's texture; a failure is `ENOTSUP`. The fence is always consumed:
  waited on with `eglWaitSyncKHR` (the GPU waits; the CPU never does), or
  closed when it cannot be, the dmabuf's implicit synchronisation then
  ordering the producer's writes.
- **Drawing.** The texture is drawn as any picture (placement, z-order,
  clipping and effects of the GPU compositor unchanged). Its colour is
  taken as premultiplied by alpha, the DRM and Wayland convention; the X
  formats are opaque. A frame the CPU has to compose (an effect the GPU
  cannot draw) shows the image's place empty; the CPU never reads a
  dmabuf.
- **Release** (`RELEASE <id> <sequence>\n`, Blit to client, no
  descriptor): Blit holds a frame's descriptors and EGLImage while it may
  still be sampled, and then destroys them and says so, to the connection
  that sent the image's newest frame:
  - a frame that was drawn is released after the compositor has presented
    a newer frame of the image (it was drawn in a frame already submitted;
    the GPU's own fences keep the producer's next writes behind that read);
  - a frame replaced before it was ever drawn, a frame superseded in a
    batch, and a stale one (a sequence not newer than the image's) are
    released at once;
  - deleting the image (`a=d` that frees it), the image being evicted or
    its session ending, a memfd frame for the image, and the GPU
    compositor stopping release what the image holds.
  So a producer with a pool of three buffers never waits for more than one
  frame. Refused frames are not released (the `ERROR` says the buffer is
  the producer's again).
- **The CPU compositor and a GPU fallback.** Under the CPU compositor the
  hello says `dmabuf=0` and every `FRM3` is `ENOTSUP`. If the GPU stops
  composing every output while dmabuf images are live, Blit releases them
  (each `RELEASE` sent), the images keep their ids and placements and show
  nothing, and later `FRM3` frames are `ENOTSUP` until the producer sends
  the image by memfd.
- **Diagnostics.** A session reports `frames.dmabuf`: `accepted`,
  `released`, and `refused` by reason (`unavailable`, `format`, `import`,
  `invalid`); an image `"dmabuf": true` while it shows one. `GET
  /v1/diagnostics` reports `graphics.frames.dmabuf`: `advertised`,
  `unavailable` (why not), `fences`, `formats` (the hello's list), the
  totals, and the same counts per session that sent any.

## Implementation status

Blit's half is in 0.3.60, RGB only:

- In: version 3 of the channel as above; the GLES import of `XRGB8888`,
  `ARGB8888`, `XBGR8888` and `ABGR8888` with the modifiers EGL lists; the
  acquire fence; release; the CPU compositor and fallback rules; the
  counts. The frame path still opens, creates and unlinks no name
  (ADR 0066): it seeks, imports and closes descriptors it was given.
- Gated locally (`frame_dmabuf_gate`, Hibr suite, under both compositors;
  the gate hosts' llvmpipe has no `EGL_EXT_image_dma_buf_import`): no
  offer and `ENOTSUP` under the CPU compositor and under a GPU compositor
  that cannot import, `EINVAL` and `EFBIG` for malformed frames, memfd
  frames on a version 3 connection, the counts. ADR 0202's gate is
  unchanged.
- Gated on hardware (`frame_dmabuf_live_gate`, run when
  `BLIT_GATE_LIVE_DMABUF` names a host): on the owner's Intel HD 515 test
  host (Mesa 25.0.7) a test producer (`tools/tests/frame_dmabuf_producer.c`,
  GBM `LINEAR` buffers on the render node, a pool of three) sent 90 frames:
  all imported, the newest held and every other released in sequence order
  as the next was shown, the newest released by `a=d`, an unlisted
  modifier refused `ENOTSUP`; the last frame's colour covered the image's
  whole area in a screenshot of the display, with no picture byte
  uploaded to the GPU meanwhile.
- Not yet: the acquire fence on hardware (the test producer sends none),
  the GPU-to-CPU fallback on hardware, a real producer (the Browser), and
  the measurement. **TODO: YUV** (`NV12`, `P010`) through
  `GL_TEXTURE_EXTERNAL_OES` and an external-sampler program, with a gate
  that needs a GPU, for hardware-decoded video. Hibr's console and hold do
  not yet pass dmabuf descriptors through.

This record stays proposed until a real producer (the Browser) sends
dmabuf frames and the ADR 0202 measurement is repeated with it.

## Consequences

- One channel, one set of rules: version 3 adds a frame type and a release
  reply, nothing else. Producers that do not use it are unaffected.
- The GPU compositor gains an external-texture path (YUV) and fence
  handling; the CPU compositor gains nothing and says so.
- Hibr's console and hold have to pass dmabuf descriptors through as they
  pass memfds now (hold keeps the last buffer per image for a late client,
  as ADR 0202 does).
- Testing needs a producer that allocates GBM buffers. The gates run the GPU
  compositor surfaceless on Mesa; a gate can allocate dmabufs from the same
  device, so the import path is testable without real hardware. Real
  zero-copy timing needs the owner's machine.

## Evidence (planned)

- A gate (`frame_dmabuf_gate`): a test producer allocates `ARGB8888` and
  `NV12` GBM buffers, sends them by version 3, and checks the screenshot,
  `ENOTSUP` on an unsupported format and under the CPU compositor, the
  release replies, newest wins and the ADR 0202 refusals.
- The ADR 0202 measurement repeated on the owner's machine with the Browser
  on dmabuf: Blit's CPU share, upload bytes (`gles_uploaded_rows`,
  `upload_ms` in `/v1/diagnostics`) and the producer's CPU, against the
  memfd numbers above.
- `GET /v1/diagnostics` reports dmabuf frames accepted, refused by reason,
  and released, per session.

This record stays proposed until the Blit half, the gate and one real
producer (the Browser) are in place and measured.
