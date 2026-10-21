#include "../../src/blit_raster.h"
#include "../../src/blit_emoji_presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(t) do { if (!(t)) { fprintf(stderr, "FAIL line=%d row=%u size=%d span=%d: %s\n", __LINE__, row, size, span, #t); exit(1); } } while (0)
static unsigned row;
static int size, span;

static void check_glyph(BlitRaster *raster, const BlitCellCluster *key, int force_colour)
{
    BlitRasterGlyph glyph, again;
    unsigned char saved[128 * 64 * 4];
    unsigned long renders;
    int x, y, ink = 0;
    CHECK(blit_raster_get_cluster(raster, key, &glyph));
    CHECK(!glyph.fallback && glyph.width == ((size + 1) / 2) * span && glyph.height == size);
    if (force_colour) CHECK(glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED);
    else if (key->length == 1 && blit_emoji_variation(key->scalars[0]) &&
             !blit_emoji_default(key->scalars[0]))
        CHECK(glyph.format == BLIT_RASTER_COVERAGE);
    CHECK(glyph.stride == glyph.width * (int)glyph.format);
    for (y = 0; y < glyph.height; ++y) for (x = 0; x < glyph.width; ++x) {
        const unsigned char *p = glyph.pixels + y * glyph.stride + x * (int)glyph.format;
        if (glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED) {
            CHECK(p[0] <= p[3] && p[1] <= p[3] && p[2] <= p[3]);
            ink += p[3] != 0;
        } else ink += p[0] != 0;
    }
    CHECK(ink > 0);
    memcpy(saved, glyph.pixels, glyph.stride * glyph.height);
    renders = blit_raster_render_count(raster);
    CHECK(blit_raster_get_cluster(raster, key, &again));
    CHECK(glyph.pixels == again.pixels && blit_raster_render_count(raster) == renders);
    CHECK(!memcmp(saved, again.pixels, again.stride * again.height));
    CHECK(blit_raster_cached_bytes(raster) <= 4U * 1024U * 1024U);
}

