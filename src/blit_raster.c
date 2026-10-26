#include "blit_raster.h"
#include "blit_emoji_presentation.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_ADVANCES_H
#include "../vendor/harfbuzz/hb.h"
#include "../vendor/harfbuzz/hb-ft.h"
#include <stdlib.h>
#include <string.h>
#include <stdio.h>

#define RASTER_SETS 128
#define RASTER_WAYS 4
#define RASTER_SLOTS (RASTER_SETS * RASTER_WAYS)
#define RASTER_MAX_BYTES (4U * 1024U * 1024U)

typedef struct {
    BlitCellCluster key;
    int valid;
    unsigned age;
    unsigned char *pixels;
    BlitRasterGlyph glyph;
} RasterEntry;

struct BlitRaster {
    BlitFont fallback;
    FT_Library library;
    FT_Face outline, colour, text_emoji, arabic;
    hb_font_t *arabic_font;
    hb_buffer_t *arabic_buffer;
    int arabic_baseline;
    char arabic_path[1024];
    int text_em[2];
    hb_font_t *colour_font;
    hb_buffer_t *shape_buffer;
    int width, height, em, baseline;
    size_t bytes;
    unsigned long renders;
    unsigned reclaim_slot;
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
    if (raster->arabic_font) hb_font_destroy(raster->arabic_font);
    if (raster->arabic_buffer) hb_buffer_destroy(raster->arabic_buffer);
    if (raster->arabic) FT_Done_Face(raster->arabic);
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

int blit_raster_set_arabic(BlitRaster *raster, const char *path)
{
    FT_Face face = NULL;
    hb_font_t *font = NULL;
    hb_buffer_t *buffer = NULL;
    FT_UInt nominal;
    FT_Pos advance;
    FT_F26Dot6 x_em, y_em;
    if (!raster || !path || strlen(path) >= sizeof(raster->arabic_path) ||
        !raster->library || raster->renders || raster->arabic)
        return 0;
    if (FT_New_Face(raster->library, path, 0, &face) || !FT_IS_SCALABLE(face) ||
        !FT_IS_FIXED_WIDTH(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE)) goto fail;
    nominal = FT_Get_Char_Index(face, 0x0628);
    if (!nominal || FT_Load_Glyph(face, nominal, FT_LOAD_NO_SCALE)) goto fail;
    advance = face->glyph->advance.x;
    if (advance <= 0 || face->ascender <= 0 || face->descender > 0) goto fail;
    /* One common transform, not per-letter fitting. Arabic advance rather
       than max_advance: the latter includes multi-cell religious ligatures.
       Fractional sizes and unhinted outlines preserve connecting strokes. */
    x_em = (FT_F26Dot6)((long)raster->width * 64 * face->units_per_EM / advance);
    y_em = (FT_F26Dot6)((long)raster->height * 64 * face->units_per_EM /
                       (face->ascender - face->descender));
    if (FT_Set_Char_Size(face, x_em, y_em, 72, 72)) goto fail;
    font = hb_ft_font_create_referenced(face);
    buffer = hb_buffer_create();
    if (font == hb_font_get_empty() ||
        !hb_buffer_pre_allocate(buffer, BLIT_CLUSTER_MAX_SCALARS)) goto fail;
    hb_ft_font_set_load_flags(font, FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP);
    raster->arabic = face;
    raster->arabic_font = font;
    raster->arabic_buffer = buffer;
    /* Mixed scripts share a baseline; overhang is handled by ink bounds,
       not by separately centring each face inside the same terminal row. */
    raster->arabic_baseline = raster->baseline;
    strcpy(raster->arabic_path, path);
    return 1;
fail:
    if (buffer) hb_buffer_destroy(buffer);
    if (font) hb_font_destroy(font);
    if (face) FT_Done_Face(face);
    return 0;
}

static int arabic_scalar(uint32_t cp)
{
    return (cp >= 0x0600 && cp <= 0x06ff) || (cp >= 0x0750 && cp <= 0x077f) ||
        (cp >= 0x08a0 && cp <= 0x08ff) || (cp >= 0xfb50 && cp <= 0xfdff) ||
        (cp >= 0xfe70 && cp <= 0xfefc);
}

static int draw_arabic(BlitRaster *raster, RasterEntry *entry)
{
    static const hb_feature_t features[] = {
        {HB_TAG('i','s','o','l'),0,0,(unsigned)-1},
        {HB_TAG('i','n','i','t'),0,0,(unsigned)-1},
        {HB_TAG('m','e','d','i'),0,0,(unsigned)-1},
        {HB_TAG('f','i','n','a'),0,0,(unsigned)-1},
        {HB_TAG('r','l','i','g'),0,0,(unsigned)-1},
        {HB_TAG('l','i','g','a'),0,0,(unsigned)-1},
        {HB_TAG('c','a','l','t'),0,0,(unsigned)-1}
    };
    hb_glyph_info_t *info;
    hb_glyph_position_t *positions;
    unsigned count, n;
    int advance = 0, origin, pass;
    int min_x = 0, min_y = 0, max_x = entry->glyph.width, max_y = entry->glyph.height;
    FT_Face face = raster->arabic;
    if (!face) return 0;
    for (n = 0; n < entry->key.length; ++n)
        if (!arabic_scalar(entry->key.scalars[n]) ||
            !FT_Get_Char_Index(face, entry->key.scalars[n])) return 0;
    hb_buffer_clear_contents(raster->arabic_buffer);
    hb_buffer_add_utf32(raster->arabic_buffer, entry->key.scalars,
        (int)entry->key.length, 0, (int)entry->key.length);
    /* This is ONE visual cell, not a logical text run. Never reverse cells. */
    hb_buffer_set_direction(raster->arabic_buffer, HB_DIRECTION_LTR);
    hb_buffer_set_script(raster->arabic_buffer, HB_SCRIPT_ARABIC);
    hb_buffer_set_language(raster->arabic_buffer, hb_language_from_string("ar", -1));
    hb_shape(raster->arabic_font, raster->arabic_buffer, features,
              sizeof(features) / sizeof(features[0]));
    if (!hb_buffer_allocation_successful(raster->arabic_buffer)) return -1;
    info = hb_buffer_get_glyph_infos(raster->arabic_buffer, &count);
    positions = hb_buffer_get_glyph_positions(raster->arabic_buffer, NULL);
    if (!count || count > BLIT_CLUSTER_MAX_SCALARS || !info || !positions) return 0;
    for (n = 0; n < count; ++n) {
        if (!info[n].codepoint) return 0;
        advance += positions[n].x_advance;
    }
    origin = (entry->glyph.width * 64 - advance) / 2;
    /* Measure actual ink first, then paint the exact bounding canvas. A cell
       advance is not an ink clipping rectangle (Arabic cursive overhangs and
       vowel marks are legitimate). Allocation remains bounded per context. */
    for (pass = 0; pass < 2; ++pass) {
      int pen_x = 0, pen_y = 0;
      for (n = 0; n < count; ++n) {
        FT_Bitmap *bitmap;
        int left, top, x, y;
        FT_Vector delta;
        int px = origin + pen_x + positions[n].x_offset;
        int py = pen_y + positions[n].y_offset;
        /* Fractional placement is consumed by FreeType, not discarded before
           scan conversion. Integer placement uses mathematical floor. */
        left = px >= 0 ? px / 64 : -((-px + 63) / 64);
        top = py >= 0 ? py / 64 : -((-py + 63) / 64);
        delta.x = px - left * 64; delta.y = py - top * 64;
        FT_Set_Transform(face, NULL, &delta);
        if (FT_Load_Glyph(face, info[n].codepoint,
                         FT_LOAD_RENDER | FT_LOAD_NO_HINTING | FT_LOAD_NO_BITMAP)) {
            FT_Set_Transform(face, NULL, NULL); return -1;
        }
        FT_Set_Transform(face, NULL, NULL);
        bitmap = &face->glyph->bitmap;
        left += face->glyph->bitmap_left;
        top = raster->arabic_baseline - top - face->glyph->bitmap_top;
        if (bitmap->pitch < 0 || bitmap->pixel_mode != FT_PIXEL_MODE_GRAY ||
            bitmap->num_grays != 256) return -1;
        if (!pass) {
            if (left < min_x) min_x = left;
            if (top < min_y) min_y = top;
            if (left + (int)bitmap->width > max_x) max_x = left + (int)bitmap->width;
            if (top + (int)bitmap->rows > max_y) max_y = top + (int)bitmap->rows;
        } else {
            left -= min_x; top -= min_y;
            if (left < 0 || top < 0 || left + (int)bitmap->width > entry->glyph.width ||
                top + (int)bitmap->rows > entry->glyph.height) return -1;
            for (y = 0; y < (int)bitmap->rows; ++y) for (x = 0; x < (int)bitmap->width; ++x) {
                unsigned char *out = entry->pixels + (top + y) * entry->glyph.stride + left + x;
                unsigned in = bitmap->buffer[y * bitmap->pitch + x];
                *out = (unsigned char)(in + (*out * (255 - in) + 127) / 255);
            }
        }
        pen_x += positions[n].x_advance;
        pen_y += positions[n].y_advance;
      }
      if (!pass) {
        unsigned char *pixels;
        size_t bytes;
        if (min_x < -2 * raster->width || max_x > 4 * raster->width ||
            min_y < -raster->height || max_y > 2 * raster->height) return 0;
        bytes = (size_t)(max_x - min_x) * (max_y - min_y);
        pixels = calloc(1, bytes);
        if (!pixels) return -1;
        free(entry->pixels);
        entry->pixels = pixels;
        entry->glyph.pixels = pixels;
        entry->glyph.width = entry->glyph.stride = max_x - min_x;
        entry->glyph.height = max_y - min_y;
        entry->glyph.ink_offset_x = min_x;
        entry->glyph.ink_offset_y = min_y;
      }
    }
    entry->glyph.baseline = raster->arabic_baseline - min_y;
    return 1;
}

static int protected_geometry(uint32_t scalar)
{
    /* Keep exact terminal geometry; outline fonts often leave seams here. */
    return (scalar >= 0x2500 && scalar <= 0x259f) ||
        (scalar >= 0xe0b0 && scalar <= 0xe0b3) ||
        (scalar >= 0x2800 && scalar <= 0x28ff);
}

/* ADR 0191: Block Elements (U+2580-259F) are cell geometry too -- halves,
   eighths, quadrants and shades, with every edge on a whole pixel so
   neighbouring cells meet without a seam (Claude Code's logo is ▐▛███▜▌).
   The PSF fallback they used to come from lacks the quadrants, which then
   drew as '?'. Returns 0 for a codepoint outside the block. */
static int draw_block_element(uint32_t scalar, RasterEntry *entry)
{
    /* Quadrant masks: bit 0 upper left, 1 upper right, 2 lower left,
       3 lower right; for U+2596-259F. */
    static const unsigned char quadrants[10] = { 4, 8, 1, 13, 9, 7, 11, 2, 6, 14 };
    int x, y, w = entry->glyph.width, h = entry->glyph.height;
    int x0 = 0, x1 = w, y0 = 0, y1 = h, mask = 0, shade = 255;
    /* One split per axis, shared by the halves, the eighths and the
       quadrants: the left and upper halves take the larger half of an odd
       cell, as the bitmap path always drew them. */
    int hx = (w + 1) / 2, hy = (h + 1) / 2;
    if (scalar == 0x2580) y1 = hy;
    else if (scalar >= 0x2581 && scalar <= 0x2588)
        y0 = h - (int)(((long)h * (long)(scalar - 0x2580)) / 8);
    else if (scalar >= 0x2589 && scalar <= 0x258f)
        x1 = (int)(((long)w * (long)(0x2590 - scalar) + 4) / 8);
    else if (scalar == 0x2590) x0 = hx;
    else if (scalar >= 0x2591 && scalar <= 0x2593) shade = (int)(scalar - 0x2590) * 64;
    else if (scalar == 0x2594) y1 = (h + 4) / 8;
    else if (scalar == 0x2595) x0 = w - (w + 4) / 8;
    else if (scalar >= 0x2596 && scalar <= 0x259f) mask = quadrants[scalar - 0x2596];
    else return 0;
    if (y1 < 1) y1 = 1;
    if (x1 < 1) x1 = 1;
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        int on;
        if (mask) {
            int bit = (y >= hy ? 2 : 0) + (x >= hx ? 1 : 0);
            on = (mask >> bit) & 1;
        } else {
            on = x >= x0 && x < x1 && y >= y0 && y < y1;
        }
        entry->pixels[y * entry->glyph.stride + x] = on ? (unsigned char)shade : 0;
    }
    return 1;
}

