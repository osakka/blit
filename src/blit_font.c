#include "blit_font.h"
#include "blit_raster.h"

#include <unistd.h>
#include <strings.h>
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
    if (font->raster_free && font->raster_state)
        font->raster_free(font->raster_state);
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

/* ADR 0195: the font list is discovered, not written by hand.
 *
 * One fonts root (BLIT_FONT_ROOT, else /usr/lib/blit/fonts) holds the glyph
 * stack, the emoji and Arabic faces, the pixel fonts, and text/, the shipped
 * text fonts. BLIT_FONT_LOCAL_ROOT (else /usr/local/lib/blit/fonts) may hold
 * an operator's own text/. A text font is any regular, scalable, fixed-width
 * TTF or OTF with a Unicode charmap; its id is its family name in lower-case
 * kebab form and its title the family name. A pixel font is a PSF in the
 * root whose cells are narrower than tall (the square one is the wide-cell
 * font, ADR 0062). What fails is listed with its reason. Each file is opened
 * once and remembered by size and mtime, so a font added later appears on
 * the next listing without a restart. */
#include <ctype.h>
#include <dirent.h>
#include <sys/stat.h>

#define BLIT_FONT_MAX_FILES 96

typedef struct {
    char path[512];
    char id[64];
    char title[96];
    char reason[96];   /* empty for a usable font */
    int outline;
    int local;
    off_t size;
    time_t mtime;
} BlitFontFile;

static BlitFontFile font_files[BLIT_FONT_MAX_FILES];
static int font_file_count;

const char *blit_font_root(void)
{
    const char *root = getenv("BLIT_FONT_ROOT");
    return root && *root ? root : "/usr/lib/blit/fonts";
}

static const char *font_local_root(void)
{
    const char *root = getenv("BLIT_FONT_LOCAL_ROOT");
    return root && *root ? root : "/usr/local/lib/blit/fonts";
}

const char *blit_font_root_file(const char *name)
{
    static char paths[4][1024];
    static int next;
    char *path = paths[next++ & 3];
    snprintf(path, 1024, "%s/%s", blit_font_root(), name);
    return path;
}

int blit_font_is_outline(const char *path)
{
    size_t length = path ? strlen(path) : 0;
    return length > 4 && (strcasecmp(path + length - 4, ".ttf") == 0 ||
                          strcasecmp(path + length - 4, ".otf") == 0);
}

static void font_kebab(const char *name, char *id, size_t capacity)
{
    size_t used = 0;
    int dash = 0;
    for (; *name && used + 1 < capacity; ++name) {
        unsigned char c = (unsigned char)*name;
        if (isalnum(c)) {
            if (dash && used) id[used++] = '-';
            if (used + 1 < capacity) id[used++] = (char)tolower(c);
            dash = 0;
        } else dash = 1;
    }
    id[used] = '\0';
}

/* Outline fonts are read by the native raster (blit_raster.c), which owns
   FreeType; the PSF loader and the standalone KMS gates stay free of it. */
static BlitFontOutlineInspector font_outline_inspector;

void blit_font_set_outline_inspector(BlitFontOutlineInspector inspector)
{
    font_outline_inspector = inspector;
}

static void font_inspect_outline(BlitFontFile *file)
{
    char family[96];
    if (!font_outline_inspector) {
        snprintf(file->reason, sizeof(file->reason), "no outline font engine");
        return;
    }
    family[0] = '\0';
    if (!font_outline_inspector(file->path, family, sizeof(family),
                                file->reason, sizeof(file->reason)))
        return;
    snprintf(file->title, sizeof(file->title), "%s", family);
    font_kebab(family, file->id, sizeof(file->id));
}

static void font_inspect_pixel(BlitFontFile *file)
{
    BlitFont font;
    const char *base = strrchr(file->path, '/');
    char name[96];
    size_t length;
    memset(&font, 0, sizeof(font));
    if (font_load_file(&font, file->path) != 0) {
        snprintf(file->reason, sizeof(file->reason), "not a readable PSF");
        return;
    }
    if (font.width >= font.height)
        snprintf(file->reason, sizeof(file->reason), "a wide-cell font");
    blit_font_free(&font);
    if (file->reason[0]) return;
    snprintf(name, sizeof(name), "%s", base ? base + 1 : file->path);
    length = strlen(name);
    if (length > 4) name[length - 4] = '\0';
    font_kebab(name, file->id, sizeof(file->id));
    snprintf(file->title, sizeof(file->title), "%s", name);
    for (length = 0; file->title[length]; ++length)
        if (file->title[length] == '-') file->title[length] = ' ';
}

