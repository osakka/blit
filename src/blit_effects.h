#ifndef BLIT_EFFECTS_H
#define BLIT_EFFECTS_H

/* Pane effects (ADR 0186). One registry for the settings parser, the API and
   both compositors: adding an effect is a row here, a parameter in
   BlitEffectSet, and one branch in each compositor.

   Two classes (ADR 0187):
   - LOCAL effects: a pixel's result depends only on its position in the
     pane and on the pixel underneath. That lets a compositor apply them
     inside exactly the rectangle it just repainted, so an unchanged pixel is
     never darkened twice and an effect costs nothing outside damage. Both
     compositors draw them, into the scene.
   - PRESENTATION effects move geometry or read neighbours (curvature, glow).
     Only the GLES compositor draws them, as it presents the scene; elsewhere
     they are kept and reported unavailable, never imitated.

   The registry order is the order of application: corners last of the local
   ones, because they define the pane's edge. No backend or user reorders
   them. One exception, decided by one predicate: when a pane's curvature is
   drawn, its scanlines are drawn on the glass by the presentation pass, not
   in the scene (blit_effect_scanlines_presented). */

#include <stdint.h>

typedef enum {
    BLIT_EFFECT_SCANLINES,
    BLIT_EFFECT_CORNERS,
    BLIT_EFFECT_CURVATURE,
    BLIT_EFFECT_GLOW,
    BLIT_EFFECT_RGB_SHIFT,
    BLIT_EFFECT_GLOWING_LINE,
    BLIT_EFFECT_FLICKER,
    BLIT_EFFECT_HORIZONTAL_SYNC,
    BLIT_EFFECT_JITTER,
    BLIT_EFFECT_COUNT
} BlitEffectId;

typedef enum {
    BLIT_EFFECT_LOCAL,
    BLIT_EFFECT_PRESENTATION
} BlitEffectClass;

typedef struct {
    const char *name;   /* the settings and API key */
    const char *param;  /* its one parameter; 0 is off */
    int maximum;
    BlitEffectClass effect_class;
    const char *title;  /* what a person reads, e.g. a control panel label */
    int animated;       /* changes with time: needs the frame clock (ADR 0188) */
} BlitEffectSpec;

static const BlitEffectSpec blit_effect_specs[BLIT_EFFECT_COUNT] = {
    /* Every second pixel row from the pane's top, darkened by intensity%. */
    { "scanlines", "intensity", 100, BLIT_EFFECT_LOCAL, "Scanlines", 0 },
    /* Each corner of the pane's region cut to a quarter circle, in black. */
    { "corners", "radius_px", 1024, BLIT_EFFECT_LOCAL, "Corners", 0 },
    /* The pane bulges like a tube: blit_effect_warp. */
    { "curvature", "amount", 100, BLIT_EFFECT_PRESENTATION, "Curvature", 0 },
    /* A soft bloom from a blurred copy of the pane, added on top. */
    { "glow", "intensity", 100, BLIT_EFFECT_PRESENTATION, "Glow", 0 },
    /* An old set's colour fringe: each channel leans to one side,
       blit_effect_rgb_shift_px apart (cool-retro-term's rgb shift). */
    { "rgb_shift", "amount", 100, BLIT_EFFECT_PRESENTATION, "RGB shift", 0 },
    /* A soft band of light sweeping down the pane: blit_effect_line_value.
       The key is cool-retro-term's name ("glowing line"); the title is the
       owner's, after the real CRT artefact it evokes. */
    { "glowing_line", "intensity", 100, BLIT_EFFECT_PRESENTATION, "Hum bar", 1 },
    /* ADR 0190, cool-retro-term's moving set: the brightness wavers, the
       picture sways sideways now and then, and its sample points shimmer. */
    { "flicker", "intensity", 100, BLIT_EFFECT_PRESENTATION, "Flicker", 1 },
    { "horizontal_sync", "amount", 100, BLIT_EFFECT_PRESENTATION, "Horizontal sync", 1 },
    { "jitter", "amount", 100, BLIT_EFFECT_PRESENTATION, "Jitter", 1 },
};

