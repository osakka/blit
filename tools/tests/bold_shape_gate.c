/* ADR 0240: a bold cell keeps its glyph's shape. Bold is the renderer's
   one-pixel overstrike with its last column dropped; a glyph whose ink
   already reaches its last column (Hack's U+25CF, the window buttons) was
   flattened on the right into a D. Drawn here through draw_cell_glyphs,
   the compositor's own cell path, for every text font given, at every cell
   height 8-64 (the raster's range) and every hinting mode:
   - a symmetric shape that reaches its last column stays symmetric in bold
     (ASCII, Latin-1, punctuation and the shape blocks are swept);
   - text, and every glyph with room, is drawn bold exactly as before (regular glyph,
     then the same glyph one pixel right, last column dropped).
   BLIT_BOLD_GATE_OLD=1 hands the raster's regular glyph to bold cells, as
   the raster did before ADR 0240: the gate must then fail. */
#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"

#define CELLS_W 160
#define CELLS_H 96

static int (*regular_get)(void *, uint32_t, int, int, int, struct BlitRasterGlyph *);
static int old_get(void *state, uint32_t cp, int columns, int pen, int bold,
                   struct BlitRasterGlyph *glyph)
{ (void)bold; return regular_get(state, cp, columns, pen, 0, glyph); }

static uint32_t pixels[CELLS_W * CELLS_H], expected[CELLS_W * CELLS_H];

/* Only the cell and a pixel round it are drawn or read: clear just that. */
static int clear_w = CELLS_W, clear_h = CELLS_H;
static void clear(uint32_t *p)
{
    int y;
    for (y = 7; y < 9 + clear_h && y < CELLS_H; ++y)
        memset(p + y * CELLS_W + 7, 0, (size_t)(clear_w + 2) * 4);
}
static int cov(const uint32_t *p, int x, int y) { return (int)(p[(8 + y) * CELLS_W + 8 + x] & 0xff); }

