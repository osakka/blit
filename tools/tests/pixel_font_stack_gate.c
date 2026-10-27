/* ADR 0199: a pixel font through its raster. What the font has, it keeps
   (the raster declines, so the font's own path draws it); what it lacks
   comes from the glyph stack, drawn and not fallback; what nothing has is
   declined for the font's own replacement. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "PIXEL_FONT_STACK_FAIL line=%d size=%d cp=%x\n", __LINE__, size, cp); return 1; } } while (0)

static int inked(const BlitRasterGlyph *g)
{
    int x, y, ink = 0;
    for (y = 0; y < g->height; ++y) for (x = 0; x < g->width; ++x)
        ink += g->pixels[y * g->stride + x * (int)g->format +
                         (g->format == BLIT_RASTER_RGBA_PREMULTIPLIED ? 3 : 0)] != 0;
    return ink;
}

int main(int argc, char **argv)
{
    BlitFont font;
    int size = 0, kept = 0, drawn = 0, ui_kept = 0, ui_drawn = 0, count = 0, i;
    unsigned cp = 0, required[128];
    /* Icons and symbols a console font lacks, and the Blit originals. */
    static const unsigned borrowed[] = { 0xf07b, 0xf015, 0xe0b0, 0x23fb, 0x27f6, 0x1f600,
                                         0x100000, 0x100005, 0x100013, 0x100018, 0x10001a };
    FILE *input;
    if (argc != 4) return 2;
    input = fopen(argv[3], "r");
    if (!input) return 2;
    while (count < 128 && fscanf(input, "%x", &cp) == 1) required[count++] = cp;
    fclose(input);
    CHECK(count == 101);
    memset(&font, 0, sizeof(font));
    CHECK(!blit_font_load_path(&font, argv[1]));
    for (size = 8; size <= 64; ++size) {
        BlitRaster *r = blit_raster_open_pixel(&font, argv[2], (size + 1) / 2, size);
        BlitRasterGlyph g;
        CHECK(r);
        /* The font's own glyphs stay its own, letters and lines alike. */
        cp = 'A'; CHECK(blit_font_has_glyph(&font, cp) && !blit_raster_get(r, cp, 1, &g));
        cp = 0x250c; CHECK(blit_font_has_glyph(&font, cp) && !blit_raster_get(r, cp, 1, &g));
        /* Wide characters stay with Blit's wide pixel font. */
        cp = 0x6f22; CHECK(!blit_raster_get(r, cp, 2, &g));
        /* Nothing has it: the font's own replacement. */
        cp = 0x10fffd; CHECK(!blit_raster_get(r, cp, 1, &g));
        for (i = 0; i < (int)(sizeof(borrowed) / sizeof(borrowed[0])); ++i) {
            int columns;
            cp = borrowed[i];
            columns = cp == 0x1f600 ? 2 : 1;
            /* Spleen draws its own Powerline arrow; then it keeps it. */
            if (blit_font_has_glyph(&font, cp) && cp < 0x100000) {
                CHECK(!blit_raster_get(r, cp, columns, &g));
                continue;
            }
            CHECK(blit_raster_get(r, cp, columns, &g) && !g.fallback && inked(&g));
            CHECK(g.width == ((size + 1) / 2) * columns && g.height == size);
            if (cp >= 0x100000 || cp == 0x1f600)
                CHECK(g.format == BLIT_RASTER_RGBA_PREMULTIPLIED);
            ++drawn;
        }
        /* Every glyph Hibr's UI uses: the font's own, or drawn. */
        for (i = 0; i < count; ++i) {
            cp = required[i];
            if (blit_font_has_glyph(&font, cp)) {
                CHECK(!blit_raster_get(r, cp, 1, &g));
                if (size == 16) ++ui_kept;
            } else {
                CHECK(blit_raster_get(r, cp, 1, &g) && !g.fallback && inked(&g));
                if (size == 16) ++ui_drawn;
            }
        }
        ++kept;
        blit_raster_close(r);
    }
    blit_font_free(&font);
    printf("PIXEL_FONT_STACK_PASS sizes=%d borrowed=%d ui=%d_own+%d_stack wide=pixel missing=declined\n",
           kept, drawn, ui_kept, ui_drawn);
    return 0;
}
