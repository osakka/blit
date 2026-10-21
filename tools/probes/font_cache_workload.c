/* Production raster-cache workload, not a display FPS benchmark. */
#define _POSIX_C_SOURCE 200809L
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <time.h>

static uint64_t signature(const BlitRasterGlyph *g)
{
    uint64_t h = 1469598103934665603ULL;
    size_t i, bytes = (size_t)g->stride * g->height;
    int metrics[] = {g->width, g->height, g->stride, g->baseline,
        g->ink_offset_x, g->ink_offset_y, g->format, g->fallback};
    for (i = 0; i < sizeof(metrics) / sizeof(metrics[0]); ++i)
        h = (h ^ (uint32_t)metrics[i]) * 1099511628211ULL;
    for (i = 0; i < bytes; ++i)
        h = (h ^ g->pixels[i]) * 1099511628211ULL;
    return h;
}

int main(int argc, char **argv)
{
    BlitFont font = {0};
    BlitRaster *r;
    BlitRasterGlyph g;
    uint32_t keys[512];
    uint64_t expected[512];
    unsigned n = 0, i, pass, scalar, count = 200;
    unsigned long before, renders;
    struct timespec start, stop;
    double ms;
    if (argc != 5) return 2;
    while (scanf("%x", &scalar) == 1) {
        if (n == 512) return 2;
        keys[n++] = scalar;
    }
    if (!n || blit_font_load_path(&font, argv[1]) != 0) return 2;
    r = blit_raster_open(&font, argv[2], argv[3], 11, 22);
    if (!r || !blit_raster_set_arabic(r, argv[4])) return 2;
    for (i = 0; i < n; ++i) {
        if (!blit_raster_get(r, keys[i], 1, &g)) return 3;
        expected[i] = signature(&g);
    }
    before = blit_raster_render_count(r);
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &start);
    for (pass = 0; pass < count; ++pass)
        for (i = 0; i < n; ++i) {
            if (!blit_raster_get(r, keys[i], 1, &g) ||
                expected[i] != signature(&g)) return 4;
        }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID, &stop);
    renders = blit_raster_render_count(r) - before;
    ms = (stop.tv_sec - start.tv_sec) * 1000.0 +
         (stop.tv_nsec - start.tv_nsec) / 1000000.0;
    printf("{\"keys\":%u,\"passes\":%u,\"lookups\":%u,"
           "\"warm_rasterizations\":%lu,\"cached_bytes\":%lu,"
           "\"process_cpu_ms_including_pixel_checks\":%.3f,"
           "\"pixels_and_metrics_unchanged\":true}\n",
           n, count, n * count, renders,
           (unsigned long)blit_raster_cached_bytes(r), ms);
    blit_raster_close(r);
    blit_font_free(&font);
    return 0;
}
