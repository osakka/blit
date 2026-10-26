/* ADR 0181 steps 3-4: the GPU composes native KMS views itself.

   Included by blit_kms.c after draw_view, so it reads the same
   BlitKmsSurfaceView and uses the CPU compositor's own glyph code to make
   its sprites: a cell's ink is rendered once, white over black and white
   over white, which yields its exact coverage (or, for colour emoji, its
   premultiplied colour and alpha), with italic, bold, wide cells and
   combining marks already applied. The GPU then only places sprites, so the
   two compositors cannot disagree about what a glyph looks like.

   The CPU compositor stays the reference. A frame the GPU cannot compose
   (an effect on) is
   composed by the CPU and uploaded instead, per frame. */

#define KMS_ATLAS_SIZE 2048
#define KMS_SPRITE_SLOTS 8192
#define KMS_SPRITE_MARGIN_Y 2
#define KMS_IMAGE_TEXTURES 32

typedef struct {
    int used;
    uint32_t cp;
    uint32_t marks[BLIT_MAX_COMBINING];
    unsigned count;
    int width, height, lean, bold;
    const void *font, *font_data, *raster, *wide, *wide_data;
    /* The ink's place in the atlas (empty ink has w == 0), its offset from
       the cell's top-left, and how it blends. */
    int atlas_x, atlas_y, w, h, dx, dy, mode;
} KmsSprite;

typedef struct {
    const BlitGraphicsImage *image;
    unsigned long long generation;
    BlitGlesTexture texture;
    int width, height, format;
    unsigned long long used_frame;
} KmsImageTexture;

struct KmsGlesResources {
    BlitGlesTexture atlas;
    int shelf_x, shelf_y, shelf_height;
    KmsSprite sprites[KMS_SPRITE_SLOTS];
    int sprite_count;
    unsigned long long atlas_resets, sprites_rendered;
    KmsImageTexture images[KMS_IMAGE_TEXTURES];
    unsigned long long frame;
    BlitGlesTexture pointer;
    unsigned long long pointer_revision;
    int pointer_width, pointer_height;
    /* The font the sprites were made from. A replaced font can land at the
       same addresses as the one it replaced, so its path and cell size are
       compared too, and any change empties the atlas. */
    char font_path[256];
    const void *font_raster, *font_data;
    int font_cell_width, font_cell_height;
    /* Time this frame spent making sprites and uploading textures. */
    long long upload_us;
    uint32_t *scratch_black, *scratch_white;
    unsigned char *sprite_pixels;
    size_t scratch_pixels;
    /* ADR 0186: the four corner masks for one radius, as a 2r x 2r coverage
       texture made by the same function the CPU compositor uses. */
    BlitGlesTexture corners;
    int corners_radius;
    unsigned long long effect_quads;
};

static void kms_gles_resources_free(BlitKms *kms)
{
    KmsGlesResources *r = kms->gles_resources;
    int i;
    if (r == NULL) return;
    if (kms->gles != NULL) {
        blit_gles_texture_delete(kms->gles, r->atlas);
        blit_gles_texture_delete(kms->gles, r->pointer);
        blit_gles_texture_delete(kms->gles, r->corners);
        for (i = 0; i < KMS_IMAGE_TEXTURES; ++i)
            blit_gles_texture_delete(kms->gles, r->images[i].texture);
    }
    free(r->scratch_black);
    free(r->scratch_white);
    free(r->sprite_pixels);
    free(r);
    kms->gles_resources = NULL;
}

static KmsGlesResources *kms_gles_resources(BlitKms *kms)
{
    if (kms->gles_resources == NULL) {
        kms->gles_resources = calloc(1, sizeof(KmsGlesResources));
        if (kms->gles_resources == NULL) return NULL;
        kms->gles_resources->atlas = blit_gles_texture_create(
            kms->gles, KMS_ATLAS_SIZE, KMS_ATLAS_SIZE, 4);
        if (kms->gles_resources->atlas == 0) {
            free(kms->gles_resources);
            kms->gles_resources = NULL;
        }
    }
    return kms->gles_resources;
}

static unsigned kms_sprite_hash(const KmsSprite *key)
{
    uint32_t h = 2166136261U;
    unsigned i;
#define KMS_MIX(v) do { h ^= (uint32_t)(v); h *= 16777619U; } while (0)
    KMS_MIX(key->cp); KMS_MIX(key->count); KMS_MIX(key->width);
    KMS_MIX(key->height); KMS_MIX(key->lean); KMS_MIX(key->bold);
    KMS_MIX((uintptr_t)key->font); KMS_MIX((uintptr_t)key->font_data);
    KMS_MIX((uintptr_t)key->raster); KMS_MIX((uintptr_t)key->wide);
    for (i = 0; i < key->count; ++i) KMS_MIX(key->marks[i]);
#undef KMS_MIX
    return h;
}

static int kms_sprite_same(const KmsSprite *a, const KmsSprite *b)
{
    return a->cp == b->cp && a->count == b->count && a->width == b->width &&
        a->height == b->height && a->lean == b->lean && a->bold == b->bold &&
        a->font == b->font && a->font_data == b->font_data &&
        a->raster == b->raster && a->wide == b->wide &&
        a->wide_data == b->wide_data &&
        memcmp(a->marks, b->marks, sizeof(uint32_t) * a->count) == 0;
}

