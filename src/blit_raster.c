#include "blit_raster.h"
#include "blit_emoji_presentation.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include "../vendor/harfbuzz/hb.h"
#include "../vendor/harfbuzz/hb-ft.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RASTER_SLOTS 128
#define RASTER_MAX_BYTES (4U * 1024U * 1024U)

typedef struct {
    BlitCellCluster key;
    int valid;
    unsigned char *pixels;
    BlitRasterGlyph glyph;
} RasterEntry;

struct BlitRaster {
    BlitFont fallback;
    FT_Library library;
    FT_Face outline, colour, text_emoji;
    int text_em[2];
    hb_font_t *colour_font;
    hb_buffer_t *shape_buffer;
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
    if (raster->shape_buffer) hb_buffer_destroy(raster->shape_buffer);
    if (raster->colour_font) hb_font_destroy(raster->colour_font);
    if (raster->text_emoji) FT_Done_Face(raster->text_emoji);
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
    /* Shallow immutable PSF view survives a transactional BlitFont move.
       Its backing allocations remain owned by the attached BlitFont. */
    raster->fallback = *fallback;
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
        char sibling[1024];
        const char *slash = strrchr(colour_path, '/');
        int columns;
        if (snprintf(sibling, sizeof(sibling), "%.*sNotoEmojiText.ttf",
                slash ? (int)(slash - colour_path + 1) : 0, colour_path) >= (int)sizeof(sibling)) goto fail;
        if (FT_New_Face(raster->library, sibling, 0, &raster->text_emoji) ||
            !FT_IS_SCALABLE(raster->text_emoji) || FT_HAS_COLOR(raster->text_emoji) ||
            FT_Select_Charmap(raster->text_emoji, FT_ENCODING_UNICODE)) goto fail;
        for (columns = 1; columns <= 2; ++columns) {
            int em;
            for (em = height; em >= 1; --em) {
                int ascent, descent, advance;
                if (FT_Set_Pixel_Sizes(raster->text_emoji, 0, em)) goto fail;
                ascent = ceil_26_6(raster->text_emoji->size->metrics.ascender);
                descent = ceil_26_6(-raster->text_emoji->size->metrics.descender);
                advance = ceil_26_6(raster->text_emoji->size->metrics.max_advance);
                if (ascent + descent <= height && advance <= width * columns) {
                    raster->text_em[columns - 1] = em;
                    break;
                }
            }
            if (!raster->text_em[columns - 1]) goto fail;
        }
        if (!hb_version_atleast(HB_VERSION_MAJOR, HB_VERSION_MINOR, HB_VERSION_MICRO)) goto fail;
        if (FT_New_Face(raster->library, colour_path, 0, &raster->colour) ||
            !FT_HAS_COLOR(raster->colour) || raster->colour->num_fixed_sizes < 1 ||
            FT_Select_Charmap(raster->colour, FT_ENCODING_UNICODE) ||
            FT_Select_Size(raster->colour, 0)) goto fail;
        raster->colour_font = hb_ft_font_create_referenced(raster->colour);
        raster->shape_buffer = hb_buffer_create();
        if (raster->colour_font == hb_font_get_empty() ||
            !hb_buffer_pre_allocate(raster->shape_buffer, BLIT_CLUSTER_MAX_SCALARS)) goto fail;
        hb_ft_font_set_load_flags(raster->colour_font, FT_LOAD_COLOR);
    }
    return raster;
fail:
    blit_raster_close(raster);
    return NULL;
}

static int protected_geometry(uint32_t scalar)
{
    /* Keep exact terminal geometry; outline fonts often leave seams here. */
    return (scalar >= 0x2500 && scalar <= 0x259f) ||
        (scalar >= 0xe0b0 && scalar <= 0xe0b3);
}

/* Core Powerline separators are cell geometry, not cap-height icons.
   One coverage mask feeds the existing cache and all compositors. */
static void draw_powerline(uint32_t scalar, RasterEntry *entry)
{
    int x,y,sx,sy,w=entry->glyph.width,h=entry->glyph.height;
    int thin=scalar&1,mirror=scalar>=0xe0b2,stroke=(h+15)/16;
    for(y=0;y<h;++y) for(x=0;x<w;++x) {
        int covered=0,px=mirror?w-1-x:x;
        for(sy=0;sy<8;++sy) for(sx=0;sx<8;++sx) {
            int sample_y=16*y+2*sy+1;
            int distance=abs(2*sample_y-16*h);
            int inside=w*(16*h-distance)-(16*px+2*sx+1)*h;
            covered+=inside>=0&&(!thin||inside<stroke*16*h);
        }
        /* Fully cover the solid base: no background seam at its join. */
        entry->pixels[y*entry->glyph.stride+x]=!thin&&px==0?255:
            (unsigned char)((covered*255+32)/64);
    }
}

