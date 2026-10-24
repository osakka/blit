#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"
#include "emoji_samples.h"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #test); return 1; } } while (0)

static int attachment_layout_gate(BlitFont *font, KmsOutput *output,
                                  uint32_t *pixels, int size)
{
    /* Match the actual server's sixteen-slot cell layout. Visible ASCII
       sentinels deliberately test indexing, not Unicode combining policy. */
    uint32_t cp[12] = {0}, fg[12] = {0}, bg[12] = {0};
    uint16_t attr[12] = {0};
    uint32_t marks[12 * 16], expected[160 * 96];
    unsigned char counts[12] = {0};
    int cases[] = {0, 1, 5, 16};
    int scenario, row, col, mark, i, width = (size + 1) / 2;
    BlitKmsSurfaceView view;
    KmsOutput reference = *output;
    memset(&view, 0, sizeof(view));
    view.columns = 4; view.rows = 2; view.stride = 6;
    view.origin_x = 8; view.origin_y = 8;
    view.cell_width = width; view.cell_height = size;
    view.codepoints = cp; view.foreground = fg; view.background = bg;
    view.attributes = attr; view.combining = marks;
    view.combining_count = counts;
    view.default_foreground = 0xabcdef;
    view.default_background = 0x123456;
    reference.back.map = expected;
    for (i = 0; i < 12 * 16; ++i) marks[i] = '!' + (i * 17 % 80);
    for (scenario = 0; scenario < 4; ++scenario) {
        for (i = 0; i < 12; ++i) counts[i] = cases[scenario];
        for (i = 0; i < 160 * 96; ++i) pixels[i] = expected[i] = 0x123456;
        draw_view(output, font, NULL, &view);
        for (row = 0; row < 2; ++row)
            for (col = 0; col < 4; ++col)
                for (mark = 0; mark < counts[row * 6 + col]; ++mark)
                    draw_glyph(&reference, font, marks[(row * 6 + col) * 16 + mark],
                        8 + col * width, 8 + row * size, width, size, 0xabcdef, 0, 0);
        if (memcmp(pixels, expected, sizeof(expected))) {
            fprintf(stderr, "KMS_ATTACHMENT_LAYOUT_FAIL size=%d count=%d stride=16\n",
                size, cases[scenario]);
            return 0;
        }
    }
    return 1;
}
static int cluster_view_gate(BlitFont *font, KmsOutput *output, uint32_t *pixels, int size)
{
    int sample, span, cursor, x, y;
    for (sample = 0; sample < (int)EMOJI_SAMPLE_COUNT; ++sample) for (span = 1; span <= 2; ++span)
        for (cursor = 0; cursor <= 1; ++cursor) {
            uint32_t cp[2] = {emoji_samples[sample][0], 0};
            uint32_t marks[2 * BLIT_MAX_COMBINING] = {0}, colours[2] = {0};
            unsigned char counts[2] = {emoji_lengths[sample] - 1, 0};
            uint16_t attrs[2] = {1 | (span == 2 ? KMS_ATTR_WIDE : 0), KMS_ATTR_CONTINUATION};
            BlitKmsSurfaceView view;
            BlitCellCluster key;
            BlitRasterGlyph glyph;
            memcpy(marks, emoji_samples[sample] + 1, counts[0] * sizeof(*marks));
            CHECK(blit_cell_cluster_init(&key, cp[0], marks, counts[0], span));
            CHECK(font->raster_cluster_get(font->raster_state, &key, &glyph));
            memset(&view, 0, sizeof(view));
            view.columns = span; view.rows = 1; view.stride = 2;
            view.origin_x = view.origin_y = 8;
            view.cell_width = (size + 1) / 2; view.cell_height = size;
            view.codepoints = cp; view.attributes = attrs;
            view.combining = marks; view.combining_count = counts;
            view.foreground = view.background = colours;
            view.default_foreground = 0xabcdef; view.default_background = 0x123456;
            view.cursor_visible = cursor; view.cursor_x = span - 1;
            for (x = 0; x < 160 * 96; ++x) pixels[x] = 0x123456;
            draw_view(output, font, NULL, &view);
            for (y = 0; y < 96; ++y) for (x = 0; x < 160; ++x) {
                uint32_t expected = 0x123456;
                if (x >= 8 && x < 8 + glyph.width && y >= 8 && y < 8 + glyph.height) {
                    expected = blit_raster_blend(&glyph, x - 8, y - 8,
                        cursor ? 0x123456 : 0xabcdef, cursor ? KMS_WHITE : 0x123456);
                    if (glyph.format == BLIT_RASTER_COVERAGE && x > 8)
                        expected = blit_raster_blend(&glyph, x - 9, y - 8,
                            cursor ? 0x123456 : 0xabcdef, expected);
                }
                CHECK(pixels[y * 160 + x] == expected);
            }
        }
    return 0;
}