static void kms_atlas_reset(KmsGlesResources *r)
{
    memset(r->sprites, 0, sizeof(r->sprites));
    r->sprite_count = 0;
    r->shelf_x = r->shelf_y = r->shelf_height = 0;
    ++r->atlas_resets;
}

static int kms_atlas_place(KmsGlesResources *r, int w, int h, int *x, int *y)
{
    if (w > KMS_ATLAS_SIZE || h > KMS_ATLAS_SIZE) return -1;
    if (r->shelf_x + w > KMS_ATLAS_SIZE) {
        r->shelf_x = 0;
        r->shelf_y += r->shelf_height;
        r->shelf_height = 0;
    }
    if (r->shelf_y + h > KMS_ATLAS_SIZE) return -1;
    *x = r->shelf_x;
    *y = r->shelf_y;
    r->shelf_x += w;
    if (h > r->shelf_height) r->shelf_height = h;
    return 0;
}

/* The cell's ink as the CPU compositor draws it, as a sprite. */
static const KmsSprite *kms_sprite(BlitKms *kms, KmsGlesResources *r,
                                   const BlitFont *font, const BlitFont *wide,
                                   uint32_t cp, const uint32_t *marks,
                                   unsigned count, int width, int height,
                                   int lean, int bold)
{
    KmsSprite key;
    KmsSprite *slot;
    unsigned start, probe;
    KmsOutput scratch;
    /* Room for everything the CPU would draw from this cell: italic lean,
       bold's extra column, and ink the font says can leave the cell. */
    int cell = font != NULL && font->raster_cell_width > 0 ? font->raster_cell_width : width;
    int ink_x = font ? (font->raster_ink_left > font->raster_ink_right ?
                        font->raster_ink_left : font->raster_ink_right) : 0;
    int ink_y = font ? (font->raster_ink_top > font->raster_ink_bottom ?
                        font->raster_ink_top : font->raster_ink_bottom) : 0;
    int margin_x = lean + 4 + (ink_x ? (ink_x + 1) * cell : 0);
    int margin_y = KMS_SPRITE_MARGIN_Y + ink_y * height;
    int sw = width + 2 * margin_x, sh = height + 2 * margin_y;
    size_t pixels = (size_t)sw * (size_t)sh;
    int i, any = 0, premultiplied = 0, x, y, x0, y0, x1, y1;
    long long made;

    memset(&key, 0, sizeof(key));
    key.cp = cp;
    key.count = count > BLIT_MAX_COMBINING ? BLIT_MAX_COMBINING : count;
    if (marks != NULL && key.count) memcpy(key.marks, marks, sizeof(uint32_t) * key.count);
    key.width = width; key.height = height; key.lean = lean; key.bold = bold;
    key.font = font; key.font_data = font ? font->data : NULL;
    key.raster = font ? font->raster_state : NULL;
    key.wide = wide; key.wide_data = wide ? wide->data : NULL;
    start = kms_sprite_hash(&key) % KMS_SPRITE_SLOTS;
    for (probe = 0; probe < KMS_SPRITE_SLOTS; ++probe) {
        KmsSprite *s = &r->sprites[(start + probe) % KMS_SPRITE_SLOTS];
        if (!s->used) break;
        if (kms_sprite_same(s, &key)) return s;
    }
    if (r->sprite_count >= KMS_SPRITE_SLOTS * 3 / 4) kms_atlas_reset(r);
    made = kms_now_us();

    if (pixels > r->scratch_pixels) {
        free(r->scratch_black); free(r->scratch_white); free(r->sprite_pixels);
        r->scratch_black = malloc(pixels * 4);
        r->scratch_white = malloc(pixels * 4);
        r->sprite_pixels = malloc(pixels * 4);
        r->scratch_pixels = r->scratch_black && r->scratch_white &&
            r->sprite_pixels ? pixels : 0;
        if (r->scratch_pixels == 0) return NULL;
    }
    /* Render it twice with the compositor's own code. */
    memset(&scratch, 0, sizeof(scratch));
    scratch.mode.hdisplay = sw;
    scratch.mode.vdisplay = sh;
    scratch.back.pitch = (uint32_t)sw * 4;
    memset(r->scratch_black, 0, pixels * 4);
    scratch.back.map = r->scratch_black;
    draw_cell_glyphs(&scratch, font, wide, cp, key.count ? key.marks : NULL, key.count,
                     margin_x, margin_y, width, height, 0xffffffU, lean, bold);
    memset(r->scratch_white, 0xff, pixels * 4);
    scratch.back.map = r->scratch_white;
    draw_cell_glyphs(&scratch, font, wide, cp, key.count ? key.marks : NULL, key.count,
                     margin_x, margin_y, width, height, 0xffffffU, lean, bold);

    x0 = sw; y0 = sh; x1 = 0; y1 = 0;
    for (i = 0; i < (int)pixels; ++i) {
        uint32_t b = r->scratch_black[i] & 0xffffffU, w = r->scratch_white[i] & 0xffffffU;
        if (b == 0 && w == 0xffffffU) continue;
        /* Coverage tints to white over black and vanishes over white; any
           other result is colour ink (emoji), kept premultiplied. */
        if (w != 0xffffffU || ((b >> 16) & 255) != ((b >> 8) & 255) ||
            ((b >> 8) & 255) != (b & 255))
            premultiplied = 1;
        any = 1;
        x = i % sw; y = i / sw;
        if (x < x0) x0 = x;
        if (y < y0) y0 = y;
        if (x + 1 > x1) x1 = x + 1;
        if (y + 1 > y1) y1 = y + 1;
    }
    key.used = 1;
    if (any) {
        key.w = x1 - x0; key.h = y1 - y0;
        key.dx = x0 - margin_x; key.dy = y0 - margin_y;
        key.mode = premultiplied ? BLIT_GLES_PREMULT : BLIT_GLES_COVERAGE;
        for (y = 0; y < key.h; ++y)
            for (x = 0; x < key.w; ++x) {
                uint32_t b = r->scratch_black[(y + y0) * sw + x + x0];
                uint32_t w = r->scratch_white[(y + y0) * sw + x + x0];
                unsigned char *out = r->sprite_pixels + ((size_t)y * key.w + x) * 4;
                unsigned rb = (b >> 16) & 255, gb = (b >> 8) & 255, bb = b & 255;
                if (premultiplied) {
                    /* Over black the result is c; over white, c + 255 - a. */
                    int a = 255 - (int)(((w >> 8) & 255) - gb);
                    out[0] = (unsigned char)rb; out[1] = (unsigned char)gb;
                    out[2] = (unsigned char)bb;
                    out[3] = (unsigned char)(a < 0 ? 0 : a > 255 ? 255 : a);
                } else {
                    out[0] = out[1] = out[2] = out[3] = (unsigned char)gb;
                }
            }
        if (kms_atlas_place(r, key.w, key.h, &key.atlas_x, &key.atlas_y) < 0) {
            kms_atlas_reset(r);
            if (kms_atlas_place(r, key.w, key.h, &key.atlas_x, &key.atlas_y) < 0)
                return NULL;
        }
        if (blit_gles_texture_update(kms->gles, r->atlas, 4, key.atlas_x, key.atlas_y,
                                     key.w, key.h, r->sprite_pixels, key.w * 4) < 0)
            return NULL;
        ++r->sprites_rendered;
    }
    /* A reset above emptied the table; find a free slot from the start. */
    for (probe = 0; probe < KMS_SPRITE_SLOTS; ++probe) {
        slot = &r->sprites[(start + probe) % KMS_SPRITE_SLOTS];
        if (!slot->used) break;
    }
    r->upload_us += kms_now_us() - made;
    if (probe == KMS_SPRITE_SLOTS) return NULL;
    *slot = key;
    ++r->sprite_count;
    return slot;
}

