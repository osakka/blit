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
