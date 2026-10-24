#define _GNU_SOURCE
#include "blit_kms.h"
#include "blit_font.h"
/* ADR 0181: the GLES compositor is part of the native backend's unit. */
#include "blit_gles.c"
#include "blit_raster.h"
#include "blit_damage.h"
#include "blit_ink.h"

#include <errno.h>
#include <fcntl.h>
#include <poll.h>
#include <pthread.h>
#include <linux/netlink.h>
#include <libdrm/drm.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <time.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

#define KMS_MAX_OUTPUTS 16
#define KMS_MAX_VIEWS 64
#define KMS_MAX_MODES BLIT_KMS_MAX_MODES
#define KMS_FONT_W 8
#define KMS_FONT_H 8
#define KMS_POINTER_W BLIT_POINTER_MAX
#define KMS_POINTER_H BLIT_POINTER_MAX
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
    /* ADR 0181 GLES compositor. A GBM buffer is held from submission until
       the flip that replaces it completes: next is drawn, pending is
       flipping, shown is on the panel. */
    BlitGlesTarget *gles_target;
    BlitKms *owner;
    void *gles_next_bo, *gles_pending_bo, *gles_shown_bo;
    uint32_t gles_next_fb, gles_pending_fb, gles_shown_fb;
    unsigned long long generation;
    int allocated;
    uint32_t connector;
    uint32_t crtc;
    drmModeModeInfo mode;
    int mode_count;
    int active_mode;
    BlitKmsMode modes[KMS_MAX_MODES];
    drmModeCrtc *old_crtc;
    KmsBuffer front;
    KmsBuffer back;
    /* Terminal glyphs and graphics are composed into ordinary RAM.  Writing
       individual pixels directly to a mapped dumb buffer is especially slow
       on the fast-changing full-screen terminal workload; a sequential copy
       into the scanout buffer is both faster and remains atomic at page flip. */
    void *scene;
    uint64_t scene_size;
    int flip_pending;
    volatile int flip_done;
    long long flip_started_us;
    int modeset_fallback;
    int initialized;
    /* Last submitted cursor state belongs to this scanout, not the server.
       An unrelated output's deadline must never consume its pending motion. */
    int pointer_state_valid;
    float previous_pointer_x;
    float previous_pointer_y;
    int previous_pointer_visible;
    int last_view_count;
    unsigned long long last_visual_revision;
    unsigned long long last_surface_ids[KMS_MAX_VIEWS];
    unsigned long long last_surface_revisions[KMS_MAX_VIEWS];
    unsigned long long last_surface_graphics_signatures[KMS_MAX_VIEWS];
    int last_surface_origin_x[KMS_MAX_VIEWS];
    int last_surface_origin_y[KMS_MAX_VIEWS];
    int last_surface_graphics_valid[KMS_MAX_VIEWS];
    int last_surface_graphics_x[KMS_MAX_VIEWS];
    int last_surface_graphics_y[KMS_MAX_VIEWS];
    int last_surface_graphics_width[KMS_MAX_VIEWS];
    int last_surface_graphics_height[KMS_MAX_VIEWS];
    unsigned char pointer_background[KMS_POINTER_W * KMS_POINTER_H * 4];
    int pointer_background_valid;
    int pointer_background_x;
    int pointer_background_y;
    int pointer_background_width;
    int pointer_background_height;
    int pointer_width;
    int pointer_height;
    BlitFont *font;
    int glyph_clip_active;
    int glyph_clip_x0, glyph_clip_y0, glyph_clip_x1, glyph_clip_y1;
} KmsOutput;

typedef struct {
    uint32_t id;
    char name[64];
    int mode_count;
    drmModeModeInfo modes[KMS_MAX_MODES];
    int crtc_count;
    uint32_t crtcs[KMS_MAX_OUTPUTS];
} KmsConnector;

typedef struct {
    int valid;
    int count;
    int unknown_count;
    uint32_t unknown[KMS_MAX_OUTPUTS];
    KmsConnector connectors[KMS_MAX_OUTPUTS];
} KmsSnapshot;

typedef struct {
    KmsOutput *output;
    unsigned long long generation;
    int used;
} KmsFlipCookie;

#define KMS_MAX_FLIP_COOKIES 256

struct BlitKms {
    int fd;
    int suspended;
    int output_count;
    KmsOutput outputs[KMS_MAX_OUTPUTS];
    int slots[KMS_MAX_OUTPUTS];
    unsigned long long generation;
    unsigned long long inventory_revision;
    KmsFlipCookie flip_cookies[KMS_MAX_FLIP_COOKIES];
    pthread_t monitor_thread;
    pthread_mutex_t monitor_lock;
    int monitor_started;
    int monitor_stop[2];
    int monitor_socket;
    KmsSnapshot snapshot;
    unsigned long long snapshot_revision;
    unsigned long long applied_snapshot_revision;
    unsigned long long monitor_scans;
    double monitor_last_scan_ms;
    char hotplug_error[128];
    BlitFont *font;
    const BlitPointerArt *pointer_art;
    unsigned long long pointer_art_revision;
    int pointer_art_width;
    int pointer_art_height;
    /* ADR 0062: the 16x16 font for wide cells. */
    BlitFont *wide_font;
    int cell_width_override;
    char screenshot_dir[256];
    unsigned long screenshot_frame;
    unsigned long screenshot_after;
    int screenshot_written;
    BlitGles *gles;
    int gles_requested;
    char gles_error[160];
};

static KmsOutput *kms_output(const BlitKms *kms, int index)
{
    return (KmsOutput *)&kms->outputs[kms->slots[index]];
}

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

static void kms_gles_release_bo(BlitKms *kms, KmsOutput *output,
                               void **bo, uint32_t *fb);

static void kms_flip_handler(int fd, unsigned int frame, unsigned int sec,
                             unsigned int usec, void *data)
{
    KmsOutput *output = (KmsOutput *)data;
    (void)fd;
    if (output != NULL) {
        unsigned long long timestamp = (unsigned long long)sec * 1000000ULL + usec;
        if (output->info.flip_completions == 0) {
            output->info.vblank_sequence = frame;
            output->info.vblank_timestamp_us = timestamp;
        } else if (timestamp > output->info.vblank_timestamp_us) {
            uint32_t delta = frame - output->info.vblank_sequence;
            if (delta != 0) {
                output->info.vblank_span_count += delta;
                output->info.vblank_span_us += timestamp - output->info.vblank_timestamp_us;
                output->info.observed_refresh_hz =
                    (double)output->info.vblank_span_count * 1000000.0 /
                    (double)output->info.vblank_span_us;
                output->info.vblank_sequence = frame;
                output->info.vblank_timestamp_us = timestamp;
            }
        }
        ++output->info.flip_completions;
        if (output->gles_pending_bo != NULL && output->owner != NULL) {
            kms_gles_release_bo(output->owner, output, &output->gles_shown_bo,
                                &output->gles_shown_fb);
            output->gles_shown_bo = output->gles_pending_bo;
            output->gles_shown_fb = output->gles_pending_fb;
            output->gles_pending_bo = NULL;
            output->gles_pending_fb = 0;
        }
        output->flip_done = 1;
        output->flip_pending = 0;
        output->flip_started_us = 0;
    }
}

static void kms_flip_event(int fd, unsigned int frame, unsigned int sec,
                            unsigned int usec, void *data)
{
    KmsFlipCookie *cookie = data;
    if (cookie == NULL || !cookie->used) return;
    if (cookie->output->generation == cookie->generation)
        kms_flip_handler(fd, frame, sec, usec, cookie->output);
    cookie->used = 0;
}

static KmsFlipCookie *kms_flip_cookie(BlitKms *kms, KmsOutput *output)
{
    int i;
    for (i = 0; i < KMS_MAX_FLIP_COOKIES; ++i) {
        KmsFlipCookie *cookie = &kms->flip_cookies[i];
        if (cookie->used) continue;
        cookie->used = 1;
        cookie->output = output;
        cookie->generation = output->generation;
        return cookie;
    }
    snprintf(kms->hotplug_error, sizeof(kms->hotplug_error),
             "page_flip_cookie_capacity_exhausted");
    return NULL;
}

static void set_error(char *error, size_t capacity, const char *message)
{
    if (error != NULL && capacity > 0)
        snprintf(error, capacity, "%s", message);
}


