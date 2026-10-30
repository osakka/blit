# Blit roadmap

Where Blit is, and what comes next, in order. Numbers like #111 are the
internal issue tracker's, not GitHub's. What already works is in
[features](features.md); the plans and restart notes from the 0.1 and 0.2
lines are kept in [history](history/roadmap-0.1-0.2.md).

## Where we are (0.3.15, 2026-10-30)

Blit is the display server under Hibr on the owner's machine, released
continuously through Hibr's apt repository. The 0.3 line started at 0.3.0
once it was stable for daily use. Since then:

- Kitty images change in place and redraw only what changed (0.3.1–0.3.3).
- File-manager icons: 63 kinds, folders in four colourways, everything else
  in its own natural colours (0.3.4–0.3.9).
- Documentation rewritten, manual pages added (0.3.10).
- The GPU compositor is the default (0.3.11).
- Terminal fixes found by writing the terminal reference (0.3.12).
- Frames no longer held or scheduled late (0.3.13–0.3.15).

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
- Odd cell heights (17, 19 px) lose the wallpaper (#114).
- Startup must not block before the API is listening (#109).
- Display mode selection, which needs a resize handshake with the desktop
  (#60).
- A stable display name so per-display settings survive a restart (#103).

### 4. Text

- Emoji sequences end to end in the terminal, not only in the raster (#110).
- Arabic typography reviewed on screen (#119).
- Solid window moves through Hibr using rectangle copy (#120).

### 5. Housekeeping

- Remove the unused display rotation plumbing (#143).
- The DRM test double should not poll every millisecond (#144).
- The gate runner must stop cleanly on Ctrl+C (#106); one entry point and CI
  for the gates (#27); a GPU in the gate suite (#79).
- Intermittent loss of fake input devices in a gate (#124).
- The QEMU visuals probe expects scanlines its staging no longer turns on
  (`tools/probes/ppm_assert.c --visuals`); give it an `effects` document.

### 6. Release acceptance (#112)

Security review, a reproducible package, and a rehearsed install, rollback
and recovery.

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
