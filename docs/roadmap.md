# Blit roadmap

Where Blit is, and what comes next, in order. Numbers like #111 are the
internal issue tracker's, not GitHub's. What already works is in
[features](features.md); the plans and restart notes from the 0.1 and 0.2
lines are kept in [history](history/roadmap-0.1-0.2.md).

## Where we are (0.3.74, 2026-11-10)

Blit is the display server under Hibr on the owner's machine, released
continuously through Hibr's apt repository. Since 0.3.20:

- Pictures by dmabuf (0.3.60, ADR 0223), bounded by memory rather than
  count (0.3.65, ADR 0226); Spleen as one font of five masters (ADR 0227).
- Programs in Hibr windows through Blit's own Wayland compositor (ADR 0228
  phase 1, 0.3.71-0.3.74): windows, popups and subsurfaces as pictures,
  dmabuf frames, Blit's keyboard and pointer with pointer lock, programs
  surviving a desktop restart, client-side frames moved and resized by
  their own title bars, and the clipboard and Vault shared with Hibr --
  the interop drawn as one owner per concern in ADR 0229. vkcube and
  Firefox are the live checks.
- The frame channel's version 5 grammar, a flag day with Hibr 0.99.482.

SDL is gone (ADR 0184): native KMS is the only way Blit presents, composed
on the GPU by default (0.3.11), or on the CPU.

## Next, in order

### 1. Performance and smoothness

Measured, not guessed: the same workloads (dragging windows, wallpaper,
video, the browser, games) on the owner's machine, timed per output.

- Done: the Hibr frame barrier no longer holds every frame, and animation
  frames start just after a vblank (0.3.13–0.3.15): late flips 14.7% → 6.0%
  on the game's monitor, 10.5% → 0% on the other. The font-size stall (#146)
  was the settings fsync, off the loop since 0.2.30; the owner confirms it
  no longer happens (close the ticket).
- Done: video through Hibr. A YouTube window cost Hibr's desktop 69%, its
  hold 23% and Blit 21% of a core as Kitty images through the terminal; by
  descriptor (ADR 0202, Hibr 0.99.398) hold is 3% and Blit 10%. What remains
  is Hibr's desktop producing the frames (about 35%), Hibr's to reduce.
- Done: dragging windows, measured with the owner: 58 fps on one monitor,
  41–50 fps across both, at most 3% of flips late; the owner finds it smooth.
  The cost while dragging is Hibr's, not Blit's.
- Done: the 106 ms loop stall of #95 (measured on the 0.1.535 loop) does not
  recur on 0.3.15: in 4 h on the owner's machine the worst loop gap was
  30 ms and the worst frame gap 42 ms, and in a 60 s sample no 500 ms window
  had a gap over 11.8 ms (close the ticket).
- High refresh rates, 120/240 Hz (#21). (Pointer latency, #101, was fixed in
  0.1.565 and accepted by the owner; dragging is measured under #125/#135.)
- The 0.2 architecture tickets: retained GPU composition and movement
  acceptance (#125, #122), fair scheduling of input, PTYs, API and rendering
  (#134), per-frame latency tracing (#126), deterministic replay (#127),
  benchmarks against Kitty (#135).

### 2. Browser image sizing (#111)

Images in the browser pane must land on the cell rectangle they were given,
across font-size changes. Reproduce on the owner's machine first: it may
already be fixed.

### 3. Display robustness

- A black band across the primary after Hibr's Restart Desktop (#142).
- Odd cell heights (17, 19 px) lose the wallpaper (#114): reproduced live on
  0.3.15 with three Hibr views; Hibr deletes the picture after an OK probe.
  Passed to Hibr.
- Shrinking the font leaves joined Hibr views in their old layout: a second
  menu bar, another display's window (#147). Blit's grids and PTYs are
  right; Hibr's to fix. Blit adds a gate for its side and re-runs the sweep.
- Startup must not block before the API is listening (#109): since 0.3.16
  a GPU start that hangs is killed after 10 s and the next start runs on the
  CPU with the API up; close the ticket once the owner agrees.
- Display mode selection, which needs a resize handshake with the desktop
  (#60).
- A stable display name so per-display settings survive a restart (#103).

### 4. Text

- Emoji sequences end to end in the terminal, not only in the raster (#110).
- Arabic typography reviewed on screen (#119).
- Solid window moves through Hibr using rectangle copy (#120).
- Super as a modifier through Hibr: Blit already reports it under the kitty
  keyboard protocol (ADR 0169); Hibr must ask for it (Hibr #415, #326).

### 5. Housekeeping

- The gate runner must stop cleanly on Ctrl+C (#106); one entry point and CI
  for the gates (#27); a GPU in the gate suite (#79).
- Intermittent loss of fake input devices in a gate (#124).
- The QEMU visuals probe expects scanlines its staging no longer turns on
  (`tools/probes/ppm_assert.c --visuals`); give it an `effects` document.

### 6. Release acceptance (#112)

Security review, a reproducible package, and a rehearsed install, rollback
and recovery.

### 7. Programs in windows (ADR 0228, ADR 0229)

- **Now: gamescope out (0.3.75).** The owner chose to remove
  `blit-gamescope` and everything that served it -- the bridge, the libei
  input path (`INPUT eis`), their gates and package, ADR 0206's hand-over --
  in one change with Hibr's and steamer's sides. Until phase 2, Steam and
  other X11 programs do not run in Blit windows.
- **Phase 0:** the latency baseline, deferred by the owner; measured before
  phase 2 is judged.
- **Phase 2: Xwayland.** Rootless Xwayland per session with the compositor
  as its X window manager; ADR 0229's rows already name the X equivalents
  (decorations, move and resize, selections), so it adds no wire. Steam Big
  Picture, then a Proton game, in a window.
- **Phase 3: full screen** scanned out directly; **phase 4** is now only
  the retirement of ADR 0206, done with 0.3.75.
- ADR 0229's later rows, each its own change: cursor shapes, drag and
  drop, activation, idle inhibit, text input, HiDPI, portals, touch.

## Waiting for hardware

These are built and tested; they need a device to confirm.

| What | Needs | Tracking |
| --- | --- | --- |
| Monitor plugged in or out while running | a third monitor | #100, #20 |
| 120/240 Hz output | a high-refresh monitor | #21 |
| Touchpad tap, tap-and-drag, typing guard | the HP Chromebook | — |
| Backlight brightness | a laptop panel | — |
| Gamepad dead zone and inversion | a gamepad | — |

## Tickets to check and close

These look done or overtaken; each needs its evidence written down before
it is closed: font completion (#107, #118), the sixel object leak (#85),
Hibr's login manager (#83), and the image's application list (#82).
