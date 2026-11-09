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
- **Dependencies.** libwayland-server and libxkbcommon are loaded at run
  time, with Blit's own declarations, as libei is (`blit` gains no hard
  dependency); the protocol code generated from pinned protocol XML is
  committed or built from a pinned wayland-scanner in `build/deps`.
  Xwayland is Recommended, not Depended. No wlroots.
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

0. **Baseline, before any code.** Key-to-pixel latency and frame delivery
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

- Build host: the protocol XML (wayland, wayland-protocols 1.44) and a
  wayland-scanner from Debian's packages, installed by the owner or pinned in
  `build/deps`; a check that the generated code builds under tcc (a
  downloaded scanner could not be run here).
- A host with Xwayland 24.1 for the phase 2 gates (the mini PC and 10.52
  have 24.1.6; the build host has none).

## Evidence (planned)

Phase 0's numbers; per phase, the stand-in-client gates, the full and GLES
suites, and the owner's live check named above.
