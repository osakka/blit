#include "blit_kms.h"
#include "blit_font.h"

#include <errno.h>
#include <fcntl.h>
#include <libdrm/drm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define KMS_MAX_OUTPUTS 16
#define KMS_MAX_VIEWS 64
#define KMS_MAX_MODES BLIT_KMS_MAX_MODES
#define KMS_FONT_W 8
#define KMS_FONT_H 8
#define KMS_POINTER_W 12
#define KMS_POINTER_H 18
#define KMS_WHITE 0xbed2beU
#define KMS_ATTR_WIDE 16
#define KMS_ATTR_CONTINUATION 32
#define KMS_ATTR_CONCEAL 64
#define KMS_ATTR_ITALIC 128

typedef struct {
    uint32_t fb;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    void *map;
} KmsBuffer;

typedef struct {
    BlitKmsDisplayInfo info;
    uint32_t connector;
    uint32_t crtc;
    drmModeModeInfo mode;
    int mode_count;
    int active_mode;
    BlitKmsMode modes[KMS_MAX_MODES];
    drmModeCrtc *old_crtc;
    KmsBuffer front;
    KmsBuffer back;
    int flip_pending;
    volatile int flip_done;
    long long flip_started_us;
    int modeset_fallback;
    int initialized;
    int last_view_count;
    unsigned long long last_visual_revision;
    unsigned long long last_surface_ids[KMS_MAX_VIEWS];
    unsigned long long last_surface_revisions[KMS_MAX_VIEWS];
    int last_damage_valid;
    int last_damage_x;
    int last_damage_y;
    int last_damage_width;
    int last_damage_height;
    unsigned char pointer_background[KMS_POINTER_W * KMS_POINTER_H * 4];
    int pointer_background_valid;
    int pointer_background_x;
    int pointer_background_y;
    int pointer_background_width;
    int pointer_background_height;
    BlitFont *font;
} KmsOutput;

struct BlitKms {
    int fd;
    int output_count;
    KmsOutput outputs[KMS_MAX_OUTPUTS];
    BlitFont *font;
    /* ADR 0062: the 16x16 font for wide cells. */
    BlitFont *wide_font;
    int cell_width_override;
    char screenshot_dir[256];
    unsigned long screenshot_frame;
    unsigned long screenshot_after;
    int screenshot_written;
    int pointer_state_valid;
    float previous_pointer_x;
    float previous_pointer_y;
    int previous_pointer_visible;
};

static long long kms_now_us(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000000LL + now.tv_nsec / 1000;
}

static int kms_flip_timeout_ms(void)
{
    const char *timeout_value = getenv("BLIT_KMS_FLIP_TIMEOUT_MS");
    int timeout_ms = 100;
    if (timeout_value != NULL && timeout_value[0] != '\0') {
        int requested = atoi(timeout_value);
        if (requested >= 1 && requested <= 1000)
            timeout_ms = requested;
    }
    return timeout_ms;
}

static void kms_flip_handler(int fd, unsigned int frame, unsigned int sec,
                             unsigned int usec, void *data)
{
    KmsOutput *output = (KmsOutput *)data;
    (void)fd; (void)frame; (void)sec; (void)usec;
    if (output != NULL) {
        output->flip_done = 1;
        output->flip_pending = 0;
        output->flip_started_us = 0;
    }
}

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0)
        snprintf(error, capacity, "%s", message);
}

static int choose_crtc(int fd, drmModeRes *resources,
                       drmModeConnector *connector, uint32_t *used,
                       int used_count, uint32_t *result)
{
    int e;
    for (e = -1; e < connector->count_encoders; ++e) {
        uint32_t encoder_id = e < 0 ? connector->encoder_id :
            connector->encoders[e];
        drmModeEncoder *encoder;
        int i;
        if (encoder_id == 0) continue;
        encoder = drmModeGetEncoder(fd, encoder_id);
        if (encoder == NULL) continue;
        for (i = 0; i < resources->count_crtcs; ++i) {
            uint32_t crtc = resources->crtcs[i];
            int used_here = 0;
            int j;
            if (!(encoder->possible_crtcs & (1U << i))) continue;
            for (j = 0; j < used_count; ++j)
                if (used[j] == crtc) used_here = 1;
            if (!used_here) {
                *result = crtc;
                drmModeFreeEncoder(encoder);
                return 0;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return -1;
}

static int choose_mode(const drmModeConnector *connector)
{
    int preferred = 0;
    int index;
    int selected;

    for (index = 0; index < connector->count_modes; ++index)
        if (connector->modes[index].type & DRM_MODE_TYPE_PREFERRED) {
            preferred = index;
            break;
        }
    selected = preferred;
    for (index = 0; index < connector->count_modes; ++index) {
        const drmModeModeInfo *candidate = &connector->modes[index];
        const drmModeModeInfo *current = &connector->modes[selected];
        if (candidate->hdisplay == current->hdisplay &&
            candidate->vdisplay == current->vdisplay &&
            candidate->vrefresh > current->vrefresh)
            selected = index;
    }
    return selected;
}

static int make_buffer(int fd, KmsOutput *output, KmsBuffer *buffer)
{
    struct drm_mode_create_dumb create;
    struct drm_mode_map_dumb map;
    memset(&create, 0, sizeof(create));
    create.width = output->mode.hdisplay;
    create.height = output->mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0) return -1;
    buffer->handle = create.handle;
    buffer->pitch = create.pitch;
    buffer->size = create.size;
    if (drmModeAddFB(fd, output->mode.hdisplay, output->mode.vdisplay,
                     24, 32, buffer->pitch, buffer->handle,
                     &buffer->fb) < 0) return -1;
    memset(&map, 0, sizeof(map));
    map.handle = buffer->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0) return -1;
    buffer->map = mmap(NULL, buffer->size, PROT_READ | PROT_WRITE,
                       MAP_SHARED, fd, map.offset);
    if (buffer->map == MAP_FAILED) {
        buffer->map = NULL;
        return -1;
    }
    memset(buffer->map, 0, buffer->size);
    return 0;
}

static void free_buffer(int fd, KmsBuffer *buffer)
{
    struct drm_mode_destroy_dumb destroy;
    if (buffer->map != NULL) munmap(buffer->map, buffer->size);
    if (buffer->fb != 0) drmModeRmFB(fd, buffer->fb);
    memset(&destroy, 0, sizeof(destroy));
    destroy.handle = buffer->handle;
    if (buffer->handle != 0) drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB,
                                      &destroy);
    memset(buffer, 0, sizeof(*buffer));
}

static void swap_buffers(KmsOutput *output)
{
    KmsBuffer temporary = output->front;
    output->front = output->back;
    output->back = temporary;
}

static void copy_rect(KmsOutput *output, int x, int y, int width, int height)
{
    int row;
    int first_x = x < 0 ? 0 : x;
    int first_y = y < 0 ? 0 : y;
    int last_x = x + width;
    int last_y = y + height;
    size_t bytes;

    if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
    if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
    if (first_x >= last_x || first_y >= last_y) return;
    bytes = (size_t)(last_x - first_x) * sizeof(uint32_t);
    for (row = first_y; row < last_y; ++row) {
        unsigned char *destination = (unsigned char *)output->back.map +
            (size_t)row * output->back.pitch + (size_t)first_x * sizeof(uint32_t);
        const unsigned char *source = (const unsigned char *)output->front.map +
            (size_t)row * output->front.pitch + (size_t)first_x * sizeof(uint32_t);
        memcpy(destination, source, bytes);
    }
}

static void include_damage(int *valid, int *x, int *y, int *width, int *height,
                           int damage_x, int damage_y,
                           int damage_width, int damage_height)
{
    int right;
    int bottom;

    if (damage_width <= 0 || damage_height <= 0) return;
    right = damage_x + damage_width;
    bottom = damage_y + damage_height;
    if (!*valid) {
        *valid = 1;
        *x = damage_x;
        *y = damage_y;
        *width = damage_width;
        *height = damage_height;
        return;
    }
    if (damage_x < *x) *x = damage_x;
    if (damage_y < *y) *y = damage_y;
    if (right > *x + *width) *width = right - *x;
    if (bottom > *y + *height) *height = bottom - *y;
}

