/*
 * Readable, controlled DRM/KMS modeset test for Blit.
 *
 * This deliberately presents a solid diagnostic frame and then restores the
 * previous CRTC state. It is the first direct-display vertical slice; it is
 * not the terminal renderer and does not start Hibr or SDL.
 */
#include <errno.h>
#include <fcntl.h>
#include <linux/input.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/select.h>
#include <sys/wait.h>
#include <pty.h>
#include <unistd.h>
#include <xf86drm.h>
#include <xf86drmMode.h>

typedef struct {
    uint32_t connector_id;
    uint32_t crtc_id;
    drmModeModeInfo mode;
    drmModeCrtc *old_crtc;
    uint32_t fb_id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    void *map;
    uint32_t back_fb_id;
    uint32_t back_handle;
    uint32_t back_pitch;
    uint64_t back_size;
    void *back_map;
} KmsFrame;

static volatile int page_flip_done;

static void page_flip_handler(int fd, unsigned int frame,
                              unsigned int sec, unsigned int usec,
                              void *data)
{
    (void)fd; (void)frame; (void)sec; (void)usec; (void)data;
    page_flip_done = 1;
}

#define GRID_COLS 160
#define GRID_ROWS 60

typedef struct {
    char cells[GRID_ROWS][GRID_COLS];
    int x;
    int y;
    int esc;
    char csi[32];
    int csi_len;
} TermGrid;

static void grid_clear(TermGrid *grid)
{
    int y;
    int x;
    for (y = 0; y < GRID_ROWS; ++y)
        for (x = 0; x < GRID_COLS; ++x)
            grid->cells[y][x] = ' ';
    grid->x = 0;
    grid->y = 0;
    grid->esc = 0;
    grid->csi_len = 0;
}

static void grid_newline(TermGrid *grid)
{
    grid->x = 0;
    if (grid->y + 1 < GRID_ROWS)
        ++grid->y;
    else {
        int y;
        int x;
        for (y = 1; y < GRID_ROWS; ++y)
            for (x = 0; x < GRID_COLS; ++x)
                grid->cells[y - 1][x] = grid->cells[y][x];
        for (x = 0; x < GRID_COLS; ++x)
            grid->cells[GRID_ROWS - 1][x] = ' ';
    }
}

static void grid_csi(TermGrid *grid, char command)
{
    int first = 1;
    int second = 1;
    int parsed;
    if (grid->csi_len > 0)
        parsed = sscanf(grid->csi, "%d;%d", &first, &second);
    else
        parsed = 0;
    if (command == 'J') {
        int y;
        int x;
        if (first == 2 || first == 3 || !parsed)
            for (y = 0; y < GRID_ROWS; ++y)
                for (x = 0; x < GRID_COLS; ++x)
                    grid->cells[y][x] = ' ';
        if (first == 2 || first == 3 || !parsed)
            grid->x = grid->y = 0;
    } else if (command == 'H' || command == 'f') {
        grid->y = first > 0 ? first - 1 : 0;
        grid->x = second > 0 ? second - 1 : 0;
        if (grid->y >= GRID_ROWS) grid->y = GRID_ROWS - 1;
        if (grid->x >= GRID_COLS) grid->x = GRID_COLS - 1;
    } else if (command == 'K') {
        int x;
        for (x = grid->x; x < GRID_COLS; ++x)
            grid->cells[grid->y][x] = ' ';
    }
}

static void grid_feed(TermGrid *grid, const char *data, int length)
{
    int i;
    for (i = 0; i < length; ++i) {
        unsigned char ch = (unsigned char)data[i];
        if (grid->esc) {
            if (grid->esc == 1 && ch == '[') {
                grid->esc = 2;
                grid->csi_len = 0;
            } else if (grid->esc == 2) {
                if ((ch >= '0' && ch <= '9') || ch == ';' || ch == '?') {
                    if (grid->csi_len + 1 < (int)sizeof(grid->csi))
                        grid->csi[grid->csi_len++] = (char)ch;
                } else {
                    grid->csi[grid->csi_len] = '\0';
                    grid_csi(grid, (char)ch);
                    grid->esc = 0;
                }
            } else {
                grid->esc = 0;
            }
            continue;
        }
        if (ch == 0x1b) { grid->esc = 1; continue; }
        if (ch == '\r') { grid->x = 0; continue; }
        if (ch == '\n') { grid_newline(grid); continue; }
        if (ch == '\b') { if (grid->x > 0) --grid->x; continue; }
        if (ch == '\t') { grid->x = (grid->x + 8) & ~7; continue; }
        if (ch < 0x20) continue;
        if (grid->x >= GRID_COLS) grid_newline(grid);
        grid->cells[grid->y][grid->x++] = (char)ch;
    }
}