/* ADR 0191: Braille (U+2800-28FF) is cell geometry, as terminals draw it --
   a font's dots rarely land on the cell grid, and graphs then show gaps.
   2 columns x 4 rows: bits 0-2 are dots 1-3 down the left column, bits 3-5
   dots 4-6 down the right, bits 6 and 7 dots 7 and 8 at the bottom. Column
   centres at 1/4 and 3/4 of the width, rows at (2k + 1) / 8 of the height;
   round dots of radius 0.3 x the smaller dot spacing, so they never touch,
   anti-aliased by 8x8 samples in sixteenths as the bar corners are. */
static void draw_braille(uint32_t scalar, RasterEntry *entry)
{
    static const int column[8] = { 0, 0, 0, 1, 1, 1, 0, 1 };
    static const int row[8] = { 0, 1, 2, 0, 1, 2, 3, 3 };
    int x, y, sx, sy, dot, w = entry->glyph.width, h = entry->glyph.height;
    unsigned bits = scalar - 0x2800;
    double spacing = (double)w / 2.0 < (double)h / 4.0 ? (double)w / 2.0 : (double)h / 4.0;
    long r16 = (long)(16.0 * 0.3 * spacing + 0.5), cx[8], cy[8];
    if (r16 < 8) r16 = 8;
    for (dot = 0; dot < 8; ++dot) {
        cx[dot] = 16L * w * (2 * column[dot] + 1) / 4;
        cy[dot] = 16L * h * (2 * row[dot] + 1) / 8;
    }
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        int covered = 0;
        for (sy = 0; sy < 8; ++sy) for (sx = 0; sx < 8; ++sx) {
            long qx = 16L * x + 2 * sx + 1, qy = 16L * y + 2 * sy + 1;
            for (dot = 0; dot < 8; ++dot) {
                long dx = qx - cx[dot], dy = qy - cy[dot];
                if ((bits & (1U << dot)) && dx * dx + dy * dy <= r16 * r16) {
                    ++covered;
                    break;
                }
            }
        }
        entry->pixels[y * entry->glyph.stride + x] = (unsigned char)((covered * 255 + 32) / 64);
    }
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

