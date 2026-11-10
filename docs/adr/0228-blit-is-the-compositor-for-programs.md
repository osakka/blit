# ADR 0228: Blit is the compositor for programs

- Status: proposed (phases 0 and 1 approved by the owner, 2026-11-09)
- Date: 2026-11-09
- Scope: a Wayland server and an X window manager inside Blit; programs and
  games in Hibr windows and full screen; supersedes the gamescope path of
  ADR 0225 and the display hand-over of ADR 0206 when complete

## Context

Programs that draw through Wayland or X11 -- Steam, Proton and native games,
GTK and Qt applications, emulators -- reach Blit today through a compositor
that is not Blit: gamescope (ADR 0225, `blit-gamescope`) for a window, the
display handed to gamescope on its own VT (ADR 0206) for full screen, and
Hibr's X Screen (Xvfb, the CPU) for X11 programs. Measured and read on
2026-11-08/09:

- gamescope's headless output size is fixed at launch (`HeadlessBackend.cpp`
  copies `-W/-H` once; no resize path). Its PipeWire capture keeps the
  output's shape (`calculate_capture_size`). A window resized to another
  shape gets bars and one scaling pass; only `GAMESCOPE_XWAYLAND_MODE_CONTROL`
  changes the game's resolution live, and not the output's.
- gamescope's stream carries no cursor and its EIS server reports neither
  the cursor nor the program's pointer mode (`pipewire.cpp`,
  `InputEmulation.cpp`, 3.16.22), so Blit draws a pointer it cannot see
  (ADR 0225 hold modes work around this with absolute positions).
- Each frame crosses gamescope's composite, PipeWire, the bridge process,
  Hibr's relay and Blit; input crosses Blit, libei and gamescope.
- Three ways to show a program (X Screen, Wayland Screen, the hand-over) are
  three compositors beside Blit's own.

The owner asked for Blit to be the compositor itself, dropping gamescope and
any other compositor (2026-11-09).

## Options considered

1. **Keep gamescope; add its resolution control.** Days of work; crisp only
   while the window keeps the launch shape; every limit above stays. A
   parallel to option 3 once that exists.
2. **Adopt wlroots inside Blit.** wlroots is C and a complete compositor
   library, but it brings its own backends, renderer and scene graph -- a
   second compositor core beside Blit's KMS/GLES one, which the one-source
   rule forbids -- plus pixman, libinput and its renderer stack, and nothing
   shows it builds under tcc.
3. **Blit implements the Wayland server and the X window manager inside
   blit-server**, on libwayland-server, drawing every surface through its
   existing picture pipeline. Months of work; no second core; every window
   size and shape 1:1; the cursor is Blit's because Blit is the compositor.
   But a fault in the X window manager -- the piece most exposed to decades
   of X11 quirks -- takes down every display and terminal on the machine.
4. **Blit's own compositor program beside the server, `blit-compositor`**,
   one per session, shipped in Blit's package as `blit-gamescope` is today:
   it is the Wayland server and the X window manager, and it hands each
   window's buffer to Blit as a picture by descriptor over the existing frame
   channel (zero copy, ADR 0223) and takes input over the existing routes
   (ADR 0225). It composites nothing -- each window is one surface, drawn by
   Blit's one picture pipeline -- so it is not a second compositor core.
   Everything option 3 gives, plus: a crash ends that session's programs,
   not the machine's displays; blit-server stays free of window-system
   code; Hibr's relay, Screens and the INPUT wire are reused as they are, the
   program replacing gamescope behind them. Cost: one descriptor hop per
   frame and the pointer sent on the wire, both of which exist today.

## Decision (proposed)

Option 4. **Blit becomes the Wayland compositor and the X window manager
for programs, as its own program, `blit-compositor`, beside blit-server. Xwayland stays: it is an X server, not a compositor, and Steam
and Proton games are X11 programs.** gamescope, `blit-gamescope`, PipeWire
and libei for programs, the ADR 0206 hand-over and, once the comparison
allows, Xvfb leave.