static void fill_rect(KmsOutput *output, int x, int y, int width, int height,
                      uint32_t color)
{
    int row;
    int column;
    int first_x = x < 0 ? 0 : x;
    int first_y = y < 0 ? 0 : y;
    int last_x = x + width;
    int last_y = y + height;
    if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
    if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
    if (first_x >= last_x || first_y >= last_y) return;
    for (row = first_y; row < last_y; ++row) {
        uint32_t *pixels = (uint32_t *)((unsigned char *)output->back.map +
                                        row * output->back.pitch);
        if ((color & 0xffU) == ((color >> 8) & 0xffU) &&
            (color & 0xffU) == ((color >> 16) & 0xffU) &&
            (color & 0xffU) == ((color >> 24) & 0xffU)) {
            memset((unsigned char *)pixels + first_x * sizeof(*pixels),
                   (int)(color & 0xffU),
                   (size_t)(last_x - first_x) * sizeof(*pixels));
            continue;
        }
        column = first_x;
        if (((uintptr_t)(pixels + column) & (sizeof(uint64_t) - 1)) != 0) {
            pixels[column] = color;
            ++column;
        }
        {
            uint64_t pair = (uint64_t)color | ((uint64_t)color << 32);
            uint64_t *wide = (uint64_t *)(pixels + column);
            int pairs = (last_x - column) / 2;
            int pair_index;
            for (pair_index = 0; pair_index < pairs; ++pair_index)
                wide[pair_index] = pair;
            column += pairs * 2;
        }
        if (column < last_x)
            pixels[column] = color;
    }
}

static void blend_rect(KmsOutput *output, int x, int y, int width, int height,
                       uint32_t color, unsigned int alpha)
{
    int row;
    int column;
    int first_x = x < 0 ? 0 : x;
    int first_y = y < 0 ? 0 : y;
    int last_x = x + width;
    int last_y = y + height;

    if (alpha >= 255U) {
        fill_rect(output, x, y, width, height, color);
        return;
    }
    if (alpha == 0U)
        return;
    if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
    if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
    if (first_x >= last_x || first_y >= last_y) return;
    for (row = first_y; row < last_y; ++row) {
        uint32_t *pixels = (uint32_t *)((unsigned char *)output->back.map +
                                        row * output->back.pitch);
        for (column = first_x; column < last_x; ++column) {
            uint32_t old = pixels[column];
            unsigned int inverse = 255U - alpha;
            unsigned int red = (old >> 16) & 0xffU;
            unsigned int green = (old >> 8) & 0xffU;
            unsigned int blue = old & 0xffU;
            pixels[column] = (((((color >> 16) & 0xffU) * alpha) +
                               red * inverse) / 255U << 16) |
                (((((color >> 8) & 0xffU) * alpha) +
                  green * inverse) / 255U << 8) |
                ((((color & 0xffU) * alpha) + blue * inverse) / 255U);
        }
    }
}

static void draw_kms_effects(KmsOutput *output, int enabled,
                             int intensity)
{
    int y;
    unsigned int alpha;

    if (!enabled || intensity <= 0)
        return;
    if (intensity > 100) intensity = 100;
    alpha = (unsigned int)(intensity * 255 / 100);
    for (y = 1; y < output->mode.vdisplay; y += 2)
        blend_rect(output, 0, y, output->mode.hdisplay, 1, 0, alpha);
}

static void draw_kms_overlays(KmsOutput *output,
                              const BlitKmsOverlay *overlays,
                              int overlay_count)
{
    int index;

    if (overlays == NULL)
        return;
    for (index = 0; index < overlay_count; ++index) {
        const BlitKmsOverlay *overlay = &overlays[index];
        if (overlay->display_id != output->info.id ||
            overlay->width < 1 || overlay->height < 1 ||
            overlay->opacity <= 0)
            continue;
        blend_rect(output, overlay->x, overlay->y, overlay->width,
                   overlay->height, overlay->color,
                   (unsigned int)(overlay->opacity * 255 / 100));
    }
}

static uint32_t view_cell_background(const BlitKmsSurfaceView *view,
                                     int index)
{
    uint32_t fg = view->foreground[index] ?
        view->foreground[index] - 1 : view->default_foreground;
    uint32_t bg = view->background[index] ?
        view->background[index] - 1 : view->default_background;
    if (view->attributes[index] & 8) {
        uint32_t temporary = fg;
        fg = bg;
        bg = temporary;
    }
    return bg;
}

/* The horizontal offset of one glyph row under a slant: `lean` at the top row,
   -`lean` at the bottom, so the glyph stays centred in its cell. */
static int glyph_row_lean(int lean, int row, int rows)
{
    if (lean == 0 || rows < 2)
        return 0;
    return lean * (rows - 1 - 2 * row) / (rows - 1);
}

/* `trim` drops that many destination pixels from the right edge, and the
   matching source columns with them, so bold's overstrike gives up its last
   column instead of writing into the next cell. */
static void draw_glyph(KmsOutput *output, const BlitFont *font,
                       uint32_t codepoint, int x, int y, int cw, int ch,
                       uint32_t color, int lean, int trim)
{
    int glyph_row;
    int glyph_column;
    size_t glyph;
    int font_width = font != NULL && font->data ? font->width : 5;
    int font_height = font != NULL && font->data ? font->height : 7;
    glyph = blit_font_glyph_index(font, codepoint);
    if (trim > 0) {
        if (trim >= cw) return;
        cw -= trim;
    }
    /* The unscaled fast path writes without a per-pixel bounds test, which a
       slant's overhang would break; a slanted glyph takes the general path. */
    if (lean == 0 && trim == 0 && font != NULL && font->data != NULL &&
        cw == font_width &&
        ch == font_height && x >= 0 && y >= 0 &&
        x + cw <= output->mode.hdisplay && y + ch <= output->mode.vdisplay) {
        /* The bundled PSF is used at native size in the normal KMS path.
           Avoid the general scaler's per-pixel divisions and clipping
           branches when no scaling is required. */
        for (glyph_row = 0; glyph_row < font_height; ++glyph_row) {
            uint32_t *pixels = (uint32_t *)((unsigned char *)output->back.map +
                                            (y + glyph_row) * output->back.pitch);
            uint32_t row_bits = blit_font_glyph_row(font, glyph, codepoint,
                                                    glyph_row);
            uint32_t bit = 1U << (font_width - 1);
            for (glyph_column = 0; glyph_column < font_width;
                 ++glyph_column, bit >>= 1)
                if (row_bits & bit)
                    pixels[x + glyph_column] = color;
        }
        return;
    }
    for (glyph_row = 0; glyph_row < ch; ++glyph_row) {
        uint32_t *pixels;
        int py = y + glyph_row;
        int source_row = glyph_row * font_height / ch;
        uint32_t row_bits = blit_font_glyph_row(font, glyph, codepoint,
                                                source_row);
        if (py < 0 || py >= output->mode.vdisplay) continue;
        if (source_row < 0 || source_row >= font_height) continue;
        pixels = (uint32_t *)((unsigned char *)output->back.map +
                              py * output->back.pitch);
        for (glyph_column = 0; glyph_column < cw; ++glyph_column) {
            int source_column = glyph_column * font_width / cw;
            int px = x + glyph_column + glyph_row_lean(lean, glyph_row, ch);
            if ((row_bits & (1U << (font_width - source_column - 1))) &&
                px >= 0 && px < output->mode.hdisplay)
                pixels[px] = color;
        }
    }
}

