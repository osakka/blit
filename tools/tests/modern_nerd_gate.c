#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "NERD_FAIL size=%d scalar=%x line=%d: %s\n", size, scalar, __LINE__, #test); return 1; } } while (0)
int main(int argc, char **argv)
{
    /* Exact distinct PUA scalars in Hibr wm/glyphs.hibr plus the menu pencil. */
    unsigned scalars[] = {0x270e, 0xf040, 0xf07b, 0xf15b, 0xf015, 0xf0a0,
        0xf014, 0xf1f8, 0xf021, 0xf00c, 0xf00d, 0xf005, 0xf024, 0xf09c,
        0xf004, 0xf071, 0xf08d, 0xf096, 0xf046, 0xf1ec};
    BlitFont fallback;
    int size = 0, samples = 0;
    unsigned scalar = 0, i;
    CHECK(argc == 4);
    memset(&fallback, 0, sizeof(fallback));
    CHECK(!blit_font_load_path(&fallback, argv[1]));
    for (size = 8; size <= 64; ++size) {
        int width = (size + 1) / 2;
        BlitRaster *raster = blit_raster_open(&fallback, argv[2], argv[3], width, size);
        CHECK(raster);
        for (i = 0; i < sizeof(scalars) / sizeof(*scalars); ++i) {
            BlitRasterGlyph glyph;
            int x, y, ink = 0;
            scalar = scalars[i];
            CHECK(blit_raster_get(raster, scalar, 1, &glyph));
            CHECK(!glyph.fallback && glyph.format == BLIT_RASTER_COVERAGE);
            CHECK(glyph.width == width && glyph.height == size);
            for (y = 0; y < size; ++y)
                for (x = 0; x < width; ++x)
                    ink += glyph.pixels[y * glyph.stride + x] != 0;
            CHECK(ink > 0);
            ++samples;
        }
        blit_raster_close(raster);
    }
    blit_font_free(&fallback);
    printf("MODERN_NERD_PASS sizes=57 glyphs=20 samples=%d no_fallback=all single_cell=all\n", samples);
    return 0;
}
