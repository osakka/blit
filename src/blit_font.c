#include "blit_font.h"

#include <unistd.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct BlitFontGlyphMap {
    uint32_t codepoint;
    size_t glyph;
} BlitFontGlyphMap;

static uint32_t read_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | ((uint32_t)p[1] << 8) |
           ((uint32_t)p[2] << 16) | ((uint32_t)p[3] << 24);
}

static int font_utf8(const unsigned char *p, size_t length, uint32_t *value,
                     size_t *used)
{
    if (length == 0) return 0;
    if (p[0] < 0x80) { *value = p[0]; *used = 1; return 1; }
    if ((p[0] & 0xe0) == 0xc0 && length >= 2) {
        *value = ((uint32_t)(p[0] & 0x1f) << 6) | (p[1] & 0x3f);
        *used = 2; return 1;
    }
    if ((p[0] & 0xf0) == 0xe0 && length >= 3) {
        *value = ((uint32_t)(p[0] & 0x0f) << 12) |
            ((uint32_t)(p[1] & 0x3f) << 6) | (p[2] & 0x3f);
        *used = 3; return 1;
    }
    if ((p[0] & 0xf8) == 0xf0 && length >= 4) {
        *value = ((uint32_t)(p[0] & 7) << 18) | ((uint32_t)(p[1] & 0x3f) << 12) |
            ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        *used = 4; return 1;
    }
    return 0;
}

static size_t glyph_hash(uint32_t codepoint)
{
    uint32_t value = codepoint;
    value ^= value >> 16;
    value *= 0x7feb352dU;
    value ^= value >> 15;
    value *= 0x846ca68bU;
    value ^= value >> 16;
    return (size_t)value;
}

void blit_font_free(BlitFont *font)
{
    if (font == NULL) return;
    free(font->codepoints);
    free(font->row_cache);
    free(font->unicode_map);
    free(font->unicode_lookup);
    free(font->data);
    memset(font, 0, sizeof(*font));
}

static void font_build_row_cache(BlitFont *font)
{
    size_t count;
    size_t glyph;
    int row;
    size_t row_bytes;
    if (font == NULL || font->data == NULL || font->width > 32 ||
        font->height < 1 || font->glyph_count == 0)
        return;
    row_bytes = (size_t)((font->width + 7) / 8);
    if (font->glyph_bytes < row_bytes * (size_t)font->height)
        return;
    count = font->glyph_count * (size_t)font->height;
    font->row_cache = (uint32_t *)malloc(count * sizeof(*font->row_cache));
    if (font->row_cache == NULL)
        return;
    font->row_cache_count = count;
    for (glyph = 0; glyph < font->glyph_count; ++glyph) {
        const unsigned char *source = font->data + font->glyph_offset +
            glyph * font->glyph_bytes;
        for (row = 0; row < font->height; ++row) {
            uint32_t bits = 0;
            int column;
            for (column = 0; column < font->width; ++column)
                if (source[row * row_bytes + column / 8] &
                    (0x80 >> (column % 8)))
                    bits |= 1U << (font->width - column - 1);
            font->row_cache[glyph * (size_t)font->height + (size_t)row] =
                bits;
        }
    }
}

static int font_add_mapping(BlitFont *font, uint32_t codepoint, size_t glyph)
{
    BlitFontGlyphMap *next;
    size_t capacity;
    if (font == NULL || glyph >= font->glyph_count) return 0;
    if (font->unicode_count == font->unicode_capacity) {
        capacity = font->unicode_capacity ? font->unicode_capacity * 2 : 256;
        next = (BlitFontGlyphMap *)realloc(font->unicode_map,
                                           capacity * sizeof(*next));
        if (next == NULL) return 0;
        font->unicode_map = (struct BlitFontGlyphMap *)next;
        font->unicode_capacity = capacity;
    }
    next = (BlitFontGlyphMap *)font->unicode_map;
    next[font->unicode_count].codepoint = codepoint;
    next[font->unicode_count].glyph = glyph;
    if (codepoint < 256) font->ascii_glyph[codepoint] = (int)glyph;
    ++font->unicode_count;
    return 1;
}

