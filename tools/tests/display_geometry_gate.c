/* Issue #69. Display rotation, asserted where it can be asserted exactly.
 *
 * Rotation is the one display setting that is Blit's own: it is a render
 * transform, not a connector property. That makes it implementable under SDL
 * -- and it makes a sign error silent. At 0 and 180 the scene's destination
 * rectangle and the output's rectangle are the same rectangle, so the common
 * mistake of passing the output's rectangle to a rotated draw produces a
 * correct picture on three quarters of the cases and a cropped one at 90.
 *
 * So this gate does not check that the numbers are self-consistent. It
 * re-implements SDL's draw model -- a destination rectangle, rotated
 * clockwise about its own centre -- and asserts that a texel placed by that
 * model lands exactly where `blit_rotate_point` says it does. Those two are
 * used by different parts of the server: the first presents the scene, the
 * second places the software pointer on top of it. If they ever disagree the
 * pointer drifts away from the cursor, which is precisely the bug a screenshot
 * gate cannot see, because the pointer is drawn after the capture it would
 * have to compare against.
 */
#include <math.h>
#include <stdio.h>

#include "../../src/blit_display_geometry.h"

static int failures;

static void expect_int(int got, int want, const char *what)
{
    if (got != want) {
        printf("DISPLAY_GEOMETRY_FAIL %s: got %d want %d\n", what, got, want);
        ++failures;
    }
}

static void expect_point(float got_x, float got_y, float want_x, float want_y,
                         const char *what)
{
    if (fabsf(got_x - want_x) > 0.01f || fabsf(got_y - want_y) > 0.01f) {
        printf("DISPLAY_GEOMETRY_FAIL %s: got (%.2f, %.2f) want (%.2f, %.2f)\n",
               what, got_x, got_y, want_x, want_y);
        ++failures;
    }
}

/* SDL's model: the texture is laid into `dst` and the whole rectangle is
   rotated clockwise about its own centre. Screen y grows downward, so a
   clockwise rotation by `angle` is (x cos - y sin, x sin + y cos). */
static void sdl_draw(int rotation, int physical_width, int physical_height,
                     float texel_x, float texel_y, float *out_x, float *out_y)
{
    float x;
    float y;
    float width;
    float height;
    double angle = blit_rotation_angle(rotation) * 3.14159265358979323846 / 180.0;
    double cosine = cos(angle);
    double sine = sin(angle);
    float centre_x;
    float centre_y;
    float relative_x;
    float relative_y;

    blit_rotation_present_rect(rotation, physical_width, physical_height,
                               &x, &y, &width, &height);
    centre_x = x + width / 2.0f;
    centre_y = y + height / 2.0f;
    relative_x = x + texel_x - centre_x;
    relative_y = y + texel_y - centre_y;
    *out_x = centre_x + (float)(relative_x * cosine - relative_y * sine);
    *out_y = centre_y + (float)(relative_x * sine + relative_y * cosine);
}

