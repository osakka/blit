/* ADR 0212: the controls glyphs through the production raster. Prints every
   one of the 20 at each requested cell size in the format
   draw-control-glyphs.py --reference prints, so control_glyph_gate.rb can
   compare Blit's pixels with the design's:
     "CP coverage HEX" for Chalk, one byte a pixel;
     "CP roles MAP" for Slate and the capsule, a character a pixel from
       their pen planes: K (FG), L (LINE), M (MIX), '.' none.
   It also requires, at every size, that a pen-drawn glyph's planes are
   disjoint and together equal its one-colour lookup, that Chalk has no
   planes, and that the codepoints just beyond the set (U+1002CD and on; U+1002AE-1002C4 are the frame's, ADR 0214, and the pressed states', ADR 0219,
   U+1002C5-1002C6 the hui mark and U+1002C7-1002CC the wiretap icons, colour icons)
   draw nothing of their own. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define FIRST 0x10029AU
#define LAST 0x1002ADU
/* ADR 0214's outer-edge frame is U+1002AE-1002BA and ADR 0219's pressed states U+1002BB-1002C4; window_glyph_gate holds them.
   U+1002C5-1002C6 are the hui mark and U+1002C7-1002CC the wiretap icons (on, off; two columns, then one),
   colour icons. */
#define NEXT_FREE 0x1002CDU

/* Pens are used by Slate's checkbox (the 4th to 6th glyph) and from the
   Slate switch on (the 11th): written out here, not read from the header. */
static int drawn_in_pens(unsigned cp)
{
    unsigned c = cp - FIRST;
    return (c >= 3 && c <= 5) || c >= 10;
}

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
        if (!raster) { fprintf(stderr, "CONTROL_GLYPH_FAIL no raster at %dx%d\n", w, h); return 1; }
        printf("SIZE %d %d\n", w, h);
        for (cp = FIRST; cp <= LAST; ++cp) {
            BlitRasterGlyph glyph, plane[3];
            int p;
            if (!blit_raster_get(raster, cp, 1, 0, &glyph)) {
                fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X not drawn at %dx%d\n", cp, w, h);
                return 1;
            }
            if (glyph.width != w || glyph.height != h || glyph.fallback ||
                glyph.ink_offset_x || glyph.ink_offset_y ||
                glyph.format != BLIT_RASTER_COVERAGE) {
                fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X is not a whole coverage cell at %dx%d\n", cp, w, h);
                return 1;
            }
            if (!drawn_in_pens(cp)) {
                for (p = 1; p <= 3; ++p)
                    if (blit_raster_get_window_plane(raster, cp, p, &plane[0])) {
                        fprintf(stderr, "CONTROL_GLYPH_FAIL Chalk U+%X has a plane at %dx%d\n", cp, w, h);
                        return 1;
                    }
                printf("%X coverage ", cp);
                for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
                    unsigned char v = glyph.pixels[y * glyph.stride + x];
                    if (v != 0 && v != 255) {
                        fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X is partial at %d,%d (%dx%d)\n", cp, x, y, w, h);
                        return 1;
                    }
                    printf("%02x", v);
                }
                printf("\n");
                continue;
            }
            for (p = 0; p < 3; ++p)
                if (!blit_raster_get_window_plane(raster, cp, p + 1, &plane[p]) ||
                    plane[p].width != w || plane[p].height != h ||
                    plane[p].format != BLIT_RASTER_COVERAGE) {
                    fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X has no plane %d at %dx%d\n", cp, p + 1, w, h);
                    return 1;
                }
            printf("%X roles ", cp);
            for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
                int set = 0, which = -1;
                for (p = 0; p < 3; ++p) {
                    unsigned char v = plane[p].pixels[y * plane[p].stride + x];
                    if (v != 0 && v != 255) {
                        fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X plane %d is partial at %d,%d\n", cp, p + 1, x, y);
                        return 1;
                    }
                    if (v) { ++set; which = p; }
                }
                if (set > 1 || (set != 0) != (glyph.pixels[y * glyph.stride + x] != 0)) {
                    fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X planes overlap or miss its lookup at %d,%d (%dx%d)\n",
                            cp, x, y, w, h);
                    return 1;
                }
                putchar(which < 0 ? '.' : "KLM"[which]);
            }
            printf("\n");
        }
        /* Beyond the set: nothing of their own. */
        for (cp = NEXT_FREE; cp <= NEXT_FREE + 3; ++cp) {
            BlitRasterGlyph glyph;
            if ((blit_raster_get(raster, cp, 1, 0, &glyph) && !glyph.fallback) ||
                blit_raster_get_window_plane(raster, cp, 1, &glyph)) {
                fprintf(stderr, "CONTROL_GLYPH_FAIL U+%X, past the set, draws at %dx%d\n", cp, w, h);
                return 1;
            }
        }
        blit_raster_close(raster);
    }
    blit_font_free(&font);
    return 0;
}