static void font_build_lookup(BlitFont *font)
{
    BlitFontGlyphMap *map;
    size_t capacity;
    size_t i;
    if (font == NULL || font->unicode_count == 0) return;
    capacity = 1;
    while (capacity < font->unicode_count * 2) capacity <<= 1;
    map = (BlitFontGlyphMap *)malloc(capacity * sizeof(*map));
    if (map == NULL) return;
    font->unicode_lookup = (struct BlitFontGlyphMap *)map;
    font->unicode_lookup_capacity = capacity;
    for (i = 0; i < capacity; ++i) map[i].codepoint = 0xffffffffU;
    for (i = 0; i < font->unicode_count; ++i) {
        BlitFontGlyphMap *source = (BlitFontGlyphMap *)font->unicode_map;
        size_t slot = glyph_hash(source[i].codepoint) & (capacity - 1);
        while (map[slot].codepoint != 0xffffffffU) {
            if (map[slot].codepoint == source[i].codepoint) break;
            slot = (slot + 1) & (capacity - 1);
        }
        if (map[slot].codepoint == 0xffffffffU) map[slot] = source[i];
    }
}

static int font_load_file(BlitFont *font, const char *path)
{
    FILE *file;
    long file_size;
    unsigned char *data;
    uint32_t magic;
    size_t i;
    file = fopen(path, "rb");
    if (file == NULL) return -1;
    if (fseek(file, 0, SEEK_END) != 0) { fclose(file); return -1; }
    file_size = ftell(file);
    if (file_size <= 0 || fseek(file, 0, SEEK_SET) != 0) {
        fclose(file); return -1;
    }
    data = (unsigned char *)malloc((size_t)file_size);
    if (data == NULL || fread(data, 1, (size_t)file_size, file) !=
        (size_t)file_size) {
        free(data); fclose(file); return -1;
    }
    fclose(file);
    magic = (uint32_t)data[0] | ((uint32_t)data[1] << 8) |
        ((uint32_t)data[2] << 16) | ((uint32_t)data[3] << 24);
    memset(font, 0, sizeof(*font));
    font->data = data;
    font->size = (size_t)file_size;
    if (data[0] == 0x36 && data[1] == 0x04 && file_size >= 4) {
        font->width = 8;
        font->height = data[3];
        font->glyph_bytes = data[3];
        font->glyph_count = (size_t)(data[2] & 1 ? 512 : 256);
        font->glyph_offset = 4;
        if (font->glyph_offset + font->glyph_bytes * font->glyph_count >
            (size_t)file_size)
            font->glyph_count = ((size_t)file_size - font->glyph_offset) /
                font->glyph_bytes;
    } else if (magic == 0x864ab572U && file_size >= 32) {
        uint32_t header = read_u32(data + 8);
        uint32_t count = read_u32(data + 16);
        uint32_t bytes = read_u32(data + 20);
        font->width = (int)read_u32(data + 28);
        font->height = (int)read_u32(data + 24);
        font->glyph_count = count;
        font->glyph_bytes = bytes;
        font->glyph_offset = header;
        if (header < 32 || bytes == 0 || count == 0 ||
            header + (size_t)bytes * count > (size_t)file_size) {
            blit_font_free(font); return -1;
        }
    } else { blit_font_free(font); return -1; }
    if (font->width < 1 || font->width > 32 || font->height < 1 ||
        font->height > 64 || font->glyph_count == 0) {
        blit_font_free(font); return -1;
    }
    font->codepoints = (uint32_t *)malloc(font->glyph_count * sizeof(uint32_t));
    if (font->codepoints == NULL) { blit_font_free(font); return -1; }
    for (i = 0; i < 256; ++i) font->ascii_glyph[i] = -1;
    for (i = 0; i < font->glyph_count; ++i) font->codepoints[i] = (uint32_t)i;
    if (data[0] == 0x36 && data[1] == 0x04 && (data[2] & 2)) {
        size_t cursor = font->glyph_offset + font->glyph_bytes * font->glyph_count;
        size_t glyph = 0;
        while (cursor + 1 < (size_t)file_size && glyph < font->glyph_count) {
            uint32_t value = (uint32_t)data[cursor] |
                ((uint32_t)data[cursor + 1] << 8);
            cursor += 2;
            if (value == 0xffffU) ++glyph;
            else if (value != 0xfffeU && !font_add_mapping(font, value, glyph)) {
                blit_font_free(font); return -1;
            }
        }
    } else if (magic == 0x864ab572U && (read_u32(data + 12) & 1U)) {
        size_t cursor = font->glyph_offset + font->glyph_bytes * font->glyph_count;
        size_t glyph = 0;
        while (cursor < (size_t)file_size && glyph < font->glyph_count) {
            if (data[cursor] == 0xff) { ++glyph; ++cursor; continue; }
            if (data[cursor] == 0xfe) { ++cursor; continue; }
            {
                uint32_t value;
                size_t used;
                if (font_utf8(data + cursor, (size_t)file_size - cursor,
                              &value, &used)) {
                    if (!font_add_mapping(font, value, glyph)) {
                        blit_font_free(font); return -1;
                    }
                    cursor += used;
                } else ++cursor;
            }
        }
    }
    for (i = 0; i < font->glyph_count && i < 256; ++i)
        if (font->ascii_glyph[i] < 0) font->ascii_glyph[i] = (int)i;
    font_build_lookup(font);
    font_build_row_cache(font);
    return 0;
}