static int choose_crtc(int fd, drmModeRes *resources,
                       drmModeConnector *connector, uint32_t *used,
                       int used_count, uint32_t *out)
{
    int i;
    int j;
    drmModeEncoder *encoder;

    for (i = -1; i < connector->count_encoders; ++i) {
        uint32_t encoder_id = i < 0 ? connector->encoder_id :
            connector->encoders[i];
        if (encoder_id == 0)
            continue;
        encoder = drmModeGetEncoder(fd, encoder_id);
        if (encoder == NULL)
            continue;
        for (j = 0; j < resources->count_crtcs; ++j) {
            uint32_t crtc_id = resources->crtcs[j];
            int k;
            if (!(encoder->possible_crtcs & (1U << j)))
                continue;
            if (crtc_id == encoder->crtc_id && encoder->crtc_id != 0) {
                /* Prefer the connector's current CRTC. */
            }
            for (k = 0; k < used_count; ++k)
                if (used[k] == crtc_id)
                    break;
            if (k == used_count) {
                *out = crtc_id;
                drmModeFreeEncoder(encoder);
                return 0;
            }
        }
        drmModeFreeEncoder(encoder);
    }
    return -1;
}

static int create_buffer(int fd, KmsFrame *frame)
{
    struct drm_mode_create_dumb create;
    struct drm_mode_map_dumb map;
    memset(&create, 0, sizeof(create));
    create.width = frame->mode.hdisplay;
    create.height = frame->mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        return -1;
    frame->handle = create.handle;
    frame->pitch = create.pitch;
    frame->size = create.size;
    if (drmModeAddFB(fd, frame->mode.hdisplay, frame->mode.vdisplay,
                     24, 32, frame->pitch, frame->handle,
                     &frame->fb_id) < 0)
        return -1;
    memset(&map, 0, sizeof(map));
    map.handle = frame->handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
        return -1;
    frame->map = mmap(NULL, frame->size, PROT_READ | PROT_WRITE,
                      MAP_SHARED, fd, map.offset);
    if (frame->map == MAP_FAILED)
        return -1;
    return 0;
}

static void destroy_buffer(int fd, KmsFrame *frame)
{
    struct drm_mode_destroy_dumb destroy;
    if (frame->map != NULL && frame->map != MAP_FAILED)
        munmap(frame->map, frame->size);
    if (frame->fb_id != 0)
        drmModeRmFB(fd, frame->fb_id);
    memset(&destroy, 0, sizeof(destroy));
    destroy.handle = frame->handle;
    if (frame->handle != 0)
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
    if (frame->back_map != NULL && frame->back_map != MAP_FAILED)
        munmap(frame->back_map, frame->back_size);
    if (frame->back_fb_id != 0)
        drmModeRmFB(fd, frame->back_fb_id);
    memset(&destroy, 0, sizeof(destroy));
    destroy.handle = frame->back_handle;
    if (frame->back_handle != 0)
        drmIoctl(fd, DRM_IOCTL_MODE_DESTROY_DUMB, &destroy);
}

static int create_back_buffer(int fd, KmsFrame *frame)
{
    struct drm_mode_create_dumb create;
    struct drm_mode_map_dumb map;
    memset(&create, 0, sizeof(create));
    create.width = frame->mode.hdisplay;
    create.height = frame->mode.vdisplay;
    create.bpp = 32;
    if (drmIoctl(fd, DRM_IOCTL_MODE_CREATE_DUMB, &create) < 0)
        return -1;
    frame->back_handle = create.handle;
    frame->back_pitch = create.pitch;
    frame->back_size = create.size;
    if (drmModeAddFB(fd, frame->mode.hdisplay, frame->mode.vdisplay,
                     24, 32, frame->back_pitch, frame->back_handle,
                     &frame->back_fb_id) < 0)
        return -1;
    memset(&map, 0, sizeof(map));
    map.handle = frame->back_handle;
    if (drmIoctl(fd, DRM_IOCTL_MODE_MAP_DUMB, &map) < 0)
        return -1;
    frame->back_map = mmap(NULL, frame->back_size, PROT_READ | PROT_WRITE,
                           MAP_SHARED, fd, map.offset);
    return frame->back_map == MAP_FAILED ? -1 : 0;
}