static BlitGlesTexture kms_gles_image(BlitKms *kms, KmsGlesResources *r,
                                      const BlitGraphicsImage *image)
{
    KmsImageTexture *slot = NULL;
    int bytes = image->format == 24 ? 3 : 4;
    int i;
    long long uploaded;
    for (i = 0; i < KMS_IMAGE_TEXTURES; ++i)
        if (r->images[i].texture && r->images[i].image == image) {
            slot = &r->images[i];
            break;
        }
    if (slot != NULL && slot->generation == image->generation &&
        slot->width == image->width && slot->height == image->height &&
        slot->format == image->format) {
        slot->used_frame = r->frame;
        return slot->texture;
    }
    if (slot == NULL) {
        slot = &r->images[0];
        for (i = 0; i < KMS_IMAGE_TEXTURES; ++i) {
            if (!r->images[i].texture) { slot = &r->images[i]; break; }
            if (r->images[i].used_frame < slot->used_frame) slot = &r->images[i];
        }
    }
    if (slot->texture && (slot->width != image->width ||
                          slot->height != image->height ||
                          slot->format != image->format)) {
        blit_gles_texture_delete(kms->gles, slot->texture);
        slot->texture = 0;
    }
    uploaded = kms_now_us();
    if (!slot->texture)
        slot->texture = blit_gles_texture_create(kms->gles, image->width,
                                                 image->height, bytes);
    if (!slot->texture ||
        blit_gles_texture_update(kms->gles, slot->texture, bytes, 0, 0,
                                 image->width, image->height, image->pixels,
                                 image->width * bytes) < 0) {
        slot->image = NULL;
        r->upload_us += kms_now_us() - uploaded;
        return 0;
    }
    r->upload_us += kms_now_us() - uploaded;
    slot->image = image;
    slot->generation = image->generation;
    slot->width = image->width;
    slot->height = image->height;
    slot->format = image->format;
    slot->used_frame = r->frame;
    return slot->texture;
}

/* NULL when the GPU can compose this output's frame; otherwise why not. */
static const char *kms_gles_unsupported(BlitKms *kms, const KmsOutput *output,
                                        const BlitKmsSurfaceView *views,
                                        int view_count)
{
    int v, d;
    for (v = 0; v < view_count; ++v) {
        BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
        int count;
        if (views[v].display_id != output->info.id || views[v].graphics == NULL)
            continue;
        count = blit_graphics_collect(views[v].graphics, views[v].graphics_count,
                                      draws, BLIT_GRAPHICS_MAX_DRAWS);
        for (d = 0; d < count; ++d) {
            if (draws[d].image->format != 24 && draws[d].image->format != 32)
                return "an image has an unsupported format";
        }
    }
    return NULL;
}

