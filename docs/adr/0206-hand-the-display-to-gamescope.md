# ADR 0206: hand the display to gamescope for full-speed games

- Status: proposed
- Date: 2026-11-03
- Scope: lifecycle, KMS ownership, physical input, sessions, API
- Depends on: [ADR 0083](0083-blit-root-owned-hibr-login-manager.md)

## Context

The owner wants Steam at full speed under Hibr, without a desktop X server
or a Wayland desktop. Valve's answer on SteamOS is gamescope running on its
own DRM backend: it owns the GPU and the display, runs a private Xwayland for
the Steam client and the game (both are X11 programs, so some X11 is
unavoidable), and puts their frames on the screen directly, with no
compositor between them.

Blit owns the display (DRM master) and the physical input (evdev) for the
whole machine. Two programs cannot both own them. Blit can already give
them up: when the console switches to another VT, the `VT_PROCESS` release
path (`src/blit_server.c`, `blit_kms_suspend` and `blit_vt_acknowledge`)
releases held keys and buttons, drops DRM master and suspends physical
input, and the acquire path takes them back and repaints. Input needs no
more than that: Blit never grabs an evdev device (`EVIOCGRAB` appears
nowhere), so another program can read the same devices, and while Blit's VT
is not active `native_console_accepts_event` drops every physical event, so
nothing a game is sent also reaches the hidden desktop. Nothing starts
another program on its own VT and returns when it ends.

Measured on the owner's mini PC (Debian 13, Radeon): `steam-installer`
and `seatd`/`libseat1` are available from apt; `gamescope` is not.

## Options considered

1. **Hand over the whole machine (SteamOS's model).** gamescope runs in its
   own logind session on a free VT with `--backend drm`; Blit switches to
   that VT and its existing release path gives up the display and input;
   when gamescope exits Blit switches back and repaints. Full speed, direct
   scanout, lowest latency, and it reuses code that is already exercised.
   While a game runs, the desktop is not visible on any display.
2. **Lease one display.** Blit gives gamescope a DRM lease on one connector
   (`drmModeCreateLease`) and keeps the others. The desktop stays on DP while
   a game runs on HDMI. It needs gamescope to accept a leased DRM fd, which
   is unverified, and input must be split between two owners. Kept as the
   next step.
3. **gamescope in a window.** gamescope runs headless and its frames reach
   Blit as dma-bufs to compose like any surface. The most flexible, but it
   adds a composition step and latency, and needs the zero-copy dma-buf
   import that is still future work. Kept for later.
4. **Run Steam under an X server or a Wayland compositor on top of Blit.**
   Rejected: that is the desktop-stack overhead the owner wants to avoid.

## Decision

Option 1.

- **The session.** Blit starts gamescope as the signed-in user in a proper
  logind session on a free VT of seat0 (a PAM session with that TTY, e.g.
  through `systemd-run` with `PAMName=` and `TTYPath=`), so gamescope's
  libseat (logind backend) is granted the GPU and input devices the normal
  way. Blit does not pass it file descriptors or run it as root.
- **The hand-over.** Blit activates that VT. Its existing release path runs
  unchanged: held keys and buttons are released to their owners, DRM master
  is dropped, physical input is suspended.
- **The return.** When the gamescope process exits (normally, by crash or
  by a kill), Blit activates its own VT again; the acquire path takes DRM
  master back, resynchronises modifiers and repaints everything. The session
  ends with its process, so nothing of it is left running.
- **API.** `POST /v1/control/handover` with the command to run (gamescope
  with its arguments, e.g. `gamescope --backend drm -e -- steam
  -gamepadui`); one hand-over at a time (409 while one runs). `GET` reports
  the running one with its pid, VT and start time; `DELETE` ends it.
  Diagnostics and the journal log the start, the release, the return and
  the exit status.
- **Hibr** owns the launcher: a Steam app and a Control Panel entry that
  call the API. Blit owns the hand-over.
- **gamescope** comes from a package the project builds (hibr-apt) or from
  Debian once it is there; Blit does not vendor it.

## Consequences

- Games run as on SteamOS: their frames go from the GPU to the display with
  nothing of Blit or Hibr in the path.
- While a game runs, the Hibr desktop is not shown and Blit's API answers
  but draws nothing; the performance panel cannot show the game.
- A gamescope that hangs without exiting keeps the display until it is
  ended through the API, over ssh, or with a VT switch key, which Blit's
  acquire path already handles.

## Open questions, to settle before this is accepted

- Whether gamescope's DRM backend runs on the mini PC's Radeon from a
  logind session started this way, and with `seatd` instead of logind.
- Audio: Steam and the game use the user's PipeWire, which runs in the
  user's systemd instance and is unaffected by the VT; to be confirmed.

## Evidence

To come with the implementation:

- A gate with a stand-in for gamescope (a program that opens the DRM device
  through libseat, draws one frame and exits): Blit releases DRM master and
  input, the stand-in gets both, and on its exit Blit reacquires, repaints,
  and reports the exit status; a second hand-over while one runs is a 409.
- On the mini PC: Steam Big Picture under gamescope, frame rate and
  latency against the desktop, and the return to the desktop.