static void swap_buffers(KmsFrame *frame)
{
    uint32_t id;
    uint32_t handle;
    uint32_t pitch;
    uint64_t size;
    void *map;
    id = frame->fb_id; frame->fb_id = frame->back_fb_id; frame->back_fb_id = id;
    handle = frame->handle; frame->handle = frame->back_handle; frame->back_handle = handle;
    pitch = frame->pitch; frame->pitch = frame->back_pitch; frame->back_pitch = pitch;
    size = frame->size; frame->size = frame->back_size; frame->back_size = size;
    map = frame->map; frame->map = frame->back_map; frame->back_map = map;
}

static int page_flip(int fd, KmsFrame *frame)
{
    drmEventContext context;
    fd_set readable;
    struct timeval timeout;
    memset(&context, 0, sizeof(context));
    context.version = DRM_EVENT_CONTEXT_VERSION;
    context.page_flip_handler = page_flip_handler;
    page_flip_done = 0;
    if (drmModePageFlip(fd, frame->crtc_id, frame->fb_id,
                        DRM_MODE_PAGE_FLIP_EVENT, NULL) < 0)
        return -1;
    while (!page_flip_done) {
        FD_ZERO(&readable);
        FD_SET(fd, &readable);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (select(fd + 1, &readable, NULL, NULL, &timeout) <= 0)
            return -1;
        if (drmHandleEvent(fd, &context) < 0)
            return -1;
    }
    return 0;
}

static void fill_buffer(KmsFrame *frame, uint32_t color)
{
    uint32_t *pixels = frame->map;
    uint32_t width = frame->mode.hdisplay;
    uint32_t height = frame->mode.vdisplay;
    uint32_t y;
    uint32_t x;
    for (y = 0; y < height; ++y)
        for (x = 0; x < width; ++x)
            pixels[(y * frame->pitch / 4) + x] = color;
}

static int glyph_rows(char ch, uint8_t rows[7])
{
    memset(rows, 0, 7);
    switch (ch) {
    case 'A': rows[0]=14; rows[1]=17; rows[2]=17; rows[3]=31; rows[4]=17; rows[5]=17; rows[6]=17; break;
    case 'B': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=30; rows[4]=17; rows[5]=17; rows[6]=30; break;
    case 'C': rows[0]=14; rows[1]=17; rows[2]=16; rows[3]=16; rows[4]=16; rows[5]=17; rows[6]=14; break;
    case 'D': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=17; rows[4]=17; rows[5]=17; rows[6]=30; break;
    case 'E': rows[0]=31; rows[1]=16; rows[2]=16; rows[3]=30; rows[4]=16; rows[5]=16; rows[6]=31; break;
    case 'I': rows[0]=31; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=31; break;
    case 'K': rows[0]=17; rows[1]=18; rows[2]=20; rows[3]=24; rows[4]=20; rows[5]=18; rows[6]=17; break;
    case 'L': rows[0]=16; rows[1]=16; rows[2]=16; rows[3]=16; rows[4]=16; rows[5]=16; rows[6]=31; break;
    case 'M': rows[0]=17; rows[1]=27; rows[2]=21; rows[3]=21; rows[4]=17; rows[5]=17; rows[6]=17; break;
    case 'O': rows[0]=14; rows[1]=17; rows[2]=17; rows[3]=17; rows[4]=17; rows[5]=17; rows[6]=14; break;
    case 'P': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=30; rows[4]=16; rows[5]=16; rows[6]=16; break;
    case 'R': rows[0]=30; rows[1]=17; rows[2]=17; rows[3]=30; rows[4]=20; rows[5]=18; rows[6]=17; break;
    case 'S': rows[0]=15; rows[1]=16; rows[2]=16; rows[3]=14; rows[4]=1; rows[5]=1; rows[6]=30; break;
    case 'T': rows[0]=31; rows[1]=4; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=4; break;
    case 'U': rows[0]=17; rows[1]=17; rows[2]=17; rows[3]=17; rows[4]=17; rows[5]=17; rows[6]=14; break;
    case '0': rows[0]=14; rows[1]=17; rows[2]=19; rows[3]=21; rows[4]=25; rows[5]=17; rows[6]=14; break;
    case '1': rows[0]=4; rows[1]=12; rows[2]=4; rows[3]=4; rows[4]=4; rows[5]=4; rows[6]=14; break;
    case '2': rows[0]=14; rows[1]=17; rows[2]=1; rows[3]=2; rows[4]=4; rows[5]=8; rows[6]=31; break;
    default: return ch == ' ' ? 0 : -1;
    }
    return 0;
}