- **Sovereignty, exactly.** What it buys: control of a window's size, the
  cursor, focus, input, timing, full screen and the dependency set -- every
  limit in the context traces to one of these, and none can be removed by
  swapping one external compositor for another, since capturing a foreign
  compositor's output as video and compositing it again is what imposes
  them. What it does not buy: Xwayland stays someone else's, and games and
  GPU drivers behave as they do; those become Blit's to triage.
- **One picture pipeline.** A Wayland or X11 window's buffer is a picture
  by descriptor in its Blit session -- the object the frame channel creates
  today: dmabuf import (ADR 0223), placement and z-order, memory budgets
  (ADR 0226), input routes (ADR 0225). No second surface model.
- **The desktop is the window manager; Blit is the compositor.** Hibr
  places, moves, resizes, raises, focuses and titles windows. Blit tells
  the session about each new top-level and its hints (title, app id,
  minimum and maximum size, a full-screen request, transient parent) and
  the desktop answers with its placement and size; Blit configures the
  client at exactly that size in pixels, so a resize is 1:1 at any shape.
  This event path between blit-compositor and the session, on the frame
  channel Hibr already speaks, is phase 1's central design and gets its own
  wire definition in `docs/api.md`. **Hibr builds its side:** a native
  window kind in the desktop -- hints in, placement and size out -- the same
  shape as its X Screen windows.
- **Input stays Blit's**, through the route model of ADR 0225: `wl_seat`
  keyboard and pointer from evdev, focus from the desktop's `INPUT on/off`;
  `?7701` and Release Pointer map onto `zwp_pointer_constraints_v1` and
  `zwp_relative_pointer_manager_v1`; the desktop's own chords still reach
  it. The keymap is new: Blit derives an xkb keymap from its keyboard
  settings (one source), compiled with libxkbcommon.
- **Security.** One blit-compositor and one Wayland socket per Blit
  session, running as that session's user, its frame channel authenticated
  and peer-checked as today; never one socket for the server. Its name reaches the session as `WAYLAND_DISPLAY`.
- **Dependencies.** blit-compositor is its own program in its own package,
  so it links libwayland-server and libxkbcommon as any program does; the
  `blit` package and blit-server gain nothing. Its protocol code is generated
  at build time by wayland-scanner from the protocol XML of Debian's
  `wayland-protocols`. Xwayland is Recommended, not Depended. No wlroots.