static void draw_graphic(KmsOutput *output, const BlitGraphicsDraw *draw,
                         int origin_x, int origin_y, int clip_width,
                         int clip_height)
{
    const BlitGraphicsImage *image = draw->image;
    int width = draw->width;
    int height = draw->height;
    int bytes_per_pixel = image->format == 24 ? 3 : 4;
    int destination_y;

    if (width < 1 || height < 1 || draw->src_w < 1 || draw->src_h < 1)
        return;
    /* Emulator frames normally arrive at their presentation size.  Keep the
       native-size path free of the scaler's two integer divisions and
       per-pixel source-coordinate calculation.  Clipping is still applied so
       a canvas may safely extend beyond a pane or output. */
    if (width == draw->src_w && height == draw->src_h) {
        int destination_x = origin_x + draw->x;
        int destination_y = origin_y + draw->y;
        int first_x = destination_x > origin_x ? destination_x : origin_x;
        int first_y = destination_y > origin_y ? destination_y : origin_y;
        int last_x = destination_x + width;
        int last_y = destination_y + height;
        int clip_last_x = origin_x + clip_width;
        int clip_last_y = origin_y + clip_height;
        int y;

        if (last_x > clip_last_x) last_x = clip_last_x;
        if (last_y > clip_last_y) last_y = clip_last_y;
        if (first_x < 0) first_x = 0;
        if (first_y < 0) first_y = 0;
        if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
        if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
        if (first_x >= last_x || first_y >= last_y)
            return;
        for (y = first_y; y < last_y; ++y) {
            int x;
            int source_y = draw->src_y + (y - destination_y);
            int source_x = draw->src_x + (first_x - destination_x);
            const unsigned char *source = image->pixels +
                ((size_t)source_y * (size_t)image->width +
                 (size_t)source_x) * (size_t)bytes_per_pixel;
            uint32_t *destination = (uint32_t *)((unsigned char *)output->back.map +
                (size_t)y * output->back.pitch) + first_x;
            if (image->format == 32 && image->opaque) {
                const uint32_t *source_pixels = (const uint32_t *)source;
                for (x = first_x; x < last_x; ++x) {
                    uint32_t rgba = *source_pixels++;
                    *destination++ = ((rgba & 0x000000ffU) << 16) |
                        (rgba & 0x0000ff00U) |
                        ((rgba & 0x00ff0000U) >> 16);
                }
                continue;
            }
            for (x = first_x; x < last_x; ++x) {
                unsigned int alpha = image->format == 32 ? source[3] : 255U;
                if (alpha == 255U) {
                    *destination = ((uint32_t)source[0] << 16) |
                        ((uint32_t)source[1] << 8) | source[2];
                } else if (alpha != 0U) {
                    uint32_t old = *destination;
                    unsigned int inverse = 255U - alpha;
                    unsigned int red = (old >> 16) & 0xffU;
                    unsigned int green = (old >> 8) & 0xffU;
                    unsigned int blue = old & 0xffU;
                    *destination = (((unsigned int)source[0] * alpha +
                                     red * inverse) / 255U << 16) |
                        (((unsigned int)source[1] * alpha + green * inverse) /
                         255U << 8) |
                        (((unsigned int)source[2] * alpha + blue * inverse) /
                         255U);
                }
                ++destination;
                source += bytes_per_pixel;
            }
        }
        return;
    }
    for (destination_y = 0; destination_y < height; ++destination_y) {
        int py = origin_y + draw->y + destination_y;
        int source_y = draw->src_y + destination_y * draw->src_h / height;
        int destination_x;
        if (py < 0 || py >= output->mode.vdisplay) continue;
        for (destination_x = 0; destination_x < width; ++destination_x) {
            int px = origin_x + draw->x + destination_x;
            int source_x = draw->src_x + destination_x * draw->src_w / width;
            const unsigned char *source;
            uint32_t *destination;
            unsigned int alpha;
            if (px < 0 || px >= output->mode.hdisplay ||
                px < origin_x || px >= origin_x + clip_width ||
                py < origin_y || py >= origin_y + clip_height) continue;
            source = image->pixels +
                ((size_t)source_y * (size_t)image->width + (size_t)source_x) *
                (size_t)bytes_per_pixel;
            destination = (uint32_t *)((unsigned char *)output->back.map +
                (size_t)py * output->back.pitch) + px;
            alpha = image->format == 32 ? source[3] : 255U;
            if (alpha == 255U) {
                *destination = ((uint32_t)source[0] << 16) |
                    ((uint32_t)source[1] << 8) | source[2];
            } else if (alpha != 0U) {
                uint32_t old = *destination;
                unsigned int inverse = 255U - alpha;
                unsigned int red = (old >> 16) & 0xffU;
                unsigned int green = (old >> 8) & 0xffU;
                unsigned int blue = old & 0xffU;
                *destination = (((unsigned int)source[0] * alpha +
                                 red * inverse) / 255U << 16) |
                    (((unsigned int)source[1] * alpha + green * inverse) /
                     255U << 8) |
                    (((unsigned int)source[2] * alpha + blue * inverse) /
                     255U);
            }
        }
    }
}

static void draw_view(KmsOutput *output, const BlitFont *font,
                      const BlitFont *wide_font,
                      const BlitKmsSurfaceView *view)
{
    int row;
    int column;
    int cw = view->cell_width > 0 ? view->cell_width :
        (font != NULL && font->width > 0 ? font->width : KMS_FONT_W);
    int ch = view->cell_height > 0 ? view->cell_height :
        (font != NULL && font->height > 0 ? font->height : KMS_FONT_H);
    int first_row = 0;
    int last_row = view->rows;
    int first_column = 0;
    int last_column = view->columns;
    int background_uniform = 1;
    int background_seen = 0;
    int italic_lean = (ch + 15) / 16;
    uint32_t uniform_background = 0;
    if (view->damage_valid) {
        first_row = view->damage_y;
        last_row = view->damage_y + view->damage_height;
        first_column = view->damage_x;
        last_column = view->damage_x + view->damage_width;
        if (first_row < 0) first_row = 0;
        if (first_column < 0) first_column = 0;
        if (last_row > view->rows) last_row = view->rows;
        if (last_column > view->columns) last_column = view->columns;
    }
    /* Hibr themes commonly paint a single background across a complete
       surface.  Detect that case once so the hot cell loop does not invoke
       fill_rect for every cell; mixed backgrounds retain the exact old path. */
    for (row = first_row; row < last_row && background_uniform; ++row) {
        for (column = first_column; column < last_column; ++column) {
            uint32_t background = view_cell_background(
                view, row * view->stride + column);
            if (!background_seen) {
                uniform_background = background;
                background_seen = 1;
            } else if (background != uniform_background) {
                background_uniform = 0;
                break;
            }
        }
    }
    if (background_uniform && background_seen && uniform_background != 0)
        fill_rect(output, view->origin_x + first_column * cw,
                  view->origin_y + first_row * ch,
                  (last_column - first_column) * cw,
                  (last_row - first_row) * ch, uniform_background);
    else if (!background_uniform) {
        /* Mixed Hibr surfaces often use long runs of the same background
           color.  Fill those runs once per row; a rectangle per cell makes
           a full-screen redraw needlessly expensive on a dumb KMS buffer. */
        for (row = first_row; row < last_row; ++row) {
            int run_column = first_column;
            while (run_column < last_column) {
                int run_end = run_column + 1;
                uint32_t run_background = view_cell_background(
                    view, row * view->stride + run_column);
                while (run_end < last_column &&
                       view_cell_background(view, row * view->stride + run_end) ==
                       run_background)
                    ++run_end;
                if (run_background != 0) {
                    fill_rect(output,
                              view->origin_x + run_column * cw,
                              view->origin_y + row * ch,
                              (run_end - run_column) * cw, ch,
                              run_background);
                }
                run_column = run_end;
            }
        }
    }
    for (row = first_row; row < last_row; ++row) {
        for (column = first_column; column < last_column; ++column) {
            int index = row * view->stride + column;
            uint32_t fg = view->foreground[index] ?
                view->foreground[index] - 1 : view->default_foreground;
            uint32_t bg = view->background[index] ?
                view->background[index] - 1 : view->default_background;
            uint32_t underline = view->underline_color != NULL &&
                view->underline_color[index] ?
                view->underline_color[index] - 1 : fg;
            uint32_t cp = view->codepoints[index] ? view->codepoints[index] : ' ';
            uint16_t attr = view->attributes[index];
            int glyph_width = (attr & KMS_ATTR_WIDE) ? cw * 2 : cw;
            int lean = (attr & KMS_ATTR_ITALIC) ? italic_lean : 0;
            int x = view->origin_x + column * cw;
            int y = view->origin_y + row * ch;
            if (attr & 8) { uint32_t t = fg; fg = bg; bg = t; }
            if (attr & 2) fg = (fg & 0xfefefeU) >> 1;
            if (cp == ' ' && bg == 0 && !(attr & 4) &&
                (view->combining_count == NULL ||
                 view->combining_count[index] == 0))
                continue;
            /* ADR 0062: a wide character's glyph is 16 pixels wide and lives
               in the wide font; a codepoint that font does not map stays on
               the primary one, whose substitute is the visible marker. */
            const BlitFont *glyph_font =
                (attr & KMS_ATTR_WIDE) && wide_font != NULL &&
                blit_font_has_glyph(wide_font, cp) ? wide_font : font;
            if (!(attr & KMS_ATTR_CONTINUATION)) {
                draw_glyph(output, glyph_font, cp, x, y, glyph_width, ch, fg,
                           lean, 0);
            }
            if (!(attr & KMS_ATTR_CONTINUATION) && (attr & 4))
                fill_rect(output, x, y + ch - 1, glyph_width, 1, underline);
            if (!(attr & KMS_ATTR_CONTINUATION) && (attr & 512))
                fill_rect(output, x, y + ch / 2, glyph_width, 1, fg);
            if (!(attr & KMS_ATTR_CONTINUATION) && (attr & 1))
                /* Shifted a pixel right, so it gives up its last column
                   rather than writing into the next cell. */
                draw_glyph(output, glyph_font, cp, x + 1, y, glyph_width, ch,
                           fg, lean, 1);
            if (!(attr & KMS_ATTR_CONTINUATION) && view->combining != NULL &&
                view->combining_count != NULL) {
                int combining_index;
                for (combining_index = 0;
                     combining_index < view->combining_count[index] &&
                     combining_index < 4; ++combining_index) {
                    uint32_t mark = view->combining[index * 4 + combining_index];
                    draw_glyph(output, font, mark, x, y, glyph_width, ch, fg,
                               lean, 0);
                }
            }
        }
    }
    if (view->graphics != NULL) {
        BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
        int count = blit_graphics_collect(view->graphics, view->graphics_count,
                                          draws, BLIT_GRAPHICS_MAX_DRAWS);
        int index;
        for (index = 0; index < count; ++index)
            draw_graphic(output, &draws[index], view->origin_x,
                         view->origin_y, view->columns * cw,
                         view->rows * ch);
    }
    if (view->cursor_visible && view->cursor_x >= 0 && view->cursor_y >= 0 &&
        view->cursor_y < view->rows) {
        int cursor_column = view->cursor_x < view->columns ?
            view->cursor_x : view->columns - 1;
        int cursor_cells = 1;
        int cursor_index;
        uint16_t cursor_attr;
        int cursor_x;
        int cursor_y = view->origin_y + view->cursor_y * ch;
        if (cursor_column < 0) cursor_column = 0;
        cursor_index = view->cursor_y * view->stride + cursor_column;
        cursor_attr = view->attributes[cursor_index];
        /* A wide character owns both of its cells, so the cursor covers the
           pair rather than half a glyph. */
        if ((cursor_attr & KMS_ATTR_CONTINUATION) && cursor_column > 0) {
            --cursor_column;
            cursor_index = view->cursor_y * view->stride + cursor_column;
            cursor_attr = view->attributes[cursor_index];
        }
        if (cursor_attr & KMS_ATTR_WIDE) cursor_cells = 2;
        cursor_x = view->origin_x + cursor_column * cw;
        if (view->cursor_style == 3 || view->cursor_style == 4)
            fill_rect(output, cursor_x, cursor_y + ch - 1,
                      cw * cursor_cells, 1, KMS_WHITE);
        else if (view->cursor_style == 5 || view->cursor_style == 6)
            fill_rect(output, cursor_x, cursor_y, 1, ch, KMS_WHITE);
        else {
            uint32_t cursor_cp = view->codepoints[cursor_index];
            uint32_t cursor_bg = view_cell_background(view, cursor_index);
            const BlitFont *cursor_font =
                (cursor_attr & KMS_ATTR_WIDE) && wide_font != NULL &&
                blit_font_has_glyph(wide_font, cursor_cp) ? wide_font : font;
            fill_rect(output, cursor_x, cursor_y, cw * cursor_cells, ch,
                      KMS_WHITE);
            /* A block cursor is an inverse cell, not an eraser: the
               character under it is what the user is about to overwrite and
               has to stay readable. */
            if (cursor_cp != 0 && cursor_cp != ' ' &&
                !(cursor_attr & KMS_ATTR_CONCEAL))
                draw_glyph(output, cursor_font, cursor_cp, cursor_x, cursor_y,
                           cw * cursor_cells, ch, cursor_bg,
                           (cursor_attr & KMS_ATTR_ITALIC) ? italic_lean : 0,
                           0);
        }
    }
}