int main(void)
{
    static const int rotations[4] = { 0, 90, 180, 270 };
    int index;

    expect_int(blit_rotation_valid(0), 1, "0 is valid");
    expect_int(blit_rotation_valid(90), 1, "90 is valid");
    expect_int(blit_rotation_valid(180), 1, "180 is valid");
    expect_int(blit_rotation_valid(270), 1, "270 is valid");
    expect_int(blit_rotation_valid(45), 0, "45 is not valid");
    expect_int(blit_rotation_valid(360), 0, "360 is not valid");
    expect_int(blit_rotation_valid(-90), 0, "-90 is not valid");
    expect_int(blit_rotation_valid(1), 0, "1 is not valid");

    /* The host's panel, so the numbers in the acceptance record and the
       numbers here are the same numbers. */
    {
        int width;
        int height;
        blit_rotate_size(0, 1920, 2160, &width, &height);
        expect_int(width, 1920, "1920x2160 at 0: width");
        expect_int(height, 2160, "1920x2160 at 0: height");
        blit_rotate_size(90, 1920, 2160, &width, &height);
        expect_int(width, 2160, "1920x2160 at 90: width");
        expect_int(height, 1920, "1920x2160 at 90: height");
        blit_rotate_size(180, 1920, 2160, &width, &height);
        expect_int(width, 1920, "1920x2160 at 180: width");
        expect_int(height, 2160, "1920x2160 at 180: height");
        blit_rotate_size(270, 1920, 2160, &width, &height);
        expect_int(width, 2160, "1920x2160 at 270: width");
        expect_int(height, 1920, "1920x2160 at 270: height");
    }

    /* The contract sentence: at 90 logical (0, 0) is the physical top-right.
       Each corner is named rather than computed, so a change of convention
       has to be made here deliberately. */
    {
        float x;
        float y;
        blit_rotate_point(0, 1024, 768, 0.0f, 0.0f, &x, &y);
        expect_point(x, y, 0.0f, 0.0f, "at 0, logical origin is top-left");
        blit_rotate_point(90, 1024, 768, 0.0f, 0.0f, &x, &y);
        expect_point(x, y, 1024.0f, 0.0f, "at 90, logical origin is top-right");
        blit_rotate_point(180, 1024, 768, 0.0f, 0.0f, &x, &y);
        expect_point(x, y, 1024.0f, 768.0f,
                     "at 180, logical origin is bottom-right");
        blit_rotate_point(270, 1024, 768, 0.0f, 0.0f, &x, &y);
        expect_point(x, y, 0.0f, 768.0f,
                     "at 270, logical origin is bottom-left");
        /* The remaining three corners at 90, so that a transform which gets
           one corner right by accident still fails. */
        blit_rotate_point(90, 1024, 768, 768.0f, 0.0f, &x, &y);
        expect_point(x, y, 1024.0f, 768.0f, "at 90, logical top-right");
        blit_rotate_point(90, 1024, 768, 768.0f, 1024.0f, &x, &y);
        expect_point(x, y, 0.0f, 768.0f, "at 90, logical bottom-right");
        blit_rotate_point(90, 1024, 768, 0.0f, 1024.0f, &x, &y);
        expect_point(x, y, 0.0f, 0.0f, "at 90, logical bottom-left");
    }

    /* The present rectangle is the scene's own size, centred -- and at 90 on
       a landscape panel that means a negative y, which is correct and looks
       wrong. */
    {
        float x;
        float y;
        float width;
        float height;
        blit_rotation_present_rect(90, 1024, 768, &x, &y, &width, &height);
        expect_point(x, y, 128.0f, -128.0f, "present rect origin at 90");
        expect_point(width, height, 768.0f, 1024.0f, "present rect size at 90");
        blit_rotation_present_rect(0, 1024, 768, &x, &y, &width, &height);
        expect_point(x, y, 0.0f, 0.0f, "present rect origin at 0");
        expect_point(width, height, 1024.0f, 768.0f, "present rect size at 0");
    }

    /* The rotated present rectangle must cover the output exactly. This is
       what fails when the output's rectangle is passed instead of the
       scene's: at 90 the footprint comes back 768x1024 on a 1024x768 panel. */
    for (index = 0; index < 4; ++index) {
        int rotation = rotations[index];
        float logical_width;
        float logical_height;
        float x;
        float y;
        float minimum_x = 0.0f;
        float minimum_y = 0.0f;
        float maximum_x = 0.0f;
        float maximum_y = 0.0f;
        int corner;
        char what[96];

        blit_rotation_present_rect(rotation, 1024, 768, &x, &y,
                                   &logical_width, &logical_height);
        for (corner = 0; corner < 4; ++corner) {
            float texel_x = (corner == 1 || corner == 2) ? logical_width : 0.0f;
            float texel_y = (corner == 2 || corner == 3) ? logical_height : 0.0f;
            float landed_x;
            float landed_y;

            sdl_draw(rotation, 1024, 768, texel_x, texel_y, &landed_x,
                     &landed_y);
            if (corner == 0) {
                minimum_x = maximum_x = landed_x;
                minimum_y = maximum_y = landed_y;
            }
            if (landed_x < minimum_x) minimum_x = landed_x;
            if (landed_x > maximum_x) maximum_x = landed_x;
            if (landed_y < minimum_y) minimum_y = landed_y;
            if (landed_y > maximum_y) maximum_y = landed_y;
        }
        snprintf(what, sizeof(what), "footprint at %d", rotation);
        expect_point(minimum_x, minimum_y, 0.0f, 0.0f, what);
        snprintf(what, sizeof(what), "footprint size at %d", rotation);
        expect_point(maximum_x - minimum_x, maximum_y - minimum_y, 1024.0f,
                     768.0f, what);
    }

    /* The cross-check the gate exists for: the draw model and the point
       transform must agree, for every rotation and for points that are not
       corners. */
    for (index = 0; index < 4; ++index) {
        int rotation = rotations[index];
        int logical_width;
        int logical_height;
        int step;

        blit_rotate_size(rotation, 1024, 768, &logical_width, &logical_height);
        for (step = 0; step <= 4; ++step) {
            float logical_x = (float)logical_width * (float)step / 4.0f;
            float logical_y = (float)logical_height * (float)(4 - step) / 4.0f;
            float drawn_x;
            float drawn_y;
            float mapped_x;
            float mapped_y;
            char what[128];

            sdl_draw(rotation, 1024, 768, logical_x, logical_y, &drawn_x,
                     &drawn_y);
            blit_rotate_point(rotation, 1024, 768, logical_x, logical_y,
                              &mapped_x, &mapped_y);
            snprintf(what, sizeof(what),
                     "draw and point agree at %d on (%.0f, %.0f)", rotation,
                     logical_x, logical_y);
            expect_point(mapped_x, mapped_y, drawn_x, drawn_y, what);
        }
    }

    if (failures > 0) {
        printf("FAIL display-geometry (%d)\n", failures);
        return 1;
    }
    printf("PASS display-geometry rotations=4 corners=named "
           "footprint=exact draw=agrees\n");
    return 0;
}
