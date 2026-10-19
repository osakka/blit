#ifndef BLIT_RASTER_H
#define BLIT_RASTER_H

#include "blit_font.h"

/* Backend-independent cell rasters. No SDL, DRM, or terminal state here.
   Coverage is tinted by terminal foreground; premultiplied RGBA is not.
   Pixels and metrics remain valid until the next get or close on this object.
   The immutable PSF fallback must outlive the rasterizer. Single-thread owner. */
typedef enum {
    BLIT_RASTER_COVERAGE = 1,
    BLIT_RASTER_RGBA_PREMULTIPLIED = 4
} BlitRasterFormat;

typedef struct {
    const unsigned char *pixels;
    int width, height, stride;
    int baseline;
    BlitRasterFormat format;
    int fallback;
} BlitRasterGlyph;

typedef struct BlitRaster BlitRaster;

/* NULL outline selects deliberate pixel-art PSF rendering. A requested face
   that cannot load is an error, not a silently substituted configuration.
   Open a replacement first, then swap: failed changes leave the old one alive. */
BlitRaster *blit_raster_open(const BlitFont *fallback,
                            const char *outline_path, const char *colour_path,
                            int cell_width, int cell_height);
void blit_raster_close(BlitRaster *raster);
int blit_raster_get(BlitRaster *raster, uint32_t scalar, int columns,
                    BlitRasterGlyph *glyph);
size_t blit_raster_cached_bytes(const BlitRaster *raster);
unsigned long blit_raster_render_count(const BlitRaster *raster);
/* RGB destination, integer premultiplied source-over. Only coverage masks
   receive foreground tint. The same compositor is usable by every backend. */
uint32_t blit_raster_blend(const BlitRasterGlyph *glyph, int x, int y,
                         uint32_t foreground, uint32_t background);

#endif
