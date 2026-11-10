# ADR 0225: Steam and games in a window, through a headless gamescope

- Status: superseded by [ADR 0228](0228-blit-is-the-compositor-for-programs.md);
  deprecated 2026-11-10 by the owner, removed in 0.3.75 (blit-gamescope,
  the libei input path and their gates)
- Removed: in 0.3.75 -- the bridge (`src/blit_gamescope*.{c,h}`) and its
  package, the libei backend of the input route (`INPUT eis`), the EIS and
  stand-in libei gates; the route model and its hold modes stay, for
  blit-compositor's `INPUT sock` routes (ADR 0228). `blit` Breaks
  `blit-gamescope` and `steamer`, retired with it
- Date: 2026-11-07
- Scope: a new producer program for the frame channel (`blit-gamescope`),
  input forwarding from a Blit session to a nested compositor, gamepad
  ownership, packaging
- Depends on: [ADR 0223](0223-frames-by-dmabuf.md) (frames by dmabuf),
  [ADR 0202](0202-local-frames-by-descriptor.md) (the frame channel),
  [ADR 0198](0198-pointer-lock.md) (pointer lock),
  [ADR 0206](0206-hand-the-display-to-gamescope.md) (full screen by hand-over)
- Amended: lines in v5 grammar since 0.3.73 (ADR 0228)

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
3. **Asks Blit for input, carrying none itself:** once gamescope's EIS
   socket exists, it sends `INPUT eis <path> <image-id>` on its frame
   channel and, in phase 1 only, `INPUT on <image-id>` once Blit answers
   `OK input <image-id>` (with `--no-place`, phase 2, the desktop app sends
   `INPUT on`/`off` itself; see "`--no-place` for phase 2" below), and Blit
   sends the physical keyboard and mouse to that
   socket (see "Input: Blit feeds gamescope's EIS directly" below; the wire is
   in `docs/api.md`). The bridge has no input parser and never captures the
   mouse; the desktop does, with its Release Pointer, and phase 1 has no
   capture (see "The mouse" below).
4. **Resizes** with the pane, and never restarts gamescope (a restart ends
   Steam and the game). There is no `requested_size`: the stream stays at
   gamescope's output size (`-W`/`-H`, from the pane's pixels at launch),
   which is also gamescope's EIS pointer coordinate space, and on a resize
   the bridge only places the picture again; Blit scales it into the pane
   through the placement's cell size (`c=`/`r=`), aspect kept, as it scales
   any picture. The bridge never resamples.
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
(`INPUT eis <path> <image-id>` after the hello, then `INPUT on <image-id>`/`INPUT off <image-id>`
as its window gains and loses the keyboard; wire text in `docs/api.md`; the name comes from an
authenticated, uid-checked peer, never from the terminal stream, so ADR 0066
holds). While the route is on and the bridge's session has Blit's focus (inside
Hibr only the desktop knows which window has the keys, so `on`/`off` come
from the app's focus callback through Hibr's listener, not from the bridge),
Blit sends evdev key codes with
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

**One program, one frame output, no input parser.** `blit-gamescope` sends
FRM3 frames and its INPUT request on whatever `BLIT_FRAME_SOCKET`/`BLIT_FRAME_TOKEN`
its environment names, and reads on its standard input only its pane's
pixel size (`CSI 4 ; h ; w t`), focus reports and the `?7701` lock notice.
Who sets that environment decides the layer, not the bridge: in a plain
Blit session Blit sets the socket; as a Hibr desktop app the module launches
it with a frame socket served by the console (relayed through `cn_image`,
`hold` and Blit), and the app turns its focus callback into `INPUT on`/`off`.
One wire format per
stream in both places, and no second code path in the bridge.

