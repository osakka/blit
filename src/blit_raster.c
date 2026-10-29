#include "blit_raster.h"
#include "blit_emoji_presentation.h"
#include <ft2build.h>
#include FT_FREETYPE_H
#include FT_COLOR_H
#include FT_ADVANCES_H
#include FT_OUTLINE_H
#include FT_TRUETYPE_TABLES_H
#include "../vendor/harfbuzz/hb.h"
#include "../vendor/harfbuzz/hb-ft.h"
#include <math.h>
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
    int pixel;  /* a pixel font's raster: blit_raster_open_pixel (ADR 0199) */
    FT_Library library;
    FT_Face outline, colour, text_emoji, arabic;
    /* ADR 0195: the glyph stack beside the text font. Each may be absent;
       a missing face is skipped, never fatal. */
    FT_Face icons, nerd, symbols[3], unifont[2];
    int cap; /* the text font's cap height in pixels: the icons' box */
    /* Blit's icon font, in its own units (ADR 0197): one column's advance,
       and the height of the box every icon is drawn in. A part spanning
       columns has an advance of its span in columns. */
    double icon_advance, icon_cap;
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

/* The loaded glyph's top, in 26.6 pixels above the baseline. */
static FT_Pos face_top(FT_Face face)
{
    FT_BBox box;
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return 0;
    FT_Outline_Get_CBox(&face->glyph->outline, &box);
    return box.yMax;
}

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
    if (raster->icons) FT_Done_Face(raster->icons);
    if (raster->nerd) FT_Done_Face(raster->nerd);
    for (i = 0; i < 3; ++i) if (raster->symbols[i]) FT_Done_Face(raster->symbols[i]);
    for (i = 0; i < 2; ++i) if (raster->unifont[i]) FT_Done_Face(raster->unifont[i]);
    if (raster->text_emoji) FT_Done_Face(raster->text_emoji);
    if (raster->outline) FT_Done_Face(raster->outline);
    if (raster->colour) FT_Done_Face(raster->colour);
    if (raster->library) FT_Done_FreeType(raster->library);
    free(raster);
}

/* A face from the font directory, or NULL when it is absent or unusable. */
static FT_Face stack_face(BlitRaster *raster, const char *directory, int length,
                          const char *name)
{
    char path[1024];
    FT_Face face = NULL;
    if (snprintf(path, sizeof(path), "%.*s%s", length, directory, name) >= (int)sizeof(path))
        return NULL;
    if (FT_New_Face(raster->library, path, 0, &face)) return NULL;
    if (!FT_IS_SCALABLE(face) || FT_Select_Charmap(face, FT_ENCODING_UNICODE)) {
        FT_Done_Face(face);
        return NULL;
    }
    return face;
}

/* ADR 0199: a pixel font has no metrics but its pixels. Its baseline is the
   row under its H, its cap height that H's ink, and its em the size a text
   face with that cap height would have; each scaled to the cell. */
static void pixel_metrics(BlitRaster *raster)
{
    const BlitFont *font = &raster->fallback;
    size_t index = blit_font_glyph_index(font, 'H');
    int top = -1, bottom = -1, row, column;

    for (row = 0; row < (int)font->height; ++row)
        for (column = 0; column < (int)font->width; ++column)
            if (blit_font_glyph_pixel(font, index, 'H', row, column)) {
                if (top < 0) top = row;
                bottom = row;
            }
    if (top < 0) {
        raster->baseline = raster->height * 4 / 5;
        raster->cap = raster->height * 7 / 10;
    } else {
        raster->baseline = (bottom + 1) * raster->height / (int)font->height;
        raster->cap = (bottom - top + 1) * raster->height / (int)font->height;
    }
    raster->em = raster->cap * 10 / 7;
    if (raster->em > raster->height) raster->em = raster->height;
}

/* ADR 0195: the stack's faces sit beside the colour emoji face. The icons'
   box is the text font's cap height, as the outline generator fitted them:
   OS/2 sCapHeight where the font says, else the height of its H. */