int main(int argc, char **argv)
{
    BlitFont font, wide;
    KmsOutput output;
    uint32_t pixels[160 * 96];
    int size, samples = 0;
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font)); memset(&output, 0, sizeof(output));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    memset(&wide, 0, sizeof(wide));
    CHECK(blit_font_load_path(&wide, "assets/fonts/Blit-Hibr-Unifont16.psf") == 0);
    CHECK(blit_font_has_glyph(&wide, 0x2615));
    output.mode.hdisplay = 160; output.mode.vdisplay = 96;
    output.back.map = pixels; output.back.pitch = 160 * 4;
    for (size = 8; size <= 64; ++size) {
        uint32_t scalars[] = {'A', 'g', 0x2588, 0x2500, 0x1f600, '9', 0x25c6, 0xfe0f, 0x200d,
            0x270e, 0xf040, 0xf015, 0xf07b, 0xf014, 0xf021, 0xf00c, 0xf071, 0x1f514,
            0xe0b0, 0xe0b1, 0xe0b2, 0xe0b3, 0x2615,
            0x100000, 0x100001, 0x100002, 0x100003, 0x100004, 0x100005, 0x100006,
            0x100007, 0x100008};
        unsigned index;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], (size + 1) / 2, size);
        CHECK(raster); blit_raster_attach(&font, raster);
        CHECK(attachment_layout_gate(&font, &output, pixels, size));
        CHECK(cluster_view_gate(&font, &output, pixels, size) == 0);
        for (index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
            BlitRasterGlyph glyph;
            int columns = scalars[index] == 0x1f600 || scalars[index] == 0x1f514 || scalars[index] == 0x2615 ? 2 : 1;
            int powerline = scalars[index] >= 0xe0b0 && scalars[index] <= 0xe0b3;
            int x, y;
            CHECK(font.raster_get(font.raster_state, scalars[index], columns, &glyph));
            for (x = 0; x < 160 * 96; ++x) pixels[x] = 0x123456;
            draw_cell_glyphs(&output, &font, columns == 2 ? &wide : NULL,
                       scalars[index], NULL, 0, 8, 8, glyph.width,
                       glyph.height, 0xabcdef, 0, 0);
            if (powerline) draw_glyph(&output, &font, scalars[index], 9, 8,
                glyph.width, glyph.height, 0xabcdef, 0, 1);
            for (y = 0; y < 96; ++y)
                for (x = 0; x < 160; ++x) {
                    uint32_t expected = x >= 8 && y >= 8 && x < 8 + glyph.width && y < 8 + glyph.height ?
                        blit_raster_blend(&glyph, x - 8, y - 8, 0xabcdef, 0x123456) : 0x123456;
                    if (powerline && x > 8 && x < 8 + glyph.width && y >= 8 && y < 8 + glyph.height)
                        expected = blit_raster_blend(&glyph, x - 9, y - 8, 0xabcdef, expected);
                    CHECK(pixels[y * 160 + x] == expected);
                }
            ++samples;
        }
    }
    blit_font_free(&font);
    blit_font_free(&wide);
    printf("FONT_KMS_RASTER_PASS sizes=57 glyph_samples=%d attachment_layout_cases=228 cluster_cases=%u cursor=covered tolerance=0 guard_pixels=exact\n", samples, (unsigned)EMOJI_SAMPLE_COUNT * 57 * 4);
    return 0;
}
