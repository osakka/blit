#include "blit_raster.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include <stdlib.h>
#include <string.h>

#define RASTER_SLOTS 128
#define RASTER_MAX_BYTES (4U * 1024U * 1024U)

typedef struct {
    uint32_t scalar;
    int columns, valid;
    unsigned char *pixels;
    BlitRasterGlyph glyph;
} RasterEntry;

struct BlitRaster {
    const BlitFont *fallback;
    FT_Library library;
    FT_Face outline, colour;
    int width, height, em, baseline;
    size_t bytes;
    unsigned long renders;
    RasterEntry cache[RASTER_SLOTS];
};

static int ceil_26_6(FT_Pos value) { return (int)((value + 63) / 64); }

void blit_raster_close(BlitRaster *raster)
{
    int i;
    if (!raster) return;
    for (i = 0; i < RASTER_SLOTS; ++i) free(raster->cache[i].pixels);
    if (raster->outline) FT_Done_Face(raster->outline);
    if (raster->colour) FT_Done_Face(raster->colour);
    if (raster->library) FT_Done_FreeType(raster->library);
    free(raster);
}

BlitRaster *blit_raster_open(const BlitFont *fallback,
                            const char *outline_path, const char *colour_path,
                            int width, int height)
{
    BlitRaster *raster;
    if (!fallback || !fallback->data || width < 1 || width > 64 ||
        height < 8 || height > 64) return NULL;
    raster = calloc(1, sizeof(*raster));
    if (!raster) return NULL;
    raster->fallback = fallback;
    raster->width = width;
    raster->height = height;
    raster->baseline = height;
    if (!outline_path && !colour_path) return raster;
    if (FT_Init_FreeType(&raster->library)) goto fail;
    if (outline_path) {
        int em;
        if (FT_New_Face(raster->library, outline_path, 0, &raster->outline) ||
            !FT_IS_SCALABLE(raster->outline) ||
            !FT_IS_FIXED_WIDTH(raster->outline) ||
            FT_Select_Charmap(raster->outline, FT_ENCODING_UNICODE)) goto fail;
        /* API height means exact terminal cell height, not em size. Fit both
           ascent/descent and advance; do not stretch an already made bitmap. */
        for (em = height; em >= 1; --em) {
            int ascent, descent, advance;
            if (FT_Set_Pixel_Sizes(raster->outline, 0, em)) goto fail;
            ascent = ceil_26_6(raster->outline->size->metrics.ascender);
            descent = ceil_26_6(-raster->outline->size->metrics.descender);
            advance = ceil_26_6(raster->outline->size->metrics.max_advance);
            if (ascent + descent <= height && advance <= width) {
                raster->em = em;
                raster->baseline = (height - ascent - descent) / 2 + ascent;
                break;
            }
        }
        if (!raster->em) goto fail;
    }
    if (colour_path) {
        if (FT_New_Face(raster->library, colour_path, 0, &raster->colour) ||
            !FT_HAS_COLOR(raster->colour) || raster->colour->num_fixed_sizes < 1 ||
            FT_Select_Charmap(raster->colour, FT_ENCODING_UNICODE) ||
            FT_Select_Size(raster->colour, 0)) goto fail;
    }
    return raster;
fail:
    blit_raster_close(raster);
    return NULL;
}

static int protected_geometry(uint32_t scalar)
{
    /* Keep exact terminal geometry; outline fonts often leave seams here. */
    return scalar >= 0x2500 && scalar <= 0x259f;
}

static int draw_outline(BlitRaster *raster, uint32_t scalar, RasterEntry *entry)
{
    FT_Face face = raster->outline;
    FT_Bitmap *bitmap;
    int x, y, left, top;
    if (!face || !FT_Get_Char_Index(face, scalar)) return 0;
    if (FT_Load_Char(face, scalar, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL))
        return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays != 256 ||
        bitmap->pitch < 0) return -1;
    left = (entry->glyph.width - ceil_26_6(face->glyph->advance.x)) / 2 +
        face->glyph->bitmap_left;
    top = raster->baseline - face->glyph->bitmap_top;
    /* Never silently clip a glyph. Caller can use the canonical fallback. */
    if (left < 0 || top < 0 || left + (int)bitmap->width > entry->glyph.width ||
        top + (int)bitmap->rows > entry->glyph.height) return 0;
    for (y = 0; y < (int)bitmap->rows; ++y)
        for (x = 0; x < (int)bitmap->width; ++x)
            entry->pixels[(top + y) * entry->glyph.stride + left + x] =
                bitmap->buffer[y * bitmap->pitch + x];
    return 1;
}

