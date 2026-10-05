#include "blit_kms.h"

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
#define KMS_WHITE 0xbed2beU

typedef struct {
    uint32_t codepoint;
    size_t glyph;
} KmsGlyphMap;

typedef struct {
    uint32_t fb;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    void *map;
} KmsBuffer;

typedef struct {
    unsigned char *data;
    size_t size;
    size_t glyph_offset;
    size_t glyph_bytes;
    size_t glyph_count;
    int width;
    int height;
    uint32_t *codepoints;
    int ascii_glyph[256];
    KmsGlyphMap *unicode_map;
    size_t unicode_count;
    size_t unicode_capacity;
} KmsFont;

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
    int modeset_fallback;
    int initialized;
    int last_view_count;
    unsigned long long last_surface_ids[KMS_MAX_VIEWS];
    unsigned long long last_surface_revisions[KMS_MAX_VIEWS];
    KmsFont *font;
} KmsOutput;

struct BlitKms {
    int fd;
    int output_count;
    KmsOutput outputs[KMS_MAX_OUTPUTS];
    KmsFont font;
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

static void kms_flip_handler(int fd, unsigned int frame, unsigned int sec,
                             unsigned int usec, void *data)
{
    KmsOutput *output = (KmsOutput *)data;
    (void)fd; (void)frame; (void)sec; (void)usec;
    if (output != NULL) {
        output->flip_done = 1;
        output->flip_pending = 0;
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

static void put_pixel(KmsOutput *output, int x, int y, uint32_t color)
{
    uint32_t *pixels;
    if (x < 0 || y < 0 || x >= output->mode.hdisplay ||
        y >= output->mode.vdisplay) return;
    pixels = (uint32_t *)((unsigned char *)output->back.map +
                          y * output->back.pitch);
    pixels[x] = color;
}

static void fill_rect(KmsOutput *output, int x, int y, int width, int height,
                      uint32_t color)
{
    int row;
    int column;
    for (row = 0; row < height; ++row)
        for (column = 0; column < width; ++column)
            put_pixel(output, x + column, y + row, color);
}

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
        *value = ((uint32_t)(p[0] & 7) << 18) |
            ((uint32_t)(p[1] & 0x3f) << 12) |
            ((uint32_t)(p[2] & 0x3f) << 6) | (p[3] & 0x3f);
        *used = 4; return 1;
    }
    return 0;
}

static void font_free(KmsFont *font)
{
    if (font == NULL) return;
    free(font->codepoints);
    free(font->unicode_map);
    free(font->data);
    memset(font, 0, sizeof(*font));
}

static int font_add_mapping(KmsFont *font, uint32_t codepoint,
                            size_t glyph)
{
    KmsGlyphMap *next;
    size_t capacity;
    if (font == NULL || glyph >= font->glyph_count)
        return 0;
    if (font->unicode_count == font->unicode_capacity) {
        capacity = font->unicode_capacity ? font->unicode_capacity * 2 : 256;
        next = (KmsGlyphMap *)realloc(font->unicode_map,
                                      capacity * sizeof(*next));
        if (next == NULL) return 0;
        font->unicode_map = next;
        font->unicode_capacity = capacity;
    }
    font->unicode_map[font->unicode_count].codepoint = codepoint;
    font->unicode_map[font->unicode_count].glyph = glyph;
    if (codepoint < 256)
        font->ascii_glyph[codepoint] = (int)glyph;
    ++font->unicode_count;
    return 1;
}

static int font_load_file(KmsFont *font, const char *path)
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
        /* PSF1 stores mode at byte 2 and bytes-per-glyph at byte 3. */
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
            header + (size_t)bytes * count > (size_t)file_size)
            { font_free(font); return -1; }
    } else {
        font_free(font); return -1;
    }
    if (font->width < 1 || font->width > 32 || font->height < 1 ||
        font->height > 64 || font->glyph_count == 0) {
        font_free(font); return -1;
    }
    font->codepoints = (uint32_t *)malloc(font->glyph_count * sizeof(uint32_t));
    if (font->codepoints == NULL) { font_free(font); return -1; }
    for (i = 0; i < 256; ++i) font->ascii_glyph[i] = -1;
    for (i = 0; i < font->glyph_count; ++i) font->codepoints[i] = (uint32_t)i;
    if (data[0] == 0x36 && data[1] == 0x04 && (data[2] & 2)) {
        size_t cursor = font->glyph_offset + font->glyph_bytes * font->glyph_count;
        size_t glyph = 0;
        while (cursor + 1 < (size_t)file_size && glyph < font->glyph_count) {
            uint32_t value = (uint32_t)data[cursor] |
                ((uint32_t)data[cursor + 1] << 8);
            cursor += 2;
            if (value == 0xffffU) {
                ++glyph;
            } else if (value != 0xfffeU) {
                if (!font_add_mapping(font, value, glyph)) {
                    font_free(font);
                    return -1;
                }
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
                        font_free(font);
                        return -1;
                    }
                    cursor += used;
                } else ++cursor;
            }
        }
    }
    for (i = 0; i < font->glyph_count && i < 256; ++i)
        if (font->ascii_glyph[i] < 0)
            font->ascii_glyph[i] = (int)i;
    return 0;
}

