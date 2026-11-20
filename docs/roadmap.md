# Blit roadmap

Where Blit is, and what comes next, in order. Numbers like #111 are the
internal issue tracker's, not GitHub's. What already works is in
[features](features.md); the plans and restart notes from the 0.1 and 0.2
lines are kept in [history](history/roadmap-0.1-0.2.md).

## Where we are (0.3.127, 2026-11-20)

Blit is the display server under Hibr on the owner's machine, released
continuously through Hibr's apt repository. SDL is gone (ADR 0184): native
KMS is the only way Blit presents, composed on the GPU by default, or on the
CPU.

Since 0.3.74:

- **Programs in windows** (ADR 0228, ADR 0229): gamescope, libei and steamer
  removed (0.3.75); cursor shapes (0.3.76); X programs through Xwayland with
  Blit's X window manager -- milestone A (0.3.78), Steam and a game on the
  owner's machine (0.3.79, milestone B), menus, dialogs and the X11 switch
  (0.3.81, C), X selections (0.3.84, D); focus requests and activation
  (0.3.86); displays as programs see them (0.3.88); text input through
  hibr-ime (0.3.101); drag and drop both ways, Wayland and X, with drag
  icons (0.3.114-0.3.123); `wp_viewporter` (0.3.115) and crops and scales
  shown (0.3.124-0.3.125); Kitty placements at a pixel size (0.3.127).
- **Keyboards** from the system's xkb, listed and switched live, with dead
  keys and compose (ADR 0231, 0.3.91-0.3.94); Arabizi typing in terminals
  and programs (ADR 0233, 0.3.100-0.3.109).
- **The pointer**: shake to find it (ADR 0234, 0.3.102); a busy pointer
  over a program that stops answering (ADR 0235, 0.3.106-0.3.113).
- **Updates** install `blit` and `blit-compositor` together (ADR 0232,
  0.3.97); a release is gated by one runner and recorded in a manifest
  (0.3.125).
- **Displays**: the start guard for a GPU or display start that hangs
  (0.3.124-0.3.125); display mode selection, tried and kept (ADR 0236,
  0.3.125).
- **Glyphs**: player glyphs (ADR 0237, 0.3.126) and keys and big discs
  (0.3.127).

## Next, in order

### 1. Performance and smoothness

Measured, not guessed: the same workloads (dragging windows, wallpaper,
video, the browser, games) on the owner's machine, timed per output.

- High refresh rates, 120/240 Hz (#21).
- The 0.2 architecture tickets: retained GPU composition and movement
  acceptance (#125, #122), fair scheduling of input, PTYs, API and rendering
  (#134), per-frame latency tracing (#126), deterministic replay (#127),
  benchmarks against Kitty (#135).
- Solid window moves through Hibr using rectangle copy (#120): the counters
  shipped in 0.3.124 (`rectangle_copies`); a live solid drag is still to be
  measured.

### 2. Browser image sizing (#111)

Images in the browser pane must land on the cell rectangle they were given,
across font-size changes. Reproduce on the owner's machine first: it may
already be fixed.

### 3. Display robustness

- Odd cell heights (17, 19 px) lose the wallpaper (#114): reproduced live on
  0.3.15 with three Hibr views; Hibr deletes the picture after an OK probe.
  Passed to Hibr.
- Shrinking the font leaves joined Hibr views in their old layout (#147).
  Blit's gate for its side shipped in 0.3.15; the layout is Hibr's to fix.
- A display mode tried live on the owner's display (#60): built and gated
  on the mock in 0.3.125; Hibr's containment fix is in Hibr 0.99.553.
- Monitor hot-plug on real hardware (#100, #20): see "Waiting for hardware".

### 4. Text

- Emoji sequences end to end in the terminal, not only in the raster
  (#110). `text_layout: "emoji"` already shapes them, opt-in, since 0.1.578.
- Arabic typography reviewed on screen (#119).
- A refined selectable font with explicit coverage (#107).

### 5. Programs in windows (ADR 0228, ADR 0229)

- **Phase 2's milestone E:** Steam Big Picture full screen, and a Proton
  game at the display's refresh (#169).
- **Viewports from Hibr's side** (#159): Blit draws crops and scales and
  takes pixel-sized placements (0.3.127); Hibr's sub-window views follow.
- **A dropped file Firefox takes but doesn't open** (#161).
- **Phase 3: full screen** scanned out directly (#174).
- ADR 0229's remaining rows, each its own change: HiDPI output scale (outputs
  say scale 1; no fractional scale, #170), idle inhibit (#171), portals
  (#172), touch and tablets (#173).

### 6. Housekeeping

- One entry point exists (`tools/run-gates.sh`, ADR 0216); CI for it is
  still to come (#27). A GPU in the gate suite (#79): the GLES suites run on
  Mesa's software GL, a real GPU only through the opt-in
  `BLIT_GATE_LIVE_DMABUF`.
- The QEMU fixtures cannot run as documented: Hibr staging, the kitty
  limits expectation, the visuals probe's scanlines, and no in-repo recipe
  for the kernel and initramfs (#166).
- The documentation review of 2026-11-20 filed what the docs promise and
  the code does not do under the `docs-gap` label (#163-#179).

### 7. Release acceptance (#112)

Done: reproducible packages, releases built in the Debian 13 environment
(0.3.80), and an install-and-update rehearsal with real dpkg
(`update_pair_gate`, 0.3.97). Still to do: a security review, and a
rehearsed rollback and recovery.

## Waiting for hardware

These are built and tested; they need a device to confirm.

| What | Needs | Tracking |
| --- | --- | --- |
| Monitor plugged in or out while running | unplugging and replugging on the owner's machine (two displays since 0.3.126) | #100, #20 |
| 120/240 Hz output | a high-refresh monitor | #21 |
| Touchpad tap, tap-and-drag, typing guard | the HP Chromebook | — |
| Backlight brightness | a laptop panel | — |
| Gamepad dead zone and inversion | a gamepad | — |

## Tickets to check and close

These look done or overtaken; each needs its evidence written down before
it is closed: Hibr's login manager (#83, the login manager has shipped since
0.1, with `BLIT_LOGIN_AUTOSTART`), and the freeze after a display layout
change (#156, the ENOTSUP retry shipped in 0.3.110).
