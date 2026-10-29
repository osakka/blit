# Blit roadmap

Where Blit is, and what comes next, in order. Numbers like #111 are the
internal issue tracker's, not GitHub's. What already works is in
[features](features.md); the plans and restart notes from the 0.1 and 0.2
lines are kept in [history](history/roadmap-0.1-0.2.md).

## Where we are (0.3.10, 2026-10-29)

Blit is the display server under Hibr on the owner's machine, released
continuously through Hibr's apt repository. The 0.3 line started at 0.3.0
once it was stable for daily use. Since then:

- Kitty images change in place and redraw only what changed (0.3.1–0.3.3).
- File-manager icons: 63 kinds, folders in four colourways, everything else
  in its own natural colours (0.3.4–0.3.9).
- Documentation rewritten, manual pages added (0.3.10).

SDL is gone (ADR 0184): native KMS is the only way Blit presents, composed
on the CPU or, when chosen, the GPU.

## Next, in order

### 1. Terminal fixes found by the documentation sweep

Writing the [terminal reference](terminal.md) from the code turned up
these. Each is small and has a clear test.

- Sixel images over 16 KiB of data are cut off.
- Bright colours (SGR 90–97, 100–107, palette 8–15) come out darker than the
  normal ones.
- SGR `4:1`/`4:2`/`4:4`/`4:5` also set bold, dim or blink;
  `38:2:CS:r:g:b` is misread.
- `CSI 3 J` clears the screen too; it should clear only the scrollback.
- `CSI = c` answers as DA1; DECRQM answers 0 for modes 69, 1048, 2026;
  DECRQSS for SGR leaves out colours.
- Kitty failures that send no reply at all; `a=q` refuses PNG.

### 2. Performance and smoothness

Measured, not guessed: the same workloads (dragging windows, wallpaper,
video, the browser, games) on the owner's machine, timed per output.

- A font-size change that stalls the loop for over 10 s on the real GPU (#146).
- Isolated loop stalls and skipped refreshes (#95).
- Pointer and dragging latency (#101); high refresh rates, 120/240 Hz (#21).
- The 0.2 architecture tickets: retained GPU composition and movement
  acceptance (#125, #122), fair scheduling of input, PTYs, API and rendering
  (#134), per-frame latency tracing (#126), deterministic replay (#127),
  benchmarks against Kitty (#135).

### 3. Browser image sizing (#111)

Images in the browser pane must land on the cell rectangle they were given,
across font-size changes. Reproduce on the owner's machine first: it may
already be fixed.

### 4. Display robustness

- A black band across the primary after Hibr's Restart Desktop (#142).
- Odd cell heights (17, 19 px) lose the wallpaper (#114).
- Startup must not block before the API is listening (#109).
- Display mode selection, which needs a resize handshake with the desktop
  (#60).
- A stable display name so per-display settings survive a restart (#103).

### 5. Text

- Emoji sequences end to end in the terminal, not only in the raster (#110).
- Arabic typography reviewed on screen (#119).
- Solid window moves through Hibr using rectangle copy (#120).

### 6. Housekeeping

- Remove the unused display rotation plumbing (#143).
- The DRM test double should not poll every millisecond (#144).
- The gate runner must stop cleanly on Ctrl+C (#106); one entry point and CI
  for the gates (#27); a GPU in the gate suite (#79).
- Intermittent loss of fake input devices in a gate (#124).
- The QEMU visuals probe expects scanlines its staging no longer turns on
  (`tools/probes/ppm_assert.c --visuals`); give it an `effects` document.

### 7. Release acceptance (#112)

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
