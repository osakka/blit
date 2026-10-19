#ifndef BLIT_FONT_H
#define BLIT_FONT_H

#include <stddef.h>
#include <stdint.h>

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
    void (*raster_free)(void *);
    uint32_t (*raster_blend)(const struct BlitRasterGlyph *, int, int,
                            uint32_t, uint32_t);
    int raster_cell_width, raster_cell_height;
} BlitFont;

void blit_font_load(BlitFont *font);
/* Load the 16x16 font used for wide cells (ADR 0062). Returns 0 on success;
   a failure is not fatal and leaves wide cells on the primary font. */
int blit_font_load_wide(BlitFont *font);
int blit_font_load_path(BlitFont *font, const char *path);
void blit_font_free(BlitFont *font);
int blit_font_profile_count(void);
const char *blit_font_profile_id(int index);
const char *blit_font_profile_path(int index);
int blit_font_profile_available(int index);
const char *blit_font_profile_for_path(const char *path);
int blit_font_has_glyph(const BlitFont *font, uint32_t codepoint);
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
