#ifndef BLIT_POINTER_ART_H
#define BLIT_POINTER_ART_H

/* One code-native vector source for the pointer. Straight-alpha RGBA bytes;
   raster work is bounded and only performed when appearance changes. */
#include <stdint.h>
#include <string.h>

#include "blit_pointer_names.h"

#define BLIT_POINTER_MAX 264   /* a shake grows the pointer to 256 px (ADR 0234) */
#define BLIT_POINTER_SPOKES 12 /* the busy pointer's spokes (ADR 0235) */
#define BLIT_POINTER_SHAPE_BUSY 4
/* The busy pointer's phases a turn: 36, so the wheel turns 10 degrees a
   step (the spokes step every third phase, one spoke). */
#define BLIT_POINTER_PHASES 36
/* The busy pointer's styles (ADR 0235, amended 0.3.113): a colour wheel
   whose colours go round, or the ring of spokes that dim in turn. */
#define BLIT_POINTER_BUSY_SPOKES 0
#define BLIT_POINTER_BUSY_WHEEL 1
/* The busy pointer's spokes, clockwise from 12 o'clock: unit directions
   (sin, -cos) of k * 30 degrees, constants so the art needs no libm. */
static const float blit_pointer_spoke_dir[BLIT_POINTER_SPOKES][2] = {
    {0.0f, -1.0f}, {0.5f, -0.8660254f}, {0.8660254f, -0.5f}, {1.0f, 0.0f},
    {0.8660254f, 0.5f}, {0.5f, 0.8660254f}, {0.0f, 1.0f}, {-0.5f, 0.8660254f},
    {-0.8660254f, 0.5f}, {-1.0f, 0.0f}, {-0.8660254f, -0.5f}, {-0.5f, -0.8660254f}
};
typedef struct {
    int shape;                 /* index into blit_pointer_shapes */
    int palette;               /* pearl, obsidian, neon, amber */
    int size;                  /* nominal24..64 physical pixels */
    int shadow;
    int busy_style;            /* BLIT_POINTER_BUSY_WHEEL or _SPOKES */
} BlitPointerConfig;
typedef struct {
    BlitPointerConfig config;
    int width, height, hotspot_x, hotspot_y;
    unsigned long long revision;
    unsigned char rgba[BLIT_POINTER_MAX * BLIT_POINTER_MAX * 4];
} BlitPointerArt;

/* The first BLIT_POINTER_API_SHAPES are the pointers the settings API offers
   as the display's own; the rest exist for programs asking by OSC 22. The
   order is the stored index: append, never reorder. */
static const char *const blit_pointer_shapes[] = {
    "arrow", "hand", "precision", "text", "wait", "move", "ew-resize",
    "ns-resize", "nwse-resize", "nesw-resize", "not-allowed"
};
#define BLIT_POINTER_SHAPE_COUNT 11
#define BLIT_POINTER_API_SHAPES 3
static const char *const blit_pointer_palettes[] = {"pearl", "obsidian", "neon", "amber"};
static const char *const blit_pointer_busy_styles[] = {"spokes", "wheel"};

/* One sequence for every piece of pointer art, so two arts never share a
   revision: the display and the GLES texture cache take an equal revision
   for the same picture, and the busy pointer hands them the shown art and
   its spun copy in turn (ADR 0235). */
static unsigned long long blit_pointer_next_revision(void)
{
    static unsigned long long revision;
    return ++revision;
}

static BlitPointerConfig blit_pointer_default(void)
{
    BlitPointerConfig config = {0, 0, 24, 1, BLIT_POINTER_BUSY_WHEEL};
    return config;
}

static float blit_pointer_segment(float x, float y, float ax, float ay,
                                  float bx, float by)
{
    float dx = bx - ax, dy = by - ay;
    float t = ((x - ax) * dx + (y - ay) * dy) / (dx * dx + dy * dy);
    if (t < 0) t = 0;
    if (t > 1) t = 1;
    dx = x - ax - t * dx; dy = y - ay - t * dy;
    return dx * dx + dy * dy;
}

static float blit_pointer_capsule(float x, float y, float ax, float ay,
                                  float bx, float by, float r)
{
    return blit_pointer_segment(x, y, ax, ay, bx, by) - r * r;
}