static int wait_flip(BlitKms *kms, KmsOutput *output)
{
    drmEventContext context;
    fd_set set;
    struct timeval timeout;
    int result;
    long long deadline;
    int timeout_ms = kms_flip_timeout_ms();
    if (!output->flip_pending) return 0;
    memset(&context, 0, sizeof(context));
    context.version = DRM_EVENT_CONTEXT_VERSION;
    context.page_flip_handler = kms_flip_handler;
    output->flip_done = 0;
    deadline = kms_now_us() + (long long)timeout_ms * 1000LL;
    while (!output->flip_done) {
        long long remaining = deadline - kms_now_us();
        if (remaining <= 0) return -2;
        FD_ZERO(&set); FD_SET(kms->fd, &set);
        timeout.tv_sec = (long)(remaining / 1000000LL);
        timeout.tv_usec = (long)(remaining % 1000000LL);
        result = select(kms->fd + 1, &set, NULL, NULL, &timeout);
        if (result == 0) return -2;
        if (result < 0) {
            if (errno == EINTR) continue;
            return -1;
        }
        if (drmHandleEvent(kms->fd, &context) < 0) return -1;
    }
    return 0;
}

static void write_screenshot(BlitKms *kms, const KmsOutput *output)
{
    char path[512];
    FILE *file;
    unsigned char *rgb_row;
    size_t row_bytes;
    int y;

    if (kms == NULL || output == NULL || kms->screenshot_dir[0] == '\0')
        return;
    snprintf(path, sizeof(path), "%s/display-%llu.ppm",
             kms->screenshot_dir, output->info.id);
    file = fopen(path, "wb");
    if (file == NULL)
        return;
    row_bytes = (size_t)output->mode.hdisplay * 3U;
    rgb_row = (unsigned char *)malloc(row_bytes);
    if (rgb_row == NULL) {
        fclose(file);
        return;
    }
    fprintf(file, "P6\n%d %d\n255\n", output->mode.hdisplay,
            output->mode.vdisplay);
    for (y = 0; y < output->mode.vdisplay; ++y) {
        int x;
        const unsigned char *row = (const unsigned char *)output->back.map +
            (size_t)y * output->back.pitch;
        for (x = 0; x < output->mode.hdisplay; ++x) {
            rgb_row[x * 3 + 0] = row[x * 4 + 2];
            rgb_row[x * 3 + 1] = row[x * 4 + 1];
            rgb_row[x * 3 + 2] = row[x * 4 + 0];
        }
        if (fwrite(rgb_row, 1, row_bytes, file) != row_bytes)
            break;
    }
    free(rgb_row);
    fclose(file);
}

static int output_view_unchanged(const KmsOutput *output,
                                 const BlitKmsSurfaceView *view)
{
    int i;
    for (i = 0; i < output->last_view_count && i < KMS_MAX_VIEWS; ++i)
        if (output->last_surface_ids[i] == view->surface_id)
            return output->last_surface_revisions[i] == view->surface_revision;
    return 0;
}

static void remember_output_views(KmsOutput *output,
                                  const BlitKmsSurfaceView *views,
                                  int view_count, int output_view_count)
{
    unsigned long long previous_ids[KMS_MAX_VIEWS];
    unsigned long long previous_revisions[KMS_MAX_VIEWS];
    int previous_count = output->last_view_count < KMS_MAX_VIEWS ?
        output->last_view_count : KMS_MAX_VIEWS;
    int view_index;
    int stored = 0;
    int i;

    if (previous_count < 0) previous_count = 0;
    for (i = 0; i < previous_count; ++i) {
        previous_ids[i] = output->last_surface_ids[i];
        previous_revisions[i] = output->last_surface_revisions[i];
    }
    output->last_view_count = output_view_count;
    for (view_index = 0; view_index < view_count &&
         stored < KMS_MAX_VIEWS; ++view_index) {
        if (views[view_index].display_id != output->info.id)
            continue;
        output->last_surface_ids[stored] = views[view_index].surface_id;
        output->last_surface_revisions[stored] =
            views[view_index].surface_revision;
        /* A synchronized view was not drawn.  Remembering the revision it
           carries mid-batch would make the release frame look like one that
           had already been presented, and it would never be drawn.  Keep the
           revision this output last actually put on screen. */
        if (views[view_index].synchronized)
            for (i = 0; i < previous_count; ++i)
                if (previous_ids[i] == views[view_index].surface_id)
                    output->last_surface_revisions[stored] =
                        previous_revisions[i];
        ++stored;
    }
}

