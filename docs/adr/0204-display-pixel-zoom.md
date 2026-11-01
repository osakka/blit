# ADR 0204: pixel zoom per display

- Status: proposed
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

## Decision (proposed)

- **The setting.** `PATCH /v1/displays/{id}` takes `zoom`, a percentage:
  100 (off), 125, 150, 175, 200, 250, 300 or 400. `capabilities` lists
  `zoom` on every KMS display and `configuration` reports it. It is kept in
  the settings document's `displays` section under the connector name
  (`DP-1`), so it survives restarts and follows the monitor, and is applied
  before the first frame. This is the first thing that section keeps for an
  attached display.
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
  (x·100/zoom, y·100/zoom), integer division. At 200%, 300% and 400% every
  pixel becomes an even square; at 125%–250% some rows and columns repeat
  once more than others, which is the look of pixel zoom and identical on
  both compositors. GLES: the scene texture at the logical size, drawn to
  the output with that integer mapping in the shader (`texelFetch`), not
  the GPU's filtering. CPU: the scene buffer at the logical size, enlarged
  into the scanout buffer by a row-replicating copy over the damaged rows
  only.
- **Not zoomed:** the pointer and the performance panel are drawn after
  the enlargement, at their normal size, so the pointer stays a normal
  pointer and the panel stays readable. Screenshots
  (`/v1/diagnostics/screenshots`) are what is on the panel: physical.
- **Input.** The pointer moves in physical pixels, so it is as fast as on
  any other display; its position is converted to logical pixels for hit
  testing, mouse reports and pointer lock.
- **Damage.** A logical damaged rectangle becomes its enlarged physical
  rectangle; nothing else is repainted.

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
  row-replicating copy of the damaged area.

## Verification

- A renderer-suite gate, both compositors: at 200% a known pattern's pixel
  (x, y) appears as a 2x2 block; at 150% the row/column repeat pattern
  matches the formula; both compositors produce the same screenshot; the
  session's grid follows the logical size, and its `TIOCSWINSZ` pixels are
  columns × the unchanged cell width (rows likewise); the pointer's
  reported position is logical; the panel and pointer are not enlarged;
  100% is byte-identical to today.
- The display API gate: `zoom` in `capabilities` and `configuration`,
  invalid values refused (400 `invalid_display_zoom`), persisted per
  connector across a restart.
