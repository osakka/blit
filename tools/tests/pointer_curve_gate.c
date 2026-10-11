/* Issue #67. The acceleration curve, asserted exactly and at its boundaries.
 *
 * This is a C gate rather than an HTTP one for a reason the repo has met
 * before: a fabricated evdev node is a regular file, nothing opens it as a
 * pointer, and `update_evdev_pointer` deliberately publishes nothing when no
 * pointer is open -- so no amount of injection produces pointer travel in a
 * gate environment. Issue #52's gate says the same thing about
 * sensitivity_percent and sends that measurement to the live host.
 *
 * The curve is the part worth pinning exactly, so it is a pure function and
 * this asserts it: the floor, the ceiling, the midpoint, the sign, and that
 * `flat` is byte-for-byte what sensitivity_percent always did.
 */
#include <stdio.h>
#include <string.h>

#include "../../src/blit_input_pointer.h"

static int failures;

static void expect(float got, float want, const char *what)
{
    float difference = got - want;

    if (difference < 0.0f)
        difference = -difference;
    if (difference > 0.0005f) {
        printf("POINTER_CURVE_FAIL %s: got %.4f want %.4f\n", what, got, want);
        ++failures;
    }
}

int main(void)
{
    /* Flat is the identity on the delta, scaled only by the percentage. It
       must stay exactly that: issue #67 adds a profile, it does not change
       what sensitivity_percent means. */
    expect(blit_scale_pointer_delta(20, 100, 0), 20.0f, "flat at 100%");
    expect(blit_scale_pointer_delta(20, 200, 0), 40.0f, "flat at 200%");
    expect(blit_scale_pointer_delta(20, 50, 0), 10.0f, "flat at 50%");
    expect(blit_scale_pointer_delta(-20, 200, 0), -40.0f, "flat keeps sign");
    expect(blit_scale_pointer_delta(1, 100, 0), 1.0f, "flat on one count");

    /* Adaptive: untouched at and below the floor, tripled at and above the
       ceiling, linear between. */
    expect(blit_pointer_accel_factor(0), 1.0f, "factor at zero");
    expect(blit_pointer_accel_factor(1), 1.0f, "factor below the floor");
    expect(blit_pointer_accel_factor(2), 1.0f, "factor at the floor");
    expect(blit_pointer_accel_factor(3), 1.0f + 2.0f / 18.0f,
           "factor one past the floor");
    expect(blit_pointer_accel_factor(11), 2.0f, "factor at the midpoint");
    expect(blit_pointer_accel_factor(19), 1.0f + 17.0f * 2.0f / 18.0f,
           "factor one below the ceiling");
    expect(blit_pointer_accel_factor(20), 3.0f, "factor at the ceiling");
    expect(blit_pointer_accel_factor(200), 3.0f, "factor far past it");
    expect(blit_pointer_accel_factor(-20), 3.0f, "factor on magnitude only");

    expect(blit_scale_pointer_delta(2, 100, 1), 2.0f, "adaptive at the floor");
    expect(blit_scale_pointer_delta(11, 100, 1), 22.0f,
           "adaptive at the midpoint");
    expect(blit_scale_pointer_delta(20, 100, 1), 60.0f,
           "adaptive at the ceiling");
    expect(blit_scale_pointer_delta(-20, 100, 1), -60.0f,
           "adaptive keeps sign");
    /* The two multipliers compose, and in that order: the percentage is what
       a person set and the factor is what the curve made of the movement. */
    expect(blit_scale_pointer_delta(20, 200, 1), 120.0f,
           "adaptive composes with the percentage");
    expect(blit_scale_pointer_delta(20, 50, 1), 30.0f,
           "and with a percentage below 100");

    /* The curve is monotonic, which a controller showing a slider relies on:
       a larger movement must never travel less. */
    {
        int delta;
        float previous = -1.0f;
        for (delta = 0; delta <= 40; ++delta) {
            float travel = blit_scale_pointer_delta(delta, 100, 1);
            if (travel < previous) {
                printf("POINTER_CURVE_FAIL the curve is not monotonic at "
                       "%d: %.4f after %.4f\n", delta, travel, previous);
                ++failures;
            }
            previous = travel;
        }
    }
    /* And never below flat: adaptive accelerates, it does not brake. */
    {
        int delta;
        for (delta = 0; delta <= 40; ++delta)
            if (blit_scale_pointer_delta(delta, 100, 1) <
                blit_scale_pointer_delta(delta, 100, 0) - 0.0005f) {
                printf("POINTER_CURVE_FAIL adaptive is slower than flat at "
                       "%d\n", delta);
                ++failures;
            }
    }

    if (failures > 0) {
        printf("POINTER_CURVE_FAIL %d assertion(s)\n", failures);
        return 1;
    }
    printf("POINTER_CURVE_PASS floor=%d ceiling=%d max_factor=%.1f\n",
           BLIT_POINTER_ACCEL_FLOOR, BLIT_POINTER_ACCEL_CEILING,
           (double)BLIT_POINTER_ACCEL_MAX_FACTOR);
    return 0;
}
