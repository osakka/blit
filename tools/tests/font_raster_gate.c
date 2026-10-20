#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

int main(int argc, char **argv)
{
    BlitFont fallback;
    int i;
    CHECK(argc == 4);
    {
        unsigned char alpha[] = {0, 128, 255};
        unsigned char colour[] = {64, 32, 16, 128};
        BlitRasterGlyph glyph;
        memset(&glyph, 0, sizeof(glyph));
        glyph.pixels = alpha; glyph.width = 3; glyph.height = 1;
        glyph.stride = 3; glyph.format = BLIT_RASTER_COVERAGE;
        CHECK(blit_raster_blend(&glyph, 0, 0, 0xffffff, 0x123456) == 0x123456);
        CHECK(blit_raster_blend(&glyph, 1, 0, 0xffffff, 0) == 0x808080);
        CHECK(blit_raster_blend(&glyph, 2, 0, 0x123456, 0xffffff) == 0x123456);
        CHECK(blit_raster_blend(&glyph, 1, 0, 0, 0xffffff) == 0x7f7f7f);
        CHECK(blit_raster_blend(&glyph, -1, 0, 0, 0xabcdef) == 0xabcdef);
        glyph.pixels = colour; glyph.width = 1; glyph.stride = 4;
        glyph.format = BLIT_RASTER_RGBA_PREMULTIPLIED;
        CHECK(blit_raster_blend(&glyph, 0, 0, 0, 0) == 0x402010);
        CHECK(blit_raster_blend(&glyph, 0, 0, 0xffffff, 0) == 0x402010);
        CHECK(blit_raster_blend(&glyph, 0, 0, 0, 0xffffff) == 0xbf9f8f);
    }
    memset(&fallback, 0, sizeof(fallback));
    CHECK(blit_font_load_path(&fallback, argv[1]) == 0);
    {
        /* At22px the fitted Modern text em is18px, not the22px cell.
           Emoji must use that typographic bound without changing advance. */
        BlitRaster *raster = blit_raster_open(&fallback, argv[2], argv[3], 11, 22);
        BlitRasterGlyph bell;
        int x, y, top = 22, bottom = -1;
        CHECK(raster && blit_raster_get(raster, 0x1f514, 2, &bell));
        CHECK(bell.width == 22 && bell.height == 22 && !bell.fallback);
        for (y = 0; y < bell.height; ++y)
            for (x = 0; x < bell.width; ++x)
                if (bell.pixels[y * bell.stride + x * 4 + 3]) {
                    if (y < top) top = y;
                    if (y > bottom) bottom = y;
                }
        CHECK(bottom >= top && bottom - top + 1 <= 18);
        blit_raster_close(raster);
    }
    for (i = 8; i <= 64; ++i) {
        int size = i, width = (size + 1) / 2;
        BlitRaster *raster = blit_raster_open(&fallback, argv[2], argv[3], width, size);
        BlitRasterGlyph a, again, block, emoji;
        unsigned long count;
        int x, y, partial = 0, coloured = 0;
        CHECK(raster);
        CHECK(blit_raster_get(raster, 'A', 1, &a));
        CHECK(a.width == width && a.height == size && a.stride == width);
        CHECK(a.format == BLIT_RASTER_COVERAGE && !a.fallback);
        CHECK(a.baseline > 0 && a.baseline <= size);
        for (y = 0; y < size; ++y)
            for (x = 0; x < width; ++x)
                if (a.pixels[y * a.stride + x] > 0 && a.pixels[y * a.stride + x] < 255) ++partial;
        CHECK(partial > 0);
        count = blit_raster_render_count(raster);
        CHECK(blit_raster_get(raster, 'A', 1, &again));
        CHECK(a.pixels == again.pixels && blit_raster_render_count(raster) == count);
        CHECK(blit_raster_get(raster, 0x2588, 1, &block));
        CHECK(block.fallback && block.format == BLIT_RASTER_COVERAGE);
        for (y = 0; y < size; ++y)
            for (x = 0; x < width; ++x) CHECK(block.pixels[y * block.stride + x] == 255);
        CHECK(blit_raster_get(raster, 0x1f600, 2, &emoji));
        CHECK(emoji.width == width * 2 && emoji.height == size);
        CHECK(emoji.format == BLIT_RASTER_RGBA_PREMULTIPLIED && !emoji.fallback);
        for (y = 0; y < size; ++y)
            for (x = 0; x < emoji.width; ++x) {
                const unsigned char *p = emoji.pixels + y * emoji.stride + x * 4;
                CHECK(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
                if (p[0] != p[1] || p[1] != p[2]) ++coloured;
            }
        CHECK(coloured > 0);
        {
            uint32_t invisible[] = {0x200c, 0x200d, 0xfe0e, 0xfe0f, 0xe0100};
            int mark, byte;
            for (mark = 0; mark < 5; ++mark) {
                CHECK(blit_raster_get(raster, invisible[mark], 1, &again));
                CHECK(again.format == BLIT_RASTER_COVERAGE && !again.fallback);
                for (byte = 0; byte < again.stride * again.height; ++byte)
                    CHECK(again.pixels[byte] == 0);
            }
        }
        CHECK(!blit_raster_get(raster, 0xd800, 1, &again));
        CHECK(!blit_raster_get(raster, 0x110000, 1, &again));
        CHECK(!blit_raster_get(raster, 'A', 3, &again));
        CHECK(!blit_raster_open(&fallback, "/no/such/font.ttf", NULL, width, size));
        CHECK(blit_raster_get(raster, 'A', 1, &again));
        /* Pressure all cache buckets, then verify bounded memory and recovery. */
        for (x = 32; x < 1024; ++x) CHECK(blit_raster_get(raster, (uint32_t)x, 1, &again));
        CHECK(blit_raster_cached_bytes(raster) <= 4U * 1024U * 1024U);
        CHECK(blit_raster_get(raster, 'A', 1, &again) && !again.fallback);
        printf("size=%d cell=%dx%d partial=%d coloured=%d cached_bytes=%lu PASS\n",
            size, width, size, partial, coloured, (unsigned long)blit_raster_cached_bytes(raster));
        blit_raster_close(raster);
    }
    {
        BlitRaster *retro = blit_raster_open(&fallback, NULL, NULL, 12, 23);
        BlitRasterGlyph glyph;
        CHECK(retro && blit_raster_get(retro, 'A', 1, &glyph));
        CHECK(glyph.fallback && glyph.format == BLIT_RASTER_COVERAGE);
        for (i = 0; i < glyph.stride * glyph.height; ++i)
            CHECK(glyph.pixels[i] == 0 || glyph.pixels[i] == 255);
        blit_raster_close(retro);
    }
    CHECK(!blit_raster_open(&fallback, NULL, NULL, 32, 7));
    CHECK(!blit_raster_open(&fallback, NULL, NULL, 32, 65));
    blit_font_free(&fallback);
    puts("FONT_RASTER_PASS scope=shared_module_not_backend_acceptance");
    return 0;
}
