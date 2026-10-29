# Kitty comparison: rendering architecture, not just glyph caching

Follow-up: the0.2 development candidate now implements the first shared
placement-damage stage in[ADR0177](../adr/0177-retained-graphics-damage.md).
The source findings and original probe outputs below describe the pinned
pre-fix checkpoint; the probe itself was removed with SDL in
[ADR 0184](../adr/0184-sdl-is-removed.md). Image uploads, CPU native composition
and whole-output copying are not claimed fixed by that stage. See current-state/verification notes.

Date: 2026-10-21. Blit source checkpoint: c5e241e, development0.2.0.
Live host remains0.1.589/native direct-KMS, untouched by this investigation.
Parent tracker: internal Blit122; architectural follow-up125. This is an investigation and proposed
implementation sequence, not an accepted GPU rewrite or a performance win.

## Question and boundary

The owner reports smooth, fast dragging of graphical windows playing YouTube
inside Kitty on a high-resolution display, with very low CPU. We proceed on
the assumption that these are Hibr application windows **inside** Kitty, not
the OS compositor moving Kitty's native window. That distinction needs
confirmation. Application/Chrome decode and compositor/GPU work must be
included in a matched comparison; low terminal CPU alone is not total cost.

The previous glyph-cache improvement removes real repeated work but does
not explain or solve the entire graphics/video gap. The important question
is whether the CPU is rebuilding pixels, or submitting GPU resources and
placement geometry. At this checkpoint the native and SDL paths differed
materially (SDL, removed in ADR 0184).

## What Kitty actually does

