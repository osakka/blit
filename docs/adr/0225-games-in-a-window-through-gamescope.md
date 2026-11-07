# ADR 0225: Steam and games in a window, through a headless gamescope

- Status: proposed
- Date: 2026-11-07
- Scope: a new producer program for the frame channel (`blit-gamescope`),
  input forwarding from a Blit session to a nested compositor, gamepad
  ownership, packaging
- Depends on: [ADR 0223](0223-frames-by-dmabuf.md) (frames by dmabuf),
  [ADR 0202](0202-local-frames-by-descriptor.md) (the frame channel),
  [ADR 0198](0198-pointer-lock.md) (pointer lock),
  [ADR 0206](0206-hand-the-display-to-gamescope.md) (full screen by hand-over)

## Context

The owner wants Steam and its games under Hibr, both full screen and in a
window, and has put Blit in charge of making it work end to end; the Steamer
app (a Hibr app that launches Steam URLs and reads manifests) follows. Full
screen is ADR 0206: hand the display to gamescope on its own VT. This record
is the window.

**Blit draws cells and pictures; anything that needs a window system is a
producer behind the frame channel.** That rule already holds for video and
the Browser (ADR 0202) and is what keeps Blit free of a desktop stack. The
Steam client is a CEF program and games are X11 or Wayland clients (Proton
through Xwayland), so a Wayland/X server has to exist somewhere. The
question is whether Blit writes one or reuses one.

What was established on 2026-11-07:

- **gamescope 3.16.22** is in Debian 13 through trixie-backports
  (`3.16.22+ds-1~bpo13+1`, built with PipeWire and libei, OpenVR off) and is
  installed on the owner's mini PC at `/usr/games`. ADR 0206's "gamescope is
  not in Debian" is out of date.
- **Its headless backend** (`--backend headless`) composites with Vulkan on
  the GPU, takes no DRM master and no seat, and needs only a render node the
  user can open (`src/Backends/HeadlessBackend.cpp`: not session based,
  `Present` is a no-op, a timer at `-r`).
- **It exports frames** as a PipeWire `Video/Source` named `gamescope`: BGRx
  (DRM `XR24`) with modifier LINEAR, as dmabufs when the consumer asks for a
  modifier, else memfd; whole frames, at most one per refresh, dropped when
  no buffer is free (`src/pipewire.cpp`). It waits on the CPU for its GPU
  work before handing a buffer over, so a buffer is complete when it arrives
  (no acquire fence). The cursor is not in the stream.
- **It takes input** through an EIS server (libei) on a per-user socket
  `$XDG_RUNTIME_DIR/<display>-ei`: keyboard (evdev codes), relative and
  absolute pointer, buttons, scroll; no touch, no gamepad; no root needed.
- **Smoke test on the mini PC** (Radeon, RADV): `gamescope --backend headless
  -W 640 -H 480 -r 60 -- vkcube`, with a small PipeWire consumer built for the
  purpose: "Successfully initialized libei for input emulation!", a stream on
  node 50, 30 of 30 frames non-blank about 16.7 ms apart, the last one the
  vkcube image; asked for DMA-BUF with LINEAR it delivered dmabufs (`XR24`,
  stride 2560, rotating four buffers). Blit 0.3.60 advertises `XR24/0`, so
  the stream matches FRM3 with no copy. **Gamescope issue #1984** (the
  headless stream empty or pointer-only) did not occur with vkcube; every
  report of it was `-e --backend headless -- steam`, and that case is
  untested here, so the risk is not retired (first open question).
- **Input today:** Blit never grabs evdev (`EVIOCGRAB` is nowhere), so any
  process that can open `/dev/input/event*` sees the same events. On the mini
  PC the user is in `input`. Headless gamescope does not read evdev; Steam
  Input and SDL in games open gamepads directly.

## Options considered

1. **Build a Wayland compositor into Blit.** Blit would host xdg-shell,
   linux-dmabuf, seats, pointer constraints and Xwayland. Rejected: it makes
   Blit a desktop compositor, the weight it exists to avoid, and would
   reproduce years of Steam-specific behaviour (overlay, focus by appid, Big
   Picture) badly.
2. **A Hibr module that is a compositor.** The same mistake one layer up:
   Hibr is a shell in terminal cells.
