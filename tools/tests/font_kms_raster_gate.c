#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)
int main(int argc, char **argv)
{
    BlitFont font;
    KmsOutput output;
    uint32_t pixels[160 * 96];
    int size, samples = 0;
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font)); memset(&output, 0, sizeof(output));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    output.mode.hdisplay = 160; output.mode.vdisplay = 96;
    output.back.map = pixels; output.back.pitch = 160 * 4;
    for (size = 8; size <= 64; ++size) {
        uint32_t scalars[] = {'A', 'g', 0x2588, 0x2500, 0x1f600, '9', 0x25c6, 0xfe0f, 0x200d,
            0x270e, 0xf040, 0xf015, 0xf07b, 0xf014, 0xf021, 0xf00c, 0xf071};
        unsigned index;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], (size + 1) / 2, size);
        CHECK(raster); blit_raster_attach(&font, raster);
        for (index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
            BlitRasterGlyph glyph;
            int columns = scalars[index] == 0x1f600 ? 2 : 1;
            int x, y;
            CHECK(font.raster_get(font.raster_state, scalars[index], columns, &glyph));
            for (x = 0; x < 160 * 96; ++x) pixels[x] = 0x123456;
            draw_glyph(&output, &font, scalars[index], 8, 8, glyph.width,
                       glyph.height, 0xabcdef, 0, 0);
            for (y = 0; y < 96; ++y)
                for (x = 0; x < 160; ++x) {
                    uint32_t expected = x >= 8 && y >= 8 && x < 8 + glyph.width && y < 8 + glyph.height ?
                        blit_raster_blend(&glyph, x - 8, y - 8, 0xabcdef, 0x123456) : 0x123456;
                    CHECK(pixels[y * 160 + x] == expected);
                }
            ++samples;
        }
    }
    blit_font_free(&font);
    printf("FONT_KMS_RASTER_PASS sizes=57 glyph_samples=%d tolerance=0 guard_pixels=exact\n", samples);
    return 0;
}
