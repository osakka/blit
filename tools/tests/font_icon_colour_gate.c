/* ADR 0220: fonts.icon_colour at the raster. The default is what Blit's
   colour icons always were, byte for byte. Black and white keep every
   icon's coverage and give it one pen; the hui badge is a solid disc with
   the letters knocked out of it. Window glyphs, text and emoji do not
   follow the setting. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>
#include <stdlib.h>

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

#define FNV_BASIS 14695981039346656037ULL
/* The hash of the default rasters of kIcons at kSizes, recorded from the
   raster as it was before the setting existed. */
#define DEFAULT_ICON_HASH 0x53c5388ca3796218ULL
/* hibr-pen, battery-4, volume-3, blit, wifi-4, hui-left, hui-right */
static const uint32_t kIcons[] = { 0x100001, 0x10000d, 0x100017, 0x100000, 0x100005, 0x100018, 0x100019 };
#define HUI_LEFT 5
/* Chalk's first window glyph, the letters a and W, and a colour emoji. */
static const uint32_t kOther[] = { 0x100231, 0x100245, 'a', 'W', 0x1f600 };
static const int kSizes[] = { 10, 12, 16, 22, 44 };
#define NSIZES ((int)(sizeof(kSizes) / sizeof(kSizes[0])))
#define NICONS ((int)(sizeof(kIcons) / sizeof(kIcons[0])))
#define NOTHER ((int)(sizeof(kOther) / sizeof(kOther[0])))

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
static const unsigned char *at(const BlitRasterGlyph *g, int x, int y)
{
    return g->pixels + y * g->stride + x * 4;
}
#endif

int main(int argc, char **argv)
{
    BlitFont font;
    unsigned long long def = FNV_BASIS;
    int i, s;
#ifndef BASELINE
    unsigned long long colour = FNV_BASIS, other[3], icon[3];
    int m;
#endif
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < NICONS; ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], -1, kIcons[i], &def, NULL));
    printf("default icon hash 0x%llxULL\n", def);
#ifdef BASELINE
    return 0;
#else
    CHECK(def == DEFAULT_ICON_HASH);   /* byte-identical to before the setting */
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < NICONS; ++i)
            CHECK(render(&font, argv[2], argv[3], kSizes[s], BLIT_RASTER_ICON_COLOUR, kIcons[i], &colour, NULL));
    CHECK(colour == def);              /* colour is the default, not a neighbour */
    {
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], 6, 12);
        CHECK(raster && !blit_raster_set_icon_colour(raster, (BlitRasterIconColour)3));
        CHECK(!blit_raster_set_icon_colour(raster, (BlitRasterIconColour)-1));
        blit_raster_close(raster);
        CHECK(!blit_raster_set_icon_colour(NULL, BLIT_RASTER_ICON_BLACK));
    }
    for (m = 0; m < 3; ++m) {
        icon[m] = other[m] = FNV_BASIS;
        for (s = 0; s < NSIZES; ++s) {
            for (i = 0; i < NICONS; ++i)
                CHECK(render(&font, argv[2], argv[3], kSizes[s], m, kIcons[i], &icon[m], NULL));
            for (i = 0; i < NOTHER; ++i)
                CHECK(render(&font, argv[2], argv[3], kSizes[s], m, kOther[i], &other[m], NULL));
        }
        printf("mode %d icons 0x%llx others 0x%llx\n", m, icon[m], other[m]);
    }
    CHECK(icon[0] != icon[1] && icon[0] != icon[2] && icon[1] != icon[2]);
    CHECK(other[0] == other[1] && other[1] == other[2]);  /* chrome, text, emoji: unmoved */
    /* Plain icons keep their coverage and take one pen. The glyphs are
       premultiplied, so black has no colour at all and white has exactly
       its alpha. The badge: whatever is dark in one mode is light in the
       other, at the same coverage. */
    for (s = 0; s < NSIZES; ++s)
        for (i = 0; i < NICONS; ++i) {
            BlitRasterGlyph c, b, w;
            unsigned long long h = 0;
            int x, y, badge = i >= HUI_LEFT;
            CHECK(render(&font, argv[2], argv[3], kSizes[s], 0, kIcons[i], &h, &c));
            CHECK(render(&font, argv[2], argv[3], kSizes[s], 1, kIcons[i], &h, &b));
            CHECK(render(&font, argv[2], argv[3], kSizes[s], 2, kIcons[i], &h, &w));
            CHECK(c.format == BLIT_RASTER_RGBA_PREMULTIPLIED && b.format == c.format && w.format == c.format);
            CHECK(c.width == b.width && c.height == b.height && c.baseline == b.baseline);
            for (y = 0; y < c.height; ++y)
                for (x = 0; x < c.width; ++x) {
                    const unsigned char *pc = at(&c, x, y), *pb = at(&b, x, y), *pw = at(&w, x, y);
                    CHECK(pb[3] == pw[3]);
                    if (!badge) {
                        CHECK(pc[3] == pb[3]);
                        CHECK(pb[0] == 0 && pb[1] == 0 && pb[2] == 0);
                        CHECK(pw[0] == pw[3] && pw[1] == pw[3] && pw[2] == pw[3]);
                    } else {
                        CHECK(pb[0] == pb[1] && pb[1] == pb[2] && pw[0] == pw[1] && pw[1] == pw[2]);
                        CHECK(pb[0] + pw[0] == pb[3]);
                    }
                }
            free((void *)c.pixels); free((void *)b.pixels); free((void *)w.pixels);
        }
    /* The badge is a solid disc: the ring's middle, which the colour badge
       leaves empty, is covered; the letters are the opposite shade. */
    {
        BlitRasterGlyph c, b, w;
        unsigned long long h = 0;
        int x, y, covered_c = 0, covered_b = 0, dark_b = 0, light_b = 0, dark_w = 0, light_w = 0;
        CHECK(render(&font, argv[2], argv[3], 44, 0, kIcons[HUI_LEFT], &h, &c));
        CHECK(render(&font, argv[2], argv[3], 44, 1, kIcons[HUI_LEFT], &h, &b));
        CHECK(render(&font, argv[2], argv[3], 44, 2, kIcons[HUI_LEFT], &h, &w));
        for (y = 0; y < c.height; ++y)
            for (x = 0; x < c.width; ++x) {
                const unsigned char *pc = at(&c, x, y), *pb = at(&b, x, y), *pw = at(&w, x, y);
                covered_c += pc[3] == 255;
                covered_b += pb[3] == 255;
                if (pb[3] == 255) { dark_b += pb[0] == 0; light_b += pb[0] == 255; }
                if (pw[3] == 255) { dark_w += pw[0] == 0; light_w += pw[0] == 255; }
            }
        printf("hui-left 44px: colour covers %d, mono covers %d (dark %d light %d)\n",
               covered_c, covered_b, dark_b, light_b);
        CHECK(covered_b > covered_c);                    /* the disc is solid */
        CHECK(dark_b > 0 && light_b > 0);                /* black disc, white letters */
        CHECK(dark_w == light_b && light_w == dark_b);   /* and the reverse */
        CHECK(dark_b > light_b);                         /* the disc is the greater part */
    }
    puts("font icon colour gate: ok");
    return 0;
#endif
}