typedef struct {
    const char *id;
    const char *path;
} BlitFontProfile;

static const BlitFontProfile blit_font_profiles[] = {
    {"blit-hibr-terminus16", "/usr/lib/blit/fonts/Blit-Hibr-Terminus16.psf"},
    {"uni2-terminus16", "/usr/lib/blit/fonts/Uni2-Terminus16.psf"},
    {"lat15-terminus16", "/usr/lib/blit/fonts/Lat15-Terminus16.psf"},
    {"uni2-console", "/usr/share/consolefonts/Uni2-Terminus16.psf"},
    {"uni2-blit", "/usr/share/blit/Uni2-Terminus16.psf"},
    {"uni2-fixed16", "/usr/share/consolefonts/Uni2-Fixed16.psf"},
    {"lat15-console", "/usr/share/consolefonts/Lat15-Terminus16.psf"}
};

int blit_font_profile_count(void)
{
    return (int)(sizeof(blit_font_profiles) / sizeof(blit_font_profiles[0]));
}

const char *blit_font_profile_id(int index)
{
    if (index < 0 || index >= blit_font_profile_count()) return NULL;
    return blit_font_profiles[index].id;
}

const char *blit_font_profile_path(int index)
{
    if (index < 0 || index >= blit_font_profile_count()) return NULL;
    return blit_font_profiles[index].path;
}

const char *blit_font_profile_for_path(const char *path)
{
    int i;
    if (path == NULL) return "custom";
    for (i = 0; i < blit_font_profile_count(); ++i)
        if (strcmp(path, blit_font_profiles[i].path) == 0)
            return blit_font_profiles[i].id;
    return "custom";
}

int blit_font_load_path(BlitFont *font, const char *path)
{
    if (font == NULL || path == NULL || path[0] == '\0') return -1;
    blit_font_free(font);
    if (font_load_file(font, path) != 0) return -1;
    snprintf(font->path, sizeof(font->path), "%s", path);
    fprintf(stderr, "blit: font=%s size=%dx%d glyphs=%lu mappings=%lu\n",
            path, font->width, font->height,
            (unsigned long)font->glyph_count,
            (unsigned long)font->unicode_count);
    return 0;
}

void blit_font_load(BlitFont *font)
{
    const char *configured = getenv("BLIT_PSF_FONT");
    int i;
    if (font == NULL) return;
    if (configured != NULL && configured[0] != '\0' &&
        blit_font_load_path(font, configured) == 0) return;
    for (i = 0; i < blit_font_profile_count(); ++i)
        if (blit_font_load_path(font, blit_font_profile_path(i)) == 0) return;
    fprintf(stderr, "blit: font unavailable; using built-in fallback\n");
}

size_t blit_font_glyph_index(const BlitFont *font, uint32_t codepoint)
{
    BlitFontGlyphMap *map;
    size_t i;
    if (font == NULL || font->glyph_count == 0) return 0;
    if (codepoint < 256 && font->ascii_glyph[codepoint] >= 0)
        return (size_t)font->ascii_glyph[codepoint];
    map = (BlitFontGlyphMap *)font->unicode_lookup;
    if (map != NULL) {
        size_t slot = glyph_hash(codepoint) & (font->unicode_lookup_capacity - 1);
        while (map[slot].codepoint != 0xffffffffU) {
            if (map[slot].codepoint == codepoint) return map[slot].glyph;
            slot = (slot + 1) & (font->unicode_lookup_capacity - 1);
        }
    } else {
        map = (BlitFontGlyphMap *)font->unicode_map;
        for (i = 0; i < font->unicode_count; ++i)
            if (map[i].codepoint == codepoint) return map[i].glyph;
    }
    for (i = 0; i < font->glyph_count; ++i)
        if (font->codepoints[i] == codepoint) break;
    if (i != font->glyph_count)
        return i;
    /* An unmapped Unicode scalar must remain visibly identifiable.  Using
       glyph zero for codepoints outside the PSF range silently produces a
       blank cell for fonts whose first glyph is NUL. */
    if (font->ascii_glyph[(unsigned char)'?'] >= 0)
        return (size_t)font->ascii_glyph[(unsigned char)'?'];
    return 0;
}

