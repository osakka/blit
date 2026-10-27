/* Original glyphs through the production raster, not cmap presence alone. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CUSTOM_ICON_FAIL line=%d size=%d cp=%x\n", __LINE__, size, cp); return 1; } } while (0)

int main(int argc, char **argv)
{
    BlitFont font;
    int size = 0, samples = 0;
    unsigned cp = 0, required[128];
    int required_count = 0, ui_samples = 0, script_samples = 0;
    /* ADR 0195: what the glyph stack adds to any font -- an overhanging
       long arrow, and scripts no shipped text font has (Cyrillic,
       Hebrew, Thai, Georgian, Ethiopic, CJK) -- drawn, never fallback. */
    static const unsigned scripts[] = { 0x27f6, 0x0416, 0x05e9, 0x0e01, 0x10d0, 0x1200, 0x6f22, 0x5b57 };
    FILE *input;
    if (argc != 5) return 2;
    input = fopen(argv[4], "r");
    if (!input) return 2;
    while (required_count < 128 && fscanf(input, "%x", &cp) == 1)
        required[required_count++] = cp;
    fclose(input);
    CHECK(required_count == 101);
    memset(&font, 0, sizeof(font));
    CHECK(!blit_font_load_path(&font, argv[1]));
    for (size = 8; size <= 64; ++size) {
        BlitRaster *r = blit_raster_open(&font, argv[2], argv[3], (size + 1) / 2, size);
        unsigned hashes[7];
        int index;
        CHECK(r);
        for (index = 0; index < required_count; ++index) {
            int span;
            cp = required[index];
            for (span = 1; span <= 2; ++span) {
                BlitRasterGlyph g;
                int x, y, ink = 0;
                CHECK(blit_raster_get(r, cp, span, &g));
                CHECK(!g.fallback || blit_font_has_glyph(&font, cp));
                for (y = 0; y < g.height; ++y) for (x = 0; x < g.width; ++x)
                    ink += g.pixels[y * g.stride + x * (int)g.format +
                        (g.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? 3 : 0)] != 0;
                CHECK(ink);
                ++ui_samples;
            }
        }
        for (index = 0; index < (int)(sizeof(scripts) / sizeof(scripts[0])); ++index) {
            BlitRasterGlyph g;
            int x, y, ink = 0, span = scripts[index] >= 0x3000 ? 2 : 1;
            cp = scripts[index];
            CHECK(blit_raster_get(r, cp, span, &g));
            CHECK(!g.fallback);
            for (y = 0; y < g.height; ++y) for (x = 0; x < g.width; ++x)
                ink += g.pixels[y * g.stride + x * (int)g.format +
                    (g.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? 3 : 0)] != 0;
            CHECK(ink);
            ++script_samples;
        }
        for (cp = 0x100000; cp <= 0x100006; ++cp) {
            BlitRasterGlyph g, again;
            unsigned hash = 2166136261U;
            int x, y, ink = 0, colour = 0, partial = 0;
            unsigned long renders;
            CHECK(blit_raster_get(r, cp, 1, &g));
            CHECK(!g.fallback && g.format == BLIT_RASTER_RGBA_PREMULTIPLIED);
            CHECK(g.width == (size + 1) / 2 && g.height == size);
            for (y = 0; y < g.height; ++y) for (x = 0; x < g.width; ++x) {
                const unsigned char *p = g.pixels + y * g.stride + x * 4;
                int c;
                CHECK(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
                ink += p[3] > 0;
                colour += p[3] && (p[0] != p[1] || p[1] != p[2]);
                partial += p[3] > 0 && p[3] < 255;
                for (c = 0; c < 4; ++c) hash = (hash ^ p[c]) * 16777619U;
                CHECK(blit_raster_blend(&g, x, y, 0xff0000, 0x123456) ==
                      blit_raster_blend(&g, x, y, 0x00ff00, 0x123456));
            }
            CHECK(ink && colour && partial);
            hashes[cp - 0x100000] = hash;
            renders = blit_raster_render_count(r);
            CHECK(blit_raster_get(r, cp, 1, &again));
            CHECK(renders == blit_raster_render_count(r));
            ++samples;
        }
        for (index = 2; index < 6; ++index) {
            int other;
            for (other = index + 1; other < 6; ++other) CHECK(hashes[index] != hashes[other]);
        }
        {
            BlitRasterGlyph g;
            cp = 0x270e;
            CHECK(blit_raster_get(r, cp, 1, &g));
            CHECK(!g.fallback && g.format == BLIT_RASTER_COVERAGE);
            for (index = 0; index < 2; ++index) {
                cp = index ? 0x21cc : 0x16d2;
                CHECK(blit_raster_get(r, cp, 1, &g));
                CHECK(!g.fallback && g.format == BLIT_RASTER_COVERAGE);
            }
        }
        CHECK(blit_raster_cached_bytes(r) <= 4U * 1024U * 1024U);
        blit_raster_close(r);
    }
    blit_font_free(&font);
    printf("CUSTOM_ICON_PASS samples=%d sizes=57 icons=7 distinct_wifi=4 colour=premultiplied pen_alias=themed cache=bounded ui_scalars=%d ui_span_cases=%d scripts=%d no_replacement=verified\n", samples, required_count, ui_samples, script_samples);
    return 0;
}
