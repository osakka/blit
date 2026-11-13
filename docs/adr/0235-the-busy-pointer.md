# ADR 0235: The busy pointer

- Status: proposed (0.3.106; accepted once the owner has seen a hung
  program spin on the mini PC)
- Date: 2026-11-13
- Scope: blit-compositor (pings, `WINDOW busy`, `INPUT busy`), the pointer
  art (`src/blit_pointer_art.h`), `src/blit_server.c` (the spin),
  `document.pointer`, the Blit Mouse pane; agreed with Hibr

## Context

The owner asked: "When a program is busy, and we have it in the
foreground, does it show the busy (mac circley, going round) mouse
pointer?" It didn't. blit-compositor answered nothing on xdg_wm_base.pong
and never pinged. Blit's `wait` shape, which programs ask for by OSC 22
and through `wp_cursor_shape_v1`, was a still hourglass. Every feature
here is tuned in a Control Panel, never dictated.

## Options considered

1. **The desktop decides from frame timing.** It can't tell a still
   program from a hung one: an idle window draws nothing.
2. **Pings, in the compositor.** xdg_wm_base.ping exists for this. A
   client that doesn't pong within a threshold isn't dispatching events,
   which is what "not responding" means.
3. **Who changes the shape.** Either the desktop, on a new busy line, or
   the compositor, through the existing `WINDOW cursor` path that Hibr
   already relays as the window's OSC 22 shape. The second needs no Hibr
   change and keeps one source of cursors.

## Decision

Options 2 and 3, agreed with Hibr (2026-11-13).

- **Pings:** when input (a key, a button, motion, the wheel) is given to a
  window, blit-compositor pings that window's program. At most one ping is
  outstanding per xdg_wm_base, and none goes sooner than 500 ms after the
  last.
- **Busy:** a ping unanswered for Blit's threshold makes the program busy.
  Each of its top-levels is said `WINDOW busy <id> on`, then
  `WINDOW cursor <id> wait`, unless the program hid its pointer (`none`).
  The pong ends it: `WINDOW busy <id> off` and the program's own shape
  back (`default` when it named none).
- **Exceptions:** a window holding a pointer lock or confinement (a game)
  is never shown busy. X programs answer no xdg ping and are out of scope
  (_NET_WM_PING later).
- **The threshold is Blit's:** `document.pointer`'s `busy_pointer`
  (default true) and `busy_after_ms` (500–10000, default 2000). Blit sends
  it to the compositor on each route as `INPUT busy <id> <ms|off>`, when
  the route goes live and on each change. Before that line, the
  compositor pings nothing.
- **The art:** shape 4 (`wait`, `progress`, `watch`, `clock`) is a ring
  of 12 spokes. It is drawn once at a neutral phase. Each phase dims the
  spokes by how far they trail the lit one, in a pass over the pixels,
  into a second buffer handed to the display. The shown art, the cache key
  for builds and the shake's master, never changes per frame. One turn
  takes `busy_spin_ms` (400–3000, default 1000), in 12 steps. The loop is
  woken for the next step only while the busy pointer is shown.
- **Hibr's part:** "(not responding)" after the title while busy, and
  Force Quit offered, killing nothing without the person confirming.
  Hibr ignores the line until it reads it.

## Consequences

- One source of cursors: Blit's art, and the desktop relays the name as
  before.
- A busy program's windows show the spinner wherever the pointer is over
  them. Programs asking `wait` by OSC 22 spin too.
- Pings cost a message per input burst, at most two a second per program.
- The mini PC's running compositor learns `INPUT busy` only after a
  Restart Desktop. An older compositor logs the line and ignores it.

## Evidence

- `compositor_gate` busy part (tcc, and ASan/UBSan), using the stand-in
  `--hang`: it stops reading anything, pings included, until SIGUSR1.
  - Answering, with motion for 2.5 s: no busy line.
  - Hung: `WINDOW busy 1 on`, then `WINDOW cursor 1 wait`.
  - Released: `WINDOW busy 1 off` and `WINDOW cursor 1 default`.
  - With `INPUT busy 1 off`, hung: no busy line.
  - Killed while busy, its ping outstanding (Force Quit's path): its
    window goes and the compositor lives on, freeing cleanly under the
    sanitizers.
- `pointer_art_gate`: the busy pointer's 12 phases all differ with the
  geometry kept, and no two arts (built, spun, resampled) share a
  revision. The display and the GLES texture cache take an equal revision
  for the same picture, so a shared one could leave a spinner frame drawn
  after a program recovers. Every piece of pointer art now takes its
  revision from one sequence. The art needs no libm: the spokes'
  directions are constants.
- `compositor_wire_unit`: `INPUT busy` parse, including 0, a value over
  60000, and `off` with trailing text, all refused.
- `frame_input_gate` sock: the route gets `INPUT busy 7 2000` when it
  goes live, then 3500, `off` and 2000 again through live
  `PUT /v1/settings`.
- `pointer_shape_gate` (native and gles), with the pointer still and
  nothing else changing:
  - the pixels under the busy pointer differ a third of a turn apart, and
    `spin_frames` counts up;
  - after it leaves, no more frames and `spin_phase` -1.
- `pointer_appearance_gate`: the defaults, and out-of-range values
  refused.
- Not gated: a locked or confined window kept from showing busy (code
  only), and Hibr's title and Force Quit (Hibr's).
- Not yet: the owner's mini PC.
