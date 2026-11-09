# ADR 0198: Pointer lock: a focused program can hold the pointer and read raw motion

- Status: accepted; in use since 0.2.42
- Date: 2026-10-27
- Scope: `blit_server.c` (terminal modes, pointer motion, mouse reports,
  focus), the API diagnostics, a new gate
- Tickets: blit #139, hibr #379, hibr-boxer #1

## Context

Boxer, Hibr's DOS game player, runs games that turn with the mouse, such
as Doom. They need endless relative motion, but Blit's pointer stops at the
edge of the surface. No common terminal can hold the pointer for a program.
Blit draws its own pointer, so it can.

## Decision

The protocol was agreed with Hibr and Boxer. The owner chose the release
behaviour.

- **Mode ?7701,** a Blit-private DEC mode, well clear of the ranges xterm,
  kitty, contour and ghostty use.
  - A program asks first with DECRQM, so nothing is inferred from `TERM`.
  - `CSI ? 7701 h` engages only while the session has Blit's focus.
    Otherwise it is refused, with `CSI ? 7701 ; 2 $ y`, so the program
    always learns the outcome.
- **While locked:**
  - The visible pointer does not move and is hidden. Every motion source
    (evdev mouse, touchpad, API injection) still adds into the pointer, and
    that motion is undone at the one place the pointer is moved,
    `update_pointer_position`. (Since 0.3.70 a frame channel's input route
    in hold `confine` keeps the pointer shown and moving inside its picture
    instead; ADR 0225, "Hold modes".)
  - The program is sent **raw counts**, before sensitivity and
    acceleration, as Boxer asked: aim must not depend on hand speed. The
    form is SGR-Pixels with signed deltas, `CSI < b ; dx ; dy M`, summed
    and sent at most once a frame, as pixel motion already is.
  - Buttons and the wheel go to the holder in the same form, with `0;0`.
    Absolute motion is ignored, and no other session hears anything.
- **Release:**
  - The program gives the lock back with `CSI ? 7701 l`, and needs no
    reply.
  - Blit takes it back when focus moves to another session, the program
    exits or the session closes, or on RIS or a soft reset. Blit then sends
    an unsolicited `CSI ? 7701 ; 2 $ y`, and retries it if the program's
    input was full.
  - The pointer reappears where it was locked.
- **A desktop's views are one program** (revised 2026-11-01, 0.3.28). A
  Hibr desktop is several Blit sessions, one per view. On the owner's mini
  PC, Boxer's lock was held by the HDMI view while the desktop's Release
  Pointer gave `?7701l` back on another view. The lock stayed on, because
  "focus moves away" can't happen while the pointer is still, and the
  stall check saw a program reading normally. So nothing ended it, and the
  raw reports reached a window as typing, again after a reboot. Now a
  `?7701l` from any view of the same desktop (the frame barrier's
  membership: the login desktop, or primaries and joins of one
  `--session`) releases the lock wherever that desktop holds it, with the
  usual unasked notice to the holder. Every engage and release is a
  journal line and a trace event with its session, display and reason, and
  `performance.pointer_lock_released` counts releases by reason. Turning
  unrouted reports into keystrokes is the desktop's to stop, and Hibr is
  fixing it there.
- **No release key in Blit.** The owner chose one release key, the
  desktop's Release Pointer shortcut, which Hibr turns into
  `CSI ? 7701 l`. A fixed Blit chord would be a second key, and would
  disagree the day the desktop's is rebound.
- **The safety net is a stuck program.** A few times a second Blit asks the
  terminal how much input waits unread (FIONREAD on the PTY's slave). If
  the holder leaves input unread for two seconds, it is stuck, and Blit
  takes the lock back. A program reading normally empties its input within
  milliseconds. A failed PTY write would only show this after tens of
  kilobytes, about a minute of motion, so it is not used.

## Consequences

- **Hibr** asks for the mode, carries it through hold without offsetting
  deltas, and gives the lock back on its shortcut, on focus loss and on
  menus.
- **Boxer** passes the raw deltas to DOSBox Pure, which scales them with
  its own sensitivity.
- **Injected relative motion** (`pointer_relative`) now moves the pointer
  even on a machine with no mouse, as absolute injection always has.
  Before, it was dropped until an evdev mouse existed.
- A program that blocks for over two seconds while locked loses the lock,
  and is told.

- **Refusals are counted (0.2.46).** On the owner's machine the lock never
  engaged, and nothing showed whether Hibr had asked. It had asked on a
  display it had only moved across, not the one Blit had focused. Each
  session's `mouse_reporting.pointer_lock_refusals` now counts the requests
  refused.

## Evidence (0.2.42)

`pointer_lock_gate` (new) uses raw-mode sessions that copy every byte they
are sent to a file. It checks:
- DECRQM answers 2, then 1 once engaged;
- an unfocused request is refused, and the program is told;
- while locked, the pointer is hidden and does not move;
- 20 motions of (7, −3) arrive summed exactly, as (140, −60), in fewer than
  20 reports;
- a click arrives in the lock form, and absolute motion is ignored;
- focus moving away releases the lock, with the notice, and the pointer
  returns where it was locked;
- the bystander session hears nothing;
- a program's own release gets no notice;
- a program that stops reading loses the lock within seconds.

## Verification (0.3.28)

- `pointer_lock_desktop_gate.rb`: a primary and a `--join` of one desktop.
  The join takes the lock. A session outside the desktop giving it back
  changes nothing. The primary's give-back ends it, with the unasked
  notice to the holder, the journal lines and the `desktop_view` count,
  and no signed motion follows. It fails without the fix.