static int pointer_intersects_output(const KmsOutput *output,
                                     float x, float y, int visible)
{
    int local_x;
    int local_y;
    if (!visible) return 0;
    local_x = (int)x - output->info.x;
    local_y = (int)y - output->info.y;
    return local_x < output->mode.hdisplay && local_x + 12 > 0 &&
        local_y < output->mode.vdisplay && local_y + 18 > 0;
}

static void restore_pointer_background(KmsOutput *output)
{
    int row;

    if (output == NULL || !output->pointer_background_valid)
        return;
    for (row = 0; row < output->pointer_background_height; ++row) {
        unsigned char *destination =
            (unsigned char *)output->back.map +
            (size_t)(output->pointer_background_y + row) * output->back.pitch +
            (size_t)output->pointer_background_x * 4;
        memcpy(destination, output->pointer_background +
               (size_t)row * KMS_POINTER_W * 4,
               (size_t)output->pointer_background_width * 4);
    }
    output->pointer_background_valid = 0;
}

static void save_pointer_background(KmsOutput *output, float x, float y)
{
    int left = (int)x - output->info.x;
    int top = (int)y - output->info.y;
    int right = left + KMS_POINTER_W;
    int bottom = top + KMS_POINTER_H;
    int row;

    if (left < 0) left = 0;
    if (top < 0) top = 0;
    if (right > output->mode.hdisplay) right = output->mode.hdisplay;
    if (bottom > output->mode.vdisplay) bottom = output->mode.vdisplay;
    if (left >= right || top >= bottom) {
        output->pointer_background_valid = 0;
        return;
    }
    output->pointer_background_x = left;
    output->pointer_background_y = top;
    output->pointer_background_width = right - left;
    output->pointer_background_height = bottom - top;
    for (row = 0; row < output->pointer_background_height; ++row) {
        const unsigned char *source =
            (const unsigned char *)output->back.map +
            (size_t)(top + row) * output->back.pitch + (size_t)left * 4;
        memcpy(output->pointer_background +
               (size_t)row * KMS_POINTER_W * 4, source,
               (size_t)output->pointer_background_width * 4);
    }
    output->pointer_background_valid = 1;
}

static void include_pointer_damage(const KmsOutput *output,
                                   const BlitKmsSurfaceView *view,
                                   float x, float y, int visible,
                                   int *damage_valid, int *damage_x,
                                   int *damage_y, int *damage_width,
                                   int *damage_height)
{
    int local_x;
    int local_y;
    int left;
    int top;
    int right;
    int bottom;
    int first_column;
    int first_row;
    int last_column;
    int last_row;
    int cw;
    int ch;
    if (!pointer_intersects_output(output, x, y, visible)) return;
    local_x = (int)x - output->info.x;
    local_y = (int)y - output->info.y;
    cw = view->cell_width > 0 ? view->cell_width : KMS_FONT_W;
    ch = view->cell_height > 0 ? view->cell_height : KMS_FONT_H;
    left = local_x > view->origin_x ? local_x : view->origin_x;
    top = local_y > view->origin_y ? local_y : view->origin_y;
    right = local_x + 12 < view->origin_x + view->columns * cw ?
        local_x + 12 : view->origin_x + view->columns * cw;
    bottom = local_y + 18 < view->origin_y + view->rows * ch ?
        local_y + 18 : view->origin_y + view->rows * ch;
    if (left >= right || top >= bottom) return;
    first_column = (left - view->origin_x) / cw;
    first_row = (top - view->origin_y) / ch;
    last_column = (right - view->origin_x + cw - 1) / cw;
    last_row = (bottom - view->origin_y + ch - 1) / ch;
    if (first_column < 0) first_column = 0;
    if (first_row < 0) first_row = 0;
    if (last_column > view->columns) last_column = view->columns;
    if (last_row > view->rows) last_row = view->rows;
    if (first_column >= last_column || first_row >= last_row) return;
    if (!*damage_valid) {
        *damage_valid = 1;
        *damage_x = first_column;
        *damage_y = first_row;
        *damage_width = last_column - first_column;
        *damage_height = last_row - first_row;
        return;
    }
    if (first_column < *damage_x) {
        *damage_width += *damage_x - first_column;
        *damage_x = first_column;
    }
    if (first_row < *damage_y) {
        *damage_height += *damage_y - first_row;
        *damage_y = first_row;
    }
    if (last_column > *damage_x + *damage_width)
        *damage_width = last_column - *damage_x;
    if (last_row > *damage_y + *damage_height)
        *damage_height = last_row - *damage_y;
}

static void clear_pointer_rect(KmsOutput *output, float x, float y,
                               int visible)
{
    int local_x;
    int local_y;
    if (!pointer_intersects_output(output, x, y, visible)) return;
    local_x = (int)x - output->info.x;
    local_y = (int)y - output->info.y;
    fill_rect(output, local_x, local_y, 12, 18, 0);
}

static int view_has_content(const BlitKmsSurfaceView *view)
{
    int y;
    int x;
    if (view == NULL || view->codepoints == NULL)
        return 0;
    for (y = 0; y < view->rows; ++y)
        for (x = 0; x < view->columns; ++x)
            if (view->codepoints[y * view->stride + x] > 32)
                return 1;
    return 0;
}

static int all_outputs_have_content(const BlitKms *kms,
                                    const BlitKmsSurfaceView *views,
                                    int view_count)
{
    int output_index;
    int view_index;
    if (kms == NULL || views == NULL || view_count < 1)
        return 0;
    for (output_index = 0; output_index < kms->output_count; ++output_index) {
        int found = 0;
        for (view_index = 0; view_index < view_count; ++view_index) {
            if (views[view_index].display_id !=
                kms->outputs[output_index].info.id)
                continue;
            if (view_has_content(&views[view_index])) {
                found = 1;
                break;
            }
        }
        if (!found) return 0;
    }
    return 1;
}

BlitKms *blit_kms_new(BlitFont *font, BlitFont *wide_font)
{
    BlitKms *kms = (BlitKms *)calloc(1, sizeof(BlitKms));
    if (kms != NULL) { kms->fd = -1; kms->font = font;
    kms->wide_font = wide_font; }
    return kms;
}