int main(int argc, char **argv)
{
    static const uint32_t symmetric[] = {
        0x25cf, 0x25cb, 0x25a0, 0x25a1, 0x25c6, 0x25c7, 0x25c9, 0x25ce, 0x2b24,
        0x2022, 0x25aa, 0x25ab, 0x26ab, 0x26aa
    };
    int old = getenv("BLIT_BOLD_GATE_OLD") && !strcmp(getenv("BLIT_BOLD_GATE_OLD"), "1");
    int f, cases = 0, room = 0, shaped = 0, same = 0, failures = 0;
    BlitFont fallback;
    KmsOutput output, reference;
    if (argc < 4) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 2;
    memset(&output, 0, sizeof(output));
    output.mode.hdisplay = CELLS_W; output.mode.vdisplay = CELLS_H;
    output.back.pitch = CELLS_W * 4;
    reference = output;
    output.back.map = pixels; reference.back.map = expected;
    for (f = 3; f < argc; ++f) {
        int height, hinting;
        for (height = 8; height <= 64; ++height)
            for (hinting = BLIT_RASTER_HINTING_FULL; hinting <= BLIT_RASTER_HINTING_NONE; ++hinting) {
                int width = (height + 1) / 2;
                BlitFont font = fallback;
                BlitRaster *r;
                uint32_t cp;
                unsigned i;
                if (width < 4 || width + 10 > CELLS_W - 8 || height + 10 > CELLS_H - 8) continue;
                font.raster_state = NULL; font.raster_free = NULL;
                r = blit_raster_open(&fallback, argv[f], argv[2], width, height);
                if (!r || !blit_raster_set_hinting(r, (BlitRasterHinting)hinting)) return 2;
                blit_raster_attach(&font, r);
                regular_get = font.raster_get;
                if (old) font.raster_get = old_get;
                /* Every glyph: the shape check where it reaches its last
                   column, otherwise exactly the old overstrike. */
                for (cp = 0x21; cp <= 0x2bff; ++cp) {
                    BlitRasterGlyph regular;
                    int sym = 0, x, y, edge = 0;
                    /* Text (ASCII, Latin-1, punctuation and bullets) and the
                       shape blocks: arrows, technical, geometric shapes,
                       miscellaneous symbols, dingbats, symbols and arrows. */
                    if (cp == 0x7f) cp = 0xa1;
                    if (cp == 0x100) cp = 0x2010;
                    if (cp == 0x2070) cp = 0x2190;
                    if (cp == 0x2200) cp = 0x2300;
                    if (cp == 0x2400) cp = 0x25a0;
                    if (cp == 0x27c0) cp = 0x2b00;
                    if (!regular_get(font.raster_state, cp, 1, 0, 0, &regular) ||
                        regular.fallback || regular.format != BLIT_RASTER_COVERAGE ||
                        regular.ink_offset_x || regular.ink_offset_y) continue;
                    for (i = 0; i < sizeof(symmetric) / sizeof(symmetric[0]); ++i)
                        if (symmetric[i] == cp) sym = 1;
                    for (y = 0; y < regular.height; ++y)
                        if (regular.pixels[y * regular.stride + width - 1] >= 64) edge = 1;
                    ++cases;
                    clear_w = width + 1; clear_h = height;
                    clear(pixels);
                    draw_cell_glyphs(&output, &font, NULL, cp, NULL, 0, 8, 8, width, height,
                                     0xffffff, 0, 1, 0);
                    /* Text (letters, digits, punctuation) keeps the plain
                       overstrike even at its last column. */
                    if (!((cp >= 0x2190 && cp <= 0x2bff) || cp == 0x2022 ||
                          cp == 0x2023 || cp == 0x2043)) edge = 0;
                    if (!edge) {
                        clear(expected);
                        draw_glyph(&reference, &font, cp, 8, 8, width, height, 0xffffff, 0, 0, 0, 0);
                        draw_glyph(&reference, &font, cp, 9, 8, width, height, 0xffffff, 0, 1, 0, 0);
                        int differs = 0, row;
                        for (row = 7; row < 9 + height && !differs; ++row)
                            differs = memcmp(pixels + row * CELLS_W + 7, expected + row * CELLS_W + 7,
                                             (size_t)(width + 3) * 4) != 0;
                        if (differs) {
                            if (failures++ < 8)
                                fprintf(stderr, "BOLD changed %s h=%d hinting=%d U+%04X\n",
                                        argv[f], height, hinting, cp);
                        } else ++same;
                        continue;
                    }
                    ++room;
                    /* Nothing may leave the cell. */
                    for (y = -1; y <= height; ++y)
                        if (cov(pixels, -1, y) || cov(pixels, width, y)) {
                            if (failures++ < 8)
                                fprintf(stderr, "BOLD ink left the cell %s h=%d U+%04X\n",
                                        argv[f], height, cp);
                            break;
                        }
                    if (!sym) continue;
                    /* A symmetric shape: its edge columns, row by row, match
                       (an overstrike lying on the soft edge makes the right
                       one solid where the left one is soft). */
                    {
                        /* What bold adds to the edge difference: a font's own
                           lean (light hinting leaves x unhinted) is not bold's. */
                        long worst = 0, lean = 0;
                        for (y = 0; y < height; ++y) {
                            long d = cov(pixels, width - 1, y) - cov(pixels, 0, y);
                            long r = regular.pixels[y * regular.stride + width - 1] -
                                     (long)regular.pixels[y * regular.stride];
                            if (d - (r > 0 ? r : 0) > worst) worst = d - (r > 0 ? r : 0);
                            lean += d - r;
                        }
                        if (worst > 112 || lean > 160) {
                            if (failures++ < 8)
                                fprintf(stderr, "BOLD flattened %s h=%d hinting=%d U+%04X worst=%ld lean=%ld\n",
                                        argv[f], height, hinting, cp, worst, lean);
                        } else ++shaped;
                    }
                }
                font.raster_get = regular_get;
                blit_raster_attach(&font, NULL);
            }
    }
    blit_font_free(&fallback);
    printf("BOLD_SHAPE cases=%d unchanged=%d needing_room=%d symmetric_kept=%d failed=%d%s\n",
           cases, same, room, shaped, failures, old ? " (old raster)" : "");
    return failures || !room || !shaped ? 1 : 0;
}
