#ifndef BLIT_FONT_H
#define BLIT_FONT_H

#include <stddef.h>
#include <stdint.h>

typedef struct {
    unsigned char *data;
    size_t size;
    size_t glyph_offset;
    size_t glyph_bytes;
    size_t glyph_count;
    int width;
    int height;
    uint32_t *codepoints;
    int ascii_glyph[256];
    struct BlitFontGlyphMap *unicode_map;
    size_t unicode_count;
    size_t unicode_capacity;
    struct BlitFontGlyphMap *unicode_lookup;
    size_t unicode_lookup_capacity;
} BlitFont;

void blit_font_load(BlitFont *font);
void blit_font_free(BlitFont *font);
int blit_font_width(const BlitFont *font);
int blit_font_height(const BlitFont *font);
int blit_font_pixel(const BlitFont *font, uint32_t codepoint,
                    int row, int column);

#endif