static void draw_text(KmsFrame *frame, int x, int y, int scale,
                      const char *text, uint32_t color)
{
    uint32_t *pixels = frame->map;
    int cursor = x;
    while (*text != '\0') {
        uint8_t rows[7];
        int row;
        int column;
        if (glyph_rows(*text, rows) == 0) {
            for (row = 0; row < 7; ++row)
                for (column = 0; column < 5; ++column)
                    if (rows[row] & (1 << (4 - column))) {
                        int sy;
                        int sx;
                        for (sy = 0; sy < scale; ++sy)
                            for (sx = 0; sx < scale; ++sx)
                                pixels[((y + row * scale + sy) *
                                        frame->pitch / 4) +
                                       cursor + column * scale + sx] = color;
                    }
        }
        cursor += 6 * scale;
        ++text;
    }
}

static void draw_grid(KmsFrame *frame, TermGrid *grid, int scale,
                      uint32_t color)
{
    int y;
    int x;
    for (y = 0; y < GRID_ROWS && y * 8 + 7 < (int)frame->mode.vdisplay / scale; ++y)
        for (x = 0; x < GRID_COLS && x * 6 + 5 < (int)frame->mode.hdisplay / scale; ++x) {
            char text[2];
            text[0] = grid->cells[y][x];
            text[1] = '\0';
            draw_text(frame, x * 6 * scale, y * 8 * scale,
                      scale, text, color);
        }
}

static void draw_pointer(KmsFrame *frame, int x, int y)
{
    uint32_t *pixels = frame->map;
    int width = frame->mode.hdisplay;
    int height = frame->mode.vdisplay;
    int row;
    int column;
    for (row = 0; row < 18; ++row)
        for (column = 0; column < 14; ++column) {
            int draw = column == 0 || row == 0 ||
                (row == column && row < 12) ||
                (column == 1 && row > 1 && row < 15);
            int px = x + column;
            int py = y + row;
            if (draw && px >= 0 && py >= 0 && px < width && py < height)
                pixels[(py * frame->pitch / 4) + px] = 0x00000000U;
        }
    for (row = 1; row < 14; ++row)
        for (column = 1; column < 10; ++column) {
            int draw = column == 1 || row == column;
            int px = x + column;
            int py = y + row;
            if (draw && px >= 0 && py >= 0 && px < width && py < height)
                pixels[(py * frame->pitch / 4) + px] = 0x00ffffffU;
        }
}

static int is_relative_mouse(int fd)
{
    unsigned long relative[2];
    memset(relative, 0, sizeof(relative));
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relative)), relative) < 0)
        return 0;
    return (relative[REL_X / (sizeof(unsigned long) * 8)] &
            (1UL << (REL_X % (sizeof(unsigned long) * 8)))) != 0 &&
        (relative[REL_Y / (sizeof(unsigned long) * 8)] &
         (1UL << (REL_Y % (sizeof(unsigned long) * 8)))) != 0;
}

static int open_mice(int *fds, char paths[][64], int capacity)
{
    const char *configured = getenv("BLIT_EVDEV_MOUSE");
    int index;
    int fd;
    int count = 0;
    if (configured != NULL && configured[0] != '\0' && count < capacity) {
        fd = open(configured, O_RDONLY | O_NONBLOCK);
        if (fd >= 0 && is_relative_mouse(fd)) {
            fds[count] = fd;
            snprintf(paths[count], 64, "%s", configured);
            ++count;
        } else if (fd >= 0) {
            close(fd);
        }
    }
    for (index = 0; index < 32; ++index) {
        char path[64];
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK);
        if (fd < 0)
            continue;
        if (is_relative_mouse(fd) && count < capacity) {
            fds[count] = fd;
            snprintf(paths[count], 64, "%s", path);
            ++count;
        } else {
            close(fd);
        }
    }
    return count;
}

