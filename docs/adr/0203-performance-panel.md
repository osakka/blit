# ADR 0203: a performance panel drawn by Blit, with a section Hibr feeds

- Status: proposed
- Note: reviewed by the Hibr maintainers, 2026-11-01; the frame-socket
  channel, `HUD1` and their line set are theirs.
- Date: 2026-11-01
- Scope: compositor (both), settings, diagnostics, the frame socket, the
  Control Panel's Blit pane
- Depends on: [ADR 0002](0002-network-api-and-streaming.md),
  [ADR 0181](0181-gles-compositor-inside-native-kms.md),
  [ADR 0202](0202-local-frames-by-descriptor.md)

## Context

Every performance question this week was answered the same way: someone
sampled `/v1/diagnostics/runtime`, `/v1/terminal-sessions` and Hibr's
`desktop ctl stats` from another machine, and diffed the numbers. Task
Manager's 1 Hz stall, the leaked pictures, YouTube's 468 ms draw and the
Alt-drag were each found that way. It works, but it needs a second person
and a script, and the owner sees the symptom long before anyone measures it.

The owner asked for a "stats for nerds" panel: small, floating, switched on
and off from the Control Panel and the API, showing frame rate, refresh,
dropped frames, the compositor, timings, a moving graph and errors, for Blit
and for Hibr.

## Options considered

1. **A Hibr window** fed from the API. Rejected: on the owner's desktop
   today every window is redrawn on every frame (Hibr #418; the cause is
   under investigation, not the design), so the panel would cost what it
   measures. It also dies with the desktop, and it can't be seen at the
   login screen.
2. **Two panels**, one per project. Rejected: two places to look, and two
   implementations of the same small drawing.
3. **One panel drawn by Blit's compositor** (this decision), with a section
   whose lines Hibr sends.

## Decision (proposed)

Option 3.

- **Drawn by Blit**, after the panes and overlays, in both compositors and
  pixel-identical between them. Text uses the glyph cache and the current
  font; the graph is bars drawn with the existing rectangle blend. No new
  rasterizer, no new dependency.
- **It must not disturb what it measures.** It updates at a fixed rate
  (`hud.hz`, default 2, at most 10) and damages only its own rectangle. A
  frame presented only to update the panel is counted apart
  (`hud_frames`) and left out of the frame rate and missed deadlines it
  shows. Budget: under 0.5 ms of compositor time per update, gated.
- **Every number is the API's.** The panel reads counters the server
  already keeps; it measures nothing of its own. Each line can be
  reproduced from `/v1/diagnostics/runtime` or `/v1/diagnostics`.
- **Blit's lines**, per output on the panel's display:
  - frame rate and refresh (`frame_rate_hz`, the output's `refresh_hz`), and
    the mode;
  - missed deadlines in the window, and late flips (`flip.submit_phase_ms`);
  - the compositor (`gles`/`cpu`) and why, and the driver;
  - render and present time (last and max over the window), the worst loop
    gap, and input dispatch time;
  - pictures: Kitty and Sixel payloads, frames by descriptor accepted and
    refused per second, with the last error;
  - errors: Kitty rejections, frame refusals, dropped stream events, dropped
    keys, watchdog reports;
  - a graph of the last 60 s of frame time against the refresh interval,
    with missed frames marked.
- **Counters to fix first**, because the panel will only show what is
  true: `desktop_cpu_ms` follows the login wrapper, not the desktop process,
  and reads 0. Either it follows the desktop or it goes. Glyph cache hits
  and misses are counted for the first time; nothing else is shown as
  hits/misses.
- **Hibr's section**, sent over the frame socket (ADR 0202). Hibr's desktop
  has no API token (by design since Hibr 0.99.357). The frame socket is
  authenticated and already open, and it is local only, so a remote Hibr
  shows no section, which is fine.
  - **Asked for, not pushed.** A client that says version 2 in its hello
    may be sent `HUD1 on\n` or `HUD1 off\n`: on when the panel is shown
    with the `hibr` section on a display that session feeds, off when it
    stops. A version 1 client never receives one, so today's clients see
    only error replies, as before. Hibr times itself only while it is on;
    its timing runs a little code around every callback.
  - **`STA1`**, client to Blit, about once a second while on: up to 8 lines
    of up to 64 bytes each, `key=value` text. More lines or longer ones are
    refused with `EINVAL`. Blit shows the latest under "hibr", with its
    age, and drops it 5 s after the last one.
  - **Through hold.** On a held desktop, frames go console → hold's server →
    the attaching client → Blit, and `STA1` and `HUD1` travel the same path
    in both directions, so hold forwards them.
  - **Hibr's lines** (theirs to define; at present):
    1. `fps=` frames/s, `wakes=` wakes/s;
    2. `cpu=` desktop self + children %;
    3. `slow=` the longest single callback in the last second and its ms
       (`max_ms`, Hibr ADR 0049);
    4. `woke=` the top two wake sources and their counts;
    5. `drawn=` windows drawn/left this second and the top `drawn_why`;
    6. `frame=` the desktop's own draw time, average and max ms;
    7. `held=` callbacks over 150 ms in the last minute;
    8. `flush=` average ms, and `live=` while a film or game paces frames.
    Frames and bytes per display are Blit's own counts and aren't repeated.
- **Control.** A `hud` object in the settings document: `enabled`,
  `display_id` (or `all`), `corner`, `hz`, `sections` (`blit`, `hibr`,
  `graph`). It is persisted and revisioned like every setting and changed
  with `PUT /v1/settings`, so the Control Panel's Blit pane toggles it with
  the token it already keeps. The startup default is `BLIT_HUD`. The live
  state is reported in `/v1/diagnostics`; the panel is not a client overlay
  and does not take a slot in `/v1/overlays`.

## Consequences

- One place to look, on the screen where the problem is, for both projects.
- The panel adds a little cost while it is on: one damaged rectangle at
  `hud.hz`. Off, it costs nothing.
- Hibr's numbers are only as fresh as Hibr sends them; Blit shows their age
  and never fetches them.
- Hibr's work: in the desktop, send `STA1` through the console while
  `HUD1` is on; in hold, forward both ways.

## Verification

- A renderer-suite gate, both compositors: off, the panel is absent from the
  screenshot; on, it is present, its text matches `/v1/diagnostics/runtime`,
  and over an idle 5 s the frame count rises by no more than `hud.hz` × 5,
  all counted as `hud_frames`. Update time is under budget.
- The frame-socket gate: a version 2 client gets `HUD1 on` when the panel
  with a hibr section is shown and `HUD1 off` when it goes; a version 1
  client gets neither. `STA1` lines appear in the panel; oversize lines and
  more than 8 are refused with `EINVAL`; lines older than 5 s are dropped.
- The documentation gate: blit(8) (`BLIT_HUD`), the API guide, a features row.