/* Blit originals that are cell geometry (blit#140), in the catalogue's
   cell_geometry section: a cell filled except a quarter-circle cut at its
   top-left (U+100007) or top-right (U+100008), radius the cell's shorter
   side. A title bar's ends in the bar's colour: rounded top corners only,
   the bottom square where it meets the window. Edge to edge at every size,
   so neighbouring bar cells join without a seam. */
#define BLIT_BAR_CORNER_LEFT 0x100007
#define BLIT_BAR_CORNER_RIGHT 0x100008

static int bar_corner(uint32_t scalar)
{
    return scalar == BLIT_BAR_CORNER_LEFT || scalar == BLIT_BAR_CORNER_RIGHT;
}

static void draw_bar_corner(uint32_t scalar, RasterEntry *entry)
{
    int x, y, sx, sy, w = entry->glyph.width, h = entry->glyph.height;
    int mirror = scalar == BLIT_BAR_CORNER_RIGHT;
    long r = w < h ? w : h;
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        int px = mirror ? w - 1 - x : x, covered = 0;
        /* 8x8 samples per pixel, in sixteenths: centre of the arc at (r, r). */
        for (sy = 0; sy < 8; ++sy) for (sx = 0; sx < 8; ++sx) {
            long qx = 16L * px + 2 * sx + 1, qy = 16L * y + 2 * sy + 1;
            long dx = 16 * r - qx, dy = 16 * r - qy;
            covered += !(qx < 16 * r && qy < 16 * r && dx * dx + dy * dy > 256 * r * r);
        }
        entry->pixels[y * entry->glyph.stride + x] = (unsigned char)((covered * 255 + 32) / 64);
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
    FT_Fixed advance;
    FT_Vector delta;
    FT_UInt index;
    int x, y, left, top, origin, failed;
    if (!face || !(index = FT_Get_Char_Index(face, scalar))) return 0;
    /* Centre the unrounded advance in the cell at 26.6 precision. Rounding
       it up first put all the slack on the right: a symbol that fills its
       advance got a hard left edge and a faint right one. Horizontal only,
       so vertical hinting is untouched. */
    if (FT_Get_Advance(face, index, FT_LOAD_NO_HINTING, &advance)) return -1;
    origin = (int)((entry->glyph.width * 65536L - advance) / 2048);
    left = origin >= 0 ? origin / 64 : -((-origin + 63) / 64);
    delta.x = origin - left * 64; delta.y = 0;
    FT_Set_Transform(face, NULL, &delta);
    failed = FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL |
        (entry->glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED ? FT_LOAD_COLOR : 0));
    FT_Set_Transform(face, NULL, NULL);
    if (failed) return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pitch < 0 ||
        (entry->glyph.format == BLIT_RASTER_COVERAGE &&
         (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays != 256)) ||
        (entry->glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED &&
         bitmap->pixel_mode != FT_PIXEL_MODE_BGRA)) return -1;
    left += face->glyph->bitmap_left;
    top = baseline - face->glyph->bitmap_top;
    /* Never silently clip a glyph. Caller can use the canonical fallback.
       A blank glyph has no ink to clip, wherever its origin lands. */
    if (!bitmap->width || !bitmap->rows) return 1;
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
    unsigned slot, way, chosen = 0, colour_index = 0;
    RasterEntry *set;
    uint32_t scalar = key->scalars[0];
    int columns = key->columns;
    int arabic = raster->arabic && arabic_scalar(scalar);
    int text_selector = key->length == 2 && key->scalars[1] == 0xfe0e && blit_emoji_variation(scalar);
    int outline_colour = !arabic && key->length == 1 && !protected_geometry(scalar) &&
        !invisible_format(scalar) && outline_has_colour(raster->outline, scalar);
    int result = 0, x, y;
    slot = (unsigned)(blit_cell_cluster_hash(key) % RASTER_SETS);
    set = &raster->cache[slot * RASTER_WAYS];
    /* Four bounded full-key comparisons; collisions may coexist. Ages are
       per-set ranks, not an ever-growing clock. Failure leaves the set alone. */
    for (way = 0; way < RASTER_WAYS; ++way) {
        entry = &set[way];
        if (entry->valid && blit_cell_cluster_equal(&entry->key, key)) {
            unsigned previous_age = entry->age, other;
            for (other = 0; other < RASTER_WAYS; ++other)
                if (set[other].valid && set[other].age < previous_age)
                    ++set[other].age;
            entry->age = 0;
            *glyph = entry->glyph;
            return 1;
        }
    }
    for (way = 0; way < RASTER_WAYS; ++way) {
        if (!set[way].valid) { chosen = way; break; }
        if (set[way].age > set[chosen].age) chosen = way;
    }
    entry = &set[chosen];
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
    if (arabic) next.glyph.format = BLIT_RASTER_COVERAGE;
    if (text_selector) next.glyph.format = BLIT_RASTER_COVERAGE;
    else if (key->length > 1 && !arabic) {
        colour_index = colour_cluster_glyph(raster, key);
        if (!colour_index) return 0;
        next.glyph.format = BLIT_RASTER_RGBA_PREMULTIPLIED;
    } else if (next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED)
        colour_index = FT_Get_Char_Index(raster->colour, scalar);
    next.glyph.stride = next.glyph.width * (int)next.glyph.format;
    bytes = (size_t)next.glyph.stride * next.glyph.height;
    old_bytes = entry->valid ? (size_t)entry->glyph.stride * entry->glyph.height : 0;
    if (bytes > RASTER_MAX_BYTES) return 0;
    next.pixels = calloc(1, bytes);
    if (!next.pixels) return 0;
    next.glyph.pixels = next.pixels;
    if (arabic) result = draw_arabic(raster, &next);
    else if (key->length == 1 && scalar >= 0xe0b0 && scalar <= 0xe0b3) {
        draw_powerline(scalar,&next); result=1;
    } else if (key->length == 1 && bar_corner(scalar)) {
        draw_bar_corner(scalar, &next); result = 1;
    } else if (key->length == 1 && scalar >= 0x2800 && scalar <= 0x28ff &&
               next.glyph.format == BLIT_RASTER_COVERAGE) {
        draw_braille(scalar, &next); result = 1;
    } else if (key->length == 1 && scalar >= 0x2580 && scalar <= 0x259f &&
               next.glyph.format == BLIT_RASTER_COVERAGE &&
               draw_block_element(scalar, &next)) {
        result = 1;
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
    bytes = (size_t)next.glyph.stride * next.glyph.height;
    if (bytes > RASTER_MAX_BYTES) { free(next.pixels); return 0; }
    /* Only after a successful raster: reclaim resident pixels round-robin
       across sets when larger glyphs exhaust the byte budget. Never reject
       a supported glyph merely because other cached glyphs occupy it. */
    while (raster->bytes - old_bytes + bytes > RASTER_MAX_BYTES) {
        unsigned victim_slot = raster->reclaim_slot;
        RasterEntry *victim = &raster->cache[victim_slot];
        raster->reclaim_slot = (victim_slot + 1) % RASTER_SLOTS;
        if (victim != entry && victim->valid) {
            unsigned other, start = victim_slot / RASTER_WAYS * RASTER_WAYS;
            for (other = start; other < start + RASTER_WAYS; ++other)
                if (raster->cache[other].valid && raster->cache[other].age > victim->age)
                    --raster->cache[other].age;
            raster->bytes -= (size_t)victim->glyph.stride * victim->glyph.height;
            free(victim->pixels);
            memset(victim, 0, sizeof(*victim));
        }
    }
    next.valid = 1;
    for (way = 0; way < RASTER_WAYS; ++way)
        if (way != chosen && set[way].valid) ++set[way].age;
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
const char *blit_raster_arabic_path(const BlitRaster *raster)
{ return raster && raster->arabic ? raster->arabic_path : NULL; }
const char *blit_raster_arabic_family(const BlitRaster *raster)
{ return raster && raster->arabic ? raster->arabic->family_name : NULL; }
int blit_raster_baseline(const BlitRaster *raster)
{ return raster ? raster->baseline : 0; }
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
    font->raster_max_width = raster ? raster->width * (raster->arabic ? 6 : 2) : 0;
    font->raster_max_height = raster ? raster->height * (raster->arabic ? 3 : 1) : 0;
    font->raster_ink_left = raster && raster->arabic ? 2 : 0;
    font->raster_ink_right = raster && raster->arabic ? 3 : 0;
    font->raster_ink_top = font->raster_ink_bottom = raster && raster->arabic ? 1 : 0;
}