static float blit_pointer_polygon(const float (*p)[2], int n, float x, float y)
{
    float d = 1000000.0f;
    int i, j, inside = 0;
    for (i = 0, j = n - 1; i < n; j = i++) {
        float q = blit_pointer_segment(x, y, p[i][0], p[i][1], p[j][0], p[j][1]);
        if (q < d) d = q;
        if (((p[i][1] > y) != (p[j][1] > y)) &&
            x < (p[j][0] - p[i][0]) * (y - p[i][1]) /
                (p[j][1] - p[i][1]) + p[i][0]) inside = !inside;
    }
    return inside ? -d : d;
}

#define BLIT_POINTER_MIN(d, q) do { float q_ = (q); if (q_ < (d)) (d) = q_; } while (0)

/* Double-headed arrows share one head: a triangle pointing at tip. */
static float blit_pointer_head(float x, float y, float tx, float ty,
                               float dx, float dy)
{
    float p[3][2];
    p[0][0] = tx; p[0][1] = ty;
    p[1][0] = tx - dx * 7 - dy * 5; p[1][1] = ty - dy * 7 + dx * 5;
    p[2][0] = tx - dx * 7 + dy * 5; p[2][1] = ty - dy * 7 - dx * 5;
    return blit_pointer_polygon((const float (*)[2])p, 3, x, y);
}

/* Signed squared distance to the silhouette: negative inside. */
static float blit_pointer_distance(int shape, float x, float y)
{
    static const float arrow[][2] = {
        {2, 1}, {2, 24}, {8, 18}, {13, 28}, {17, 26}, {12, 16}, {21, 16}
    };
    static const float hand[][5] = {
        {12, 4, 12, 19, 2.3f}, {17, 12, 17, 21, 2.2f},
        {21, 14, 21, 23, 2.0f}, {25, 17, 25, 23, 1.8f},
        {5, 16, 13, 26, 2.4f}, {15, 22, 22, 24, 4.8f}
    };
    float d = 1000000.0f;
    int i, j, inside = 0;
    if (shape == 3) {
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 16, 6, 16, 26, 1.6f));
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 11.5f, 5, 20.5f, 5, 1.6f));
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 11.5f, 27, 20.5f, 27, 1.6f));
        return d;
    }
    if (shape == 4) {
        /* Busy (wait, progress): a ring of BLIT_POINTER_SPOKES spokes about
           the hotspot, drawn alike; blit_pointer_spin dims them in turn, so
           it goes round. */
        int k;
        for (k = 0; k < BLIT_POINTER_SPOKES; ++k) {
            float dx = blit_pointer_spoke_dir[k][0], dy = blit_pointer_spoke_dir[k][1];
            BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 16 + 6.0f * dx, 16 + 6.0f * dy,
                                                     16 + 13.0f * dx, 16 + 13.0f * dy, 1.7f));
        }
        return d;
    }
    if (shape >= 5 && shape <= 9) {
        const float k = 0.70710678f;
        if (shape == 5 || shape == 6) {
            BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 6, 16, 26, 16, 1.6f));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 2, 16, -1, 0));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 30, 16, 1, 0));
        }
        if (shape == 5 || shape == 7) {
            BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 16, 6, 16, 26, 1.6f));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 16, 2, 0, -1));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 16, 30, 0, 1));
        }
        if (shape == 8) {
            BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 8, 8, 24, 24, 1.6f));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 4, 4, -k, -k));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 28, 28, k, k));
        }
        if (shape == 9) {
            BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 24, 8, 8, 24, 1.6f));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 28, 4, k, -k));
            BLIT_POINTER_MIN(d, blit_pointer_head(x, y, 4, 28, -k, k));
        }
        return d;
    }
    if (shape == 10) {
        float r = (x - 16) * (x - 16) + (y - 16) * (y - 16);
        /* (r - R^2) / 2R approximates the distance from the circle. */
        float ring = (r - 10.5f * 10.5f) / 21.0f;
        d = ring * ring - 2.4f * 2.4f;
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 9, 9, 23, 23, 1.8f));
        return d;
    }
    if (shape == 2) {
        float r = (x - 16) * (x - 16) + (y - 16) * (y - 16);
        float ring = r < 64 ? (36 - r) / 16 : (r - 100) / 16;
        float q;
        d = ring;
        q = blit_pointer_segment(x,y,16,2,16,7) - 2.25f; if (q < d) d = q;
        q = blit_pointer_segment(x,y,16,25,16,30) - 2.25f; if (q < d) d = q;
        q = blit_pointer_segment(x,y,2,16,7,16) - 2.25f; if (q < d) d = q;
        q = blit_pointer_segment(x,y,25,16,30,16) - 2.25f; if (q < d) d = q;
        return d;
    }
    if (shape == 1) {
        for (i = 0; i < 6; ++i) {
            float q = blit_pointer_segment(x, y, hand[i][0], hand[i][1], hand[i][2], hand[i][3]);
            q -= hand[i][4] * hand[i][4];
            if (q < d) d = q;
        }
        return d;
    }
    for (i = 0, j = 6; i < 7; j = i++) {
        float q = blit_pointer_segment(x, y, arrow[i][0], arrow[i][1], arrow[j][0], arrow[j][1]);
        if (q < d) d = q;
        if (((arrow[i][1] > y) != (arrow[j][1] > y)) &&
            x < (arrow[j][0] - arrow[i][0]) * (y - arrow[i][1]) /
                (arrow[j][1] - arrow[i][1]) + arrow[i][0]) inside = !inside;
    }
    return inside ? -d : d;
}