static void open_stack(BlitRaster *raster, const char *colour_path)
{
    static const char *const symbols[3] = {
        "NotoSansSymbols2-Regular.ttf", "NotoSansSymbols-Regular.ttf", "NotoSansMath-Regular.ttf"
    };
    const char *slash = strrchr(colour_path, '/');
    int length = slash ? (int)(slash - colour_path + 1) : 0, i;
    TT_OS2 *os2 = raster->outline ?
        (TT_OS2 *)FT_Get_Sfnt_Table(raster->outline, FT_SFNT_OS2) : NULL;
    raster->icons = stack_face(raster, colour_path, length, "BlitIcons-Regular.ttf");
    if (raster->icons) {
        FT_Fixed advance = 0;
        TT_OS2 *icon_os2 = (TT_OS2 *)FT_Get_Sfnt_Table(raster->icons, FT_SFNT_OS2);
        raster->icon_advance = !FT_Get_Advance(raster->icons, 0, FT_LOAD_NO_SCALE, &advance) &&
            advance > 0 ? (double)advance : raster->icons->units_per_EM * 0.6;
        raster->icon_cap = icon_os2 && icon_os2->sCapHeight > 0 ?
            (double)icon_os2->sCapHeight : raster->icons->units_per_EM * 0.7;
    }
    raster->nerd = stack_face(raster, colour_path, length, "SymbolsNerdFontMono-Regular.ttf");
    for (i = 0; i < 3; ++i) raster->symbols[i] = stack_face(raster, colour_path, length, symbols[i]);
    raster->unifont[0] = stack_face(raster, colour_path, length, "Unifont-Regular.otf");
    raster->unifont[1] = stack_face(raster, colour_path, length, "UnifontUpper-Regular.otf");
    if (!raster->outline)
        return;  /* a pixel font's metrics come from its own H */
    if (os2 && os2->version >= 2 && os2->sCapHeight > 0)
        raster->cap = (int)((long)os2->sCapHeight * raster->em / raster->outline->units_per_EM);
    else if (!FT_Set_Pixel_Sizes(raster->outline, 0, raster->em) &&
             !FT_Load_Char(raster->outline, 'H', FT_LOAD_DEFAULT))
        raster->cap = (int)(face_top(raster->outline) + 63) / 64;
    if (raster->cap < 1 || raster->cap > raster->height) raster->cap = raster->height * 7 / 10;
}

/* ADR 0195: what makes a file a Blit text font -- regular, scalable,
   fixed-width, with a Unicode charmap -- answered with FreeType for the font
   list (blit_font_set_outline_inspector). */