/* Exact rounded product/255 for two8-bit operands. Maximum intermediate is
   (65025+128)*257 <2^24; avoid six TCC integer divisions per coverage pixel.
   Macro keeps this bounded arithmetic inline with TCC (ADR0179). */
#define RASTER_MUL255(a, b) (((((a) * (b)) + 128U) * 257U) >> 16)
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
        if (alpha == 0) return background & 0xffffffU;
        if (alpha == 255) return foreground & 0xffffffU;
        r = RASTER_MUL255((foreground >> 16) & 255, alpha);
        g = RASTER_MUL255((foreground >> 8) & 255, alpha);
        b = RASTER_MUL255(foreground & 255, alpha);
    } else if (glyph->format == BLIT_RASTER_RGBA_PREMULTIPLIED) {
        r = p[0]; g = p[1]; b = p[2]; alpha = p[3];
    } else return background;
    r += RASTER_MUL255((background >> 16) & 255, 255 - alpha);
    g += RASTER_MUL255((background >> 8) & 255, 255 - alpha);
    b += RASTER_MUL255(background & 255, 255 - alpha);
    return ((r > 255 ? 255 : r) << 16) |
        ((g > 255 ? 255 : g) << 8) | (b > 255 ? 255 : b);
}
#undef RASTER_MUL255