static int font_name_order(const void *a, const void *b)
{
    return strcmp(*(char *const *)a, *(char *const *)b);
}

/* One directory's candidate files, sorted, appended to the new list. */
static void font_scan(const char *directory, int outline, int local,
                      const BlitFontFile *previous, int previous_count,
                      BlitFontFile *next, int *count)
{
    DIR *dir = opendir(directory);
    struct dirent *entry;
    char *names[BLIT_FONT_MAX_FILES];
    int found = 0, i, j;
    if (!dir) return;
    while ((entry = readdir(dir)) != NULL && found < BLIT_FONT_MAX_FILES) {
        size_t length = strlen(entry->d_name);
        int wanted = outline ? blit_font_is_outline(entry->d_name) :
            length > 4 && strcmp(entry->d_name + length - 4, ".psf") == 0;
        if (wanted && entry->d_name[0] != '.') names[found++] = strdup(entry->d_name);
    }
    closedir(dir);
    qsort(names, (size_t)found, sizeof(names[0]), font_name_order);
    for (i = 0; i < found; ++i) {
        BlitFontFile file;
        struct stat info;
        memset(&file, 0, sizeof(file));
        snprintf(file.path, sizeof(file.path), "%s/%s", directory, names[i]);
        free(names[i]);
        if (*count >= BLIT_FONT_MAX_FILES || stat(file.path, &info) != 0 ||
            !S_ISREG(info.st_mode)) continue;
        file.outline = outline;
        file.local = local;
        file.size = info.st_size;
        file.mtime = info.st_mtime;
        for (j = 0; j < previous_count; ++j)
            if (!strcmp(previous[j].path, file.path) && previous[j].size == file.size &&
                previous[j].mtime == file.mtime) break;
        if (j < previous_count) file = previous[j];
        else if (outline) font_inspect_outline(&file);
        else font_inspect_pixel(&file);
        /* The first font to claim an id keeps it: the shipped set before an
           operator's own. A later file of the same family is reported. */
        if (!file.reason[0])
            for (j = 0; j < *count; ++j)
                if (!next[j].reason[0] && !strcmp(next[j].id, file.id)) {
                    snprintf(file.reason, sizeof(file.reason), "same family as %s",
                             strrchr(next[j].path, '/') + 1);
                    break;
                }
        next[(*count)++] = file;
    }
}

void blit_font_discover(void)
{
    static BlitFontFile next[BLIT_FONT_MAX_FILES];
    char directory[1024];
    int count = 0;
    snprintf(directory, sizeof(directory), "%s", blit_font_root());
    font_scan(directory, 0, 0, font_files, font_file_count, next, &count);
    snprintf(directory, sizeof(directory), "%s/text", blit_font_root());
    font_scan(directory, 1, 0, font_files, font_file_count, next, &count);
    snprintf(directory, sizeof(directory), "%s/text", font_local_root());
    font_scan(directory, 1, 1, font_files, font_file_count, next, &count);
    memcpy(font_files, next, sizeof(next[0]) * (size_t)count);
    font_file_count = count;
}

/* The usable fonts are the profiles; index i is the i-th usable file. */
static const BlitFontFile *font_profile(int index)
{
    int i, seen = 0;
    if (!font_file_count) blit_font_discover();
    for (i = 0; i < font_file_count; ++i)
        if (!font_files[i].reason[0] && seen++ == index) return &font_files[i];
    return NULL;
}

/* The 16x16 font for wide cells (ADR 0062). It is a separate artifact because
   PSF2 carries one cell size per file, so it is not a profile. */
int blit_font_load_wide(BlitFont *font)
{
    const char *configured = getenv("BLIT_PSF_WIDE_FONT");

    if (font == NULL) return -1;
    if (configured != NULL && configured[0] != '\0')
        return blit_font_load_path(font, configured);
    if (blit_font_load_path(font, blit_font_root_file("Blit-Unifont16.psf")) == 0)
        return 0;
    /* Not fatal: a wide cell falls back to the primary font's substitute,
       which is what every release before ADR 0062 did. */
    fprintf(stderr, "blit: wide font unavailable; wide cells fall back to the "
                    "primary font\n");
    return -1;
}

int blit_font_profile_count(void)
{
    int i, count = 0;
    if (!font_file_count) blit_font_discover();
    for (i = 0; i < font_file_count; ++i) count += !font_files[i].reason[0];
    return count;
}

const char *blit_font_profile_id(int index)
{
    const BlitFontFile *file = font_profile(index);
    return file ? file->id : NULL;
}

const char *blit_font_profile_title(int index)
{
    const BlitFontFile *file = font_profile(index);
    return file ? file->title : NULL;
}

