/* Offline specimen using the production raster, no display: every shipped
   text font, a size ramp, Blit's own icons, file icons, Nerd glyphs and
   colour emoji. Arguments: PSF fallback, emoji face, icon rows file, output
   PPM, then name=path for each text font. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define WIDTH 1200
#define HEIGHT 1110
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

/* ASCII plus the few UTF-8 sequences the specimen uses. */
static void text(BlitRaster *r, int size, const char *s, int x, int y, unsigned fg)
{
    const unsigned char *p = (const unsigned char *)s;
    while (*p) {
        unsigned cp = *p++;
        if (cp >= 0xe0) { cp = (cp & 15) << 12 | (p[0] & 63) << 6 | (p[1] & 63); p += 2; }
        else if (cp >= 0xc0) { cp = (cp & 31) << 6 | (p[0] & 63); p += 1; }
        glyph(r, cp, 1, x, y, fg);
        x += (size + 1) / 2;
    }
}

static BlitRaster *open_face(const BlitFont *fallback, const char *face, const char *emoji, int size)
{
    BlitRaster *r = blit_raster_open(fallback, face, emoji, (size + 1) / 2, size);
    if (!r) exit(1);
    return r;
}

int main(int argc, char **argv)
{
    BlitFont fallback;
    BlitRaster *r, *label;
    const char *hack = NULL;
    int i, x, y, row;
    FILE *file;
    char line[2048];
    static const unsigned nerd[] = {0xf015, 0xf07b, 0xf0a2, 0xf021, 0xf1de, 0xe70c,
        0xf120, 0xf013, 0xf005, 0xf023, 0xf1eb, 0xf17c, 0xe702, 0xf308};
    static const unsigned palette[] = {0xffbb66, 0x6de3bd, 0x7ebaff, 0xf393ce};
    static const uint32_t clusters[][10] = {
        {0x1f600}, {0x1f680}, {0x1f525}, {0x1f98a}, {0x2764, 0xfe0f},
        {0x31, 0xfe0f, 0x20e3}, {0x1f1ec, 0x1f1e7}, {0x1f44d, 0x1f3fd},
        {0x1f469, 0x200d, 0x1f4bb},
        {0x1f468, 0x200d, 0x1f469, 0x200d, 0x1f467, 0x200d, 0x1f466},
        {0x1f3f4, 0xe0067, 0xe0062, 0xe0065, 0xe006e, 0xe0067, 0xe007f}
    };
    static const unsigned lengths[] = {1, 1, 1, 1, 2, 3, 2, 2, 3, 7, 7};
    const unsigned ink = 0xd6e7f5, muted = 0x8ca2b4, heading = 0xffc579;
    if (argc < 6) return 2;
    for (i = 5; i < argc; ++i)
        if (strncmp(argv[i], "Hack=", 5) == 0) hack = argv[i] + 5;
    if (!hack) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 1;
    for (i = 0; i < WIDTH * HEIGHT; ++i) pixels[i] = 0x10171f;

    label = open_face(&fallback, hack, argv[2], 18);
    r = open_face(&fallback, hack, argv[2], 28);
    text(r, 28, "Blit: fonts, glyphs and icons", 34, 24, heading);
    blit_raster_close(r);
    text(label, 18, "Drawn by Blit's own raster, offline: not a photograph of a screen.", 34, 66, muted);

    /* Every shipped text font, at one size. */
    text(label, 18, "TEXT FONTS", 34, 112, heading);
    y = 142;
    for (i = 5; i < argc; ++i) {
        char name[64];
        const char *eq = strchr(argv[i], '=');
        size_t n = (size_t)(eq - argv[i]);
        if (!eq || n >= sizeof(name)) return 2;
        memcpy(name, argv[i], n);
        name[n] = '\0';
        text(label, 18, name, 34, y + 4, muted);
        r = open_face(&fallback, eq + 1, argv[2], 24);
        text(r, 24, "The quick brown fox \xe2\x86\x92 0O 1Il {} [] <> @#& \xe2\x94\x82\xe2\x96\x88\xe2\x96\x92", 250, y, ink);
        blit_raster_close(r);
        y += 38;
    }

    /* One font from 16 to 42 pixels, with Powerline separators. */
    text(label, 18, "ANY SIZE, 8 TO 64 PX", 34, y + 16, heading);
    y += 46;
    for (i = 0; i < 4; ++i) {
        static const int sizes[] = {16, 22, 30, 42};
        int size = sizes[i], k;
        r = open_face(&fallback, hack, argv[2], size);
        text(r, size, "The quick brown fox", 34, y, ink);
        for (k = 0; k < 4; ++k)
            glyph(r, 0xe0b0 + k, 1, 34 + 20 * ((size + 1) / 2) + k * ((size + 1) / 2), y, palette[i]);
        y += size + 12;
        blit_raster_close(r);
    }

    /* Blit's own icons, rows from the catalogue: "cp" or "left,right". */
    file = fopen(argv[3], "r");
    if (!file) return 1;
    r = open_face(&fallback, hack, argv[2], 48);
    for (row = 0; fgets(line, sizeof(line), file); ++row) {
        char *token = strtok(line, " \n");
        if (row == 0) { text(label, 18, "BLIT ICONS", 34, y + 16, heading); y += 48; }
        if (row == 2) { text(label, 18, "FILE ICONS: FOLDERS IN FOUR COLOURS, EVERYTHING ELSE IN ITS OWN", 34, y + 16, heading); y += 48; }
        for (x = 34; token; token = strtok(NULL, " \n")) {
            unsigned left, right;
            if (sscanf(token, "%x,%x", &left, &right) == 2) {
                glyph(r, left, 1, x, y, ink);
                glyph(r, right, 1, x + 24, y, ink);
                x += 48 + 18;
            } else if (sscanf(token, "%x", &left) == 1) {
                glyph(r, left, 1, x, y, ink);
                x += 24 + 14;
            }
        }
        y += 58;
    }
    fclose(file);

    /* Nerd glyphs and colour emoji under every font. */
    text(label, 18, "NERD FONTS (10,386) AND COLOUR EMOJI (EMOJI 18.0)", 34, y + 16, heading);
    y += 48;
    for (i = 0, x = 34; i < (int)(sizeof(nerd) / sizeof(*nerd)); ++i, x += 52)
        glyph(r, nerd[i], 1, x, y, palette[i % 4]);
    y += 64;
    for (i = 0, x = 34; i < (int)(sizeof(lengths) / sizeof(*lengths)); ++i, x += 74) {
        BlitCellCluster key;
        BlitRasterGlyph g;
        if (!blit_cell_cluster_init(&key, clusters[i][0], clusters[i] + 1, lengths[i] - 1, 2) ||
            !blit_raster_get_cluster(r, &key, &g)) return 1;
        paint(&g, x, y, 0xffffff);
    }
    blit_raster_close(r);
    blit_raster_close(label);

    file = fopen(argv[4], "wb");
    if (!file) return 1;
    fprintf(file, "P6\n%d %d\n255\n", WIDTH, HEIGHT);
    for (i = 0; i < WIDTH * HEIGHT; ++i) {
        fputc(pixels[i] >> 16 & 255, file);
        fputc(pixels[i] >> 8 & 255, file);
        fputc(pixels[i] & 255, file);
    }
    if (fclose(file)) return 1;
    blit_font_free(&fallback);
    return 0;
}