3. **Reuse gamescope headless behind a small bridge** (this decision). It is
   Valve's, in Debian, already on the target machine, Steam-aware, and its
   output and input interfaces are standard protocols (PipeWire, EIS) that
   match Blit's frame channel and the bridge's needs.
4. **A wlroots compositor (sway headless, or a small one of our own).**
   Exports dmabufs (wlr-screencopy) and takes virtual keyboard/pointer, but
   is not Steam-aware. Kept as the fallback if gamescope's headless path
   regresses (see "Independence").

## Decision (proposed)

Option 3: **`blit-gamescope`**, a small program shipped by Blit as its own
package (Suggested by `blit`, like `blit-font-sources`), run as a **Hibr
desktop app** (a module and a window, the shape Boxer has), which:

1. **Starts gamescope** as the session's user:
   `gamescope --backend headless -W <w> -H <h> -r <refresh> [-e] -- <command>`
   with `PATH` including `/usr/games` and the user's `XDG_RUNTIME_DIR`.
   `<w>x<h>` is the pane's size in pixels as the terminal reports it
   (XTWINOPS `CSI 14 t`, which Blit answers), so gamescope renders 1:1 into
   the picture's placement and Blit composites it without resampling;
   `<refresh>` is the pane's display refresh. `-e` (Steam integration) when
   the command is Steam. gamescope paints only when a window commits a new
   frame, so a static game costs nothing.