static void font_load(KmsFont *font)
{
    const char *configured = getenv("BLIT_PSF_FONT");
    const char *paths[4];
    int i;
    paths[0] = configured;
    paths[1] = "/usr/share/consolefonts/Uni2-Terminus16.psf";
    paths[2] = "/usr/share/blit/Uni2-Terminus16.psf";
    paths[3] = "/usr/share/consolefonts/Uni2-Fixed16.psf";
    for (i = 0; i < 4; ++i)
        if (paths[i] != NULL && font_load_file(font, paths[i]) == 0) {
            fprintf(stderr, "blit: KMS font=%s size=%dx%d glyphs=%lu mappings=%lu\n",
                    paths[i], font->width, font->height,
                    (unsigned long)font->glyph_count,
                    (unsigned long)font->unicode_count);
            return;
        }
    fprintf(stderr, "blit: KMS font unavailable; using built-in fallback\n");
}

static int font_pixel(const KmsFont *font, uint32_t codepoint, int row, int col)
{
    size_t i;
    size_t row_bytes;
    if (font == NULL || font->data == NULL || row < 0 || col < 0 ||
        row >= font->height || col >= font->width) return 0;
    if (codepoint < 256 && font->ascii_glyph[codepoint] >= 0) {
        i = (size_t)font->ascii_glyph[codepoint];
        goto glyph_found;
    }
    if (font->unicode_map != NULL) {
        for (i = 0; i < font->unicode_count; ++i)
            if (font->unicode_map[i].codepoint == codepoint) {
                i = font->unicode_map[i].glyph;
                goto glyph_found;
            }
    }
    for (i = 0; i < font->glyph_count; ++i)
        if (font->codepoints[i] == codepoint) break;
    if (i == font->glyph_count) i = codepoint < font->glyph_count ? codepoint : 0;
glyph_found:
    row_bytes = (size_t)((font->width + 7) / 8);
    return (font->data[font->glyph_offset + i * font->glyph_bytes +
                       row * row_bytes + col / 8] & (0x80 >> (col % 8))) != 0;
}

/* Deliberately small built-in glyphs keep the first KMS backend dependency-free.
   The terminal state is owned by blit_server; a font provider will replace this
   rasterizer without changing the KMS contract. */
static int glyph_bit(unsigned int codepoint, int row, int column)
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
    if (codepoint >= '0' && codepoint <= '9' && row < 7)
        return (digits[codepoint - '0'][row] >> (4 - column)) & 1;
    if (codepoint >= 'A' && codepoint <= 'Z' && row < 7)
        return (letters[codepoint - 'A'][row] >> (4 - column)) & 1;
    if (codepoint >= 'a' && codepoint <= 'z' && row < 7)
        return (letters[codepoint - 'a'][row] >> (4 - column)) & 1;
    if (codepoint == '.' && row == 6 && column == 2) return 1;
    if (codepoint == ':' && (row == 2 || row == 5) && column == 2) return 1;
    if (codepoint == '-' && row == 3 && column >= 1 && column <= 3) return 1;
    return 0;
}

