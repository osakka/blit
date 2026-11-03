# ADR 0204: pixel zoom per display

- Status: accepted
- Note: reviewed by the Hibr maintainers, 2026-11-01.
- Date: 2026-11-01
- Scope: displays API, settings, topology, both compositors, input mapping,
  screenshots
- Depends on: [ADR 0181](0181-gles-compositor-inside-native-kms.md),
  [ADR 0184](0184-sdl-is-removed.md)

## Context

The owner wants to magnify one monitor, everything on it, independently of
the font size, which is global. Two ways were put to them: drawing that
display's cells larger (crisp at any step), or composing the display at a
lower resolution and enlarging the image (pixel zoom). They chose **pixel
zoom**: the whole display, pictures included, magnified as an image.

## Decision

- **The setting.** `PATCH /v1/displays/{id}` takes `zoom`, a percentage:
  any multiple of 25 from 100 (off) to 400, so the Control Panel can offer
  it as a slider. `capabilities` lists `zoom` on every KMS display and
  `configuration` reports it. It is kept in the settings document's
  `displays` section under the connector name, the display's
  `state.connector` (`DP-1`), so it survives restarts and follows the
  monitor, and is applied before the first frame. The display's `name` is
  not that: native KMS names a display after its DRM object id ("KMS
  connector 93"), which is why 0.3.26, keying by `name`, was corrected
  (issue #103). Only a display with no connector name is keyed by `name`.
  An entry under the old `name` key is still read, and the next save
  writes it under the connector key and drops the old one. This is the
  first thing that section keeps for an attached display.
- **The logical display.** A zoomed display has a logical size of its mode
  divided by the zoom, rounded down to whole pixels: 2560x1440 at 200% is
  1280x720, at 150% is 1706x960. Everything above the compositor works in
  that size: the topology rectangle, the panes and their cell grids, Kitty
  and descriptor pictures, effects, the pointer's position and the mouse
  reports. To every program and to Hibr the display is simply smaller, and
  a change is a resize of that display's sessions (SIGWINCH), as a font
  change is, on that display only.
- **The winsize is logical.** Hibr's console takes the cell size as
  `ws_xpixel / cols` and `ws_ypixel / rows`, re-read on every SIGWINCH,
  and hold passes each client's pixel size through. So a zoomed display's
  sessions report `TIOCSWINSZ` in logical pixels: columns × the same cell
  width. Reporting the mode's pixels with fewer cells would make every
  cell read twice as big, and every picture and frame would be built at
  the wrong size. Nothing else is needed: SIGWINCH and a consistent
  winsize.
- **The image.** The compositor composes the display at its logical size,
  then enlarges it to the mode with **nearest-neighbour** sampling: the
  physical pixel (x, y) shows logical pixel
  (x·100/zoom, y·100/zoom), integer division, clamped to the last logical
  column and row: the logical size is rounded down, so where the mode is
  not a whole multiple of it the last physical columns or rows map one past
  it and show its last column or row again (1024 wide at 150% is 682
  logical columns; physical 1023 maps to 682 and shows 681). At 200%,
  300% and 400% every pixel becomes an even square; at 125%–250% some rows
  and columns repeat once more than others, which is the look of pixel
  zoom and identical on both compositors. GLES: the scene texture at the logical size, drawn to
  the output with that integer mapping in the shader, not the GPU's
  filtering: GLSL ES 1.00 has no `texelFetch`, so the shader floors the
  division in highp and samples the texel's centre with nearest filtering,
  which is exact at these sizes. A context without highp fragment floats
  composes a zoomed display on the CPU instead, with that reason
  reported. CPU: the scene buffer at the logical size, enlarged into the
  scanout buffer by a row-replicating copy over the damaged rows only.
- **Not zoomed:** the pointer and the performance panel are drawn after
  the enlargement, at their normal size, so the pointer stays a normal
  pointer and the panel stays readable. Screenshots
  (`/v1/diagnostics/screenshots`) are what is on the panel: physical.
- **Input.** The pointer moves in physical pixels, so it is as fast as on
  any other display; its position is converted to logical pixels for hit
  testing, mouse reports and pointer lock.
- **Damage.** A logical damaged rectangle becomes its enlarged physical
  rectangle; nothing else is repainted.
- **Effects** (revised 2026-11-03; 0.3.29 did not draw them zoomed). A
  zoomed display draws every effect it draws at 100%, the picture at 100%
  enlarged. The local effects (scanlines, corners) are drawn into the
  logical scene and enlarged with it. On GLES the presentation pass
  (curvature, glow, RGB shift, the glowing line, flicker, horizontal sync,
  jitter; ADR 0187, 0188, 0190) is the enlargement too: the plain copy is
  the zoom program over the whole frame, and each pane with presentation
  effects is drawn again over its physical rectangle by the presentation
  program, which knows the zoom. For each physical pixel it takes the
  logical pixel the zoom program shows there (the same expression, clamp
  included) and evaluates everything at that pixel's centre in the pane's
  logical rectangle -- the warp, the samples, RGB shift's distance,
  jitter's grid, the corners, the glass scanlines' rows, the line's band
  and the glow's blurred copy, which is built from the logical scene as
  before. So every effect is the 100% one, pixel-enlarged: each logical
  pixel an even block at 200%, nothing re-evaluated between them, no new
  smoothness and no thinner scanlines. The pass draws the glass scanlines
  and the still corners under sync or jitter on a zoomed display as it
  does at 100%. A pane without presentation effects is the zoom program's
  pixels exactly, and at 100% the program takes the screen pixel as it is:
  the same pixels as before, for one uniform branch per fragment. Nothing is reported
  unavailable for being zoomed; the CPU compositor still says `cpu
  compositor`. Clicks follow the warp in logical pixels, as the pointer's
  position is logical. The first decision -- no presentation effects on a
  zoomed display, reported `zoomed display` -- was a gap the owner did not
  want: a CRT look and a magnified display are independent choices.

## Consequences

- A zoomed display has fewer cells: at 200% a quarter of the area. On a
  held desktop that is one client resizing: hold resizes per client and the
  desktop re-reads its displays. Where windows that sat past the new
  logical edge end up is Hibr #409's shape; Hibr adds a check (two attached
  clients, one shrinks to half, its windows stay reachable, the other
  display is untouched).
- Text and pictures are enlarged pixels, blocky, and uneven at fractional
  zooms; that is the choice made. Video by descriptor and Kitty pictures
  are built at the window's logical size and enlarged with everything
  else, so a film on a 200% display looks blocky. To see a film in more
  detail, make its window bigger, not the display. A crisp zoom (larger cells per display) remains possible
  later as a separate setting.
- Cost: the enlargement is one textured quad on GLES; on the CPU a
  row-replicating copy of the damaged area. A pane with presentation
  effects on a zoomed display costs its presentation quad over its physical
  rectangle, as on an unzoomed display of that mode, with the zoom
  program's fragments under it also run.

## Verification

- A renderer-suite gate, both compositors: at 200% a known pattern's pixel
  (x, y) appears as a 2x2 block; at 150% the row/column repeat pattern
  matches the formula; both compositors produce the same screenshot; the
  session's grid follows the logical size, and its `TIOCSWINSZ` pixels are
  columns × the unchanged cell width (rows likewise); the pointer's
  reported position is logical; the panel and pointer are not enlarged;
  100% is byte-identical to today.
- The zoomed effects gate (`zoomed_effects_gate.rb`, GLES, renderer
  suite): display-1 1024x768 at 200% beside display-2 512x384 at 100%, the
  same stripes and the same curvature, RGB shift, scanlines and corners on
  both. Every physical pixel of display-1 (x, y) is display-2's (x / 2,
  y / 2) within 2 -- the effects are drawn, in logical coordinates; every
  2x2 block is one colour exactly; the tube's outside is black and stripe
  edges fringe; the glowing line ticks and moves on the zoomed display and
  stays in 2x2 blocks; a click on the curved zoomed pane reaches the cell
  the warp shows; `presentation_unavailable` is empty; back at 100% the
  frame with effects is byte-identical to the one before zooming. Against
  0.3.29 it fails (about 392,000 pixels, half the frame, are not the
  enlarged 100% picture), and its 100% capture with effects is
  byte-identical between 0.3.29 and the fix.
- The display API gate: `zoom` in `capabilities` and `configuration`,
  invalid values refused (400 `invalid_display_zoom`), any step of 25
  accepted, persisted per connector across a restart, and an entry under
  the old `name` key read and migrated to the connector key by the next
  save.
