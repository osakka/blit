/* blit#140: a title bar's rounded top corners, U+100007 (left) and
   U+100008 (right), as cell geometry in the shared raster: filled edge to
   edge except a quarter-circle cut at the top corner, radius the cell's
   shorter side, so bar cells join without a seam at every size. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(t) do { if (!(t)) { fprintf(stderr,"BAR_CORNER_FAIL size=%d line=%d: %s\n",size,__LINE__,#t); return 1; } } while(0)
int main(int argc, char **argv)
{
    BlitFont font;
    int size = 0, x, y, cases = 0, min_edge = 255;
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font));
    CHECK(!blit_font_load_path(&font, argv[1]));
    for (size = 8; size <= 64; ++size) {
        int cw = (size + 1) / 2;
        BlitRaster *r = blit_raster_open(&font, argv[2], argv[3], cw, size);
        BlitRasterGlyph left, right, again;
        unsigned char saved[64 * 64];
        unsigned long count;
        int partial = 0, radius = cw < size ? cw : size;
        CHECK(r);
        CHECK(blit_raster_get(r, 0x100007, 1, &left));
        CHECK(left.format == BLIT_RASTER_COVERAGE && !left.fallback);
        CHECK(left.width == cw && left.height == size);
        CHECK(left.ink_offset_x == 0 && left.ink_offset_y == 0);
        memcpy(saved, left.pixels, (size_t)left.stride * left.height);
        CHECK(blit_raster_get(r, 0x100008, 1, &right));
        CHECK(right.format == BLIT_RASTER_COVERAGE && !right.fallback);
        for (y = 0; y < size; ++y)
            for (x = 0; x < cw; ++x) {
                int a = saved[y * left.stride + x];
                /* Mirror images of each other. */
                CHECK(a == right.pixels[y * right.stride + cw - 1 - x]);
                /* Below the arc, and the whole right column of the left
                   corner, are solid: no seam against the bar's next cell. */
                if (y >= radius || (x == cw - 1 && y > 0)) CHECK(a == 255);
                /* The arc meets the inner edge at the top row: its last
                   pixel there is a sliver short of full. */
                if (x == cw - 1 && y == 0) {
                    CHECK(a >= 240);
                    if (a < min_edge) min_edge = a;
                }
                partial += a > 0 && a < 255;
            }
        /* The corner itself is cut, the arc antialiased. */
        CHECK(saved[0] == 0);
        CHECK(partial > 0);
        /* A narrow cell's arc reaches its far edge only at the top row. */
        CHECK(saved[(radius - 1) * left.stride] > 0);
        count = blit_raster_render_count(r);
        CHECK(blit_raster_get(r, 0x100007, 1, &again));
        CHECK(blit_raster_render_count(r) == count);
        CHECK(!memcmp(saved, again.pixels, (size_t)again.stride * again.height));
        blit_raster_close(r);
        cases += 2;
    }
    blit_font_free(&font);
    printf("BAR_CORNER_PASS sizes=57 scalars=U+100007,U+100008 cases=%d below_arc=solid "
           "inner_edge=solid top_edge_min=%d mirror=exact arc=antialiased cache=stable\n",
           cases, min_edge);
    return 0;
}
