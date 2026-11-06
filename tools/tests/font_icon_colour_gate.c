/* ADR 0220: fonts.icon_colour at the raster. The default is what Blit's
   colour icons always were, byte for byte (for the icons whose artwork the
   setting did not touch). Black, white and auto draw the icon's monochrome
   shape: black and white are exactly black and white, auto is coverage for the
   cell's own foreground, and all three have the same shape. An icon with no
   levels and no knockouts keeps exactly its colour coverage. The hui badge is
   a solid disc with its letters cut out. Window glyphs, text and emoji do not
   follow the setting. The state families' shapes are font_icon_shape_gate.c's. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

#define FNV_BASIS 14695981039346656037ULL
/* The hash of the default rasters of kPlain at kSizes, recorded from the
   raster and icon font as they were before the setting existed. */
#define DEFAULT_ICON_HASH 0xb7481b6c085ee233ULL
/* Icons whose artwork is untouched and which have no levels and no knockouts:
   hibr-pen, blit, bluetooth, power, phone-on, volume-3. */
static const uint32_t kPlain[] = { 0x100001, 0x100000, 0x100006, 0x10001a, 0x10001d, 0x100017 };
/* Untouched artwork with a monochrome glyph: the hui badge, two columns. */
static const uint32_t kHui[] = { 0x100018, 0x100019 };
/* Edited or with levels: battery-2, battery-3-charging, battery-ac, wifi-2, volume-1. */
static const uint32_t kState[] = { 0x10000b, 0x100011, 0x100013, 0x100003, 0x100015 };
/* Chalk's first window glyph, the letters a and W, and a colour emoji. */
static const uint32_t kOther[] = { 0x100231, 0x100245, 'a', 'W', 0x1f600 };
static const int kSizes[] = { 10, 12, 16, 22, 44 };
#define NSIZES ((int)(sizeof(kSizes) / sizeof(kSizes[0])))
#define LEN(a) ((int)(sizeof(a) / sizeof((a)[0])))

static unsigned long long fnv(unsigned long long h, const BlitRasterGlyph *g)
{
    int y, x;
    for (y = 0; y < g->height; ++y)
        for (x = 0; x < g->width * (int)g->format; ++x) {
            h ^= g->pixels[y * g->stride + x];
            h *= 1099511628211ULL;
        }
    return h;
}

/* mode < 0: the raster as opened, never told. A copy of the glyph goes to
   *out when it is asked for. */
static int render(const BlitFont *font, const char *outline, const char *colour,
                  int size, int mode, uint32_t scalar, unsigned long long *hash,
                  BlitRasterGlyph *out)
{
    BlitRaster *raster = blit_raster_open(font, outline, colour, (size + 1) / 2, size);
    BlitRasterGlyph glyph;
    if (!raster) return 0;
#ifndef BASELINE
    if (mode >= 0 && !blit_raster_set_icon_colour(raster, (BlitRasterIconColour)mode)) return 0;
#endif
    if (!blit_raster_get(raster, scalar, 1, &glyph)) return 0;
    *hash = fnv(*hash, &glyph);
    if (out) {
        size_t bytes = (size_t)glyph.stride * glyph.height;
        *out = glyph;
        out->pixels = malloc(bytes);
        if (!out->pixels) return 0;
        memcpy((void *)out->pixels, glyph.pixels, bytes);
    }
    blit_raster_close(raster);
    return 1;
}

#ifndef BASELINE
/* The coverage of a glyph in any format. */
static int alpha(const BlitRasterGlyph *g, int x, int y)
{
    return g->format == BLIT_RASTER_COVERAGE ? g->pixels[y * g->stride + x]
                                             : g->pixels[y * g->stride + x * 4 + 3];
}

static int pen(const BlitRasterGlyph *g, int x, int y, int channel)
{
    return g->pixels[y * g->stride + x * 4 + channel];
}

static void release(BlitRasterGlyph *g) { free((void *)g->pixels); }
#endif

int main(int argc, char **argv)
{
    BlitFont font;
    unsigned long long def = FNV_BASIS;
    int i, s;
#ifndef BASELINE
    unsigned long long colour = FNV_BASIS, other[4];
    int m;
#endif
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < LEN(kPlain); ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], -1, kPlain[i], &def, NULL));
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < LEN(kHui); ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], -1, kHui[i], &def, NULL));
    printf("default icon hash 0x%llxULL\n", def);
#ifdef BASELINE
    return 0;