static void draw_view(KmsOutput *output, const KmsFont *font,
                      const BlitKmsSurfaceView *view)
{
    int row;
    int column;
    int glyph_row;
    int glyph_column;
    int cw = view->cell_width > 0 ? view->cell_width :
        (font != NULL && font->width > 0 ? font->width : KMS_FONT_W);
    int ch = view->cell_height > 0 ? view->cell_height :
        (font != NULL && font->height > 0 ? font->height : KMS_FONT_H);
    int first_row = 0;
    int last_row = view->rows;
    int first_column = 0;
    int last_column = view->columns;
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
    for (row = first_row; row < last_row; ++row) {
        for (column = first_column; column < last_column; ++column) {
            int index = row * view->stride + column;
            uint32_t fg = view->foreground[index] ?
                view->foreground[index] - 1 : KMS_WHITE;
            uint32_t bg = view->background[index] ?
                view->background[index] - 1 : 0;
            uint32_t cp = view->codepoints[index] ? view->codepoints[index] : ' ';
            unsigned char attr = view->attributes[index];
            int x = view->origin_x + column * cw;
            int y = view->origin_y + row * ch;
            if (attr & 8) { uint32_t t = fg; fg = bg; bg = t; }
            if (attr & 2) fg = (fg & 0xfefefeU) >> 1;
            if (cp == ' ' && bg == 0 && !(attr & 4) &&
                (view->combining_count == NULL ||
                 view->combining_count[index] == 0))
                continue;
            if (bg) fill_rect(output, x, y, cw, ch, bg);
            for (glyph_row = 0; glyph_row < (font != NULL && font->data ?
                 font->height : 7) && glyph_row < ch; ++glyph_row)
                for (glyph_column = 0; glyph_column < (font != NULL && font->data ?
                     cw : 5) && glyph_column < cw;
                     ++glyph_column)
                    if (font != NULL && font->data ?
                        font_pixel(font, cp, glyph_row,
                                   cw < font->width ?
                                   glyph_column * font->width / cw : glyph_column) :
                        glyph_bit(cp, glyph_row, glyph_column))
                        put_pixel(output, x + glyph_column, y + glyph_row, fg);
            if (attr & 4) fill_rect(output, x, y + ch - 1, cw, 1, fg);
            if (attr & 1)
                for (glyph_row = 0; glyph_row < (font != NULL && font->data ?
                     font->height : 7) && glyph_row < ch; ++glyph_row)
                    for (glyph_column = 0; glyph_column < (font != NULL && font->data ?
                     cw : 5) && glyph_column < cw;
                         ++glyph_column)
                        if (font != NULL && font->data ?
                            font_pixel(font, cp, glyph_row,
                                       cw < font->width ?
                                       glyph_column * font->width / cw : glyph_column) :
                        glyph_bit(cp, glyph_row, glyph_column))
                        put_pixel(output, x + glyph_column + 1,
                                      y + glyph_row, fg);
            if (view->combining != NULL && view->combining_count != NULL) {
                int combining_index;
                for (combining_index = 0;
                     combining_index < view->combining_count[index] &&
                     combining_index < 4; ++combining_index) {
                    uint32_t mark = view->combining[index * 4 + combining_index];
                    for (glyph_row = 0; glyph_row < (font != NULL && font->data ?
                         font->height : 7) && glyph_row < ch; ++glyph_row)
                        for (glyph_column = 0; glyph_column < (font != NULL && font->data ?
                             cw : 5) && glyph_column < cw; ++glyph_column)
                            if (font != NULL && font->data ?
                                font_pixel(font, mark, glyph_row,
                                           cw < font->width ?
                                           glyph_column * font->width / cw : glyph_column) :
                                glyph_bit(mark, glyph_row, glyph_column))
                                put_pixel(output, x + glyph_column,
                                          y + glyph_row, fg);
                }
            }
        }
    }
    if (view->cursor_visible && view->cursor_x >= 0 && view->cursor_y >= 0)
        fill_rect(output, view->origin_x + view->cursor_x * cw,
                  view->origin_y + view->cursor_y * ch + ch - 1, cw, 1, KMS_WHITE);
}

