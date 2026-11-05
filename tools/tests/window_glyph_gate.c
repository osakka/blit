/* ADR 0210, 0211: the window-chrome glyphs through the production raster.
   Prints every glyph at each requested cell size in the format
   draw-window-glyphs.py --reference prints, so window_glyph_gate.rb can
   compare Blit's pixels with the design's:
     "CP coverage HEX" for Chalk, one byte a pixel;
     "CP roles MAP" for Slate, a character a pixel from its pen planes:
       K (FG), L (LINE), M (MIX), '.' none.
   It also requires, at every size, that a Slate glyph's planes are
   disjoint and together equal its one-colour lookup, and that the retired
   Slate dark codepoints (U+100277-100299) draw nothing of their own. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST 0x100231U
#define SLATE 0x100254U
#define LAST 0x100276U
/* ADR 0214: the outer-edge frame, nine glyphs; the last two (Slate's title
   ends) are drawn in pens, the first seven are coverage. */
#define FRAME_FIRST 0x1002AEU
#define FRAME_LAST 0x1002BAU /* 0.3.45: four plain title ends, U+1002B7-1002BA */
#define FRAME_SLATE 0x1002B5U /* Slate's title ends, U+1002B5-B6, and the plain ones, U+1002B9-BA */

int main(int argc, char **argv)
{
    BlitFont font;
    int arg;
    if (argc < 6 || argc % 2 != 0) return 2;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1])) return 2;
    for (arg = 4; arg + 1 < argc; arg += 2) {
        int w = atoi(argv[arg]), h = atoi(argv[arg + 1]), x, y;
        unsigned cp, n;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], w, h);
        if (!raster) { fprintf(stderr, "WINDOW_GLYPH_FAIL no raster at %dx%d\n", w, h); return 1; }
        printf("SIZE %d %d\n", w, h);
        for (n = 0; n < (LAST - FIRST + 1) + (FRAME_LAST - FRAME_FIRST + 1); ++n) {
            BlitRasterGlyph glyph, plane[3];
            int pen_drawn;
            int p;
            cp = n <= LAST - FIRST ? FIRST + n : FRAME_FIRST + (n - (LAST - FIRST + 1));
            pen_drawn = (cp >= SLATE && cp <= LAST) || cp == 0x1002B5U || cp == 0x1002B6U || cp == 0x1002B9U || cp == 0x1002BAU;
            if (!blit_raster_get(raster, cp, 1, &glyph)) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X not drawn at %dx%d\n", cp, w, h);
                return 1;
            }
            if (glyph.width != w || glyph.height != h || glyph.fallback ||
                glyph.ink_offset_x || glyph.ink_offset_y ||
                glyph.format != BLIT_RASTER_COVERAGE) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X is not a whole coverage cell at %dx%d\n", cp, w, h);
                return 1;
            }
            if (!pen_drawn) {
                /* Coverage in the foreground alone: no pen planes. */
                if (blit_raster_get_window_plane(raster, cp, 1, &plane[0])) {
                    fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X is one colour but has a plane\n", cp);
                    return 1;
                }
                printf("%X coverage ", cp);
                for (y = 0; y < h; ++y) for (x = 0; x < w; ++x)
                    printf("%02x", glyph.pixels[y * glyph.stride + x]);
                printf("\n");
                continue;
            }
            for (p = 0; p < 3; ++p)
                if (!blit_raster_get_window_plane(raster, cp, p + 1, &plane[p]) ||
                    plane[p].width != w || plane[p].height != h ||
                    plane[p].format != BLIT_RASTER_COVERAGE) {
                    fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X has no plane %d at %dx%d\n", cp, p + 1, w, h);
                    return 1;
                }
            printf("%X roles ", cp);
            for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
                int set = 0, which = -1;
                for (p = 0; p < 3; ++p) {
                    unsigned char v = plane[p].pixels[y * plane[p].stride + x];
                    if (v != 0 && v != 255) {
                        fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X plane %d is partial at %d,%d\n", cp, p + 1, x, y);
                        return 1;
                    }
                    if (v) { ++set; which = p; }
                }
                if (set > 1 || (set != 0) != (glyph.pixels[y * glyph.stride + x] != 0)) {
                    fprintf(stderr, "WINDOW_GLYPH_FAIL U+%X planes overlap or miss its lookup at %d,%d (%dx%d)\n",
                            cp, x, y, w, h);
                    return 1;
                }
                putchar(which < 0 ? '.' : "KLM"[which]);
            }
            printf("\n");
        }
        /* Retired: nothing of the window glyphs' own; at most the fallback. */
        for (cp = 0x100277U; cp <= 0x100299U; ++cp) {
            BlitRasterGlyph glyph;
            if (blit_raster_get(raster, cp, 1, &glyph) && !glyph.fallback) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL retired U+%X still draws at %dx%d\n", cp, w, h);
                return 1;
            }
            if (blit_raster_get_window_plane(raster, cp, 1, &glyph)) {
                fprintf(stderr, "WINDOW_GLYPH_FAIL retired U+%X still has a plane\n", cp);
                return 1;
            }
        }
        blit_raster_close(raster);
    }
    blit_font_free(&font);
    return 0;
}
