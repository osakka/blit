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

/* ADR 0218: the FreeType hinting of text glyphs. Full (zero, the default) is
   FT_LOAD_TARGET_NORMAL, light is FT_LOAD_TARGET_LIGHT, none is
   FT_LOAD_NO_HINTING. Fitted icons and symbols ignore it. */
typedef enum { BLIT_RASTER_HINTING_FULL = 0, BLIT_RASTER_HINTING_LIGHT, BLIT_RASTER_HINTING_NONE } BlitRasterHinting;
/* Set before the first lookup: a raster's cache is never mixed. Returns 0
   for an unknown mode and leaves the raster unchanged. */
int blit_raster_set_hinting(BlitRaster *raster, BlitRasterHinting hinting);

/* ADR 0220: the pen of Blit's colour icons (U+100000 and up that carry colour).
   Colour (zero, the default) draws them in their own colours. Black and white
   keep every pixel's coverage and give it that one pen; a disc badge (hui) is
   a solid disc in the pen with its letters in the opposite shade. Window
   glyphs, controls, text and emoji ignore it. */
typedef enum { BLIT_RASTER_ICON_COLOUR = 0, BLIT_RASTER_ICON_BLACK, BLIT_RASTER_ICON_WHITE } BlitRasterIconColour;
/* Set before the first lookup: a raster's cache is never mixed. Returns 0
   for an unknown mode and leaves the raster unchanged. */
int blit_raster_set_icon_colour(BlitRaster *raster, BlitRasterIconColour icon_colour);

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
/* ADR 0211, 0212: a pen-drawn glyph's plane for one pen (1 FG, 2 LINE, 3 MIX,
   as blit_window_glyphs.h numbers them), coverage, one cell: a Slate window
   glyph, or a Slate or capsule control. */
int blit_raster_get_window_plane(BlitRaster *raster, uint32_t scalar, int plane,
                                 BlitRasterGlyph *glyph);
/* Internal bounded colour/opt-in Arabic cluster path. Unsupported forms fail
   without replacing cache entries or the caller's output. Does not segment
   terminal input or choose a cluster's column allocation. */
int blit_raster_get_cluster(BlitRaster *raster, const BlitCellCluster *cluster,
                            BlitRasterGlyph *glyph);
size_t blit_raster_cached_bytes(const BlitRaster *raster);
unsigned long blit_raster_render_count(const BlitRaster *raster);
/* The glyph cache's hits and misses since the raster was made. */
void blit_raster_cache_counts(const BlitRaster *raster, unsigned long long *hits,
                              unsigned long long *misses);
/* Install an already validated context on the canonical loaded font. */
void blit_raster_attach(BlitFont *font, BlitRaster *raster);
/* RGB destination, integer premultiplied source-over. Only coverage masks
   receive foreground tint. The same compositor is usable by every backend. */
uint32_t blit_raster_blend(const BlitRasterGlyph *glyph, int x, int y,
                         uint32_t foreground, uint32_t background);

int blit_raster_inspect_outline(const char *path, char *family, size_t family_size,
                                char *reason, size_t reason_size);

#endif
