/* Inject only the public runtime-version query. This is a fail-closed logic
   fixture, not proof of loading an actual older runtime library. */
#include "../../src/blit_raster.h"
#include "../../vendor/harfbuzz/hb.h"
#include <stdio.h>
#include <string.h>
static unsigned queries;
hb_bool_t hb_version_atleast(unsigned major, unsigned minor, unsigned micro)
{
    if (major == 6 && minor == 0 && micro == 0) ++queries;
    return 0;
}
#define CHECK(t) do { if (!(t)) { fprintf(stderr, "BASELINE_FAIL line=%d\n", __LINE__); return 1; } } while (0)
int main(int argc, char **argv)
{
    BlitFont font;
    BlitRaster *plain, *outline;
    BlitRasterGlyph glyph;
    CHECK(argc == 4);
    memset(&font, 0, sizeof(font));
    CHECK(!blit_font_load_path(&font, argv[1]));
    plain = blit_raster_open(&font, NULL, NULL, 8, 16);
    outline = blit_raster_open(&font, argv[2], NULL, 8, 16);
    CHECK(plain && outline && queries == 0);
    CHECK(!blit_raster_open(&font, argv[2], argv[3], 8, 16));
    CHECK(queries == 1);
    CHECK(blit_raster_get(plain, 'A', 1, &glyph));
    CHECK(blit_raster_get(outline, 'A', 1, &glyph));
    blit_raster_close(outline); blit_raster_close(plain); blit_font_free(&font);
    puts("HARFBUZZ_BASELINE_PASS injected_old_runtime=colour_rejected existing_contexts=preserved");
    return 0;
}
