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
   has shipped with its gates green and vkcube and one everyday Wayland
   application are usable in Hibr windows on the owner's machine.
   (Amended 2026-11-10 by the owner: the condition that phase 0's latency be
   matched or beaten is dropped. Phase 0 measured the gamescope path, which
   the owner removed in 0.3.75; phase 2 is judged on its own latency
   numbers.)
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

## Phase 2, planned (approved by the owner, 2026-11-10)

Steam and other X11 programs in Hibr windows, through Xwayland, the real X
server, with blit-compositor as its X window manager -- the same split as
libwayland and our compositor: the server is upstream's, the behaviour is
ours. Writing an X server (years; DRI3/Present/GLX are the hard part),
Xorg on the display (two owners of KMS and input), Xvfb (no GPU) and a
compositor library (a foreign compositor under Blit again) were each worse.

**Decisions.**
- **Xwayland per compositor**, started with it (lazy start a later
  switch): the compositor creates the X socket (`/tmp/.X11-unix/X<n>`, the
  first free) and passes it with `-listenfd`, the WM connection with
  `-wm`, `-rootless`. Xwayland dying ends its clients; the compositor
  starts it again for the next and logs why. Xwayland is `Recommends`:
  without it the compositor runs Wayland only and says so once.
- **Association by `xwayland_shell_v1`** (Xwayland >= 23.1; Debian 13 has
  24.1.6): the serial Xwayland gives the window manager in a
  `WL_SURFACE_SERIAL` client message on the X window (l[0] its low 32
  bits, l[1] its high; corrected at milestone A from "a
  `_XWAYLAND_SURFACE_SERIAL` property", which does not exist:
  xwayland-shell-v1.xml) matched to the surface's `set_serial`. No
  `WL_SURFACE_ID` path; an older Xwayland is refused with the reason.
- **The window manager** on libxcb (pkg-config `xcb`, `xcb-composite`,
  `xcb-xfixes`, `xcb-res`, ADR 0230), built against wlroots' `xwm.c`
  (MIT) as the reference: its order of `CompositeRedirectSubwindows(root,
  Manual)` first, `_NET_SUPPORTING_WM_CHECK` and `_NET_SUPPORTED`, focus
  by `SetInputFocus` honouring `WM_TAKE_FOCUS` when a window's route goes
  active, override-redirect windows, `_NET_WM_MOVERESIZE`, INCR
  selections. Where ours differs, a line here says why.
