/* A touchpad as a pointer, pinned (src/blit_touchpad.h).
 *
 * A MacBook Air mid 2012 has a bcm5974 trackpad, which reports where the
 * finger is rather than how far it moved, and Blit counted only devices that
 * report movement as pointers -- so the trackpad did nothing at all. This
 * feeds the arithmetic the sequence that trackpad sends and checks every
 * answer: motion for one finger, no jump when a finger is lifted or added, a
 * click from the pad's own button (right with two fingers on it), wheel ticks
 * for two fingers moving, a slow finger still moving the pointer, and, only
 * where tap_to_click is on, a quick touch as a click: left for one finger,
 * right for two, nothing for a long touch, a moving one or a pressed pad.
 *
 * ADR 0193, dragging: while the button is held the pointer follows the
 * finger that moves, from the multitouch slots, so a finger holding the
 * pad down and another moving drags; and with tap-and-drag a tap holds
 * the button until a second touch drags it, or a deadline lets it go.
 *
 * The ranges are the 2012 Air's: x -4620..5140 and y -150..6600 with no
 * resolution reported, so the whole width is BLIT_TOUCHPAD_WIDTH_PX (640)
 * pixels and the height is scaled by its own span the same way.
 */
#include <stdio.h>

#include "../../src/blit_touchpad.h"

static int failures;
static BlitTouchpad pad;
static BlitTouchpadOut out;
static long long now_us;

static int ev(int type, int code, int value)
{
    return blit_touchpad_event(&pad, now_us, type, code, value, &out);
}

static int syn(void)
{
    return ev(EV_SYN, SYN_REPORT, 0);
}

/* A whole touch: down at x,y with the given finger tool, held for ms,
   moved by dx units, and lifted. Answers the tap the lift gave, or -1. */
static int touch(int tool, int ms, int dx, int press)
{
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, tool, 1);
    ev(EV_ABS, ABS_X, 1000);
    ev(EV_ABS, ABS_Y, 1000);
    syn();
    if (press)
        ev(EV_KEY, BTN_LEFT, 1);
    now_us += (long long)ms * 500;
    ev(EV_ABS, ABS_X, 1000 + dx);
    syn();
    if (press)
        ev(EV_KEY, BTN_LEFT, 0);
    now_us += (long long)ms * 500;
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, tool, 0);
    syn();
    now_us += 1000000;
    return out.tap_button;
}

/* Multitouch, type B: a finger in a slot with a tracking id. */
static void mt_at(int slot, int id, int x, int y)
{
    ev(EV_ABS, ABS_MT_SLOT, slot);
    if (id != 0) ev(EV_ABS, ABS_MT_TRACKING_ID, id);
    ev(EV_ABS, ABS_MT_POSITION_X, x);
    ev(EV_ABS, ABS_MT_POSITION_Y, y);
}

static void mt_lift(int slot)
{
    ev(EV_ABS, ABS_MT_SLOT, slot);
    ev(EV_ABS, ABS_MT_TRACKING_ID, -1);
}

/* One finger down, still, for ms, and up: the lift's answer is in out. */
static int quick(int ms)
{
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 1000);
    ev(EV_ABS, ABS_Y, 1000);
    syn();
    now_us += (long long)ms * 1000;
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    return syn();
}

static int tick(void)
{
    return blit_touchpad_tick(&pad, now_us, &out);
}

static void expect(int got, int want, const char *what)
{
    if (got != want) {
        printf("TOUCHPAD_FAIL %s: got %d, wanted %d\n", what, got, want);
        ++failures;
    }
}

