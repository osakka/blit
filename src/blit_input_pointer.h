#ifndef BLIT_INPUT_POINTER_H
#define BLIT_INPUT_POINTER_H

/* How a relative pointer delta becomes pointer travel (issue #67).
 *
 * `sensitivity_percent` was the whole of this, and it is a linear multiplier
 * -- so there was exactly one acceleration curve and it was the identity. The
 * flat profile is that, unchanged and still the default, because changing
 * what an existing property means is not an upgrade.
 *
 * The adaptive profile multiplies it again by a factor that rises with the
 * size of the delta:
 *
 *     magnitude <= FLOOR       factor 1.0
 *     magnitude >= CEILING     factor MAX_FACTOR
 *     between                  linear between 1.0 and MAX_FACTOR
 *
 * The input is the magnitude of one event's delta and not a speed in counts
 * per second. That is a real trade and it is made deliberately: it makes the
 * curve exact and therefore gateable, at the cost of depending on the
 * device's report rate -- a 1000 Hz mouse sends small deltas for the same
 * hand speed that makes a 125 Hz mouse send large ones, so `adaptive` feels
 * different on different hardware. docs/api.md says so where a client will
 * read it.
 *
 * It lives in a header as a pure function for the same reason
 * blit_input_identity.h does: the curve is the part worth asserting exactly,
 * at its boundaries, without a server and without a pointer -- and a
 * fabricated evdev node cannot produce pointer travel at all, because nothing
 * opens it as a pointer.
 */

#define BLIT_POINTER_ACCEL_FLOOR 2
#define BLIT_POINTER_ACCEL_CEILING 20
#define BLIT_POINTER_ACCEL_MAX_FACTOR 3.0f

#define BLIT_INPUT_SENSITIVITY_DEFAULT 100
#define BLIT_INPUT_SENSITIVITY_MIN 10
#define BLIT_INPUT_SENSITIVITY_MAX 1000

static float blit_pointer_accel_factor(int value)
{
    int magnitude = value < 0 ? -value : value;

    if (magnitude <= BLIT_POINTER_ACCEL_FLOOR)
        return 1.0f;
    if (magnitude >= BLIT_POINTER_ACCEL_CEILING)
        return BLIT_POINTER_ACCEL_MAX_FACTOR;
    return 1.0f + (float)(magnitude - BLIT_POINTER_ACCEL_FLOOR) /
        (float)(BLIT_POINTER_ACCEL_CEILING - BLIT_POINTER_ACCEL_FLOOR) *
        (BLIT_POINTER_ACCEL_MAX_FACTOR - 1.0f);
}

/* The pointer position is a float, so the scale does not have to round per
   event and a 50% setting does not throw away every second count of a
   one-count movement. The percentage is an integer because the request parser
   has json_int and no float reader, and a percentage is the resolution a
   control panel offers anyway. */
static float blit_scale_pointer_delta(int value, int sensitivity_percent,
                                      int adaptive)
{
    float scaled = sensitivity_percent == BLIT_INPUT_SENSITIVITY_DEFAULT ?
        (float)value :
        (float)value * (float)sensitivity_percent / 100.0f;

    return adaptive ? scaled * blit_pointer_accel_factor(value) : scaled;
}

#endif