int main(int argc, char **argv)
{
    BlitFont fallback;
    BlitCellCluster *keys;
    BlitRaster *raster;
    unsigned total, n, i, j;
    /* Corpus once at16px per requested profile, representative families at
       every8..64 size; both owner-supplied spans, without width inference. */
    const unsigned reps[][10] = {
        {0x2764, 0xfe0f}, {0x31, 0xfe0f, 0x20e3}, {0x1f1ec, 0x1f1e7},
        {0x1f44d, 0x1f3fd}, {0x1f469, 0x200d, 0x1f4bb},
        {0x1f468, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466},
        {0x1f3f4, 0xe0067, 0xe0062, 0xe0065, 0xe006e, 0xe0067, 0xe007f}
    };
    const unsigned lengths[] = {2, 3, 2, 2, 3, 7, 7};
    CHECK(argc == 5);
    total = (unsigned)atoi(argv[4]);
    CHECK(total > 0 && total < 10000);
    keys = calloc(total, sizeof(*keys)); CHECK(keys);
    memset(&fallback, 0, sizeof(fallback));
    CHECK(!blit_font_load_path(&fallback, argv[1]));
    for (row = 0; row < total; ++row) {
        uint32_t scalars[BLIT_CLUSTER_MAX_SCALARS];
        CHECK(scanf("%u", &n) == 1 && n && n <= BLIT_CLUSTER_MAX_SCALARS);
        for (i = 0; i < n; ++i) CHECK(scanf("%x", &scalars[i]) == 1);
        CHECK(blit_cell_cluster_init(&keys[row], scalars[0], scalars + 1, n - 1, 2));
    }
    size = 16;
    raster = blit_raster_open(&fallback, argv[2], argv[3], 8, size); CHECK(raster);
    for (row = 0; row < total; ++row) for (span = 1; span <= 2; ++span) {
        keys[row].columns = span;
        check_glyph(raster, &keys[row], keys[row].length > 1);
    }
    {
        unsigned buckets[128] = {0}, first = total, second = total;
        BlitRasterGlyph glyph;
        unsigned char saved[16 * 16 * 4];
        unsigned long renders;
        for (i = 0; i < total; ++i) {
            unsigned bucket = blit_cell_cluster_hash(&keys[i]) % 128;
            if (buckets[bucket]) { first = buckets[bucket] - 1; second = i; break; }
            buckets[bucket] = i + 1;
        }
        CHECK(first < total && second < total);
        CHECK(!blit_cell_cluster_equal(&keys[first], &keys[second]));
        CHECK(blit_raster_get_cluster(raster, &keys[first], &glyph));
        memcpy(saved, glyph.pixels, glyph.stride * glyph.height);
        renders = blit_raster_render_count(raster);
        CHECK(blit_raster_get_cluster(raster, &keys[second], &glyph));
        CHECK(blit_raster_render_count(raster) <= renders + 1);
        renders = blit_raster_render_count(raster);
        CHECK(blit_raster_get_cluster(raster, &keys[first], &glyph));
        CHECK(blit_raster_render_count(raster) == renders);
        CHECK(!memcmp(saved, glyph.pixels, glyph.stride * glyph.height));
    }
    blit_raster_close(raster);
    /* No selected outline: standardized text-default singletons must use
       the shared monochrome fallback, not silently become colour. */
    raster = blit_raster_open(&fallback, NULL, argv[3], 8, size); CHECK(raster);
    for (row = 0; row < total; ++row) {
        span = 2; keys[row].columns = span;
        check_glyph(raster, &keys[row], keys[row].length > 1 ||
            blit_emoji_default(keys[row].scalars[0]));
    }
    blit_raster_close(raster);
    for (size = 8; size <= 64; ++size) {
        raster = blit_raster_open(&fallback, argv[2], argv[3], (size + 1) / 2, size); CHECK(raster);
        for (row = 0; row < 7; ++row) for (span = 1; span <= 2; ++span) {
            BlitCellCluster key;
            CHECK(blit_cell_cluster_init(&key, reps[row][0], reps[row] + 1, lengths[row] - 1, span));
            check_glyph(raster, &key, 1);
        }
        {
            const uint32_t invalid[][2] = {{'A', 'B'}, {'A', 0xfe0e}, {0x1f469, 'A'}};
            BlitCellCluster key;
            BlitRasterGlyph glyph, before;
            unsigned long renders = blit_raster_render_count(raster);
            memset(&glyph, 0x5a, sizeof(glyph)); before = glyph;
            for (j = 0; j < 3; ++j) {
                CHECK(blit_cell_cluster_init(&key, invalid[j][0], invalid[j] + 1, 1, 2));
                CHECK(!blit_raster_get_cluster(raster, &key, &glyph));
                CHECK(!memcmp(&glyph, &before, sizeof(glyph)));
                CHECK(blit_raster_render_count(raster) == renders);
            }
            key.length = BLIT_CLUSTER_MAX_SCALARS + 1;
            CHECK(!blit_raster_get_cluster(raster, &key, &glyph));
            CHECK(!blit_raster_get_cluster(raster, NULL, &glyph));
            /* A fresh cache miss must recover after rejected shapes; do not
               let a cache hit hide stale buffer content or properties. */
            {
                uint32_t tail[] = {0x1f3fd, 0x200d, 0x1f692};
                CHECK(blit_cell_cluster_init(&key, 0x1f469, tail, 3, 2));
                span = 2; check_glyph(raster, &key, 1);
                CHECK(blit_raster_render_count(raster) == renders + 1);
                CHECK(blit_raster_get(raster, 'A', 1, &glyph));
                CHECK(glyph.format == BLIT_RASTER_COVERAGE && !glyph.fallback);
            }
        }
        blit_raster_close(raster);
    }
    free(keys); blit_font_free(&fallback);
    printf("EMOJI_CLUSTER_RASTER_PASS corpus=%u corpus_size=16 spans=1,2 representative_sequences=7 representative_sizes=8..64 scope=shared_raster_not_terminal_acceptance\n", total);
    return 0;
}
