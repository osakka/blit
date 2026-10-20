/* Exercise production atlas creation/upload/queue/flush against SDL software
   and GL; reference pixels come from the canonical shared cell rasterizer. */
#define main blit_server_entry_for_font_gate
#include "../../src/blit_server.c"
#undef main
#include "emoji_samples.h"

#define CHECK(test) do { if (!(test)) { fprintf(stderr, "FAIL line %d: %s SDL=%s\n", __LINE__, #test, SDL_GetError()); return 1; } } while (0)
int main(int argc, char **argv)
{
    BlitFont font;
    BlitOutput output;
    SDL_Window *window;
    SDL_Renderer *renderer;
    int size, samples = 0;
    const char *name = getenv("BLIT_TEST_RENDERER");
    CHECK(argc == 4);
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    window = SDL_CreateWindow("raster gate", 320, 160, SDL_WINDOW_HIDDEN);
    CHECK(window);
    renderer = SDL_CreateRenderer(window, name ? name : "software");
    CHECK(renderer);
    CHECK(strcmp(SDL_GetRendererName(renderer), name ? name : "software") == 0);
    memset(&font, 0, sizeof(font));
    CHECK(blit_font_load_path(&font, argv[1]) == 0);
    for (size = 8; size <= 64; ++size) {
        uint32_t scalars[] = {'A', 'g', 0x2588, 0x2500, 0x1f600, '9', 0x25c6, 0xfe0f, 0x200d,
            0x270e, 0xf040, 0xf015, 0xf07b, 0xf014, 0xf021, 0xf00c, 0xf071, 0x1f514,
            0xe0b0, 0xe0b1, 0xe0b2, 0xe0b3};
        unsigned index;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], (size + 1) / 2, size);
        CHECK(raster);
        blit_raster_attach(&font, raster);
        memset(&output, 0, sizeof(output));
        output.renderer = renderer;
        output.glyph_geometry_enabled = 1;
        output.glyph_atlas = create_sdl_glyph_atlas(renderer, &font,
            &output.glyph_atlas_columns, &output.glyph_atlas_width, &output.glyph_atlas_height);
        CHECK(output.glyph_atlas);
        for (index = 0; index < sizeof(scalars) / sizeof(scalars[0]); ++index) {
            int columns = scalars[index] == 0x1f600 || scalars[index] == 0x1f514 ? 2 : 1;
            int powerline = scalars[index] >= 0xe0b0 && scalars[index] <= 0xe0b3;
            int x, y, channel;
            BlitRasterGlyph glyph;
            SDL_Surface *image;
            CHECK(font.raster_get(font.raster_state, scalars[index], columns, &glyph));
            SDL_SetRenderDrawColor(renderer, 0x12, 0x34, 0x56, 255);
            CHECK(SDL_RenderClear(renderer));
            render_or_queue_sdl_glyph(&output, &font, 0xabcdef, scalars[index],
                8, 8, (float)glyph.width, (float)glyph.height, 0, 0);
            if (powerline) render_or_queue_sdl_glyph(&output, &font, 0xabcdef, scalars[index],
                9, 8, (float)glyph.width, (float)glyph.height, 0, 1);
            flush_sdl_glyph_batch(&output);
            image = SDL_RenderReadPixels(renderer, NULL);
            CHECK(image);
            for (y = -1; y <= glyph.height; ++y)
                for (x = -1; x <= glyph.width; ++x) {
                    Uint8 r, g, b, a;
                    uint32_t expected = blit_raster_blend(&glyph, x, y, 0xabcdef, 0x123456);
                    int outside = x < 0 || x >= glyph.width || y < 0 || y >= glyph.height;
                    int tolerance = outside ? 0 : powerline ? 4 : 2;
                    if (powerline && !outside && x > 0)
                        expected = blit_raster_blend(&glyph, x - 1, y, 0xabcdef, expected);
                    int actual[3], want[3];
                    CHECK(SDL_ReadSurfacePixel(image, 8 + x, 8 + y, &r, &g, &b, &a));
                    actual[0] = r; actual[1] = g; actual[2] = b;
                    want[0] = (expected >> 16) & 255; want[1] = (expected >> 8) & 255; want[2] = expected & 255;
                    for (channel = 0; channel < 3; ++channel) {
                        int delta = actual[channel] - want[channel];
                        if (delta < -tolerance || delta > tolerance) {
                            fprintf(stderr, "PIXEL size=%d scalar=%x xy=%d,%d actual=%02x%02x%02x expected=%06x\n",
                                size, scalars[index], x, y, r, g, b, expected);
                            return 1;
                        }
                    }
                }
            SDL_DestroySurface(image);
            ++samples;
        }
        for (index = 0; index < EMOJI_SAMPLE_COUNT; ++index) {
            int span, cursor;
            for (span = 1; span <= 2; ++span) for (cursor = 0; cursor <= 1; ++cursor) {
                BlitCellCluster key;
                BlitRasterGlyph glyph;
                SDL_Surface *image;
                uint32_t bg = cursor ? 0xbed2be : 0x123456;
                int x, y;
                CHECK(blit_cell_cluster_init(&key, emoji_samples[index][0],
                    emoji_samples[index] + 1, emoji_lengths[index] - 1, span));
                CHECK(font.raster_cluster_get(font.raster_state, &key, &glyph));
                SDL_SetRenderDrawColor(renderer, bg >> 16 & 255, bg >> 8 & 255, bg & 255, 255);
                CHECK(SDL_RenderClear(renderer));
                CHECK(render_sdl_cluster(&output, &font, 0xabcdef, key.scalars[0],
                    key.scalars + 1, key.length - 1, 8, 8, glyph.width, glyph.height, 0, 1));
                flush_sdl_glyph_batch(&output);
                image = SDL_RenderReadPixels(renderer, NULL); CHECK(image);
                for (y = -1; y <= glyph.height; ++y) for (x = -1; x <= glyph.width; ++x) {
                    Uint8 r, g, b, a;
                    int outside = x < 0 || x >= glyph.width || y < 0 || y >= glyph.height;
                    /* SDL's8-bit premultiplied modulation/blend can differ by
                       two levels per pass. Text bold adds a second pass;
                       colour skips that pass. Guard pixels remain exact. */
                    int tolerance = outside ? 0 : glyph.format == BLIT_RASTER_COVERAGE ? 4 : 2;
                    uint32_t want = blit_raster_blend(&glyph, x, y, 0xabcdef, bg);
                    if (glyph.format == BLIT_RASTER_COVERAGE && x > 0 && x < glyph.width &&
                        y >= 0 && y < glyph.height)
                        want = blit_raster_blend(&glyph, x - 1, y, 0xabcdef, want);
                    CHECK(SDL_ReadSurfacePixel(image, x + 8, y + 8, &r, &g, &b, &a));
                    if (abs((int)r - (int)(want >> 16 & 255)) > tolerance ||
                        abs((int)g - (int)(want >> 8 & 255)) > tolerance ||
                        abs((int)b - (int)(want & 255)) > tolerance)
                        fprintf(stderr, "CLUSTER_PIXEL size=%d sample=%u span=%d cursor=%d xy=%d,%d actual=%02x%02x%02x expected=%06x\n", size, index, span, cursor, x, y, r, g, b, want);
                    CHECK(abs((int)r - (int)(want >> 16 & 255)) <= tolerance);
                    CHECK(abs((int)g - (int)(want >> 8 & 255)) <= tolerance);
                    CHECK(abs((int)b - (int)(want & 255)) <= tolerance);
                }
                SDL_DestroySurface(image);
            }
        }
        SDL_DestroyTexture(output.glyph_atlas);
        free(output.glyph_vertices); free(output.glyph_indices);
    }
    /* Queue colliding codepoints before reading, so replacing an atlas slot
       must not change vertices already queued against its previous glyph. */
    {
        uint32_t collision[] = {'A', 0xc1, 'A', 0xc1};
        uint32_t expected[64 * 32];
        int k, x, y;
        SDL_Surface *image;
        BlitRaster *raster = blit_raster_open(&font, argv[2], argv[3], 8, 16);
        CHECK(raster); blit_raster_attach(&font, raster);
        memset(&output, 0, sizeof(output)); output.renderer = renderer;
        output.glyph_geometry_enabled = 1;
        output.glyph_atlas = create_sdl_glyph_atlas(renderer, &font,
            &output.glyph_atlas_columns, &output.glyph_atlas_width, &output.glyph_atlas_height);
        CHECK(output.glyph_atlas);
        SDL_SetRenderDrawColor(renderer, 0x12, 0x34, 0x56, 255); SDL_RenderClear(renderer);
        for (k = 0; k < 4; ++k) {
            BlitRasterGlyph glyph;
            CHECK(font.raster_get(font.raster_state, collision[k], 1, &glyph));
            for (y = 0; y < 16; ++y)
                for (x = 0; x < 8; ++x)
                    expected[y * 32 + k * 8 + x] = blit_raster_blend(&glyph, x, y, 0xabcdef, 0x123456);
            render_or_queue_sdl_glyph(&output, &font, 0xabcdef, collision[k],
                                      8 + k * 8, 8, 8, 16, 0, 0);
        }
        flush_sdl_glyph_batch(&output);
        image = SDL_RenderReadPixels(renderer, NULL); CHECK(image);
        for (y = 0; y < 16; ++y)
            for (x = 0; x < 32; ++x) {
                Uint8 r, g, b, a;
                uint32_t wanted = expected[y * 32 + x];
                CHECK(SDL_ReadSurfacePixel(image, 8 + x, 8 + y, &r, &g, &b, &a));
                CHECK(abs((int)r - (int)((wanted >> 16) & 255)) <= 2);
                CHECK(abs((int)g - (int)((wanted >> 8) & 255)) <= 2);
                CHECK(abs((int)b - (int)(wanted & 255)) <= 2);
            }
        SDL_DestroySurface(image); SDL_DestroyTexture(output.glyph_atlas);
        free(output.glyph_vertices); free(output.glyph_indices);
    }
    blit_font_free(&font);
    SDL_DestroyRenderer(renderer); SDL_DestroyWindow(window); SDL_Quit();
    printf("FONT_SDL_RASTER_PASS renderer=%s sizes=57 glyph_samples=%d cluster_cases=%u tolerance=2_per_pass bold_coverage=4 guard_pixels=exact\n",
           name ? name : "software", samples, (unsigned)EMOJI_SAMPLE_COUNT * 57 * 4);
    return 0;
}
