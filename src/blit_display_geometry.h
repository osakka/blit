#ifndef BLIT_DISPLAY_GEOMETRY_H
#define BLIT_DISPLAY_GEOMETRY_H

/* Issue #69. Display rotation, stated once so that the renderer, the API and
 * a gate all agree on what it means.
 *
 * `rotation` is the angle, clockwise, by which Blit rotates the rendered
 * image before presenting it on the panel. At 90 the content Blit drew at
 * logical (0, 0) appears at the physical top-right corner. That sentence is
 * the contract -- it is in the OpenAPI description and
 * `display_geometry_gate.c` asserts exactly it -- because every other way of
 * saying it borrows someone else's enum: DRM's ROTATE_90 is anticlockwise,
 * RandR calls the same transform "right", and SDL's render angle is
 * clockwise. Naming the corner a pixel lands in cannot be read two ways.
 *
 * Two sizes follow, and the repo keeps both:
 *
 *   physical  the mode the connector drives, which never rotates
 *   logical   the rectangle Blit composes into, which is the physical one
 *             with width and height swapped at 90 and 270
 *
 * A display's `bounds` is the logical rectangle, because that is the space
 * the cell grid, the hit test and the topology placements already work in --
 * and because it is what a rotated monitor occupies on a desk. Its `modes`
 * stay physical. That split is what `xrandr` reports too.
 */

static int blit_rotation_valid(int rotation)
{
    return rotation == 0 || rotation == 90 || rotation == 180 ||
           rotation == 270;
}

/* 90 and 270 exchange the two axes; 0 and 180 do not. */
static int blit_rotation_swaps_axes(int rotation)
{
    return rotation == 90 || rotation == 270;
}

static void blit_rotate_size(int rotation, int width, int height,
                             int *out_width, int *out_height)
{
    if (blit_rotation_swaps_axes(rotation)) {
        *out_width = height;
        *out_height = width;
    } else {
        *out_width = width;
        *out_height = height;
    }
}

/* A point in the logical rectangle, placed in the physical one. The physical
   size is passed rather than derived so that a caller which already holds the
   connector's mode cannot disagree with this function about it. */
static void blit_rotate_point(int rotation, int physical_width,
                              int physical_height, float logical_x,
                              float logical_y, float *out_x, float *out_y)
{
    switch (rotation) {
    case 90:
        *out_x = (float)physical_width - logical_y;
        *out_y = logical_x;
        break;
    case 180:
        *out_x = (float)physical_width - logical_x;
        *out_y = (float)physical_height - logical_y;
        break;
    case 270:
        *out_x = logical_y;
        *out_y = (float)physical_height - logical_x;
        break;
    default:
        *out_x = logical_x;
        *out_y = logical_y;
        break;
    }
}

/* The destination rectangle for the one draw that puts the composed logical
   scene on the physical output. It is the scene's *own* size centred on the
   output, and the rotation is about that rectangle's centre -- passing the
   output's rectangle instead is the mistake this function exists to stop,
   because at 0 and 180 the two are identical and the bug only appears at 90.

   `blit_rotation_angle` is what SDL is handed; it is clockwise, so it is the
   rotation itself. */
static double blit_rotation_angle(int rotation)
{
    return blit_rotation_valid(rotation) ? (double)rotation : 0.0;
}

static void blit_rotation_present_rect(int rotation, int physical_width,
                                       int physical_height, float *out_x,
                                       float *out_y, float *out_width,
                                       float *out_height)
{
    int logical_width;
    int logical_height;

    blit_rotate_size(rotation, physical_width, physical_height,
                     &logical_width, &logical_height);
    *out_width = (float)logical_width;
    *out_height = (float)logical_height;
    *out_x = (float)(physical_width - logical_width) / 2.0f;
    *out_y = (float)(physical_height - logical_height) / 2.0f;
}

#endif
