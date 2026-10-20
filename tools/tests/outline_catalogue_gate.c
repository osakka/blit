/* Actual production raster coverage, not cmap presence alone. */
#include "../../src/blit_raster.h"
#include "../../src/blit_emoji_presentation.h"
#include <stdio.h>
#include <string.h>

int main(int argc, char **argv)
{
    BlitFont fallback;
    FILE *catalogue;
    unsigned scalars[11000], scalar;
    int count = 0, size, i, samples = 0, failures = 0;
    if (argc != 5) return 2;
    catalogue = fopen(argv[4], "r");
    if (!catalogue) return 2;
    while (count < 11000 && fscanf(catalogue, "%x", &scalar) == 1)
        scalars[count++] = scalar;
    fclose(catalogue);
    if (count != 10386) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 2;
    for (size = 8; size <= 64; ++size) {
        int width = (size + 1) / 2;
        BlitRaster *raster = blit_raster_open(&fallback, argv[2], argv[3], width, size);
        if (!raster) return 2;
        for (i = 0; i < count; ++i) {
            BlitRasterGlyph glyph;
            int x, y, ink = 0;
            int ok = blit_raster_get(raster, scalars[i], 1, &glyph);
            if (ok) {
                for (y = 0; y < glyph.height; ++y)
                    for (x = 0; x < glyph.width; ++x)
                        ink += glyph.pixels[y * glyph.stride +
                            x * (int)glyph.format + (glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? 3 : 0)] != 0;
            }
            if (!ok || glyph.fallback || glyph.format != (blit_emoji_default(scalars[i]) ?
                    BLIT_RASTER_RGBA_PREMULTIPLIED : BLIT_RASTER_COVERAGE) ||
                glyph.width != width || glyph.height != size ||
                /* Nerd's cod-blank is intentionally empty, not missing. */
                (scalars[i] == 0xec03 ? ink != 0 : ink == 0)) {
                if (failures < 25) fprintf(stderr,
                    "CATALOGUE_FAIL size=%d cp=%x ok=%d fallback=%d ink=%d\n",
                    size, scalars[i], ok, ok ? glyph.fallback : -1, ink);
                ++failures;
            }
            ++samples;
        }
        blit_raster_close(raster);
    }
    blit_font_free(&fallback);
    printf("OUTLINE_CATALOGUE_%s face=%s sizes=57 glyphs=%d samples=%d failures=%d\n",
        failures ? "FAIL" : "PASS", argv[2], count, samples, failures);
    return failures != 0;
}