static double mode_refresh_hz(const drmModeModeInfo *mode)
{
    double hz;
    /* The kernel rounds vrefresh to an integer for the userspace modeinfo.
       Keep the timing fraction so 59.94 Hz is not advertised as 60.0 Hz. */
    if (mode->clock == 0 || mode->htotal == 0 || mode->vtotal == 0)
        return mode->vrefresh > 0 ? (double)mode->vrefresh : 60.0;
    hz = (double)mode->clock * 1000.0 /
        ((double)mode->htotal * (double)mode->vtotal);
    if (mode->flags & DRM_MODE_FLAG_INTERLACE) hz *= 2.0;
    if (mode->flags & DRM_MODE_FLAG_DBLSCAN) hz /= 2.0;
    if (mode->vscan > 1) hz /= (double)mode->vscan;
    return hz;
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

/* After a page-flip, front is the buffer currently scanned out and back is
   the buffer that was displayed one frame earlier.  An incremental frame is
   drawn into back, so it must begin as a complete copy of front.  Copying
   only the previous damage rectangle leaves older pixels everywhere else in
   back; pointer motion and window movement then expose those pixels as
   trails.  This is the same complete-scene invariant used by the SDL scene
   ping-pong path. */
static void copy_front_to_back(KmsOutput *output)
{
    int row;

    if (output == NULL || output->front.map == NULL ||
        output->back.map == NULL)
        return;
    for (row = 0; row < output->mode.vdisplay; ++row)
        memcpy((unsigned char *)output->back.map +
                   (size_t)row * output->back.pitch,
               (const unsigned char *)output->front.map +
                   (size_t)row * output->front.pitch,
               output->mode.hdisplay * sizeof(uint32_t));
}

static int ensure_scene_buffer(KmsOutput *output)
{
    if (output == NULL || output->back.size == 0)
        return 0;
    if (output->scene != NULL && output->scene_size == output->back.size)
        return 1;
    free(output->scene);
    output->scene = calloc(1, (size_t)output->back.size);
    if (output->scene == NULL) {
        output->scene_size = 0;
        return 0;
    }
    output->scene_size = output->back.size;
    return 1;
}

static void copy_scene_to_back(KmsOutput *output)
{
    int row;

    if (output == NULL || output->scene == NULL || output->back.map == NULL)
        return;
    for (row = 0; row < output->mode.vdisplay; ++row)
        memcpy((unsigned char *)output->back.map +
                   (size_t)row * output->back.pitch,
               (const unsigned char *)output->scene +
                   (size_t)row * output->back.pitch,
               (size_t)output->mode.hdisplay * sizeof(uint32_t));
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
   -`lean` at the bottom, so the glyph stays centred in its cell. Rounded to
   the nearest pixel: truncating toward zero moved only the first and last
   rows at a lean of 1, where most glyphs have no ink, so italic drew as
   plain in 16-pixel cells. */
static int glyph_row_lean(int lean, int row, int rows)
{
    int numerator, denominator;
    if (lean == 0 || rows < 2)
        return 0;
    numerator = lean * (rows - 1 - 2 * row);
    denominator = rows - 1;
    return numerator >= 0 ?
        (2 * numerator + denominator) / (2 * denominator) :
        -((-2 * numerator + denominator) / (2 * denominator));
}

/* `trim` drops that many destination pixels from the right edge, and the
   matching source columns with them, so bold's overstrike gives up its last
   column instead of writing into the next cell. */
static void draw_cell_raster(KmsOutput *output, const BlitFont *font,
                              const BlitRasterGlyph *raster, int x, int y,
                              int cw, int ch, uint32_t color, int lean, int trim)
{
    int row, column;
    if (trim > 0 && raster->format == BLIT_RASTER_RGBA_PREMULTIPLIED) return;
    for (row = 0; row < raster->height; ++row) {
        int py = y + row + raster->ink_offset_y;
        uint32_t *pixels;
        if (py < 0 || py >= output->mode.vdisplay) continue;
        if (output->glyph_clip_active &&
            (py < output->glyph_clip_y0 || py >= output->glyph_clip_y1)) continue;
        pixels = (uint32_t *)((unsigned char *)output->back.map + py * output->back.pitch);
        for (column = 0; column < raster->width - trim; ++column) {
            int px = x + column + raster->ink_offset_x +
                glyph_row_lean(lean, row + raster->ink_offset_y, ch);
            if (output->glyph_clip_active &&
                (px < output->glyph_clip_x0 || px >= output->glyph_clip_x1)) continue;
            if (px >= 0 && px < output->mode.hdisplay)
                pixels[px] = font->raster_blend(raster, column, row, color, pixels[px]);
        }
    }
}

static void draw_glyph(KmsOutput *output, const BlitFont *font,
                       uint32_t codepoint, int x, int y, int cw, int ch,
                       uint32_t color, int lean, int trim)
{
    int glyph_row;
    int glyph_column;
    size_t glyph;
    int font_width = font != NULL && font->data ? font->width : 5;
    int font_height = font != NULL && font->data ? font->height : 7;
    if (font && font->raster_get && font->raster_blend) {
        BlitRasterGlyph raster;
        int columns = cw > font->raster_cell_width ? 2 : 1;
        if (font->raster_get(font->raster_state, codepoint, columns, &raster)) {
            draw_cell_raster(output, font, &raster, x, y, cw, ch, color, lean, trim);
            return;
        }
    }
    glyph = blit_font_glyph_index(font, codepoint);
    if (trim > 0) {
        if (trim >= cw) return;
        cw -= trim;
    }
    /* The unscaled fast path writes without a per-pixel bounds test, which a
       slant's overhang would break; a slanted glyph takes the general path. */
    if (!output->glyph_clip_active && lean == 0 && trim == 0 && font != NULL && font->data != NULL &&
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
        if (output->glyph_clip_active &&
            (py < output->glyph_clip_y0 || py >= output->glyph_clip_y1)) continue;
        if (source_row < 0 || source_row >= font_height) continue;
        pixels = (uint32_t *)((unsigned char *)output->back.map +
                              py * output->back.pitch);
        for (glyph_column = 0; glyph_column < cw; ++glyph_column) {
            int source_column = glyph_column * font_width / cw;
            int px = x + glyph_column + glyph_row_lean(lean, glyph_row, ch);
            if (output->glyph_clip_active &&
                (px < output->glyph_clip_x0 || px >= output->glyph_clip_x1)) continue;
            if ((row_bits & (1U << (font_width - source_column - 1))) &&
                px >= 0 && px < output->mode.hdisplay)
                pixels[px] = color;
        }
    }
}

/* Kitty z bands, shared with the SDL path (issue #84, #88): below
   -2^30 an image is under cell backgrounds, from there up to zero it is over
   backgrounds and under text, and from zero up it is over text. */
/* One cell path for normal text and block-cursor repaint. Unsupported
   combining text retains its established scalar overlays. */
static void draw_cell_glyphs(KmsOutput *output, const BlitFont *font,
                              const BlitFont *wide_font, uint32_t cp,
                              const uint32_t *marks, unsigned count,
                              int x, int y, int cw, int ch, uint32_t fg,
                              int lean, int bold)
{
    const BlitFont *glyph_font = blit_font_cell_source(font, wide_font, cp);
    BlitCellCluster key;
    BlitRasterGlyph raster;
    unsigned i;
    if (count && font && font->raster_cluster_get && font->raster_blend &&
        blit_cell_cluster_init(&key, cp, marks, count,
                               cw > font->raster_cell_width ? 2 : 1) &&
        font->raster_cluster_get(font->raster_state, &key, &raster)) {
        draw_cell_raster(output, font, &raster, x, y, cw, ch, fg, lean, 0);
        if (bold) draw_cell_raster(output, font, &raster, x + 1, y, cw, ch, fg, lean, 1);
        return;
    }
    draw_glyph(output, glyph_font, cp, x, y, cw, ch, fg, lean, 0);
    if (bold) draw_glyph(output, glyph_font, cp, x + 1, y, cw, ch, fg, lean, 1);
    if (marks) for (i = 0; i < count && i < BLIT_MAX_COMBINING; ++i)
        draw_glyph(output, font, marks[i], x, y, cw, ch, fg, lean, 0);
}

#define KMS_GRAPHICS_Z_UNDER_BACKGROUND (-1073741824)

/* The image is clipped to [clip_x0,clip_x1) x [clip_y0,clip_y1), an absolute
   rectangle, so an image drawn below the text on a damaged redraw cannot
   paint over text outside the damage. */
static void draw_graphic(KmsOutput *output, const BlitGraphicsDraw *draw,
                         int origin_x, int origin_y, int clip_x0, int clip_y0,
                         int clip_x1, int clip_y1)
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
        int first_x = destination_x > clip_x0 ? destination_x : clip_x0;
        int first_y = destination_y > clip_y0 ? destination_y : clip_y0;
        int last_x = destination_x + width;
        int last_y = destination_y + height;
        int y;

        if (last_x > clip_x1) last_x = clip_x1;
        if (last_y > clip_y1) last_y = clip_y1;
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

    /* Hibr wallpapers commonly arrive as an opaque RGB image and are placed
       at an exact integer scale (for example 960x1080 -> 1920x2160).  The
       general scaler below is correct, but its destination-pixel divisions
       make every damaged redraw needlessly expensive.  Replicate each source
       pixel over its integer destination rectangle instead.  Keep this path
       deliberately bounded and opaque-only: filtered and alpha images retain
       the general compositor semantics below. */
    if ((image->format == 24 || (image->format == 32 && image->opaque)) &&
        width % draw->src_w == 0 && height % draw->src_h == 0 &&
        width / draw->src_w <= 8 && height / draw->src_h <= 8) {
        int scale_x = width / draw->src_w;
        int scale_y = height / draw->src_h;
        int destination_x = origin_x + draw->x;
        int destination_y = origin_y + draw->y;
        int first_x = destination_x > clip_x0 ? destination_x : clip_x0;
        int first_y = destination_y > clip_y0 ? destination_y : clip_y0;
        int last_x = destination_x + width;
        int last_y = destination_y + height;
        int source_y;

        if (last_x > clip_x1) last_x = clip_x1;
        if (last_y > clip_y1) last_y = clip_y1;
        if (first_x < 0) first_x = 0;
        if (first_y < 0) first_y = 0;
        if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
        if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
        if (first_x >= last_x || first_y >= last_y)
            return;

        for (source_y = 0; source_y < draw->src_h; ++source_y) {
            int row_y = destination_y + source_y * scale_y;
            int row_last_y = row_y + scale_y;
            int visible_y0 = row_y > first_y ? row_y : first_y;
            int visible_y1 = row_last_y < last_y ? row_last_y : last_y;
            int source_x;
            if (visible_y0 >= visible_y1)
                continue;
            for (source_x = 0; source_x < draw->src_w; ++source_x) {
                int column_x = destination_x + source_x * scale_x;
                int column_last_x = column_x + scale_x;
                int visible_x0 = column_x > first_x ? column_x : first_x;
                int visible_x1 = column_last_x < last_x ? column_last_x : last_x;
                const unsigned char *source;
                uint32_t colour;
                int y;
                if (visible_x0 >= visible_x1)
                    continue;
                source = image->pixels +
                    ((size_t)(draw->src_y + source_y) * (size_t)image->width +
                     (size_t)(draw->src_x + source_x)) *
                    (size_t)bytes_per_pixel;
                if (image->format == 32) {
                    uint32_t rgba;
                    memcpy(&rgba, source, sizeof(rgba));
                    colour = ((rgba & 0x000000ffU) << 16) |
                        (rgba & 0x0000ff00U) |
                        ((rgba & 0x00ff0000U) >> 16);
                } else {
                    colour = ((uint32_t)source[0] << 16) |
                        ((uint32_t)source[1] << 8) | source[2];
                }
                for (y = visible_y0; y < visible_y1; ++y) {
                    uint32_t *destination =
                        (uint32_t *)((unsigned char *)output->back.map +
                                     (size_t)y * output->back.pitch) +
                        visible_x0;
                    int x;
                    for (x = visible_x0; x < visible_x1; ++x)
                        *destination++ = colour;
                }
            }
        }
        return;
    }

    /* Bound iteration itself to the intersection. Testing the clip inside
       a full-image loop made a tiny pointer repaint scan millions of pixels
       on fractionally scaled wallpapers (ADR0132). Keep source coordinates
       relative to the original placement so clipping cannot change sampling. */
    {
    int left = origin_x + draw->x;
    int top = origin_y + draw->y;
    int first_x = clip_x0 > left ? clip_x0 : left;
    int first_y = clip_y0 > top ? clip_y0 : top;
    int last_x = clip_x1 < left + width ? clip_x1 : left + width;
    int last_y = clip_y1 < top + height ? clip_y1 : top + height;
    if (first_x < 0) first_x = 0;
    if (first_y < 0) first_y = 0;
    if (last_x > output->mode.hdisplay) last_x = output->mode.hdisplay;
    if (last_y > output->mode.vdisplay) last_y = output->mode.vdisplay;
    if (first_x >= last_x || first_y >= last_y) return;
    for (destination_y = first_y - top; destination_y < last_y - top; ++destination_y) {
        int py = origin_y + draw->y + destination_y;
        int source_y = draw->src_y + destination_y * draw->src_h / height;
        int destination_x;
        for (destination_x = first_x - left; destination_x < last_x - left; ++destination_x) {
            int px = origin_x + draw->x + destination_x;
            int source_x = draw->src_x + destination_x * draw->src_w / width;
            const unsigned char *source;
            uint32_t *destination;
            unsigned int alpha;
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
    uint32_t floor_colour = view->default_background;
    BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
    int draw_count = 0;
    int draw_index;
    int clip_x0;
    int clip_y0;
    int clip_x1;
    int clip_y1;
    BlitInkRegion ink;
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
    if (last_row <= first_row || last_column <= first_column) return;
    ink = blit_ink_region(font, first_column, first_row, last_column, last_row,
                           view->columns, view->rows);
    first_column = ink.x0; first_row = ink.y0;
    last_column = ink.x1; last_row = ink.y1;
    clip_x0 = view->origin_x + first_column * cw;
    clip_y0 = view->origin_y + first_row * ch;
    clip_x1 = view->origin_x + last_column * cw;
    clip_y1 = view->origin_y + last_row * ch;
    if (clip_x1 > view->origin_x + view->columns * cw)
        clip_x1 = view->origin_x + view->columns * cw;
    if (clip_y1 > view->origin_y + view->rows * ch)
        clip_y1 = view->origin_y + view->rows * ch;
    if (last_row <= first_row || last_column <= first_column)
        return;
    output->info.drawn_cells += (unsigned long long)(last_column-first_column) *
                                 (last_row-first_row);
    /* The terminal's default background is the floor of the view, not a quad
       drawn over whatever is below it: a cell that resolves to the default
       colour is transparent to an image under the text, and an explicit
       colour -- explicit black included -- is opaque. */
    fill_rect(output, clip_x0, clip_y0, clip_x1 - clip_x0,
              clip_y1 - clip_y0, floor_colour);
    if (view->graphics != NULL)
        draw_count = blit_graphics_collect(view->graphics,
                                           view->graphics_count, draws,
                                           BLIT_GRAPHICS_MAX_DRAWS);
    for (draw_index = 0; draw_index < draw_count; ++draw_index)
        if (draws[draw_index].z < KMS_GRAPHICS_Z_UNDER_BACKGROUND)
            draw_graphic(output, &draws[draw_index], view->origin_x,
                         view->origin_y, clip_x0, clip_y0, clip_x1, clip_y1);
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
    if (background_uniform && background_seen &&
        uniform_background != floor_colour)
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
                if (run_background != floor_colour) {
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
    for (draw_index = 0; draw_index < draw_count; ++draw_index)
        if (draws[draw_index].z >= KMS_GRAPHICS_Z_UNDER_BACKGROUND &&
            draws[draw_index].z < 0)
            draw_graphic(output, &draws[draw_index], view->origin_x,
                         view->origin_y, clip_x0, clip_y0, clip_x1, clip_y1);
    output->glyph_clip_active = blit_font_has_overhang(font);
    output->glyph_clip_x0 = clip_x0; output->glyph_clip_y0 = clip_y0;
    output->glyph_clip_x1 = clip_x1; output->glyph_clip_y1 = clip_y1;
    for (row = ink.source_y0; row < ink.source_y1; ++row) {
        for (column = ink.source_x0; column < ink.source_x1; ++column) {
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
            if (cp == ' ' && bg == floor_colour && !(attr & 4) &&
                (view->combining_count == NULL ||
                 view->combining_count[index] == 0))
                continue;
            /* ADR 0062: a wide character's glyph is 16 pixels wide and lives
               in the wide font; a codepoint that font does not map stays on
               the primary one, whose substitute is the visible marker. */
            if (!(attr & KMS_ATTR_CONTINUATION)) {
                draw_cell_glyphs(output, font, (attr & KMS_ATTR_WIDE) ? wide_font : NULL,
                    cp, view->combining ? &view->combining[index * BLIT_MAX_COMBINING] : NULL,
                    view->combining_count ? view->combining_count[index] : 0,
                    x, y, glyph_width, ch, fg, lean, (attr & 1) != 0);
            }
            if (!(attr & KMS_ATTR_CONTINUATION) && (attr & 4))
                if (y + ch - 1 >= clip_y0 && y + ch - 1 < clip_y1) {
                    int left = x > clip_x0 ? x : clip_x0;
                    int right = x + glyph_width < clip_x1 ? x + glyph_width : clip_x1;
                    fill_rect(output, left, y + ch - 1, right - left, 1, underline);
                }
            if (!(attr & KMS_ATTR_CONTINUATION) && (attr & 512))
                if (y + ch / 2 >= clip_y0 && y + ch / 2 < clip_y1) {
                    int left = x > clip_x0 ? x : clip_x0;
                    int right = x + glyph_width < clip_x1 ? x + glyph_width : clip_x1;
                    fill_rect(output, left, y + ch / 2, right - left, 1, fg);
                }
        }
    }
    output->glyph_clip_active = 0;
    for (draw_index = 0; draw_index < draw_count; ++draw_index)
        if (draws[draw_index].z >= 0)
            draw_graphic(output, &draws[draw_index], view->origin_x,
                         view->origin_y, clip_x0, clip_y0, clip_x1, clip_y1);
    /* The cursor is part of the terminal view, not an output-wide overlay.
       A partial repaint may only write inside its cell damage rectangle;
       drawing the cursor outside that rectangle corrupts the retained scene
       and produces a moving block/trail on the next page flip. SDL applies
       the same row boundary before drawing its cursor. */
    if (view->cursor_visible && view->cursor_x >= 0 && view->cursor_y >= 0 &&
        view->cursor_y < view->rows && view->cursor_y >= first_row &&
        view->cursor_y < last_row) {
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
        if (cursor_column >= last_column ||
            cursor_column + cursor_cells <= first_column)
            return;
        cursor_x = view->origin_x + cursor_column * cw;
        if (view->cursor_style == 3 || view->cursor_style == 4)
            fill_rect(output, cursor_x, cursor_y + ch - 1,
                      cw * cursor_cells, 1, KMS_WHITE);
        else if (view->cursor_style == 5 || view->cursor_style == 6)
            fill_rect(output, cursor_x, cursor_y, 1, ch, KMS_WHITE);
        else {
            uint32_t cursor_cp = view->codepoints[cursor_index];
            uint32_t cursor_bg = view_cell_background(view, cursor_index);
            fill_rect(output, cursor_x, cursor_y, cw * cursor_cells, ch,
                      KMS_WHITE);
            if (blit_font_has_overhang(font)) {
                /* Recolour every source whose ink enters the cursor cell,
                   not just its owner. Keep outside marks in normal colours. */
                BlitInkRegion sources = blit_ink_region(font, cursor_column,
                    view->cursor_y, cursor_column + cursor_cells,
                    view->cursor_y + 1, view->columns, view->rows);
                int sy, sx;
                output->glyph_clip_active = 1;
                output->glyph_clip_x0 = cursor_x > clip_x0 ? cursor_x : clip_x0;
                output->glyph_clip_x1 = cursor_x + cursor_cells*cw < clip_x1 ?
                    cursor_x + cursor_cells*cw : clip_x1;
                output->glyph_clip_y0 = cursor_y > clip_y0 ? cursor_y : clip_y0;
                output->glyph_clip_y1 = cursor_y + ch < clip_y1 ? cursor_y + ch : clip_y1;
                for (sy=sources.source_y0; sy<sources.source_y1; ++sy)
                    for (sx=sources.source_x0; sx<sources.source_x1; ++sx) {
                        int at=sy*view->stride+sx;
                        uint16_t a=view->attributes[at];
                        uint32_t cp=view->codepoints[at];
                        if (!cp || (a & (KMS_ATTR_CONTINUATION|KMS_ATTR_CONCEAL))) continue;
                        draw_cell_glyphs(output,font,(a & KMS_ATTR_WIDE)?wide_font:NULL,
                            cp,view->combining?view->combining+at*BLIT_MAX_COMBINING:NULL,
                            view->combining_count?view->combining_count[at]:0,
                            view->origin_x+sx*cw,view->origin_y+sy*ch,
                            (a & KMS_ATTR_WIDE)?cw*2:cw,ch,cursor_bg,
                            (a & KMS_ATTR_ITALIC)?italic_lean:0,(a & 1)!=0);
                    }
                output->glyph_clip_active = 0;
            }
            /* A block cursor is an inverse cell, not an eraser: the
               character under it is what the user is about to overwrite and
               has to stay readable. */
            if (!blit_font_has_overhang(font) && cursor_cp != 0 && cursor_cp != ' ' &&
                !(cursor_attr & KMS_ATTR_CONCEAL))
                draw_cell_glyphs(output, font, (cursor_attr & KMS_ATTR_WIDE) ? wide_font : NULL,
                    cursor_cp, view->combining ? &view->combining[cursor_index * BLIT_MAX_COMBINING] : NULL,
                    view->combining_count ? view->combining_count[cursor_index] : 0,
                    cursor_x, cursor_y, cw * cursor_cells, ch, cursor_bg,
                    (cursor_attr & KMS_ATTR_ITALIC) ? italic_lean : 0, (cursor_attr & 1) != 0);
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
    context.page_flip_handler = kms_flip_event;
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
            return output->last_surface_revisions[i] == view->surface_revision &&
                output->last_surface_origin_x[i] == view->origin_x &&
                output->last_surface_origin_y[i] == view->origin_y &&
                output->last_surface_graphics_signatures[i] ==
                    view->graphics_signature;
    return 0;
}

static int output_view_origin_changed(const KmsOutput *output,
                                      const BlitKmsSurfaceView *view)
{
    int i;
    if (output == NULL || view == NULL)
        return 0;
    for (i = 0; i < output->last_view_count && i < KMS_MAX_VIEWS; ++i)
        if (output->last_surface_ids[i] == view->surface_id)
            return output->last_surface_origin_x[i] != view->origin_x ||
                output->last_surface_origin_y[i] != view->origin_y;
    return 0;
}

static int output_graphics_unchanged(const KmsOutput *output,
                                     const BlitKmsSurfaceView *view)
{
    int i;
    for (i = 0; i < output->last_view_count && i < KMS_MAX_VIEWS; ++i)
        if (output->last_surface_ids[i] == view->surface_id)
            return output->last_surface_graphics_signatures[i] ==
                view->graphics_signature;
    return 0;
}

static int graphics_floor_div(int value, int divisor)
{
    if (value >= 0) return value / divisor;
    return -(((-value) + divisor - 1) / divisor);
}

static int graphics_ceil_div(int value, int divisor)
{
    if (value >= 0) return (value + divisor - 1) / divisor;
    return -((-value) / divisor);
}

/* Convert all visible retained graphics in a view into the smallest complete
   cell rectangle that contains them. The KMS renderer's damage unit is a
   cell, while Kitty placements may begin or end at a pixel offset. */
static int graphics_bounds_for_view(const BlitKmsSurfaceView *view,
                                    int *x, int *y, int *width, int *height)
{
    BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
    int draw_count;
    int draw_index;
    int cell_width;
    int cell_height;
    int left = 0;
    int top = 0;
    int right = 0;
    int bottom = 0;
    int have_bounds = 0;

    if (view == NULL || view->graphics == NULL ||
        view->columns < 1 || view->rows < 1 ||
        view->cell_width < 1 || view->cell_height < 1)
        return 0;
    cell_width = view->cell_width;
    cell_height = view->cell_height;
    draw_count = blit_graphics_collect(view->graphics, view->graphics_count,
                                       draws, BLIT_GRAPHICS_MAX_DRAWS);
    for (draw_index = 0; draw_index < draw_count; ++draw_index) {
        const BlitGraphicsDraw *draw = &draws[draw_index];
        int draw_right = draw->x + draw->width;
        int draw_bottom = draw->y + draw->height;
        if (!have_bounds || draw->x < left) left = draw->x;
        if (!have_bounds || draw->y < top) top = draw->y;
        if (!have_bounds || draw_right > right) right = draw_right;
        if (!have_bounds || draw_bottom > bottom) bottom = draw_bottom;
        have_bounds = 1;
    }
    if (!have_bounds) return 0;
    *x = graphics_floor_div(left, cell_width);
    *y = graphics_floor_div(top, cell_height);
    right = graphics_ceil_div(right, cell_width);
    bottom = graphics_ceil_div(bottom, cell_height);
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
    if (right > view->columns) right = view->columns;
    if (bottom > view->rows) bottom = view->rows;
    if (right <= *x || bottom <= *y) return 0;
    *width = right - *x;
    *height = bottom - *y;
    return 1;
}

static void include_cell_damage(int *damage_valid, int *damage_x,
                                int *damage_y, int *damage_width,
                                int *damage_height, int x, int y, int width,
                                int height)
{
    blit_damage_include(damage_valid, damage_x, damage_y, damage_width,
                        damage_height, x, y, width, height);
}

static int output_previous_graphics_bounds(const KmsOutput *output,
                                           const BlitKmsSurfaceView *view,
                                           int *x, int *y, int *width,
                                           int *height)
{
    int i;
    if (output == NULL || view == NULL) return 0;
    for (i = 0; i < output->last_view_count && i < KMS_MAX_VIEWS; ++i)
        if (output->last_surface_ids[i] == view->surface_id) {
            if (!output->last_surface_graphics_valid[i]) return 0;
            *x = output->last_surface_graphics_x[i];
            *y = output->last_surface_graphics_y[i];
            *width = output->last_surface_graphics_width[i];
            *height = output->last_surface_graphics_height[i];
            return 1;
        }
    return 0;
}

static void remember_output_views(KmsOutput *output,
                                  const BlitKmsSurfaceView *views,
                                  int view_count, int output_view_count)
{
    unsigned long long previous_ids[KMS_MAX_VIEWS];
    unsigned long long previous_revisions[KMS_MAX_VIEWS];
    unsigned long long previous_graphics_signatures[KMS_MAX_VIEWS];
    int previous_graphics_valid[KMS_MAX_VIEWS];
    int previous_graphics_x[KMS_MAX_VIEWS];
    int previous_graphics_y[KMS_MAX_VIEWS];
    int previous_graphics_width[KMS_MAX_VIEWS];
    int previous_graphics_height[KMS_MAX_VIEWS];
    int previous_count = output->last_view_count < KMS_MAX_VIEWS ?
        output->last_view_count : KMS_MAX_VIEWS;
    int view_index;
    int stored = 0;
    int i;

    if (previous_count < 0) previous_count = 0;
    for (i = 0; i < previous_count; ++i) {
        previous_ids[i] = output->last_surface_ids[i];
        previous_revisions[i] = output->last_surface_revisions[i];
        previous_graphics_signatures[i] =
            output->last_surface_graphics_signatures[i];
        previous_graphics_valid[i] =
            output->last_surface_graphics_valid[i];
        previous_graphics_x[i] = output->last_surface_graphics_x[i];
        previous_graphics_y[i] = output->last_surface_graphics_y[i];
        previous_graphics_width[i] =
            output->last_surface_graphics_width[i];
        previous_graphics_height[i] =
            output->last_surface_graphics_height[i];
    }
    output->last_view_count = output_view_count;
    for (view_index = 0; view_index < view_count &&
         stored < KMS_MAX_VIEWS; ++view_index) {
        if (views[view_index].display_id != output->info.id)
            continue;
        output->last_surface_ids[stored] = views[view_index].surface_id;
        output->last_surface_revisions[stored] =
            views[view_index].surface_revision;
        output->last_surface_graphics_signatures[stored] =
            views[view_index].graphics_signature;
        output->last_surface_origin_x[stored] = views[view_index].origin_x;
        output->last_surface_origin_y[stored] = views[view_index].origin_y;
        output->last_surface_graphics_valid[stored] =
            graphics_bounds_for_view(&views[view_index],
                                     &output->last_surface_graphics_x[stored],
                                     &output->last_surface_graphics_y[stored],
                                     &output->last_surface_graphics_width[stored],
                                     &output->last_surface_graphics_height[stored]);
        /* A synchronized view was not drawn.  Remembering the revision it
           carries mid-batch would make the release frame look like one that
           had already been presented, and it would never be drawn.  Keep the
           revision this output last actually put on screen. */
        if (views[view_index].synchronized) {
            for (i = 0; i < previous_count; ++i) {
                if (previous_ids[i] != views[view_index].surface_id)
                    continue;
                output->last_surface_revisions[stored] =
                    previous_revisions[i];
                output->last_surface_graphics_signatures[stored] =
                    previous_graphics_signatures[i];
                output->last_surface_graphics_valid[stored] =
                    previous_graphics_valid[i];
                output->last_surface_graphics_x[stored] = previous_graphics_x[i];
                output->last_surface_graphics_y[stored] = previous_graphics_y[i];
                output->last_surface_graphics_width[stored] =
                    previous_graphics_width[i];
                output->last_surface_graphics_height[stored] =
                    previous_graphics_height[i];
                break;
            }
        }
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
    return local_x < output->mode.hdisplay &&
        local_x + (output->pointer_width ? output->pointer_width : 12) > 0 &&
        local_y < output->mode.vdisplay &&
        local_y + (output->pointer_height ? output->pointer_height : 18) > 0;
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

/* The software pointer is composed into the persistent scene, but its
   underlying pixels are retained separately so pointer motion can repaint
   only the old and new footprints. Any non-full scene repaint must restore
   those pixels first, even when the pointer itself did not move. Otherwise an
   unrelated terminal update leaves the pointer in the scene and the next
   pointer move saves/restores that already-drawn pointer as if it were
   background, producing a cursor-shaped trail. */
static void prepare_repaint_buffer(KmsOutput *output, int full_redraw,
                                   int render_scene)
{
    if (full_redraw) {
        memset(output->back.map, 0, output->back.size);
        output->pointer_background_valid = 0;
    } else {
        if (!render_scene)
            copy_front_to_back(output);
        restore_pointer_background(output);
    }
}

static void save_pointer_background(KmsOutput *output, float x, float y)
{
    int left = (int)x - output->info.x;
    int top = (int)y - output->info.y;
    int right = left + (output->pointer_width ? output->pointer_width : 12);
    int bottom = top + (output->pointer_height ? output->pointer_height : 18);
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
    right = local_x + (output->pointer_width ? output->pointer_width : 12);
    if (right > view->origin_x + view->columns*cw) right=view->origin_x+view->columns*cw;
    bottom = local_y + (output->pointer_height ? output->pointer_height : 18);
    if (bottom > view->origin_y + view->rows*ch) bottom=view->origin_y+view->rows*ch;
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
    include_cell_damage(damage_valid, damage_x, damage_y, damage_width,
                        damage_height, first_column, first_row,
                        last_column - first_column, last_row - first_row);
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
                kms_output(kms, output_index)->info.id)
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

static void kms_gles_detach(BlitKms *kms, KmsOutput *output, const char *reason);
static void kms_gles_attach(BlitKms *kms, KmsOutput *output);
#include "blit_kms_hotplug.h"

static void kms_gles_release_bo(BlitKms *kms, KmsOutput *output,
                               void **bo, uint32_t *fb)
{
    if (*fb != 0) drmModeRmFB(kms->fd, *fb);
    if (*bo != NULL) blit_gles_release(kms->gles, output->gles_target, *bo);
    *bo = NULL;
    *fb = 0;
}

/* Back to the CPU compositor for this output, keeping the reason. The CPU
   frame is composed regardless, so the next flip simply uses it. */
static void kms_gles_detach(BlitKms *kms, KmsOutput *output, const char *reason)
{
    if (output->gles_target != NULL) {
        kms_gles_release_bo(kms, output, &output->gles_next_bo, &output->gles_next_fb);
        kms_gles_release_bo(kms, output, &output->gles_pending_bo, &output->gles_pending_fb);
        kms_gles_release_bo(kms, output, &output->gles_shown_bo, &output->gles_shown_fb);
        blit_gles_target_close(kms->gles, output->gles_target);
        output->gles_target = NULL;
    }
    snprintf(output->info.compositor, sizeof(output->info.compositor), "cpu");
    if (reason != NULL) {
        snprintf(output->info.compositor_reason,
                 sizeof(output->info.compositor_reason), "%s", reason);
        if (kms->gles_requested)
            fprintf(stderr, "blit: GLES compositor off for connector=%u: %s\n",
                    output->connector, reason);
    }
}

static void kms_gles_attach(BlitKms *kms, KmsOutput *output)
{
    char error[160];
    snprintf(output->info.compositor_requested,
             sizeof(output->info.compositor_requested), "%s",
             kms->gles_requested ? "gles" : "cpu");
    snprintf(output->info.compositor, sizeof(output->info.compositor), "cpu");
    output->info.compositor_reason[0] = '\0';
    if (!kms->gles_requested) return;
    if (kms->gles == NULL) {
        kms_gles_detach(kms, output, kms->gles_error);
        return;
    }
    error[0] = '\0';
    output->gles_target = blit_gles_target_open(kms->gles, output->mode.hdisplay,
                                                output->mode.vdisplay,
                                                error, sizeof(error));
    if (output->gles_target == NULL) {
        kms_gles_detach(kms, output, error);
        return;
    }
    snprintf(output->info.compositor, sizeof(output->info.compositor), "gles");
    snprintf(output->info.compositor_renderer,
             sizeof(output->info.compositor_renderer), "%s",
             blit_gles_identity(kms->gles));
}

/* Step 2: the CPU frame in back is drawn by GLES. Surfaceless (tests) reads
   the result back into back, so every screenshot shows GLES output; GBM
   locks the drawn buffer for the flip. */
static int kms_gles_frame(BlitKms *kms, KmsOutput *output)
{
    char error[160];
    uint32_t handle = 0, pitch = 0;
    if (output->gles_target == NULL) return 0;
    error[0] = '\0';
    if (blit_gles_draw_frame(kms->gles, output->gles_target, output->back.map,
                             (int)output->back.pitch, error, sizeof(error)) < 0) {
        kms_gles_detach(kms, output, error);
        return -1;
    }
    if (getenv("BLIT_GLES_PLATFORM") != NULL &&
        strcmp(getenv("BLIT_GLES_PLATFORM"), "surfaceless") == 0) {
        if (blit_gles_read_frame(kms->gles, output->gles_target, output->back.map,
                                 (int)output->back.pitch) < 0) {
            kms_gles_detach(kms, output, "GLES readback failed");
            return -1;
        }
        ++output->info.gles_frames;
        return 0;
    }
    output->gles_next_bo = blit_gles_lock_front(kms->gles, output->gles_target,
                                                &handle, &pitch);
    if (output->gles_next_bo == NULL ||
        drmModeAddFB(kms->fd, output->mode.hdisplay, output->mode.vdisplay, 24, 32,
                     pitch, handle, &output->gles_next_fb) < 0) {
        output->gles_next_fb = 0;
        kms_gles_detach(kms, output, "GBM buffer cannot be scanned out");
        return -1;
    }
    ++output->info.gles_frames;
    return 0;
}

BlitKms *blit_kms_new(BlitFont *font, BlitFont *wide_font)
{
    BlitKms *kms = (BlitKms *)calloc(1, sizeof(BlitKms));
    if (kms != NULL) { kms->fd = -1; kms->font = font;
    kms->monitor_socket = kms->monitor_stop[0] = kms->monitor_stop[1] = -1;
    pthread_mutex_init(&kms->monitor_lock, NULL);
    kms->wide_font = wide_font; }
    return kms;
}

int blit_kms_open(BlitKms *kms, char *error, size_t error_capacity)
{
    KmsSnapshot snapshot;
    const char *configured;
    if (kms == NULL) return -1;
    kms->fd = open(getenv("BLIT_DRM_DEVICE") ? getenv("BLIT_DRM_DEVICE") :
                   "/dev/dri/card0", O_RDWR | O_CLOEXEC | O_NONBLOCK);
    if (kms->fd < 0) { set_error(error, error_capacity, strerror(errno)); return -1; }
    configured = getenv("BLIT_KMS_COMPOSITOR");
    kms->gles_requested = configured != NULL && strcmp(configured, "gles") == 0;
    if (kms->gles_requested && getenv("BLIT_KMS_GLES_BLOCKED") != NULL) {
        snprintf(kms->gles_error, sizeof(kms->gles_error), "%s",
                 getenv("BLIT_KMS_GLES_BLOCKED"));
        fprintf(stderr, "blit: GLES compositor not started: %s\n", kms->gles_error);
    } else if (kms->gles_requested) {
        const char *platform = getenv("BLIT_GLES_PLATFORM");
        kms->gles = platform != NULL && strcmp(platform, "surfaceless") == 0 ?
            blit_gles_open_surfaceless(kms->gles_error, sizeof(kms->gles_error)) :
            blit_gles_open_gbm(kms->fd, kms->gles_error, sizeof(kms->gles_error));
        if (kms->gles != NULL)
            fprintf(stderr, "blit: GLES compositor: %s\n", blit_gles_identity(kms->gles));
        else
            fprintf(stderr, "blit: GLES compositor unavailable, using CPU: %s\n",
                    kms->gles_error);
    }
    configured = getenv("BLIT_KMS_SCREENSHOT_DIR");
    if (configured != NULL) snprintf(kms->screenshot_dir, sizeof(kms->screenshot_dir), "%s", configured);
    configured = getenv("BLIT_KMS_SCREENSHOT_AFTER");
    kms->screenshot_after = configured ? strtoul(configured, NULL, 10) : 30;
    configured = getenv("BLIT_KMS_CELL_WIDTH");
    if (configured != NULL) kms->cell_width_override = atoi(configured);
    if (kms->cell_width_override < 5 || kms->cell_width_override > 32) kms->cell_width_override = 0;
    if (kms_read_snapshot(kms, &snapshot) < 0) {
        set_error(error, error_capacity, "DRM connector snapshot unavailable"); return -1;
    }
    if (kms_start_monitor(kms) < 0) {
        set_error(error, error_capacity, "DRM connector monitor unavailable"); return -1;
    }
    kms_reconcile_snapshot(kms, &snapshot);
    return 0;
}

int blit_kms_suspend(BlitKms *kms)
{
    int i;
    if (kms == NULL) return -1;
    if (kms->suspended) return 0;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = kms_output(kms, i);
        if (wait_flip(kms, output) < 0) return -1;
        if (output->old_crtc && output->old_crtc->mode_valid &&
            drmModeSetCrtc(kms->fd, output->old_crtc->crtc_id,
                output->old_crtc->buffer_id, output->old_crtc->x,
                output->old_crtc->y, &output->connector, 1,
                &output->old_crtc->mode) < 0) return -1;
    }
    if (drmDropMaster(kms->fd) < 0) return -1;
    kms->suspended = 1;
    return 0;
}

int blit_kms_resume(BlitKms *kms)
{
    int i;
    if (kms == NULL) return -1;
    if (!kms->suspended) return 0;
    if (drmSetMaster(kms->fd) < 0) return -1;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = kms_output(kms, i);
        /* Existing buffers remain allocated. Repaint and modeset once;
           damage accumulated while another VT owned scanout is not enough. */
        output->last_view_count = -1;
        output->modeset_fallback = 1;
        output->pointer_state_valid = 0;
        output->pointer_background_valid = 0;
    }
    kms->suspended = 0;
    return 0;
}

int blit_kms_display_count(const BlitKms *kms) { return kms ? kms->output_count : 0; }

unsigned long long blit_kms_inventory_revision(const BlitKms *kms)
{
    return kms ? kms->inventory_revision : 0;
}

void blit_kms_hotplug_info(BlitKms *kms, BlitKmsHotplugInfo *info)
{
    memset(info, 0, sizeof(*info));
    if (kms == NULL) return;
    info->inventory_revision = kms->inventory_revision;
    info->observer = kms->monitor_started;
    snprintf(info->error, sizeof(info->error), "%s", kms->hotplug_error);
    pthread_mutex_lock(&kms->monitor_lock);
    info->uevents = kms->monitor_socket >= 0;
    info->scans = kms->monitor_scans;
    info->last_scan_ms = kms->monitor_last_scan_ms;
    pthread_mutex_unlock(&kms->monitor_lock);
}

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
    if (kms->suspended) return 0;
    if (readable) {
        memset(&context, 0, sizeof(context));
        context.version = DRM_EVENT_CONTEXT_VERSION;
        context.page_flip_handler = kms_flip_event;
        if (drmHandleEvent(kms->fd, &context) < 0) return -1;
        for (i = 0; i < kms->output_count; ++i)
            if (!kms_output(kms, i)->flip_pending)
                changed = 1;
    }
    if (kms->monitor_started) {
        KmsSnapshot snapshot;
        int available = 0;
        pthread_mutex_lock(&kms->monitor_lock);
        if (kms->snapshot_revision != kms->applied_snapshot_revision) {
            snapshot = kms->snapshot;
            kms->applied_snapshot_revision = kms->snapshot_revision;
            available = 1;
        }
        pthread_mutex_unlock(&kms->monitor_lock);
        if (available && kms_reconcile_snapshot(kms, &snapshot)) changed = 1;
    }
    now = kms_now_us();
    timeout_us = (long long)kms_flip_timeout_ms() * 1000LL;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = kms_output(kms, i);
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
    *info = kms_output(kms, index)->info;
    return 0;
}

uint32_t blit_kms_ready_output_mask(const BlitKms *kms)
{
    uint32_t mask = 0;
    int i;
    if (kms == NULL) return 0;
    for (i = 0; i < kms->output_count; ++i)
        if (!kms_output(kms, i)->flip_pending &&
            kms_output(kms, i)->info.presentation_state != BLIT_KMS_PRESENTATION_FAILED)
            mask |= 1U << i;
    return mask;
}

int blit_kms_set_display_position(BlitKms *kms, int index, int x, int y)
{
    if (kms == NULL || index < 0 || index >= kms->output_count) return -1;
    kms_output(kms, index)->info.x = x;
    kms_output(kms, index)->info.y = y;
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

void blit_kms_set_pointer_art(BlitKms *kms, const BlitPointerArt *art)
{
    int i;
    if (kms == NULL || art == NULL || !art->revision) return;
    kms->pointer_art = art;
    if (kms->pointer_art_revision == art->revision) return;
    kms->pointer_art_revision = art->revision;
    /* A footprint that changes size can leave pixels of the previous shape
       outside the new one, so that repaints everything. A shape swap at the
       same size -- every OSC 22 change -- is covered by the saved
       background under the old stamp, and stays incremental. */
    if (kms->pointer_art_width != art->width ||
        kms->pointer_art_height != art->height) {
        kms->pointer_art_width = art->width;
        kms->pointer_art_height = art->height;
        for (i = 0; i < kms->output_count; ++i)
            kms_output(kms,i)->initialized = 0;
    }
    for (i = 0; i < kms->output_count; ++i)
        kms_output(kms,i)->pointer_state_valid = 0;
}

int blit_kms_present(BlitKms *kms, const BlitKmsSurfaceView *views, int view_count,
                     float pointer_x, float pointer_y, int pointer_visible,
                     uint32_t output_mask, const BlitKmsOverlay *overlays,
                     int overlay_count, unsigned long long visual_revision,
                     int effect_enabled, int effect_intensity,
                     int hold_scene,
                     uint32_t screenshot_output_mask,
                     uint32_t *submitted_output_mask,
                     uint32_t *deferred_output_mask)
{
    int i;
    int capture;
    int pointer_changed;
    int full_redraw;
    int failed = 0;
    int expected_captures = 0;
    int completed_captures = 0;
    long long started = 0;
    const char *screenshot_ready_file = getenv("BLIT_KMS_SCREENSHOT_READY_FILE");
    if (kms == NULL) return -1;
    if (kms->suspended) return 0;
    /* The canonical state/hit-testing coordinates stay at the hotspot.
       Only the compositing coordinates are translated to the stamp origin. */
    if (kms->pointer_art) {
        pointer_x -= kms->pointer_art->hotspot_x;
        pointer_y -= kms->pointer_art->hotspot_y;
    }
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
                kms_output(kms, i)->info.presentation_state !=
                    BLIT_KMS_PRESENTATION_FAILED)
                ++expected_captures;
    }
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = kms_output(kms, i);
        int view_index;
        int output_view_count = 0;
        long long output_started = 0;
        long long draw_started = 0;
        long long flip_started = 0;
        int pointer_affects_output;
        int modeset_frame;
        int trace_phases = getenv("BLIT_KMS_TRACE_PHASES") != NULL;
        output->pointer_width = kms->pointer_art ? kms->pointer_art->width : 12;
        output->pointer_height = kms->pointer_art ? kms->pointer_art->height : 18;
        if (output->info.presentation_state == BLIT_KMS_PRESENTATION_FAILED)
            continue;
        if (!(output_mask & (1U << i)))
            continue;
        pointer_changed = !output->pointer_state_valid ||
            output->previous_pointer_x != pointer_x ||
            output->previous_pointer_y != pointer_y ||
            output->previous_pointer_visible != pointer_visible;
        pointer_affects_output = pointer_changed &&
            (pointer_intersects_output(output, output->previous_pointer_x,
                                       output->previous_pointer_y,
                                       output->previous_pointer_visible) ||
             pointer_intersects_output(output, pointer_x, pointer_y,
                                       pointer_visible));
        if (pointer_affects_output)
            ++output->info.pointer_change_requests;
        /* A scene fence permits only cursor composition over an already
           committed buffer. No first frame or invalidated cache is safe. */
        if (hold_scene && !output->initialized) {
            if (pointer_affects_output)
                ++output->info.pointer_frame_deferred;
            continue;
        }
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
            (screenshot_output_mask & (1U << i)) != 0 ||
            pointer_affects_output;
        /* Inspect scene changes even when pointer/capture work has already
           scheduled this output. Relocation must clear the old bounds. */
        for (view_index = 0; !hold_scene && view_index < view_count; ++view_index) {
            if (views[view_index].display_id != output->info.id ||
                views[view_index].synchronized)
                continue;
            if (!output_view_unchanged(output, &views[view_index])) {
                if (output_view_origin_changed(output, &views[view_index]))
                    full_redraw = 1;
                output_changed = 1;
            }
        }
        /* A page-flipped buffer is owned by the CRTC until its event arrives.
           Do not block the gateway waiting for it: leave this output intact
           and retry it after the main loop services the DRM event. */
        if (output->flip_pending && getenv("BLIT_KMS_MODESET") == NULL &&
            !output->modeset_fallback) {
            if (pointer_affects_output)
                ++output->info.pointer_frame_deferred;
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
        if (hold_scene)
            full_redraw = 0;
        /* A timeout requests one recovery modeset.  It must not turn the
           normal frame path into a modeset-per-frame loop: that produces
           visible blanking and can make a slow KMS driver appear frozen. */
        modeset_frame = getenv("BLIT_KMS_MODESET") != NULL ||
            output->modeset_fallback;
        if (modeset_frame)
            output->flip_pending = 0;
        {
            void *scanout_map = output->back.map;
            int render_scene = ensure_scene_buffer(output);
            if (render_scene)
                output->back.map = output->scene;
        if (trace_phases)
            fprintf(stderr, "blit: KMS phase connector=%u wait_us=%lld\n",
                    output->connector, kms_now_us() - output_started);
        /* Fallback for an allocation failure: preserve the complete
           displayed scene before applying smaller damage. The pointer
           background is restored for every non-full repaint, not only when
           the pointer moved; terminal damage can otherwise preserve a
           pointer-shaped hole in the scene cache. */
        prepare_repaint_buffer(output, full_redraw, render_scene);
        draw_started = kms_now_us();
        output->info.drawn_cells = 0;
        for (view_index = 0; !hold_scene && view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id) {
                BlitKmsSurfaceView draw = views[view_index];
                int pointer_damage = 0;
                int graphics_view_changed = 0;
                /* Hold the last presented pixels.  They are already in this
                   buffer: a non-full frame is rebuilt from the front buffer
                   above, so leaving the view undrawn preserves it.  A genuine
                   full redraw started from a cleared buffer and has nothing
                   left to hold, so the current cells are drawn -- one tear
                   rather than a hole. */
                if (views[view_index].synchronized && !full_redraw)
                    continue;
                if (pointer_changed) {
                    int pointer_x0 = 0, pointer_y0 = 0;
                    int pointer_width = 0, pointer_height = 0;
                    include_pointer_damage(output, &views[view_index],
                                           output->previous_pointer_x,
                                           output->previous_pointer_y,
                                           output->previous_pointer_visible,
                                           &pointer_damage, &pointer_x0,
                                           &pointer_y0, &pointer_width,
                                           &pointer_height);
                    include_pointer_damage(output, &views[view_index],
                                           pointer_x, pointer_y,
                                           pointer_visible, &pointer_damage,
                                           &pointer_x0, &pointer_y0,
                                           &pointer_width, &pointer_height);
                    if (pointer_damage)
                        include_cell_damage(&draw.damage_valid,
                                            &draw.damage_x, &draw.damage_y,
                                            &draw.damage_width,
                                            &draw.damage_height, pointer_x0,
                                            pointer_y0, pointer_width,
                                            pointer_height);
                }
                if (pointer_damage) draw.damage_valid = 1;
                /* The server supplies complete old/new descriptor damage.
                   Unknown callers retain the conservative union of all old
                   and current graphics; never trust a partial cell-only
                   rectangle to cover a graphics deletion by itself. */
                graphics_view_changed = !full_redraw &&
                    !output_graphics_unchanged(output, &views[view_index]);
                if (graphics_view_changed && !views[view_index].graphics_damage_complete) {
                    int current_x;
                    int current_y;
                    int current_width;
                    int current_height;
                    int previous_x;
                    int previous_y;
                    int previous_width;
                    int previous_height;
                    int bounds_valid = 0;
                    if (graphics_bounds_for_view(&views[view_index],
                                                 &current_x, &current_y,
                                                 &current_width,
                                                 &current_height)) {
                        include_cell_damage(&draw.damage_valid,
                                            &draw.damage_x, &draw.damage_y,
                                            &draw.damage_width,
                                            &draw.damage_height, current_x,
                                            current_y, current_width,
                                            current_height);
                        bounds_valid = 1;
                    }
                    if (output_previous_graphics_bounds(
                            output, &views[view_index], &previous_x,
                            &previous_y, &previous_width, &previous_height)) {
                        include_cell_damage(&draw.damage_valid,
                                            &draw.damage_x, &draw.damage_y,
                                            &draw.damage_width,
                                            &draw.damage_height, previous_x,
                                            previous_y, previous_width,
                                            previous_height);
                        bounds_valid = 1;
                    }
                    /* If a scene mutation cannot be bounded, preserve the
                       old correctness contract and repaint the whole view. */
                    if (!bounds_valid) {
                        draw.damage_valid = 1;
                        draw.damage_x = 0;
                        draw.damage_y = 0;
                        draw.damage_width = draw.columns;
                        draw.damage_height = draw.rows;
                    }
                }
                if (!full_redraw && output_view_unchanged(output,
                                                           &views[view_index]) &&
                    !pointer_damage)
                    continue;
                if (pointer_damage) draw.damage_valid = 1;
                if (!full_redraw && (views[view_index].damage_valid ||
                                     graphics_view_changed)) {
                    int damage_x = draw.damage_x;
                    int damage_y = draw.damage_y;
                    int damage_right = damage_x + draw.damage_width;
                    int damage_bottom = damage_y + draw.damage_height;
                    int cw = views[view_index].cell_width > 0 ?
                        views[view_index].cell_width : KMS_FONT_W;
                    int ch = views[view_index].cell_height > 0 ?
                        views[view_index].cell_height : KMS_FONT_H;
                    if (!draw.damage_valid) {
                        damage_x = views[view_index].damage_x;
                        damage_y = views[view_index].damage_y;
                        damage_right = damage_x + views[view_index].damage_width;
                        damage_bottom = damage_y + views[view_index].damage_height;
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
        if (!hold_scene) {
            draw_kms_effects(output, effect_enabled, effect_intensity);
            draw_kms_overlays(output, overlays, overlay_count);
        }
        if (pointer_intersects_output(output, pointer_x, pointer_y, pointer_visible)) {
            int x = (int)pointer_x - output->info.x;
            int y = (int)pointer_y - output->info.y;
            save_pointer_background(output, pointer_x, pointer_y);
            if (kms->pointer_art) {
                const BlitPointerArt *art = kms->pointer_art;
                int px, py;
                for (py=0; py<art->height; ++py) for (px=0; px<art->width; ++px) {
                    int dx=x+px, dy=y+py;
                    uint32_t *pixel;
                    const unsigned char *rgba;
                    if (dx<0 || dy<0 || dx>=output->mode.hdisplay || dy>=output->mode.vdisplay) continue;
                    rgba=art->rgba+(py*art->width+px)*4;
                    if (!rgba[3]) continue;
                    pixel=(uint32_t *)((unsigned char *)output->back.map+(size_t)dy*output->back.pitch)+dx;
                    *pixel=blit_pointer_blend(*pixel,rgba);
                }
            } else {
                fill_rect(output, x, y, 2, 18, 0xffffffU);
                fill_rect(output, x, y, 12, 2, 0xffffffU);
            }
        } else {
            output->pointer_background_valid = 0;
        }
        flip_started = kms_now_us();
        output->info.draw_ms = (double)(flip_started - draw_started) / 1000.0;
        if (render_scene) {
            output->back.map = scanout_map;
            copy_scene_to_back(output);
        }
        kms_gles_frame(kms, output);
        output->info.copy_ms = (double)(kms_now_us() - flip_started) / 1000.0;
        /* Capture the frame composed by this present call.  Capturing before
           the scene-buffer copy reads the previous scanout buffer and makes
           diagnostics disagree with the authoritative surface state. */
        if (capture) {
            write_screenshot(kms, output);
            ++completed_captures;
        }
        {
            unsigned int flags = DRM_MODE_PAGE_FLIP_EVENT;
            int flip_result;
            long long submit_started = kms_now_us();
            if (modeset_frame) {
                flip_result = drmModeSetCrtc(kms->fd, output->crtc,
                                             output->gles_next_fb ?
                                                 output->gles_next_fb :
                                                 output->back.fb, 0, 0,
                                             &output->connector, 1,
                                             &output->mode);
                if (flip_result < 0)
                    kms_gles_release_bo(kms, output, &output->gles_next_bo,
                                        &output->gles_next_fb);
                else if (output->gles_next_bo != NULL) {
                    kms_gles_release_bo(kms, output, &output->gles_shown_bo,
                                        &output->gles_shown_fb);
                    kms_gles_release_bo(kms, output, &output->gles_pending_bo,
                                        &output->gles_pending_fb);
                    output->gles_shown_bo = output->gles_next_bo;
                    output->gles_shown_fb = output->gles_next_fb;
                    output->gles_next_bo = NULL;
                    output->gles_next_fb = 0;
                }
                output->info.submit_ms = (double)(kms_now_us() - submit_started) / 1000.0;
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
                output->generation = ++kms->generation;
                output->flip_pending = 0;
                output->flip_started_us = 0;
                swap_buffers(output);
                for (view_index = 0; !hold_scene && view_index < view_count; ++view_index)
                    if (views[view_index].display_id == output->info.id &&
                        views[view_index].damage_consumed != NULL)
                        *views[view_index].damage_consumed = 1;
                output->initialized = 1;
                if (!hold_scene) {
                    output->last_visual_revision = visual_revision;
                    remember_output_views(output, views, view_count,
                                          output_view_count);
                }
                output->info.last_present_ms =
                    (double)(kms_now_us() - output_started) / 1000.0;
                if (output->info.last_present_ms > output->info.max_present_ms)
                    output->info.max_present_ms = output->info.last_present_ms;
                ++output->info.present_calls;
                output->pointer_state_valid = 1;
                output->previous_pointer_x = pointer_x;
                output->previous_pointer_y = pointer_y;
                output->previous_pointer_visible = pointer_visible;
                if (pointer_affects_output)
                    ++output->info.pointer_frame_submissions;
                if (submitted_output_mask != NULL)
                    *submitted_output_mask |= 1U << i;
                continue;
            }
            if (getenv("BLIT_KMS_ASYNC_PAGEFLIP") != NULL)
                flags |= DRM_MODE_PAGE_FLIP_ASYNC;
            KmsFlipCookie *cookie = kms_flip_cookie(kms, output);
            if (cookie == NULL) {
                output->info.presentation_state = BLIT_KMS_PRESENTATION_FAILED;
                failed = 1;
                continue;
            }
            output->flip_done = 0;
            {
                uint32_t scanout_fb = output->gles_next_fb ?
                    output->gles_next_fb : output->back.fb;
                flip_result = drmModePageFlip(kms->fd, output->crtc,
                                              scanout_fb, flags, cookie);
                if (flip_result < 0 && (flags & DRM_MODE_PAGE_FLIP_ASYNC))
                    flip_result = drmModePageFlip(kms->fd, output->crtc,
                                                  scanout_fb,
                                                  DRM_MODE_PAGE_FLIP_EVENT,
                                                  cookie);
            }
            if (flip_result < 0)
                kms_gles_release_bo(kms, output, &output->gles_next_bo,
                                    &output->gles_next_fb);
            else if (output->gles_next_bo != NULL) {
                output->gles_pending_bo = output->gles_next_bo;
                output->gles_pending_fb = output->gles_next_fb;
                output->gles_next_bo = NULL;
                output->gles_next_fb = 0;
            }
            output->info.submit_ms = (double)(kms_now_us() - submit_started) / 1000.0;
            if (flip_result < 0) {
                cookie->used = 0;
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
        for (view_index = 0; !hold_scene && view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id &&
                views[view_index].damage_consumed != NULL)
                *views[view_index].damage_consumed = 1;
        output->initialized = 1;
        if (!hold_scene)
            output->last_visual_revision = visual_revision;
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
        output->pointer_state_valid = 1;
        output->previous_pointer_x = pointer_x;
        output->previous_pointer_y = pointer_y;
        output->previous_pointer_visible = pointer_visible;
        if (pointer_affects_output)
            ++output->info.pointer_frame_submissions;
        if (!hold_scene)
            remember_output_views(output, views, view_count, output_view_count);
        }
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

/* Whether the buffer a screenshot reads is the one being scanned out. The
   front buffer is the last frame submitted; until its flip completes the
   panel still shows the one before it. */
int blit_kms_front_presented(const BlitKms *kms, int index)
{
    if (kms == NULL || index < 0 || index >= kms->output_count) return 0;
    return !kms_output(kms, index)->flip_pending &&
        kms_output(kms, index)->initialized;
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
    output = kms_output(kms, index);
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
    if (kms->monitor_started) {
        write(kms->monitor_stop[1], "x", 1);
        pthread_join(kms->monitor_thread, NULL);
    }
    if (kms->monitor_stop[0] >= 0) close(kms->monitor_stop[0]);
    if (kms->monitor_stop[1] >= 0) close(kms->monitor_stop[1]);
    if (kms->monitor_socket >= 0) close(kms->monitor_socket);
    for (i = 0; i < KMS_MAX_OUTPUTS; ++i) {
        KmsOutput *output = &kms->outputs[i];
        if (!output->allocated) continue;
        wait_flip(kms, output);
        if (kms->suspended || kms_release_output(kms, output, 1) < 0) {
            /* Close the DRM file before unmapping a failed teardown's buffers. */
            if (kms->fd >= 0) close(kms->fd);
            kms->fd = -1;
            kms_gles_detach(kms, output, NULL);
            free_buffer(-1, &output->front); free_buffer(-1, &output->back);
            free(output->scene);
            if (output->old_crtc) drmModeFreeCrtc(output->old_crtc);
        }
    }
    if (kms->gles != NULL) blit_gles_close(kms->gles);
    if (kms->fd >= 0) close(kms->fd);
    pthread_mutex_destroy(&kms->monitor_lock);
    free(kms);
}