static void kms_gles_font_check(KmsGlesResources *r, const BlitFont *font)
{
    const char *path = font != NULL ? font->path : "";
    const void *raster = font != NULL ? font->raster_state : NULL;
    const void *data = font != NULL ? font->data : NULL;
    int cw = font != NULL ? font->raster_cell_width : 0;
    int ch = font != NULL ? font->raster_cell_height : 0;
    if (strcmp(r->font_path, path) == 0 && r->font_raster == raster &&
        r->font_data == data && r->font_cell_width == cw && r->font_cell_height == ch)
        return;
    kms_atlas_reset(r);
    snprintf(r->font_path, sizeof(r->font_path), "%s", path);
    r->font_raster = raster;
    r->font_data = data;
    r->font_cell_width = cw;
    r->font_cell_height = ch;
}

static void kms_gles_images(BlitKms *kms, KmsGlesResources *r,
                            const BlitKmsSurfaceView *view,
                            const BlitGraphicsDraw *draws, int count,
                            int band)
{
    int d;
    for (d = 0; d < count; ++d) {
        const BlitGraphicsDraw *draw = &draws[d];
        BlitGlesTexture texture;
        int in_band = band == 0 ? draw->z < KMS_GRAPHICS_Z_UNDER_BACKGROUND :
            band == 1 ? (draw->z >= KMS_GRAPHICS_Z_UNDER_BACKGROUND && draw->z < 0) :
            draw->z >= 0;
        if (!in_band) continue;
        texture = kms_gles_image(kms, r, draw->image);
        if (!texture) continue;
        blit_gles_image(kms->gles, texture, draw->image->width, draw->image->height,
                        draw->src_x, draw->src_y, draw->src_w, draw->src_h,
                        view->origin_x + draw->x, view->origin_y + draw->y,
                        draw->width, draw->height);
    }
}

static void kms_gles_glyph(BlitKms *kms, KmsGlesResources *r,
                           const BlitKmsSurfaceView *view, int index,
                           int x, int y, int width, int ch, uint32_t fg,
                           int lean, int bold)
{
    uint16_t attr = view->attributes[index];
    const KmsSprite *sprite = kms_sprite(kms, r, kms->font,
        (attr & KMS_ATTR_WIDE) ? kms->wide_font : NULL,
        view->codepoints[index] ? view->codepoints[index] : ' ',
        view->combining ? &view->combining[index * BLIT_MAX_COMBINING] : NULL,
        view->combining_count ? view->combining_count[index] : 0,
        width, ch, lean, bold);
    if (sprite == NULL || sprite->w == 0) return;
    blit_gles_quad(kms->gles, sprite->mode, r->atlas, KMS_ATLAS_SIZE, KMS_ATLAS_SIZE,
                   sprite->atlas_x, sprite->atlas_y, sprite->w, sprite->h,
                   x + sprite->dx, y + sprite->dy, sprite->w, sprite->h, fg);
}

/* One view in draw_view's order: whole, or (damage) only the cells
   [dx0,dx1) x [dy0,dy1), widened for ink overhang exactly as draw_view
   widens them. The persistent scene keeps every other pixel. */
/* ADR 0186: a pane's effects over the rectangle kms_gles_view just
   repainted, recorded into the output's scene with the rest of the view so
   they replay with it. The same pixels as apply_view_effects (blit_kms.c). */
/* At most this many panes per output get the presentation pass. */
#define BLIT_KMS_PRESENT_PANES 16

static void kms_gles_effects(BlitKms *kms, KmsGlesResources *r,
                             const BlitKmsSurfaceView *view,
                             int x0, int y0, int x1, int y1)
{
    const BlitEffectSet *set = &view->effects;
    int y, radius, corner;
    if (!blit_effect_set_has(set, BLIT_EFFECT_LOCAL) || x1 <= x0 || y1 <= y0) return;
    blit_gles_clip(kms->gles, x0, y0, x1 - x0, y1 - y0);
    /* ADR 0187: a curved pane's scanlines are drawn on the glass instead. */
    if (set->value[BLIT_EFFECT_SCANLINES] > 0 &&
        !blit_effect_scanlines_presented(set, blit_gles_presents(kms->gles)))
        for (y = y0; y < y1; ++y) {
            unsigned alpha = blit_effect_scanline_alpha(set, y - view->effect_y);
            if (alpha) {
                blit_gles_rect(kms->gles, x0, y, x1 - x0, 1, 0, alpha);
                ++r->effect_quads;
            }
        }
    radius = blit_effect_corner_radius(set, view->effect_width, view->effect_height);
    if (radius > 0 && (r->corners == 0 || r->corners_radius != radius)) {
        int size = radius * 2, x;
        unsigned char *mask = calloc((size_t)size * (size_t)size, 4);
        blit_gles_texture_delete(kms->gles, r->corners);
        r->corners = 0;
        if (mask != NULL) {
            for (y = 0; y < size; ++y)
                for (x = 0; x < size; ++x)
                    mask[((size_t)y * size + x) * 4 + 3] = blit_effect_corner_coverage(radius,
                        x < radius ? x : size - 1 - x, y < radius ? y : size - 1 - y);
            r->corners = blit_gles_texture_create(kms->gles, size, size, 4);
            if (r->corners)
                blit_gles_texture_update(kms->gles, r->corners, 4, 0, 0, size, size,
                                         mask, size * 4);
            free(mask);
        }
        r->corners_radius = radius;
    }
    for (corner = 0; radius > 0 && r->corners && corner < 4; ++corner) {
        int right = corner & 1, bottom = corner >> 1;
        blit_gles_quad(kms->gles, BLIT_GLES_COVERAGE, r->corners, radius * 2, radius * 2,
                       right ? radius : 0, bottom ? radius : 0, radius, radius,
                       view->effect_x + (right ? view->effect_width - radius : 0),
                       view->effect_y + (bottom ? view->effect_height - radius : 0),
                       radius, radius, 0);
        ++r->effect_quads;
    }
}

