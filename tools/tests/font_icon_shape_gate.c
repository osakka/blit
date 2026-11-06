/* ADR 0220: a monochrome icon says its state by shape, not by colour. The
   state families of Blit's menu-bar icons (battery levels, charging, mains,
   cellular bars, Wi-Fi fan, volume waves) are drawn in the black pen and compared as
   masks, binarised at 50% alpha: for every pair in a family
   |A xor B| / |A or B| must be at least 15% at 22 px (a cell of 11x22) and
   10% at 16 px (8x16). The table is printed, so the margin is visible.
   A two-column icon (ADR 0197) is compared as its whole badge, its left and
   right columns side by side: wiretap on (a drop under the spout) against
   wiretap off (no drop). The one-column wiretap-1 pair (on, off) is
   compared as one cell, as the battery and Wi-Fi icons are; its drop is a
   level, clear when off, so both states are fitted in one box and only the
   drop differs. The Wi-Fi fan (0.3.54): wifi-signal-1..4 pairwise, and
   wifi-off (the cross alone in monochrome) against each level. The bars
   are measured under their live names, cellular-1..4 (wifi-1..4 are the
   same drawing, deprecated). */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define MIN22 0.15
#define MIN16 0.10

typedef struct { const char *name; uint32_t cp; } Icon;
static const Icon kIcons[] = {
    {"battery-0", 0x100009}, {"battery-1", 0x10000a}, {"battery-2", 0x10000b},
    {"battery-3", 0x10000c}, {"battery-4", 0x10000d},
    {"battery-0-charging", 0x10000e}, {"battery-1-charging", 0x10000f},
    {"battery-2-charging", 0x100010}, {"battery-3-charging", 0x100011},
    {"battery-4-charging", 0x100012}, {"battery-ac", 0x100013},
    {"volume-0", 0x100014}, {"volume-1", 0x100015}, {"volume-2", 0x100016},
    {"volume-3", 0x100017},
    {"cellular-1", 0x1002d6}, {"cellular-2", 0x1002d7}, {"cellular-3", 0x1002d8}, {"cellular-4", 0x1002d9},
    {"wiretap", 0x1002c7}, {"wiretap-off", 0x1002c9},
    {"wiretap-1", 0x1002cb}, {"wiretap-1-off", 0x1002cc},
    {"wifi-signal-1", 0x1002d1}, {"wifi-signal-2", 0x1002d2}, {"wifi-signal-3", 0x1002d3},
    {"wifi-signal-4", 0x1002d4}, {"wifi-off", 0x1002d5},
};
#define NICONS ((int)(sizeof(kIcons) / sizeof(kIcons[0])))
/* A two-column icon's entry is its left codepoint; its right is the next. */
enum { B0 = 0, C0 = 5, AC = 10, V0 = 11, W1 = 15, TAP = 19, TAP1 = 21, WS1 = 23, WOFF = 27 };

typedef struct { int w, h; unsigned char *m; } Mask;

static int mask_of(const BlitFont *font, const char *outline, const char *colour,
                   int size, uint32_t cp, int columns, Mask *out)
{
    BlitRaster *raster = blit_raster_open(font, outline, colour, (size + 1) / 2, size);
    BlitRasterGlyph g;
    int x, y, c;
    if (!raster) return 0;
    out->m = NULL;
    for (c = 0; c < columns; ++c) {
        if (!blit_raster_get(raster, cp + (uint32_t)c, 1, BLIT_RASTER_ICON_BLACK, &g) || g.format != BLIT_RASTER_RGBA_PREMULTIPLIED) return 0;
        if (!out->m) {
            out->w = g.width * columns; out->h = g.height;
            out->m = calloc((size_t)out->w, (size_t)g.height);
            if (!out->m) return 0;
        }
        for (y = 0; y < g.height; ++y)
            for (x = 0; x < g.width; ++x)
                out->m[y * out->w + c * g.width + x] = g.pixels[y * g.stride + x * 4 + 3] >= 128;
    }
    blit_raster_close(raster);
    return 1;
}

static double diff(const Mask *a, const Mask *b)
{
    int i, n = a->w * a->h, either = 0, differ = 0;
    for (i = 0; i < n; ++i) {
        either += a->m[i] || b->m[i];
        differ += a->m[i] != b->m[i];
    }
    return either ? (double)differ / either : 0.0;
}

static int failures;

static void pair(const Mask *m, int a, int b, double min, const char *family)
{
    double d = diff(&m[a], &m[b]);
    int bad = d < min;
    printf("  %-11s %-20s %-20s %5.1f%%%s\n", family, kIcons[a].name, kIcons[b].name,
           d * 100.0, bad ? "  FAIL" : "");
    failures += bad;
}

int main(int argc, char **argv)
{
    static const int kSizes[] = { 22, 16 };
    BlitFont font;
    int s, i, j;
    if (argc != 4) return 2;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1]) != 0) return 2;
    for (s = 0; s < 2; ++s) {
        double min = kSizes[s] == 22 ? MIN22 : MIN16;
        Mask m[NICONS];
        for (i = 0; i < NICONS; ++i)
            if (!mask_of(&font, argv[2], argv[3], kSizes[s], kIcons[i].cp, i == TAP || i == TAP + 1 ? 2 : 1, &m[i])) {
                fprintf(stderr, "FAIL: %s did not render\n", kIcons[i].name);
                return 1;
            }
        printf("%d px, cell %dx%d, minimum %.0f%%\n", kSizes[s], m[0].w, m[0].h, min * 100);
        for (i = B0; i < B0 + 5; ++i) for (j = i + 1; j < B0 + 5; ++j) pair(m, i, j, min, "battery");
        for (i = C0; i < C0 + 5; ++i) for (j = i + 1; j < C0 + 5; ++j) pair(m, i, j, min, "charging");
        for (i = 0; i < 5; ++i) pair(m, B0 + i, C0 + i, min, "drain/chg");
        for (i = B0; i < B0 + 10; ++i) pair(m, AC, i, min, "ac");
        for (i = W1; i < W1 + 4; ++i) for (j = i + 1; j < W1 + 4; ++j) pair(m, i, j, min, "cellular");
        for (i = V0; i < V0 + 4; ++i) for (j = i + 1; j < V0 + 4; ++j) pair(m, i, j, min, "volume");
        pair(m, TAP, TAP + 1, min, "wiretap");
        pair(m, TAP1, TAP1 + 1, min, "wiretap-1");
        for (i = WS1; i < WS1 + 4; ++i) for (j = i + 1; j < WS1 + 4; ++j) pair(m, i, j, min, "wifi-signal");
        for (i = WS1; i < WS1 + 4; ++i) pair(m, WOFF, i, min, "wifi-signal");
        for (i = 0; i < NICONS; ++i) free(m[i].m);
    }
    if (failures) {
        fprintf(stderr, "font icon shape gate: %d pair(s) below the minimum\n", failures);
        return 1;
    }
    puts("font icon shape gate: ok");
    return 0;
}
