#ifndef BLIT_FONT_H
#define BLIT_FONT_H

#include <stddef.h>
#include <stdint.h>
#include "blit_cell_cluster.h"

struct BlitRasterGlyph;
typedef struct BlitFont {
    unsigned char *data;
    size_t size;
    size_t glyph_offset;
    size_t glyph_bytes;
    size_t glyph_count;
    int width;
    int height;
    uint32_t *codepoints;
    uint32_t *row_cache;
    size_t row_cache_count;
    int ascii_glyph[256];
    struct BlitFontGlyphMap *unicode_map;
    size_t unicode_count;
    size_t unicode_capacity;
    struct BlitFontGlyphMap *unicode_lookup;
    size_t unicode_lookup_capacity;
    char path[256];
    /* Optional shared native-size rasterizer. Function pointers keep the PSF
       loader and standalone legacy KMS gates independent of FreeType. */
    void *raster_state;
    int (*raster_get)(void *, uint32_t, int, struct BlitRasterGlyph *);
    int (*raster_cluster_get)(void *, const BlitCellCluster *, struct BlitRasterGlyph *);
    void (*raster_free)(void *);
    uint32_t (*raster_blend)(const struct BlitRasterGlyph *, int, int,
                            uint32_t, uint32_t);
    int raster_cell_width, raster_cell_height;
    int raster_max_width, raster_max_height;
    /* Maximum ink outside the advance rectangle, in whole cells. */
    int raster_ink_left, raster_ink_right, raster_ink_top, raster_ink_bottom;
} BlitFont;

void blit_font_load(BlitFont *font);
/* Load the 16x16 font used for wide cells (ADR 0062). Returns 0 on success;
   a failure is not fatal and leaves wide cells on the primary font. */
int blit_font_load_wide(BlitFont *font);
int blit_font_load_path(BlitFont *font, const char *path);
void blit_font_free(BlitFont *font);
/* ADR 0195: the font list is discovered from the fonts root (see
   blit_font.c). blit_font_discover rescans; the other calls read the last
   scan, scanning once if none was made. */
const char *blit_font_root(void);
const char *blit_font_root_file(const char *name);
int blit_font_is_outline(const char *path);
void blit_font_discover(void);
/* Reads an outline font: 1 with its family name when it is a usable text
   font, else 0 with the reason. Set by the native raster's owner; without
   one, outline files are listed as rejected. */
typedef int (*BlitFontOutlineInspector)(const char *path, char *family, size_t family_size,
                                        char *reason, size_t reason_size);
void blit_font_set_outline_inspector(BlitFontOutlineInspector inspector);
const char *blit_font_profile_title(int index);
int blit_font_profile_local(int index);
int blit_font_rejected_count(void);
const char *blit_font_rejected(int index, const char **reason);
int blit_font_profile_count(void);
const char *blit_font_profile_id(int index);
const char *blit_font_profile_path(int index);
int blit_font_profile_available(int index);
const char *blit_font_arabic_path(const char *primary);
const char *blit_font_profile_for_path(const char *path);
int blit_font_has_glyph(const BlitFont *font, uint32_t codepoint);
/* Pass wide only for a two-cell character. Prefer a real native outline or
   colour raster; retain the wide bitmap for genuinely unsupported scalars. */
const BlitFont *blit_font_cell_source(const BlitFont *font,
                                     const BlitFont *wide, uint32_t codepoint);
size_t blit_font_glyph_index(const BlitFont *font, uint32_t codepoint);
uint32_t blit_font_glyph_row(const BlitFont *font, size_t glyph,
                             uint32_t codepoint, int row);
int blit_font_glyph_pixel(const BlitFont *font, size_t glyph,
                          uint32_t codepoint, int row, int column);
int blit_font_width(const BlitFont *font);
int blit_font_height(const BlitFont *font);
int blit_font_pixel(const BlitFont *font, uint32_t codepoint,
                    int row, int column);

#endif
