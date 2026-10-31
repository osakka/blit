# ADR 0181: a GLES compositor inside native KMS; SDL retired at cutover

- Status: accepted; in use since 0.2.9
- Date: 2026-10-24
- Scope: presentation backends, GPU composition, test oracles, packaging.
  Decides the open mechanism/ownership question ADR 0180 leaves to internal 128.

## Context

The live host presents through native direct KMS (`/v1/diagnostics`:
`health.backend: drm-kms`, `render.direct_kms: true`), the CPU compositor in
`src/blit_kms.c`. A read-only 10 s sample of an idle desktop on 0.2.8 shows the
cost of that path growing with the area repainted: typical frames draw in
0.5 ms, but 1,188 cells took 12 ms, 3,652 cells 27 ms and 5,940 cells 40 ms,
about 7 µs a cell, against a 16.67 ms budget at 60 Hz; 132 deadlines were
missed in the window. A full 1920x2160 screen is 32,400 cells. Window drags and
video are exactly the large repaints, and CPU blending cannot be tuned under
the budget for them.

The planned answer (internal 126–135, ADR 0180) is retained GPU composition on
the SDL/GL path. Three facts argue against building it on SDL:

- `SDL_Renderer` exposes textures and `SDL_RenderGeometry`, not persistent
  vertex/instance buffers. Retained cell state of the kind Kitty uses is not
  available through it, which is internal 128's open feasibility question.
- On the live GLES2 host, drawing incrementally into a retained SDL render
  target did not keep its contents (ADR 0099); the ping-pong copy that fixed
  it is the opposite of retention.
- SDL's own KMSDRM backend faulted on the host with two outputs
  (`add_plane_property`, SDL_kmsdrmvideo.c:248, deployed 571). Native KMS
  already owns modes, page flips, hotplug and multiple outputs.

Verified on 2026-10-24 in the build environment:

- **Headless GLES works.** A tcc-built program created an EGL 1.5 context on
  Mesa's surfaceless platform, rendered to a framebuffer object and read back
  the expected pixel (255,0,0,255): llvmpipe, OpenGL ES 3.2 (system Mesa
  22.3.6), and softpipe, OpenGL ES 3.1 (`build/deps` Mesa 26.2.3).
- **tcc can build it without new link dependencies.** The real Khronos and GBM
  headers are already in `build/deps/prefix/include/{EGL,GLES2,KHR}` and
  `gbm.h`; `libEGL.so.1`, `libGLESv2.so.2` and `libgbm.so.1` are loaded with
  `dlopen`, as SDL does today.
- **SDL is contained.** Every SDL call is in `src/blit_server.c` (62 distinct
  functions). `blit_kms.c`, `blit_vt.c`, `blit_raster.c` and `blit_font.c`
  have none. Input is evdev; SDL input is used only in windowed developer
  mode.

## Options considered

1. **Retained GPU composition through SDL_Renderer** (ADR 0180 as written).
   Keeps today's structure, but cannot hold persistent cell buffers, carries
   the ADR 0099 render-target hazard, and leaves display ownership with SDL's
   KMSDRM code that faults with two outputs.
2. **Raw GL alongside SDL_Renderer.** Forbidden by internal 128 and ADR 0180:
   two owners of one GL context's state.
3. **A GLES compositor inside native KMS, with SDL retired once it is proven.**
   Native keeps the display (DRM master, modes, flips, hotplug, outputs) and
   gains a second compositor that draws with GLES into GBM buffers it flips
   with its existing code. One GL owner, full control of buffers.
4. **Strip SDL first, then build GLES.** Removes the pixel source for 77 of the
   210 gates before a replacement oracle exists; the new compositor would be
   written blind and judged by itself.

## Decision

Choose option 3, sequenced so that nothing is removed before its replacement
passes.

**Ownership.** Native KMS is the only presentation backend. It has two
compositors over the same `BlitKmsSurfaceView` input and the same canonical
terminal, graphics store, geometry and settings:

- **CPU** — today's `draw_view`/`blit_kms_present`. It stays the default and
  the vanilla baseline, and is the reference oracle.
- **GLES** — opt-in, selected the same way the backend is today (saved
  settings or environment, no new competing writable preference). If EGL, GBM
  or context creation fails at startup or on context loss, the output falls
  back to CPU and says so in `/v1/diagnostics`.

**GPU-first, not a CPU port.** The GLES compositor keeps state on the GPU
between frames: a glyph atlas texture, persistent per-surface cell buffers
updated only for changed ranges (instanced cells), and image textures keyed by
image identity and pixel generation (internal 132). A frame is composed from
those resources; the CPU describes changes, it does not rebuild rows.

**One composition contract for both compositors**, as fixed by
`kms_compose_gate` and `sdl_graphics_band_gate`: default-background floor,
images with z below -2^30 under cell backgrounds, background quads only for
non-default cells (explicit black is opaque), images with -2^30 <= z < 0 over
backgrounds and under text, glyphs, cursor, images with z >= 0, overlays and
effects, pointer (shown art, OSC 22 hidden state). Glyph overhang, wide cells
and combining marks follow ADR 0179's exact raster rules.

**The oracle.** The CPU compositor is driven headless (memory buffer, DRM
mocked, as `kms_compose_gate.c` already does) and is the reference. The GLES
compositor renders the same views into a framebuffer object on Mesa's
surfaceless platform and must match it per pixel within ±1 per channel, the
tolerance `pointer_pixel_gate` already uses for blending rounding. Any other
difference is a failure, never a mask.

**Order.**

1. **Headless oracle.** Grow the `kms_compose_gate.c` approach into a harness
   that covers what the SDL screenshot gates cover today (internal 127's
   deliverable), and make `/v1/diagnostics/screenshots` serve BMP from the
   native readback, so the Ruby gates can run against native without SDL.
