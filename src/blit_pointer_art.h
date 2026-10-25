#ifndef BLIT_POINTER_ART_H
#define BLIT_POINTER_ART_H

/* One code-native vector source for the pointer. Straight-alpha RGBA bytes;
   raster work is bounded and only performed when appearance changes. */
#include <stdint.h>
#include <string.h>

#define BLIT_POINTER_MAX 72
typedef struct {
    int shape;                 /* index into blit_pointer_shapes */
    int palette;               /* pearl, obsidian, neon, amber */
    int size;                  /* nominal24..64 physical pixels */
    int shadow;
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
/* OSC 22 results that are not art: the display's own pointer, and none. */
#define BLIT_POINTER_SHAPE_DEFAULT (-2)
#define BLIT_POINTER_SHAPE_HIDDEN (-3)

/* kitty's OSC 22 names and the xterm/X11 names it accepts as aliases, mapped
   to the art drawn for them. A name Blit has no art for is absent: a query
   answers 0 for it and setting it shows the display's own pointer. */
static int blit_pointer_name_shape(const char *name)
{
    static const struct { const char *name; int shape; } names[] = {
        {"default", BLIT_POINTER_SHAPE_DEFAULT},
        {"left_ptr", BLIT_POINTER_SHAPE_DEFAULT},
        {"top_left_arrow", BLIT_POINTER_SHAPE_DEFAULT},
        {"arrow", BLIT_POINTER_SHAPE_DEFAULT},
        {"none", BLIT_POINTER_SHAPE_HIDDEN},
        {"pointer", 1}, {"grab", 1}, {"grabbing", 1}, {"hand", 1},
        {"hand1", 1}, {"hand2", 1}, {"pointing_hand", 1},
        {"openhand", 1}, {"closedhand", 1},
        {"crosshair", 2}, {"cell", 2}, {"cross", 2}, {"tcross", 2},
        {"plus", 2}, {"precision", 2},
        {"text", 3}, {"xterm", 3}, {"ibeam", 3},
        {"wait", 4}, {"progress", 4}, {"watch", 4}, {"clock", 4},
        {"left_ptr_watch", 4},
        {"move", 5}, {"all-scroll", 5}, {"fleur", 5}, {"size_all", 5},
        {"e-resize", 6}, {"w-resize", 6}, {"ew-resize", 6},
        {"col-resize", 6}, {"sb_h_double_arrow", 6},
        {"h_double_arrow", 6}, {"left_side", 6}, {"right_side", 6},
        {"size_hor", 6}, {"split_h", 6},
        {"n-resize", 7}, {"s-resize", 7}, {"ns-resize", 7},
        {"row-resize", 7}, {"sb_v_double_arrow", 7},
        {"v_double_arrow", 7}, {"top_side", 7}, {"bottom_side", 7},
        {"size_ver", 7}, {"split_v", 7},
        {"nw-resize", 8}, {"se-resize", 8}, {"nwse-resize", 8},
        {"top_left_corner", 8}, {"bottom_right_corner", 8},
        {"size_fdiag", 8},
        {"ne-resize", 9}, {"sw-resize", 9}, {"nesw-resize", 9},
        {"top_right_corner", 9}, {"bottom_left_corner", 9},
        {"size_bdiag", 9},
        {"not-allowed", 10}, {"no-drop", 10}, {"forbidden", 10},
        {"crossed_circle", 10}
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i].name)) return names[i].shape;
    return -1;
}
static const char *const blit_pointer_palettes[] = {"pearl", "obsidian", "neon", "amber"};

static BlitPointerConfig blit_pointer_default(void)
{
    BlitPointerConfig config = {0, 0, 24, 1};
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
        static const float glass[][2] = {
            {9, 5}, {23, 5}, {23, 8}, {17.5f, 16}, {23, 24}, {23, 27},
            {9, 27}, {9, 24}, {14.5f, 16}, {9, 8}
        };
        d = blit_pointer_polygon(glass, 10, x, y);
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 7, 4, 25, 4, 1.6f));
        BLIT_POINTER_MIN(d, blit_pointer_capsule(x, y, 7, 28, 25, 28, 1.6f));
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

static void blit_pointer_build(BlitPointerArt *art, BlitPointerConfig config)
{
    static const unsigned char colours[4][3] = {
        {244, 248, 255}, {48, 59, 78}, {42, 239, 220}, {255, 192, 62}
    };
    unsigned long long revision = art->revision + 1;
    int x, y, sx, sy, c;
    float scale;
    if (config.shape < 0 || config.shape >= BLIT_POINTER_SHAPE_COUNT || config.palette < 0 ||
        config.palette > 3 || config.size < 24 || config.size > 64) return;
    scale = (float)config.size / 32.0f;
    memset(art, 0, sizeof(*art)); art->config = config; art->revision = revision;
    art->width = art->height = config.size + 6;
    art->hotspot_x = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 12 : 2) * scale + 1);
    art->hotspot_y = (int)((config.shape >= 2 ? 16 : config.shape == 1 ? 2 : 1) * scale + 1);
    for (y = 0; y < art->height; ++y) for (x = 0; x < art->width; ++x) {
        unsigned int alpha = 0, rgb[3] = {0, 0, 0};
        unsigned char *out = art->rgba + (y * art->width + x) * 4;
        for (sy = 0; sy < 4; ++sy) for (sx = 0; sx < 4; ++sx) {
            float vx = (x + (sx + 0.5f) / 4 - 1) / scale;
            float vy = (y + (sy + 0.5f) / 4 - 1) / scale;
            float d = blit_pointer_distance(config.shape, vx, vy);
            unsigned int a = 0, colour[3] = {0, 0, 0};
            if (d <= 0.8f) {
                a = 255;
                for (c = 0; c < 3; ++c) {
                    float light = 1.0f - vy * 0.006f;
                    colour[c] = (unsigned int)(colours[config.palette][c] * light);
                    if (colour[c] > 255) colour[c] = 255;
                    if (d >= -1.5f) colour[c] = config.palette == 1 ? 235 : 25;
                }
            } else if (config.shadow && blit_pointer_distance(config.shape, vx - 1.4f, vy - 2.2f) <= 1.4f) {
                a = 75;
            }
            alpha += a;
            for (c = 0; c < 3; ++c) rgb[c] += colour[c] * a;
        }
        out[3] = (unsigned char)((alpha + 8) / 16);
        if (alpha) for (c = 0; c < 3; ++c) out[c] = (unsigned char)(rgb[c] / alpha);
    }
}

static uint32_t blit_pointer_blend(uint32_t background, const unsigned char *rgba)
{
    unsigned int a = rgba[3], inverse = 255 - a;
    return (((rgba[0] * a + ((background >> 16) & 255) * inverse + 127) / 255) << 16) |
           (((rgba[1] * a + ((background >> 8) & 255) * inverse + 127) / 255) << 8) |
           ((rgba[2] * a + (background & 255) * inverse + 127) / 255);
}
#endif
