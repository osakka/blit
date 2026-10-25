#ifndef BLIT_EFFECTS_H
#define BLIT_EFFECTS_H

/* Pane effects (ADR 0186). One registry for the settings parser, the API and
   both compositors: adding an effect is a row here, a parameter in
   BlitEffectSet, and one branch in each compositor.

   Every effect here is LOCAL: a pixel's result depends only on its position
   in the pane and on the pixel underneath. That is what lets a compositor
   apply them inside exactly the rectangle it just repainted, so an unchanged
   pixel is never darkened twice and an effect costs nothing outside damage.
   Effects that sample neighbours or move geometry (curvature, glow) need an
   offscreen pass and are a different class, not added here.

   The registry order is the order of application: corners last, because
   they define the pane's edge. No backend or user reorders them. */

#include <stdint.h>

typedef enum {
    BLIT_EFFECT_SCANLINES,
    BLIT_EFFECT_CORNERS,
    BLIT_EFFECT_COUNT
} BlitEffectId;

typedef struct {
    const char *name;   /* the settings and API key */
    const char *param;  /* its one parameter; 0 is off */
    int maximum;
} BlitEffectSpec;

static const BlitEffectSpec blit_effect_specs[BLIT_EFFECT_COUNT] = {
    /* Every second pixel row from the pane's top, darkened by intensity%. */
    { "scanlines", "intensity", 100 },
    /* Each corner of the pane's region cut to a quarter circle, in black. */
    { "corners", "radius_px", 1024 },
};

/* A pane's effects, by BlitEffectId. All zero is no effect. */
typedef struct {
    int value[BLIT_EFFECT_COUNT];
} BlitEffectSet;

static inline int blit_effect_set_active(const BlitEffectSet *set)
{
    int i;
    for (i = 0; i < BLIT_EFFECT_COUNT; ++i)
        if (set->value[i] > 0) return 1;
    return 0;
}

/* The black alpha laid on pane row `row` (0 at the pane's top). */
static inline unsigned blit_effect_scanline_alpha(const BlitEffectSet *set,
                                                  int row)
{
    int intensity = set->value[BLIT_EFFECT_SCANLINES];
    if (intensity <= 0 || (row & 1) == 0) return 0;
    return (unsigned)(intensity > 100 ? 100 : intensity) * 255U / 100U;
}

/* The radius actually drawn: never more than half the pane's shorter side. */
static inline int blit_effect_corner_radius(const BlitEffectSet *set,
                                            int width, int height)
{
    int radius = set->value[BLIT_EFFECT_CORNERS];
    if (radius > width / 2) radius = width / 2;
    if (radius > height / 2) radius = height / 2;
    return radius > 0 ? radius : 0;
}

/* Black coverage, 0..255, of pixel (x, y) in the top-left corner's r x r
   square: the share of the pixel outside the quarter circle centred at
   (r, r). 8x8 samples per pixel in sixteenths, as the title-bar corners
   (blit_raster.c, draw_bar_corner) are drawn, so the two arcs match. The
   other corners mirror the coordinates. */
static inline unsigned char blit_effect_corner_coverage(int radius, int x, int y)
{
    long r = radius, outside = 0;
    int sx, sy;
    for (sy = 0; sy < 8; ++sy)
        for (sx = 0; sx < 8; ++sx) {
            long qx = 16L * x + 2 * sx + 1, qy = 16L * y + 2 * sy + 1;
            long dx = 16 * r - qx, dy = 16 * r - qy;
            outside += dx > 0 && dy > 0 && dx * dx + dy * dy > 256 * r * r;
        }
    return (unsigned char)((outside * 255 + 32) / 64);
}

#endif
