#ifndef BLIT_RASTER_H
#define BLIT_RASTER_H

#include "blit_font.h"

/* Backend-independent cell rasters. No DRM or terminal state here.
   Coverage is tinted by terminal foreground; premultiplied RGBA is not.
   Pixels and metrics remain valid until the next get or close on this object.
   The immutable PSF fallback must outlive the rasterizer. Single-thread owner. */
typedef enum {
    BLIT_RASTER_COVERAGE = 1,
    BLIT_RASTER_RGBA_PREMULTIPLIED = 4
} BlitRasterFormat;

typedef struct BlitRasterGlyph {
    const unsigned char *pixels;
    int width, height, stride;
    int baseline;
    /* Ink may extend beyond its allocated terminal cell. Consumers must add
       these offsets, and invalidate/repaint neighbouring ink on damage.
       Currently nonzero only for the opt-in Arabic module experiment. */
    int ink_offset_x, ink_offset_y;
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
/* ADR 0199: a pixel font's raster. It declines (returns 0 for) every glyph
   the PSF has, so those keep the font's own drawing path, and draws what it
   lacks from the glyph stack beside colour_path, fitted to the font's H. A
   glyph nothing has is declined too, for the font's own replacement. */
BlitRaster *blit_raster_open_pixel(const BlitFont *font, const char *colour_path,
                                   int cell_width, int cell_height);
void blit_raster_close(BlitRaster *raster);
/* Experimental Arabic companion. Attach once before any raster lookup;
   failures leave the context unchanged. No bidi, paragraph shaping or profile
   auto-selection. All letters share one face transform and baseline. */
int blit_raster_set_arabic(BlitRaster *raster, const char *path);
const char *blit_raster_arabic_path(const BlitRaster *raster);
const char *blit_raster_arabic_family(const BlitRaster *raster);
int blit_raster_baseline(const BlitRaster *raster);
int blit_raster_get(BlitRaster *raster, uint32_t scalar, int columns,
                    BlitRasterGlyph *glyph);
/* Internal bounded colour/opt-in Arabic cluster path. Unsupported forms fail
   without replacing cache entries or the caller's output. Does not segment
   terminal input or choose a cluster's column allocation. */
int blit_raster_get_cluster(BlitRaster *raster, const BlitCellCluster *cluster,
                            BlitRasterGlyph *glyph);
size_t blit_raster_cached_bytes(const BlitRaster *raster);
unsigned long blit_raster_render_count(const BlitRaster *raster);
/* Install an already validated context on the canonical loaded font. */
void blit_raster_attach(BlitFont *font, BlitRaster *raster);
/* RGB destination, integer premultiplied source-over. Only coverage masks
   receive foreground tint. The same compositor is usable by every backend. */
uint32_t blit_raster_blend(const BlitRasterGlyph *glyph, int x, int y,
                         uint32_t foreground, uint32_t background);

int blit_raster_inspect_outline(const char *path, char *family, size_t family_size,
                                char *reason, size_t reason_size);

#endif
