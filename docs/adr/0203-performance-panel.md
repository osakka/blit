# ADR 0203: a performance panel drawn by Blit, with a section Hibr feeds

- Status: accepted
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

## Decision

Option 3.

- **Drawn by Blit**, after the panes and overlays, in both compositors and
  pixel-identical between them. The server makes it as one opaque picture
  that both compositors copy 1:1. Text is the boot PSF, loaded once for
  the panel, so it looks the same whatever font the terminals use; the
  graph is bars. No new rasterizer, no new dependency.
- **It must not disturb what it measures.** It updates at a fixed rate
  (`hud.hz`, default 2, at most 10) and damages only its own rectangle. A
  frame presented only to update the panel is counted apart
  (`hud_frames`) and left out of the frame rate and missed deadlines it
  shows. Only a line or graph that changed is redrawn, and the GPU uploads
  only those rows. Budget: under 0.5 ms of compositor time per update,
  gated.
- **Every number is the API's.** The panel reads counters the server
  already keeps; it measures nothing of its own. Each line can be
  reproduced from `/v1/diagnostics/runtime` or `/v1/diagnostics`.
- **Blit's lines**, per output, about that output only (revised after the
  first week live, when a panel of Blit-wide numbers on every display read
  as nonsense: "fps 60" while a 9 fps video played):
  - the output, its mode and refresh;
  - its draws in the last second, less the panel's own, with late flips and
    missed deadlines in that second;
  - why it drew: an animated effect, a picture, the pointer alone, changed
    text, or anything else, each a rate, zeros left out;
  - its draw and flip time, with the worst in the last 10 s;
  - its gpu (the renderer, shortened) or, on the CPU compositor, why;
  - the pictures its sessions sent: frames by descriptor, Kitty and Sixel
    payloads, a second, and refusals when there are any;
  - a graph of the last 60 s: each second's worst frame time against the
    refresh interval, newest at the right, over-interval bars marked.
  Blit as a whole is said once, on the first panel: the worst loop gap and
  input dispatch in 10 s (or "idle"), the glyph cache's hit rate, and the
  errors (Kitty rejections, dropped stream events, dropped keys) in 10 s.
  Every one of these is in `/v1/diagnostics`, kept whether or not the panel
  is on.
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
    may be sent `HUD1 on\n` or `HUD1 off\n` (a single line whose first
    word is `HUD1`, read beside `OK` and `ERROR`): on when the panel is shown
    with the `hibr` section on a display that session feeds, off when it
    stops. A version 1 client never receives one, so today's clients see
    only error replies, as before. Hibr times itself only while it is on;
    its timing runs a little code around every callback.
  - **`STA1`**, client to Blit, about once a second while on: up to 8 lines
    of up to 64 bytes each, `key=value` text, after a little-endian line
    count. Each line ends in `\n`; the count is only a check, and a
    message whose lines disagree with it is refused with `EINVAL`, as are
    more lines or longer ones. Blit shows the latest under "hibr", with its
    age, and drops it 5 s after the last one. The bytes are in ADR 0202's
    "Wire format, version 2".
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
  `graph`), and `opacity` (10–100 %, default 100: only the background and
  the graph's fill go translucent, so the text stays readable; both
  compositors blend with the same integer rule, and the CPU one keeps what
  is under the panel, as it does under the pointer, so blends never stack).
  It is persisted and revisioned like every setting and changed
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