static int invisible_format(uint32_t scalar)
{
    return scalar == 0x200c || scalar == 0x200d ||
        (scalar >= 0xfe00 && scalar <= 0xfe0f) ||
        (scalar >= 0xe0100 && scalar <= 0xe01ef);
}

static unsigned char fallback_coverage(const BlitFont *font, size_t index, uint32_t scalar,
                          int x, int y, int width, int height, int native)
{
    int sx = x * font->width / width, sy = y * font->height / height;
    /* A shrinking point sample can skip all ink of a stroke/checkbox. Native
       profiles retain source-footprint coverage rather than dropping it.
       Exact fractional/quadrant block geometry and deliberate bitmap-only
       profiles keep their existing point-sampling contract. */
    if (native && (!(scalar >= 0x2580 && scalar <= 0x259f) ||
                    (scalar >= 0x2591 && scalar <= 0x2593)) &&
        (width < font->width || height < font->height)) {
        int ex = (x + 1) * font->width / width;
        int ey = (y + 1) * font->height / height;
        int ix, iy, covered = 0;
        if (ex <= sx) ex = sx + 1;
        if (ey <= sy) ey = sy + 1;
        for (iy = sy; iy < ey; ++iy) for (ix = sx; ix < ex; ++ix)
            covered += blit_font_glyph_pixel(font, index, scalar, iy, ix) != 0;
        return (unsigned char)(covered * 255 / ((ex - sx) * (ey - sy)));
    }
    return blit_font_glyph_pixel(font, index, scalar, sy, sx) ? 255 : 0;
}

static int draw_outline_face(FT_Face face, int baseline, uint32_t scalar, RasterEntry *entry)
{
    FT_Bitmap *bitmap;
    int x, y, left, top;
    if (!face || !FT_Get_Char_Index(face, scalar)) return 0;
    if (FT_Load_Char(face, scalar, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL |
        (entry->glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? FT_LOAD_COLOR : 0)))
        return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pitch < 0 ||
        (entry->glyph.format == BLIT_RASTER_COVERAGE &&
         (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays != 256)) ||
        (entry->glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED &&
         bitmap->pixel_mode != FT_PIXEL_MODE_BGRA)) return -1;
    left = (entry->glyph.width - ceil_26_6(face->glyph->advance.x)) / 2 +
        face->glyph->bitmap_left;
    top = baseline - face->glyph->bitmap_top;
    /* Never silently clip a glyph. Caller can use the canonical fallback. */
    if (left < 0 || top < 0 || left + (int)bitmap->width > entry->glyph.width ||
        top + (int)bitmap->rows > entry->glyph.height) return 0;
    for (y = 0; y < (int)bitmap->rows; ++y)
        for (x = 0; x < (int)bitmap->width; ++x) {
            unsigned char *out = entry->pixels + (top + y) * entry->glyph.stride +
                (left + x) * (int)entry->glyph.format;
            const unsigned char *in = bitmap->buffer + y * bitmap->pitch +
                x * (int)entry->glyph.format;
            if (entry->glyph.format == BLIT_RASTER_COVERAGE) out[0] = in[0];
            else {
                out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3];
            }
        }
    return 1;
}

static int draw_outline(BlitRaster *raster, uint32_t scalar, RasterEntry *entry)
{ return draw_outline_face(raster->outline, raster->baseline, scalar, entry); }

static int outline_has_colour(FT_Face face, uint32_t scalar)
{
    FT_LayerIterator iterator;
    FT_UInt glyph, colour;
    if (!face || !FT_HAS_COLOR(face)) return 0;
    memset(&iterator, 0, sizeof(iterator));
    return FT_Get_Color_Glyph_Layer(face, FT_Get_Char_Index(face, scalar),
                                   &glyph, &colour, &iterator);
}