static int fallback_pixel(uint32_t codepoint, int row, int column)
{
    static const unsigned char digits[10][7] = {
        {14,17,19,21,25,17,14},{4,12,4,4,4,4,14},
        {14,17,1,2,4,8,31},{30,1,1,14,1,1,30},
        {2,6,10,18,31,2,2},{31,16,16,30,1,1,30},
        {14,16,16,30,17,17,14},{31,1,2,4,8,8,8},
        {14,17,17,14,17,17,14},{14,17,17,15,1,1,14}
    };
    static const unsigned char letters[26][7] = {
        {14,17,17,31,17,17,17},{30,17,17,30,17,17,30},
        {14,17,16,16,16,17,14},{30,17,17,17,17,17,30},
        {31,16,16,30,16,16,31},{31,16,16,30,16,16,16},
        {14,17,16,23,17,17,14},{17,17,17,31,17,17,17},
        {14,4,4,4,4,4,14},{7,2,2,2,18,18,12},
        {17,18,20,24,20,18,17},{16,16,16,16,16,16,31},
        {17,27,21,21,17,17,17},{17,25,21,19,17,17,17},
        {14,17,17,17,17,17,14},{30,17,17,30,16,16,16},
        {14,17,17,17,21,18,13},{30,17,17,30,20,18,17},
        {15,16,16,14,1,1,30},{31,4,4,4,4,4,4},
        {17,17,17,17,17,17,14},{17,17,17,17,17,10,4},
        {17,17,17,21,21,27,17},{17,17,10,4,10,17,17},
        {17,17,10,4,4,4,4},{31,1,2,4,8,16,31}
    };
    if (codepoint >= '0' && codepoint <= '9' && row < 7 && column < 5)
        return (digits[codepoint - '0'][row] >> (4 - column)) & 1;
    if (codepoint >= 'A' && codepoint <= 'Z' && row < 7 && column < 5)
        return (letters[codepoint - 'A'][row] >> (4 - column)) & 1;
    if (codepoint >= 'a' && codepoint <= 'z' && row < 7 && column < 5)
        return (letters[codepoint - 'a'][row] >> (4 - column)) & 1;
    if (codepoint == '.' && row == 6 && column == 2) return 1;
    if (codepoint == ':' && (row == 2 || row == 5) && column == 2) return 1;
    if (codepoint == '-' && row == 3 && column >= 1 && column <= 3) return 1;
    return 0;
}

uint32_t blit_font_glyph_row(const BlitFont *font, size_t glyph,
                             uint32_t codepoint, int row)
{
    uint32_t bits = 0;
    int width = font != NULL && font->data != NULL && font->width > 0 ?
        font->width : 5;
    int column;
    if (font != NULL && font->data != NULL && row >= 0 &&
        row < font->height && width <= 32) {
        if (font->row_cache != NULL && glyph < font->glyph_count &&
            glyph * (size_t)font->height + (size_t)row <
            font->row_cache_count)
            return font->row_cache[glyph * (size_t)font->height +
                                   (size_t)row];
        size_t row_bytes = (size_t)((width + 7) / 8);
        const unsigned char *source = font->data + font->glyph_offset +
            glyph * font->glyph_bytes + row * row_bytes;
        for (column = 0; column < width; ++column)
            if (source[column / 8] & (0x80 >> (column % 8)))
                bits |= 1U << (width - column - 1);
        return bits;
    }
    if (width > 32)
        return 0;
    for (column = 0; column < width; ++column)
        if (fallback_pixel(codepoint, row, column))
            bits |= 1U << (width - column - 1);
    return bits;
}

int blit_font_glyph_pixel(const BlitFont *font, size_t glyph,
                          uint32_t codepoint, int row, int column)
{
    if (font == NULL || font->data == NULL)
        return fallback_pixel(codepoint, row, column);
    if (font->width <= 0 || font->width > 32 || row < 0 || column < 0 ||
        row >= font->height || column >= font->width)
        return 0;
    return (blit_font_glyph_row(font, glyph, codepoint, row) &
            (1U << (font->width - column - 1))) != 0;
}

int blit_font_pixel(const BlitFont *font, uint32_t codepoint, int row, int column)
{
    return blit_font_glyph_pixel(font, blit_font_glyph_index(font, codepoint),
                                 codepoint, row, column);
}

int blit_font_width(const BlitFont *font)
{
    return font != NULL && font->width > 0 ? font->width : 8;
}

int blit_font_height(const BlitFont *font)
{
    return font != NULL && font->height > 0 ? font->height : 8;
}
