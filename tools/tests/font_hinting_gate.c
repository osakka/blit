/* ADR 0218: fonts.hinting at the raster. The default is what text always
   was, byte for byte; none, light and full differ for text glyphs at small
   sizes; a fitted icon does not follow the setting. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

#define FNV_BASIS 14695981039346656037ULL
/* The hash of the default rasters of kText at kSizes, recorded from the
   raster as it was before the setting existed. */
#define DEFAULT_TEXT_HASH 0x673063c60970474cULL
static const uint32_t kText[] = { 'a', 'e', 'g', 'S', '@', 'W', 'k', 's' };
static const uint32_t kIcon = 0xf07b;
static const int kSizes[] = { 10, 12, 14, 16, 22 };
#define NSIZES ((int)(sizeof(kSizes) / sizeof(kSizes[0])))
#define NTEXT ((int)(sizeof(kText) / sizeof(kText[0])))

static unsigned long long fnv(unsigned long long h, const BlitRasterGlyph *g)
{
    int y, x;
    for (y = 0; y < g->height; ++y)
        for (x = 0; x < g->width; ++x) {
            h ^= g->pixels[y * g->stride + x];
            h *= 1099511628211ULL;
        }
    return h;
}

/* mode < 0: the raster as opened, never told. */
static int render(const BlitFont *font, const char *outline, const char *colour,
                  int size, int mode, uint32_t scalar, unsigned long long *hash)
{
    BlitRaster *raster = blit_raster_open(font, outline, colour, (size + 1) / 2, size);
    BlitRasterGlyph glyph;
    if (!raster) return 0;
#ifndef BASELINE
    if (mode >= 0 && !blit_raster_set_hinting(raster, (BlitRasterHinting)mode)) return 0;
#endif
    if (!blit_raster_get(raster, scalar, 1, 0, &glyph) || glyph.format != BLIT_RASTER_COVERAGE) return 0;
    *hash = fnv(*hash, &glyph);
    blit_raster_close(raster);
    return 1;
}

int main(int argc, char **argv)
{
    BlitFont font;
    unsigned long long def = FNV_BASIS;
    int i, s;
#ifndef BASELINE
    unsigned long long full = FNV_BASIS, text[3], icon[3];
    int m;
#endif
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < NTEXT; ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], -1, kText[i], &def));
    printf("default text hash 0x%llxULL\n", def);
#ifdef BASELINE
    return 0;
#else
    CHECK(def == DEFAULT_TEXT_HASH);   /* byte-identical to before the setting */
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < NTEXT; ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], BLIT_RASTER_HINTING_FULL, kText[i], &full));
    CHECK(full == def);                /* full is the default, not a neighbour */
    {
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], 6, 12);
        CHECK(raster && !blit_raster_set_hinting(raster, (BlitRasterHinting)3));
        CHECK(!blit_raster_set_hinting(raster, (BlitRasterHinting)-1));
        blit_raster_close(raster);
        CHECK(!blit_raster_set_hinting(NULL, BLIT_RASTER_HINTING_NONE));
    }
    for (m = 0; m < 3; ++m) {
        text[m] = icon[m] = FNV_BASIS;
        for (s = 0; s < NSIZES; ++s) {
            CHECK(render(&font, argv[2], argv[3], kSizes[s], m, kIcon, &icon[m]));
            for (i = 0; i < NTEXT; ++i)
                CHECK(render(&font, argv[2], argv[3], kSizes[s], m, kText[i], &text[m]));
        }
        printf("mode %d text 0x%llx icon 0x%llx\n", m, text[m], icon[m]);
    }
    CHECK(icon[0] == icon[1] && icon[1] == icon[2]);   /* fitted: ignores it */
    CHECK(text[0] != text[1] && text[0] != text[2] && text[1] != text[2]);
    /* At the smallest size every mode is its own raster. */
    for (m = 0; m < 3; ++m) {
        int n;
        for (n = m + 1; n < 3; ++n) {
            unsigned long long a = FNV_BASIS, b = FNV_BASIS;
            for (i = 0; i < NTEXT; ++i) {
                CHECK(render(&font, argv[2], argv[3], kSizes[0], m, kText[i], &a));
                CHECK(render(&font, argv[2], argv[3], kSizes[0], n, kText[i], &b));
            }
            CHECK(a != b);
        }
    }
    puts("font hinting gate: ok");
    return 0;
#endif
}
