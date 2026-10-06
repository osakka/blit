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
#define KMS_FONT_W 8
#define KMS_FONT_H 8
#define KMS_POINTER_W 12
#define KMS_POINTER_H 18
#define KMS_WHITE 0xbed2beU

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
    drmModeCrtc *old_crtc;
    KmsBuffer front;
    KmsBuffer back;
    int flip_pending;
    volatile int flip_done;
    long long flip_started_us;
    int modeset_fallback;
    int initialized;
    int last_view_count;
    unsigned long long last_surface_ids[KMS_MAX_VIEWS];
    unsigned long long last_surface_revisions[KMS_MAX_VIEWS];
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
        for (column = first_x; column < last_x; ++column)
            pixels[column] = color;
    }
}

static uint32_t view_cell_background(const BlitKmsSurfaceView *view,
                                     int index)
{
    uint32_t fg = view->foreground[index] ?
        view->foreground[index] - 1 : KMS_WHITE;
    uint32_t bg = view->background[index] ?
        view->background[index] - 1 : 0;
    if (view->attributes[index] & 8) {
        uint32_t temporary = fg;
        fg = bg;
        bg = temporary;
    }
    return bg;
}

static void draw_glyph(KmsOutput *output, const BlitFont *font,
                       uint32_t codepoint, int x, int y, int cw, int ch,
                       uint32_t color)
{
    int glyph_row;
    int glyph_column;
    int font_width = font != NULL && font->data ? font->width : 5;
    int font_height = font != NULL && font->data ? font->height : 7;
    for (glyph_row = 0; glyph_row < ch; ++glyph_row) {
        uint32_t *pixels;
        int py = y + glyph_row;
        int source_row = glyph_row * font_height / ch;
        if (py < 0 || py >= output->mode.vdisplay) continue;
        if (source_row < 0 || source_row >= font_height) continue;
        pixels = (uint32_t *)((unsigned char *)output->back.map +
                              py * output->back.pitch);
        for (glyph_column = 0; glyph_column < cw; ++glyph_column) {
            int source_column = glyph_column * font_width / cw;
            int px = x + glyph_column;
            if (blit_font_pixel(font, codepoint, source_row, source_column) &&
                px >= 0 && px < output->mode.hdisplay)
                pixels[px] = color;
        }
    }
}

