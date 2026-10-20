/* Offline specimen using the production raster and compositor, no display. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1200
#define HEIGHT 1030
static unsigned pixels[WIDTH * HEIGHT];

static void glyph(BlitRaster *r, unsigned cp, int columns, int x, int y, unsigned fg)
{
    BlitRasterGlyph g;
    int dx, dy;
    if (!blit_raster_get(r, cp, columns, &g)) exit(1);
    for (dy = 0; dy < g.height && y + dy < HEIGHT; ++dy)
        for (dx = 0; dx < g.width && x + dx < WIDTH; ++dx) {
            unsigned *p = &pixels[(y + dy) * WIDTH + x + dx];
            *p = blit_raster_blend(&g, dx, dy, fg, *p);
        }
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
    unsigned palette[] = {0xffbb66, 0x6de3bd, 0x7ebaff, 0xf393ce};
    if (argc != 6) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 1;
    for (i = 0; i < WIDTH * HEIGHT; ++i) pixels[i] = 0x10171f;
    for (profile = 0; profile < 2; ++profile) {
        unsigned foreground = profile ? 0xd6e7f5 : 0xffc579;
        int top = profile * 510;
        BlitRaster *label = blit_raster_open(&fallback, argv[profile + 2], argv[4], 12, 24);
        if (!label) return 1;
        text(label, 24, profile ? "BLIT MODERN / smooth, crisp, colourful" :
            "BLIT RETRO / rounded mainframe soul", 34, top + 22, foreground);
        text(label, 24, "Native cell heights: 16 / 22 / 30 / 42 px", 34, top + 58, 0x8ca2b4);
        blit_raster_close(label);
        y = top + 106;
        for (i = 0; i < 4; ++i) {
            int sizes[] = {16, 22, 30, 42}, size = sizes[i];
            BlitRaster *r = blit_raster_open(&fallback, argv[profile + 2], argv[4], (size + 1) / 2, size);
            if (!r) return 1;
            text(r, size, "The quick brown fox / 0O 1Il / {} [] <> @#&", 34, y, foreground);
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
            blit_raster_close(r);
        }
        label = blit_raster_open(&fallback, argv[profile + 2], argv[4], 10, 20);
        if (!label) return 1;
        text(label, 20, "10,386 Nerd codepoints / intrinsic-colour scalar emoji", 34, top + 444, 0x8ca2b4);
        text(label, 20, "Production raster specimen, not a physical-display screenshot", 34, top + 472, 0x8ca2b4);
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