- **Protocols, phase 1:** `wl_compositor` (v6), `wl_subcompositor`,
  `wl_shm`, `wl_output` (v4) and `zxdg_output_manager_v1`, `wl_seat` (v8,
  keyboard with the xkb keymap, pointer), `wl_data_device_manager`,
  `xdg_wm_base` (v6) with server-side `zxdg_decoration_manager_v1` (the
  desktop draws frames), `zwp_linux_dmabuf_v1` v4 with default feedback
  (Mesa's Vulkan WSI requires v3 or later: `wsi_common_wayland.c`, 25.0.7),
  `wp_presentation`, `wp_viewporter`, `zwp_relative_pointer_manager_v1`,
  `zwp_pointer_constraints_v1`. Phase 2 adds `xwayland_shell_v1`. Later,
  as games need them: `wp_linux_drm_syncobj_v1` (explicit sync),
  `wp_tearing_control_v1`, `wp_fractional_scale_v1`, `wp_fifo_v1`,
  `wp_commit_timing_v1`.
- **Xwayland's needs, checked in 24.1.6's source** (`xwayland-screen.c`
  `registry_global`, `xwayland-input.c`): rootless it needs `wl_compositor`,
  `wl_shm`, `wl_seat` v3+ and `wl_output` v2+; it uses
  `xwayland_shell_v1` when offered (surface association by serial,
  `WL_SURFACE_SERIAL`) and falls back to the `WL_SURFACE_ID` client
  message; `xdg_wm_base` only rootful; linux-dmabuf for glamor, falling back
  to software without it; relative pointer, pointer constraints, viewporter
  and the keyboard grab when present.
- **The X window manager** (phase 2) speaks X11 to rootless Xwayland as a
  client: map and configure requests, `WM_TRANSIENT_FOR`, override-redirect
  menus, `_NET_WM_STATE_FULLSCREEN`, `WM_NAME`/`_NET_WM_NAME`, focus,
  `WM_TAKE_FOCUS`, and the selections (CLIPBOARD, PRIMARY) bridged to
  the desktop's clipboard through what Hibr's console already accepts.
  Whether it speaks the X protocol itself (as Hibr's xscr does) or through
  libxcb loaded at run time is phase 2's first decision.
- **Keeps** ADR 0225's principle that blit-server gains no window-system
  code: it lives in blit-compositor. **Supersedes**, when phase 4
  completes, ADR 0225's gamescope path and ADR 0206.
- **Full screen** is unaffected by the process boundary: a dmabuf picture
  that covers an output is scanned out directly whichever process produced
  it.

**Phases.** Each is its own release with its own gates and one result the
owner can see.

0. **Baseline** (deferred by the owner on 2026-11-09 so phase 1 could
   start; it must be measured before phase 2 starts, since phase 2's
   criterion needs it; Blit 0.3.71 carries the instrument, `frame_us` and
   `last_inject_us`). Key-to-pixel latency and frame delivery
   for a game in the gamescope window and in the ADR 0206 hand-over, on the
   mini PC, measured the same way phase 3 will be. The compositor must match
   or beat it.
1. **Wayland core.** The protocols above, the top-level event path to the
   desktop, the xkb keymap, the per-session socket. Gate: an in-repo
   stand-in client on libwayland-client (the `frame_dmabuf_producer.c`
   pattern). The owner sees vkcube (Vulkan, Wayland WSI) in a Hibr window,
   resized live at any shape, 1:1, with keys and mouse.
   **The criterion for phase 2, decided now:** phase 2 starts when phase 1
   has shipped with its gates green, vkcube and one everyday Wayland
   application are usable in Hibr windows on the owner's machine, and phase
   0's latency is matched or beaten. Otherwise the owner decides again with
   phase 1's experience, and gamescope stays.
2. **Xwayland.** Rootless Xwayland per session, blit-compositor as its
   window manager.
   The owner sees Steam Big Picture in a window with no gamescope, then one
   Proton game. The gamescope path is frozen to fixes from here.
3. **Full screen.** A full-screen window that covers an output is scanned
   out directly from its dmabuf on the primary plane, with no composite, the
   desktop alive underneath. Measured against phase 0. ADR 0206 retired.
4. **Cut over.** `blit-gamescope` and the gamescope dependency removed;
   Screens' engine becomes Blit's own windows; X Screen's retirement decided
   on the GTK comparison (clipboard, dialogs, menus).
   **Planned with phase 2: the colour scheme.** Programs follow the
   desktop's light or dark look live: the desktop tells blit-compositor
   `WINDOW scheme 0 light|dark` (agreed with Hibr 2026-11-10), and the
   compositor serves the session's xdg-desktop-portal Settings backend
   (`org.freedesktop.appearance color-scheme`, later accent and contrast)
   through sd-bus, registered for `XDG_CURRENT_DESKTOP=Hibr`, so GTK4,
   libadwaita, Qt 6.5+, Firefox, Chromium and Steam's CEF follow it.
5. **Later, optional:** an upscaler (an FSR/NIS-like GLES pass), a frame
   limiter, HDR. Not part of this decision; listed so the loss is explicit.

## Consequences

- Any program in a Hibr window at any size and shape, rendered 1:1, with
  the cursor drawn by Blit at the position the program sees; full screen
  without handing the display away.
- One compositor: gamescope, PipeWire and libei for programs, the bridge
  process, the hand-over and eventually Xvfb leave.
- Blit's package gains window-system code -- a Wayland server, an X window
  manager, an xkb keymap -- in its own program. It is the largest piece
  Blit has taken on.
- Lost against gamescope, to be rebuilt only if wanted: its upscalers,
  HDR and frame limiter; Steam's gamescope-mode integration (Deck-style
  overlay focus, the quick-access menu, per-game resolution) -- Steam runs
  as on any Linux desktop; and gamescope's years of game-specific window and
  focus quirks, met here one game at a time.
- Months of work across phases 1-4; each phase ships on its own and the
  gamescope path keeps working until phase 4.

## Preconditions

- **Build host: met (2026-11-09).** libwayland-dev and libwayland-bin
  1.21.0, wayland-protocols 1.31 and libxkbcommon-dev 1.5.0 from Debian 12,
  installed with the owner's approval. Server code generated by
  wayland-scanner for xdg-shell, linux-dmabuf and presentation-time compiles
  with tcc and links and runs against libwayland-server. 1.31 carries every
  protocol phases 1 and 2 name (xdg-decoration, xdg-output, viewporter,
  pointer-constraints, relative-pointer, and xwayland-shell in staging). The
  targets run libwayland 1.23 and protocols 1.44 (Debian 13); the wire is
  versioned, so the older XML only bounds the versions offered.
- A host with Xwayland 24.1 for the phase 2 gates (the mini PC and 10.52
  have 24.1.6; the build host has none).

## Evidence

Planned: phase 0's numbers; per phase, the stand-in-client gates, the full
and GLES suites, and the owner's live check named above.

**Phase 1, milestone A (0.3.71, in development, 2026-11-09).** The first
stand-in-client build for Blit and Hibr to test against.

- `blit-compositor` (`src/blit_compositor.c`, its frame channel in
  `src/blit_compositor_wire.h`), built by `tools/build-tcc.sh compositor`
  with tcc against libwayland-server, its protocol code generated at build
  time into `build/protocol` from the system XML (xdg-shell,
  xdg-decoration), never committed. It speaks the frame channel as version
  4 and the `WINDOW` lines exactly as `docs/api.md` drafts them, listens on
  `$XDG_RUNTIME_DIR/blit-<n>` (the first free n), and offers
  `wl_compositor`, `wl_subcompositor` (subsurfaces accepted and logged, not
  composited), `wl_shm` (libwayland's, ARGB8888 and XRGB8888), `wl_output`
  (one mode: 1920x1080 until the desktop's first size, then the newest
  non-zero `WINDOW size` of any window, so every client sees the mode
  change on any resize -- one reading of "the window size"; the full-screen
  size or the first size are the alternatives, to decide with Hibr),
  `wl_seat`, `xdg_wm_base`
  and `zxdg_decoration_manager_v1` (server-side). A toplevel is announced at
  its initial commit and configured only when the desktop sends its size,
  exactly that size; each committed `wl_shm` buffer is copied into one
  sealed FRM1 frame (XRGB8888 as RGB, ARGB8888 un-premultiplied to RGBA,
  which Blit reads as straight alpha), released at once, and its frame
  callbacks answered when the frame was sent.
- **Versions offered on the build host.** The ADR names `wl_compositor` v6
  and `xdg_wm_base` v6; Debian 12's libwayland 1.21 and wayland-protocols
  1.31 carry v5 of both, and libwayland refuses a global above its
  interface's version, so the compositor offers the lower of the two
  (`BC_VERSION`): `wl_compositor` 5, `xdg_wm_base` 5, `wl_output` 4,
  `wl_seat` 8, `wl_subcompositor` 1, `zxdg_decoration_manager_v1` 1. Built
  against Debian 13's XML it offers v6 of both without a change.
- **blit-server**: a version 4 hello is answered as version 3's plus
  ` window=0`, and every `WINDOW` line `ERROR WINDOW <id> ENOTSUP` (the id
  0 when the line has none, as for INPUT) -- the only server change.
- **Gate** `compositor_gate` (hibr suite): a fake desktop answers the hello
  `window=1`; with the in-repo stand-in client
  (`tools/tests/wl_standin_client.c`, libwayland-client) it sees
  `WINDOW display blit-0`, then `new 1 0`, `title`, `appid`, `limits 1 64
  48 0 0` and `decor 1 server` and no frame before its size; after `WINDOW
  size 1 320 200` a 320x200 frame; the client's popup as `WINDOW popup 2 1
  12 63` (its positioner's offset in the toplevel's surface pixels) and an
  80x60 frame of picture 2, which `WINDOW close 2` ends through
  `popup_done` with `WINDOW gone 2`; a second client, given 100x80 and
  then killed outright as a crashing game is, brings `WINDOW gone 3` and
  nothing else; after `500 300 activated` a 500x300
  frame, each sealed and equal to the client's pattern pixel for pixel;
  `0 0` gives the client's own 256x160; `WINDOW close 1` ends the client
  and brings `WINDOW gone 1`; the client counts four draws, four releases
  and four frame callbacks, takes a keyboard and a pointer from `wl_seat`,
  and is told the output's mode 1920x1080, then 320x200 and 500x300; the
  compositor ends, status 1, when the desktop's
  channel closes. Against a real blit-server on the mock display: version 3
  unchanged, version 4 `OK dmabuf=0 window=0`, `ENOTSUP` for four `WINDOW`
  lines, version 5 `EINVAL`, and blit-compositor reaching Blit directly
  keeps running, says why it shows nothing and configures no client. The
  compositor, the client and the wire's unit cases run again under
  AddressSanitizer and UBSan (without address randomisation: GCC 12's ASan
  cannot map its shadow under this kernel's at random).
- **Not in milestone A:** dmabuf frames (`zwp_linux_dmabuf_v1` v4, FRM3),
  input (`wl_seat` focus and events, the route, `GET /v1/input/keymap` and
  the xkb keymap, pointer constraints and relative pointer, `WINDOW
  cursor`), `wp_presentation` and frame pacing, `zxdg_output_manager_v1`,
  `wl_data_device_manager`, `wp_viewporter`, subsurfaces composited, the
  package, and the live check with vkcube.

**Phase 1, milestone B (0.3.71, in development, 2026-11-10).** Every line
of `docs/api.md` "Program windows" as agreed with Hibr, except the
proposals named there.

- **Window states and outputs.** `WINDOW maximize`, `minimize`, `parent`,
  `hide` and the `maximized` state of `WINDOW size` (and the xdg
  capabilities to match). `WINDOW output` replaces milestone A's "newest
  size" rule: one `wl_output` global per display mode the desktop names,
  the window's surface entering it and leaving the one before, an output
  no window is on given the next new mode so globals never come and go.
  The line carries a mode, not a display, so two displays of one mode are
  one output -- to revisit with Hibr if a program ever needs to tell them
  apart.
- **dmabuf.** `zwp_linux_dmabuf_v1` v4 (the Debian 12 XML's highest), only
  for a hello answered `dmabuf=1` with `formats=`, exactly those pairs, in
  the v4 format table and one tranche. A dmabuf commit is one `FRM3` of the
  client's own planes, held until `RELEASE` and given back before `WINDOW
  gone`; `ERROR <id> ENOTSUP` copies that frame, and every later one of
  the connection, as `FRM1` when its one linear plane maps. The feedback's
  main device is the first DRM render node (the hello does not name
  Blit's GPU). The build host has no DRM device and no `/dev/udmabuf`, and
  libgbm has no headers here: the stand-in's planes are memfds, so the gate
  proves the wire and descriptor passing (the plane Blit receives is the
  client's own file), not a GPU import.
- **Pacing.** Frame callbacks when Blit has taken the frame -- an `FRM1`
  when sent, an `FRM3` at once when it is the only one held, else at the
  `RELEASE` of the frame before it -- and at most once per refresh of the
  window's output; `wp_presentation` presented/discarded at the same
  moment, `CLOCK_MONOTONIC`, no vsync flag.
- **Keymap.** blit-server's `GET /v1/input/keymap`: every level asked of
  `evdev_key_character` and every role of the device's `modifiers` remap,
  so Blit's tables are the one source (`src/blit_xkb_keymap.h`).
- **Input.** No libei: Blit's route machinery (ADR 0225) gained a second
  backend, `INPUT sock`, which connects to blit-compositor's own input
  socket and writes key/ptr/rel/btn/wheel/frame lines; blit-compositor
  turns them into `wl_keyboard` (the keymap, compiled with libxkbcommon)
  and `wl_pointer` events, and `INPUT rel` into
  `zwp_relative_pointer_v1`. `zwp_pointer_constraints_v1` waits on the
  proposed `WINDOW lock`.
- **Gates.** `compositor_gate` (desktop, states, pacing, dmabuf, Blit,
  input; every part again under ASan/UBSan), `keymap_gate` (the keymap
  compiles with libxkbcommon and types what Blit types, before and after a
  PATCH), `frame_input_gate` sock mode (the route's lines, live rule,
  release-all, a line not taken), `compositor_wire_unit`.
- **Not in milestone B:** `INPUT member` (in api.md, not in Blit),
  `zwp_pointer_constraints_v1` and `WINDOW lock` (a proposal),
  `WINDOW cursor` and `wl_pointer.set_cursor`, Blit's per-device key repeat
  on the wire, `zxdg_output_manager_v1`, `wl_data_device_manager`,
  `wp_viewporter`, subsurfaces composited, the version 5 grammar (a
  coordinated Blit+Hibr change), the package, and the live checks on the
  mini PC: vkcube's dmabuf frames imported by Blit's GPU, its pacing and
  keys and mouse.

**Phase 1, milestone C (0.3.72, in development, 2026-11-10).** The
Blit-side work that closes phase 1, apart from the owner's live check.

- **Pointer lock and confinement.** `zwp_pointer_constraints_v1` (version
  1) exactly as `docs/api.md` "Pointer lock", agreed with Hibr: the
  program's `lock_pointer` or `confine_pointer` is asked of the desktop,
  `WINDOW lock <id> relative|confine` (the top-level's id, also for a
  popup's surface), and waits until the desktop's `WINDOW lock <id> on`
  (an `off` before it, the desktop's "no gesture yet", leaves it waiting);
  `off` deactivates it, after which a oneshot constraint is defunct and its
  request spent, and a persistent one waits for the next `on`. The program
  destroying a constraint still asked for, or its window going, is `WINDOW
  lock <id> off`, before `WINDOW gone`. While a lock is active the
  compositor sends no absolute motion; relative motion is milestone B's
  `INPUT rel`. Regions and the cursor hint are not carried (Blit confines
  to the picture's drawn rectangle).
- **Popups take input: `INPUT member` in Blit.** A popup's picture joins
  its top-level's route at its offset; the pointer over its drawn
  rectangle is hit-tested with the top-level's (the topmost draw wins) and
  sent as a point in the top-level's surface pixels -- the member's offset
  plus the point within it, negative past the top-level's left or top --
  for both backends (EIS scales it to the region as the top-level's own
  points are). The member ends with its picture or its route. Its answers
  follow the other INPUT requests (`OK input <popup-id>`, `ERROR INPUT
  <popup-id> EINVAL|EBUSY`); api.md said `OK window`, which no INPUT
  request answers, and now says this. The compositor hit-tests its popups
  from top-level coordinates as milestone B did, now with negative points.
- **Key repeat from Blit's settings.** `GET /v1/input/keymap` answers, 200
  and 304 alike, with `X-Blit-Repeat-Rate` and `X-Blit-Repeat-Delay`: the
  keyboard's repeat as the kernel holds it (the keyboard settings'
  read-back), 0 and 0 without autorepeat. Headers rather than a second
  endpoint: one request, and repeat is not part of the keymap's text, so
  the ETag keeps its meaning and a 304 still brings a new repeat.
  blit-compositor sends `wl_keyboard.repeat_info` from them; its 25 a
  second after 600 ms is gone.
- **The keymap read only when it changes.** Blit keeps a keymap revision,
  bumped by any keyboard's device change (the event stream's
  `device_changed` and this are one signal) and when the default keyboard
  moves, and writes `INPUT keymap <id> <revision>` on each live `sock`
  route that has not been told it -- at once, and when a route goes live
  after a change. It is a notice, not an event: no `frame`, not counted.
  The compositor reads the keymap for a revision it has not read for,
  `If-None-Match`, **synchronously, bounded at 250 ms on loopback, only on
  the notice**: chosen over a non-blocking read so that the key Blit writes
  after the notice is interpreted with the new keymap; the event loop
  waits at most that long, and only when the keyboard changed. It no longer
  reads at a keyboard enter (only to retry a read that failed).
- **Which keyboard.** The default keymap is the keyboard whose key came
  last while it is there and enabled, else the first enabled keyboard; a
  press on another keyboard moves it, and the notice is written before
  that key.
- **The `blit-compositor` package**, like `blit-gamescope`:
  `/usr/bin/blit-compositor`, a README naming this ADR, a
  GPL-3.0-or-later copyright file; `Depends: blit (= version), libc6,
  libwayland-server0, libxkbcommon0`, `Recommends: xwayland` (as decided
  above; phase 1 does not start it); `blit` Suggests it. tcc 0.9.27 has no
  `-ffile-prefix-map` (it accepts the flag and ignores it: measured, the
  working directory and source paths stay in its stabs), so the compositor
  target is built without `-g`: no path is recorded, and builds from two
  protocol directories are the same bytes. Two package builds of one tree
  gave one sha256.
- **Gates.** `compositor_gate`: the lock part (the stand-in's `--lock`:
  oneshot pending through an `off`, locked with relative motion and no
  absolute, unlocked and defunct; persistent locked again, its destroy
  `off`; a confinement whose pointer moves; the window gone, `off` then
  `gone`), and the input part end to end with a real Blit on the mock
  display and two fabricated keyboards: Blit's route into the compositor's
  input socket, the pointer over the popup's picture (`INPUT member`)
  reaching the popup surface at its own pixels; repeat 30/250 from Blit; a
  repeat PATCH while live reaching the client; a key from the second
  keyboard bringing its keymap and repeat before the key, and the first's
  back; the keymap read only at start and on notices; all again under
  AddressSanitizer and UBSan. `frame_input_gate` sock and stub: `INPUT
  member`'s answers, its mapping (a negative offset too) and its end with
  its picture, through lines and through EIS; the notice when a route goes
  live, once. `keymap_gate`: the repeat headers on 200 and 304, a repeat
  PATCH changing them and not the ETag, the default following the last
  key's keyboard, disabled and enabled. `package_artifact_gate`'s
  compositor leg, run by hand on the built package (the full gate needs a
  release manifest).
- **Not in milestone C:** `WINDOW cursor` and `wl_pointer.set_cursor`,
  `zxdg_output_manager_v1`, `wl_data_device_manager`, `wp_viewporter`,
  subsurfaces composited, the version 5 grammar, Hibr's side of `WINDOW
  lock` and `INPUT member`, and the owner's live check on the mini PC:
  vkcube's dmabuf frames imported by Blit's GPU, an everyday Wayland
  program (a GTK one), keys, mouse, popups and pointer lock.