static void kms_gles_view(BlitKms *kms, KmsGlesResources *r, const KmsOutput *output,
                          const BlitKmsSurfaceView *view, int partial,
                          int dx0, int dy0, int dx1, int dy1)
{
    const BlitFont *font = kms->font;
    int cw = view->cell_width > 0 ? view->cell_width :
        (font != NULL && font->width > 0 ? font->width : KMS_FONT_W);
    int ch = view->cell_height > 0 ? view->cell_height :
        (font != NULL && font->height > 0 ? font->height : KMS_FONT_H);
    int italic_lean = (ch + 15) / 16;
    uint32_t floor_colour = view->default_background;
    BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
    int draw_count = 0, row, column;
    int x0 = view->origin_x, y0 = view->origin_y;
    int x1 = x0 + view->columns * cw, y1 = y0 + view->rows * ch;
    int first_row = 0, last_row = view->rows, first_column = 0, last_column = view->columns;
    int src_row0 = 0, src_row1 = view->rows, src_col0 = 0, src_col1 = view->columns;
    int cx0 = x0, cy0 = y0, cx1 = x1, cy1 = y1;
    int overhang = blit_font_has_overhang(font);
    (void)output;
    if (view->columns < 1 || view->rows < 1) return;
    if (partial) {
        BlitInkRegion ink;
        first_row = dy0 < 0 ? 0 : dy0;
        first_column = dx0 < 0 ? 0 : dx0;
        last_row = dy1 > view->rows ? view->rows : dy1;
        last_column = dx1 > view->columns ? view->columns : dx1;
        if (last_row <= first_row || last_column <= first_column) return;
        ink = blit_ink_region(font, first_column, first_row, last_column, last_row,
                              view->columns, view->rows);
        first_column = ink.x0; first_row = ink.y0;
        last_column = ink.x1; last_row = ink.y1;
        src_col0 = ink.source_x0; src_row0 = ink.source_y0;
        src_col1 = ink.source_x1; src_row1 = ink.source_y1;
        cx0 = x0 + first_column * cw; cy0 = y0 + first_row * ch;
        cx1 = x0 + last_column * cw; cy1 = y0 + last_row * ch;
        if (cx1 > x1) cx1 = x1;
        if (cy1 > y1) cy1 = y1;
        if (cx1 <= cx0 || cy1 <= cy0) return;
    }
    blit_gles_clip(kms->gles, cx0, cy0, cx1 - cx0, cy1 - cy0);
    blit_gles_rect(kms->gles, cx0, cy0, cx1 - cx0, cy1 - cy0, floor_colour, 255);
    if (view->graphics != NULL)
        draw_count = blit_graphics_collect(view->graphics, view->graphics_count,
                                           draws, BLIT_GRAPHICS_MAX_DRAWS);
    kms_gles_images(kms, r, view, draws, draw_count, 0);
    for (row = first_row; row < last_row; ++row) {
        column = first_column;
        while (column < last_column) {
            uint32_t background = view_cell_background(view, row * view->stride + column);
            int end = column + 1;
            while (end < last_column &&
                   view_cell_background(view, row * view->stride + end) == background)
                ++end;
            if (background != floor_colour)
                blit_gles_rect(kms->gles, x0 + column * cw, y0 + row * ch,
                               (end - column) * cw, ch, background, 255);
            column = end;
        }
    }
    kms_gles_images(kms, r, view, draws, draw_count, 1);
    /* draw_view clips glyph ink to the region only for a font that
       overhangs; otherwise a damaged cell's ink may cross its edge. */
    if (partial && !overhang)
        blit_gles_clip(kms->gles, x0, y0, x1 - x0, y1 - y0);
    for (row = src_row0; row < src_row1; ++row)
        for (column = src_col0; column < src_col1; ++column) {
            int index = row * view->stride + column;
            uint32_t fg = view->foreground[index] ?
                view->foreground[index] - 1 : view->default_foreground;
            uint32_t bg = view->background[index] ?
                view->background[index] - 1 : view->default_background;
            uint32_t underline = view->underline_color != NULL &&
                view->underline_color[index] ? view->underline_color[index] - 1 : fg;
            uint32_t cp = view->codepoints[index] ? view->codepoints[index] : ' ';
            uint16_t attr = view->attributes[index];
            int width = (attr & KMS_ATTR_WIDE) ? cw * 2 : cw;
            int lean = (attr & KMS_ATTR_ITALIC) ? italic_lean : 0;
            int x = x0 + column * cw, y = y0 + row * ch;
            if (attr & 8) { uint32_t t = fg; fg = bg; bg = t; }
            if (attr & 2) fg = (fg & 0xfefefeU) >> 1;
            if (cp == ' ' && bg == floor_colour && !(attr & 4) &&
                (view->combining_count == NULL || view->combining_count[index] == 0))
                continue;
            if (attr & KMS_ATTR_CONTINUATION) continue;
            kms_gles_glyph(kms, r, view, index, x, y, width, ch, fg, lean, (attr & 1) != 0);
            /* Underline and strike are clipped to the region, as fill_rect
               is in draw_view. */
            if (attr & (4 | 512)) {
                int left = x > cx0 ? x : cx0;
                int right = x + width < cx1 ? x + width : cx1;
                if ((attr & 4) && y + ch - 1 >= cy0 && y + ch - 1 < cy1)
                    blit_gles_rect(kms->gles, left, y + ch - 1, right - left, 1, underline, 255);
                if ((attr & 512) && y + ch / 2 >= cy0 && y + ch / 2 < cy1)
                    blit_gles_rect(kms->gles, left, y + ch / 2, right - left, 1, fg, 255);
            }
        }
    if (partial && !overhang)
        blit_gles_clip(kms->gles, cx0, cy0, cx1 - cx0, cy1 - cy0);
    kms_gles_images(kms, r, view, draws, draw_count, 2);
    /* The cursor only where draw_view draws it: inside the region's rows
       and columns; draw_view does not clip it further. */
    if (partial) blit_gles_clip(kms->gles, x0, y0, x1 - x0, y1 - y0);
    if (view->cursor_visible && view->cursor_x >= 0 && view->cursor_y >= 0 &&
        view->cursor_y < view->rows && view->cursor_y >= first_row &&
        view->cursor_y < last_row) {
        int cursor_column = view->cursor_x < view->columns ? view->cursor_x : view->columns - 1;
        int cursor_cells = 1, cursor_index, cursor_x, cursor_y = y0 + view->cursor_y * ch;
        uint16_t cursor_attr;
        if (cursor_column < 0) cursor_column = 0;
        cursor_index = view->cursor_y * view->stride + cursor_column;
        cursor_attr = view->attributes[cursor_index];
        if ((cursor_attr & KMS_ATTR_CONTINUATION) && cursor_column > 0) {
            --cursor_column;
            cursor_index = view->cursor_y * view->stride + cursor_column;
            cursor_attr = view->attributes[cursor_index];
        }
        if (cursor_attr & KMS_ATTR_WIDE) cursor_cells = 2;
        if (cursor_column >= last_column || cursor_column + cursor_cells <= first_column)
            goto effects;
        cursor_x = x0 + cursor_column * cw;
        if (view->cursor_style == 3 || view->cursor_style == 4)
            blit_gles_rect(kms->gles, cursor_x, cursor_y + ch - 1, cw * cursor_cells, 1,
                           KMS_WHITE, 255);
        else if (view->cursor_style == 5 || view->cursor_style == 6)
            blit_gles_rect(kms->gles, cursor_x, cursor_y, 1, ch, KMS_WHITE, 255);
        else {
            uint32_t cursor_cp = view->codepoints[cursor_index];
            uint32_t cursor_bg = view_cell_background(view, cursor_index);
            blit_gles_rect(kms->gles, cursor_x, cursor_y, cw * cursor_cells, ch,
                           KMS_WHITE, 255);
            if (blit_font_has_overhang(font)) {
                /* Every glyph whose ink enters the cursor cell is redrawn in
                   the cursor's colour, clipped to the cell, as the CPU does. */
                BlitInkRegion sources = blit_ink_region(font, cursor_column,
                    view->cursor_y, cursor_column + cursor_cells,
                    view->cursor_y + 1, view->columns, view->rows);
                int sx, sy, cx0 = cursor_x > x0 ? cursor_x : x0;
                int cx1 = cursor_x + cursor_cells * cw < x1 ? cursor_x + cursor_cells * cw : x1;
                int cy0 = cursor_y > y0 ? cursor_y : y0;
                int cy1 = cursor_y + ch < y1 ? cursor_y + ch : y1;
                blit_gles_clip(kms->gles, cx0, cy0, cx1 - cx0, cy1 - cy0);
                for (sy = sources.source_y0; sy < sources.source_y1; ++sy)
                    for (sx = sources.source_x0; sx < sources.source_x1; ++sx) {
                        int at = sy * view->stride + sx;
                        uint16_t a = view->attributes[at];
                        if (!view->codepoints[at] ||
                            (a & (KMS_ATTR_CONTINUATION | KMS_ATTR_CONCEAL)))
                            continue;
                        kms_gles_glyph(kms, r, view, at, x0 + sx * cw, y0 + sy * ch,
                                       (a & KMS_ATTR_WIDE) ? cw * 2 : cw, ch, cursor_bg,
                                       (a & KMS_ATTR_ITALIC) ? italic_lean : 0, (a & 1) != 0);
                    }
                blit_gles_clip(kms->gles, x0, y0, x1 - x0, y1 - y0);
            } else if (cursor_cp != 0 && cursor_cp != ' ' && !(cursor_attr & KMS_ATTR_CONCEAL))
                kms_gles_glyph(kms, r, view, cursor_index, cursor_x, cursor_y,
                               cw * cursor_cells, ch, cursor_bg,
                               (cursor_attr & KMS_ATTR_ITALIC) ? italic_lean : 0,
                               (cursor_attr & 1) != 0);
        }
    }
effects:
    kms_gles_effects(kms, r, view, cx0, cy0, cx1, cy1);
    blit_gles_clip(kms->gles, 0, 0, 0, 0);
}

