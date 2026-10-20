/* Offline specimen using the production raster and compositor, no display. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1200
#define HEIGHT 1170
static unsigned pixels[WIDTH * HEIGHT];

static void paint(const BlitRasterGlyph *g, int x, int y, unsigned fg)
{
    int dx, dy;
    for (dy = 0; dy < g->height && y + dy < HEIGHT; ++dy)
        for (dx = 0; dx < g->width && x + dx < WIDTH; ++dx) {
            unsigned *p = &pixels[(y + dy) * WIDTH + x + dx];
            *p = blit_raster_blend(g, dx, dy, fg, *p);
        }
}

static void glyph(BlitRaster *r, unsigned cp, int columns, int x, int y, unsigned fg)
{
    BlitRasterGlyph g;
    if (!blit_raster_get(r, cp, columns, &g)) exit(1);
    paint(&g, x, y, fg);
}

static void text(BlitRaster *r, int size, const char *s, int x, int y, unsigned fg)
{
    for (; *s; ++s, x += (size + 1) / 2)
        glyph(r, (unsigned char)*s, 1, x, y, fg);
}

int main(int argc, char **argv)
{
    BlitFont fallback;
    int profile, i, x, y;
    FILE *output;
    unsigned icons[] = {0x270e, 0xf015, 0xf07b, 0xf0a2, 0xf021, 0xf1de,
        0xe70c, 0xf120, 0xf013, 0xf005, 0xf023, 0xf1eb, 0xf17c};
    unsigned emoji[] = {0x1f600, 0x1f680, 0x1f514, 0x1f525, 0x1f308, 0x1f98a, 0x1f389};
    const uint32_t clusters[][10] = {
        {0x2764, 0xfe0f}, {0x31, 0xfe0f, 0x20e3}, {0x1f1ec, 0x1f1e7},
        {0x1f44d, 0x1f3fd}, {0x1f469, 0x200d, 0x1f4bb},
        {0x1f468, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466},
        {0x1f3f4, 0xe0067, 0xe0062, 0xe0065, 0xe006e, 0xe0067, 0xe007f}
    };
    const unsigned lengths[] = {2, 3, 2, 2, 3, 7, 7};
    unsigned palette[] = {0xffbb66, 0x6de3bd, 0x7ebaff, 0xf393ce};
    if (argc != 6) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 1;
    for (i = 0; i < WIDTH * HEIGHT; ++i) pixels[i] = 0x10171f;
    for (profile = 0; profile < 2; ++profile) {
        unsigned foreground = profile ? 0xd6e7f5 : 0xffc579;
        int top = profile * 580;
        BlitRaster *label = blit_raster_open(&fallback, argv[profile + 2], argv[4], 12, 24);
        if (!label) return 1;
        text(label, 24, profile ? "BLIT MODERN / smooth, crisp, colourful" :
            "BLIT RETRO / rounded mainframe soul", 34, top + 22, foreground);
        text(label, 24, "Native cell heights: 16 / 22 / 30 / 42 px", 34, top + 58, 0x8ca2b4);
        text(label, 24, "POWERLINE", 800, top + 58, 0x8ca2b4);
        blit_raster_close(label);
        y = top + 106;
        for (i = 0; i < 4; ++i) {
            int sizes[] = {16, 22, 30, 42}, size = sizes[i];
            BlitRaster *r = blit_raster_open(&fallback, argv[profile + 2], argv[4], (size + 1) / 2, size);
            if (!r) return 1;
            text(r, size, "The quick brown fox / 0O 1Il / {} [] <> @#&", 34, y, foreground);
            if (i < 3) {
                int k;
                for (k = 0; k < 4; ++k)
                    glyph(r, 0xe0b0 + k, 1, 800 + k * ((size + 1) / 2), y, foreground);
            }
            y += size + 14;
            blit_raster_close(r);
        }
        {
            BlitRaster *r = blit_raster_open(&fallback, argv[profile + 2], argv[4], 24, 48);
            if (!r) return 1;
            for (i = 0, x = 34; i < (int)(sizeof(icons)/sizeof(*icons)); ++i, x += 65)
                glyph(r, icons[i], 1, x, top + 296, palette[i % 4]);
            for (i = 0, x = 34; i < (int)(sizeof(emoji)/sizeof(*emoji)); ++i, x += 74)
                glyph(r, emoji[i], 2, x, top + 370, 0xffffff);
            for (i = 0, x = 34; i < 7; ++i, x += 74) {
                BlitCellCluster key;
                BlitRasterGlyph g;
                if (!blit_cell_cluster_init(&key, clusters[i][0], clusters[i] + 1, lengths[i] - 1, 2) ||
                    !blit_raster_get_cluster(r, &key, &g)) return 1;
                paint(&g, x, top + 430, 0xffffff);
            }
            for (i = 0; i < 3; ++i) {
                uint32_t bases[] = {0x2764, 0x1f41f, 0x2600};
                int selector;
                for (selector = 0; selector < 2; ++selector) {
                    uint32_t mark = selector ? 0xfe0f : 0xfe0e;
                    BlitCellCluster key;
                    BlitRasterGlyph g;
                    if (!blit_cell_cluster_init(&key, bases[i], &mark, 1, 2) ||
                        !blit_raster_get_cluster(r, &key, &g)) return 1;
                    paint(&g, 650 + i * 144 + selector * 52, top + 430, foreground);
                }
            }
            blit_raster_close(r);
        }
        label = blit_raster_open(&fallback, argv[profile + 2], argv[4], 10, 20);
        if (!label) return 1;
        text(label, 20, "10,386 Nerd codepoints / scalar + shaped colour emoji", 34, top + 506, 0x8ca2b4);
        text(label, 20, "VS15 / VS16", 650, top + 380, 0x8ca2b4);
        text(label, 20, "Offline raster specimen / not a physical display capture", 34, top + 534, 0x8ca2b4);
        blit_raster_close(label);
    }
    output = fopen(argv[5], "wb");
    if (!output) return 1;
    fprintf(output, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (i = 0; i < WIDTH * HEIGHT; ++i) {
        fputc(pixels[i] >> 16 & 255, output);
        fputc(pixels[i] >> 8 & 255, output);
        fputc(pixels[i] & 255, output);
    }
    if (fclose(output)) return 1;
    blit_font_free(&fallback);
    return 0;
}
