#ifndef BLIT_TOUCHPAD_H
#define BLIT_TOUCHPAD_H

/* A touchpad as a pointer.
 *
 * A mouse reports how far it moved (EV_REL); a touchpad reports where the
 * finger is (EV_ABS), and Blit counted only the first kind as a pointer, so
 * a MacBook Air's trackpad (bcm5974) was found and never used: "no usable
 * evdev mouse". This is the arithmetic that turns the second into the
 * first, kept in a header of its own with no server state, so a gate can
 * feed it a recorded sequence and check every answer (tools/tests/
 * touchpad_gate.c), the way blit_evdev_char.h is checked.
 *
 * It reads the single-touch position every multitouch driver also reports
 * (ABS_X/ABS_Y), how many fingers are down (BTN_TOOL_FINGER, _DOUBLETAP,
 * _TRIPLETAP, _QUADTAP) and whether one is touching (BTN_TOUCH):
 *
 *   one finger moving        pointer motion, in pixels
 *   two fingers moving       wheel ticks, vertical and horizontal
 *   the pad pressed down     a click: left, or right with two fingers on
 *                            the pad -- the clickpad's own button, which is
 *                            what a Mac's trackpad is
 *
 * A change in the number of fingers starts the movement again from where
 * they now are, so lifting or adding one never jumps the pointer.
 *
 * Tapping to click is a per-device choice (tap_to_click), off unless asked
 * for: macOS does not tap by default either, and a palm resting while
 * typing is a click nobody meant. A tap is a touch that ends within
 * BLIT_TOUCHPAD_TAP_MS having moved less than BLIT_TOUCHPAD_TAP_PX, and the
 * pad never pressed down during it: one finger is a left click, two are a
 * right one. Tap-and-drag is not done.
 */

#include <linux/input.h>
#include <string.h>

#ifndef BLIT_TOUCHPAD_PX_PER_MM
#define BLIT_TOUCHPAD_PX_PER_MM 8
#endif
#ifndef BLIT_TOUCHPAD_WIDTH_PX
#define BLIT_TOUCHPAD_WIDTH_PX 640
#endif
#ifndef BLIT_TOUCHPAD_SCROLL_PX
#define BLIT_TOUCHPAD_SCROLL_PX 24
#endif
#ifndef BLIT_TOUCHPAD_TAP_MS
#define BLIT_TOUCHPAD_TAP_MS 180
#endif
#ifndef BLIT_TOUCHPAD_TAP_PX
#define BLIT_TOUCHPAD_TAP_PX 8
#endif

typedef struct BlitTouchpad {
    int active;
    int min_x, max_x, min_y, max_y;
    int res_x, res_y;
    int touching;
    int fingers;
    int fingers_seen;
    int retouch;
    int have;
    int x, y;
    int nx, ny;
    int got_x, got_y;
    long frac_x, frac_y;
    long scroll_x, scroll_y;
    int pressed;
    int pressed_button;
    /* Tapping: whether it is wanted, and the touch it would be. */
    int tap;
    int down;
    long long down_us;
    int down_fingers;
    long down_moved;
    int down_pressed;
} BlitTouchpad;

/* What one event asks of the pointer: motion and wheel on a SYN_REPORT,
   a button on the pad being pressed or let go, or a tap -- a whole click,
   press and release, of tap_button (-1 for none). */
typedef struct BlitTouchpadOut {
    int dx, dy;
    int wheel_x, wheel_y;
    int button_changed;
    int button;
    int button_pressed;
    int tap_button;
} BlitTouchpadOut;

/* Whether a device's capabilities are a touchpad's: absolute X and Y, a
   finger tool and touch, and not a gamepad (which has axes too). */
static int blit_touchpad_caps(int has_abs_x, int has_abs_y, int has_finger,
                              int has_touch, int is_gamepad)
{
    return has_abs_x && has_abs_y && has_finger && has_touch && !is_gamepad;
}

/* Start one, with the ranges and resolutions EVIOCGABS gave. */
static void blit_touchpad_init(BlitTouchpad *t, int min_x, int max_x,
                               int res_x, int min_y, int max_y, int res_y)
{
    memset(t, 0, sizeof(*t));
    t->active = 1;
    t->min_x = min_x;
    t->max_x = max_x > min_x ? max_x : min_x + 1;
    t->min_y = min_y;
    t->max_y = max_y > min_y ? max_y : min_y + 1;
    t->res_x = res_x;
    t->res_y = res_y;
}

/* Pad units to 1/256 pixel: by the pad's millimetres where the driver says
   how many units one is, else so the pad's whole width is a fixed span. */
static long blit_touchpad_scale(const BlitTouchpad *t, int units, int axis_y)
{
    int res = axis_y ? t->res_y : t->res_x;
    long span = axis_y ? (long)(t->max_y - t->min_y) :
        (long)(t->max_x - t->min_x);

    if (res > 0)
        return (long)units * 256 * BLIT_TOUCHPAD_PX_PER_MM / res;
    return (long)units * 256 * BLIT_TOUCHPAD_WIDTH_PX / span;
}

/* Whole pixels out of an accumulator of 1/256 pixels, the rest kept, so a
   slow finger still moves the pointer rather than rounding to nothing. */
static int blit_touchpad_take(long *acc)
{
    long whole = *acc / 256;
    *acc -= whole * 256;
    return (int)whole;
}

/* Pixels of 1/256 to add to a touch's travel, whichever way it went. */
static long blit_touchpad_abs(long v)
{
    return v < 0 ? -v : v;
}