2. **GLES bring-up.** EGL on GBM for the host, surfaceless for tests; clear,
   flip, and context-loss fallback on one output. Opt-in.
3. **Retained text and images** (internal 131/132) on the GLES compositor,
   each step checked against the oracle.
4. **Composition** (internal 133): the full layer order on GLES.
5. **Cutover**, when every criterion below holds. Then remove SDL.

**Cutover criteria.**

- Every gate migrated off SDL passes on both compositors.
- On the host, a single-output GLES canary runs the same workload the native
  baseline was sampled with, and beats it: the bar is the 0.2.8 native sample
  (27 ms for 3,652 cells, 40 ms for 5,940 cells, 132 missed deadlines in 10 s).
  The exact workload and probe are fixed by internal 127/126 before measuring.
- Two outputs are verified on GLES, or explicitly deferred in the ledger.
- `/v1/displays` and the screenshot endpoint are unchanged in schema.

**Removed at cutover** (each with its replacement):

| SDL use | replacement |
|---|---|
| `SDL_Renderer` drawing, ping-pong scene, glyph/graphics texture caches | GLES compositor |
| `SDL_GetDisplays`, `SDL_GetDisplayBounds`, `SDL_GetCurrentDisplayMode`, `SDL_GetFullscreenDisplayModes` (feeding `/v1/displays`) | native enumeration (`blit_kms_display_count`/`_info`), already used when `direct_kms` |
| `SDL_RenderReadPixels`/`SDL_SaveBMP` screenshots | native readback (`blit_kms_screenshot`), written as BMP |
| dummy video driver for gates | headless CPU oracle and surfaceless GLES |
| windowed developer mode, SDL mouse events | removed; evdev remains the input path |
| vendored `libSDL3` (10.7 MB in the package), `build/sdl3-kms`, SDL include path | removed from `build-tcc.sh` and `build-deb.sh` |

## Refinement, 2026-10-24: steps 3 and 4 are one compositor

Text cannot move to the GPU on its own: glyphs sit between the under-text
and over-text image bands and under the cursor, overlays and pointer, so a
GPU text layer over a CPU frame breaks every z >= 0 element. The GPU
therefore composes a whole view in `draw_view`'s order, and an output's
frame (scene, overlays, pointer). Each output keeps a persistent GPU scene,
as the CPU keeps its RAM scene: changed views are recomposed into it, held
and unchanged views keep their pixels.

Glyph sprites are produced by the CPU compositor's own `draw_cell_glyphs`,
rendered once white over black and once white over white: that pair gives
exact coverage, or premultiplied colour and alpha for colour emoji, with
italic, bold, wide cells and marks applied. One glyph policy serves both
compositors.

Per frame and per output, a frame the GPU cannot compose yet (an effect, a
font whose ink overhangs its cell, a scaled image) is composed by the CPU
and uploaded (step 2); the next CPU frame after GPU frames is a full redraw.
`gles_compose_gate` compares the two compositors pixel by pixel within 1 per
channel.

## Consequences

- Supersedes ADR 0137's three first-class paths: one backend, two
  compositors. Supersedes ADR 0180's mechanism question; its "no third GL
  backend" rule still holds, because SDL/GL is retired, not kept beside.
  ADR 0099 becomes historical. ADR 0153's presentation trial and ADR 0155's
  SDL hint stay in use until cutover, to measure SDL before it goes.
- No GL code paths outside `blit_kms.c` and its GLES unit; no raw GL mixed
  with another renderer's state.
- Native gains a dependency on Mesa's EGL/GBM/GLES at runtime, loaded with
  `dlopen`, so a host without them still runs the CPU compositor.
- Until cutover the SDL path is maintained only to keep its gates green; it
  gets no new features.
- Internal 128 records this as its decision; 131/132/133 target the GLES
  compositor; 129/130 (SDL startup/hotplug) are closed as superseded once the
  owner accepts this ADR.

## Limitations

- The build environment has no `/dev/dri`, so tests use Mesa's surfaceless
  platform. The GBM path the host uses (render node, scanout buffers, flips) is
  testable only on the host. This is the gate-versus-deployment gap
  `development-workflow.md` warns about; host canaries remain required.
- llvmpipe/softpipe prove correctness, not speed. Performance claims come only
  from the host.
- Nothing here switches the live backend. Each host trial is explicit and
  reversible.

## Evidence

- Native 0.2.8 baseline: `tools/probes/performance_baseline.rb 10` against the
  host, 2026-10-24 (figures above).
- Headless GLES probe: tcc build against `build/deps/prefix/include`,
  `dlopen` of `libEGL.so.1`/`libGLESv2.so.2`, surfaceless context, FBO
  readback 255,0,0,255 on llvmpipe (GLES 3.2) and softpipe (GLES 3.1).
- SDL containment: SDL symbols occur only in `src/blit_server.c`.
- The owner accepted the decision on 2026-10-24. Evidence that the
  implementation meets it is the cutover criteria above.

## Revised (0.3.11): the GPU is the default

The owner, 2026-10-29: Blit should use the GPU when there is one, and the
CPU only when it is chosen. An unset `BLIT_KMS_COMPOSITOR` (and no saved
`server.kms_compositor`) now means `gles`; `cpu` chooses the CPU. Nothing
else changes: a GPU that cannot start, or a GPU start that crashed last
time, still composes on the CPU, with the reason in `/v1/diagnostics`. The
test harnesses name `cpu` explicitly, so the CPU compositor stays the one
the default suites measure, and `BLIT_GATE_BACKEND=gles` the GPU.
