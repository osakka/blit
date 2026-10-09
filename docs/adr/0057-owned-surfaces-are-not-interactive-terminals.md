# ADR 0057: A surface with a registered owner is not an interactive terminal

- Status: accepted
- Date: 2026-10-09
- Scope: physical input routing, terminal scrollback, Hibr adapter boundary

## Context

A Blit terminal session has two independent writers to the same surface:

1. the PTY byte stream, parsed by Blit's terminal engine into cells; and
2. the API, when a registered owner authors cells directly
   (`PUT /v1/surfaces/{id}/cells`).

For an ordinary shell session only the first exists and everything is
consistent. For a Hibr desktop both exist, and Blit has been driving both.

[ADR 0034](0034-hibr-adapter-input-only-stream.md) already decided that the
adapter receives input over the WebSocket stream, and it does: every physical
key is broadcast there. But `send_evdev_key` *also* writes the keystroke into
`session->pty_fd`, so the same keystroke arrives twice by two different
routes. The PTY copy has no legitimate consumer on an owned surface and three
destructive effects, all observed on the live host:

- **The tty echoes it.** The line discipline writes the character back, Blit's
  terminal engine renders it into the surface at the cursor, and the owner's
  next repaint erases it. Typing produces characters that appear anywhere on
  the desktop and then vanish.
- **Navigation keys become scrollback commands.** Page Up and Page Down are
  encoded as `CSI 5~` and `CSI 6~`, which Blit's own terminal interprets as
  scrollback navigation. That activates the scrollback view, which replaces
  the whole surface with historical cells — erasing content the owner drew
  and never recorded in terminal state.
- **Leaving scrollback restores the wrong thing.** A later click, *with
  scrollback active*, calls `terminal_scrollback_show_live`, which
  memcpy-restores the terminal's idea
  of the live screen. For an owned surface that is not the desktop; the
  screen stays blank until the owner happens to repaint. The observed symptom
  is the desktop disappearing for one to two seconds after a click, and a
  window that shows, vanishes and returns.

The same collision explains two earlier observations that were filed as
separate puzzles: a `--resume` attachment that wrote `-1` down all 134 rows,
and a session reporting `running` with 127 printable cells while the screen
was blank.

## Options considered

1. **Have the owner tolerate the terminal's writes.** Impossible to do
   correctly: the owner cannot know which cells the terminal will touch, and
   the two writers have different ideas of the cursor.

2. **Stop echoing on the PTY.** Narrower, but it only addresses the first
   effect. Scrollback navigation and the restore-to-wrong-content problem
   remain, and it makes the session's tty settings load-bearing for display
   correctness.

3. **Stop routing physical input through the PTY for an owned surface, and
   stop applying scrollback gestures to it.** Chosen.

## Decision

When a session was launched as a Hibr desktop, its surface is authored by an
owner over the API and the session is not an interactive terminal from the
point of view of **physical** input:

- `send_evdev_key` does not write to the PTY. The key still reaches the owner
  over the input stream, which ADR 0034 already established as the path.
- A pointer report for a session that has not enabled mouse reporting does
  nothing, rather than falling back to scrollback navigation. The owner
  receives pointer events over the same stream.

**The application-requested PTY paths are deliberately kept.** An audit of
every writer to a session PTY found exactly one unconditional physical-input
writer, `send_evdev_key`, which this ADR gates. The others each require the
application to have asked:

| writer | gate |
| --- | --- |
| `terminal_write_reply` | answering a device query the application sent |
| `write_terminal_input` | the explicit `POST .../input` API request |
| `send_focus_report` | `terminal_focus_reporting` (DEC 1004) |
| `send_mouse_report` | `terminal_mouse_mode != 0` (DEC 1000/1002/1003) |
| `send_mouse_motion_report` | `terminal_mouse_mode` 1002 or 1003 |

So if an owner one day enables mouse reporting through DECSET, it gets
terminal mouse reports on its PTY, because it asked for them. The rule is
"nothing physical reaches the PTY unasked", not "the PTY is closed".

Both changes are gated on the session's **desktop role**
(`hibr_desktop_primary` or `hibr_desktop_join`), which is set at session
create from the launch arguments — not on `hibr_owner_registered`, which
arrives asynchronously after the handshake. Gating on the role closes the
startup window, which is precisely the phase that produced a session
reporting `running` with 127 printable cells and a blank screen. A session with no registered owner — an
ordinary shell, every existing terminal gate — keeps exactly today's
behaviour, including evdev-to-PTY keys and scrollback by wheel and page keys.

This does not change the explicit API write path. `POST
/v1/terminal-sessions/{id}/input` remains a deliberate request to put bytes
on the PTY, and a client that asks for that still gets it, owner or not.

## Consequences

An owned surface has one writer again, which is the invariant the surface
model assumed from the start.

Scrollback is unavailable on an owned surface. That is correct rather than a
loss: the terminal's scrollback holds PTY history, and an owned surface's
content never passed through the PTY, so what it would show was never the
desktop.

Physical keys reach a Hibr desktop only through the adapter's stream. If that
stream is down, keyboard input stops rather than silently falling back to a
path that corrupts the display — a visible failure instead of an invisible
one.

This does not fix mouse buttons. Those are lost in Hibr, which records the
mouse mode without notifying the display provider
(`itdlabs/hibr#164`); no amount of routing on this side changes that.

## Evidence

The behaviour this ADR changes is reachable only on a session launched as a
Hibr desktop, and the conformance suite has no Hibr. The evidence is
therefore split deliberately rather than claimed as one gate:

- **Locally, by regression.** `tools/tests/terminal_conformance.rb` keeps its
  scrollback, page-key and mouse vectors. Every one of them runs on a session
  with no desktop role, so all must continue to pass unchanged — that is the
  assertion that this change did not alter ordinary terminal behaviour, and
  it is the half that CI can repeat.
- **Locally, by inspection.** The audit table above is the claim that no
  other unconditional physical-input PTY writer exists. It is reproducible
  with `grep -n "pty_fd" src/blit_server.c | grep -iE "write|send"` and should
  be re-run if a writer is added.
- **On the live host, by measurement.** The keyboard counters added
  alongside this change make the gate observable, so this is no longer an
  eyeball check. A synthetic evdev keyboard created through `/dev/uinput`
  emits six key presses; the same device is then routed to each kind of
  session in turn:

  | counter | owned desktop | ordinary shell |
  | --- | --- | --- |
  | `evdev_keyboard_key_presses` | +6 | +6 |
  | `evdev_keyboard_keys_delivered` | **0** | **6** |
  | `evdev_keyboard_keys_undelivered` | **6** | **0** |
  | surface | unchanged | shows `aaaaaa` |

  Equal press counts on both sides are the control: the keys were read in
  both cases, so this measures the gate rather than a dead input path. The
  device must declare `KEY_A` and `KEY_ENTER` or `evdev_has_keyboard`
  will not open it.

  A click on the owned surface holds its 32,400 printable cells across press
  and release, where it previously cleared them.

Folding a fake owner registration into the suite was considered and rejected:
`POST /v1/terminal-sessions/{id}/hibr-owner` would let a test claim the role
without a desktop, but the behaviour worth asserting is the *absence* of
echo on a surface an owner is actively drawing, which a stub owner does not
reproduce.
