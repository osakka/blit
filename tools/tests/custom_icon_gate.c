/* Original glyphs through the production raster, not cmap presence alone. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "CUSTOM_ICON_FAIL line=%d size=%d cp=%x\n", __LINE__, size, cp); return 1; } } while (0)

int main(int argc, char **argv)
{
    BlitFont font;
    int size = 0, samples = 0;
    unsigned cp = 0, required[128], distinct[512], live[1024], retired[1024];
    int required_count = 0, distinct_count = 0, live_count = 0, retired_count = 0,
        ui_samples = 0, script_samples = 0, at;
    /* ADR 0195: what the glyph stack adds to any font -- an overhanging
       long arrow, and scripts no shipped text font has (Cyrillic,
       Hebrew, Thai, Georgian, Ethiopic, CJK) -- drawn, never fallback. */
    static const unsigned scripts[] = { 0x27f6, 0x0416, 0x05e9, 0x0e01, 0x10d0, 0x1200, 0x6f22, 0x5b57 };
    FILE *input;
    if (argc != 8) return 2;
    input = fopen(argv[4], "r");
    if (!input) return 2;
    while (required_count < 128 && fscanf(input, "%x", &cp) == 1)
        required[required_count++] = cp;
    fclose(input);
    CHECK(required_count == 101);
    /* One left-half codepoint per distinct file drawing (ADR 0201). */
    input = fopen(argv[5], "r");
    if (!input) return 2;
    while (distinct_count < 512 && fscanf(input, "%x", &cp) == 1)
        distinct[distinct_count++] = cp;
    fclose(input);
    CHECK(distinct_count > 1);
    /* Every colour original in the catalogue, and every retired codepoint
       (ADR 0201): read, not written here as ranges. */
    input = fopen(argv[6], "r");
    if (!input) return 2;
    while (live_count < 1024 && fscanf(input, "%x", &cp) == 1) live[live_count++] = cp;
    fclose(input);
    input = fopen(argv[7], "r");
    if (!input) return 2;
    while (retired_count < 1024 && fscanf(input, "%x", &cp) == 1) retired[retired_count++] = cp;
    fclose(input);
    CHECK(live_count > 0 && retired_count > 0);
    memset(&font, 0, sizeof(font));
    CHECK(!blit_font_load_path(&font, argv[1]));
    for (size = 8; size <= 64; ++size) {
        BlitRaster *r = blit_raster_open(&font, argv[2], argv[3], (size + 1) / 2, size);
        unsigned hashes[0x231];
        int partial_icons = 0;
        int seam[2][96], reach[2], tall[2];
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
        /* A retired codepoint draws nothing from the icons; every live one
           is a colour original. U+100007/8, the bar corners, are cell
           geometry and in neither list. U+100018/9 are the two columns of
           the hui badge (ADR 0197). */
        for (at = 0; at < retired_count; ++at) {
            BlitRasterGlyph g;
            cp = retired[at];
            CHECK(!blit_raster_get(r, cp, 1, &g) || g.fallback);
        }
        for (at = 0; at < live_count; ++at) {
            BlitRasterGlyph g, again;
            unsigned hash = 2166136261U;
            int x, y, ink = 0, colour = 0, partial = 0;
            unsigned long renders;
            cp = live[at];
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
            /* Every icon has ink in colour. Antialiased edges are the
               renderer's, so they are required of the set at each size, not
               of every drawing: one made only of straight rectangles can
               land exactly on whole pixels (the USB stick's right half does
               at 20 px). */
            CHECK(ink && colour);
            partial_icons += partial > 0;
            hashes[cp - 0x100000] = hash;
            if (cp == 0x100018 || cp == 0x100019) {
                /* The badge's edge columns where its halves meet. */
                int column = cp == 0x100018 ? g.width - 1 : 0;
                for (y = 0; y < g.height; ++y)
                    seam[cp - 0x100018][y] = g.pixels[y * g.stride + column * 4 + 3];
                {
                    int first = -1, last = -1;
                    for (y = 0; y < g.height; ++y) for (x = 0; x < g.width; ++x)
                        if (g.pixels[y * g.stride + x * 4 + 3]) {
                            if (first < 0) first = y;
                            last = y;
                        }
                    tall[cp - 0x100018] = last - first + 1;
                }
                /* How far the ring reaches from the seam, on the middle row. */
                reach[cp - 0x100018] = -1;
                for (x = 0; x < g.width; ++x) {
                    int at = cp == 0x100018 ? x : g.width - 1 - x;
                    if (g.pixels[(g.height / 2) * g.stride + at * 4 + 3]) {
                        reach[cp - 0x100018] = cp == 0x100018 ? g.width - x : at + 1;
                        break;
                    }
                }
            }
            renders = blit_raster_render_count(r);
            CHECK(blit_raster_get(r, cp, 1, &again));
            CHECK(renders == blit_raster_render_count(r));
            ++samples;
        }
        /* Each level is its own raster: Wi-Fi 1-4, battery 0-4, charging
           0-4 and on mains, volume muted and 1-3, and the four telephone
           states in each style. */
        {
            static const int groups[][2] = {{2, 6}, {9, 20}, {0x14, 0x18}, {0x1b, 0x1f}, {0x1f, 0x23}};
            int group;
            for (group = 0; group < 5; ++group)
                for (index = groups[group][0]; index < groups[group][1]; ++index) {
                    int other;
                    for (other = index + 1; other < groups[group][1]; ++other)
                        CHECK(hashes[index] != hashes[other]);
                }
        }
        CHECK(partial_icons > 0);
        /* Two-column icons are told apart whole -- a plain half of one page
           may match another's -- and from 16 px, below which two icons can
           honestly round to the same pixels: one per file drawing -- each
           kind, and each colourway of a folder -- so no two look alike. */
        if (size >= 16) {
            int a, b;
            for (a = 0; a < distinct_count; ++a)
                for (b = a + 1; b < distinct_count; ++b) {
                    int i = (int)distinct[a] - 0x100000, j = (int)distinct[b] - 0x100000;
                    CHECK(hashes[i] != hashes[j] || hashes[i + 1] != hashes[j + 1]);
                }
        }
        /* The halves meet: no row is clearly inked on one side of the seam
           and blank on the other, which a half shifted up or down makes;
           and the ring reaches as far either side of the seam, give or
           take a pixel, which a half shifted sideways breaks. The edge
           columns are neighbours, not copies, so their values may differ. */
        {
            int x, y, left = 0, right = 0, apart = 0;
            CHECK(hashes[0x18] != hashes[0x19]);
            for (y = 0; y < size; ++y) {
                int most = seam[0][y] > seam[1][y] ? seam[0][y] : seam[1][y];
                int least = seam[0][y] < seam[1][y] ? seam[0][y] : seam[1][y];
                left += seam[0][y] > 0;
                right += seam[1][y] > 0;
                apart += most >= 96 && least == 0;
            }
            CHECK(left > 0 && right > 0 && apart == 0);
            CHECK(reach[0] > 0 && reach[1] > 0 && abs(reach[0] - reach[1]) <= 1);
            /* The ring is whole, 80% of the cell tall: kept inside the
               cell, not clipped by it. */
            for (x = 0; x < 2; ++x) CHECK(tall[x] >= (int)(0.8 * size) - 1);
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
    printf("CUSTOM_ICON_PASS samples=%d sizes=57 icons=catalogue distinct_levels=wifi4+battery11+volume4+phone4x2 file_icons=distinct_drawings retired=catalogue hui_seam=met colour=premultiplied pen_alias=themed cache=bounded ui_scalars=%d ui_span_cases=%d scripts=%d no_replacement=verified\n", samples, required_count, ui_samples, script_samples);
    return 0;
}