**Routes per image, not per connection (decided 2026-11-07, 0.3.64).** Hibr
multiplexes every picture of a desktop over one frame connection per
display (the console is the one writer of pictures, ADR 0202), so a route
per connection would make a second game window `EBUSY` (Hibr #486). Each
INPUT line therefore names its image (`INPUT on <image-id>`,
`INPUT off <image-id>`) and each answer carries it (`OK input <image-id>`,
`ERROR INPUT <image-id> <code>`); one connection holds a route per image,
each with its own libei context, devices and held keys and buttons, and
`EBUSY` means only the same image twice on one connection. A route ends
with its image or its connection, and a late `ENOENT` ends only its own.
**At most one route is on per session**, across all of its connections:
`INPUT on <image-id>` turns the others off, releasing what they held first,
so two windows never both get the keys even if an `off` is lost; the
pointer and relative motion go to the live route only. `frames.input`
lists the session's routes (`image`, `state`, `region`) beside its
`events` and `last_error`. There is no older form: the unnumbered `OK input`,
`ERROR INPUT <code>` and bare `INPUT on`/`off` are gone.

**`--no-place` for phase 2.** In a Hibr desktop app the desktop places the
picture (it is the one writer of pictures), and the bridge's standard
output is for its log, not for pictures. `blit-gamescope --no-place` (with `-W`/`-H`,
since it then asks the pane nothing) never writes Kitty `a=p` or `a=d`; with
`-W`/`-H` the bridge already sends no `CSI 14 t` at start, and with
`--no-place` none on `SIGWINCH` either. It writes no terminal set-up either:
its standard output carries only its log. Nor does it send `INPUT on` or
`INPUT off`: the desktop owns window focus, so the app sends
`INPUT on <image-id>`/`INPUT off <image-id>` itself from its focus callback,
through Hibr's listener, and the bridge sends only `INPUT eis <path>
<image-id>` and logs the answers. Without `--no-place` (phase 1, the pane is
the window) the bridge sends `INPUT on <image-id>` on `OK input <image-id>`.

**Phases.** (1) The bridge standalone in a plain Blit session on the owner's
machine: Steam playable in a pane, every Blit-side piece proven and measured
(`graphics.frames.dmabuf`, no uploads, presents per second, key-to-pixel
latency against ADR 0206). (2) The Hibr desktop app, once Hibr's key phases
(#485), its dmabuf relay and the console's app-facing frame socket exist.
Steamer's launch instructions follow phase 2.

**The mouse: absolute, and the bridge never captures it.** gamescope's
stream has no cursor and nothing outside gamescope knows when a game grabs
the mouse, so the pointer is absolute (Blit's own cursor over the picture,
right for Steam's menus). Capture (pointer lock, relative motion) belongs to
the desktop, which has the one release key of ADR 0198: in phase 2 the Hibr
app captures and releases through the desktop's own mechanism
(`dt_pointerlock`, ended by the desktop's Release Pointer shortcut, Hibr's
`ctrl-f10` by default, or a focus change), as Boxer does. Phase 1 has no
capture: a bare Blit pane has no release key, so a lock taken there could
not be undone. The bridge turns on no mouse reporting (Blit sends the
pointer to gamescope itself); it keeps focus reports (1004) and the size
query, and only logs Blit's `CSI ? 7701 ; 2 $ y` notice and focus out and
in, since a desktop may still lock the pointer. Blit releases held keys and
buttons to EIS when a route stops being live.

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
- **Launch (owner, 2026-11-08):** the desktop side is **Screens**, one Hibr
  app that X Screen folds into: one program list kept by a Control Panel
  pane, each program a menu entry with an engine of its choosing -- X
  Screen (Xvfb, the CPU, X11 only) or Wayland Screen, a window that runs
  `blit-gamescope --no-place [--steam] -W -H -r --log -- <command>` (the GPU,
  X11 and Wayland); Wayland Screen is the default where blit-gamescope is
  installed, and nothing is deprecated.
  Per-program options are the bridge's (Steam integration, keys to the
  program, pointer capture on click, close or keep when it ends); size and
  refresh come from the window and the display. Steam is one built-in entry,
  shown when Steam is installed, with pointer capture off by default: under
  capture Blit hides its pointer and gamescope's own cursor is not in the
  stream, so Big Picture's menus would have no visible pointer (seen live on
  the mini PC; lifted by "Hold modes", below). The earlier Steam-only app (Steamer, then steamwin) folds
  into it.

**Hold modes, owner 2026-11-09 (0.3.70).** Under capture (the desktop's
`?7701`) Blit hid its pointer and sent gamescope raw relative motion, and
gamescope's stream carries no cursor, so in Steam's menus there was no
pointer to be seen. Drawing a pointer of Blit's own driven by the same
relative stream was considered and rejected: checked against gamescope
3.16.22's source, the two would drift apart, and nothing tells Blit where
gamescope's pointer has got to. So each route has a hold mode,
set by the desktop with `INPUT hold <image-id> relative|confine` (wire text
in `docs/api.md`):

- `relative`, the default, is the behaviour above: for games that turn with
  the mouse and want endless motion.
- `confine`, for menus and pointer-driven games: while the session holds
  `?7701`, the route is live and its picture is drawn on the pointer's
  output, Blit's pointer stays shown with its usual shape, moves as an
  unlocked pointer does and is kept inside the picture's drawn rectangle
  (its topmost placement, the one the absolute mapping tries first), and
  the route is sent that point as an **absolute** position through the
  same mapping, never relative motion. Absolute positions are the one way
  to make gamescope's pointer and the drawn one the same point: gamescope
  warps its pointer to what it is sent (`wlserver_mousewarp`), so there is
  nothing to drift. What the lock adds over no lock is that the pointer
  cannot leave the window and that the desktop's other windows hear
  nothing, which is what capture is for.
- Buttons and the wheel go to the route as in either mode, and the
  session still gets the lock's raw delta reports, which the desktop
  ignores for this window; nothing else about the lock changes. Where the
  picture is not drawn, or the pointer is on another output, the lock is
  `relative`, so a hold never strands the pointer.
- The hold is still the desktop's to take and release (ADR 0198):
  `INPUT hold` takes and gives back nothing, and Release Pointer, a focus
  change, a VT switch, the image deleted or the route ending end the lock
  exactly as before. The pointer then stays where it was, inside the
  picture.
- One function, `frame_input_confined` (`src/blit_frames_input.h`),
  answers "is this lock confined, and to which rectangle"; the pointer's
  visibility (`update_pointer_shape`) and its motion
  (`update_pointer_position`, `flush_mouse_motion`) both ask it.

With `confine`, the Screens app can leave pointer capture on for Steam,
which "Launch" above had to turn off.

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
  `xwayland`, and `libei1` (the EIS client library Blit loads at run time for
  the input route; gamescope itself pulls only `libeis1`, so `libei1` is not
  on the mini PC yet); the bridge itself needs only `libpipewire-0.3-0`.
- Latency measured end to end (key press to pixel) against ADR 0206.
- **Live resize: settled 2026-11-07, then reversed.** gamescope 3.16.22
  honours `requested_size` only to shrink (its `calculate_capture_size`
  scales the capture down, never up): on the mini PC an 880x660 stream went
  to 587x440 for a 660x440 pane, and back to 880x660 when the pane grew to
  1100x880, where Blit scaled it into an 1100x836 placement, aspect kept.
  The bridge no longer asks for it: gamescope's EIS pointer coordinates are
  its output's pixels (`-W`/`-H`; its region is INT32_MAX square and
  absolute x/y go straight to `wlserver_mousewarp`), and Blit maps the
  pointer to the frame's size, so a shrunk stream would misplace the
  pointer. The stream stays at gamescope's output size and Blit scales it.

## Evidence

- **Phase 1 frames, 2026-11-07** (`src/blit_gamescope.c`,
  `gamescope_bridge_gate`): on the mini PC against Blit 0.3.60, vkcube
  (640x480) sent 703 dmabuf frames in 12 s and Steam Big Picture (1280x720)
  2153 in about 39 s, both about 60 fps, none refused for import, every
  buffer released, no memfd frames; no gamescope, Steam or vkcube process
  left after the session ended. The gate covers the hello, the exact FRM3
  bytes, release order, the `ENOTSUP` fallback to memfd, the INPUT answers
  and the terminal reader, and draws memfd frames in a real Blit session.
  Not yet: what the frames show on a placed pane, any input, a Proton game.
- **Blit's input route, 2026-11-07** (`src/blit_frames_input.h`, 0.3.62,
  `frame_input_gate`): the `INPUT` request as `docs/api.md` defines it.
  On a host with no libei: `ENOTSUP`; against a stand-in libei, `EINVAL`
  for 18 malformed or unknown-image requests, `ENOENT` for a missing path,
  a regular file, a link to a socket and a socket nobody listens on,
  `EACCES` for the system D-Bus socket (another user's), `OK input`, then
  `EBUSY`, and `frames.input` going off, on, live, off, live and none with
  INPUT on/off, focus and the channel's close. Against a recording stand-in
  libei that plays a compositor (one seat, one device, a 320x160 region):
  keys pressed and released in order, the kernel's repeats not sent,
  nothing while off, unfocused or with the console given up
  (`BLIT_TEST_VT_SIGNALS`), every key and button released and emulation
  stopped on `INPUT off`, a focus change, the console given up, the channel
  closed and the image deleted; wheel notches as discrete scroll; the
  absolute pointer scaled exactly to the compositor's 880x440 region (not
  the 320x160 frames) at a 160x80 and a 320x160 placement and after
  resizing to 64x32, the picture's far corner reaching the region's,
  nothing outside the picture; relative motion under `?7701` summed exactly
  (70,-30); the compositor dropping a live route answered
  `ERROR INPUT ENOENT` at once and the route ended. Decided the same day:
  an image without a frame yet may be named, the pointer is scaled to a bounded
  EIS region, else to the frame (gamescope's region is unbounded, and its
  stream stays at its output size, so the frame is its coordinate space), and a later refusal or drop is told on the wire. The same cases against libeis
  (`tools/tests/eis_receiver.c`, `frame_input_eis_gate`) need libei1 and
  libeis1 and are not run yet: they are for the Debian 13 test host.
  The bridge's half (`INPUT eis` after the hello, `INPUT on` on `OK input`,
  and under `--no-place` no `INPUT on`) is in `gamescope_bridge_gate`.
- **Live input, 2026-11-07, mini PC** (the bridge in a bare Blit pane, input
  by the route): keys reached vkcube (Space paused it, Escape closed it, 978
  events). Two design faults were found and fixed in the bridge.
  Capture-on-click left the owner stuck: the click asked for pointer lock,
  a bare pane has no release key, the pointer was hidden and unreachable,
  and only Escape, which closes the game, got them out; the bridge now never
  captures (above). gamescope's EIS region is INT32_MAX square (its output's
  pixels, unbounded): Blit now treats an unbounded region as none and maps
  the pointer to the frame (main `625f546`), so the bridge no longer shrinks
  the stream with `requested_size` (above). A late `ERROR INPUT ENOENT`
  after `OK input` is logged as the compositor gone, not as a Blit without
  input. `gamescope_bridge_gate` covers clicks and the wheel giving no event
  and no `?7701h`, no mouse modes, and the late ENOENT.

- **Routes per image, 2026-11-07** (0.3.64, `src/blit_frames_input.h`,
  `frame_input_gate`, `gamescope_bridge_gate`): on any host, against the
  stand-in libei, which now records each route to its own socket's log and,
  for order across routes, to one shared file: two images on one connection
  get a route each and the same image twice is `EBUSY`; `INPUT on B`
  releases the key and button held through A and stops A before B starts;
  a late `ERROR INPUT <A> ENOENT` leaves B working; deleting one image
  releases, stops and ends only its route (`INPUT on` for it is then
  `EINVAL`); a second connection of the same session turning its route on
  turns the first's off, releasing first, and back. Unreadable lines are
  answered with image 0, readable ones with their id. The bridge parses the
  numbered answers, ignores another image's, and with `--no-place` writes
  no placement or delete (its placement goes only through `GsPlacement`,
  checked in the gate); run with `-W`/`-H` in a Blit session it wrote no
  `CSI 14 t` (it found no gamescope on the build host, so no frame and no
  `a=p` could have been reached either way). Against libeis, still not run.

- **Packaged, 0.3.66** (`packaging/build-deb.sh`, `package_artifact_gate`):
  `blit-gamescope_<version>-1_amd64.deb` carries `/usr/bin/blit-gamescope`,
  a README and a copyright file; `Depends: blit (= <version>-1), libc6,
  libpipewire-0.3-0, libei1, gamescope`, and `blit` suggests it. It is
  built against PipeWire 1.4.2's headers (Debian 13's libpipewire-0.3-dev
  and libspa-0.2-dev, docs/toolchain.md) and is byte-for-byte reproducible
  from any directory. The gate checks its control fields, its exact file
  list, and that the binary is the bridge at the package's version. Under
  `--no-place` the bridge no longer sends `INPUT on`/`off`; the desktop app
  does (`gamescope_bridge_gate`).

- **Hold modes, 2026-11-09** (0.3.70, `src/blit_frames_input.h`,
  `frame_input_gate`): `INPUT hold` answered `OK input 7` for `confine` and
  `relative` and `ERROR INPUT <id> EINVAL` for an unknown or missing mode,
  an image with no route and a malformed line; each route's `hold` in
  `frames.input`, `relative` by default. Against the recording stand-in
  libei: under `relative` the pointer hidden and still and the raw sum sent
  as relative motion, as before; under `confine` the pointer shown, moved
  by the motion, held at both far corners of a 160x80 picture, the
  compositor sent exactly the matching absolute positions and no relative
  motion, the holder its raw deltas, a click sent; the picture's placement
  removed, the pointer hidden and relative motion sent, and placed again,
  confined again; after `?7701l` the pointer where it was and ordinary
  absolute motion. Not yet run against libeis or gamescope.

## Evidence (planned)

- A gate for the bridge with a stand-in producer: a PipeWire video source
  that emits known dmabuf frames into the bridge, checking that Blit draws
  them, that buffers come back only after `RELEASE` or `ERROR`, the memfd
  fallback under the CPU compositor, and the input mapping (keys, absolute,
  relative, buttons, scroll) against an EIS stand-in -- done on Blit's side
  (`frame_input_gate`, `frame_input_eis_gate`, above), the bridge's PipeWire
  half not yet.
- On the mini PC, with the owner's OK: vkcube, then Steam's Big Picture, then
  one Proton game in a pane: frame rate, latency and the frame channel
  counters (`graphics.frames.dmabuf`: accepted, released, no uploads).
