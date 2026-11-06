/* The raster's coverage for full-cell glyphs, for glyph_edge_sweep_gate.rb:
   "CP HEX" a line, one byte a pixel, at the cell WxH, for each codepoint on
   stdin. Run as: gate PSF OUTLINE COLOUR W H < codepoints. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    BlitFont font;
    BlitRaster *raster;
    unsigned cp;
    int w, h;
    if (argc < 6) return 2;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1])) return 2;
    w = atoi(argv[4]); h = atoi(argv[5]);
    raster = blit_raster_open(&font, argv[2], argv[3], w, h);
    if (!raster) return 3;
    while (scanf("%x", &cp) == 1) {
        BlitRasterGlyph glyph;
        int x, y;
        if (!blit_raster_get(raster, cp, 1, 0, &glyph) || glyph.format != BLIT_RASTER_COVERAGE ||
            glyph.width != w || glyph.height != h) { printf("%X none\n", cp); continue; }
        printf("%X ", cp);
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) printf("%02x", glyph.pixels[y * glyph.stride + x]);
        printf("\n");
    }
    blit_raster_close(raster);
    return 0;
}
