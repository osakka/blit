#ifndef BLIT_DAMAGE_H
#define BLIT_DAMAGE_H

/* Half-open rectangle union shared by state and presentation. Preserve the
   old right/bottom before moving the origin; pending damage never shrinks. */
static void blit_damage_include(int *valid, int *x, int *y, int *width,
                                 int *height, int left, int top, int w, int h)
{
    int right, bottom;
    if (w <= 0 || h <= 0) return;
    right = left + w;
    bottom = top + h;
    if (*valid) {
        int old_right = *x + *width;
        int old_bottom = *y + *height;
        if (old_right > right) right = old_right;
        if (old_bottom > bottom) bottom = old_bottom;
        if (*x < left) left = *x;
        if (*y < top) top = *y;
    }
    *valid = 1;
    *x = left; *y = top;
    *width = right - left;
    *height = bottom - top;
}

#endif
