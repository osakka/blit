/* ADR 0210: the window-chrome glyph sets through the production raster. Prints
   every one of the 105 at each requested cell size as
   "CP FORMAT HEX", the format draw-window-glyphs.py --reference prints, so
   window_glyph_gate.rb can compare Blit's pixels with the design's. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

int main(int argc, char **argv)
{
    BlitFont font;
    int arg;
    if (argc < 6 || argc % 2 != 0) return 2;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1])) return 2;
    for (arg = 4; arg + 1 < argc; arg += 2) {
        int w = atoi(argv[arg]), h = atoi(argv[arg + 1]), x, y;
        unsigned cp;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], w, h);
        if (!raster) { fprintf(stderr, "WINDOW_GLYPH_FAIL no raster at %dx%d\n", w, h); return 1; }
        printf("SIZE %d %d\n", w, h);
        for (cp = 0x100231; cp <= 0x100299; ++cp) {
            BlitRasterGlyph glyph;
            int bytes;
            if (!blit_raster_get(raster, cp, 1, &glyph)) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X not drawn at %dx%d\n", cp, w, h);
                return 1;
            }
            if (glyph.width != w || glyph.height != h || glyph.fallback ||
                glyph.ink_offset_x || glyph.ink_offset_y) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X is not a whole cell at %dx%d\n", cp, w, h);
                return 1;
            }
            bytes = glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? 4 : 1;
            printf("%X %s ", cp, bytes == 4 ? "rgba" : "coverage");
            for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
                const unsigned char *p = glyph.pixels + y * glyph.stride + x * bytes;
                if (bytes == 1) printf("%02x", p[0]);
                else printf("%02x%02x%02x%02x", p[0], p[1], p[2], p[3]);
            }
            printf("\n");
        }
        blit_raster_close(raster);
    }
    blit_font_free(&font);
    return 0;
}