/* The glowing line (ADR 0188), as cool-retro-term's randomPass: its length,
   a third of the pane; its speed, 0.15 sweeps a second (one sweep is the
   pane's height plus the band's length); its strength, white light up to
   0.2 at 100. */
#define BLIT_EFFECT_LINE_LENGTH (1.0 / 3.0)
#define BLIT_EFFECT_LINE_SPEED 0.15
#define BLIT_EFFECT_LINE_GAIN 0.2

/* Glow at intensity 100 adds this much of the blurred colour. */
#define BLIT_EFFECT_GLOW_GAIN 0.6f

/* A pane's effects, by BlitEffectId. All zero is no effect. */
typedef struct {
    int value[BLIT_EFFECT_COUNT];
} BlitEffectSet;

/* Whether the set has any effect of that class. */
static inline int blit_effect_set_has(const BlitEffectSet *set,
                                      BlitEffectClass effect_class)
{
    int i;
    for (i = 0; i < BLIT_EFFECT_COUNT; ++i)
        if (set->value[i] > 0 && blit_effect_specs[i].effect_class == effect_class)
            return 1;
    return 0;
}

/* The one predicate of ADR 0187: a pane's scanlines move from the scene to
   the presentation pass exactly when its curvature is drawn, which needs a
   compositor that draws presentation effects (`presented`). */
static inline int blit_effect_scanlines_presented(const BlitEffectSet *set,
                                                  int presented)
{
    return presented && set->value[BLIT_EFFECT_CURVATURE] > 0;
}

/* The curvature warp's k: 0 off, 0.25 at the maximum. */
static inline double blit_effect_curvature_k(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_CURVATURE];
    if (amount <= 0) return 0.0;
    return (amount > 100 ? 100 : amount) / 400.0;
}

/* Whether the set has an effect that changes with time. */
static inline int blit_effect_set_animated(const BlitEffectSet *set)
{
    int i;
    for (i = 0; i < BLIT_EFFECT_COUNT; ++i)
        if (set->value[i] > 0 && blit_effect_specs[i].animated) return 1;
    return 0;
}

/* The glowing line's strength, 0..BLIT_EFFECT_LINE_GAIN. */
static inline double blit_effect_line_strength(const BlitEffectSet *set)
{
    int intensity = set->value[BLIT_EFFECT_GLOWING_LINE];
    if (intensity <= 0) return 0.0;
    return (intensity > 100 ? 100 : intensity) / 100.0 * BLIT_EFFECT_LINE_GAIN;
}

/* The line's phase, 0..1, at a monotonic time: computed here in double,
   never as a raw time in a shader's float. */
static inline double blit_effect_line_phase(double seconds)
{
    double cycles = seconds * BLIT_EFFECT_LINE_SPEED;
    return cycles - (double)(long long)cycles;
}

/* The line's added light, 0..1, at `y` (0 at the pane's top, 1 at its
   bottom) for a phase: a smoothstep ramp up to the leading edge at
   (1 + length) * phase, and nothing below it -- cool-retro-term's
   fract(smoothstep(-L, 0, y - p)). The shader computes the same. */
static inline double blit_effect_line_value(double phase, double y)
{
    double edge = (1.0 + BLIT_EFFECT_LINE_LENGTH) * phase;
    double d = y - edge, u;
    if (d <= -BLIT_EFFECT_LINE_LENGTH || d >= 0.0) return 0.0;
    u = (d + BLIT_EFFECT_LINE_LENGTH) / BLIT_EFFECT_LINE_LENGTH;
    return u * u * (3.0 - 2.0 * u);
}

/* ADR 0190: one frame's randomness, from its number, never the clock --
   cool-retro-term reads two values of a noise texture per frame; this is
   splitmix64 of the frame number, so a frame's values can be checked. `r`
   and `g` play the texture's red and green, `seed` jitter's noise seed. */
static inline double blit_effect_unit(uint64_t x)
{
    x += 0x9e3779b97f4a7c15ULL;
    x = (x ^ (x >> 30)) * 0xbf58476d1ce4e5b9ULL;
    x = (x ^ (x >> 27)) * 0x94d049bb133111ebULL;
    x ^= x >> 31;
    return (double)(x >> 11) / 9007199254740992.0;
}

