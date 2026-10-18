#ifndef BLIT_POINTER_ART_H
#define BLIT_POINTER_ART_H

/* One code-native vector source for SDL and KMS. Straight-alpha RGBA bytes;
   raster work is bounded and only performed when appearance changes. */
#include <stdint.h>
#include <string.h>

#define BLIT_POINTER_MAX 72
typedef struct {
    int shape;                 /* arrow, hand, precision */
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

static const char *const blit_pointer_shapes[] = {"arrow", "hand", "precision"};
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
    if (config.shape < 0 || config.shape > 2 || config.palette < 0 ||
        config.palette > 3 || config.size < 24 || config.size > 64) return;
    scale = (float)config.size / 32.0f;
    memset(art, 0, sizeof(*art)); art->config = config; art->revision = revision;
    art->width = art->height = config.size + 6;
    art->hotspot_x = (int)((config.shape == 2 ? 16 : config.shape == 1 ? 12 : 2) * scale + 1);
    art->hotspot_y = (int)((config.shape == 2 ? 16 : config.shape == 1 ? 2 : 1) * scale + 1);
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
