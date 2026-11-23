# ADR 0242: A program scale per display, pixels on the wire

- Status: accepted
- Note: The owner's decisions, 2026-11-21, through hibr. Landing in steps:
  step 1 (the layout's scale word, the outputs' scale and logical size, X
  told 1), step 2 (sizes, units and fractional scale) and step 3 (a scale
  that changes under a shown window, applied at the program's
  acknowledging commit) shipped in 0.3.130 (commits a666ab89, a387c07f,
  2ae2ea1b). Not built yet: the filtered (bilinear) shrink of a frame
  picture drawn smaller than its source (pictures are still sampled
  nearest), X's own scale (`WINDOW xscale`, `Xft.dpi`, the XSETTINGS
  manager: X is still told 1) and each display's physical size
  (`physical_mm`). The real-program checks its Evidence asks for are not
  recorded either.
- Decided: the program scale is Hibr's, a fixed "Program Scale" row per
  display (Control Panel > Displays, default 100%, set by hand, never
  derived from the cell size: a font change does not move it); X programs
  have one scale of their own, Hibr's "X Programs Scale" row; Blit reports
  each display's physical size in millimetres.
- Date: 2026-11-21
- Scope: `src/blit_compositor.c` (outputs, `wl_surface`, the window
  lines, input, the X window manager), `src/blit_compositor_wire.h`
  (`WINDOW layout`), `tools/build-tcc.sh` (fractional-scale-v1), the
  picture draw of `src/blit_gles.c` and `src/blit_kms.c` (frame pictures
  drawn smaller than their source), `docs/api.md` "Program windows",
  `docs/programs-in-windows.md`. Amends ADR 0229's Scale row. Hibr: the
  Control Panel's Displays pane and `dt_natlayout`.

## Context