int blit_kms_open(BlitKms *kms, char *error, size_t error_capacity)
{
    drmModeRes *resources;
    uint32_t used[KMS_MAX_OUTPUTS];
    int used_count = 0;
    int i;
    if (kms == NULL) return -1;
    /* Page-flip submission belongs to the gateway's event loop.  A blocking
       DRM descriptor lets a slow driver or virtual vblank stall every other
       output, PTY, and input device while drmModePageFlip waits in the ioctl.
       Keep submission and event draining nonblocking; readiness and the
       bounded per-output timeout remain the only sources of waiting. */
    kms->fd = open(getenv("BLIT_DRM_DEVICE") ? getenv("BLIT_DRM_DEVICE") :
                   "/dev/dri/card0", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (kms->fd < 0) { set_error(error, error_capacity, strerror(errno)); return -1; }
    {
        const char *screenshot_dir = getenv("BLIT_KMS_SCREENSHOT_DIR");
        const char *screenshot_after = getenv("BLIT_KMS_SCREENSHOT_AFTER");
        if (screenshot_dir != NULL)
            snprintf(kms->screenshot_dir, sizeof(kms->screenshot_dir), "%s",
                     screenshot_dir);
        kms->screenshot_after = screenshot_after != NULL ?
            (unsigned long)strtoul(screenshot_after, NULL, 10) : 30;
    }
    {
        const char *configured = getenv("BLIT_KMS_CELL_WIDTH");
        if (configured != NULL && configured[0] != '\0')
            kms->cell_width_override = atoi(configured);
        if (kms->cell_width_override < 5 || kms->cell_width_override > 32)
            kms->cell_width_override = 0;
    }
    resources = drmModeGetResources(kms->fd);
    if (resources == NULL) { set_error(error, error_capacity, "DRM resources unavailable"); return -1; }
    for (i = 0; i < resources->count_connectors && kms->output_count < KMS_MAX_OUTPUTS; ++i) {
        drmModeConnector *connector = drmModeGetConnector(kms->fd, resources->connectors[i]);
        KmsOutput *output;
        int mode_index;
        int inventory_index;
        if (connector == NULL || connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
            if (connector) drmModeFreeConnector(connector);
            continue;
        }
        output = &kms->outputs[kms->output_count];
        memset(output, 0, sizeof(*output));
        output->connector = connector->connector_id;
        mode_index = choose_mode(connector);
        output->mode = connector->modes[mode_index];
        output->mode_count = connector->count_modes < KMS_MAX_MODES ?
            connector->count_modes : KMS_MAX_MODES;
        output->active_mode = 0;
        for (inventory_index = 0; inventory_index < output->mode_count;
             ++inventory_index) {
            drmModeModeInfo *mode = &connector->modes[inventory_index];
            output->modes[inventory_index].width = mode->hdisplay;
            output->modes[inventory_index].height = mode->vdisplay;
            output->modes[inventory_index].refresh_hz = mode->vrefresh > 0 ?
                (double)mode->vrefresh : 60.0;
            output->modes[inventory_index].preferred =
                (mode->type & DRM_MODE_TYPE_PREFERRED) != 0;
            if (inventory_index == mode_index)
                output->active_mode = inventory_index;
        }
        if (choose_crtc(kms->fd, resources, connector, used, used_count,
                        &output->crtc) < 0) { drmModeFreeConnector(connector); continue; }
        output->old_crtc = drmModeGetCrtc(kms->fd, output->crtc);
        output->info.id = output->connector;
        snprintf(output->info.name, sizeof(output->info.name), "KMS connector %u", output->connector);
        output->info.x = 0; output->info.y = 0;
        {
            int previous;
            for (previous = 0; previous < kms->output_count; ++previous)
                output->info.x += kms->outputs[previous].info.width;
        }
        output->info.width = output->mode.hdisplay; output->info.height = output->mode.vdisplay;
        output->info.refresh_hz = output->mode.vrefresh > 0 ?
            (double)output->mode.vrefresh : 60.0;
        output->info.mode_count = output->mode_count;
        output->info.active_mode = output->active_mode;
        memcpy(output->info.modes, output->modes,
               sizeof(output->info.modes));
        output->info.presentation_state = BLIT_KMS_PRESENTATION_PRESENTING;
    if (make_buffer(kms->fd, output, &output->front) < 0 ||
        make_buffer(kms->fd, output, &output->back) < 0) {
        drmModeFreeConnector(connector); drmModeFreeResources(resources); return -1;
    }
    fprintf(stderr,
            "blit: KMS output connector=%u mode=%dx%d pitch=%u/%u size=%llu/%llu\n",
            output->connector, output->mode.hdisplay, output->mode.vdisplay,
            output->front.pitch, output->back.pitch,
            (unsigned long long)output->front.size,
            (unsigned long long)output->back.size);
    if (drmModeSetCrtc(kms->fd, output->crtc, output->front.fb, 0, 0,
                           &output->connector, 1, &output->mode) < 0) {
            free_buffer(kms->fd, &output->front); free_buffer(kms->fd, &output->back);
            drmModeFreeConnector(connector); drmModeFreeResources(resources); return -1;
        }
        used[used_count++] = output->crtc;
        ++kms->output_count;
        drmModeFreeConnector(connector);
    }
    drmModeFreeResources(resources);
    if (kms->output_count == 0) { set_error(error, error_capacity, "no connected DRM/KMS outputs"); return -1; }
    return 0;
}

int blit_kms_display_count(const BlitKms *kms) { return kms ? kms->output_count : 0; }

int blit_kms_fd(const BlitKms *kms)
{
    return kms != NULL ? kms->fd : -1;
}

int blit_kms_service(BlitKms *kms, int readable)
{
    drmEventContext context;
    int i;
    int changed = 0;
    long long now;
    long long timeout_us;

    if (kms == NULL) return -1;
    if (readable) {
        memset(&context, 0, sizeof(context));
        context.version = DRM_EVENT_CONTEXT_VERSION;
        context.page_flip_handler = kms_flip_handler;
        if (drmHandleEvent(kms->fd, &context) < 0) return -1;
        for (i = 0; i < kms->output_count; ++i)
            if (!kms->outputs[i].flip_pending)
                changed = 1;
    }
    /* DRM hotplug changes are not page-flip events. Poll the bounded
       connector set so a disconnected output becomes visible to the API even
       when the driver accepts no subsequent page flip. */
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        drmModeConnector *connector = drmModeGetConnector(kms->fd,
                                                            output->connector);
        int connected = connector != NULL &&
            connector->connection == DRM_MODE_CONNECTED;
        if (connector != NULL)
            drmModeFreeConnector(connector);
        if (!connected && output->info.presentation_state !=
                            BLIT_KMS_PRESENTATION_FAILED) {
            output->flip_pending = 0;
            output->flip_started_us = 0;
            output->modeset_fallback = 0;
            output->info.presentation_state = BLIT_KMS_PRESENTATION_FAILED;
            fprintf(stderr,
                    "blit: KMS connector=%u disconnected; marking output failed\n",
                    output->connector);
            changed = 1;
        }
    }
    now = kms_now_us();
    timeout_us = (long long)kms_flip_timeout_ms() * 1000LL;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        if (output->info.presentation_state == BLIT_KMS_PRESENTATION_FAILED)
            continue;
        if (!output->flip_pending || output->flip_started_us <= 0 ||
            now - output->flip_started_us < timeout_us)
            continue;
        output->modeset_fallback = 1;
        output->flip_pending = 0;
        output->flip_started_us = 0;
        output->info.presentation_state = BLIT_KMS_PRESENTATION_RECOVERING;
        fprintf(stderr,
                "blit: KMS page-flip timeout on connector=%u; entering modeset recovery\n",
                output->connector);
        changed = 1;
    }
    return changed;
}

int blit_kms_display_info(const BlitKms *kms, int index, BlitKmsDisplayInfo *info)
{
    if (kms == NULL || info == NULL || index < 0 || index >= kms->output_count) return -1;
    *info = kms->outputs[index].info;
    return 0;
}

int blit_kms_set_display_position(BlitKms *kms, int index, int x, int y)
{
    if (kms == NULL || index < 0 || index >= kms->output_count) return -1;
    kms->outputs[index].info.x = x;
    kms->outputs[index].info.y = y;
    return 0;
}

int blit_kms_cell_width(const BlitKms *kms)
{
    if (kms != NULL && kms->cell_width_override > 0)
        return kms->cell_width_override;
    return kms != NULL ? blit_font_width(kms->font) : KMS_FONT_W;
}

int blit_kms_cell_height(const BlitKms *kms)
{
    return kms != NULL ? blit_font_height(kms->font) : KMS_FONT_H;
}