int blit_raster_inspect_outline(const char *path, char *family, size_t family_size,
                                char *reason, size_t reason_size)
{
    static FT_Library library;
    FT_Face face;
    int usable = 0;
    if (!library && FT_Init_FreeType(&library)) {
        snprintf(reason, reason_size, "font engine unavailable");
        return 0;
    }
    if (FT_New_Face(library, path, 0, &face)) {
        snprintf(reason, reason_size, "not a readable font");
        return 0;
    }
    if (!FT_IS_SCALABLE(face)) snprintf(reason, reason_size, "not scalable");
    else if (!FT_IS_FIXED_WIDTH(face)) snprintf(reason, reason_size, "not monospaced");
    else if (face->style_flags & (FT_STYLE_FLAG_BOLD | FT_STYLE_FLAG_ITALIC))
        snprintf(reason, reason_size, "not a regular style");
    else if (FT_Select_Charmap(face, FT_ENCODING_UNICODE))
        snprintf(reason, reason_size, "no Unicode character map");
    else if (!face->family_name || !*face->family_name)
        snprintf(reason, reason_size, "no family name");
    else {
        snprintf(family, family_size, "%s", face->family_name);
        usable = 1;
    }
    FT_Done_Face(face);
    return usable;
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
           ascent/descent and advance; do not stretch an already made bitmap.
           The advance is the font's cell, that of its '0' (ADR 0195): a
           font's widest glyph (Iosevka has a few) is not its cell, and
           fitting that shrank all its text. An overhanging glyph is fitted
           on its own when drawn. */
        FT_UInt cell_glyph = FT_Get_Char_Index(raster->outline, '0');
        for (em = height; em >= 1; --em) {
            int ascent, descent, advance;
            FT_Fixed cell_advance;
            if (FT_Set_Pixel_Sizes(raster->outline, 0, em)) goto fail;
            ascent = ceil_26_6(raster->outline->size->metrics.ascender);
            descent = ceil_26_6(-raster->outline->size->metrics.descender);
            advance = cell_glyph && !FT_Get_Advance(raster->outline, cell_glyph, FT_LOAD_DEFAULT, &cell_advance) ?
                (int)((cell_advance + 65535) / 65536) : ceil_26_6(raster->outline->size->metrics.max_advance);
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
    if (raster->outline && colour_path) open_stack(raster, colour_path);
    return raster;
fail:
    blit_raster_close(raster);
    return NULL;
}

/* ADR 0199: a pixel font's raster. It declines every glyph the font has,
   so those keep the font's own drawing path, and draws the rest from the
   glyph stack, fitted to the font's own H. */
BlitRaster *blit_raster_open_pixel(const BlitFont *font, const char *colour_path,
                                   int width, int height)
{
    BlitRaster *raster = blit_raster_open(font, NULL, colour_path, width, height);
    if (!raster) return NULL;
    raster->pixel = 1;
    pixel_metrics(raster);
    open_stack(raster, colour_path);
    return raster;
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

/* ADR 0195: Box Drawing (U+2500-257F) is cell geometry, as kitty, WezTerm
   and Alacritty draw it. A font's lines rarely reach the cell's edges at a
   fitted size, so frames showed gaps; this range already came from the
   16 px PSF fallback, scaled. Each codepoint is its arms -- up, right,
   down, left, each light, heavy or double -- plus a dash count, or an arc
   or a diagonal. The table is derived from the Unicode character names
   (BOX DRAWINGS DOWN LIGHT AND RIGHT HEAVY and so on), not drawn by hand.
   Arms meet on whole pixels at the cell's centre and reach its edges, so
   neighbouring cells join; double lines meet with inner and outer corners;
   arcs and diagonals are anti-aliased by 4x4 samples. */
typedef struct {
    unsigned char arms;   /* 2 bits each: up, right<<2, down<<4, left<<6 */
    unsigned char dashes; /* 0, or 2, 3 or 4 dashes along the line */
    unsigned char kind;   /* 0 lines, 1 arc, 2 diagonal */
    unsigned char diagonal; /* 1 upper right to lower left, 2 upper left to lower right, 3 both */
} BoxSpec;

static const BoxSpec box_specs[128] = {
    /* 2500 light horizontal */ {0x44, 0, 0, 0},
    /* 2501 heavy horizontal */ {0x88, 0, 0, 0},
    /* 2502 light vertical */ {0x11, 0, 0, 0},
    /* 2503 heavy vertical */ {0x22, 0, 0, 0},
    /* 2504 light triple dash horizontal */ {0x44, 3, 0, 0},
    /* 2505 heavy triple dash horizontal */ {0x88, 3, 0, 0},
    /* 2506 light triple dash vertical */ {0x11, 3, 0, 0},
    /* 2507 heavy triple dash vertical */ {0x22, 3, 0, 0},
    /* 2508 light quadruple dash horizontal */ {0x44, 4, 0, 0},
    /* 2509 heavy quadruple dash horizontal */ {0x88, 4, 0, 0},
    /* 250A light quadruple dash vertical */ {0x11, 4, 0, 0},
    /* 250B heavy quadruple dash vertical */ {0x22, 4, 0, 0},
    /* 250C light down and right */ {0x14, 0, 0, 0},
    /* 250D down light and right heavy */ {0x18, 0, 0, 0},
    /* 250E down heavy and right light */ {0x24, 0, 0, 0},
    /* 250F heavy down and right */ {0x28, 0, 0, 0},
    /* 2510 light down and left */ {0x50, 0, 0, 0},
    /* 2511 down light and left heavy */ {0x90, 0, 0, 0},
    /* 2512 down heavy and left light */ {0x60, 0, 0, 0},
    /* 2513 heavy down and left */ {0xa0, 0, 0, 0},
    /* 2514 light up and right */ {0x05, 0, 0, 0},
    /* 2515 up light and right heavy */ {0x09, 0, 0, 0},
    /* 2516 up heavy and right light */ {0x06, 0, 0, 0},
    /* 2517 heavy up and right */ {0x0a, 0, 0, 0},
    /* 2518 light up and left */ {0x41, 0, 0, 0},
    /* 2519 up light and left heavy */ {0x81, 0, 0, 0},
    /* 251A up heavy and left light */ {0x42, 0, 0, 0},
    /* 251B heavy up and left */ {0x82, 0, 0, 0},
    /* 251C light vertical and right */ {0x15, 0, 0, 0},
    /* 251D vertical light and right heavy */ {0x19, 0, 0, 0},
    /* 251E up heavy and right down light */ {0x16, 0, 0, 0},
    /* 251F down heavy and right up light */ {0x25, 0, 0, 0},
    /* 2520 vertical heavy and right light */ {0x26, 0, 0, 0},
    /* 2521 down light and right up heavy */ {0x1a, 0, 0, 0},
    /* 2522 up light and right down heavy */ {0x29, 0, 0, 0},
    /* 2523 heavy vertical and right */ {0x2a, 0, 0, 0},
    /* 2524 light vertical and left */ {0x51, 0, 0, 0},
    /* 2525 vertical light and left heavy */ {0x91, 0, 0, 0},
    /* 2526 up heavy and left down light */ {0x52, 0, 0, 0},
    /* 2527 down heavy and left up light */ {0x61, 0, 0, 0},
    /* 2528 vertical heavy and left light */ {0x62, 0, 0, 0},
    /* 2529 down light and left up heavy */ {0x92, 0, 0, 0},
    /* 252A up light and left down heavy */ {0xa1, 0, 0, 0},
    /* 252B heavy vertical and left */ {0xa2, 0, 0, 0},
    /* 252C light down and horizontal */ {0x54, 0, 0, 0},
    /* 252D left heavy and right down light */ {0x94, 0, 0, 0},
    /* 252E right heavy and left down light */ {0x58, 0, 0, 0},
    /* 252F down light and horizontal heavy */ {0x98, 0, 0, 0},
    /* 2530 down heavy and horizontal light */ {0x64, 0, 0, 0},
    /* 2531 right light and left down heavy */ {0xa4, 0, 0, 0},
    /* 2532 left light and right down heavy */ {0x68, 0, 0, 0},
    /* 2533 heavy down and horizontal */ {0xa8, 0, 0, 0},
    /* 2534 light up and horizontal */ {0x45, 0, 0, 0},
    /* 2535 left heavy and right up light */ {0x85, 0, 0, 0},
    /* 2536 right heavy and left up light */ {0x49, 0, 0, 0},
    /* 2537 up light and horizontal heavy */ {0x89, 0, 0, 0},
    /* 2538 up heavy and horizontal light */ {0x46, 0, 0, 0},
    /* 2539 right light and left up heavy */ {0x86, 0, 0, 0},
    /* 253A left light and right up heavy */ {0x4a, 0, 0, 0},
    /* 253B heavy up and horizontal */ {0x8a, 0, 0, 0},
    /* 253C light vertical and horizontal */ {0x55, 0, 0, 0},
    /* 253D left heavy and right vertical light */ {0x95, 0, 0, 0},
    /* 253E right heavy and left vertical light */ {0x59, 0, 0, 0},
    /* 253F vertical light and horizontal heavy */ {0x99, 0, 0, 0},
    /* 2540 up heavy and down horizontal light */ {0x56, 0, 0, 0},
    /* 2541 down heavy and up horizontal light */ {0x65, 0, 0, 0},
    /* 2542 vertical heavy and horizontal light */ {0x66, 0, 0, 0},
    /* 2543 left up heavy and right down light */ {0x96, 0, 0, 0},
    /* 2544 right up heavy and left down light */ {0x5a, 0, 0, 0},
    /* 2545 left down heavy and right up light */ {0xa5, 0, 0, 0},
    /* 2546 right down heavy and left up light */ {0x69, 0, 0, 0},
    /* 2547 down light and up horizontal heavy */ {0x9a, 0, 0, 0},
    /* 2548 up light and down horizontal heavy */ {0xa9, 0, 0, 0},
    /* 2549 right light and left vertical heavy */ {0xa6, 0, 0, 0},
    /* 254A left light and right vertical heavy */ {0x6a, 0, 0, 0},
    /* 254B heavy vertical and horizontal */ {0xaa, 0, 0, 0},
    /* 254C light double dash horizontal */ {0x44, 2, 0, 0},
    /* 254D heavy double dash horizontal */ {0x88, 2, 0, 0},
    /* 254E light double dash vertical */ {0x11, 2, 0, 0},
    /* 254F heavy double dash vertical */ {0x22, 2, 0, 0},
    /* 2550 double horizontal */ {0xcc, 0, 0, 0},
    /* 2551 double vertical */ {0x33, 0, 0, 0},
    /* 2552 down single and right double */ {0x1c, 0, 0, 0},
    /* 2553 down double and right single */ {0x34, 0, 0, 0},
    /* 2554 double down and right */ {0x3c, 0, 0, 0},
    /* 2555 down single and left double */ {0xd0, 0, 0, 0},
    /* 2556 down double and left single */ {0x70, 0, 0, 0},
    /* 2557 double down and left */ {0xf0, 0, 0, 0},
    /* 2558 up single and right double */ {0x0d, 0, 0, 0},
    /* 2559 up double and right single */ {0x07, 0, 0, 0},
    /* 255A double up and right */ {0x0f, 0, 0, 0},
    /* 255B up single and left double */ {0xc1, 0, 0, 0},
    /* 255C up double and left single */ {0x43, 0, 0, 0},
    /* 255D double up and left */ {0xc3, 0, 0, 0},
    /* 255E vertical single and right double */ {0x1d, 0, 0, 0},
    /* 255F vertical double and right single */ {0x37, 0, 0, 0},
    /* 2560 double vertical and right */ {0x3f, 0, 0, 0},
    /* 2561 vertical single and left double */ {0xd1, 0, 0, 0},
    /* 2562 vertical double and left single */ {0x73, 0, 0, 0},
    /* 2563 double vertical and left */ {0xf3, 0, 0, 0},
    /* 2564 down single and horizontal double */ {0xdc, 0, 0, 0},
    /* 2565 down double and horizontal single */ {0x74, 0, 0, 0},
    /* 2566 double down and horizontal */ {0xfc, 0, 0, 0},
    /* 2567 up single and horizontal double */ {0xcd, 0, 0, 0},
    /* 2568 up double and horizontal single */ {0x47, 0, 0, 0},
    /* 2569 double up and horizontal */ {0xcf, 0, 0, 0},
    /* 256A vertical single and horizontal double */ {0xdd, 0, 0, 0},
    /* 256B vertical double and horizontal single */ {0x77, 0, 0, 0},
    /* 256C double vertical and horizontal */ {0xff, 0, 0, 0},
    /* 256D light arc down and right */ {0x14, 0, 1, 0},
    /* 256E light arc down and left */ {0x50, 0, 1, 0},
    /* 256F light arc up and left */ {0x41, 0, 1, 0},
    /* 2570 light arc up and right */ {0x05, 0, 1, 0},
    /* 2571 light diagonal upper right to lower left */ {0x00, 0, 2, 1},
    /* 2572 light diagonal upper left to lower right */ {0x00, 0, 2, 2},
    /* 2573 light diagonal cross */ {0x00, 0, 2, 3},
    /* 2574 light left */ {0x40, 0, 0, 0},
    /* 2575 light up */ {0x01, 0, 0, 0},
    /* 2576 light right */ {0x04, 0, 0, 0},
    /* 2577 light down */ {0x10, 0, 0, 0},
    /* 2578 heavy left */ {0x80, 0, 0, 0},
    /* 2579 heavy up */ {0x02, 0, 0, 0},
    /* 257A heavy right */ {0x08, 0, 0, 0},
    /* 257B heavy down */ {0x20, 0, 0, 0},
    /* 257C light left and heavy right */ {0x48, 0, 0, 0},
    /* 257D light up and heavy down */ {0x21, 0, 0, 0},
    /* 257E heavy left and light right */ {0x84, 0, 0, 0},
    /* 257F heavy up and light down */ {0x12, 0, 0, 0},
};

enum { BOX_UP, BOX_RIGHT, BOX_DOWN, BOX_LEFT };

static void box_rect(RasterEntry *entry, int vertical, int a0, int a1,
                     int c0, int c1, int dashes, int length)
{
    int a, c, w = entry->glyph.width, h = entry->glyph.height;
    for (a = a0 < 0 ? 0 : a0; a < a1 && a < length; ++a) {
        if (dashes) {
            /* Dashes evenly along the whole cell, each a third gap; a part
               too short to hold both stays solid, so a tiny cell keeps ink. */
            int part = a * dashes / length;
            int p0 = part * length / dashes, p1 = (part + 1) * length / dashes;
            int gap = p1 - p0 < 2 ? 0 : (p1 - p0) / 3 > 0 ? (p1 - p0) / 3 : 1;
            if (a < p0 + gap / 2 || a >= p1 - (gap - gap / 2)) continue;
        }
        for (c = c0 < 0 ? 0 : c0; c < c1; ++c) {
            int x = vertical ? c : a, y = vertical ? a : c;
            if (x < w && y < h) entry->pixels[y * entry->glyph.stride + x] = 255;
        }
    }
}

/* One arm, along its axis from the centre to its edge. sign is +1 for the
   right and down arms, -1 for the left and up ones; lo and hi are the
   perpendicular arms on the low and high side of its cross axis. */
static void box_arm(RasterEntry *entry, int vertical, int sign, int weight,
                    int opposite, int lo, int hi, int light, int heavy,
                    int offset, int dashes)
{
    int length = vertical ? entry->glyph.height : entry->glyph.width;
    int cross = vertical ? entry->glyph.width : entry->glyph.height;
    int ca = length / 2, cc = cross / 2, k;
    int tlo = lo == 2 ? heavy : light, thi = hi == 2 ? heavy : light;
    if (!weight) return;
    if (weight == 3) {
        for (k = -1; k <= 1; k += 2) {
            int near = k < 0 ? lo : hi, far = k < 0 ? hi : lo;
            int tnear = k < 0 ? tlo : thi, tfar = k < 0 ? thi : tlo;
            int edge;
            /* Stop at the near perpendicular's line on this side; meet the
               opposite arm at the centre; round an outer corner past the
               far perpendicular; or start at the centre alone. */
            if (near) edge = sign > 0 ? (near == 3 ? ca + offset - light / 2 : ca - tnear / 2)
                                      : (near == 3 ? ca - offset - light / 2 + light : ca - tnear / 2 + tnear);
            else if (opposite) edge = ca;
            else if (far) edge = sign > 0 ? (far == 3 ? ca - offset - light / 2 : ca - tfar / 2)
                                          : (far == 3 ? ca + offset - light / 2 + light : ca - tfar / 2 + tfar);
            else edge = sign > 0 ? ca - light / 2 : ca - light / 2 + light;
            if (sign > 0) box_rect(entry, vertical, edge, length,
                                   cc + k * offset - light / 2, cc + k * offset - light / 2 + light, dashes, length);
            else box_rect(entry, vertical, 0, edge,
                          cc + k * offset - light / 2, cc + k * offset - light / 2 + light, dashes, length);
        }
    } else {
        int t = weight == 2 ? heavy : light;
        int double_perp = lo == 3 || hi == 3;
        int tp = (lo ? tlo : 0) > (hi ? thi : 0) ? tlo : (hi ? thi : 0);
        int edge;
        if (double_perp)
            edge = opposite ? ca : (sign > 0 ? ca + offset - light / 2 : ca - offset - light / 2 + light);
        else if (lo || hi) edge = sign > 0 ? ca - tp / 2 : ca - tp / 2 + tp;
        else edge = opposite ? ca : (sign > 0 ? ca - t / 2 : ca - t / 2 + t);
        if (sign > 0) box_rect(entry, vertical, edge, length, cc - t / 2, cc - t / 2 + t, dashes, length);
        else box_rect(entry, vertical, 0, edge, cc - t / 2, cc - t / 2 + t, dashes, length);
    }
}

/* Distance in pixels from (x, y) to the segment (ax, ay)-(bx, by). */
static double box_segment_distance(double x, double y, double ax, double ay, double bx, double by)
{
    double dx = bx - ax, dy = by - ay, t = 0, length = dx * dx + dy * dy;
    if (length > 0) t = ((x - ax) * dx + (y - ay) * dy) / length;
    if (t < 0) t = 0; else if (t > 1) t = 1;
    dx = x - (ax + t * dx); dy = y - (ay + t * dy);
    return sqrt(dx * dx + dy * dy);
}

static void box_curve(const BoxSpec *spec, RasterEntry *entry, int light)
{
    int x, y, sx, sy, w = entry->glyph.width, h = entry->glyph.height;
    /* The straight lines' centres, so a curve meets them exactly. */
    double lx = w / 2 - light / 2 + light / 2.0, ly = h / 2 - light / 2 + light / 2.0;
    double half = light / 2.0, r = (w < h ? w : h) / 2.0;
    int right = (spec->arms >> 2) & 3, down = (spec->arms >> 4) & 3;
    double ux = right ? 1 : -1, uy = down ? 1 : -1;
    double ox = lx + ux * r, oy = ly + uy * r;
    if (spec->kind == 1) {
        /* The straight runs to the edges are whole pixels, as in every
           other line; only the quarter circle between them is sampled. */
        int ty = (int)(down ? floor(oy) : ceil(oy)), tx = (int)(right ? floor(ox) : ceil(ox));
        box_rect(entry, 1, down ? ty : 0, down ? h : ty, w / 2 - light / 2, w / 2 - light / 2 + light, 0, h);
        box_rect(entry, 0, right ? tx : 0, right ? w : tx, h / 2 - light / 2, h / 2 - light / 2 + light, 0, w);
    }
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        int covered = 0;
        unsigned char *out = entry->pixels + y * entry->glyph.stride + x;
        for (sy = 0; sy < 4; ++sy) for (sx = 0; sx < 4; ++sx) {
            double px = x + (sx + 0.5) / 4, py = y + (sy + 0.5) / 4, d = 1e9;
            if (spec->kind == 2) {
                double d1 = box_segment_distance(px, py, w, 0, 0, h);
                double d2 = box_segment_distance(px, py, 0, 0, w, h);
                d = spec->diagonal == 1 ? d1 : spec->diagonal == 2 ? d2 : (d1 < d2 ? d1 : d2);
                covered += d <= half + 0.25;
            } else if ((px - ox) * ux <= 0 && (py - oy) * uy <= 0) {
                d = fabs(sqrt((px - ox) * (px - ox) + (py - oy) * (py - oy)) - r);
                covered += d <= half;
            }
        }
        if (covered) {
            unsigned char value = (unsigned char)((covered * 255 + 8) / 16);
            if (value > *out) *out = value;
        }
    }
}

static void draw_box_drawing(uint32_t scalar, RasterEntry *entry)
{
    const BoxSpec *spec = &box_specs[scalar - 0x2500];
    int w = entry->glyph.width;
    /* Light lines a tenth of the cell's width, at least a pixel; heavy
       about twice that; double lines light, a light line apart. */
    int light = (w + 5) / 10 > 0 ? (w + 5) / 10 : 1;
    int heavy = light * 2 > light + 1 ? light * 2 : light + 1;
    int arm[4], i;
    for (i = 0; i < 4; ++i) arm[i] = (spec->arms >> (2 * i)) & 3;
    if (spec->kind) { box_curve(spec, entry, light); return; }
    box_arm(entry, 0, +1, arm[BOX_RIGHT], arm[BOX_LEFT], arm[BOX_UP], arm[BOX_DOWN], light, heavy, light, spec->dashes);
    box_arm(entry, 0, -1, arm[BOX_LEFT], arm[BOX_RIGHT], arm[BOX_UP], arm[BOX_DOWN], light, heavy, light, spec->dashes);
    box_arm(entry, 1, +1, arm[BOX_DOWN], arm[BOX_UP], arm[BOX_LEFT], arm[BOX_RIGHT], light, heavy, light, spec->dashes);
    box_arm(entry, 1, -1, arm[BOX_UP], arm[BOX_DOWN], arm[BOX_LEFT], arm[BOX_RIGHT], light, heavy, light, spec->dashes);
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

/* ADR 0195: the glyph stack's two fitting rules, moved here from the
   outline generator (ADRs 0156, 0191) so any text font gets them.
   - FIT_ICON (Nerd and Blit icons): within 90% of the advance and the text
     font's cap height, centred on half the cap height, never larger than
     the icon's own one-advance design.
   - FIT_SYMBOL (Noto symbols, Unifont, an overhanging text glyph): at the
     text font's size, shrunk only to fit 90% of the advance and 95% of the
     cell, centred across the cell, on its own baseline unless that would
     leave the cell.
   - A Blit icon that spans columns (ADR 0197) is one codepoint per
     column, each drawn as its slice of the whole: the icon font gives every
     part the whole outline, shifted left by its column, and an advance of
     the whole span. Its design box -- the icon font's cap height -- is
     drawn at 80% of the cell's height, centred on the span and on half the
     text's cap height and kept inside the cell, so a family of spanning
     icons shares one scale and one line; each cell keeps its own slice.
   Returns 1 when drawn, 0 when the face has no such glyph, -1 on error.
   The face's size is left changed: only the text face is shared, and its
   caller restores it. */
enum { FIT_ICON, FIT_SYMBOL };

static int draw_fitted(BlitRaster *raster, FT_Face face, int rule, uint32_t scalar,
                       RasterEntry *entry, int colour)
{
    FT_UInt index;
    FT_BBox box;
    FT_Vector delta;
    FT_Bitmap *bitmap;
    double natural, fit, bw, bh, ox, oy, limit;
    int w = entry->glyph.width, h = entry->glyph.height, left, top, x, y, failed, span = 1;
    if (!face || !(index = FT_Get_Char_Index(face, scalar))) return 0;
    if (FT_Load_Glyph(face, index, FT_LOAD_NO_SCALE)) return -1;
    if (face->glyph->format != FT_GLYPH_FORMAT_OUTLINE) return 0;
    FT_Outline_Get_CBox(&face->glyph->outline, &box);
    bw = (double)(box.xMax - box.xMin);
    bh = (double)(box.yMax - box.yMin);
    if (bw <= 0 || bh <= 0) return 1;
    if (rule == FIT_ICON) {
        double advance = face->glyph->metrics.horiAdvance > 0 ?
            (double)face->glyph->metrics.horiAdvance : face->units_per_EM * 0.6;
        if (face == raster->icons && raster->icon_advance > 0)
            span = (int)floor(advance / raster->icon_advance + 0.5);
        if (span > 1) {
            /* The part's span is its advance; its column is where its ink's
               centre falls, which the generator keeps near its column's
               share of the span. Every spanning icon is fitted by its design
               box -- the icon font's cap height to 80% of the cell -- so a
               family is drawn at one scale and on one line, shrunk only if
               its ink would not fit the span. */
            double one = raster->icon_advance;
            int column = (int)floor((span * one / 2.0 - (box.xMin + box.xMax) / 2.0) / one + 0.5);
            fit = 0.8 * h / raster->icon_cap;
            if (0.95 * span * w / bw < fit) fit = 0.95 * span * w / bw;
            ox = (span / 2.0 - column) * (w - fit * one);
            oy = raster->cap / 2.0 - fit * raster->icon_cap / 2.0;
            if (oy + fit * box.yMax > raster->baseline) oy = raster->baseline - fit * box.yMax;
            if (oy + fit * box.yMin < raster->baseline - h) oy = raster->baseline - h - fit * box.yMin;
            goto place;
        }
        natural = w / advance;
        limit = raster->cap / bh;
    } else {
        natural = (double)raster->em / face->units_per_EM;
        limit = 0.95 * h / bh;
    }
    fit = natural;
    if (0.9 * w / bw < fit) fit = 0.9 * w / bw;
    if (limit < fit) fit = limit;
    ox = w / 2.0 - fit * (box.xMin + box.xMax) / 2.0;
    if (rule == FIT_ICON) oy = raster->cap / 2.0 - fit * (box.yMin + box.yMax) / 2.0;
    else {
        double above = fit * box.yMax, below = -fit * box.yMin;
        oy = 0;
        if (above > raster->baseline) oy = raster->baseline - above;
        else if (below > h - raster->baseline) oy = below - (h - raster->baseline);
    }
place:
    if (FT_Set_Char_Size(face, 0, (FT_F26Dot6)(fit * face->units_per_EM * 64.0 + 0.5), 72, 72))
        return -1;
    left = (int)floor(ox);
    delta.x = (FT_Pos)((ox - left) * 64.0);
    delta.y = (FT_Pos)(oy * 64.0);
    FT_Set_Transform(face, NULL, &delta);
    failed = FT_Load_Glyph(face, index, FT_LOAD_RENDER | FT_LOAD_TARGET_NORMAL |
                           (colour ? FT_LOAD_COLOR : 0));
    FT_Set_Transform(face, NULL, NULL);
    if (failed) return -1;
    bitmap = &face->glyph->bitmap;
    if (bitmap->pitch < 0 ||
        (!colour && (bitmap->pixel_mode != FT_PIXEL_MODE_GRAY || bitmap->num_grays != 256)) ||
        (colour && bitmap->pixel_mode != FT_PIXEL_MODE_BGRA)) return -1;
    left += face->glyph->bitmap_left;
    top = raster->baseline - face->glyph->bitmap_top;
    /* Fitted to the cell, so at most hinting's pixel can stray: trim it. */
    for (y = 0; y < (int)bitmap->rows; ++y) for (x = 0; x < (int)bitmap->width; ++x) {
        int cx = left + x, cy = top + y;
        unsigned char *out;
        const unsigned char *in;
        if (cx < 0 || cy < 0 || cx >= w || cy >= h) continue;
        out = entry->pixels + cy * entry->glyph.stride + cx * (int)entry->glyph.format;
        in = bitmap->buffer + y * bitmap->pitch + x * (colour ? 4 : 1);
        if (!colour) out[0] = in[0];
        else { out[0] = in[2]; out[1] = in[1]; out[2] = in[0]; out[3] = in[3]; }
    }
    return 1;
}

/* The text font's glyph at the cell's size; one that would overhang the
   cell (Hack's long arrows, U+27F5-27FF) is shrunk to fit, not dropped to
   the PSF's '?'. */
static int draw_outline(BlitRaster *raster, uint32_t scalar, RasterEntry *entry)
{
    int result = draw_outline_face(raster->outline, raster->baseline, scalar, entry);
    if (result == 0 && raster->outline && FT_Get_Char_Index(raster->outline, scalar)) {
        result = draw_fitted(raster, raster->outline, FIT_SYMBOL, scalar, entry,
                             entry->glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED);
        if (FT_Set_Pixel_Sizes(raster->outline, 0, raster->em)) return -1;
    }
    return result;
}

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
    /* ADR 0195: Blit's own icons come first: its colour icons in plane 16
       and the pen at U+270E, whatever the text font has there. */
    int blit_icon = !arabic && key->length == 1 && raster->icons &&
        (scalar >= 0x100000 || scalar == 0x270e) && !bar_corner(scalar) &&
        FT_Get_Char_Index(raster->icons, scalar);
    int outline_colour = !arabic && !blit_icon && key->length == 1 && !protected_geometry(scalar) &&
        !invisible_format(scalar) && outline_has_colour(raster->outline, scalar);
    int result = 0, x, y;
    /* ADR 0199: a pixel font draws every glyph it has itself, on its own
       path and at its own pixels; the stack is only for what it lacks.
       Declining here is what sends the cell back to that path. */
    if (raster->pixel && key->length == 1 && !blit_icon &&
        blit_font_has_glyph(&raster->fallback, scalar))
        return 0;
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
    if (blit_icon) next.glyph.format = outline_has_colour(raster->icons, scalar) ?
        BLIT_RASTER_RGBA_PREMULTIPLIED : BLIT_RASTER_COVERAGE;
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
    } else if (key->length == 1 && scalar >= 0x2500 && scalar <= 0x257f &&
               next.glyph.format == BLIT_RASTER_COVERAGE) {
        draw_box_drawing(scalar, &next); result = 1;
    } else if (key->length == 1 && scalar >= 0x2800 && scalar <= 0x28ff &&
               next.glyph.format == BLIT_RASTER_COVERAGE) {
        draw_braille(scalar, &next); result = 1;
    } else if (key->length == 1 && scalar >= 0x2580 && scalar <= 0x259f &&
               next.glyph.format == BLIT_RASTER_COVERAGE &&
               draw_block_element(scalar, &next)) {
        result = 1;
    } else if (key->length == 1 && invisible_format(scalar)) result = 1;
    else if (blit_icon)
        result = draw_fitted(raster, raster->icons, FIT_ICON, scalar, &next,
                             next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED);
    else if (next.glyph.format == BLIT_RASTER_RGBA_PREMULTIPLIED && !outline_colour)
        result = draw_colour_glyph(raster, colour_index, &next);
    else if (!protected_geometry(scalar)) {
        /* ADR 0195, the stack: Nerd's private-use icons from Nerd Symbols
           whatever the text font puts there; then the text font; Blit's
           monochrome text glyphs; any other Nerd icon; text emoji where
           asked for; the Noto symbol faces; and Unifont last. */
        int pua = (scalar >= 0xe000 && scalar <= 0xf8ff) ||
            (scalar >= 0xf0000 && scalar <= 0xffffd);
        int face;
        if (pua) result = draw_fitted(raster, raster->nerd, FIT_ICON, scalar, &next, 0);
        if (!result) result = draw_outline(raster, scalar, &next);
        if (!result) result = draw_fitted(raster, raster->icons, FIT_ICON, scalar, &next, 0);
        if (!result && !pua) result = draw_fitted(raster, raster->nerd, FIT_ICON, scalar, &next, 0);
        if (!result && (text_selector || blit_emoji_variation(scalar)))
            result = draw_text_emoji(raster, scalar, &next);
        for (face = 0; !result && face < 3; ++face)
            result = draw_fitted(raster, raster->symbols[face], FIT_SYMBOL, scalar, &next, 0);
        /* A pixel font's wide characters stay pixels: Blit's wide PSF has
           them, so Unifont's outlines are only its one-column net. */
        for (face = 0; !result && face < 2 && (!raster->pixel || columns == 1); ++face)
            result = draw_fitted(raster, raster->unifont[face], FIT_SYMBOL, scalar, &next, 0);
    }
    if (result < 0 || (outline_colour && !result)) { free(next.pixels); return 0; }
    if (!result && key->length > 1 && !text_selector) { free(next.pixels); return 0; }
    /* Nothing has it: a pixel font shows its own replacement, as before. */
    if (!result && raster->pixel) { free(next.pixels); return 0; }
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