Every `wl_output` blit-compositor offers says scale 1, `wl_surface` is
never told a preferred scale, and `wp_fractional_scale_v1` is not offered
(Gitea #170). On a 4K panel a GTK, Qt or Firefox window draws its widgets
at 1:1: a button a quarter of the area it is on a 1080p panel. Blit's own
text is not affected -- its font size is in pixels and the person sets it
-- so programs are the odd ones out.

Since 0.3.124 the compositor already honours what a program sends: a
buffer scale and a viewport become `WINDOW view`, and the desktop draws
that rectangle of the buffer at that size. What is missing is the other
half: telling programs what scale to draw at, and sizing their windows
for it.

Three things are fixed and this decision keeps them:

- **The desktop sizes windows in pixels.** Hibr computes a window's size
  from its cells and the cell size (`dt_natsize`: `w * DT_NATCW`), and its
  layout of displays in the same pixels (`dt_natlayout`). These are Blit's
  logical pixels: on a zoomed display (ADR 0204) the zoom's, which Blit
  then enlarges.
- **Every offset on the wire is a surface pixel of the top-level** (ADR
  0229, "The owners"). At scale 1 a surface pixel and a desktop pixel are
  one; at any other scale they are not, and one of them has to be the
  wire's.
- **X programs live in X space, and X space is the desktop's pixels**
  (0.3.88: each display one RandR output at its place and size). Steam,
  Proton and every game that sizes itself to its monitor depend on it.

What the shipped software does, checked on this host and in the Debian 13
release environment (Evidence):

- libwayland-server 1.23.1 and wayland-protocols 1.44: `wl_compositor` v6
  (`wl_surface.preferred_buffer_scale`, an integer), `wp_viewporter` and
  `wp_fractional_scale_v1` (staging, since 1.31: the build's minimum
  already) are all there.
- Programs whose binaries carry `wp_fractional_scale_manager_v1` and
  `wp_viewporter`: GTK 4.18.6, Qt 6.8.2's Wayland client, Firefox ESR 153,
  Chromium 156, SDL 2.32.4, SDL 3.2.10, mpv 0.40. That shows the protocol
  compiled in, not used by default: SDL, for one, is expected to use it
  only for windows that ask for high DPI. GTK 3.24.49 carries neither: it
  draws at an integer scale from the `wl_output` it is on, and its X
  backend carries the names `GDK_SCALE`, `Gdk/WindowScalingFactor` and
  `Xft/DPI`.
- Xwayland 24.1.6: `-hidpi` exists but is rootful only ("adjust to output
  scale when rootful"); Blit runs it `-rootless`. Its RandR outputs take
  their size from `zxdg_output_v1`'s logical size when there is one (the
  compositor's outputs section, from `xwayland-output.c`). It binds
  `wp_fractional_scale_manager_v1` and `wp_viewporter` too.
- Blit draws a picture placed at a size other than its source by nearest
  sampling, on both compositors (`blit_gles_texture_create` and
  `blit_gles_dmabuf_import`: `GL_NEAREST`; `blit_kms.c`'s image draw:
  integer source index). A buffer shrunk by 0.75 drops every fourth
  column.

## Options considered

1. **Where the scale lives.**
   a. A per-display program scale in Hibr, a Control Panel row, said to
      the compositor on `WINDOW layout`. Hibr already owns the displays as
      programs see them (ADR 0229: their names, places, refresh and the
      primary) and every window size; the scale is one more fact of that
      same layout.
   b. A Blit API setting, `PATCH /v1/displays/{id}` `scale`, beside
      `zoom`. Blit owns modes and zoom, but the compositor does not speak
      Blit's API (a desktop the login manager starts has no API token:
      `dt_nathz`), the scale changes nothing Blit draws, and Hibr would
      still have to read it to size windows: two readers of one fact.
2. **Which pixels are the wire's.**
   a. The desktop's pixels (display pixels): every line keeps its numbers
      at scale 1, and at scale S the compositor converts both ways. The
      desktop stays scale-blind.
   b. The programs' logical pixels: Hibr would have to know each window's
      scale to place popups and subsurfaces, convert its own cell sizes,
      and Blit's pointer mapping would change.
3. **Integer or fractional.**
   a. Integer only (`wl_output.scale`, `preferred_buffer_scale`): 150%
      means drawing at 2 and shrinking by 0.75 for everyone.
   b. Fractional (`wp_fractional_scale_v1` with `wp_viewporter`) for the
      programs that take it, integer for the rest, the programs that know
      neither enlarged.
4. **X programs.**
   a. Xwayland told the scale (logical outputs): X space shrinks to
      pixels / S, every X window is drawn at logical size and enlarged.
      Blurry, and a game asking for its monitor's size gets the logical
      one.
   b. Xwayland told scale 1 and the real pixels, as now, and X programs
      told the scale the way X has always told it: `Xft.dpi` and
      XSETTINGS.

## Decision

Options 1a (decided by the owner), 2a, 3b, 4b; X's scale is its own
setting (decided), not the layout's largest.

**The setting (Hibr's, proposed).** Each display has a program scale, a
percentage: 100 (the default), 125, ..., 400, steps of 25 -- the steps
ADR 0204's zoom uses, and each one an exact number of 120ths, the unit
`wp_fractional_scale_v1` speaks (125% is 150/120). Hibr keeps it with its
display, offers it as a Control Panel > Displays row, and says it as a
seventh word on that display's `WINDOW layout` line. Nothing on Blit's API
changes. The compositor keeps no copy beyond the layout it was last sent.

**Pixels on the wire.** Every number on the frame channel stays a
desktop pixel, as at scale 1: `WINDOW size`, `limits`, `geometry`,
`popup`, `sub`, `view`'s shown size, `menu`, `dragicon`, the `CLIP`
drag and drop points, and the `INPUT` positions. The compositor converts
at the wire, each way, with the scale of the window's display:

- out: a logical edge `e` is `round(e * S)`, half away from zero -- edges,
  not sizes: a subsurface's width is `round((x + w) * S) - round(x * S)`,
  so neighbours tile with no gap or overlap (rounding positions and sizes
  apart leaves a one-pixel seam in 19% of placements: Evidence);
- in: a desktop pixel `p` is `p / S` in `wl_fixed`, so the pointer keeps
  the display's whole precision.

A window's whole tree (its popups, subsurfaces and drag icon) uses its
top-level's scale. X windows always use 1.

**What programs are told.** For a display at S:

- `wl_output.scale` is `ceil(S)`; `wl_output.mode` stays the display's
  pixels; `zxdg_output_v1` gives its logical size, `round(w / S)` by
  `round(h / S)`, at its place in the desktop's pixels (the layout's
  numbers; with mixed scales the logical extents leave gaps between
  displays, which nothing a program in a Hibr window uses);
- each surface on it gets `wl_surface.preferred_buffer_scale` `ceil(S)`
  (`wl_surface` v6) and, if it asked, `wp_fractional_scale_v1.preferred_scale`
  `S * 120`;
- the client that is the compositor's own Xwayland is told 1 on all of
  these and the display's pixels as its logical size, whatever S is.

**Window sizes.** `WINDOW size W H` configures the smallest logical size
whose pixels cover it: `w = ceil((2W - 1) * 60 / n)` for a scale of
`n`/120, so `round(w * S) >= W`. The rule is exact at integer scales on
even multiples and otherwise over by at most `ceil(S) - 1` pixels (one up
to 200%, three at 400%: Evidence), and a program's own size said back to
it (Hibr's "own size", Gitea #508, which reads the shown size) configures
that same size again -- plain `ceil(W / S)` changes it by one in 22,500
cases of 65,000. The pixels over are cut from the right and bottom: the
window's `WINDOW view` crops to `W x H`, or its `WINDOW geometry` does
when it has one. A window is never resampled to absorb rounding. `0 0`
stays "the program chooses".

**What Blit draws for a buffer.** Per kind of program, at S on a body of
W x H desktop pixels:

| Program | Buffer | Shown |
|---|---|---|
| Fractional (those that use it: expected GTK4, Qt6, Firefox, Chromium, mpv; SDL for high-DPI windows) | `round(w * S)` with viewport destination `w` | 1:1, no `WINDOW view` (or a crop of the rounding) |
| Integer (GTK3, older) | `w * ceil(S)` at buffer scale `ceil(S)` | 1:1 at integer S; at fractional S shrunk by `S / ceil(S)` (0.625-0.875), `WINDOW view` |
| Neither | `w` at scale 1 | enlarged by S, `WINDOW view` |
| X (Xwayland) | the window's pixels | 1:1, always |

A buffer within `ceil(S)` pixels of its shown size whose program drew it
for this scale (a fractional object, a viewport destination, or a buffer
scale of `ceil(S)` at integer S) is shown 1:1 and cropped, never scaled by
a pixel: the spec leaves subsurface rounding to the program, and a
one-pixel stretch of Firefox's page doubles a column of text. For a
top-level that comes up short of its body (a program rounding otherwise
than the protocol says for top-levels), the picture is that much smaller
than the body, and today Hibr scales a mismatch to fit; drawing it 1:1 at
the body's top left instead is asked of Hibr (open).

The shrink is the one real resampling, and Blit draws it filtered: a
frame-channel picture (`FRM1`, `FRM3`; a new mark on `BlitGraphicsImage`
where those frames land, since today only `dmabuf` tells an `FRM3` apart
and an `FRM1` is `pixels` as a Kitty picture is) drawn smaller than its
source rectangle is sampled bilinearly, on the GPU with `GL_LINEAR` for that
draw and on the CPU compositor with the same formula. At ratios of 0.5
and up, which is every ratio this produces, bilinear touches every source
pixel. Enlargement stays nearest, the look ADR 0204 chose; Kitty pictures
and every other placement are unchanged.

**Pointer and cursors.** Blit maps the pointer into the picture's
desktop pixels through `INPUT view` as since 0.3.125; the compositor
divides. Relative motion stays the device's counts (ADR 0204: relative
motion is physical; a game's mouse look must not slow at 200%). Cursors
are Blit's art by name (ADR 0229, wp_cursor_shape_v1 and X's names), drawn
at Blit's pointer size in physical pixels, so the scale changes nothing
there; cursor images (`wl_pointer.set_cursor` surfaces) are still never
drawn, so their buffer scale has no meaning here.

**X programs.** X space stays the desktop's pixels: RandR outputs,
monitors, the root, the primary, and every X window 1:1. X has one screen
and so one scale. Not the primary's: Hibr's X primary is, by default, the
display the person is working on (`DT_NEWON=working`: `dt_natprimary`
sends `WINDOW primary` whenever the focus moves to another display), so a
scale tied to it would change under X programs as the focus crosses
displays. X takes **its own scale**, Hibr's "X Programs Scale" row, sent as `WINDOW xscale <pct>` (decided by the owner; the layout's largest was proposed): it changes
only when the person sets a scale or a display comes or goes. (Open with
Hibr: a scale of X's own, or its main display's.) The compositor, as X's
window manager, says it the two ways X toolkits read:

- `Xft.dpi: <96 * S>` in the root's `RESOURCE_MANAGER` (merged into what
  is there; nothing in Blit or Hibr sets it today) -- expected to be read
  at start by Xft, Qt and Chromium-based programs;
- an XSETTINGS manager (`_XSETTINGS_S0`) with `Gdk/WindowScalingFactor`
  `floor(S)`, `Gdk/UnscaledDPI` `96 * S / floor(S) * 1024` and `Xft/DPI`
  `96 * S * 1024` -- the names GTK 3's X backend carries; the values are
  the split GNOME's settings daemon is known to use (an integer window
  scale, the rest as text DPI), and GTK 3 is expected to take a change
  live. If another client already owns `_XSETTINGS_S0`, the compositor
  leaves it and says so in its log.

The toolkits' reactions are expectations until Step 5's gate (the
properties) and Step 7's real programs (what they do with them) show
them. An X window on a display of a lower scale is drawn at X's scale,
unresampled: larger there than its Wayland neighbours.

**A scale that changes.** A new layout naming another scale for a display
is told to every client on it -- `wl_output.scale` and `done`,
`zxdg_output_v1`'s size, each surface's preferred scales -- and each
Wayland top-level on it is configured again at its last `WINDOW size`
with the new rule, by the compositor itself: the desktop's pixels did not
change, so neither side's repeat filter would pass a `WINDOW size` (the
compositor's since 0.3.87, Hibr's `sent`). Until the program's commit that
acknowledges that configure, its tree is shown with the old scale, so an
old frame never jumps; at that commit every line of the tree in pixels
(`geometry`, `limits`, `popup`, `sub`, `view`) is said again, before its
frame. A window the desktop moves to a display of another scale (`WINDOW
output`) is the same change for that window. For X, the XSETTINGS values
and `Xft.dpi` follow the layout's largest scale; GTK 3 is expected to
redraw, programs reading `Xft.dpi` at start take it at their next
start.