#else
    CHECK(def == DEFAULT_ICON_HASH);   /* byte-identical to before the setting */
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < LEN(kPlain); ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], BLIT_RASTER_ICON_COLOUR, kPlain[i], &colour, NULL));
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < LEN(kHui); ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], BLIT_RASTER_ICON_COLOUR, kHui[i], &colour, NULL));
    CHECK(colour == def);              /* colour is the default, not a neighbour */
    {
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], 6, 12);
        CHECK(raster && !blit_raster_set_icon_colour(raster, (BlitRasterIconColour)4));
        CHECK(!blit_raster_set_icon_colour(raster, (BlitRasterIconColour)-1));
        CHECK(blit_raster_set_icon_colour(raster, BLIT_RASTER_ICON_AUTO));
        blit_raster_close(raster);
        CHECK(!blit_raster_set_icon_colour(NULL, BLIT_RASTER_ICON_BLACK));
    }
    /* Chrome, text and emoji: the four modes draw the same. */
    for (m = 0; m < 4; ++m) {
        other[m] = FNV_BASIS;
        for (s = 0; s < NSIZES; ++s)
            for (i = 0; i < LEN(kOther); ++i)
                CHECK(render(&font, argv[2], argv[3], kSizes[s], m, kOther[i], &other[m], NULL));
    }
    CHECK(other[0] == other[1] && other[1] == other[2] && other[2] == other[3]);
    printf("others 0x%llx in every mode\n", other[0]);
    /* Every icon: one shape in black, white and auto; exactly black, exactly
       white, and coverage for the cell's own pen. A plain icon's shape is its
       colour coverage, exactly. */
    for (s = 0; s < NSIZES; ++s) {
        static const uint32_t *lists[3] = { kPlain, kHui, kState };
        static const int counts[3] = { LEN(kPlain), LEN(kHui), LEN(kState) };
        int l;
        for (l = 0; l < 3; ++l)
            for (i = 0; i < counts[l]; ++i) {
                BlitRasterGlyph c, b, w, a;
                unsigned long long h = 0;
                int x, y;
                CHECK(render(&font, argv[2], argv[3], kSizes[s], 0, lists[l][i], &h, &c));
                CHECK(render(&font, argv[2], argv[3], kSizes[s], 1, lists[l][i], &h, &b));
                CHECK(render(&font, argv[2], argv[3], kSizes[s], 2, lists[l][i], &h, &w));
                CHECK(render(&font, argv[2], argv[3], kSizes[s], 3, lists[l][i], &h, &a));
                CHECK(c.format == BLIT_RASTER_RGBA_PREMULTIPLIED && b.format == c.format && w.format == c.format);
                CHECK(a.format == BLIT_RASTER_COVERAGE);
                CHECK(c.width == b.width && c.height == b.height && c.baseline == b.baseline);
                for (y = 0; y < c.height; ++y)
                    for (x = 0; x < c.width; ++x) {
                        int ab = alpha(&b, x, y);
                        CHECK(ab == alpha(&w, x, y) && ab == alpha(&a, x, y));
                        CHECK(pen(&b, x, y, 0) == 0 && pen(&b, x, y, 1) == 0 && pen(&b, x, y, 2) == 0);
                        CHECK(pen(&w, x, y, 0) == ab && pen(&w, x, y, 1) == ab && pen(&w, x, y, 2) == ab);
                        if (l == 0) CHECK(ab == alpha(&c, x, y));
                    }
                release(&c); release(&b); release(&w); release(&a);
            }
    }
    /* A state icon is not just its colour coverage: wifi-2 draws two bars. */
    {
        BlitRasterGlyph c, b;
        unsigned long long h = 0;
        int x, y, covered_c = 0, covered_b = 0;
        CHECK(render(&font, argv[2], argv[3], 44, 0, 0x100003, &h, &c));
        CHECK(render(&font, argv[2], argv[3], 44, 1, 0x100003, &h, &b));
        for (y = 0; y < c.height; ++y)
            for (x = 0; x < c.width; ++x) {
                covered_c += alpha(&c, x, y) >= 128;
                covered_b += alpha(&b, x, y) >= 128;
            }
        CHECK(covered_b < covered_c);      /* the two inactive bars are left out */
        release(&c); release(&b);
    }
    /* The badge is a solid disc with the letters cut out: more covered
       pixels than the colour ring, and transparent pixels inside it. */
    {
        BlitRasterGlyph c, b;
        unsigned long long h = 0;
        int x, y, covered_c = 0, covered_b = 0, holes = 0;
        CHECK(render(&font, argv[2], argv[3], 44, 0, kHui[0], &h, &c));
        CHECK(render(&font, argv[2], argv[3], 44, 1, kHui[0], &h, &b));
        for (y = 0; y < c.height; ++y)
            for (x = 0; x < c.width; ++x) {
                covered_c += alpha(&c, x, y) == 255;
                covered_b += alpha(&b, x, y) == 255;
                /* Inside the disc: between the first and last covered pixel of the row. */
                if (alpha(&b, x, y) == 0) {
                    int l = x - 1, r = x + 1;
                    while (l >= 0 && alpha(&b, l, y) == 0) --l;
                    while (r < b.width && alpha(&b, r, y) == 0) ++r;
                    holes += l >= 0 && r < b.width && x > b.width / 3;
                }
            }
        printf("hui-left 44px: colour covers %d, mono covers %d, %d letter pixels cut out\n",
               covered_c, covered_b, holes);
        CHECK(covered_b > covered_c);
        CHECK(holes > 0);
        release(&c); release(&b);
    }
    puts("font icon colour gate: ok");
    return 0;
#endif
}
