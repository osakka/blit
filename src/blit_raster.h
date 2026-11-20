#ifndef BLIT_RASTER_H
#define BLIT_RASTER_H

/* The shared raster cache's shape (ADR 0176, amended for Gitea #118): sets
   of ways, a key in the set its cluster hash picks, LRU within the set. */
#define BLIT_RASTER_CACHE_SETS 128
#define BLIT_RASTER_CACHE_WAYS 8

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

/* ADR 0220, 0221: the icon pen of a cell, the pen Blit's colour icons
   (U+100000 and up that carry colour) are drawn in. It is a per-lookup
   argument, not a raster setting: the terminal's cells carry it (SGR 68), and
   the values are the cell attribute's field. Colour (zero) draws the icon in
   its own colours. The other three draw its monochrome shape, which the icon
   font carries (levels below the signal level left out, knockouts cut away):
   auto is a coverage glyph, so the cell's foreground draws it and the cell's
   background shows through the knockouts, as it does for the text beside it;
   black is exactly black and white exactly white. Every other glyph (window
   glyphs, controls, text, emoji, an icon with no colour) ignores the pen and
   is cached once. */
typedef enum { BLIT_RASTER_ICON_COLOUR = 0, BLIT_RASTER_ICON_AUTO, BLIT_RASTER_ICON_BLACK,
               BLIT_RASTER_ICON_WHITE } BlitRasterIconPen;

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
/* ADR 0241: the face Arabic words are shaped in (Noto Naskh Arabic), for
   any text font, pixel fonts included. Once, before any lookup; 0 leaves
   the raster unchanged. With it, runs of Arabic cells are drawn as slices
   of their shaped word; without it, cell by cell with the companion. */
int blit_raster_set_arabic_words(BlitRaster *raster, const char *path);
const char *blit_raster_arabic_words_path(const BlitRaster *raster);
const char *blit_raster_arabic_words_family(const BlitRaster *raster);
/* ADR 0241: cell `index` of a run's shaped word, as a glyph whose pixels
   are a view into the word's canvas: one cell wide, the canvas's stride,
   no sideways offset, `ink_offset_y` the canvas's top (it may overhang a
   row above and below). index -1 is the whole canvas. Bold is made on the
   whole word; the renderer adds no overstrike. `serial` names the canvas
   and variant, never reused. 0: no word face, or the face lacks a scalar
   of the word (draw its cells one by one). Pixels stay valid until the
   next word lookup or close. */
struct BlitCellWord;
int blit_raster_get_word(BlitRaster *raster, const struct BlitCellWord *word, int index, int bold,
                         BlitRasterGlyph *glyph, unsigned long long *serial);
void blit_raster_word_counts(const BlitRaster *raster, unsigned long long *hits,
                             unsigned long long *misses, size_t *bytes,
                             unsigned long long *dropped);
int blit_raster_baseline(const BlitRaster *raster);
/* ADR 0227: the design cell of the master a font of masters is drawn with,
   and whether the cell height is a whole multiple of it (crisp) or not
   (scaled); NULL for any other font. */
const BlitFontGrid *blit_raster_master(const BlitRaster *raster, int *crisp);
/* pen: the cell's icon pen (above); an unknown pen returns 0. */
int blit_raster_get(BlitRaster *raster, uint32_t scalar, int columns,
                    BlitRasterIconPen pen, BlitRasterGlyph *glyph);
/* ADR 0240: the glyph a bold cell draws, before the renderer's one-pixel
   overstrike. The regular glyph, except one whose ink reaches its last
   column, which comes narrowed by a column so the overstrike keeps its
   shape. bold 0 is blit_raster_get. */
int blit_raster_get_styled(BlitRaster *raster, uint32_t scalar, int columns,
                           BlitRasterIconPen pen, int bold, BlitRasterGlyph *glyph);
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
int blit_raster_get_cluster_styled(BlitRaster *raster, const BlitCellCluster *cluster,
                                   int bold, BlitRasterGlyph *glyph);
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
                                BlitFontGrid *grid, char *reason, size_t reason_size);

#endif