int blit_kms_present(BlitKms *kms, const BlitKmsSurfaceView *views, int view_count,
                     float pointer_x, float pointer_y, int pointer_visible,
                     uint32_t output_mask, const BlitKmsOverlay *overlays,
                     int overlay_count, unsigned long long visual_revision,
                     int effect_enabled, int effect_intensity,
                     uint32_t *submitted_output_mask,
                     uint32_t *deferred_output_mask)
{
    int i;
    int capture;
    int pointer_changed;
    int full_redraw;
    int deferred = 0;
    int failed = 0;
    int expected_captures = 0;
    int completed_captures = 0;
    long long started = 0;
    const char *screenshot_ready_file = getenv("BLIT_KMS_SCREENSHOT_READY_FILE");
    if (kms == NULL) return -1;
    if (submitted_output_mask != NULL) *submitted_output_mask = 0;
    if (deferred_output_mask != NULL) *deferred_output_mask = 0;
    for (i = 0; i < view_count; ++i)
        if (views[i].damage_consumed != NULL)
            *views[i].damage_consumed = 0;
    if (getenv("BLIT_KMS_TRACE_TIMING") != NULL)
        started = kms_now_us();

    capture = kms->screenshot_dir[0] != '\0' && !kms->screenshot_written &&
        kms->screenshot_frame >= kms->screenshot_after &&
        (screenshot_ready_file == NULL ||
         access(screenshot_ready_file, F_OK) == 0) &&
        all_outputs_have_content(kms, views, view_count);
    if (capture) {
        for (i = 0; i < kms->output_count; ++i)
            if ((output_mask & (1U << i)) &&
                kms->outputs[i].info.presentation_state !=
                    BLIT_KMS_PRESENTATION_FAILED)
                ++expected_captures;
    }
    pointer_changed = !kms->pointer_state_valid ||
        kms->previous_pointer_x != pointer_x ||
        kms->previous_pointer_y != pointer_y ||
        kms->previous_pointer_visible != pointer_visible;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        int view_index;
        int output_view_count = 0;
        long long output_started = 0;
        long long draw_started = 0;
        long long flip_started = 0;
        int modeset_frame;
        int next_damage_valid = 0;
        int next_damage_x = 0;
        int next_damage_y = 0;
        int next_damage_width = 0;
        int next_damage_height = 0;
        int trace_phases = getenv("BLIT_KMS_TRACE_PHASES") != NULL;
        if (output->info.presentation_state == BLIT_KMS_PRESENTATION_FAILED)
            continue;
        if (!(output_mask & (1U << i)))
            continue;
        output_started = kms_now_us();
        int output_changed;
        full_redraw = !output->initialized ||
            output->last_visual_revision != visual_revision;
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id)
                ++output_view_count;
        if (output_view_count != output->last_view_count)
            full_redraw = 1;
        output_changed = full_redraw || capture ||
            (pointer_changed &&
             (pointer_intersects_output(output, kms->previous_pointer_x,
                                        kms->previous_pointer_y,
                                        kms->previous_pointer_visible) ||
              pointer_intersects_output(output, pointer_x, pointer_y,
                                         pointer_visible)));
        if (!output_changed) {
            for (view_index = 0; view_index < view_count; ++view_index) {
                if (views[view_index].display_id != output->info.id)
                    continue;
                if (views[view_index].synchronized)
                    continue;
                if (!output_view_unchanged(output, &views[view_index])) {
                    output_changed = 1;
                    break;
                }
            }
        }
        /* A page-flipped buffer is owned by the CRTC until its event arrives.
           Do not block the gateway waiting for it: leave this output intact
           and retry it after the main loop services the DRM event. */
        if (output->flip_pending && getenv("BLIT_KMS_MODESET") == NULL &&
            !output->modeset_fallback) {
            deferred = 1;
            if (deferred_output_mask != NULL)
                *deferred_output_mask |= 1U << i;
            continue;
        }
        /* A render request can be generated by a PTY or control event that
           does not alter this output.  Do not submit a page flip in that
           case: page flips are not a heartbeat, and repeatedly submitting
           unchanged buffers makes a slow KMS driver look hung while wasting
           a CPU and event slot on every display. */
        if (!output_changed)
            continue;
        /* A timeout requests one recovery modeset.  It must not turn the
           normal frame path into a modeset-per-frame loop: that produces
           visible blanking and can make a slow KMS driver appear frozen. */
        modeset_frame = getenv("BLIT_KMS_MODESET") != NULL ||
            output->modeset_fallback;
        if (modeset_frame)
            output->flip_pending = 0;
        if (trace_phases)
            fprintf(stderr, "blit: KMS phase connector=%u wait_us=%lld\n",
                    output->connector, kms_now_us() - output_started);
        if (full_redraw) {
            memset(output->back.map, 0, output->back.size);
            output->pointer_background_valid = 0;
            next_damage_valid = 1;
            next_damage_width = output->mode.hdisplay;
            next_damage_height = output->mode.vdisplay;
        } else if (output->last_damage_valid) {
            copy_rect(output, output->last_damage_x, output->last_damage_y,
                      output->last_damage_width, output->last_damage_height);
        } else {
            memcpy(output->back.map, output->front.map, output->back.size);
            next_damage_valid = 1;
            next_damage_width = output->mode.hdisplay;
            next_damage_height = output->mode.vdisplay;
        }
        if (pointer_changed)
            restore_pointer_background(output);
        if (pointer_changed && !full_redraw) {
            include_damage(&next_damage_valid, &next_damage_x, &next_damage_y,
                           &next_damage_width, &next_damage_height,
                           (int)kms->previous_pointer_x - output->info.x,
                           (int)kms->previous_pointer_y - output->info.y,
                           KMS_POINTER_W, KMS_POINTER_H);
            include_damage(&next_damage_valid, &next_damage_x, &next_damage_y,
                           &next_damage_width, &next_damage_height,
                           (int)pointer_x - output->info.x,
                           (int)pointer_y - output->info.y,
                           KMS_POINTER_W, KMS_POINTER_H);
        }
        if (trace_phases)
            draw_started = kms_now_us();
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id) {
                BlitKmsSurfaceView draw = views[view_index];
                int pointer_damage = 0;
                /* Hold the last presented pixels.  They are already in this
                   buffer: a non-full frame is rebuilt from the front buffer
                   above, so leaving the view undrawn preserves it.  A genuine
                   full redraw started from a cleared buffer and has nothing
                   left to hold, so the current cells are drawn -- one tear
                   rather than a hole. */
                if (views[view_index].synchronized && !full_redraw)
                    continue;
                if (pointer_changed) {
                    include_pointer_damage(output, &views[view_index],
                                           kms->previous_pointer_x,
                                           kms->previous_pointer_y,
                                           kms->previous_pointer_visible,
                                           &pointer_damage, &draw.damage_x,
                                           &draw.damage_y, &draw.damage_width,
                                           &draw.damage_height);
                    include_pointer_damage(output, &views[view_index],
                                           pointer_x, pointer_y,
                                           pointer_visible, &pointer_damage,
                                           &draw.damage_x, &draw.damage_y,
                                           &draw.damage_width,
                                           &draw.damage_height);
                }
                if (pointer_damage) draw.damage_valid = 1;
                if (draw.damage_valid)
                    include_damage(&next_damage_valid, &next_damage_x,
                                   &next_damage_y, &next_damage_width,
                                   &next_damage_height,
                                   draw.origin_x + draw.damage_x *
                                       (draw.cell_width > 0 ? draw.cell_width : KMS_FONT_W),
                                   draw.origin_y + draw.damage_y *
                                       (draw.cell_height > 0 ? draw.cell_height : KMS_FONT_H),
                                   draw.damage_width *
                                       (draw.cell_width > 0 ? draw.cell_width : KMS_FONT_W),
                                   draw.damage_height *
                                       (draw.cell_height > 0 ? draw.cell_height : KMS_FONT_H));
                if (!full_redraw && output_view_unchanged(output,
                                                           &views[view_index]) &&
                    !pointer_damage)
                    continue;
                if (pointer_damage) draw.damage_valid = 1;
                if (!full_redraw && views[view_index].damage_valid) {
                    int damage_x = views[view_index].damage_x;
                    int damage_y = views[view_index].damage_y;
                    int damage_right = damage_x + views[view_index].damage_width;
                    int damage_bottom = damage_y + views[view_index].damage_height;
                    int cw = views[view_index].cell_width > 0 ?
                        views[view_index].cell_width : KMS_FONT_W;
                    int ch = views[view_index].cell_height > 0 ?
                        views[view_index].cell_height : KMS_FONT_H;
                    if (pointer_damage) {
                        if (damage_x > draw.damage_x) damage_x = draw.damage_x;
                        if (damage_y > draw.damage_y) damage_y = draw.damage_y;
                        if (damage_right < draw.damage_x + draw.damage_width)
                            damage_right = draw.damage_x + draw.damage_width;
                        if (damage_bottom < draw.damage_y + draw.damage_height)
                            damage_bottom = draw.damage_y + draw.damage_height;
                        draw.damage_x = damage_x;
                        draw.damage_y = damage_y;
                        draw.damage_width = damage_right - damage_x;
                        draw.damage_height = damage_bottom - damage_y;
                    }
                    fill_rect(output,
                              views[view_index].origin_x + damage_x * cw,
                              views[view_index].origin_y + damage_y * ch,
                              (damage_right - damage_x) * cw,
                              (damage_bottom - damage_y) * ch, 0);
                }
                if (full_redraw)
                    draw.damage_valid = 0;
                draw_view(output, kms != NULL ? kms->font : NULL,
                          kms != NULL ? kms->wide_font : NULL, &draw);
            }
        draw_kms_effects(output, effect_enabled, effect_intensity);
        draw_kms_overlays(output, overlays, overlay_count);
        if (effect_enabled) {
            next_damage_valid = 1;
            next_damage_x = 0;
            next_damage_y = 0;
            next_damage_width = output->mode.hdisplay;
            next_damage_height = output->mode.vdisplay;
        }
        if (pointer_visible && pointer_x >= output->info.x &&
            pointer_x < output->info.x + output->info.width &&
            pointer_y >= output->info.y && pointer_y < output->info.y + output->info.height) {
            int x = (int)pointer_x - output->info.x;
            int y = (int)pointer_y - output->info.y;
            save_pointer_background(output, pointer_x, pointer_y);
            fill_rect(output, x, y, 2, 18, 0xffffffU);
            fill_rect(output, x, y, 12, 2, 0xffffffU);
        } else {
            output->pointer_background_valid = 0;
        }
        if (trace_phases)
            flip_started = kms_now_us();
        if (capture) {
            write_screenshot(kms, output);
            ++completed_captures;
        }
        {
            unsigned int flags = DRM_MODE_PAGE_FLIP_EVENT;
            int flip_result;
            if (modeset_frame) {
                flip_result = drmModeSetCrtc(kms->fd, output->crtc,
                                             output->back.fb, 0, 0,
                                             &output->connector, 1,
                                             &output->mode);
                if (flip_result < 0) {
                    output->info.presentation_state =
                        BLIT_KMS_PRESENTATION_FAILED;
                    failed = 1;
                    continue;
                }
                if (output->modeset_fallback) {
                    fprintf(stderr,
                            "blit: KMS modeset recovery complete on connector=%u; resuming page flips\n",
                            output->connector);
                    output->modeset_fallback = 0;
                    output->info.presentation_state =
                        BLIT_KMS_PRESENTATION_PRESENTING;
                }
                output->flip_pending = 0;
                output->flip_started_us = 0;
                swap_buffers(output);
                for (view_index = 0; view_index < view_count; ++view_index)
                    if (views[view_index].display_id == output->info.id &&
                        views[view_index].damage_consumed != NULL)
                        *views[view_index].damage_consumed = 1;
                output->initialized = 1;
                output->last_visual_revision = visual_revision;
                output->last_damage_valid = 1;
                output->last_damage_x = 0;
                output->last_damage_y = 0;
                output->last_damage_width = output->mode.hdisplay;
                output->last_damage_height = output->mode.vdisplay;
                remember_output_views(output, views, view_count,
                                      output_view_count);
                output->info.last_present_ms =
                    (double)(kms_now_us() - output_started) / 1000.0;
                if (output->info.last_present_ms > output->info.max_present_ms)
                    output->info.max_present_ms = output->info.last_present_ms;
                ++output->info.present_calls;
                if (submitted_output_mask != NULL)
                    *submitted_output_mask |= 1U << i;
                continue;
            }
            if (getenv("BLIT_KMS_ASYNC_PAGEFLIP") != NULL)
                flags |= DRM_MODE_PAGE_FLIP_ASYNC;
            output->flip_done = 0;
            flip_result = drmModePageFlip(kms->fd, output->crtc,
                                          output->back.fb, flags, output);
            if (flip_result < 0 && (flags & DRM_MODE_PAGE_FLIP_ASYNC))
                flip_result = drmModePageFlip(kms->fd, output->crtc,
                                              output->back.fb,
                                              DRM_MODE_PAGE_FLIP_EVENT,
                                              output);
            if (flip_result < 0) {
                output->info.presentation_state =
                    BLIT_KMS_PRESENTATION_FAILED;
                failed = 1;
                continue;
            }
        }
        output->flip_pending = 1;
        output->flip_started_us = kms_now_us();
        if (submitted_output_mask != NULL)
            *submitted_output_mask |= 1U << i;
        swap_buffers(output);
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id &&
                views[view_index].damage_consumed != NULL)
                *views[view_index].damage_consumed = 1;
        output->initialized = 1;
        output->last_visual_revision = visual_revision;
        output->last_damage_valid = next_damage_valid;
        output->last_damage_x = next_damage_x;
        output->last_damage_y = next_damage_y;
        output->last_damage_width = next_damage_width;
        output->last_damage_height = next_damage_height;
        if (trace_phases)
            fprintf(stderr,
                    "blit: KMS phase connector=%u draw_us=%lld flip_us=%lld total_us=%lld\n",
                    output->connector, flip_started - draw_started,
                    kms_now_us() - flip_started,
                    kms_now_us() - output_started);
        output->info.last_present_ms =
            (double)(kms_now_us() - output_started) / 1000.0;
        if (output->info.last_present_ms > output->info.max_present_ms)
            output->info.max_present_ms = output->info.last_present_ms;
        ++output->info.present_calls;
        remember_output_views(output, views, view_count, output_view_count);
    }
    if (!deferred) {
        kms->pointer_state_valid = 1;
        kms->previous_pointer_x = pointer_x;
        kms->previous_pointer_y = pointer_y;
        kms->previous_pointer_visible = pointer_visible;
    }
    ++kms->screenshot_frame;
    if (capture && completed_captures >= expected_captures)
        kms->screenshot_written = 1;
    if (started != 0)
        fprintf(stderr, "blit: KMS present views=%d outputs=%d duration_us=%lld\n",
                view_count, kms->output_count, kms_now_us() - started);
    return failed ? -1 : 0;
}