static int draw_colour(BlitRaster *raster, uint32_t scalar, RasterEntry *entry)
{
    FT_Bitmap *bitmap;
    FT_Face face = raster->colour;
    int width, height, x, y, x0, y0;
    if (!face || !FT_Get_Char_Index(face, scalar)) return 0;
    if (FT_Load_Char(face, scalar, FT_LOAD_COLOR | FT_LOAD_RENDER)) return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_BGRA || bitmap->pitch < 0 ||
        !bitmap->width || !bitmap->rows) return -1;
    width = entry->glyph.width;
    height = (int)(bitmap->rows * width / bitmap->width);
    if (height > entry->glyph.height) {
        height = entry->glyph.height;
        width = (int)(bitmap->width * height / bitmap->rows);
    }
    if (width < 1 || height < 1) return -1;
    x0 = (entry->glyph.width - width) / 2;
    y0 = (entry->glyph.height - height) / 2;
    /* Area averaging premultiplied channels preserves transparent edges.
       The fixed colour strike is larger than the supported destination. */
    for (y = 0; y < height; ++y) {
        int sy0 = y * bitmap->rows / height;
        int sy1 = (y + 1) * bitmap->rows / height;
        if (sy1 <= sy0) sy1 = sy0 + 1;
        for (x = 0; x < width; ++x) {
            int sx0 = x * bitmap->width / width;
            int sx1 = (x + 1) * bitmap->width / width;
            unsigned sums[4] = {0, 0, 0, 0}, count = 0;
            int sx, sy, c;
            unsigned char *out = entry->pixels +
                (y + y0) * entry->glyph.stride + (x + x0) * 4;
            if (sx1 <= sx0) sx1 = sx0 + 1;
            for (sy = sy0; sy < sy1; ++sy)
                for (sx = sx0; sx < sx1; ++sx) {
                    const unsigned char *in = bitmap->buffer + sy * bitmap->pitch + sx * 4;
                    for (c = 0; c < 4; ++c) sums[c] += in[c];
                    ++count;
                }
            out[0] = (unsigned char)(sums[2] / count);
            out[1] = (unsigned char)(sums[1] / count);
            out[2] = (unsigned char)(sums[0] / count);
            out[3] = (unsigned char)(sums[3] / count);
        }
    }
    return 1;
}

int blit_raster_get(BlitRaster *raster, uint32_t scalar, int columns,
                    BlitRasterGlyph *glyph)
{
    RasterEntry next, *entry;
    size_t bytes, old_bytes;
    unsigned slot;
    int result = 0, x, y;
    if (!raster || !glyph || (columns != 1 && columns != 2) ||
        scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)) return 0;
    slot = (unsigned)((scalar * 2654435761U + (unsigned)columns) % RASTER_SLOTS);
    entry = &raster->cache[slot];
    if (entry->valid && entry->scalar == scalar && entry->columns == columns) {
        *glyph = entry->glyph;
        return 1;
    }
    memset(&next, 0, sizeof(next));
    next.scalar = scalar;
    next.columns = columns;
    next.glyph.width = raster->width * columns;
    next.glyph.height = raster->height;
    next.glyph.baseline = raster->baseline;
    next.glyph.format = raster->colour && !protected_geometry(scalar) &&
        (!raster->outline || !FT_Get_Char_Index(raster->outline, scalar)) &&
        FT_Get_Char_Index(raster->colour, scalar) ?
        BLIT_RASTER_RGBA_PREMULTIPLIED : BLIT_RASTER_COVERAGE;
    next.glyph.stride = next.glyph.width * (int)next.glyph.format;
    bytes = (size_t)next.glyph.stride * next.glyph.height;
    old_bytes = entry->valid ? (size_t)entry->glyph.stride * entry->glyph.height : 0;
    if (raster->bytes - old_bytes + bytes > RASTER_MAX_BYTES) return 0;
    next.pixels = calloc(1, bytes);
    if (!next.pixels) return 0;
    next.glyph.pixels = next.pixels;
    if (next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED)
        result = draw_colour(raster, scalar, &next);
    else if (!protected_geometry(scalar)) result = draw_outline(raster, scalar, &next);
    if (result < 0) { free(next.pixels); return 0; }
    if (!result) {
        const BlitFont *font = raster->fallback;
        size_t index = blit_font_glyph_index(font, scalar);
        next.glyph.fallback = 1;
        for (y = 0; y < next.glyph.height; ++y)
            for (x = 0; x < next.glyph.width; ++x)
                next.pixels[y * next.glyph.stride + x] =
                    blit_font_glyph_pixel(font, index, scalar,
                        y * font->height / next.glyph.height,
                        x * font->width / next.glyph.width) ? 255 : 0;
    }
    next.valid = 1;
    free(entry->pixels);
    *entry = next;
    raster->bytes = raster->bytes - old_bytes + bytes;
    ++raster->renders;
    *glyph = entry->glyph;
    return 1;
}

size_t blit_raster_cached_bytes(const BlitRaster *raster)
{ return raster ? raster->bytes : 0; }
unsigned long blit_raster_render_count(const BlitRaster *raster)
{ return raster ? raster->renders : 0; }

uint32_t blit_raster_blend(const BlitRasterGlyph *glyph, int x, int y,
                         uint32_t foreground, uint32_t background)
{
    const unsigned char *p;
    unsigned alpha, r, g, b;
    if (!glyph || !glyph->pixels || x < 0 || y < 0 || x >= glyph->width ||
        y >= glyph->height) return background;
    p = glyph->pixels + y * glyph->stride + x * (int)glyph->format;
    if (glyph->format == BLIT_RASTER_COVERAGE) {
        alpha = p[0];
        r = (((foreground >> 16) & 255) * alpha + 127) / 255;
        g = (((foreground >> 8) & 255) * alpha + 127) / 255;
        b = ((foreground & 255) * alpha + 127) / 255;
    } else if (glyph->format == BLIT_RASTER_RGBA_PREMULTIPLIED) {
        r = p[0]; g = p[1]; b = p[2]; alpha = p[3];
    } else return background;
    r += (((background >> 16) & 255) * (255 - alpha) + 127) / 255;
    g += (((background >> 8) & 255) * (255 - alpha) + 127) / 255;
    b += ((background & 255) * (255 - alpha) + 127) / 255;
    return ((r > 255 ? 255 : r) << 16) |
        ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}