**A desktop that sends no layout** (older than 0.3.88) gets scale 1: the
scale is said per display, and such a desktop names no display.

## Not chosen

- **Blit's API as the owner** (option 1b): a second reader of one fact,
  and a token the desktop does not always have. Kept open only because
  the owner has the question.
- **The scale on `WINDOW output`**, as ADR 0229's row sketched: that line
  names the window's display, which has one scale; putting it on the
  layout says it once per display, not once per window.
- **A hello word** (`scale=1`): the desktop already learns what the
  compositor understands from `blit-compositor --version` (Hibr's
  `DT_NATCVER`: 3081 for X11, 3088 for the layout), and only a desktop
  that sends the seventh word can make the wire's pixels differ from the
  programs'. An older compositor reads a seven-word layout line as
  unknown, so Hibr sends it only to a compositor that takes it.
- **Logical pixels on the wire** (option 2b): every desktop line and
  Blit's pointer mapping would change, for a fact only the compositor
  needs.
- **Integer scale only** (option 3a): GTK4, Qt6, Firefox, Chromium, SDL
  and mpv would draw at 2 and be shrunk at 150%, when every one of them
  can draw at 1.5.
- **`ceil(W / S)` or `round(W / S)` for the logical size:** round leaves
  the picture short of the body (a strip of background, or a stretch);
  ceil configures a different size when a program's own size is said
  back.
- **Stretching the picture to the body** to hide rounding: a resample of
  the whole window for one pixel.
- **Showing programs that know no scale at 1:1** (small) instead of
  enlarged: the window would not fill the body Hibr sized for it.
- **Nearest sampling for the shrink:** drops a column in four at 150%.
  **Bilinear for enlargement too:** blurs where ADR 0204 chose pixels; it
  can be a Control Panel choice later, as every feature has its tuning.
- **Xwayland at the scale** (option 4a, and `-hidpi`, which is rootful
  only): X space would stop being the desktop's pixels, and every X
  window would be enlarged, never sharp.
- **`GDK_SCALE` and `QT_SCALE_FACTOR` in programs' environments:** fixed
  at a program's start, the same on every display, and they reach Wayland
  programs too, which would then scale twice.
- **X's primary display's scale for X:** Hibr moves X's primary with the
  focus by default, so X's scale would follow the person's focus and an X
  program would get whichever display was primary when it started.