/* One event, at now_us on a monotonic clock (only a tap reads it). Returns
   1 when out holds something for the pointer. */
static int blit_touchpad_event(BlitTouchpad *t, long long now_us, int type,
                               int code, int value, BlitTouchpadOut *out)
{
    int fingers;
    int dx_units, dy_units;
    long px, py;

    memset(out, 0, sizeof(*out));
    out->tap_button = -1;
    if (type == EV_ABS) {
        if (code == ABS_X) { t->nx = value; t->got_x = 1; }
        else if (code == ABS_Y) { t->ny = value; t->got_y = 1; }
        return 0;
    }
    if (type == EV_KEY) {
        switch (code) {
        /* Any change to the fingers, however it ends up, starts the
           movement again at the next report: the count can change and come
           back between two reports, and the position before it is stale. */
        case BTN_TOUCH: t->touching = value != 0; t->retouch = 1; return 0;
        case BTN_TOOL_FINGER: if (value) t->fingers_seen = 1; else if (t->fingers_seen == 1) t->fingers_seen = 0; t->retouch = 1; return 0;
        case BTN_TOOL_DOUBLETAP: if (value) t->fingers_seen = 2; else if (t->fingers_seen == 2) t->fingers_seen = 0; t->retouch = 1; return 0;
        case BTN_TOOL_TRIPLETAP: if (value) t->fingers_seen = 3; else if (t->fingers_seen == 3) t->fingers_seen = 0; t->retouch = 1; return 0;
        case BTN_TOOL_QUADTAP: if (value) t->fingers_seen = 4; else if (t->fingers_seen == 4) t->fingers_seen = 0; t->retouch = 1; return 0;
        case BTN_LEFT:
            if (value == 2)
                return 0;
            if (value) {
                if (t->pressed)
                    return 0;
                t->pressed = 1;
                t->pressed_button = t->fingers_seen >= 2 ? 2 : 0;
                t->down_pressed = 1;
            } else {
                if (!t->pressed)
                    return 0;
                t->pressed = 0;
            }
            out->button_changed = 1;
            out->button = t->pressed_button;
            out->button_pressed = value != 0;
            return 1;
        default:
            return 0;
        }
    }
    if (type != EV_SYN || code != SYN_REPORT)
        return 0;
    fingers = t->fingers_seen;
    if (!t->touching || fingers == 0) {
        int tapped = t->tap && t->down && !t->down_pressed && !t->pressed &&
            now_us - t->down_us <= BLIT_TOUCHPAD_TAP_MS * 1000LL &&
            t->down_moved < 256L * BLIT_TOUCHPAD_TAP_PX;
        if (tapped)
            out->tap_button = t->down_fingers >= 2 ? 2 : 0;
        t->down = 0;
        t->have = 0;
        t->retouch = 0;
        t->got_x = t->got_y = 0;
        t->fingers = 0;
        return tapped;
    }
    if (!t->down) {
        t->down = 1;
        t->down_us = now_us;
        t->down_fingers = 0;
        t->down_moved = 0;
        t->down_pressed = t->pressed;
    }
    if (fingers > t->down_fingers)
        t->down_fingers = fingers;
    if (!t->have || t->retouch || fingers != t->fingers) {
        t->retouch = 0;
        if (t->got_x) t->x = t->nx;
        if (t->got_y) t->y = t->ny;
        t->have = t->got_x || t->got_y || t->have;
        t->fingers = fingers;
        t->frac_x = t->frac_y = 0;
        t->scroll_x = t->scroll_y = 0;
        t->got_x = t->got_y = 0;
        return 0;
    }
    dx_units = t->got_x ? t->nx - t->x : 0;
    dy_units = t->got_y ? t->ny - t->y : 0;
    if (t->got_x) t->x = t->nx;
    if (t->got_y) t->y = t->ny;
    t->got_x = t->got_y = 0;
    if (dx_units == 0 && dy_units == 0)
        return 0;
    px = blit_touchpad_scale(t, dx_units, 0);
    py = blit_touchpad_scale(t, dy_units, 1);
    t->down_moved += blit_touchpad_abs(px) + blit_touchpad_abs(py);
    if (fingers >= 2) {
        /* Two fingers scroll the traditional way: an upward stroke is a
           wheel tick up, as a wheel turned away from the person is. Natural
           Scroll inverts it where every wheel's direction is applied. */
        t->scroll_x += px;
        t->scroll_y += py;
        while (t->scroll_y <= -256L * BLIT_TOUCHPAD_SCROLL_PX) { out->wheel_y += 1; t->scroll_y += 256L * BLIT_TOUCHPAD_SCROLL_PX; }
        while (t->scroll_y >= 256L * BLIT_TOUCHPAD_SCROLL_PX) { out->wheel_y -= 1; t->scroll_y -= 256L * BLIT_TOUCHPAD_SCROLL_PX; }
        while (t->scroll_x <= -256L * BLIT_TOUCHPAD_SCROLL_PX) { out->wheel_x -= 1; t->scroll_x += 256L * BLIT_TOUCHPAD_SCROLL_PX; }
        while (t->scroll_x >= 256L * BLIT_TOUCHPAD_SCROLL_PX) { out->wheel_x += 1; t->scroll_x -= 256L * BLIT_TOUCHPAD_SCROLL_PX; }
        return out->wheel_x != 0 || out->wheel_y != 0;
    }
    t->frac_x += px;
    t->frac_y += py;
    out->dx = blit_touchpad_take(&t->frac_x);
    out->dy = blit_touchpad_take(&t->frac_y);
    return out->dx != 0 || out->dy != 0;
}

#endif