- **X onto the existing wire** (ADR 0229): `WM_NAME`/`_NET_WM_NAME` title,
  `WM_CLASS` app id, `WM_NORMAL_HINTS` limits, `_MOTIF_WM_HINTS` decor,
  `_NET_WM_STATE` full screen and maximized, `WM_TRANSIENT_FOR` parent,
  `_NET_WM_MOVERESIZE` move and resize, `WM_DELETE_WINDOW` close, XFixes
  cursor names to `WINDOW cursor` (Blit's table knows the X names). Every
  toplevel keeps a stable position in X space (a virtual layout; Hibr
  still places the picture), so an override-redirect menu or tooltip is
  `WINDOW popup` at its offset from its parent toplevel (`WM_TRANSIENT_FOR`,
  else the toplevel under its origin).
- **One wire addition:** `WINDOW display <wayland-name> <x-display>`, the
  second word new (`-` when X is off), so the desktop exports `DISPLAY` to
  its terminals as it does `WAYLAND_DISPLAY`.
- **Selections:** CLIPBOARD and PRIMARY onto `CLIP` as a third kind of
  owner beside a Wayland source and the desktop; INCR for large ones.
- **Shaped for phase 3:** a full-screen toplevel's frame carries a flag so
  phase 3 can scan it out whole without restructuring; not built now.

**Milestones,** each gated (`xwayland_gate` with an xcb stand-in client:
map, title and class, an shm draw, `WM_DELETE_WINDOW`, an override-
redirect popup, a full-screen request; it skips with the reason where
`Xwayland` is absent) and measured -- frame latency from an X client's
Present to Blit's flip, and frames per second:
- **A.** One X program (xterm, glxgears) in a Hibr window, keys and mouse.
- **B.** The Steam client window, on the owner's machine, before any more
  window-manager depth: Steam decides the scope.
- **C.** What Steam showed: states, transients, popups, move and resize,
  cursors.
- **D.** The X selections onto `CLIP`.
- **E.** Big Picture full screen and one Proton game, at the monitor's
  refresh.

**Milestone A (2026-11-10; released in 0.3.78).** One
X program in a Hibr window, keys and mouse (`src/blit_compositor.c`, the X
section; `docs/api.md`, "X programs"):

- **Xwayland** is started with the compositor: `Xwayland` on `PATH`
  (`BLIT_COMPOSITOR_XWAYLAND` names another, gates only; `--x11 off`,
  the desktop's X11 Programs setting since 0.3.81, runs none), refused below 23.1 by its `-version` with the reason; the first
  free display from :0 to :32 taken as X does (`/tmp/.X<n>-lock` with the
  compositor's pid, a dead pid's lock taken over; `@/tmp/.X11-unix/X<n>`
  and `/tmp/.X11-unix/X<n>`, passed by `-listenfd`), `-rootless`, `-wm` on
  a socketpair, `WAYLAND_SOCKET` a `wl_client` of ours, `-displayfd` for
  readiness, no `-terminate`. Restarted when it dies, its status logged;
  three deaths within 5 s of a start in a row stop it (X off, the sockets
  closed, `-` to the next desktop). Ended and its sockets and lock removed
  at the compositor's end.
- **The wire:** `WINDOW display <wayland-name> <x-display>`, `:<n>` or
  `-`, on every connection.
- **Window kinds:** an X top-level is a `bc_xdg` (the picture) with its
  X window `xwin`; `window_is_toplevel`, `window_configure` and
  `window_close` let xdg and X top-levels share the announcement, frames,
  input routing, outputs and re-announcement, and only the X section knows
  X. The keyboard entering an X window's surface gives it X's focus.
- **Evidence here (the build host: Debian 12, no Xwayland).** The
  compositor builds with tcc and with `gcc -Wall -Wextra` (no new
  warning); `compositor_gate` passes, also under ASan/UBSan, with X off
  and `WINDOW display blit-0 -`, against a scratch copy of wayland-protocols
  1.44's XML on `PKG_CONFIG_PATH` (the host's 1.31 has no cursor-shape-v1,
  so `run-gates.sh` skips it here); `xwayland_gate` skips with its reason.
  Against a scratch imitation of Xwayland -- Xvfb serving X behind the
  compositor's own sockets and `-wm`, and a Wayland client doing Xwayland's
  side of xwayland-shell, never committed -- `xwayland_gate.py` passed end
  to end under tcc and ASan/UBSan in each order of the association (the
  commit first, the X message first, and buffers committed with the serial
  before the X message, which is the held-buffer path), and scratch checks
  passed the restart, a desktop reconnect re-announcing `:0`, the give-up
  after three quick deaths and the sockets' removal, X off (`none`, absent,
  older than 23.1: `WINDOW display blit-0 -` and the reason logged), and a
  window mapped before the window manager, mapped again and shown. Commit to frame sent
  there, 400x300 by wl_shm: median 1.3-2.4 ms, minimum 0.4-0.9 ms.
- **Pending:** the gate against Xwayland 24.1 on the owner's Debian 13
  desktop (`sh tools/tests/xwayland_gate.sh`); Hibr's side (exporting
  `DISPLAY` from `WINDOW display`'s second word); xterm and glxgears live.

**Where blit-compositor's window manager differs from wlroots' `xwm.c` and
`server.c`, and why.**
- One fork and SIGCHLD (libwayland's signalfd), not a double fork: the
  compositor reaps Xwayland itself, so it can log why it ended.
- Started with the compositor, never lazily, and no `-terminate`: the
  ADR's decision; X programs find their display ready and it does not end
  between them.
- Quick deaths are restarted (after 1 s, then 2 s) and given up only after
  three in a row; wlroots does not restart one that lived under 5 s. A
  given-up display's sockets close, so X programs fail at once.
- `xwayland_shell_v1` only: no `WL_SURFACE_ID`. An Xwayland older than
  23.1 is refused before it starts; one that sends `WL_SURFACE_ID` anyway
  is logged and its windows not shown.
- `CompositeRedirectSubwindows` before the root's event mask (the ADR's
  order; wlroots selects the mask first; both go in the first flush,
  before any client can map), and the mask request is checked: another
  window manager on the root is logged and refused.
- A window's properties are read at its MapRequest and kept by
  PropertyNotify, not at the association (wlroots reads them in a surface
  commit): its title and class are known when it is announced, and no X
  round trip is made from inside a Wayland request.