/* The wheel (ADR 0235, 0.3.113): a disc of radius 12.5 about (16, 16) in
   the art's 32-unit space, its colours sweeping once round, lighter toward
   the middle and from above, inside a thin dark rim so it reads on any
   background. Blit's own drawing, no libm: the angle is an atan2 to about
   0.3 degrees, the hue a six-segment ramp. */
#define BLIT_POINTER_WHEEL_R 12.5f
#define BLIT_POINTER_WHEEL_RIM 0.9f

/* The angle of (dx, dy), clockwise from 12 o'clock, in turns (0 .. 1). */
static float blit_pointer_turns(float dx, float dy)
{
    float ax = dx < 0 ? -dx : dx, ay = dy < 0 ? -dy : dy, a, s, r, t;
    if (ax == 0 && ay == 0) return 0;
    a = (ax < ay ? ax : ay) / (ax < ay ? ay : ax);
    s = a * a;
    r = ((-0.0464964749f * s + 0.15931422f) * s - 0.327622764f) * s * a + a;   /* atan(a), radians */
    if (ax > ay) r = 1.57079637f - r;             /* the angle from the vertical axis */
    t = r / 6.28318531f;                          /* 0 .. 0.25 within the quadrant */
    if (dx >= 0 && dy < 0) return t;              /* top right */
    if (dx >= 0) return 0.5f - t;                 /* bottom right */
    if (dy >= 0) return 0.5f + t;                 /* bottom left */
    return 1.0f - t;                              /* top left */
}

/* The wheel's colour at `turns` round and `r` (32-unit space) out. */
static void blit_pointer_wheel_colour(float turns, float r, float vy, unsigned int colour[3])
{
    static const float stops[7][3] = {
        {255, 72, 92}, {255, 168, 48}, {255, 226, 74}, {84, 214, 120},
        {58, 160, 255}, {150, 98, 255}, {255, 72, 92}
    };
    float h, f, lift, light, rgb[3];
    int k, c;
    turns -= (float)(int)turns;
    if (turns < 0) turns += 1;
    h = turns * 6;
    k = (int)h;
    if (k > 5) k = 5;
    f = h - k;
    for (c = 0; c < 3; ++c) rgb[c] = stops[k][c] + (stops[k + 1][c] - stops[k][c]) * f;
    /* Lighter toward the middle, a little from above. */
    lift = 1.0f - r / BLIT_POINTER_WHEEL_R;
    lift = lift * lift * 0.45f;
    light = 1.0f - (vy - 16.0f) * 0.010f;
    for (c = 0; c < 3; ++c) {
        float v = (rgb[c] + (255 - rgb[c]) * lift) * light;
        colour[c] = v > 255 ? 255 : v < 0 ? 0 : (unsigned int)v;
    }
}

/* One sample of the art at vector point (vx, vy): its alpha and colour, and
   its band -- 0 nothing, 1 the shadow, 2 the fill, 3 the outline -- the
   same band everywhere meaning the same alpha and a colour that varies only
   linearly. */