static int wait_flip(BlitKms *kms, KmsOutput *output)
{
    drmEventContext context;
    fd_set set;
    struct timeval timeout;
    int result;
    long long deadline;
    int timeout_ms = 100;
    const char *timeout_value = getenv("BLIT_KMS_FLIP_TIMEOUT_MS");
    if (!output->flip_pending) return 0;
    if (timeout_value != NULL && timeout_value[0] != '\0') {
        int requested = atoi(timeout_value);
        if (requested >= 1 && requested <= 1000)
            timeout_ms = requested;
    }
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

BlitKms *blit_kms_new(void)
{
    BlitKms *kms = (BlitKms *)calloc(1, sizeof(BlitKms));
    if (kms != NULL) kms->fd = -1;
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
    font_load(&kms->font);
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
    return kms != NULL && kms->font.width > 0 ? kms->font.width : KMS_FONT_W;
}

int blit_kms_cell_height(const BlitKms *kms)
{
    return kms != NULL && kms->font.height > 0 ? kms->font.height : KMS_FONT_H;
}

int blit_kms_present(BlitKms *kms, const BlitKmsSurfaceView *views, int view_count,
                     float pointer_x, float pointer_y, int pointer_visible)
{
    int i;
    int capture;
    int pointer_changed;
    int full_redraw;
    long long started = 0;
    if (kms == NULL) return -1;
    if (getenv("BLIT_KMS_TRACE_TIMING") != NULL)
        started = kms_now_us();
    capture = kms->screenshot_dir[0] != '\0' && !kms->screenshot_written &&
        kms->screenshot_frame >= kms->screenshot_after && view_count >= 1 &&
        view_has_content(&views[0]);
    pointer_changed = !kms->pointer_state_valid ||
        kms->previous_pointer_x != pointer_x ||
        kms->previous_pointer_y != pointer_y ||
        kms->previous_pointer_visible != pointer_visible;
    for (i = 0; i < kms->output_count; ++i) {
        KmsOutput *output = &kms->outputs[i];
        int view_index;
        int output_view_count = 0;
        int output_changed;
        full_redraw = !output->initialized || pointer_changed;
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id)
                ++output_view_count;
        if (output_view_count != output->last_view_count)
            full_redraw = 1;
        output_changed = full_redraw || capture;
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
        /* A render request can be generated by a PTY or control event that
           does not alter this output.  Do not submit a page flip in that
           case: page flips are not a heartbeat, and repeatedly submitting
           unchanged buffers makes a slow KMS driver look hung while wasting
           a CPU and event slot on every display. */
        if (!output_changed)
            continue;
        /* The previous page flip must complete before the old front buffer
           becomes our writable back buffer.  Drawing first can modify the
           buffer while the CRTC is still scanning it, producing horizontal
           smear/tearing across the whole output. */
        if (getenv("BLIT_KMS_MODESET") == NULL &&
            !output->modeset_fallback) {
            int wait_result = wait_flip(kms, output);
            if (wait_result == -2) {
                output->modeset_fallback = 1;
                output->flip_pending = 0;
                fprintf(stderr,
                        "blit: KMS page-flip timeout on connector=%u; using modeset fallback\n",
                        output->connector);
            } else if (wait_result < 0) {
                return -1;
            }
        } else {
            output->flip_pending = 0;
        }
        if (full_redraw)
            memset(output->back.map, 0, output->back.size);
        for (view_index = 0; view_index < view_count; ++view_index)
            if (views[view_index].display_id == output->info.id) {
                BlitKmsSurfaceView draw = views[view_index];
                if (!full_redraw && output_view_unchanged(output,
                                                           &views[view_index]))
                    continue;
                if (!full_redraw && views[view_index].damage_valid) {
                    int cw = views[view_index].cell_width > 0 ?
                        views[view_index].cell_width : KMS_FONT_W;
                    int ch = views[view_index].cell_height > 0 ?
                        views[view_index].cell_height : KMS_FONT_H;
                    fill_rect(output,
                              views[view_index].origin_x +
                                  views[view_index].damage_x * cw,
                              views[view_index].origin_y +
                                  views[view_index].damage_y * ch,
                              views[view_index].damage_width * cw,
                              views[view_index].damage_height * ch, 0);
                }
                if (full_redraw)
                    draw.damage_valid = 0;
                draw_view(output, kms != NULL ? &kms->font : NULL,
                          &draw);
            }
        if (pointer_visible && pointer_x >= output->info.x &&
            pointer_x < output->info.x + output->info.width &&
            pointer_y >= output->info.y && pointer_y < output->info.y + output->info.height) {
            int x = (int)pointer_x - output->info.x;
            int y = (int)pointer_y - output->info.y;
            fill_rect(output, x, y, 2, 18, 0xffffffU);
            fill_rect(output, x, y, 12, 2, 0xffffffU);
        }
        if (capture)
            write_screenshot(kms, output);
        {
            unsigned int flags = DRM_MODE_PAGE_FLIP_EVENT;
            int flip_result;
            if (getenv("BLIT_KMS_MODESET") != NULL ||
                output->modeset_fallback) {
                flip_result = drmModeSetCrtc(kms->fd, output->crtc,
                                             output->back.fb, 0, 0,
                                             &output->connector, 1,
                                             &output->mode);
                if (flip_result < 0) return -1;
                output->flip_pending = 0;
                swap_buffers(output);
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
        swap_buffers(output);
        output->initialized = 1;
        output->last_view_count = output_view_count;
        {
            int stored = 0;
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
    }
    kms->pointer_state_valid = 1;
    kms->previous_pointer_x = pointer_x;
    kms->previous_pointer_y = pointer_y;
    kms->previous_pointer_visible = pointer_visible;
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
    font_free(&kms->font);
    free(kms);
}