const char *blit_font_profile_path(int index)
{
    const BlitFontFile *file = font_profile(index);
    return file ? file->path : NULL;
}

int blit_font_profile_local(int index)
{
    const BlitFontFile *file = font_profile(index);
    return file ? file->local : 0;
}

int blit_font_rejected_count(void)
{
    int i, count = 0;
    for (i = 0; i < font_file_count; ++i) count += font_files[i].reason[0] != 0;
    return count;
}

const char *blit_font_rejected(int index, const char **reason)
{
    int i, seen = 0;
    for (i = 0; i < font_file_count; ++i)
        if (font_files[i].reason[0] && seen++ == index) {
            if (reason) *reason = font_files[i].reason;
            return font_files[i].path;
        }
    return NULL;
}

const char *blit_font_profile_for_path(const char *path)
{
    int i;
    if (path == NULL) return "custom";
    for (i = 0; i < blit_font_profile_count(); ++i)
        if (strcmp(path, blit_font_profile_path(i)) == 0)
            return blit_font_profile_id(i);
    return "custom";
}

/* ADR 0195: one Arabic companion serves every outline font. */
const char *blit_font_arabic_path(const char *primary)
{
    return blit_font_is_outline(primary) ? blit_font_root_file("BlitArabicModern.ttf") : NULL;
}

int blit_font_profile_available(int index)
{
    static const char *const needed[] = {
        "NotoColorEmoji.ttf", "NotoEmojiText.ttf", "Blit-Terminus16.psf", "BlitArabicModern.ttf"
    };
    const char *path = blit_font_profile_path(index);
    size_t i;
    if (!path || access(path, R_OK) != 0) return 0;
    if (!blit_font_is_outline(path)) return 1;
    for (i = 0; i < sizeof(needed) / sizeof(needed[0]); ++i)
        if (access(blit_font_root_file(needed[i]), R_OK) != 0) return 0;
    return 1;
}

int blit_font_load_path(BlitFont *font, const char *path)
{
    char fallback[512];
    const char *source = path;
    if (font == NULL || path == NULL || path[0] == '\0') return -1;
    blit_font_free(font);
    if (blit_font_is_outline(path)) {
        /* An outline font's canonical PSF fallback is the root's Terminus. */
        if (access(path, R_OK) != 0) return -1;
        snprintf(fallback, sizeof(fallback), "%s", blit_font_root_file("Blit-Terminus16.psf"));
        source = fallback;
    }
    if (font_load_file(font, source) != 0) return -1;
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
    if (blit_font_load_path(font, blit_font_root_file("Blit-Terminus16.psf")) == 0) return;
    for (i = 0; i < blit_font_profile_count(); ++i)
        if (blit_font_load_path(font, blit_font_profile_path(i)) == 0) return;
    fprintf(stderr, "blit: font unavailable; using built-in fallback\n");
}

/* Whether this font maps this codepoint to a glyph of its own.
   blit_font_glyph_index cannot answer that: it returns the '?' substitute, or
   glyph zero for a font that has no '?', so "it returned something" says
   nothing. Choosing between the primary and the wide font needs the real
   answer (ADR 0062). */
const BlitFont *blit_font_cell_source(const BlitFont *font,
                                     const BlitFont *wide, uint32_t codepoint)
{
    BlitRasterGlyph glyph;
    if (!wide || !blit_font_has_glyph(wide, codepoint)) return font;
    if (font && font->raster_get &&
        font->raster_get(font->raster_state, codepoint, 2, &glyph) &&
        !glyph.fallback) return font;
    return wide;
}

int blit_font_has_glyph(const BlitFont *font, uint32_t codepoint)
{
    BlitFontGlyphMap *map;
    size_t i;

    if (font == NULL || font->glyph_count == 0) return 0;
    if (codepoint < 256)
        return font->ascii_glyph[codepoint] >= 0;
    map = (BlitFontGlyphMap *)font->unicode_lookup;
    if (map != NULL) {
        size_t slot = glyph_hash(codepoint) &
            (font->unicode_lookup_capacity - 1);
        while (map[slot].codepoint != 0xffffffffU) {
            if (map[slot].codepoint == codepoint) return 1;
            slot = (slot + 1) & (font->unicode_lookup_capacity - 1);
        }
        return 0;
    }
    map = (BlitFontGlyphMap *)font->unicode_map;
    for (i = 0; i < font->unicode_count; ++i)
        if (map[i].codepoint == codepoint) return 1;
    if (font->codepoints != NULL)
        for (i = 0; i < font->glyph_count; ++i)
            if (font->codepoints[i] == codepoint) return 1;
    return 0;
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