static void kms_bmp32(unsigned char *header, uint32_t offset,
                      uint32_t file_size, int width, int height,
                      uint32_t image_size)
{
    memset(header, 0, 54);
    header[0] = 'B';
    header[1] = 'M';
    header[2] = (unsigned char)file_size;
    header[3] = (unsigned char)(file_size >> 8);
    header[4] = (unsigned char)(file_size >> 16);
    header[5] = (unsigned char)(file_size >> 24);
    header[10] = (unsigned char)offset;
    header[11] = (unsigned char)(offset >> 8);
    header[12] = (unsigned char)(offset >> 16);
    header[13] = (unsigned char)(offset >> 24);
    header[14] = 40;
    header[18] = (unsigned char)width;
    header[19] = (unsigned char)(width >> 8);
    header[20] = (unsigned char)(width >> 16);
    header[21] = (unsigned char)(width >> 24);
    header[22] = (unsigned char)height;
    header[23] = (unsigned char)(height >> 8);
    header[24] = (unsigned char)(height >> 16);
    header[25] = (unsigned char)(height >> 24);
    header[26] = 1;
    header[28] = 32;
    header[34] = (unsigned char)image_size;
    header[35] = (unsigned char)(image_size >> 8);
    header[36] = (unsigned char)(image_size >> 16);
    header[37] = (unsigned char)(image_size >> 24);
}

int blit_kms_screenshot(BlitKms *kms, int index,
                        unsigned char **bytes, size_t *length)
{
    KmsOutput *output;
    size_t row_bytes;
    size_t image_size;
    size_t total;
    unsigned char *result;
    int y;

    if (bytes == NULL || length == NULL) return -1;
    *bytes = NULL;
    *length = 0;
    if (kms == NULL || index < 0 || index >= kms->output_count)
        return -1;
    output = &kms->outputs[index];
    if (output->front.map == NULL || !output->initialized)
        return 0;
    if (output->mode.hdisplay <= 0 || output->mode.vdisplay <= 0)
        return -1;
    row_bytes = (size_t)output->mode.hdisplay * 4U;
    image_size = row_bytes * (size_t)output->mode.vdisplay;
    total = 54U + image_size;
    if (row_bytes / 4U != (size_t)output->mode.hdisplay ||
        image_size / row_bytes != (size_t)output->mode.vdisplay ||
        total < image_size || total > 64U * 1024U * 1024U)
        return -1;
    result = (unsigned char *)malloc(total);
    if (result == NULL) return -1;
    kms_bmp32(result, 54U, (uint32_t)total, output->mode.hdisplay,
              output->mode.vdisplay, (uint32_t)image_size);
    for (y = 0; y < output->mode.vdisplay; ++y) {
        const unsigned char *source =
            (const unsigned char *)output->front.map +
            (size_t)(output->mode.vdisplay - 1 - y) * output->front.pitch;
        memcpy(result + 54U + (size_t)y * row_bytes, source, row_bytes);
    }
    *bytes = result;
    *length = total;
    return 1;
}

void blit_kms_close(BlitKms *kms)
{
    int i;
    if (kms == NULL) return;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        wait_flip(kms, output);
        if (output->old_crtc != NULL) {
            drmModeSetCrtc(kms->fd, output->old_crtc->crtc_id,
                           output->old_crtc->buffer_id, output->old_crtc->x,
                           output->old_crtc->y, &output->connector, 1,
                           &output->old_crtc->mode);
            drmModeFreeCrtc(output->old_crtc);
        }
        free_buffer(kms->fd, &output->front);
        free_buffer(kms->fd, &output->back);
    }
    if (kms->fd >= 0) close(kms->fd);
    free(kms);
}