static int blit_pointer_sample(const BlitPointerConfig *config, float vx, float vy,
                               unsigned int *a, unsigned int colour[3])
{
    static const unsigned char colours[4][3] = {
        {244, 248, 255}, {48, 59, 78}, {42, 239, 220}, {255, 192, 62}
    };
    float d = blit_pointer_distance(config->shape, vx, vy);
    int c;
    colour[0] = colour[1] = colour[2] = 0;
    *a = 0;
    if (config->shape == BLIT_POINTER_SHAPE_BUSY && config->busy_style == BLIT_POINTER_BUSY_WHEEL) {
        float dx = vx - 16, dy = vy - 16, rr = dx * dx + dy * dy, r;
        /* r by Newton from a first guess: sqrt without libm. */
        r = rr > 0 ? (rr + BLIT_POINTER_WHEEL_R * BLIT_POINTER_WHEEL_R) / (2 * BLIT_POINTER_WHEEL_R) : 0;
        if (rr > 0) { r = 0.5f * (r + rr / r); r = 0.5f * (r + rr / r); r = 0.5f * (r + rr / r); }
        if (r <= BLIT_POINTER_WHEEL_R) {
            *a = 255;
            if (r >= BLIT_POINTER_WHEEL_R - BLIT_POINTER_WHEEL_RIM) {
                colour[0] = 38; colour[1] = 40; colour[2] = 48;
                return 3;
            }
            blit_pointer_wheel_colour(blit_pointer_turns(dx, dy), r, vy, colour);
            return 2;
        }
        if (config->shadow) {
            float sx = vx - 1.4f - 16, sy = vy - 2.2f - 16;
            if (sx * sx + sy * sy <= (BLIT_POINTER_WHEEL_R + 1.0f) * (BLIT_POINTER_WHEEL_R + 1.0f)) {
                *a = 75;
                return 1;
            }
        }
        return 0;
    }
    if (d <= 0.8f) {
        *a = 255;
        for (c = 0; c < 3; ++c) {
            float light = 1.0f - vy * 0.006f;
            colour[c] = (unsigned int)(colours[config->palette][c] * light);
            if (colour[c] > 255) colour[c] = 255;
            if (d >= -1.5f) colour[c] = config->palette == 1 ? 235 : 25;
        }
        return d >= -1.5f ? 3 : 2;
    }
    if (config->shadow && blit_pointer_distance(config->shape, vx - 1.4f, vy - 2.2f) <= 1.4f) {
        *a = 75;
        return 1;
    }
    return 0;
}

/* The art for config, 4x4 samples a pixel. The settings' sizes (24..64)
   are sampled so everywhere, as they always were. A shake's larger sizes
   (ADR 0234, up to BLIT_POINTER_MAX - 8) take one sample where a pixel's
   corners, centre and quarter points are all of one band, and 4x4 only
   where they are not: built as a shake needs them, in a fraction of the
   time, with strokes there several pixels wide. */
