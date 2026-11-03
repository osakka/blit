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
     they are kept and reported unavailable, never imitated. The colour
     controls (chroma, phosphor, brightness, contrast; ADR 0207) are per
     pixel, but are presentation effects too: they must colour the light
     the pass adds, and the scene's blended quads cannot mix channels.
     Burn-in (ADR 0208) keeps a history of each pane, so it needs the GPU's
     own textures as well. The phosphor mask (ADR 0209) is the one effect
     laid out in the panel's physical pixels, not the picture's logical
     ones: it belongs to the glass.

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
    BLIT_EFFECT_STATIC_NOISE,
    BLIT_EFFECT_CHROMA,
    BLIT_EFFECT_PHOSPHOR,
    BLIT_EFFECT_BRIGHTNESS,
    BLIT_EFFECT_CONTRAST,
    BLIT_EFFECT_BURN_IN,
    BLIT_EFFECT_MASK,
    BLIT_EFFECT_MASK_TYPE,
    BLIT_EFFECT_COUNT
} BlitEffectId;

typedef enum {
    BLIT_EFFECT_LOCAL,
    BLIT_EFFECT_PRESENTATION
} BlitEffectClass;

typedef struct {
    const char *name;   /* the settings and API key */
    const char *param;  /* its one parameter; 0 is off */
    int minimum;        /* below 0 only for a control with a neutral middle (ADR 0207) */
    int maximum;
    BlitEffectClass effect_class;
    const char *title;  /* what a person reads, e.g. a control panel label */
    int animated;       /* changes with time: needs the frame clock (ADR 0188) */
} BlitEffectSpec;