2. **Carries frames** from gamescope's PipeWire node to Blit: connects as a
   consumer asking for `BGRx` with modifier LINEAR as DMA-BUF, and forwards
   each buffer's fd as an FRM3 frame (`XR24`, modifier 0, one plane, the
   chunk's stride, no fence: the buffer is complete on arrival). It holds
   each PipeWire buffer until Blit's `RELEASE <id> <seq>`, and also on any
   `ERROR`, before giving it back to the stream; it asks gamescope for five
   or six buffers (gamescope keeps one, Blit may hold a shown and a pending
   one). The picture is placed in the pane with Kitty escapes (`a=p`) as
   every frame-channel picture is. No copy anywhere; if Blit answers
   `dmabuf=0` (CPU compositor) the bridge asks PipeWire for memfd buffers and
   sends version-1 frames instead. The same fallback holds mid-stream: on
   `ERROR <id> ENOTSUP` (a view that cannot import joined, or an output fell
   back to the CPU; ADR 0223) the bridge resends by memfd and stays on memfd
   for the rest of the connection; only a new hello tries dmabuf again. The
   bridge never stalls on its pool: with no buffer free it drops that frame
   (newest wins, ADR 0202) and grows the pool, up to six. Where several views
   show the picture (Hibr's `hold`), the relay sends the producer one
   `RELEASE` per frame, after the last view has let it go.
3. **Carries input** from the pane to gamescope's EIS socket: Kitty keyboard
   protocol press/release → `ei_device_keyboard_key`; SGR-Pixels mouse
   (mode 1016) → absolute pointer, scaled from pane pixels to `<w>x<h>`;
   buttons and wheel → EIS buttons and scroll. When the game captures the
   mouse, the bridge turns on pointer lock (`?7701`, ADR 0198) and forwards
   its raw deltas as relative motion, which is what first-person games need.
   Input reaches the game only while the pane has focus.
4. **Resizes** with the pane, and never restarts gamescope (a restart ends
   Steam and the game): it asks the stream for the new size
   (`SPA_FORMAT_VIDEO_requested_size`); if gamescope does not follow, the
   stream keeps its size and Blit scales the picture into the pane through
   the placement's cell size (`c=`/`r=`), aspect kept, as it scales any
   picture. The bridge never resamples.
5. **Ends** with its child: when gamescope exits, the bridge deletes the
   picture (`a=d`) and exits with gamescope's status.

**Moving, resizing, minimising and full screen** are the window's own, done
by the desktop as for any window; the game keeps running through every one.
Full screen from a window is the window filling the display (Hibr's Full
Screen and Leave Full Screen), the same gamescope process, resized as in
item 4. The display hand-over of ADR 0206 is a separate way to launch, for
the lowest latency; it is chosen at launch, and nothing ends a running game
to move between the two.

**Blit's own work items**, needed by the bridge and useful beyond it:

- **Physical key codes to a session.** Kitty's flag 8 reports layout
  codepoints, and the bridge would have to guess evdev codes back (lossy on
  non-US layouts). Blit reads evdev itself, so it adds the base-layout key to
  its Kitty keyboard reports (flag 4, alternate keys) so the bridge maps keys
  exactly. *Done in 0.3.61*, with focus reports (`CSI O`/`CSI I`, mode 1004)
  when Blit hands physical input to another VT and takes it back.
- **Gamepads: Steam owns them while a game runs.** EIS has no gamepad and
  uinput needs root, so the pads are not forwarded. Steam Input and SDL read
  them directly. So that a pad does not drive the desktop and the game at
  once, the bridge, when it starts, asks Blit through the existing input
  routes API (`GET`/`PUT /v1/input/routes`, as the session's user) to stop
  routing gamepads to sessions, and restores the routes it changed when it
  ends, including on a crash (Blit restores them when the bridge's session
  ends, so a killed bridge cannot leave the pads disabled). No new protocol.
- **The cursor.** gamescope's stream has no cursor, so Blit's own pointer is
  shown over the picture; Steam's hover cursor shapes are not reproduced.

**Input: Blit feeds gamescope's EIS directly (decided 2026-11-07).** A
game is not a terminal program. Blit is the one process that has the raw
keyboard and mouse (evdev), and EIS is the standard way to hand input to a
compositor; carrying a game's input as terminal escapes would take five
translations (evdev, Kitty text, the PTY, `hold`, Hibr's decode and
re-encode, the bridge's parse) to rebuild what evdev already has: press,
repeat and release, physical keys, relative motion. So the bridge asks Blit,
on its authenticated frame channel, to route input to gamescope's EIS socket
(`INPUT eis <path> <image-id>` after the hello, then `INPUT on`/`INPUT off`
as its window gains and loses the keyboard; wire text in `docs/api.md`; the name comes from an
authenticated, uid-checked peer, never from the terminal stream, so ADR 0066
holds). While the route is on and the bridge's session has Blit's focus (inside
Hibr only the desktop knows which window has the keys, so `on`/`off` come
from its focus callback through the bridge), Blit sends evdev key codes with
real press and release (the compositor repeats), the absolute pointer over
the picture's drawn rectangle scaled to its latest frame size (so a scaled
or moved window needs no message), buttons and scroll, and relative motion while the pointer is locked;
focus loss, the desktop's Release Pointer and the bridge's exit end the route
and release every held key and button, as Blit already does when it gives up
the console. libei is loaded at run time, so `blit` gains no hard
dependency. The bridge has no input parser. Kitty flag 4 and Hibr's key
phases (#485) remain right for terminal programs and Boxer-class apps but no
longer carry a game's input. This replaces the terminal-input path below
where they disagree.

**One program, one frame output, one input parser.** `blit-gamescope` sends
FRM3 frames on whatever `BLIT_FRAME_SOCKET`/`BLIT_FRAME_TOKEN` its
environment names, and reads Kitty keyboard, SGR-Pixels and `?7701`
sequences on its standard input, exactly what a PTY delivers; its output is
gamescope's EIS. Who sets that environment decides the layer, not the bridge:
in a plain Blit session Blit sets the socket and the PTY carries the keys; as
a Hibr desktop app the module launches it with a frame socket served by the
console (relayed through `cn_image`, `hold` and Blit) and writes its key and
mouse events to the bridge's input as the same sequences. One wire format per
stream in both places, and no second code path in the bridge.

**Phases.** (1) The bridge standalone in a plain Blit session on the owner's
machine: Steam playable in a pane, every Blit-side piece proven and measured
(`graphics.frames.dmabuf`, no uploads, presents per second, key-to-pixel
latency against ADR 0206). (2) The Hibr desktop app, once Hibr's key phases
(#485), its dmabuf relay and the console's app-facing frame socket exist.
Steamer's launch instructions follow phase 2.

**The mouse: absolute by default, captured on a click, released by the one
release key.** gamescope's stream has no cursor and nothing outside gamescope
knows when a game grabs the mouse, so the bridge starts in absolute mode
(Blit's own cursor over the picture, right for Steam's menus) and captures
the mouse (pointer lock, relative motion) on a click in the pane, as Boxer
does. It has no key of its own: ADR 0198 gives one release key, the
desktop's Release Pointer shortcut (Hibr's, `ctrl-f10` by default,
rebindable), and Blit also ends a lock on focus change, exit or reset. The
bridge treats Blit's `CSI ? 7701 ; 2 $ y` notice and a focus-out (`CSI O`) as
the end of capture: back to absolute, held keys and buttons released to EIS.
It gives the lock back itself only when the game exits.

**Why a desktop app, not a program in a Terminal window.** One source of
truth for each stream. Frames: ADR 0202 made Hibr's console (`cn_image`) the
one writer of every picture; a desktop app hands its frames to that writer,
where a Terminal program would open a second frame connection beside it.
Input: window focus lives in Hibr, so keys reach the bridge by the path every
desktop app uses, one encoding (Blit's Kitty reports) and one decoding (the
console).

**Hibr's work items.** Everything the bridge needs crosses `hold` and the
desktop:

- **Frames:** `hold` relays memfd frames today (ADR 0202) but not dmabuf
  frames. It must pass FRM3 frames and their plane fds through to the views
  that show the picture, and the `RELEASE` replies back, as it does memfds.
  Without this the bridge works only with the memfd fallback.
- **Input: one key event, extended, not a second one.** The desktop's
  console today pushes only Kitty flag 1 and turns keys into names, so no
  desktop app sees a key release, which gamescope's EIS needs. The existing
  key event gains a phase (press, repeat, release) and the base-layout key
  (Kitty flag 4, which Blit adds); a window that wants phases declares it,
  the way the Kitty protocol does, and the same event then arrives more often.
  Default dispatch stays press-only, so other apps are unchanged. The console
  pushes Kitty flags 1, 2, 4 and 8 to Blit for the whole display (`hold`
  already passes flags up to 31). Mouse (SGR-Pixels) and pointer lock reach a
  desktop app as they reach Boxer today (`_mouse`, `dt_pointerlock`).
- **Pixel size:** a desktop app knows its window's pixels; `hold`'s emulator
  also gains XTWINOPS `CSI 14 t`/`16 t` (it answers only `CSI 18 t` today).
- **Launch:** Steamer (and a Control Panel entry) open a pane running
  `blit-gamescope -- steam ...`.

**Independence.** The bridge talks only PipeWire (frames in) and EIS (input
out); the two gamescope-specific details (finding the node by name, asking
for an appid's window) live in one file. If gamescope's headless path
regresses, a wlroots compositor fits behind the same bridge.

## Consequences

- Steam and games run in a Hibr pane at the GPU's speed with no copy between
  gamescope and the screen; one extra GPU composite (gamescope's) and about
  one refresh of latency more than the full-screen hand-over.
- Blit gains no window-system code. The new package carries a PipeWire and a
  libei dependency that `blit` itself does not.
- Steamer launches `blit-gamescope -- steam ...` (window) or the hand-over
  API (full screen); it does not talk to gamescope.
- Keyboard and mouse can reach a game only through Blit; a game that opened
  keyboard event nodes itself would also see keys meant for the desktop.
  That is not done by X11/Proton games, which take input from Xwayland.

## Open questions, to settle before this is accepted

- **Issue #1984 with Steam: settled 2026-11-07.** `gamescope --backend
  headless -W 1280 -H 720 -r 60 -e -- steam -gamepadui` on the mini PC: 120 of
  120 frames live (Steam's Big Picture sign-in screen, clock and background
  moving), also as LINEAR dmabufs; no empty or pointer-only stream.
- Steam's overlay and a Proton game in the pane (Big Picture itself is
  verified, below).
- Prerequisites on the target: `steam-installer` and i386 multiarch,
  `xwayland`, and `libei1` (the EIS client library the bridge links; gamescope
  itself pulls `libeis1`); which the `blit-gamescope` package depends on and
  which the user installs.
- Latency measured end to end (key press to pixel) against ADR 0206.
- Whether gamescope honours `requested_size` for live resizes (if not, Blit
  scales; the game is never restarted).

## Evidence (planned)

- A gate for the bridge with a stand-in producer: a PipeWire video source
  that emits known dmabuf frames into the bridge, checking that Blit draws
  them, that buffers come back only after `RELEASE` or `ERROR`, the memfd
  fallback under the CPU compositor, and the input mapping (keys, absolute,
  relative, buttons, scroll) against an EIS stand-in.
- On the mini PC, with the owner's OK: vkcube, then Steam's Big Picture, then
  one Proton game in a pane: frame rate, latency and the frame channel
  counters (`graphics.frames.dmabuf`: accepted, released, no uploads).