static void blit_pointer_build(BlitPointerArt *art, BlitPointerConfig config)
{
    static signed char band[(BLIT_POINTER_MAX + 1) * (BLIT_POINTER_MAX + 1)];
    unsigned long long revision = blit_pointer_next_revision();
    int x, y, sx, sy, c, w;
    float scale;
    if (config.shape < 0 || config.shape >= BLIT_POINTER_SHAPE_COUNT || config.palette < 0 ||
        config.palette > 3 || config.size < 24 || config.size > BLIT_POINTER_MAX - 8 ||
        config.busy_style < 0 || config.busy_style > 1) return;
    scale = (float)config.size / 32.0f;
    memset(art, 0, sizeof(*art)); art->config = config; art->revision = revision;
    art->width = art->height = config.size + 6;
    art->hotspot_x = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 12 : 2) * scale + 1);
    art->hotspot_y = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 2 : 1) * scale + 1);
    w = art->width + 1;
    for (y = 0; config.size > 64 && y <= art->height; ++y) for (x = 0; x <= art->width; ++x) {
        unsigned int a, colour[3];
        band[y * w + x] = (signed char)blit_pointer_sample(&config, (x - 1) / scale, (y - 1) / scale,
                                                           &a, colour);
    }
    for (y = 0; y < art->height; ++y) for (x = 0; x < art->width; ++x) {
        unsigned int alpha = 0, rgb[3] = {0, 0, 0}, a, colour[3];
        unsigned char *out = art->rgba + (y * art->width + x) * 4;
        int centre = 0, q, uniform = 0;
        if (config.size > 64) {
            centre = blit_pointer_sample(&config, (x + 0.5f - 1) / scale, (y + 0.5f - 1) / scale,
                                         &a, colour);
            uniform = centre == band[y * w + x] && centre == band[y * w + x + 1] &&
            centre == band[(y + 1) * w + x] && centre == band[(y + 1) * w + x + 1];
            for (q = 0; uniform && q < 4; ++q) {
                unsigned int qa, qc[3];
                uniform = blit_pointer_sample(&config, (x + 0.25f + 0.5f * (q & 1) - 1) / scale,
                                              (y + 0.25f + 0.5f * (q >> 1) - 1) / scale, &qa, qc) == centre;
            }
        }
        if (uniform) {
            out[3] = (unsigned char)a;
            if (a) for (c = 0; c < 3; ++c) out[c] = (unsigned char)colour[c];
            continue;
        }
        for (sy = 0; sy < 4; ++sy) for (sx = 0; sx < 4; ++sx) {
            blit_pointer_sample(&config, (x + (sx + 0.5f) / 4 - 1) / scale,
                                (y + (sy + 0.5f) / 4 - 1) / scale, &a, colour);
            alpha += a;
            for (c = 0; c < 3; ++c) rgb[c] += colour[c] * a;
        }
        out[3] = (unsigned char)((alpha + 8) / 16);
        if (alpha) for (c = 0; c < 3; ++c) out[c] = (unsigned char)(rgb[c] / alpha);
    }
}

/* The art at `size` (24 .. the master's), made by area-averaging `master`,
   the same art built once at its largest (ADR 0234): each pixel the
   alpha-weighted mean of the master pixels it covers, so a shake's every
   size costs a resample, not a raster. Geometry as blit_pointer_build's:
   a 1 px border, width size + 6, the hotspot scaled. */
static void blit_pointer_resample(BlitPointerArt *art, const BlitPointerArt *master, int size)
{
    BlitPointerConfig config = master->config;
    unsigned long long revision = blit_pointer_next_revision();
    float r, scale;
    int x, y;
    if (size < 24 || size > master->config.size) return;
    config.size = size;
    scale = (float)size / 32.0f;
    memset(art, 0, sizeof(*art)); art->config = config; art->revision = revision;
    art->width = art->height = size + 6;
    art->hotspot_x = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 12 : 2) * scale + 1);
    art->hotspot_y = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 2 : 1) * scale + 1);
    r = (float)master->config.size / (float)size;
    for (y = 0; y < art->height; ++y) for (x = 0; x < art->width; ++x) {
        float y0 = (y - 1) * r + 1, y1 = y0 + r, x0 = (x - 1) * r + 1, x1 = x0 + r;
        float alpha = 0, rgb[3] = {0, 0, 0}, area = 0;
        unsigned char *out = art->rgba + (y * art->width + x) * 4;
        int my, mx, c;
        for (my = (int)(y0 < 0 ? 0 : y0); my < master->height && my < y1; ++my) {
            float wy = (my + 1 < y1 ? my + 1 : y1) - (my > y0 ? my : y0);
            if (wy <= 0) continue;
            for (mx = (int)(x0 < 0 ? 0 : x0); mx < master->width && mx < x1; ++mx) {
                const unsigned char *in = master->rgba + (my * master->width + mx) * 4;
                float wx = (mx + 1 < x1 ? mx + 1 : x1) - (mx > x0 ? mx : x0), wa;
                if (wx <= 0) continue;
                wa = wx * wy * in[3];
                alpha += wa;
                for (c = 0; c < 3; ++c) rgb[c] += wa * in[c];
            }
        }
        area = r * r;
        out[3] = (unsigned char)(alpha / area + 0.5f);
        if (alpha > 0) for (c = 0; c < 3; ++c) out[c] = (unsigned char)(rgb[c] / alpha + 0.5f);
    }
}