static int draw_text_emoji(BlitRaster *raster, uint32_t scalar, RasterEntry *entry)
{
    int index = entry->key.columns - 1;
    int result, em;
    if (!raster->text_emoji) return 0;
    if (!FT_Get_Char_Index(raster->text_emoji, scalar)) return 0;
    /* Advance metrics do not bound ink: hinted outlines can overhang by a
       pixel. Re-rasterize at a smaller em rather than clipping that ink. */
    for (em = raster->text_em[index]; em >= 1; --em) {
        int ascent, descent, baseline;
        if (FT_Set_Pixel_Sizes(raster->text_emoji, 0, em)) return -1;
        ascent = ceil_26_6(raster->text_emoji->size->metrics.ascender);
        descent = ceil_26_6(-raster->text_emoji->size->metrics.descender);
        baseline = (raster->height - ascent - descent) / 2 + ascent;
        result = draw_outline_face(raster->text_emoji, baseline, scalar, entry);
        if (result < 0) return result;
        if (result > 0) { entry->glyph.baseline = baseline; return result; }
    }
    return 0;
}

static int draw_colour_glyph(BlitRaster *raster, unsigned glyph_index, RasterEntry *entry)
{
    FT_Bitmap *bitmap;
    FT_Face face = raster->colour;
    int width, height, limit_height, x, y, x0, y0;
    if (!face || !glyph_index || glyph_index >= (unsigned)face->num_glyphs) return 0;
    if (FT_Load_Glyph(face, glyph_index, FT_LOAD_COLOR | FT_LOAD_RENDER)) return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pixel_mode != FT_PIXEL_MODE_BGRA || bitmap->pitch < 0 ||
        !bitmap->width || !bitmap->rows) return -1;
    width = entry->glyph.width;
    height = (int)(bitmap->rows * width / bitmap->width);
    /* Cell height includes line spacing. Match the fitted outline em rather
       than inflating emoji into that spacing. Preserve the original cell
       allocation/advance and aspect ratio; colour-only contexts retain their
       existing cell-height bound. All adapters consume this same raster. */
    limit_height = raster->em > 0 ? raster->em : entry->glyph.height;
    if (height > limit_height) {
        height = limit_height;
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

static unsigned colour_cluster_glyph(BlitRaster *raster, const BlitCellCluster *key)
{
    hb_glyph_info_t *info;
    unsigned count, i;
    if (!raster->colour_font || !raster->shape_buffer) return 0;
    /* VS15 is an explicit text request, not an emoji ligature request. */
    for (i = 0; i < key->length; ++i)
        if (key->scalars[i] == 0xfe0e) return 0;
    hb_buffer_clear_contents(raster->shape_buffer);
    hb_buffer_set_flags(raster->shape_buffer, HB_BUFFER_FLAG_REMOVE_DEFAULT_IGNORABLES);
    hb_buffer_add_utf32(raster->shape_buffer, key->scalars, (int)key->length, 0, (int)key->length);
    hb_buffer_guess_segment_properties(raster->shape_buffer);
    hb_shape(raster->colour_font, raster->shape_buffer, NULL, 0);
    if (!hb_buffer_allocation_successful(raster->shape_buffer)) return 0;
    info = hb_buffer_get_glyph_infos(raster->shape_buffer, &count);
    return count == 1 && info ? info[0].codepoint : 0;
}

static int raster_get_key(BlitRaster *raster, const BlitCellCluster *key,
                          BlitRasterGlyph *glyph)
{
    RasterEntry next, *entry;
    size_t bytes, old_bytes;
    unsigned slot, colour_index = 0;
    uint32_t scalar = key->scalars[0];
    int columns = key->columns;
    int text_selector = key->length == 2 && key->scalars[1] == 0xfe0e && blit_emoji_variation(scalar);
    int outline_colour = key->length == 1 && !protected_geometry(scalar) &&
        !invisible_format(scalar) && outline_has_colour(raster->outline, scalar);
    int result = 0, x, y;
    slot = (unsigned)(blit_cell_cluster_hash(key) % RASTER_SLOTS);
    entry = &raster->cache[slot];
    if (entry->valid && blit_cell_cluster_equal(&entry->key, key)) {
        *glyph = entry->glyph;
        return 1;
    }
    memset(&next, 0, sizeof(next));
    next.key = *key;
    next.glyph.width = raster->width * columns;
    next.glyph.height = raster->height;
    next.glyph.baseline = raster->baseline;
    next.glyph.format = raster->colour && !protected_geometry(scalar) &&
        !invisible_format(scalar) &&
        (blit_emoji_default(scalar) || (!blit_emoji_variation(scalar) &&
         (!raster->outline || !FT_Get_Char_Index(raster->outline, scalar)))) &&
        FT_Get_Char_Index(raster->colour, scalar) ?
        BLIT_RASTER_RGBA_PREMULTIPLIED : BLIT_RASTER_COVERAGE;
    if (outline_colour) next.glyph.format = BLIT_RASTER_RGBA_PREMULTIPLIED;
    if (text_selector) next.glyph.format = BLIT_RASTER_COVERAGE;
    else if (key->length > 1) {
        colour_index = colour_cluster_glyph(raster, key);
        if (!colour_index) return 0;
        next.glyph.format = BLIT_RASTER_RGBA_PREMULTIPLIED;
    } else if (next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED)
        colour_index = FT_Get_Char_Index(raster->colour, scalar);
    next.glyph.stride = next.glyph.width * (int)next.glyph.format;
    bytes = (size_t)next.glyph.stride * next.glyph.height;
    old_bytes = entry->valid ? (size_t)entry->glyph.stride * entry->glyph.height : 0;
    if (raster->bytes - old_bytes + bytes > RASTER_MAX_BYTES) return 0;
    next.pixels = calloc(1, bytes);
    if (!next.pixels) return 0;
    next.glyph.pixels = next.pixels;
    if (key->length == 1 && scalar >= 0xe0b0 && scalar <= 0xe0b3) {
        draw_powerline(scalar,&next); result=1;
    } else if (key->length == 1 && invisible_format(scalar)) result = 1;
    else if (next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED && !outline_colour)
        result = draw_colour_glyph(raster, colour_index, &next);
    else if (!protected_geometry(scalar)) {
        result = draw_outline(raster, scalar, &next);
        if (!result && (text_selector || blit_emoji_variation(scalar)))
            result = draw_text_emoji(raster, scalar, &next);
    }
    if (result < 0 || (outline_colour && !result)) { free(next.pixels); return 0; }
    if (!result && key->length > 1 && !text_selector) { free(next.pixels); return 0; }
    if (!result) {
        const BlitFont *font = &raster->fallback;
        size_t index = blit_font_glyph_index(font, scalar);
        next.glyph.fallback = 1;
        for (y = 0; y < next.glyph.height; ++y)
            for (x = 0; x < next.glyph.width; ++x)
                next.pixels[y * next.glyph.stride + x] =
                    fallback_coverage(font, index, scalar, x, y,
                        next.glyph.width, next.glyph.height, raster->outline != NULL);
    }
    next.valid = 1;
    free(entry->pixels);
    *entry = next;
    raster->bytes = raster->bytes - old_bytes + bytes;
    ++raster->renders;
    *glyph = entry->glyph;
    return 1;
}

int blit_raster_get_cluster(BlitRaster *raster, const BlitCellCluster *cluster,
                            BlitRasterGlyph *glyph)
{
    BlitCellCluster key;
    if (!raster || !glyph || !cluster || !cluster->length ||
        cluster->length > BLIT_CLUSTER_MAX_SCALARS ||
        !blit_cell_cluster_init(&key, cluster->scalars[0], cluster->scalars + 1,
                                cluster->length - 1, cluster->columns)) return 0;
    return raster_get_key(raster, &key, glyph);
}

int blit_raster_get(BlitRaster *raster, uint32_t scalar, int columns,
                    BlitRasterGlyph *glyph)
{
    BlitCellCluster key;
    if (!raster || !glyph || !blit_cell_cluster_init(&key, scalar, NULL, 0, columns)) return 0;
    return raster_get_key(raster, &key, glyph);
}

size_t blit_raster_cached_bytes(const BlitRaster *raster)
{ return raster ? raster->bytes : 0; }
unsigned long blit_raster_render_count(const BlitRaster *raster)
{ return raster ? raster->renders : 0; }

static int attached_get(void *state, uint32_t scalar, int columns,
                         BlitRasterGlyph *glyph)
{ return blit_raster_get((BlitRaster *)state, scalar, columns, glyph); }
static void attached_free(void *state)
{ blit_raster_close((BlitRaster *)state); }
static int attached_cluster_get(void *state, const BlitCellCluster *key,
                                 BlitRasterGlyph *glyph)
{ return blit_raster_get_cluster((BlitRaster *)state, key, glyph); }
void blit_raster_attach(BlitFont *font, BlitRaster *raster)
{
    if (font->raster_state && font->raster_free)
        font->raster_free(font->raster_state);
    font->raster_state = raster;
    font->raster_get = raster ? attached_get : NULL;
    font->raster_cluster_get = raster ? attached_cluster_get : NULL;
    font->raster_free = raster ? attached_free : NULL;
    font->raster_blend = raster ? blit_raster_blend : NULL;
    font->raster_cell_width = raster ? raster->width : 0;
    font->raster_cell_height = raster ? raster->height : 0;
}

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