static void draw_view(KmsOutput *output, const BlitFont *font,
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
    for (row = first_row; row < last_row; ++row) {
        for (column = first_column; column < last_column; ++column) {
            int index = row * view->stride + column;
            uint32_t fg = view->foreground[index] ?
                view->foreground[index] - 1 : KMS_WHITE;
            uint32_t bg = view->background[index] ?
                view->background[index] - 1 : 0;
            uint32_t underline = view->underline_color != NULL &&
                view->underline_color[index] ?
                view->underline_color[index] - 1 : fg;
            uint32_t cp = view->codepoints[index] ? view->codepoints[index] : ' ';
            uint16_t attr = view->attributes[index];
            int x = view->origin_x + column * cw;
            int y = view->origin_y + row * ch;
            if (attr & 8) { uint32_t t = fg; fg = bg; bg = t; }
            if (attr & 2) fg = (fg & 0xfefefeU) >> 1;
            if (cp == ' ' && bg == 0 && !(attr & 4) &&
                (view->combining_count == NULL ||
                 view->combining_count[index] == 0))
                continue;
            if (bg && (!background_uniform || bg != uniform_background))
                fill_rect(output, x, y, cw, ch, bg);
            if (!(attr & 64)) {
                draw_glyph(output, font, cp, x, y, cw, ch, fg);
            }
            if (!(attr & 64) && (attr & 4))
                fill_rect(output, x, y + ch - 1, cw, 1, underline);
            if (!(attr & 64) && (attr & 512))
                fill_rect(output, x, y + ch / 2, cw, 1, fg);
            if (!(attr & 64) && (attr & 1))
                draw_glyph(output, font, cp, x + 1, y, cw, ch, fg);
            if (!(attr & 64) && view->combining != NULL &&
                view->combining_count != NULL) {
                int combining_index;
                for (combining_index = 0;
                     combining_index < view->combining_count[index] &&
                     combining_index < 4; ++combining_index) {
                    uint32_t mark = view->combining[index * 4 + combining_index];
                    draw_glyph(output, font, mark, x, y, cw, ch, fg);
                }
            }
        }
    }
    if (view->cursor_visible && view->cursor_x >= 0 && view->cursor_y >= 0) {
        int cursor_x = view->origin_x + view->cursor_x * cw;
        int cursor_y = view->origin_y + view->cursor_y * ch;
        if (view->cursor_style == 3 || view->cursor_style == 4)
            fill_rect(output, cursor_x, cursor_y + ch - 1, cw, 1, KMS_WHITE);
        else if (view->cursor_style == 5 || view->cursor_style == 6)
            fill_rect(output, cursor_x, cursor_y, 1, ch, KMS_WHITE);
        else
            fill_rect(output, cursor_x, cursor_y, cw, ch, KMS_WHITE);
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
    int y;

    if (kms == NULL || output == NULL || kms->screenshot_dir[0] == '\0')
        return;
    snprintf(path, sizeof(path), "%s/display-%llu.ppm",
             kms->screenshot_dir, output->info.id);
    file = fopen(path, "wb");
    if (file == NULL)
        return;
    fprintf(file, "P6\n%d %d\n255\n", output->mode.hdisplay,
            output->mode.vdisplay);
    for (y = 0; y < output->mode.vdisplay; ++y) {
        int x;
        const unsigned char *row = (const unsigned char *)output->back.map +
            (size_t)y * output->back.pitch;
        for (x = 0; x < output->mode.hdisplay; ++x) {
            unsigned char rgb[3];
            rgb[0] = row[x * 4 + 2];
            rgb[1] = row[x * 4 + 1];
            rgb[2] = row[x * 4 + 0];
            fwrite(rgb, 1, sizeof(rgb), file);
        }
    }
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
    int view_index;
    int stored = 0;
    output->last_view_count = output_view_count;
    for (view_index = 0; view_index < view_count &&
         stored < KMS_MAX_VIEWS; ++view_index) {
        if (views[view_index].display_id != output->info.id)
            continue;
        output->last_surface_ids[stored] = views[view_index].surface_id;
        output->last_surface_revisions[stored] =
            views[view_index].surface_revision;
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

BlitKms *blit_kms_new(BlitFont *font)
{
    BlitKms *kms = (BlitKms *)calloc(1, sizeof(BlitKms));
    if (kms != NULL) { kms->fd = -1; kms->font = font; }
    return kms;
}

int blit_kms_open(BlitKms *kms, char *error, size_t error_capacity)
{
    drmModeRes *resources;
    uint32_t used[KMS_MAX_OUTPUTS];
    int used_count = 0;
    int i;
    if (kms == NULL) return -1;
    kms->fd = open(getenv("BLIT_DRM_DEVICE") ? getenv("BLIT_DRM_DEVICE") :
                   "/dev/dri/card0", O_RDWR | O_CLOEXEC);
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
        if (connector == NULL || connector->connection != DRM_MODE_CONNECTED || connector->count_modes == 0) {
            if (connector) drmModeFreeConnector(connector);
            continue;
        }
        output = &kms->outputs[kms->output_count];
        memset(output, 0, sizeof(*output));
        output->connector = connector->connector_id;
        mode_index = 0;
        for (mode_index = 0; mode_index < connector->count_modes; ++mode_index)
            if (connector->modes[mode_index].type & DRM_MODE_TYPE_PREFERRED) break;
        if (mode_index >= connector->count_modes) mode_index = 0;
        output->mode = connector->modes[mode_index];
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
    now = kms_now_us();
    timeout_us = (long long)kms_flip_timeout_ms() * 1000LL;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        if (!output->flip_pending || output->flip_started_us <= 0 ||
            now - output->flip_started_us < timeout_us)
            continue;
        output->modeset_fallback = 1;
        output->flip_pending = 0;
        output->flip_started_us = 0;
        fprintf(stderr,
                "blit: KMS page-flip timeout on connector=%u; using modeset fallback\n",
                output->connector);
        changed = 1;
    }
    if (!readable) return changed;
    memset(&context, 0, sizeof(context));
    context.version = DRM_EVENT_CONTEXT_VERSION;
    context.page_flip_handler = kms_flip_handler;
    if (drmHandleEvent(kms->fd, &context) < 0) return -1;
    for (i = 0; i < kms->output_count; ++i)
        if (!kms->outputs[i].flip_pending)
            changed = 1;
    return changed;
}

int blit_kms_display_info(const BlitKms *kms, int index, BlitKmsDisplayInfo *info)
{
    if (kms == NULL || info == NULL || index < 0 || index >= kms->output_count) return -1;
    *info = kms->outputs[index].info;
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
                     float pointer_x, float pointer_y, int pointer_visible)
{
    int i;
    int capture;
    int pointer_changed;
    int full_redraw;
    int deferred = 0;
    long long started = 0;
    if (kms == NULL) return -1;
    for (i = 0; i < view_count; ++i)
        if (views[i].damage_consumed != NULL)
            *views[i].damage_consumed = 0;
    if (getenv("BLIT_KMS_TRACE_TIMING") != NULL)
        started = kms_now_us();

    capture = kms->screenshot_dir[0] != '\0' && !kms->screenshot_written &&
        kms->screenshot_frame >= kms->screenshot_after &&
        all_outputs_have_content(kms, views, view_count);
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
        int trace_phases = getenv("BLIT_KMS_TRACE_PHASES") != NULL;
        if (trace_phases)
            output_started = kms_now_us();
        int output_changed;
        full_redraw = !output->initialized;
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
        } else {
            memcpy(output->back.map, output->front.map, output->back.size);
            if (pointer_changed)
                restore_pointer_background(output);
        }
        if (trace_phases)
            draw_started = kms_now_us();
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id) {
                BlitKmsSurfaceView draw = views[view_index];
                int pointer_damage = 0;
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
                          &draw);
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
        if (capture)
            write_screenshot(kms, output);
        {
            unsigned int flags = DRM_MODE_PAGE_FLIP_EVENT;
            int flip_result;
            if (modeset_frame) {
                flip_result = drmModeSetCrtc(kms->fd, output->crtc,
                                             output->back.fb, 0, 0,
                                             &output->connector, 1,
                                             &output->mode);
                if (flip_result < 0) return -1;
                if (output->modeset_fallback) {
                    fprintf(stderr,
                            "blit: KMS modeset recovery complete on connector=%u; resuming page flips\n",
                            output->connector);
                    output->modeset_fallback = 0;
                }
                output->flip_pending = 0;
                output->flip_started_us = 0;
                swap_buffers(output);
                for (view_index = 0; view_index < view_count; ++view_index)
                    if (views[view_index].display_id == output->info.id &&
                        views[view_index].damage_consumed != NULL)
                        *views[view_index].damage_consumed = 1;
                output->initialized = 1;
                remember_output_views(output, views, view_count,
                                      output_view_count);
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
            if (flip_result < 0) return -1;
        }
        output->flip_pending = 1;
        output->flip_started_us = kms_now_us();
        swap_buffers(output);
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id &&
                views[view_index].damage_consumed != NULL)
                *views[view_index].damage_consumed = 1;
        output->initialized = 1;
        if (trace_phases)
            fprintf(stderr,
                    "blit: KMS phase connector=%u draw_us=%lld flip_us=%lld total_us=%lld\n",
                    output->connector, flip_started - draw_started,
                    kms_now_us() - flip_started,
                    kms_now_us() - output_started);
        remember_output_views(output, views, view_count, output_view_count);
    }
    if (!deferred) {
        kms->pointer_state_valid = 1;
        kms->previous_pointer_x = pointer_x;
        kms->previous_pointer_y = pointer_y;
        kms->previous_pointer_visible = pointer_visible;
    }
    ++kms->screenshot_frame;
    if (capture)
        kms->screenshot_written = 1;
    if (started != 0)
        fprintf(stderr, "blit: KMS present views=%d outputs=%d duration_us=%lld\n",
                view_count, kms->output_count, kms_now_us() - started);
    return 0;
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