/* The output's frame: the scene (views recomposed as needed, held ones
   kept), then overlays and the pointer over it, as the CPU path layers them.
   Returns 0, or -1 having dropped the output to the CPU compositor. */
static int kms_gles_compose(BlitKms *kms, KmsOutput *output,
                            const BlitKmsSurfaceView *views, int view_count,
                            int hold_scene, int full,
                            const BlitKmsOverlay *overlays, int overlay_count,
                            float pointer_x, float pointer_y, int pointer_visible,
                            void *readback)
{
    char error[160];
    KmsGlesResources *r = kms_gles_resources(kms);
    BlitGlesPresentPane present[BLIT_KMS_PRESENT_PANES];
    /* Panes (by their effect origin) redrawn into the scene this frame, so
       a pane's glow copy is rebuilt only when that pane changed. */
    int drawn_x[BLIT_KMS_PRESENT_PANES], drawn_y[BLIT_KMS_PRESENT_PANES];
    int v, present_count = 0, drawn_count = 0, everything_drawn = 0;
    error[0] = '\0';
    if (r == NULL) {
        kms_gles_detach(kms, output, "no GPU atlas");
        return -1;
    }
    unsigned long long draws_before, vertices_before, draws_after, vertices_after;
    ++r->frame;
    r->upload_us = 0;
    r->effect_quads = 0;
    blit_gles_counters(kms->gles, &draws_before, &vertices_before);
    kms_gles_font_check(r, kms->font);
    if (output->gles_scene == NULL) {
        output->gles_scene = blit_gles_scene_open(kms->gles, output->mode.hdisplay,
                                                  output->mode.vdisplay);
        output->gles_scene_fresh = 1;
        if (output->gles_scene == NULL) {
            kms_gles_detach(kms, output, "no GPU scene buffer");
            return -1;
        }
    }
    if (blit_gles_frame_begin(kms->gles, output->gles_target, error, sizeof(error)) < 0) {
        kms_gles_detach(kms, output, error);
        return -1;
    }
    if (!hold_scene) {
        int everything = full || output->gles_scene_fresh;
        blit_gles_scene_begin(kms->gles, output->gles_scene);
        if (everything)
            blit_gles_rect(kms->gles, 0, 0, output->mode.hdisplay,
                           output->mode.vdisplay, 0, 255);
        for (v = 0; v < view_count; ++v) {
            const BlitKmsSurfaceView *view = &views[v];
            int partial = 0, dx = 0, dy = 0, dw = 0, dh = 0;
            if (view->display_id != output->info.id) continue;
            if (!everything && (view->synchronized ||
                                output_view_unchanged(output, view)))
                continue;
            if (!everything) {
                /* The present path's damage rules: the view's cell damage,
                   and for a graphics change without complete damage the old
                   and new image bounds; unbounded means the whole view. */
                int graphics_changed = !output_graphics_unchanged(output, view);
                partial = view->damage_valid;
                dx = view->damage_x; dy = view->damage_y;
                dw = view->damage_width; dh = view->damage_height;
                if (graphics_changed && !view->graphics_damage_complete) {
                    int bx, by, bw, bh, bounded = 0;
                    if (graphics_bounds_for_view(view, &bx, &by, &bw, &bh)) {
                        include_cell_damage(&partial, &dx, &dy, &dw, &dh, bx, by, bw, bh);
                        bounded = 1;
                    }
                    if (output_previous_graphics_bounds(output, view, &bx, &by, &bw, &bh)) {
                        include_cell_damage(&partial, &dx, &dy, &dw, &dh, bx, by, bw, bh);
                        bounded = 1;
                    }
                    if (!bounded) partial = 0;
                }
            }
            kms_gles_view(kms, r, output, view, partial, dx, dy, dx + dw, dy + dh);
            if (drawn_count < BLIT_KMS_PRESENT_PANES) {
                drawn_x[drawn_count] = view->effect_x;
                drawn_y[drawn_count++] = view->effect_y;
            } else {
                everything_drawn = 1;
            }
            output->info.drawn_cells += partial ?
                (unsigned long long)(dw > 0 ? dw : 0) * (unsigned long long)(dh > 0 ? dh : 0) :
                (unsigned long long)view->columns * (unsigned long long)view->rows;
        }
        blit_gles_scene_end(kms->gles);
        output->gles_scene_fresh = 0;
        if (everything) everything_drawn = 1;
    }
    /* ADR 0187: panes with presentation effects are drawn again over the
       plain copy by the presentation program; with none, the copy is all. */
    for (v = 0; blit_gles_presents(kms->gles) && v < view_count &&
         present_count < BLIT_KMS_PRESENT_PANES; ++v) {
        const BlitKmsSurfaceView *view = &views[v];
        BlitGlesPresentPane *pane = &present[present_count];
        int i, duplicate = 0;
        if (view->display_id != output->info.id ||
            !blit_effect_set_has(&view->effects, BLIT_EFFECT_PRESENTATION))
            continue;
        for (i = 0; i < present_count; ++i)
            duplicate |= present[i].x == view->effect_x && present[i].y == view->effect_y;
        if (duplicate) continue;
        pane->x = view->effect_x;
        pane->y = view->effect_y;
        pane->width = view->effect_width;
        pane->height = view->effect_height;
        pane->k = blit_effect_curvature_k(&view->effects);
        pane->scanline_alpha = blit_effect_scanlines_presented(&view->effects, 1) ?
            blit_effect_scanline_alpha(&view->effects, 1) : 0;
        pane->shift_px = blit_effect_rgb_shift_px(&view->effects, view->cell_width);
        pane->glow = view->effects.value[BLIT_EFFECT_GLOW] > 0 ?
            BLIT_EFFECT_GLOW_GAIN * (view->effects.value[BLIT_EFFECT_GLOW] > 100 ? 100 :
                                     view->effects.value[BLIT_EFFECT_GLOW]) / 100.0 : 0.0;
        /* The blurred copy follows the pane: rebuilt only when it changed. */
        int changed = everything_drawn;
        for (i = 0; !changed && i < drawn_count; ++i)
            changed = drawn_x[i] == pane->x && drawn_y[i] == pane->y;
        if (pane->glow > 0 && changed &&
            blit_gles_scene_glow(kms->gles, output->gles_scene, pane->x, pane->y,
                                 pane->width, pane->height) == 0)
            ++output->info.effects_glow_rebuilds;
        ++present_count;
    }
    blit_gles_scene_present(kms->gles, output->gles_scene, present, present_count);
    output->info.effects_present_quads = (unsigned long long)present_count;
    for (v = 0; overlays != NULL && v < overlay_count; ++v) {
        const BlitKmsOverlay *overlay = &overlays[v];
        if (overlay->display_id != output->info.id || overlay->width < 1 ||
            overlay->height < 1 || overlay->opacity <= 0)
            continue;
        blit_gles_rect(kms->gles, overlay->x, overlay->y, overlay->width,
                       overlay->height, overlay->color,
                       (unsigned)(overlay->opacity * 255 / 100));
    }
    if (pointer_intersects_output(output, pointer_x, pointer_y, pointer_visible)) {
        int x = (int)pointer_x - output->info.x, y = (int)pointer_y - output->info.y;
        const BlitPointerArt *art = kms->pointer_art;
        if (art != NULL) {
            if (r->pointer == 0 || r->pointer_revision != art->revision ||
                r->pointer_width != art->width || r->pointer_height != art->height) {
                blit_gles_texture_delete(kms->gles, r->pointer);
                r->pointer = blit_gles_texture_create(kms->gles, art->width, art->height, 4);
                if (r->pointer)
                    blit_gles_texture_update(kms->gles, r->pointer, 4, 0, 0, art->width,
                                             art->height, art->rgba, art->width * 4);
                r->pointer_revision = art->revision;
                r->pointer_width = art->width;
                r->pointer_height = art->height;
            }
            if (r->pointer)
                blit_gles_quad(kms->gles, BLIT_GLES_IMAGE, r->pointer, art->width,
                               art->height, 0, 0, art->width, art->height,
                               x, y, art->width, art->height, 0);
        } else {
            blit_gles_rect(kms->gles, x, y, 2, 18, 0xffffffU, 255);
            blit_gles_rect(kms->gles, x, y, 12, 2, 0xffffffU, 255);
        }
    }
    if (readback != NULL &&
        blit_gles_read_frame(kms->gles, output->gles_target, readback,
                             (int)output->back.pitch) < 0) {
        kms_gles_detach(kms, output, "GLES readback failed");
        return -1;
    }
    if (blit_gles_frame_end(kms->gles, output->gles_target, error, sizeof(error)) < 0) {
        kms_gles_detach(kms, output, error);
        return -1;
    }
    ++output->info.gles_composed_frames;
    blit_gles_counters(kms->gles, &draws_after, &vertices_after);
    output->info.gles_frame_draws = draws_after - draws_before;
    output->info.gles_frame_quads = (vertices_after - vertices_before) / 6;
    output->info.gles_upload_ms = (double)r->upload_us / 1000.0;
    output->info.effects_quads = r->effect_quads;
    output->info.effects_last_ms = 0;
    {
        BlitGlesGpuTime gpu;
        if (blit_gles_gpu_time(output->gles_target, &gpu) == 0) {
            output->info.gpu_timed = 1;
            output->info.gpu_last_ms = gpu.last_ms;
            output->info.gpu_max_ms = gpu.max_ms;
            output->info.gpu_total_us = gpu.total_us;
            output->info.gpu_frames = gpu.frames;
        }
    }
    return 0;
}
