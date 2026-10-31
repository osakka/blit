# ADR 0193: Touchpad drag and typing: tap-and-drag, a pressed pad follows the moving finger, and the pad pauses while typing

- Status: accepted
- Date: 2026-10-26
- Scope: `blit_touchpad.h`, the main loop's wait, input device settings, the
  control panel's touchpad rows

## Context

On the owner's HP Chromebook, a window can be moved only by pressing the pad
and moving that same finger while it is held down. Two common ways fail:

- **Tap-and-drag:** tap, then touch again and move. It does nothing:
  `blit_touchpad.h` says "Tap-and-drag is not done."
- **Press with one finger, move with another.** It fails for two reasons:
  - Blit follows the pad's single-touch position, `ABS_X/ABS_Y`. The kernel
    makes that position from the oldest contact, which is the finger
    holding the click. So the moving finger is invisible.
  - With two fingers down, Blit scrolls, even while the pad is pressed.

Both are libinput's defaults on a clickpad, and what a Chromebook or a Mac
does.

## Decision

**A pressed pad follows the finger that moves.** This works where the pad
reports multitouch slots (`ABS_MT_SLOT` with `ABS_MT_POSITION_X/Y`):
- While the button is held, by the pad or by a tap-and-drag, each report
  moves the pointer by the slot that moved most.
- The finger holding the click hardly moves, so it is in effect pinned.
- A finger that has just arrived only sets its starting point, so adding
  one never jumps the pointer.
- A held pad never scrolls.

The kernel's pointer emulation copies the slot values into `ABS_X/ABS_Y`,
so the two share units, and the existing scale applies unchanged.

**Without slots, behaviour is unchanged.** The single-touch position is the
wrong finger by construction, so on such a pad drag-with-a-second-finger is
unavailable. The device reports `multitouch: false`.

**Two fingers down before the pad is pressed is still a right click,** as
in libinput's clickfinger and on macOS. Only fingers added after the press
move the pointer.

**Tap-and-drag** is on wherever tap-to-click is on, unless switched off with
a new per-touchpad setting, `tap_and_drag`. Default true, as in libinput.
- **A one-finger tap presses the left button at once.** The release waits
  `BLIT_TOUCHPAD_TAP_MS` (180 ms).
- **A touch inside that window starts a drag.** The button stays held while
  the finger moves, and lifting releases it.
- **If that second touch is itself a tap,** short and still, the result is
  a double click: the held button is released, then a whole click follows.
- **No touch in the window:** a deadline releases the button. The touchpad
  exposes it as `due_us` and `blit_touchpad_tick`. The main loop clamps its
  wait to the nearest deadline, as it does for the settings flush and the
  input resettle, so the release lands on time with no new timer.
- **Pressing the pad during a held tap** turns the hold into the pad's
  press. No second press is sent.
- **Two-finger taps stay a whole right click,** with nothing held.
- **Drag lock**, a drag that survives lifting the finger, is not built.

**Disable-while-typing,** a per-touchpad setting `disable_while_typing`,
default true as in libinput. The owner asked for it.
- **Each key typed pauses every touchpad that has the setting on** for
  `BLIT_TOUCHPAD_TYPING_MS` (500 ms). A held key repeating counts as typing.
  Modifiers don't count, so Ctrl-click and Shift-click still work.
- **While paused, the pad doesn't move the pointer, scroll or tap.** A touch
  that began during the pause stays ignored until it lifts, so a palm
  resting there never takes over once the pause ends.
- **Pressing the pad still clicks, and a held button still drags.** Both are
  deliberate acts.
- **Any keyboard pauses the pad,** not only the built-in one. libinput
  pairs a pad with its internal keyboard only. Blit doesn't yet tell the
  two apart, and an external keyboard beside a laptop pad is the less
  common case.

**Cost:** with tap-and-drag on, a tap's release comes 180 ms later. The
press, which most programs act on for focus and selection, is not delayed.
With it off, taps behave exactly as before.

## Consequences

- Windows move by tap-and-drag, or by pressing with one finger and moving
  another, as on a Chromebook or a Mac.
- The control panel gains "Tap and Drag" and "Disable While Typing" rows
  for touchpads, shown when the device offers them.
- `GET /v1/input/devices` reports a touchpad's `tap_and_drag` and whether it
  has `multitouch` slots.

## Evidence (to be met before acceptance)

`touchpad_gate.c`, fed synthesized type-B multitouch streams:
- **Pad pressed by slot 0, slot 1 arrives and moves:** the pointer moves
  with slot 1. No wheel ticks, and no extra button events. Release lets go
  of the left button.
- **The same stream on a pad without slots:** behaviour is unchanged.
- **Tap, touch again inside the window, move, lift:** press, then motion
  with the button held, then release on lift.
- **Tap alone:** press at once, and the release comes only from the
  deadline, not before.
- **Tap, tap:** a double click.
- **Tap-and-drag off:** a tap is the whole click on lift, as before.
- **Two fingers down, then press:** still the right button.

- **Typing:** a touch while paused moves nothing, and stays ignored after
  the pause. Pressing the pad still clicks and drags. A tap while paused is
  nothing, and taps work again once typing stops.

`tap_setting_gate`: `tap_and_drag` and `disable_while_typing` are offered
on touchpads only, default to true, are written with PATCH, are refused on
a mouse, and are kept in settings only when switched off.

On the owner's screen: moving a window both ways on the HP.