static const BlitEffectSpec blit_effect_specs[BLIT_EFFECT_COUNT] = {
    /* Every second pixel row from the pane's top, darkened by intensity%. */
    { "scanlines", "intensity", 0, 100, BLIT_EFFECT_LOCAL, "Scanlines", 0 },
    /* Each corner of the pane's region cut to a quarter circle, in black. */
    { "corners", "radius_px", 0, 1024, BLIT_EFFECT_LOCAL, "Corners", 0 },
    /* The pane bulges like a tube: blit_effect_warp. */
    { "curvature", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Curvature", 0 },
    /* A soft bloom from a blurred copy of the pane, added on top. */
    { "glow", "intensity", 0, 100, BLIT_EFFECT_PRESENTATION, "Glow", 0 },
    /* An old set's colour fringe: each channel leans to one side,
       blit_effect_rgb_shift_px apart (cool-retro-term's rgb shift). */
    { "rgb_shift", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "RGB shift", 0 },
    /* A soft band of light sweeping down the pane: blit_effect_line_value.
       The key is cool-retro-term's name ("glowing line"); the title is the
       owner's, after the real CRT artefact it evokes. */
    { "glowing_line", "intensity", 0, 100, BLIT_EFFECT_PRESENTATION, "Hum bar", 1 },
    /* ADR 0190, cool-retro-term's moving set: the brightness wavers, the
       picture sways sideways now and then, and its sample points shimmer. */
    { "flicker", "intensity", 0, 100, BLIT_EFFECT_PRESENTATION, "Flicker", 1 },
    { "horizontal_sync", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Horizontal sync", 1 },
    { "jitter", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Jitter", 1 },
    /* ADR 0207, the rest of cool-retro-term's tube: animated grain added as
       light (blit_effect_static_noise), then the colour controls,
       blit_effect_tube_colour. A phosphor is 0xRRGGBB, 0 none (white). */
    { "static_noise", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Static noise", 1 },
    { "chroma", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Chroma", 0 },
    { "phosphor", "color", 0, 0xffffff, BLIT_EFFECT_PRESENTATION, "Phosphor", 0 },
    { "brightness", "level", -100, 100, BLIT_EFFECT_PRESENTATION, "Brightness", 0 },
    { "contrast", "level", -100, 100, BLIT_EFFECT_PRESENTATION, "Contrast", 0 },
    /* ADR 0208, cool-retro-term's burn-in: what a pane showed fades out
       over blit_effect_burn_in_us instead of vanishing. Not `animated`: it
       needs the frame clock only while something is fading, which the
       GLES compositor reports per output, never on an idle screen. */
    { "burn_in", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "Burn-in", 0 },
    /* ADR 0209, the tube's phosphor structure: each physical pixel passes
       its own channel and dims the other two by amount%, in the pattern of
       a mask type (BLIT_EFFECT_MASK_*; 0, the default, is the aperture
       grille), then is lifted back by the mask's mean: blit_effect_mask_rgb.
       A type alone draws nothing, as a phosphor without chroma. */
    { "mask", "amount", 0, 100, BLIT_EFFECT_PRESENTATION, "CRT mask", 0 },
    { "mask_type", "kind", 0, 2, BLIT_EFFECT_PRESENTATION, "Mask type", 0 },
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

/* Static noise at 100 adds grain of up to this much white light. */
#define BLIT_EFFECT_NOISE_GAIN 0.5

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
        if (set->value[i] != 0 && blit_effect_specs[i].effect_class == effect_class)
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
        if (set->value[i] != 0 && blit_effect_specs[i].animated) return 1;
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

/* ADR 0207. Static noise's strength, 0..BLIT_EFFECT_NOISE_GAIN: each
   logical pixel gains white light of strength x a hash of the pixel and
   the frame's seed, a fresh grain every frame. */
static inline double blit_effect_static_noise(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_STATIC_NOISE];
    if (amount <= 0) return 0.0;
    return (amount > 100 ? 100 : amount) / 100.0 * BLIT_EFFECT_NOISE_GAIN;
}

/* The tube's colour controls, ADR 0207, as the shader applies them, in
   this order, to a colour in 0..1:
   - chroma a = amount / 100: c = mix(c, P x Y(c), a), Y the Rec. 709 luma
     and P the phosphor (white when 0). 0 is full colour, 1 monochrome in
     the phosphor's colour;
   - brightness: c x (1 + level / 100), so -100 is black and 100 double;
   - contrast: 0.5 + (c - 0.5) x (1 + level / 100), about mid-grey;
   each clamped to 0..1. Level 0 leaves the colour exactly as it was. */
static inline void blit_effect_phosphor_rgb(const BlitEffectSet *set, double rgb[3])
{
    int color = set->value[BLIT_EFFECT_PHOSPHOR];
    if (color <= 0) color = 0xffffff;
    rgb[0] = ((color >> 16) & 0xff) / 255.0;
    rgb[1] = ((color >> 8) & 0xff) / 255.0;
    rgb[2] = (color & 0xff) / 255.0;
}

static inline double blit_effect_chroma(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_CHROMA];
    if (amount <= 0) return 0.0;
    return (amount > 100 ? 100 : amount) / 100.0;
}

/* A level's gain, 1 + level / 100: 0..2, exactly 1 at 0. */
static inline double blit_effect_level_gain(const BlitEffectSet *set, BlitEffectId id)
{
    int level = set->value[id];
    if (level < -100) level = -100;
    if (level > 100) level = 100;
    return 1.0 + level / 100.0;
}

static inline double blit_effect_clamp01(double v)
{
    return v < 0.0 ? 0.0 : v > 1.0 ? 1.0 : v;
}

static inline void blit_effect_tube_colour(const BlitEffectSet *set, double c[3])
{
    double chroma = blit_effect_chroma(set), p[3], y;
    double brightness = blit_effect_level_gain(set, BLIT_EFFECT_BRIGHTNESS);
    double contrast = blit_effect_level_gain(set, BLIT_EFFECT_CONTRAST);
    int i;
    blit_effect_phosphor_rgb(set, p);
    y = 0.2126 * c[0] + 0.7152 * c[1] + 0.0722 * c[2];
    for (i = 0; i < 3; ++i) {
        if (chroma > 0) c[i] = c[i] + (p[i] * y - c[i]) * chroma;
        if (brightness != 1.0) c[i] = blit_effect_clamp01(c[i] * brightness);
        if (contrast != 1.0) c[i] = blit_effect_clamp01(0.5 + (c[i] - 0.5) * contrast);
    }
}

/* ADR 0208. Burn-in's fade time: light a pane stops showing falls
   linearly from its level to black in this long -- 0.16 s near 0 up to
   1.6 s at 100, cool-retro-term's range. 0 is off. */
#define BLIT_EFFECT_BURN_IN_MIN_US 160000LL
#define BLIT_EFFECT_BURN_IN_MAX_US 1600000LL
/* Levels in a channel: the history is 8 bits, and fades in whole levels. */
#define BLIT_EFFECT_BURN_IN_LEVELS 255

static inline long long blit_effect_burn_in_us(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_BURN_IN];
    if (amount <= 0) return 0;
    return BLIT_EFFECT_BURN_IN_MIN_US +
        (BLIT_EFFECT_BURN_IN_MAX_US - BLIT_EFFECT_BURN_IN_MIN_US) *
        (amount > 100 ? 100 : amount) / 100;
}

/* The fade over `elapsed_us`, in whole levels of 1/255, as the history is
   faded each presented frame: history = max(history - levels / 255, what
   the pane showed). The remainder is carried in `*carry` (microsecond
   levels, below fade_us), so frame by frame no time is lost or counted
   twice, and a frame rate never changes the speed: over any run of frames
   the levels add up to elapsed x 255 / fade_us, rounded down, or one more
   for the carry brought in. Whole levels subtract exactly in 8 bits, so a
   trail never sticks short of black, as a multiplied one would; a gap of
   a whole fade time or more is everything, 255. */
static inline int blit_effect_burn_in_levels(long long fade_us, long long elapsed_us,
                                             long long *carry)
{
    long long total;
    if (fade_us <= 0 || elapsed_us <= 0) return 0;
    if (elapsed_us >= fade_us) {
        *carry = 0;
        return BLIT_EFFECT_BURN_IN_LEVELS;
    }
    total = elapsed_us * BLIT_EFFECT_BURN_IN_LEVELS + *carry;
    *carry = total % fade_us;
    total /= fade_us;
    return (int)(total > BLIT_EFFECT_BURN_IN_LEVELS ? BLIT_EFFECT_BURN_IN_LEVELS : total);
}

/* ADR 0209. The mask types, mask_type's `kind`. Each is a pattern of
   physical pixels counted from the frame's top-left corner, so a periodic
   table: which channel a pixel passes, or none (a gap between slots).
   - APERTURE_GRILLE (Trinitron): vertical stripes, column x mod 3 is R, G,
     B. Period 3 x 1.
   - SLOT: the grille, cut into slots three rows tall by a dark row; every
     other triad (x / 3 odd) is set two rows down, as bricks. Period 6 x 4.
   - SHADOW (the delta-gun dot triad): dots two pixels wide, R G B along a
     row, each pair of rows set half a triad (3 px) along. Period 6 x 4. */
#define BLIT_EFFECT_MASK_APERTURE_GRILLE 0
#define BLIT_EFFECT_MASK_SLOT 1
#define BLIT_EFFECT_MASK_SHADOW 2

static inline double blit_effect_mask_strength(const BlitEffectSet *set)
{
    int amount = set->value[BLIT_EFFECT_MASK];
    if (amount <= 0) return 0.0;
    return (amount > 100 ? 100 : amount) / 100.0;
}

static inline int blit_effect_mask_kind(const BlitEffectSet *set)
{
    int kind = set->value[BLIT_EFFECT_MASK_TYPE];
    return kind < 0 || kind > BLIT_EFFECT_MASK_SHADOW ? 0 : kind;
}

/* The channel physical pixel (x, y), x and y >= 0, passes: 0 R, 1 G, 2 B,
   or -1 for none. */
static inline int blit_effect_mask_channel(int kind, int x, int y)
{
    if (kind == BLIT_EFFECT_MASK_SLOT)
        return (y + 2 * ((x / 3) & 1)) % 4 == 3 ? -1 : x % 3;
    if (kind == BLIT_EFFECT_MASK_SHADOW)
        return (x + 3 * (y / 2)) % 6 / 2;
    return x % 3;
}

/* The mask's compensation: 1 / its mean transmission, so a colour the gain
   does not clip keeps its average light over one period. A third of the
   pixels pass a given channel (a quarter for the slot mask, whose gaps
   pass none), the rest pass 1 - s: gain 1 / (1 - 2s/3), 3 at full
   strength; the slot mask 1 / (1 - 3s/4), 4. */
static inline double blit_effect_mask_gain(int kind, double strength)
{
    double lit = kind == BLIT_EFFECT_MASK_SLOT ? 0.25 : 1.0 / 3.0;
    return 1.0 / (lit + (1.0 - lit) * (1.0 - strength));
}

/* The factor each channel of physical pixel (x, y) is multiplied by: the
   gain where the pixel passes the channel, (1 - s) x gain where it does
   not. The shader applies min(c x factor, 1), after the colour controls.
   All 1 when the mask is off. */
static inline void blit_effect_mask_rgb(const BlitEffectSet *set, int x, int y,
                                        double factor[3])
{
    double s = blit_effect_mask_strength(set);
    int kind = blit_effect_mask_kind(set), channel, i;
    double gain = blit_effect_mask_gain(kind, s);
    channel = blit_effect_mask_channel(kind, x, y);
    for (i = 0; i < 3; ++i)
        factor[i] = s <= 0.0 ? 1.0 : i == channel ? gain : (1.0 - s) * gain;
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