static uint32_t blit_pointer_blend(uint32_t background, const unsigned char *rgba)
{
    unsigned int a = rgba[3], inverse = 255 - a;
    return (((rgba[0] * a + ((background >> 16) & 255) * inverse + 127) / 255) << 16) |
           (((rgba[1] * a + ((background >> 8) & 255) * inverse + 127) / 255) << 8) |
           ((rgba[2] * a + (background & 255) * inverse + 127) / 255);
}
/* ADR 0235: the busy pointer at `phase` (0 .. BLIT_POINTER_PHASES - 1).
   The spokes: `base`, the ring with every spoke alike, with each spoke
   dimmed by how far it trails the lit one (a spoke every third phase), so
   successive phases go round clockwise; only alpha changes. The wheel:
   `base`'s inside coloured again with its colours turned `phase` tenths
   of 36 round, clockwise; the rim and the edge's alpha stay. Geometry,
   hotspot and config are base's, so a phase costs a pass over the pixels,
   never a raster. */
static void blit_pointer_spin(BlitPointerArt *art, const BlitPointerArt *base, int phase)
{
    static const unsigned char fade[BLIT_POINTER_SPOKES] = {
        255, 222, 192, 166, 143, 124, 108, 94, 82, 72, 64, 58
    };
    unsigned long long revision = blit_pointer_next_revision();
    float scale = (float)base->config.size / 32.0f, cx, cy;
    int x, y;
    memcpy(art, base, sizeof(*art) - sizeof(art->rgba));
    art->revision = revision;
    memcpy(art->rgba, base->rgba, (size_t)base->width * (size_t)base->height * 4);
    cx = 16.0f * scale + 1;
    cy = 16.0f * scale + 1;
    phase = ((phase % BLIT_POINTER_PHASES) + BLIT_POINTER_PHASES) % BLIT_POINTER_PHASES;
    if (base->config.busy_style == BLIT_POINTER_BUSY_WHEEL) {
        /* The inside coloured again; a pixel across the rim's inner edge
           mixes the new colour with the rim's by how much of it is inside,
           so no ring of the old colours is left. The rim itself is kept. */
        float edge = (BLIT_POINTER_WHEEL_R - BLIT_POINTER_WHEEL_RIM) * scale;
        for (y = 0; y < art->height; ++y) for (x = 0; x < art->width; ++x) {
            unsigned char *px = art->rgba + (y * art->width + x) * 4;
            float dx = x + 0.5f - cx, dy = y + 0.5f - cy, rr = dx * dx + dy * dy, r, inside;
            unsigned int colour[3];
            int c;
            if (px[3] == 0 || rr > (edge + 0.5f) * (edge + 0.5f)) continue;
            r = rr > 0 ? (rr + edge * edge) / (2 * edge) : 0;
            if (rr > 0) { r = 0.5f * (r + rr / r); r = 0.5f * (r + rr / r); r = 0.5f * (r + rr / r); }
            inside = edge + 0.5f - r;
            if (inside > 1) inside = 1;
            blit_pointer_wheel_colour(blit_pointer_turns(dx, dy) - (float)phase / BLIT_POINTER_PHASES,
                                      r / scale, (y + 0.5f - 1) / scale, colour);
            {
                static const unsigned char rim[3] = {38, 40, 48};
                for (c = 0; c < 3; ++c)
                    px[c] = (unsigned char)(colour[c] * inside + rim[c] * (1 - inside) + 0.5f);
            }
        }
        return;
    }
    phase = phase * BLIT_POINTER_SPOKES / BLIT_POINTER_PHASES;
    for (y = 0; y < art->height; ++y) for (x = 0; x < art->width; ++x) {
        unsigned char *px = art->rgba + (y * art->width + x) * 4;
        float dx = x + 0.5f - cx, dy = y + 0.5f - cy, best = -1e30f;
        int spoke = 0, behind, k;
        if (px[3] == 0) continue;
        /* The spoke the pixel lies along: the direction it is most along. */
        for (k = 0; k < BLIT_POINTER_SPOKES; ++k) {
            float along = dx * blit_pointer_spoke_dir[k][0] + dy * blit_pointer_spoke_dir[k][1];
            if (along > best) { best = along; spoke = k; }
        }
        behind = ((phase - spoke) % BLIT_POINTER_SPOKES + BLIT_POINTER_SPOKES) % BLIT_POINTER_SPOKES;
        px[3] = (unsigned char)((px[3] * fade[behind] + 127) / 255);
    }
}

#endif