- A buffer committed before the match is held, not released, and is the
  first frame: the compositor keeps no surface state of its own to show
  later, as wlroots' `wlr_surface` does.
- Focus by ICCCM 4.1.7: `SetInputFocus` only when `WM_HINTS` takes input,
  `WM_TAKE_FOCUS` only to a window that lists it in `WM_PROTOCOLS`
  (wlroots sends it to every window); a program that moves X's focus
  itself is logged, not reverted (still so after milestone C: nothing
  seen needed more). With no window focused, the focus is PointerRoot,
  as wlroots (from milestone C; None before): None discards the
  keyboard a grabbing menu still needs.
- Stacking is the desktop's to draw; X's stacking only decides X's hit
  test: no `_NET_CLIENT_LIST(_STACKING)`, and a ConfigureRequest's
  sibling and stack mode are dropped. From milestone C X's order follows
  Blit's pointer: the toplevel under it is raised above the topmost other
  shown toplevel before the pointer's event goes to Xwayland, and again
  when a new toplevel is shown under a pointer at rest, never above
  override-redirect windows (wlroots' restack rule); wlroots restacks on
  activation, because its compositor's screen positions are the X
  positions, which ours are not (below). A ConfigureRequest after the
  desktop's size is answered with that size and a synthetic
  ConfigureNotify (ICCCM 4.1.5); wlroots hands it to its compositor.
- Toplevels keep the X position their program asks for, kept inside the
  root (from milestone C): X confines its pointer to the root, the
  largest output (the compositor's outputs all sit at 0,0), so a press
  past its edge reached X at the edge (the gate measured 390,170 arrive
  as 219,170). wlroots' compositors move X windows to their screen
  positions, inside the outputs by construction.
- Override-redirect windows (from milestone C) are popups of a toplevel,
  where wlroots shows them as unmanaged surfaces at their absolute X
  position: the wire has popups, not free-standing surfaces. The parent
  is the toplevel `WM_TRANSIENT_FOR` names, else the one under the
  window's origin in X space (a shown popup there first), else the X
  toplevel the pointer is in; one with none is not shown. One its program
  moves while it is shown is `WINDOW popup` again at its new offset, as an
  `xdg_popup.reposition` is (0.3.82, agreed with Hibr, whose frames module
  already took a repeated popup line as a move), as wlroots follows its
  geometry.
- X has no `popup_done`, and neither the window manager nor wlroots
  unmaps another program's override-redirect window: the desktop's
  `WINDOW close` for an X popup is logged. Menus close by X's own means --
  a press their grab gets, or their window losing X's focus.
- `_NET_WM_STATE` (milestone C) is written only from the desktop's
  `WINDOW size` states; a request from the program is a request
  (`WINDOW fullscreen`, `WINDOW maximize`) and a toggle is from what the
  property says. Maximized is both directions, as xdg has no half;
  wlroots keeps each flag and leaves it to its compositor. Modal, hidden,
  sticky, shaded, above, below and attention are not kept.
- `_NET_WM_MOVERESIZE` (milestone C) is let through only while a press
  the compositor delivered to the window is held (the button the message
  names), xdg's rule without the serial X does not have; wlroots emits
  every request and leaves the check to its compositor.
- X cursors (milestone C) are names, never images: XFixes' CursorNotify
  name through Blit's pointer table, `default` for an unnamed one.
  wlroots shows the image Xwayland sets with `wl_pointer.set_cursor`.
- Windows mapped before the window manager started (an X program waiting
  in the socket's backlog while Xwayland got ready) are unmapped and
  mapped again under it, so Xwayland sends a serial someone hears; wlroots
  starts lazily, its window manager up before the first client.
- `_NET_SUPPORTED` names what the ADR lists (state, full screen,
  maximized, active window, move/resize, name, the check window) and,
  from milestone C, focused; not wlroots' modal, hidden and client lists,
  which nothing keeps here. `_NET_ACTIVE_WINDOW` requests are logged: the
  draft has no line for a program raising its window.
- Xwayland gets exactly its five descriptors (`close_range` past them) and
  an unblocked signal mask (libwayland's signalfd blocks SIGTERM, SIGINT and
  SIGCHLD, which a child inherits); the listening sockets have a backlog of
  64, not 1, for X programs started before Xwayland is ready.
- X errors are logged by code (no xcb-errors); BadWindow, a window gone
  before a request about it, is not logged.

**Outside Blit,** for "Steam works" (written in programs-in-windows.md
before B): the Steam package (non-free), i386 multiarch, the Vulkan
loader and 32-bit Mesa, PipeWire or Pulse, and udev access to controllers
(Steam reads gamepads itself; Blit does not).

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
  its initial commit and configured at once -- the desktop's size if one
  has come, else 0x0 (the program picks) -- then exactly each size the
  desktop sends. (Milestone A configured only on the desktop's size; the
  live vkcube check of 2026-11-10 showed why that breaks the protocol: see
  Evidence); each committed `wl_shm` buffer is copied into one
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
  48 0 0` and `decor 1 server`, then the client's own 256x160 frame (the
  0x0 reply to its initial commit); after `WINDOW size 1 320 200` a
  320x200 frame; the client's popup as `WINDOW popup 2 1
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
- **The keymap read only when it changes** (superseded on 2026-11-10: the
  keymap now comes on the route itself; see "The keymap on the route, not
  the API" below). Blit keeps a keymap revision,
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

**Programs survive a desktop restart (2026-11-10; released in
0.3.73).** `docs/api.md` "Programs survive a desktop restart",
as agreed with Hibr, built in blit-compositor; it replaces milestone A's
"the compositor ends, status 1, when the desktop's channel closes".

- **`--display <name>`.** The desktop picks the Wayland socket, so it can
  export `WAYLAND_DISPLAY` before the compositor answers. The socket is now
  made before the channel is dialed: a name in use (libwayland's lock
  held), or not a name (empty, a slash, a space or control byte, over 31
  bytes, or too long for `<name>-input` in `sun_path`), is refused with a
  line on standard error and status 2, and the channel never hears of it.
- **The channel lost.** No client is ended. The channel's socket is
  closed; every FRM3 Blit held is given back except each shown window's
  newest, kept (a hold of sequence 0); a copied frame is kept as its sealed
  memfd, which every window now keeps for its newest copy (one copy per
  window, the price of sending it again). Callbacks waiting for a
  `FRAME release` are answered; while down, every commit is taken at once,
  paced at the output's refresh, its feedback `discarded`; a dmabuf
  commit's previous buffer is given back at once. Active pointer
  constraints end as at the desktop's `off`, and every one still wanted is
  asked again later.
- **Reconnecting** never blocks the event loop: the wire's hello is split
  into a non-blocking dial (`bc_wire_dial`) and the answer
  (`bc_wire_answer`), read when the socket is readable, at most 3 s; 50 ms
  doubling to 1 s between attempts, at most 60 s (the gates shorten it with
  `BLIT_COMPOSITOR_RECONNECT_MS`). An answer is read into a copy of the
  wire, so a `window=0` peer changes nothing; ids and sequences continue.
  The dmabuf formats offered stay the first hello's; a desktop answering
  others gets copies. A pointer constraint made while the channel is down
  is not marked asked (found by review: it would have been skipped on
  reconnect), so the reconnect asks it with the rest.
- **The re-announcement**, in creation order: `WINDOW display`, each
  top-level's `new` and hints and `INPUT sock` (a parent created later as
  `WINDOW parent` after them; `hide` for a window unmapped by its program,
  not for one not yet drawn), popups, subsurfaces (`WINDOW sub`), the
  newest frames, `WINDOW lock`. A
  toplevel never announced (the peer before had no `window=1`) is
  announced then, with a new id. No configure until the desktop's `WINDOW
  size`.
- **`WINDOW quit 0`**: status 0 at once, no reconnect; the parser takes id
  0 only.
- **Gate.** `compositor_gate`'s restart part, with a fake desktop
  answering `dmabuf=1`: `--display gate-display`, then that name in use,
  `a/b`, 32 bytes, `a b` and empty refused with status 2; a dmabuf program
  (stalled one frame ahead of Blit) and a wl_shm one animating 60 frames
  and asking for a pointer lock (`WINDOW lock 2 relative`, not granted);
  the channel and the listening socket closed; both programs go on, the
  wl_shm one's 60 callbacks taking about a second (the refresh), the dmabuf
  one drawing 61 buffers and given back 60 of them; the compositor, the
  Wayland socket and the input socket still there; a third program started;
  the same socket served again about a second later: `WINDOW display`,
  `new`/`title`/`appid`/`limits`/`decor` and `INPUT sock` for 1, 2 and 3,
  the newest FRM3 of 1 (64x32, the pattern) and FRM1 of 2 (160x100, the
  pattern), `WINDOW lock 2 relative` again, nothing configured until each
  new `WINDOW size`, then frames
  of 200x120, 80x40 and 100x80; `WINDOW quit 0` ending it at once, status
  0, its programs gone and its socket removed. A second compositor whose
  desktop never comes back -- only a peer answering `window=0` -- ends
  after its 1.5 s bound, status 1, its program gone, the log saying why.
  The other parts end with `WINDOW quit 0`; the desktop part keeps the
  status-1 ending, after a 600 ms bound. All again under AddressSanitizer
  and UBSan. `compositor_wire_unit`: `WINDOW quit` (only `0`).
- **Not tested here:** a real Hibr restarting (Hibr's side: the detached
  start, `--display`, the fixed socket and token, `WINDOW quit 0`, the
  members re-sent), a desktop answering a hello slower than 3 s, a desktop
  answering other dmabuf formats on reconnect, an active lock across a
  restart (its `unlocked` is the `off` path) and one made during the
  outage, popups and hidden
  windows announced again (built, not in the gate), and a real GPU's
  dmabuf held across the outage.
- **The first configure, live (2026-11-10).** `vkcube --wsi wayland` on the
  mini PC under Hibr 0.99.481 and Blit 0.3.72 ended at once with
  `xdg_surface#8: error 3: a buffer committed before the first configure
  was acknowledged` (the compositor log: `window 1: new` then `gone` 5 ms
  later, three times). vkcube commits, does one `wl_display_roundtrip` and
  presents; xdg-shell says the compositor *replies* to the initial commit
  with a configure, and the compositor was waiting for the desktop's
  `WINDOW size`, which crosses the channel and comes back later than one
  roundtrip. The configure now goes at once (0x0 before the desktop's
  size); the stand-in client checks on every run that a configure answers
  its initial commit within one roundtrip, and its scripted steps
  (popup, states, lock, animation) wait for a configure that carries the
  desktop's size.
- **The keymap on the route, not the API (2026-11-10).** On the mini PC
  the compositor logged `keymap: GET /v1/input/keymap answered 0
  (BLIT_SERVER_PORT unset): clients get no_keymap`. The desktop session
  has no BLIT_SERVER_PORT or BLIT_API_TOKEN by design: the login screen
  passes only BLIT_VERSION and the frame socket and token into the PAM
  session, its API token being the login screen's own (Hibr's
  login.hibr, a decision of 0.99.357). Passing the greeter's token on, or
  minting a session token, would have kept two hops (a notice on the
  route, then an HTTP read) and a credential the compositor has no other
  use for, and with several displays one held desktop's environment names
  one Blit session's API. Blit already writes to the compositor directly,
  so the keymap goes with the notice: `INPUT keymap <id> <revision> <size>
  <rate> <delay>` and a sealed memfd of the text and a NUL (SCM_RIGHTS),
  built by the one function GET /v1/input/keymap serves from
  (input_keymap_of). The compositor's HTTP client, its 250 ms synchronous
  read, the retry at keyboard enter and its environment lookup are gone;
  it installs a keymap whose bytes differ from the one it has (revisions
  restart with Blit) and otherwise applies only the repeat.
  `frame_input_gate` checks the memfd is sealed and byte-for-byte the
  API's keymap and repeat; `compositor_gate` runs the compositor with no
  port or token, the gate sending a real Blit's keymap as Blit does, then
  a real Blit's route switching keyboards B and A before their keys.
- **A socket per window (2026-11-10).** With 0.3.73 on the mini PC, keys
  and the pointer reached no Wayland program: the log named "Blit's route
  for window 1259388934". Hibr relays `INPUT sock` to Blit under its own
  picture id, as a relay must (Blit's ids are the desktop's), so Blit's
  lines carry that id, and the compositor looked it up among its own
  window ids and dropped every line (the keymap, which names no window,
  arrived). Every gate's fake desktop used the compositor's ids, so the
  mismatch could not show. Now each top-level has its own input socket,
  `<display>-input-<id>`, made at its first announcement and removed when
  it goes (any left by a dead compositor are cleared at start, under the
  display's lock); a connection's lines are that window's, and the line's
  id is Blit's alone. No wire change, none for Hibr. `compositor_gate`'s
  fake Blit writes id 1259388934, its real Blit pictures 41 and 42 for
  windows 1 and 2, and the restart part checks the sockets survive the
  channel and go at quit.
- **Subsurfaces (2026-11-10, phase 1's "one everyday program").** Firefox
  on the mini PC showed only its frame: it draws its page into a
  `wl_subsurface`, which phase 1 accepted and did not show. The owner chose
  to ship them with the input fix and version 5 in one release (Blit
  0.3.74, Hibr 0.99.482). A subsurface is a picture of its own, as a popup
  is, never composited by the compositor (which has no GPU path): `WINDOW
  sub <id> <parent> <x> <y> <w> <h> above|below`, agreed with Hibr, who
  asked for the size (to blank, clip and keep the cell box before and
  between frames). The tree is flattened onto the top-level; lines go
  bottom to top and all of a top-level's are resent on any change. Both
  wl_subsurface modes: synchronized state cached until the parent's
  commit, desynchronized applied at once (with cached state merged first);
  positions and order applied at the parent's commit. Input regions are
  kept (`wl_region` add and subtract, the last rectangle holding a point
  deciding) and decide the pointer: popups, subsurfaces above (topmost
  first), the top-level, those below. A subsurface that commits before its
  top-level is announced, or with the channel down, keeps its newest frame
  and has it sent when described; a frame nothing took has its callbacks
  answered at the refresh, so EGL's swap never stalls. `compositor_gate`'s
  `--sub` script covers each mode, the input region both ways, `below`,
  destruction and the reconnect; ASan/UBSan clean.
- **gamescope out now (2026-11-10).** The owner chose to remove
  blit-gamescope and every part that served it -- the bridge, the libei
  input path (`INPUT eis`), their gates and package, ADR 0206 -- in 0.3.75,
  with Hibr's and steamer's sides, before phase 2, accepting that Steam and
  other X11 programs do not run in Blit windows until Xwayland arrives
  ("I want no legacy stuff loose"). Phase 4 is then only what is left of
  ADR 0206, which was never built. 0.3.74 ships with blit-gamescope marked
  deprecated in its documentation.
- **gamescope removed in 0.3.75.** The bridge, its package, the libei
  input path and their gates are gone; `INPUT sock` is the only input
  route, and `frame_input_gate` covers on it every case the stand-in libei
  covered. `blit` Breaks `blit-gamescope` and `steamer` (retired with it),
  so an upgrade removes both.
- **Phase 0 dropped (2026-11-10).** With gamescope removed there is no
  path left to measure against; the owner dropped phase 0's latency
  condition from the phase 2 criterion.
- **Milestone A on real hardware (0.3.78, 2026-11-11).** xterm opened as
  a Hibr window on the owner's HP, and Steam reached its sign-in window.
  Then Xwayland 24.1.6 died (SIGSEGV at 0x8) about 120 s after a key:
  its AccessX timeout updates every keyboard's indicators, and Blit's
  keymap had none, so applying it had freed the indicator maps the LED
  state pointed into (Debian's xwayland-dbgsym located it). 0.3.79's
  keymap names "Caps Lock" and "Num Lock"; `xwayland_gate` runs Xwayland
  with `+accessx 1` and checks it outlives the timeout. With 0.3.79 the
  owner ran Steam and a game. `_MOTIF_WM_HINTS` (decor) moved forward
  from milestone C in 0.3.80: Steam draws its own frame and the desktop's
  stayed on top of it. A window moved to another display stopped
  updating: Hibr's hold waited for a `FRAME release` from the display the
  window had left, fixed in Hibr 0.99.488.
- **Milestone C (2026-11-11; released in 0.3.81).**
  What Steam showed, each X request onto the line its Wayland equivalent
  already produces, through one setter for both kinds, with no wire
  change (`src/blit_compositor.c`, the X section; `docs/api.md`, "X
  programs"):
  - override-redirect windows are `WINDOW popup` at their offset from
    their toplevel in X space; toplevels keep their own X position, a
    virtual layout Hibr does not see. Xwayland turns a pointer on any of
    its surfaces into a root position by that window's X position
    (`xwayland-input.c`, Xwayland 24.1), so Blit's member mapping and X
    agree. Grabs are X's: keys and presses in the program's windows reach
    the grabbing menu;
  - `WM_TRANSIENT_FOR` the parent; `WM_NORMAL_HINTS` `WINDOW limits`;
    `_NET_WM_STATE` full screen and maximize both ways;
    `_NET_WM_MOVERESIZE` `WINDOW move`/`resize`; `WM_CHANGE_STATE`
    Iconic `WINDOW minimize`; XFixes cursor names `WINDOW cursor`
    (`blit_pointer_name_shape`, moved to `src/blit_pointer_names.h` so the
    compositor asks Blit's one table).
  - Found by the gate, not by reading: a click on window 1 where its
    dialog overlapped it in X space went to the dialog (X's own stacking
    decides), hence X's stacking follows Blit's pointer; and a raise and
    a press sent in the same instant race inside Xwayland, which takes
    input before requests -- a person's press comes after the pointer got
    there, and the gate waits for the raise as a person would. Then, on
    the advice of a review, a dialog shown under a pointer at rest (the
    raise moved to its showing too) and a window past the root's edge
    (clamped, hence the window manager keeps toplevels inside the root).
  - Evidence: `xwayland_gate` (Xwayland 24.1 in the Debian 13 build
    environment) passes under tcc and ASan/UBSan with the stand-in's new
    keys -- menus as popups 2 and 3 at 30,40 (a key and a choice through
    their grabs, dismissed by a press outside); dialog `WINDOW new 4 1`
    with limits 150x80-300x200, shown under a pointer at rest, a press
    there with no move reaching window 1; popup 5 in the dialog by
    `WM_TRANSIENT_FOR` window 1 (`WINDOW close` logged, dismissed on
    deactivation); a submenu, popup 7, of window 1 by the menu under its
    origin while the pointer was on the dialog; a base size as limits;
    full screen and maximize on and off with the states written back;
    move and bottom-right resize with a press held, refused without one;
    `WINDOW minimize 1` from WM_CHANGE_STATE; a panel asking for
    1800,1000 placed at 1720,980, a press at its far corner landing,
    maximized before its map and given its parent after; `WINDOW cursor 1
    xterm`, and `default` for an unknown name. Hibr's side read: its
    focus loss sends `WINDOW size` without `activated` (`dt_natsize`),
    which is what deactivates X. `compositor_gate` and `pointer_art_gate`
    pass.
  - Pending: Steam's menus, dropdowns, title bar and cursors on the
    owner's machine. Open: a moving popup's line (the draft has none for
    either kind); an X program whose only window is override-redirect
    (some games) is not shown, nor one whose origin is in no X window
    while the pointer is in none (not gated: the gate cannot take the
    pointer away); a menu whose toolkit closes only on its own grab's
    click stays open after a click outside every X window; a toplevel
    larger than the root takes its pointer clamped past the root's edge;
    the raise-and-press race above, for synthetic input.
- **X11 programs on or off (0.3.81, 2026-11-11).** The owner asked for a
  lever per protocol. Wayland's is the desktop's existing Programs in
  Windows (the compositor itself); X's is `blit-compositor --x11 on|off`,
  which Hibr passes from its Control Panel's X11 Programs row (Hibr #504),
  applied at Restart Desktop by restarting the compositor. The gates' old
  `BLIT_COMPOSITOR_XWAYLAND=none` is gone: that variable only names a
  stand-in Xwayland now. `compositor_gate` runs with `--x11 off` and
  checks a value other than on or off is refused.
- **Milestone D (2026-11-11; in development, not released).** The X
  selections onto `CLIP`, with no wire change (`src/blit_compositor.c`,
  "X selections"; `docs/api.md`, "X programs" and "The clipboard"). X is
  one more owner and reader of the clipboard's two selections, through
  the clipboard module's own `bc_source` (an X source has no resource)
  and `clip_read` (an X paste reads the owner as a Wayland paste does):
  - X to everyone: XFixes' SelectionNotify names a new owner of
    CLIPBOARD or PRIMARY; honoured when its X client is the one with X's
    focus, its TARGETS are converted at its selection time into a window
    of the window manager's, and it becomes the selection's source --
    its best text target (UTF8_STRING, text/plain;charset=utf-8, STRING,
    TEXT, text/plain) as the five text aliases, x-kde-passwordManagerHint
    as itself, MIME targets as themselves; `CLIP offer` follows as for a
    Wayland copy. A read converts the target into a window of ours and
    streams it into the reader's pipe, STRING's Latin-1 made UTF-8, INCR
    a chunk at a time, the next asked for only when the reader has the
    last.
  - Everyone to X: for a Wayland source or the desktop's offer the window
    manager owns X's selection on a window of its own, again at every
    change, and gives it up when the selection is nobody's. The X client
    with X's focus gets TARGETS, TIMESTAMP, UTF8_STRING,
    text/plain;charset=utf-8, TEXT (as UTF8_STRING), STRING (Latin-1, `?`
    past it), the hint and a Wayland source's other types; the data
    comes from the owner through a pipe by `clip_read`, INCR in 64 KiB
    chunks from 64 KiB on.
  - Deadlines: every transfer ends after 3 s without progress -- property
    None before the answer; mid-INCR, a zero-length chunk when the owner
    stalled (the paste is short) or the requestor let go when it stopped
    deleting; a reader that stops taking an X owner's INCR is let go and
    the owner's rest taken and dropped, so the owner finishes. A transfer
    outlives a change of the selection: it is the old owner's to finish,
    as a Wayland source's pipe is.
  - Secrets: the hint travels both ways and is in TARGETS. A Vault
    secret the desktop clears (`CLIP clear`) leaves X at once: the window
    manager gives the selection up, so no X program can paste it after.
    What X cannot keep: a program that pasted it keeps its text (as a
    Wayland program does); an X clipboard manager reading while it was
    the selection sees it unless it honours the hint (KDE's and CopyQ's
    do), and only the X client with X's focus is answered at all, so a
    background one cannot read it; X programs can always talk to each
    other on selections of their own.
  - **Where ours differs from wlroots** (`xwayland/selection/`, MIT):
    targets are kept by name for STRING and the hint too (wlroots keeps
    UTF8_STRING, TEXT and names with a `/`, so an X password manager's
    secret would lose its hint and enter Hibr's history); TEXT is
    answered as UTF8_STRING and STRING as Latin-1 (wlroots maps TEXT to
    text/plain and passes the bytes as they are); each TARGETS request
    has its own window and only the newest ownership's answer counts
    (wlroots reuses one property per selection, so two owners in quick
    succession can mix); our windows are children of the window
    manager's own window, so the root never reports them; the requestor's
    window is selected for PropertyChange before INCR (wlroots hears
    deletes only on windows it already watches, so a requestor that is
    not a top-level -- as toolkits use -- stalls); every transfer has a
    deadline (wlroots has none, and a silent source hangs the
    requestor); the window manager's ownership is followed from XFixes,
    given up at its own selection time (never `CurrentTime`, which could
    clear a newer owner); a reader that is not the focused X client is
    refused, as only the focused Wayland program is offered the
    selection; an X ownership by a client without X's focus is logged
    and left to X until the selection next changes, not fought (an X
    clipboard keeper that takes the selection whenever it is lost would
    loop); no CLIPBOARD_MANAGER (an X program's selection ends with it,
    as a Wayland program's does; Hibr has read it by then); no drag and
    drop (ADR 0229's later row).
  - Evidence: `xwayland_gate` (Xwayland 24.1, Debian 13 build
    environment) passes under tcc and ASan/UBSan with leak detection: an
    X program's CLIPBOARD and PRIMARY read back byte for byte, and the
    desktop's pasted by it byte for byte; STRING both ways; INCR both ways
    (700001 bytes from X; 0, 65535, 65536 and 700001 to X, INCR from
    65536); the secret hint both ways, in TARGETS, and gone from X at
    `CLIP clear`; the owner changing mid-transfer both ways (the old
    bytes, whole); an X requestor that never takes its chunks, a desktop
    reader that never reads and an X owner that never answers, each
    ended at 3 s while the compositor answered `WINDOW size` within 0.1 s;
    two ownerships at once making one offer; a copy and a paste made
    after X's focus was taken away, ignored and refused; the Wayland
    stand-in's CLIPBOARD and PRIMARY pasted by X and X's CLIPBOARD pasted
    by it; a give-up as `CLIP clear`; Xwayland killed while an X program
    owned PRIMARY (`CLIP clear` with its window) and the desktop's
    CLIPBOARD pasted by an X program after the restart. The window
    manager's requestor in the gate is a child window, not a top-level.
    `compositor_gate` passes unchanged (its counts do not move: X is off
    there).
  - Open: a `CLIP read` pipe has no error: an X owner that stalls or
    changes mid-transfer reaches Hibr as a short text that looks whole
    (Hibr keeps what it read); the X focus rule is checked by client, so
    a program with two X connections pastes only on the one with focus;
    real programs (xterm's PRIMARY on selecting, a password manager's
    hint, Steam's fields) are to be checked on the owner's machine.
