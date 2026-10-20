/* Read-only source-raster metrics, not proof of live desktop presentation.
   Usage: font-glyph-metrics PSF OUTLINE COLOUR HEIGHT COLUMNS HEX_SCALAR */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    BlitFont fallback;
    BlitRaster *raster;
    BlitRasterGlyph glyph;
    unsigned long scalar;
    int height, columns, x, y, left, top, right = -1, bottom = -1, ink = 0;
    if (argc != 7) return 2;
    height = atoi(argv[4]); columns = atoi(argv[5]);
    scalar = strtoul(argv[6], NULL, 16);
    if (height < 8 || height > 64 || (columns != 1 && columns != 2) ||
        scalar > 0x10ffff) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 1;
    raster = blit_raster_open(&fallback, argv[2], argv[3], (height + 1) / 2, height);
    if (!raster) { blit_font_free(&fallback); return 1; }
    if (!blit_raster_get(raster, (uint32_t)scalar, columns, &glyph)) {
        blit_raster_close(raster); blit_font_free(&fallback); return 1;
    }
    left = glyph.width; top = glyph.height;
    for (y = 0; y < glyph.height; ++y)
        for (x = 0; x < glyph.width; ++x) {
            unsigned char alpha = glyph.pixels[y * glyph.stride +
                x * (int)glyph.format + (glyph.format == BLIT_RASTER_COVERAGE ? 0 : 3)];
            if (!alpha) continue;
            if (x < left) left = x;
            if (x > right) right = x;
            if (y < top) top = y;
            if (y > bottom) bottom = y;
            ++ink;
        }
    printf("{\"scalar\":%lu,\"height\":%d,\"columns\":%d,"
           "\"allocation_width\":%d,\"cell_height\":%d,\"baseline\":%d,"
           "\"format\":\"%s\",\"fallback\":%s,\"ink_pixels\":%d,\"ink_bounds\":",
        scalar, height, columns, glyph.width, glyph.height, glyph.baseline,
        glyph.format == BLIT_RASTER_COVERAGE ? "coverage" : "colour",
        glyph.fallback ? "true" : "false", ink);
    if (ink) printf("{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d}",
        left, top, right - left + 1, bottom - top + 1);
    else printf("null");
    printf("}\n");
    blit_raster_close(raster); blit_font_free(&fallback);
    return 0;
}