static inline void blit_effect_frame_noise(uint64_t frame, double *r, double *g,
                                           double *seed)
{
    *r = blit_effect_unit(frame * 3U);
    *g = blit_effect_unit(frame * 3U + 1U);
    *seed = blit_effect_unit(frame * 3U + 2U);
}

/* Flicker's brightness for a frame: 1 + (g - 0.5) x flickering. */
static inline double blit_effect_flicker(const BlitEffectSet *set, double g)
{
    int intensity = set->value[BLIT_EFFECT_FLICKER];
    if (intensity <= 0) return 1.0;
    return 1.0 + (g - 0.5) * (intensity > 100 ? 100 : intensity) / 100.0;
}

/* Horizontal sync for a frame, as cool-retro-term: strength = mix(0.05,
   0.35, h); the picture sways, x += sin(y x freq + phase) x scale, only on
   frames whose r falls under the strength. `phase` is (seconds x freq) mod
   2 pi, so no large time reaches the shader. */
static inline void blit_effect_horizontal_sync(const BlitEffectSet *set, double r,
                                               double g, double seconds,
                                               double *scale, double *freq,
                                               double *phase)
{
    int amount = set->value[BLIT_EFFECT_HORIZONTAL_SYNC];
    double h = amount <= 0 ? 0.0 : (amount > 100 ? 100 : amount) / 100.0;
    double strength = 0.05 + 0.30 * h, rand = strength - r, turns;
    *freq = 4.0 + 36.0 * g;
    *scale = h > 0 && rand > 0 ? rand * strength * h : 0.0;
    turns = seconds * *freq / 6.283185307179586;
    *phase = (turns - (double)(long long)turns) * 6.283185307179586;
}

/* Jitter's largest displacement, as fractions of the pane: (0.007, 0.002)
   x j^2, since cool-retro-term multiplies by j twice. Its noise grid is
   cell_width / 6 pixels: cool-retro-term's 1.33 x font scale on an 8 px
   base cell. */
static inline double blit_effect_jitter(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_JITTER];
    double j = amount <= 0 ? 0.0 : (amount > 100 ? 100 : amount) / 100.0;
    return j * j;
}

/* The one predicate for corners under content-moving effects: a pane with
   horizontal sync or jitter has its corners applied by the presentation
   pass, at the undisplaced position, so they stay still while the picture
   sways; the scene then skips them. */
static inline int blit_effect_corners_presented(const BlitEffectSet *set, int presented)
{
    return presented && set->value[BLIT_EFFECT_CORNERS] > 0 &&
        (set->value[BLIT_EFFECT_HORIZONTAL_SYNC] > 0 || set->value[BLIT_EFFECT_JITTER] > 0);
}

/* RGB shift's sample distance in pixels: half a cell at 100, so the fringe
   keeps its look at every font size (cool-retro-term's 4 px at its 8 px
   base cell). The three samples, left, centre and right, weigh
   R 0.1/0.6/0.3, G 0.2/0.6/0.2 and B 0.3/0.6/0.1: each sums to 1, so a flat
   colour is unchanged and only edges fringe. */
static inline double blit_effect_rgb_shift_px(const BlitEffectSet *set, int cell_width)
{
    int amount = set->value[BLIT_EFFECT_RGB_SHIFT];
    if (amount <= 0 || cell_width <= 0) return 0.0;
    return (amount > 100 ? 100 : amount) / 100.0 * cell_width / 2.0;
}

/* The warp, shared by the shader (blit_gles.c, the same expression) and
   pointer input: the screen point q, in pane-normalised [-1, 1] coordinates,
   shows the content at s = q (1 + k r^2) / (1 + k). The centre and each
   edge's midpoint stay put; the rest bulges outward. Returns 0 when s falls
   outside the pane, where the screen is black. */
static inline int blit_effect_warp(double k, double qx, double qy,
                                   double *sx, double *sy)
{
    double f = (1.0 + k * (qx * qx + qy * qy)) / (1.0 + k);
    *sx = qx * f;
    *sy = qy * f;
    return *sx >= -1.0 && *sx <= 1.0 && *sy >= -1.0 && *sy <= 1.0;
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