static int open_terminal_pty(int *master)
{
    struct winsize size;
    pid_t child;
    memset(&size, 0, sizeof(size));
    size.ws_col = GRID_COLS;
    size.ws_row = GRID_ROWS;
    child = forkpty(master, NULL, NULL, &size);
    if (child < 0)
        return -1;
    if (child == 0) {
        execl("/bin/sh", "sh", "-c",
              "printf '\\033[2J\\033[HBLIT PTY TERMINAL\\n\\033[31mDIRECT KMS\\033[0m\\nOUTPUTS SHARE ONE GRID\\n'; sleep 5",
              (char *)NULL);
        _exit(127);
    }
    return 0;
}

int main(void)
{
    int fd = open("/dev/dri/card0", O_RDWR | O_CLOEXEC);
    drmModeRes *resources;
    KmsFrame frames[2];
    uint32_t used_crtcs[2];
    int frame_count = 0;
    int pty_fd = -1;
    int mouse_fds[16];
    char mouse_paths[16][64];
    int mouse_count = 0;
    int mouse_events = 0;
    int total_width = 0;
    int pointer_x = 80;
    int pointer_y = 80;
    TermGrid grid;
    int i;

    memset(frames, 0, sizeof(frames));
    grid_clear(&grid);
    if (open_terminal_pty(&pty_fd) < 0) {
        fprintf(stderr, "cannot create terminal PTY: %s\n", strerror(errno));
        return 1;
    }
    fcntl(pty_fd, F_SETFL, O_NONBLOCK);
    usleep(100000);
    {
        char output[4096];
        int received = read(pty_fd, output, sizeof(output));
        if (received > 0)
            grid_feed(&grid, output, received);
    }
    if (fd < 0) {
        fprintf(stderr, "cannot open /dev/dri/card0: %s\n", strerror(errno));
        return 1;
    }
    resources = drmModeGetResources(fd);
    if (resources == NULL) {
        fprintf(stderr, "cannot query DRM resources: %s\n", strerror(errno));
        close(fd);
        return 1;
    }
    for (i = 0; i < resources->count_connectors && frame_count < 2; ++i) {
        drmModeConnector *connector = drmModeGetConnector(
            fd, resources->connectors[i]);
        KmsFrame *frame;
        int mode;
        if (connector == NULL ||
            connector->connection != DRM_MODE_CONNECTED ||
            connector->count_modes == 0) {
            if (connector != NULL)
                drmModeFreeConnector(connector);
            continue;
        }
        frame = &frames[frame_count];
        frame->connector_id = connector->connector_id;
        mode = 0;
        for (int m = 0; m < connector->count_modes; ++m)
            if (connector->modes[m].type & DRM_MODE_TYPE_PREFERRED) {
                mode = m;
                break;
            }
        frame->mode = connector->modes[mode];
        if (choose_crtc(fd, resources, connector, used_crtcs,
                         frame_count, &frame->crtc_id) < 0) {
            fprintf(stderr, "connector %u has no unused compatible CRTC\n",
                    frame->connector_id);
            drmModeFreeConnector(connector);
            continue;
        }
        frame->old_crtc = drmModeGetCrtc(fd, frame->crtc_id);
        if (create_buffer(fd, frame) < 0) {
            fprintf(stderr, "connector %u buffer setup failed: %s\n",
                    frame->connector_id, strerror(errno));
            destroy_buffer(fd, frame);
            drmModeFreeConnector(connector);
            continue;
        }
        if (create_back_buffer(fd, frame) < 0) {
            fprintf(stderr, "connector %u back-buffer setup failed: %s\n",
                    frame->connector_id, strerror(errno));
            destroy_buffer(fd, frame);
            drmModeFreeConnector(connector);
            continue;
        }
        used_crtcs[frame_count++] = frame->crtc_id;
        printf("prepared connector=%u crtc=%u mode=%s %ux%u@%u\n",
               frame->connector_id, frame->crtc_id, frame->mode.name,
               frame->mode.hdisplay, frame->mode.vdisplay,
               frame->mode.vrefresh);
        drmModeFreeConnector(connector);
    }
    if (frame_count == 0) {
        fprintf(stderr, "no connected output could be prepared\n");
        drmModeFreeResources(resources);
        close(fd);
        return 1;
    }
    mouse_count = open_mice(mouse_fds, mouse_paths, 16);
    if (mouse_count > 0) {
        for (i = 0; i < mouse_count; ++i)
            printf("opened evdev mouse path=%s fd=%d\n",
                   mouse_paths[i], mouse_fds[i]);
    }
    else
        fprintf(stderr, "no evdev mouse available; pointer remains stationary\n");
    for (i = 0; i < frame_count; ++i)
        total_width += frames[i].mode.hdisplay;
    for (i = 0; i < frame_count; ++i) {
        fill_buffer(&frames[i], i == 0 ? 0x00104080U : 0x00804010U);
        draw_grid(&frames[i], &grid, 2, 0x00ffffffU);
        {
            int origin = i == 0 ? 0 : frames[0].mode.hdisplay;
            int local_x = pointer_x - origin;
            if (local_x >= 0 && local_x < frames[i].mode.hdisplay)
                draw_pointer(&frames[i], local_x, pointer_y);
        }
        if (drmModeSetCrtc(fd, frames[i].crtc_id, frames[i].fb_id, 0, 0,
                           &frames[i].connector_id, 1, &frames[i].mode) < 0) {
            fprintf(stderr, "connector %u modeset failed: %s\n",
                    frames[i].connector_id, strerror(errno));
            frame_count = i;
            goto restore;
        }
    }
    printf("presented %d direct-KMS terminal frame(s) for 5 seconds\n", frame_count);
    fflush(stdout);
    {
        time_t deadline = time(NULL) + 5;
        while (time(NULL) < deadline) {
            struct input_event event;
            ssize_t received;
            if (mouse_count > 0) {
                for (i = 0; i < mouse_count; ++i)
                    while ((received = read(mouse_fds[i], &event,
                                             sizeof(event))) ==
                           (ssize_t)sizeof(event)) {
                        if (mouse_events++ == 0)
                            printf("received first evdev mouse event from %s\n",
                                   mouse_paths[i]);
                        if (event.type == EV_REL && event.code == REL_X)
                            pointer_x += event.value;
                        if (event.type == EV_REL && event.code == REL_Y)
                            pointer_y += event.value;
                        if (pointer_x < 0) pointer_x = 0;
                        if (total_width > 20 && pointer_x > total_width - 20)
                            pointer_x = total_width - 20;
                        if (pointer_y < 0) pointer_y = 0;
                    }
            }
            for (i = 0; i < frame_count; ++i) {
                int max_y = (int)frames[i].mode.vdisplay - 20;
                int origin = i == 0 ? 0 : frames[0].mode.hdisplay;
                int x = pointer_x - origin;
                int y = pointer_y > max_y ? max_y : pointer_y;
                swap_buffers(&frames[i]);
                fill_buffer(&frames[i], i == 0 ? 0x00104080U : 0x00804010U);
                draw_grid(&frames[i], &grid, 2, 0x00ffffffU);
                if (x >= 0 && x < (int)frames[i].mode.hdisplay)
                    draw_pointer(&frames[i], x, y);
                if (page_flip(fd, &frames[i]) < 0) {
                    fprintf(stderr, "connector %u page flip failed: %s\n",
                            frames[i].connector_id, strerror(errno));
                    frame_count = i;
                    goto restore;
                }
            }
            usleep(16000);
        }
    }
restore:
    for (i = 0; i < frame_count; ++i) {
        if (frames[i].old_crtc != NULL && frames[i].old_crtc->buffer_id != 0)
            drmModeSetCrtc(fd, frames[i].crtc_id,
                           frames[i].old_crtc->buffer_id,
                           frames[i].old_crtc->x, frames[i].old_crtc->y,
                           &frames[i].connector_id, 1,
                           &frames[i].old_crtc->mode);
        drmModeFreeCrtc(frames[i].old_crtc);
        destroy_buffer(fd, &frames[i]);
    }
    drmModeFreeResources(resources);
    close(pty_fd);
    for (i = 0; i < mouse_count; ++i)
        close(mouse_fds[i]);
    close(fd);
    return 0;
}