Source is pinned at
[`aef2bc23065412e95dbcdaf048c233e4537d444a`](https://github.com/kovidgoyal/kitty/tree/aef2bc23065412e95dbcdaf048c233e4537d444a).
Architectural observations, not Kitty measurements on our host:

- [`graphics.c`](https://github.com/kovidgoyal/kitty/blob/aef2bc23065412e95dbcdaf048c233e4537d444a/kitty/graphics.c):
  `handle_put_command` updates image-reference geometry separately from image
  storage. `grman_update_layers` uses dirty state, builds ordered references
  and groups them by image/program compatibility. `upload_to_gpu` handles new
  pixels. Moving an existing placement need not reload its image data.
- [`shaders.c`](https://github.com/kovidgoyal/kitty/blob/aef2bc23065412e95dbcdaf048c233e4537d444a/kitty/shaders.c):
  `draw_graphics` binds retained textures, sets source/destination rectangles
  and draws GPU quads. Cell rendering uses GPU cell buffers/instanced
  attributes; `call_cell_program` submits cells rather than CPU-blending each
  glyph pixel. `send_image_to_gpu` accepts RGB/RGBA data directly. It uses
  `glTexImage2D` here: do **not** invent a claim that all video updates use
  partial uploads or zero-copy decoding.
- [`child-monitor.c`](https://github.com/kovidgoyal/kitty/blob/aef2bc23065412e95dbcdaf048c233e4537d444a/kitty/child-monitor.c):
  PTY I/O has a dedicated thread. Rendering uses dirty/prepared state,
  repaint timing and available monitor-frame callbacks. This is not proof
  that all parsing, image decoding or rendering is off the main thread.

Kitty can redraw a scene cheaply on the GPU without sophisticated CPU damage
minimization being the sole reason for low CPU. Its image/frame loading and
some animation composition still involve CPU work. Its particular pipeline
must not be replaced with a mythical fully zero-copy video renderer.

## Confirmed Blit differences

| Boundary | Existing Blit behaviour | Consequence |
| --- | --- | --- |
| Kitty placement-only command | `terminal_apply_kitty`, action `p`, calls `mark_surface_full_damage` even when pixel generation is unchanged | Small moves can invalidate all terminal cells on both rendering paths |
| Completed image upload | Kitty completion and sixel completion mark the entire surface damaged | A new small video frame can trigger whole-surface composition |
| Native graphics invalidation | `graphics_bounds_for_view` unions **all** visible image placements; previous/current unions are included on scene changes | A retained full-pane wallpaper makes the graphics damage bounds full-pane |
| Native graphics composition | `draw_graphic` loops over destination pixels, including conversion, scaling and alpha blending on the CPU | Cached source images still cost CPU composition work on redraw |
| Native scanout preparation | `copy_scene_to_back` copies every display row for a scene submission | Full-output memory copy even after a small scene update |
| Event loop | Input service, PTY parsing/image decode and `render_current_surfaces` run in the main loop | Expensive decode/render can delay subsequent input/API service; no causal latency measurement yet |

See `src/blit_server.c`: `terminal_apply_kitty`, `terminal_apply_sixel`,
`render_current_surfaces`, `service_session_output`,
and `main`; and `src/blit_kms.c`: `graphics_bounds_for_view`, `draw_graphic`,
`draw_view`, `copy_scene_to_back` and the graphics-scene-change damage branch.
Native KMS already uses DRM page flips: do not diagnose tearing as simply
"missing vsync". Deadline misses, state coherence and presentation latency
must be investigated separately.

## Reproduced with production functions

The bounded, local-only TCC probe `graphics_work_shape.rb` (removed in ADR
0184) included production functions rather than reimplementing their logic.
It used ordinary memory, never a DRM device, Hibr, live API input or saved
settings. Source digests accompanied the report.

Observed at a model1920x2160 view,8x16 cells:

- One placement-only `a=p` command for an existing320x180 image leaves its
  pixel generation unchanged, but sets damage to all240x135 cells.
- A full-view wallpaper plus that small image produces all-graphics bounds
  of240x135 cells:4,147,200 pixels versus57,600 pixels for the small image,
  a72x area ratio. This does not assert that the minimal required damage for
  a move is only its new rectangle: old coverage and affected layers matter.
- Production scene-copy code copies16,588,800 bytes, including far corners
  outside the small image. At a hypothetical60 scene submissions/s, that is
  995MB/s of destination writes **plus** corresponding source reads, before
  composition. This is arithmetic, not measured host bandwidth or FPS.

Fresh read-only live observation:589/native direct-KMS,1920x2160@59.997Hz;
last-frame draw25.937ms,copy0.854ms,submit0.042ms,16,856 drawn cells.
Workload was uncontrolled. Drawing dominates this sample; do not present
the full-frame copy as its measured dominant bottleneck or report these as
a controlled YouTube/drag benchmark.

## Revised sequence: fix the work model first

1. Establish deterministic replay cases: move unchanged image placements;
   replace video pixels while stationary; combine video and movement; repeat
   with/without wallpaper, text, transparency, multiple panes and outputs.
   Record visual oracles and workload provenance before changing behaviour.
2. At the canonical scene boundary, separate pixel-content changes from
   placement/visibility/crop/z changes. Blit already has pixel generation;
   use it correctly. Track each changed placement's old/new coverage and
   affected layers, not a blanket full-surface invalidation. Deletion,
   same-id reuse, z changes, overlapping/alpha images and synchronized output
   must preserve exact composition. Do not just remove full-damage calls.
3. Make accelerated rendering a retained GPU scene: unchanged images stay
   resident; movement changes geometry; video changes pixel resources;
   text updates change cell/glyph data. Extend the KMS GLES compositor rather
   than creating another parallel GL terminal. Native CPU fallback
   correctness remains required, but cannot be promised Kitty-class low CPU
   for high-resolution video without hardware-specific evidence.
4. Remove avoidable CPU image repacking, instrument image bytes decoded,
   copied and uploaded, and measure preparation/submission/GPU completion.
   Preserve premultiplication, colour, pitch, clipping, image lifetime and
   atomic frame boundaries. No speculative PBO/threading rewrite first.
5. Quantify input/decode/render blocking and queue age under continuous
   high-rate motion plus video. Keep one state owner and bounded queues;
   introduce workers only if profiling justifies them and ownership is
   specified. Never discard arbitrary terminal bytes to "keep up".
6. Validate on the real host using a controlled recorded workload and then
   the actual Hibr/Chrome workload, with matched Kitty geometry/mode and
   producer/hold behaviour. Measure CPU/GPU work, p95/p99 input-to-present
   latency, unique content frames, missed deadlines and visual correctness.
   Source architecture is not a measured end-to-end explanation by itself.

The current glyph fix stays useful but is not the primary explanation for
the reported video/window experience. No renderer/backend was switched, no
live session was restarted and no Hibr source was modified here.