int main(void)
{
    int i, total;

    expect(blit_touchpad_caps(1, 1, 1, 1, 0), 1, "a trackpad is a touchpad");
    expect(blit_touchpad_caps(1, 1, 0, 0, 1), 0, "a gamepad's sticks are not");
    expect(blit_touchpad_caps(0, 0, 0, 0, 0), 0, "a mouse is not");
    expect(blit_touchpad_caps(1, 1, 0, 1, 0), 0, "a touchscreen with no finger tool is not");

    blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);

    /* One finger down at the pad's origin: nothing yet, then a move of 976
       units right (a tenth of 9760, so 64 px) and 675 down (a tenth of
       6750, 64 px). */
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 0);
    ev(EV_ABS, ABS_Y, 0);
    expect(syn(), 0, "touching down moves nothing");
    ev(EV_ABS, ABS_X, 976);
    expect(syn(), 1, "a finger moving moves the pointer");
    expect(out.dx, 64, "a tenth of the width is 64 px right");
    expect(out.dy, 0, "and nothing down");
    ev(EV_ABS, ABS_Y, 675);
    syn();
    expect(out.dy, 64, "a tenth of the height is 64 px down");

    /* Lifted, and put down again across the pad: no jump. */
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    expect(syn(), 0, "lifting moves nothing");
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 4000);
    ev(EV_ABS, ABS_Y, 5000);
    expect(syn(), 0, "putting a finger down elsewhere does not jump");
    ev(EV_ABS, ABS_X, 4000 - 976);
    syn();
    expect(out.dx, -64, "and moving from there is relative to there");

    /* The pad pressed down with one finger is a left click. */
    expect(ev(EV_KEY, BTN_LEFT, 1), 1, "pressing the pad is a button");
    expect(out.button_changed, 1, "it says so");
    expect(out.button, 0, "one finger: the left button");
    expect(out.button_pressed, 1, "pressed");
    expect(ev(EV_KEY, BTN_LEFT, 2), 0, "the kernel's autorepeat is not a click");
    ev(EV_KEY, BTN_LEFT, 0);
    expect(out.button, 0, "let go: the same button");
    expect(out.button_pressed, 0, "released");

    /* Two fingers resting on the pad while it is pressed: right click, and
       it is the right button that is let go, whatever the fingers do. */
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    syn();
    ev(EV_KEY, BTN_LEFT, 1);
    expect(out.button, 2, "two fingers: the right button");
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_KEY, BTN_LEFT, 0);
    expect(out.button, 2, "and the right button is the one released");

    /* Two fingers moving scroll: up by 48 px of pad, two ticks of 24, and
       the pointer does not move. Adding the second finger moves nothing. */
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    ev(EV_ABS, ABS_Y, 3000);
    expect(syn(), 0, "a second finger arriving moves nothing");
    ev(EV_ABS, ABS_Y, 3000 - 507);
    expect(syn(), 1, "two fingers moving up scroll");
    expect(out.wheel_y, 2, "48 px of pad is two wheel ticks up");
    expect(out.dx, 0, "and no pointer motion");
    expect(out.dy, 0, "none at all");
    ev(EV_ABS, ABS_Y, 3000);
    syn();
    expect(out.wheel_y, -2, "and back down is two ticks down");

    /* Back to one finger: no jump from where two fingers were. */
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, -4000);
    expect(syn(), 0, "dropping to one finger does not jump");

    /* A slow finger: one unit at a time is 0.066 px each, which must add up
       rather than round to nothing -- sixteen of them are one pixel. */
    total = 0;
    for (i = 1; i <= 16; ++i) {
        ev(EV_ABS, ABS_X, -4000 + i);
        if (syn())
            total += out.dx;
    }
    expect(total, 1, "sixteen units of slow movement are one pixel");

    /* A driver that says how many units make a millimetre: 100 units/mm, so
       100 units are a millimetre, BLIT_TOUCHPAD_PX_PER_MM (8) px. */
    blit_touchpad_init(&pad, 0, 10000, 100, 0, 8000, 100);
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 500);
    ev(EV_ABS, ABS_Y, 500);
    syn();
    ev(EV_ABS, ABS_X, 600);
    syn();
    expect(out.dx, 8, "a millimetre of a resolved pad is 8 px");

    /* Tapping, on the Air's ranges again. Off, nothing; on, a quick still
       touch is a click, and anything else is not. */
    blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);
    expect(touch(BTN_TOOL_FINGER, 100, 0, 0), -1, "with tapping off a tap is nothing");
    pad.tap = 1;
    expect(touch(BTN_TOOL_FINGER, 100, 0, 0), 0, "a quick one-finger tap is a left click");
    expect(touch(BTN_TOOL_DOUBLETAP, 100, 0, 0), 2, "a quick two-finger tap is a right click");
    expect(touch(BTN_TOOL_FINGER, 400, 0, 0), -1, "a long touch is not a tap");
    expect(touch(BTN_TOOL_FINGER, 100, 976, 0), -1, "a touch that moved is not a tap");
    expect(touch(BTN_TOOL_FINGER, 100, 0, 1), -1, "pressing the pad clicks once, not twice");
    expect(touch(BTN_TOOL_FINGER, 100, 30, 0), 0, "a tap that wobbles a little is still one");

    /* A pressed pad follows the finger that moves (ADR 0193). Slot 0
       presses the pad; slot 1 arrives and moves. The kernel's single-touch
       position stays on slot 0, the oldest finger. */
    for (i = 0; i < 2; ++i) {
        int mt = i == 0;
        blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);
        if (mt) blit_touchpad_multitouch(&pad, 5);
        mt_at(0, 10, 1000, 1000);
        ev(EV_KEY, BTN_TOUCH, 1);
        ev(EV_KEY, BTN_TOOL_FINGER, 1);
        ev(EV_ABS, ABS_X, 1000);
        ev(EV_ABS, ABS_Y, 1000);
        syn();
        ev(EV_KEY, BTN_LEFT, 1);
        expect(out.button, 0, "one finger pressing: the left button");
        mt_at(1, 11, 3000, 3000);
        ev(EV_KEY, BTN_TOOL_FINGER, 0);
        ev(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
        expect(syn(), 0, "a second finger arriving on a pressed pad moves nothing");
        mt_at(1, 0, 3976, 3000);
        if (mt) {
            expect(syn(), 1, "the second finger moving moves the pointer");
            expect(out.dx, 64, "by its own movement");
            expect(out.wheel_x + out.wheel_y, 0, "a pressed pad does not scroll");
            expect(out.button_changed, 0, "and no button changes");
            /* The holding finger wobbles a little; the moving one wins. */
            mt_at(0, 0, 1002, 1001);
            mt_at(1, 0, 3976 + 976, 3000);
            syn();
            expect(out.dx, 64, "the finger that moved most is the one followed");
        } else {
            syn();
            expect(out.dx, 0, "without slots the second finger cannot be seen");
        }
        ev(EV_KEY, BTN_LEFT, 0);
        expect(out.button_pressed, 0, "letting go releases");
        expect(out.button, 0, "the left button");
        mt_lift(1);
        mt_lift(0);
        ev(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
        ev(EV_KEY, BTN_TOUCH, 0);
        syn();
    }

    /* Two fingers down before the press: still the right button. */
    blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);
    blit_touchpad_multitouch(&pad, 5);
    mt_at(0, 20, 1000, 1000);
    mt_at(1, 21, 2000, 1000);
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 1);
    syn();
    ev(EV_KEY, BTN_LEFT, 1);
    expect(out.button, 2, "two fingers then a press: the right button");
    ev(EV_KEY, BTN_LEFT, 0);
    mt_lift(0);
    mt_lift(1);
    ev(EV_KEY, BTN_TOOL_DOUBLETAP, 0);
    ev(EV_KEY, BTN_TOUCH, 0);
    syn();

    /* Tap-and-drag. A tap presses at once and holds. */
    blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);
    pad.tap = 1;
    pad.drag = 1;
    now_us = 10000000;
    expect(quick(100), 1, "a tap with tap-and-drag answers at once");
    expect(out.button_changed && out.button_pressed && out.button == 0, 1, "with the left button pressed");
    expect(out.tap_button, -1, "not a whole click");
    now_us += 100000;
    expect(tick(), 0, "nothing lets it go before the deadline");
    /* A touch inside the window drags with the button held. */
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 0);
    ev(EV_ABS, ABS_Y, 0);
    expect(syn(), 0, "touching again moves nothing yet");
    ev(EV_ABS, ABS_X, 976);
    expect(syn(), 1, "the drag moves the pointer");
    expect(out.dx, 64, "by the finger");
    expect(out.button_changed, 0, "with the button still held");
    now_us += 300000;
    expect(tick(), 0, "the deadline does not end a drag");
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    expect(syn(), 1, "lifting ends the drag");
    expect(out.button_changed && !out.button_pressed && out.button == 0, 1, "releasing the left button");
    expect(out.tap_button, -1, "a drag that moved is no second click");

    /* A tap alone: the deadline lets it go. */
    now_us += 1000000;
    quick(100);
    now_us += 179000;
    expect(tick(), 0, "not before 180 ms");
    now_us += 2000;
    expect(tick(), 1, "the deadline lets the tap go");
    expect(out.button_changed && !out.button_pressed, 1, "a release");
    expect(tick(), 0, "once");

    /* Tap, tap: a double click. */
    now_us += 1000000;
    quick(100);
    now_us += 80000;
    expect(quick(90), 1, "the second tap answers");
    expect(out.button_changed && !out.button_pressed, 1, "releasing the first click");
    expect(out.tap_button, 0, "and making the second, whole");
    expect(tick(), 0, "nothing left held");

    /* The loop late past the deadline: the next touch is a new one. */
    now_us += 1000000;
    quick(100);
    now_us += 400000;
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 0);
    ev(EV_ABS, ABS_Y, 0);
    expect(syn(), 1, "a touch after the deadline answers");
    expect(out.button_changed && !out.button_pressed, 1, "with the tap's release, not a drag");
    ev(EV_ABS, ABS_X, 976);
    syn();
    expect(out.dx, 64, "and moves the pointer as an ordinary touch");
    expect(out.button_changed, 0, "with nothing held");
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    syn();

    /* Pressing the pad during a held tap: one press, the pad's release. */
    now_us += 1000000;
    quick(100);
    now_us += 50000;
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    syn();
    expect(ev(EV_KEY, BTN_LEFT, 1), 0, "the pad pressed during a held tap sends no second press");
    expect(ev(EV_KEY, BTN_LEFT, 0), 1, "letting the pad go");
    expect(out.button == 0 && !out.button_pressed, 1, "releases the held left button");
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    expect(syn(), 0, "and lifting adds nothing");
    expect(tick(), 0, "nor does the deadline");

    /* Two fingers tapping stay a whole right click. Tap-and-drag off: a
       tap is the whole click on lift, as before. */
    now_us += 1000000;
    expect(touch(BTN_TOOL_DOUBLETAP, 100, 0, 0), 2, "a two-finger tap is a whole right click");
    pad.drag = 0;
    expect(touch(BTN_TOOL_FINGER, 100, 0, 0), 0, "with tap-and-drag off a tap is a whole click");
    expect(out.button_changed, 0, "with nothing held");

    /* Disable-while-typing: a key typed pauses the pad for 500 ms. */
    blit_touchpad_init(&pad, -4620, 5140, 0, -150, 6600, 0);
    pad.tap = 1;
    now_us = 50000000;
    pad.typing_until_us = now_us + 500000;
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 0);
    ev(EV_ABS, ABS_Y, 0);
    syn();
    ev(EV_ABS, ABS_X, 976);
    expect(syn(), 0, "a finger moving while typing moves nothing");
    now_us += 600000;
    ev(EV_ABS, ABS_X, 1952);
    expect(syn(), 0, "a touch that began while typing stays ignored after it");
    ev(EV_KEY, BTN_LEFT, 1);
    expect(out.button_changed && out.button_pressed, 1, "pressing the pad still clicks");
    ev(EV_ABS, ABS_X, 2928);
    syn();
    expect(out.dx, 64, "and a pressed pad still drags");
    ev(EV_KEY, BTN_LEFT, 0);
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    syn();
    pad.typing_until_us = now_us + 500000;
    expect(touch(BTN_TOOL_FINGER, 100, 0, 0), -1, "a tap while typing is nothing");
    now_us += 1000000;
    expect(touch(BTN_TOOL_FINGER, 100, 0, 0), 0, "and taps again once typing stops");
    ev(EV_KEY, BTN_TOUCH, 1);
    ev(EV_KEY, BTN_TOOL_FINGER, 1);
    ev(EV_ABS, ABS_X, 0);
    syn();
    ev(EV_ABS, ABS_X, 976);
    syn();
    expect(out.dx, 64, "a new touch after typing moves the pointer");
    ev(EV_KEY, BTN_TOUCH, 0);
    ev(EV_KEY, BTN_TOOL_FINGER, 0);
    syn();

    if (failures) {
        printf("TOUCHPAD_GATE_FAIL %d\n", failures);
        return 1;
    }
    puts("TOUCHPAD_GATE_PASS motion=64px_per_tenth no_jump=lift,add,drop click=left,right_two_fingers scroll=2_ticks_per_48px slow=accumulated resolution=8px_per_mm tap=opt_in,left,right_two_fingers,not_long,not_moved,not_pressed pressed_drag=moving_finger,no_scroll,needs_slots tap_and_drag=hold,drag,deadline,double_click,late,pad_press,off typing=paused,touch_stays_ignored,press_clicks,no_tap");
    return 0;
}