- **A smaller scale for X than the largest:** X windows would be small on
  the HiDPI display, where small hurts most; on a lower-scale display
  they are large instead, which stays usable.

## Consequences

- A program on a HiDPI display draws at its full resolution and at the
  size its cells give it: sharp at every step for a program that uses
  fractional scale (expected: GTK4, Qt6, Firefox, Chromium, mpv; SDL for
  high-DPI windows; Step 7 shows which); sharp at 200%, 300% and 400% for
  GTK 3, and filtered-shrunk between.
- The desktop changes one word. Hibr's window sizes, its layout, its
  hit tests, `frames info`'s `vwidth`/`vheight` and the `INPUT` relays
  keep their meaning; only the numbers a program commits differ.
- Program scale and zoom (ADR 0204) multiply: a program at 150% on a
  display zoomed 200% draws at 1.5 into the zoom's logical pixels, which
  Blit then enlarges. On a HiDPI panel, program scale with a larger font
  is the sharp choice; zoom is the magnifier.
- The rounding costs at most `ceil(S) - 1` pixels at a window's right and
  bottom edge, cut, never stretched.
- The compositor becomes X's XSETTINGS manager for three keys. Hibr's
  colour scheme and theme keys, when they come (ADR 0229's portals row),
  are more keys of the same manager, said by the desktop, not a second
  manager.
- The bilinear shrink changes how a frame picture drawn smaller than its
  source looks everywhere (a mismatch Hibr scales to fit, a `WINDOW view`
  that shrinks); a picture at its size or enlarged is the same bytes as
  before.
- Programs started before a change: Wayland ones follow it live (they
  are configured again); X ones that read `Xft.dpi` at start keep the
  old size until restarted.

## Evidence

Measured for this proposal (2026-11-21), outside the repository:

- `proto/rounding.py` (this folder; integer arithmetic, scales 100-400 in
  steps of 25, widths 1-8192, a fractional client drawing
  `round(w * S)` half away from zero as the protocol says): the chosen
  rule always covers the body; it is over by at most 1 px to 200%, 2 to
  300%, 3 to 400%; a program's own size said back configures it again in
  every case (plain ceil: 22,500 misses); `round` leaves the picture 1-2
  px short; rounding positions and sizes apart misplaces 40,500 of
  212,400 subsurface placements by a pixel.
- Strings of the shipped binaries (protocol interface names are compiled
  into each program's own protocol code): `wp_fractional_scale_manager_v1`
  and `wp_viewporter` in Firefox ESR 153.7 `libxul.so`, Chromium
  156.0.8078.79, SDL 2.32.4, mpv 0.40.0 (this host), GTK 4.18.6, Qt 6.8.2
  `libQt6WaylandClient`, SDL 3.2.10 (their trixie packages, unpacked);
  neither in GTK 3.24.49's `libgdk-3`, which has `GDK_SCALE`,
  `Gdk/WindowScalingFactor` and `Xft/DPI`. Xwayland 24.1.6 in the release
  environment: `-hidpi` "adjust to output scale when rootful"; binds
  `wp_fractional_scale_manager_v1`, `wp_viewporter`, `zxdg_output_manager_v1`
  and `xwayland_shell_v1`. libwayland-server 1.23.1, wayland-protocols
  1.44 there.
- The source: every `wl_output` sends `wl_output_send_scale(r, 1)`
  (`bind_output`); `surface_damage` takes no damage ("every frame is a
  whole one"), so damage has no coordinates to convert; picture textures
  are `GL_NEAREST`.

What each landing step must show is in `plan.md`; the ADR is accepted when
`compositor_gate`'s scale part, `xwayland_gate`'s scale part and the
renderer's shrink case pass on the release environment, and real programs
(GTK4, GTK3, Qt6, Firefox, Chromium, an SDL game, mpv; under X a GTK 3
and a Qt 6 program) are seen at 150% and 200%. Steam and Proton under a
scale cannot be run here: deferred to the owner's machine, said as such.

## Physical size (added at the owner's request)

Each display's physical size, from the DRM connector (`mm_width`,
`mm_height`, the monitor's EDID), in millimetres, 0 meaning unknown
(projectors and some televisions report nothing or nonsense): on
`/v1/displays` as `physical_mm: {"width": W, "height": H}`, and to the
desktop with the layout. Nothing in Hibr uses it yet; Blit derives
nothing from it.
