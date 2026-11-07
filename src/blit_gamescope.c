/* blit-gamescope: a game or Steam in a Blit pane, through a headless
   gamescope (ADR 0225, phase 1).

     blit-gamescope [-W w -H h] [-r hz] [--steam] [--log path] -- command...

   It starts `gamescope --backend headless -W -H -r [-e] -- command` in a
   process group of its own, takes gamescope's PipeWire video stream (BGRx,
   LINEAR) and hands each frame to Blit on the session's frame channel
   (BLIT_FRAME_SOCKET, BLIT_FRAME_TOKEN): as FRM3 dmabufs when Blit imports
   them, else as FRM1 memfds. It places the picture with one Kitty `a=p` on
   its own terminal and deletes it at exit. Input does not pass through the
   bridge: it names gamescope's EIS socket to Blit with the INPUT request and
   Blit sends the pane's keys and mouse there itself. The terminal is read
   only for the pane's pixel size, focus and the click that captures the
   mouse (blit_gamescope_tty.h).

   It exits with gamescope's status and never leaves gamescope or what it
   started running: the whole process group is ended on any exit.

   Built with tools/build-tcc.sh gamescope (by `cc`: the PipeWire headers need
   GNU C), linked against libpipewire-0.3.so.0. */
#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <poll.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/mman.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#include <spa/param/video/format-utils.h>
#include <spa/param/buffers.h>
#include <spa/param/param.h>
#include <spa/pod/builder.h>
#include <spa/utils/result.h>
#include <pipewire/pipewire.h>

#include "blit_gamescope_frames.h"
#include "blit_gamescope_tty.h"

#ifndef BLIT_BUILD_VERSION
#define BLIT_BUILD_VERSION "dev"
#endif
/* gamescope's private format key (src/pipewire_gamescope.hpp). */
#define GS_SPA_FORMAT_VIDEO_requested_size 0x70000
#define GS_IMAGE_ID 1
#define GS_BUFFERS 6

struct bridge {
    /* options */
    int width, height, refresh, steam, sized_by_option;
    char **command;
    const char *log_path;
    FILE *log;
    /* terminal */
    struct termios saved_termios;
    int termios_saved;
    GsTty tty;
    int cols, rows;
    int pane_w, pane_h;          /* last size the terminal reported */
    /* gamescope */
    pid_t child;
    int child_status, child_exited;
    int child_err;               /* read end of gamescope's stdout+stderr */
    char err_line[1024];
    size_t err_length;
    char display[64];            /* gamescope-N */
    char eis_path[PATH_MAX];
    int eis_ready;               /* "Successfully initialized libei" seen */
    /* frames */
    GsFrames frames;
    int placed;
    unsigned long frames_in, frames_dropped;
    uint32_t stream_w, stream_h;
    int stream_dmabuf;           /* the negotiated buffers are dmabufs */
    int requested_w, requested_h;
    /* PipeWire */
    struct pw_main_loop *loop;
    struct pw_context *context;
    struct pw_core *core;
    struct pw_registry *registry;
    struct spa_hook core_listener, registry_listener, stream_listener;
    struct pw_stream *stream;
    int initial_sync, synced;
    uint32_t target_serial;
    int quitting;
    int exit_code;
};

static struct bridge *g_bridge;

static void logf_(struct bridge *b, const char *fmt, ...)
{
    va_list ap;
    struct timespec ts;
    if (!b->log) return;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(b->log, "[%ld.%03ld] ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_start(ap, fmt);
    vfprintf(b->log, fmt, ap);
    va_end(ap);
    fputc('\n', b->log);
    fflush(b->log);
}

static void tty_write(const char *s, size_t n)
{
    while (n) {
        ssize_t w = write(STDOUT_FILENO, s, n);
        if (w < 0 && errno == EINTR) continue;
        if (w <= 0) return;
        s += w;
        n -= (size_t)w;
    }
}

static void tty_puts(const char *s) { tty_write(s, strlen(s)); }

static void read_winsize(struct bridge *b)
{
    struct winsize ws;
    if (ioctl(STDOUT_FILENO, TIOCGWINSZ, &ws) == 0 && ws.ws_col && ws.ws_row) {
        b->cols = ws.ws_col;
        b->rows = ws.ws_row;
        if (!b->pane_w && ws.ws_xpixel && ws.ws_ypixel) {
            b->pane_w = ws.ws_xpixel;
            b->pane_h = ws.ws_ypixel;
        }
    }
}

static void place_picture(struct bridge *b)
{
    char seq[128];
    int n;
    int cols = b->cols, rows = b->rows;
    if (cols <= 0 || rows <= 0) return;
    /* The picture keeps its aspect: the largest whole-cell box of the
       stream's shape inside the pane. Blit scales it; the bridge never
       resamples. */
    if (b->stream_w && b->stream_h && b->pane_w > 0 && b->pane_h > 0) {
        double cw = (double)b->pane_w / cols, ch = (double)b->pane_h / rows;
        double sx = (double)b->pane_w / b->stream_w, sy = (double)b->pane_h / b->stream_h;
        double scale = sx < sy ? sx : sy;
        int c = (int)(b->stream_w * scale / cw + 0.5), r = (int)(b->stream_h * scale / ch + 0.5);
        if (c >= 1 && c <= cols) cols = c;
        if (r >= 1 && r <= rows) rows = r;
    }
    n = gs_frames_place(seq, sizeof(seq), GS_IMAGE_ID, cols, rows);
    if (n > 0) tty_write(seq, (size_t)n);
    b->placed = 1;
}

/* ---------------------------------------------------------------- terminal */

static void tty_setup(struct bridge *b)
{
    struct termios raw;
    if (tcgetattr(STDIN_FILENO, &b->saved_termios) == 0) {
        b->termios_saved = 1;
        raw = b->saved_termios;
        cfmakeraw(&raw);
        raw.c_oflag |= OPOST | ONLCR;
        tcsetattr(STDIN_FILENO, TCSANOW, &raw);
    }
    /* The cursor hidden; the click reports (1000, SGR 1006) only to see the
       click that captures the mouse; focus reports (1004). */
    tty_puts("\033[?25l\033[2J\033[?1000h\033[?1006h\033[?1004h");
}

static void tty_restore(struct bridge *b)
{
    char seq[64];
    int n;
    if (b->tty.captured) tty_puts("\033[?7701l");
    b->tty.captured = 0;
    if (b->placed) {
        n = gs_frames_delete(seq, sizeof(seq), GS_IMAGE_ID);
        if (n > 0) tty_write(seq, (size_t)n);
        b->placed = 0;
    }
    tty_puts("\033[?1004l\033[?1006l\033[?1000l\033[?25h");
    if (b->termios_saved) {
        tcsetattr(STDIN_FILENO, TCSANOW, &b->saved_termios);
        b->termios_saved = 0;
    }
}

static void on_tty_event(void *context, int event, int a, int c)
{
    struct bridge *b = context;
    switch (event) {
    case GS_TTY_SIZE:
        b->pane_w = a;
        b->pane_h = c;
        break;
    case GS_TTY_CAPTURE:
        /* A click in the pane asks for the mouse (ADR 0198); Blit answers
           ?7701;2$y if it will not lock, and ends the lock on its own. */
        tty_puts("\033[?7701h");
        logf_(b, "click at %d,%d: pointer lock requested", a, c);
        break;
    case GS_TTY_LOCK_ENDED:
        logf_(b, "pointer lock ended by Blit");
        break;
    case GS_TTY_FOCUS_OUT:
        logf_(b, "focus out");
        break;
    default:
        break;
    }
}

/* Before gamescope starts: the pane's size in pixels (CSI 14 t), waited for
   up to two seconds. */
static void query_size(struct bridge *b)
{
    struct timespec start, now;
    tty_puts("\033[14t");
    clock_gettime(CLOCK_MONOTONIC, &start);
    b->pane_w = b->pane_h = 0;
    for (;;) {
        struct pollfd p = { STDIN_FILENO, POLLIN, 0 };
        unsigned char buf[256];
        long left;
        clock_gettime(CLOCK_MONOTONIC, &now);
        left = 2000 - ((now.tv_sec - start.tv_sec) * 1000 + (now.tv_nsec - start.tv_nsec) / 1000000);
        if (b->pane_w > 0 || left <= 0) break;
        if (poll(&p, 1, (int)left) > 0) {
            ssize_t n = read(STDIN_FILENO, buf, sizeof(buf));
            if (n <= 0) break;
            gs_tty_feed(&b->tty, buf, (size_t)n, on_tty_event, b);
        }
    }
    read_winsize(b);
}

/* ---------------------------------------------------------------- gamescope */

static int spawn_gamescope(struct bridge *b)
{
    char w[16], h[16], r[16];
    char *argv[64];
    int argc = 0, i, pipefd[2];
    snprintf(w, sizeof(w), "%d", b->width);
    snprintf(h, sizeof(h), "%d", b->height);
    snprintf(r, sizeof(r), "%d", b->refresh);
    argv[argc++] = "gamescope";
    argv[argc++] = "--backend";
    argv[argc++] = "headless";
    argv[argc++] = "-W"; argv[argc++] = w;
    argv[argc++] = "-H"; argv[argc++] = h;
    argv[argc++] = "-r"; argv[argc++] = r;
    if (b->steam) argv[argc++] = "-e";
    argv[argc++] = "--";
    for (i = 0; b->command[i] && argc < 63; ++i) argv[argc++] = b->command[i];
    argv[argc] = NULL;
    if (pipe2(pipefd, O_CLOEXEC) != 0) return -1;
    b->child = fork();
    if (b->child < 0) return -1;
    if (b->child == 0) {
        sigset_t none;
        int null = open("/dev/null", O_RDONLY);
        const char *path = getenv("PATH");
        char newpath[4096];
        setpgid(0, 0);
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGPIPE, SIG_DFL);
        /* Never the terminal: the pane's keys are not gamescope's to read,
           and its log is not the pane's to show. */
        if (null >= 0) dup2(null, STDIN_FILENO);
        dup2(pipefd[1], STDOUT_FILENO);
        dup2(pipefd[1], STDERR_FILENO);
        if (!path || !strstr(path, "/usr/games")) {
            snprintf(newpath, sizeof(newpath), "%s%s/usr/games", path ? path : "/usr/bin:/bin",
                     path ? ":" : "");
            setenv("PATH", newpath, 1);
        }
        /* The bridge's channel is not the game's. */
        unsetenv("BLIT_FRAME_SOCKET");
        unsetenv("BLIT_FRAME_TOKEN");
        execvp("gamescope", argv);
        fprintf(stderr, "blit-gamescope: gamescope: %s\n", strerror(errno));
        _exit(127);
    }
    setpgid(b->child, b->child);
    close(pipefd[1]);
    b->child_err = pipefd[0];
    fcntl(b->child_err, F_SETFL, O_NONBLOCK);
    logf_(b, "gamescope pid %d: -W %d -H %d -r %d%s", (int)b->child, b->width, b->height,
          b->refresh, b->steam ? " -e" : "");
    return 0;
}

static void maybe_send_input(struct bridge *b)
{
    struct stat st;
    if (b->frames.input_state != GS_INPUT_NONE || !b->eis_path[0] || b->frames.fd < 0) return;
    if (stat(b->eis_path, &st) != 0 || !S_ISSOCK(st.st_mode)) return;
    if (gs_frames_send_input(&b->frames, b->eis_path, (uint32_t)b->width, (uint32_t)b->height) == 0)
        logf_(b, "INPUT eis %s %d %d sent", b->eis_path, b->width, b->height);
}

static void child_line(struct bridge *b, const char *line)
{
    const char *p;
    if (b->log) logf_(b, "gamescope: %s", line);
    if ((p = strstr(line, "wayland display '")) != NULL) {
        size_t n = strcspn(p + 17, "'");
        if (n < sizeof(b->display)) {
            const char *rt = getenv("XDG_RUNTIME_DIR");
            memcpy(b->display, p + 17, n);
            b->display[n] = '\0';
            if (rt) snprintf(b->eis_path, sizeof(b->eis_path), "%s/%s-ei", rt, b->display);
        }
    }
    if (strstr(line, "Successfully initialized libei")) {
        b->eis_ready = 1;
        maybe_send_input(b);
    }
}

static void on_child_output(void *data, int fd, uint32_t mask)
{
    struct bridge *b = data;
    char buf[4096];
    ssize_t n;
    (void)fd;
    (void)mask;
    while ((n = read(b->child_err, buf, sizeof(buf))) > 0) {
        ssize_t i;
        for (i = 0; i < n; ++i) {
            if (buf[i] == '\n' || b->err_length == sizeof(b->err_line) - 1) {
                b->err_line[b->err_length] = '\0';
                child_line(b, b->err_line);
                b->err_length = 0;
            } else {
                b->err_line[b->err_length++] = buf[i];
            }
        }
    }
}

static void end_children(struct bridge *b)
{
    int i;
    if (b->child <= 0) return;
    kill(-b->child, SIGTERM);
    for (i = 0; i < 50; ++i) {
        if (!b->child_exited && waitpid(b->child, &b->child_status, WNOHANG) == b->child)
            b->child_exited = 1;
        /* The group is gone when nothing in it can be signalled. */
        if (b->child_exited && kill(-b->child, 0) != 0) return;
        usleep(100000);
    }
    kill(-b->child, SIGKILL);
    if (!b->child_exited && waitpid(b->child, &b->child_status, 0) == b->child)
        b->child_exited = 1;
}

/* ---------------------------------------------------------------- PipeWire */

static const struct spa_pod *enum_format(struct bridge *b, struct spa_pod_builder *bld, int dmabuf)
{
    struct spa_pod_frame f;
    struct spa_rectangle def = SPA_RECTANGLE(b->width, b->height);
    struct spa_rectangle min = SPA_RECTANGLE(1, 1), max = SPA_RECTANGLE(8192, 8192);
    struct spa_fraction fdef = SPA_FRACTION(0, 1), fmin = SPA_FRACTION(0, 1), fmax = SPA_FRACTION(1000, 1);
    spa_pod_builder_push_object(bld, &f, SPA_TYPE_OBJECT_Format, SPA_PARAM_EnumFormat);
    spa_pod_builder_add(bld,
        SPA_FORMAT_mediaType, SPA_POD_Id(SPA_MEDIA_TYPE_video),
        SPA_FORMAT_mediaSubtype, SPA_POD_Id(SPA_MEDIA_SUBTYPE_raw),
        SPA_FORMAT_VIDEO_format, SPA_POD_Id(SPA_VIDEO_FORMAT_BGRx),
        SPA_FORMAT_VIDEO_size, SPA_POD_CHOICE_RANGE_Rectangle(&def, &min, &max),
        SPA_FORMAT_VIDEO_framerate, SPA_POD_CHOICE_RANGE_Fraction(&fdef, &fmin, &fmax),
        0);
    if (b->requested_w > 0) {
        struct spa_rectangle req = SPA_RECTANGLE(b->requested_w, b->requested_h);
        spa_pod_builder_add(bld, GS_SPA_FORMAT_VIDEO_requested_size, SPA_POD_Rectangle(&req), 0);
    }
    if (dmabuf) {
        spa_pod_builder_prop(bld, SPA_FORMAT_VIDEO_modifier, SPA_POD_PROP_FLAG_MANDATORY);
        spa_pod_builder_long(bld, 0); /* DRM_FORMAT_MOD_LINEAR */
    }
    return spa_pod_builder_pop(bld, &f);
}

static void requeue_cb(void *context, void *cookie)
{
    struct bridge *b = context;
    if (b->stream && cookie) pw_stream_queue_buffer(b->stream, (struct pw_buffer *)cookie);
}

/* A dmabuf that Blit will not import (FRM3 refused ENOTSUP): read on the
   CPU. It is LINEAR, so a plain mapping reads it in order. */
static int send_mapped_dmabuf(struct bridge *b, struct spa_data *d)
{
    size_t size = (size_t)d->maxsize;
    void *map;
    int rc;
    if (d->fd < 0 || size == 0) return -1;
    map = mmap(NULL, size, PROT_READ, MAP_SHARED, (int)d->fd, d->mapoffset);
    if (map == MAP_FAILED) return -1;
    rc = gs_frames_send_pixels(&b->frames, (const unsigned char *)map + d->chunk->offset,
                               b->stream_w, b->stream_h,
                               d->chunk->stride ? (uint32_t)d->chunk->stride : b->stream_w * 4);
    munmap(map, size);
    return rc;
}

static void on_process(void *data)
{
    struct bridge *b = data;
    struct pw_buffer *buf, *newest = NULL;
    struct spa_data *d;
    /* Newest wins: older buffers go straight back. */
    while ((buf = pw_stream_dequeue_buffer(b->stream)) != NULL) {
        if (newest) {
            pw_stream_queue_buffer(b->stream, newest);
            ++b->frames_dropped;
        }
        newest = buf;
    }
    if (!newest) return;
    ++b->frames_in;
    d = &newest->buffer->datas[0];
    /* A buffer from before a size change (the stream renegotiates on a
       resize) does not hold a frame of the new size: dropped, not sent. */
    if (d->chunk && b->stream_w &&
        (uint64_t)d->chunk->offset + (uint64_t)(d->chunk->stride ? d->chunk->stride : b->stream_w * 4) *
            (b->stream_h - 1) + (uint64_t)b->stream_w * 4 > d->maxsize) {
        ++b->frames_dropped;
        pw_stream_queue_buffer(b->stream, newest);
        return;
    }
    if (!d->chunk || d->chunk->size == 0 || b->frames.fd < 0 || !b->stream_w) {
        pw_stream_queue_buffer(b->stream, newest);
        return;
    }
    if (d->type == SPA_DATA_DmaBuf) {
        if (gs_frames_use_dmabuf(&b->frames)) {
            if (gs_frames_send_dmabuf(&b->frames, (int)d->fd, b->stream_w, b->stream_h,
                                      d->chunk->offset,
                                      d->chunk->stride ? (uint32_t)d->chunk->stride : b->stream_w * 4,
                                      newest) == 0) {
                if (!b->placed) place_picture(b); /* once, after the first frame */
                return; /* held until RELEASE or ERROR */
            }
        } else {
            send_mapped_dmabuf(b, d);
        }
    } else if (d->data) {
        gs_frames_send_pixels(&b->frames, (const unsigned char *)d->data + d->chunk->offset,
                              b->stream_w, b->stream_h,
                              d->chunk->stride ? (uint32_t)d->chunk->stride : b->stream_w * 4);
    }
    if (!b->placed && b->frames.sequence > 0) place_picture(b);
    pw_stream_queue_buffer(b->stream, newest);
}

static void on_param_changed(void *data, uint32_t id, const struct spa_pod *param)
{
    struct bridge *b = data;
    struct spa_video_info_raw info;
    uint8_t buffer[512];
    struct spa_pod_builder bld = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    int types;
    if (id != SPA_PARAM_Format || !param) return;
    memset(&info, 0, sizeof(info));
    if (spa_format_video_raw_parse(param, &info) < 0) return;
    b->stream_w = info.size.width;
    b->stream_h = info.size.height;
    b->stream_dmabuf = spa_pod_find_prop(param, NULL, SPA_FORMAT_VIDEO_modifier) != NULL;
    if (b->placed) place_picture(b); /* a new size keeps its aspect in the pane */
    logf_(b, "stream format %ux%u %s", b->stream_w, b->stream_h,
          b->stream_dmabuf ? "dmabuf LINEAR" : "memfd");
    types = b->stream_dmabuf ? (1 << SPA_DATA_DmaBuf) : ((1 << SPA_DATA_MemFd) | (1 << SPA_DATA_MemPtr));
    params[0] = spa_pod_builder_add_object(&bld,
        SPA_TYPE_OBJECT_ParamBuffers, SPA_PARAM_Buffers,
        SPA_PARAM_BUFFERS_buffers, SPA_POD_CHOICE_RANGE_Int(GS_BUFFERS, GS_BUFFERS, 8),
        SPA_PARAM_BUFFERS_blocks, SPA_POD_Int(1),
        SPA_PARAM_BUFFERS_dataType, SPA_POD_CHOICE_FLAGS_Int(types));
    pw_stream_update_params(b->stream, params, 1);
}

static void on_state(void *data, enum pw_stream_state old, enum pw_stream_state state, const char *error)
{
    struct bridge *b = data;
    logf_(b, "stream %s -> %s%s%s", pw_stream_state_as_string(old), pw_stream_state_as_string(state),
          error ? ": " : "", error ? error : "");
    (void)old;
}

static void on_remove_buffer(void *data, struct pw_buffer *buffer)
{
    struct bridge *b = data;
    int i;
    /* A buffer the stream takes away is not ours to give back later. */
    for (i = 0; i < GS_HOLD_MAX; ++i)
        if (b->frames.held[i].used && b->frames.held[i].cookie == buffer) {
            b->frames.held[i].used = 0;
            b->frames.held[i].cookie = NULL;
        }
}

static const struct pw_stream_events stream_events = {
    PW_VERSION_STREAM_EVENTS,
    .state_changed = on_state,
    .param_changed = on_param_changed,
    .remove_buffer = on_remove_buffer,
    .process = on_process,
};

static void connect_stream(struct bridge *b)
{
    uint8_t buffer[1024];
    struct spa_pod_builder bld = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    char serial[32];
    int dmabuf = gs_frames_use_dmabuf(&b->frames);
    snprintf(serial, sizeof(serial), "%u", b->target_serial);
    b->stream = pw_stream_new(b->core, "blit-gamescope",
        pw_properties_new(PW_KEY_MEDIA_TYPE, "Video", PW_KEY_MEDIA_CATEGORY, "Capture",
                          PW_KEY_MEDIA_ROLE, "Screen", PW_KEY_TARGET_OBJECT, serial,
                          PW_KEY_NODE_DONT_RECONNECT, "true", NULL));
    pw_stream_add_listener(b->stream, &b->stream_listener, &stream_events, b);
    params[0] = enum_format(b, &bld, dmabuf);
    if (pw_stream_connect(b->stream, PW_DIRECTION_INPUT, PW_ID_ANY,
                          PW_STREAM_FLAG_AUTOCONNECT | PW_STREAM_FLAG_MAP_BUFFERS, params, 1) < 0)
        logf_(b, "pw_stream_connect failed");
    else
        logf_(b, "stream to node serial %s (%s)", serial, dmabuf ? "dmabuf" : "memfd");
}

static void on_global(void *data, uint32_t id, uint32_t permissions, const char *type,
                      uint32_t version, const struct spa_dict *props)
{
    struct bridge *b = data;
    const char *name, *klass, *pid, *serial;
    (void)permissions;
    (void)version;
    if (!props || strcmp(type, PW_TYPE_INTERFACE_Node) != 0) return;
    name = spa_dict_lookup(props, PW_KEY_NODE_NAME);
    klass = spa_dict_lookup(props, PW_KEY_MEDIA_CLASS);
    pid = spa_dict_lookup(props, PW_KEY_APP_PROCESS_ID);
    serial = spa_dict_lookup(props, PW_KEY_OBJECT_SERIAL);
    if (!name || strcmp(name, "gamescope") != 0 || !klass || strcmp(klass, "Video/Source") != 0)
        return;
    /* Ours only: a node that is another gamescope's (by its pid, or one that
       was there before ours started) is never taken. */
    if (pid && atoi(pid) != (int)b->child) return;
    if (!pid && !b->synced) return;
    if (b->stream) return;
    b->target_serial = serial ? (uint32_t)strtoul(serial, NULL, 10) : id;
    logf_(b, "gamescope node %u (serial %u)", id, b->target_serial);
    connect_stream(b);
}

static const struct pw_registry_events registry_events = {
    PW_VERSION_REGISTRY_EVENTS,
    .global = on_global,
};

static void on_core_done(void *data, uint32_t id, int seq)
{
    struct bridge *b = data;
    if (id == PW_ID_CORE && seq == b->initial_sync) b->synced = 1;
}

static const struct pw_core_events core_events = {
    PW_VERSION_CORE_EVENTS,
    .done = on_core_done,
};

/* ---------------------------------------------------------------- loop */

static void request_resize(struct bridge *b)
{
    uint8_t buffer[1024];
    struct spa_pod_builder bld = SPA_POD_BUILDER_INIT(buffer, sizeof(buffer));
    const struct spa_pod *params[1];
    read_winsize(b);
    if (b->placed) place_picture(b);
    if (!b->stream || b->pane_w <= 0 || b->pane_h <= 0) return;
    if (b->pane_w == (int)b->stream_w && b->pane_h == (int)b->stream_h) return;
    /* gamescope honours requested_size only below its output size
       (calculate_capture_size in its src/pipewire.cpp scales down, never
       up). A larger pane keeps the picture, scaled by Blit to the
       placement; growing gamescope means restarting it with new -W/-H,
       which would end Steam and the game, so it is never done (ADR 0225). */
    if (b->pane_w > b->width || b->pane_h > b->height) {
        logf_(b, "resize: %dx%d is larger than gamescope's %dx%d; Blit scales the picture",
              b->pane_w, b->pane_h, b->width, b->height);
        if (b->requested_w <= 0) return;
        b->requested_w = b->width; /* back to full size */
        b->requested_h = b->height;
    } else {
        b->requested_w = b->pane_w;
        b->requested_h = b->pane_h;
    }
    params[0] = enum_format(b, &bld, b->stream_dmabuf);
    pw_stream_update_params(b->stream, params, 1);
    logf_(b, "resize: requested %dx%d from the stream", b->requested_w, b->requested_h);
}

static void on_stdin(void *data, int fd, uint32_t mask)
{
    struct bridge *b = data;
    unsigned char buf[1024];
    ssize_t n = read(fd, buf, sizeof(buf));
    int pane_w = b->pane_w, pane_h = b->pane_h;
    (void)mask;
    if (n > 0) {
        gs_tty_feed(&b->tty, buf, (size_t)n, on_tty_event, b);
        if (b->pane_w != pane_w || b->pane_h != pane_h) request_resize(b);
    } else if (n == 0 || (errno != EAGAIN && errno != EINTR)) {
        logf_(b, "terminal closed");
        b->exit_code = 129;
        pw_main_loop_quit(b->loop);
    }
}

static void on_frame_socket(void *data, int fd, uint32_t mask)
{
    struct bridge *b = data;
    int input = b->frames.input_state;
    (void)fd;
    (void)mask;
    if (gs_frames_read(&b->frames) != 0) {
        logf_(b, "frame channel closed by Blit");
        b->exit_code = 1;
        pw_main_loop_quit(b->loop);
        return;
    }
    if (input != b->frames.input_state) {
        if (b->frames.input_state == GS_INPUT_OK)
            logf_(b, "INPUT OK: Blit sends the pane's input to %s", b->eis_path);
        else
            logf_(b, "INPUT %s: this Blit does not send input to gamescope; frames only",
                  b->frames.input_error);
    }
    if (b->frames.dmabuf_refused && b->frames.errors == 1)
        logf_(b, "FRM3 refused (%s): frames by memfd from now on", b->frames.last_error);
}

static void on_signal(void *data, int signal_number)
{
    struct bridge *b = data;
    switch (signal_number) {
    case SIGCHLD:
        if (!b->child_exited && waitpid(b->child, &b->child_status, WNOHANG) == b->child) {
            b->child_exited = 1;
            b->exit_code = WIFEXITED(b->child_status) ? WEXITSTATUS(b->child_status)
                                                       : 128 + WTERMSIG(b->child_status);
            logf_(b, "gamescope exited: %d", b->exit_code);
            pw_main_loop_quit(b->loop);
        }
        break;
    case SIGWINCH:
        tty_puts("\033[14t"); /* the answer arrives on stdin and resizes */
        read_winsize(b);
        if (b->placed) place_picture(b);
        break;
    default:
        logf_(b, "signal %d", signal_number);
        b->exit_code = 128 + signal_number;
        pw_main_loop_quit(b->loop);
        break;
    }
}

static void on_tick(void *data, uint64_t expirations)
{
    struct bridge *b = data;
    (void)expirations;
    maybe_send_input(b);
}

static void usage(void)
{
    fprintf(stderr, "usage: blit-gamescope [-W width -H height] [-r hz] [--steam] [--log path] -- command...\n");
}

int main(int argc, char **argv)
{
    struct bridge bridge, *b = &bridge;
    const char *socket_name = getenv("BLIT_FRAME_SOCKET");
    const char *token = getenv("BLIT_FRAME_TOKEN");
    struct pw_loop *loop;
    struct spa_source *timer;
    struct timespec interval = { 0, 250000000 };
    int i;

    memset(b, 0, sizeof(*b));
    g_bridge = b;
    b->refresh = 60;
    b->child_err = -1;
    for (i = 1; i < argc; ++i) {
        if (!strcmp(argv[i], "--")) { b->command = argv + i + 1; break; }
        else if (!strcmp(argv[i], "-W") && i + 1 < argc) b->width = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-H") && i + 1 < argc) b->height = atoi(argv[++i]);
        else if (!strcmp(argv[i], "-r") && i + 1 < argc) b->refresh = atoi(argv[++i]);
        else if (!strcmp(argv[i], "--steam")) b->steam = 1;
        else if (!strcmp(argv[i], "--log") && i + 1 < argc) b->log_path = argv[++i];
        else if (!strcmp(argv[i], "--version")) { printf("blit-gamescope %s\n", BLIT_BUILD_VERSION); return 0; }
        else { usage(); return 2; }
    }
    if (!b->command || !b->command[0] || b->refresh <= 0) { usage(); return 2; }
    if (!socket_name || !token) {
        fprintf(stderr, "blit-gamescope: no BLIT_FRAME_SOCKET: run it in a local Blit session\n");
        return 2;
    }
    if (b->log_path) b->log = fopen(b->log_path, "a");
    gs_frames_init(&b->frames, GS_IMAGE_ID, requeue_cb, b);
    if (gs_frames_connect(&b->frames, socket_name, token) != 0) {
        fprintf(stderr, "blit-gamescope: frame channel: %s\n", b->frames.last_error);
        return 1;
    }
    logf_(b, "blit-gamescope %s: frame channel dmabuf=%d formats=%s", BLIT_BUILD_VERSION,
          b->frames.dmabuf, b->frames.formats);

    {   /* Signals are taken by the loop; block them before any child. */
        sigset_t set;
        sigemptyset(&set);
        sigaddset(&set, SIGCHLD); sigaddset(&set, SIGWINCH); sigaddset(&set, SIGTERM);
        sigaddset(&set, SIGINT); sigaddset(&set, SIGHUP); sigaddset(&set, SIGQUIT);
        sigprocmask(SIG_BLOCK, &set, NULL);
        signal(SIGPIPE, SIG_IGN);
    }
    tty_setup(b);
    if (b->width > 0 && b->height > 0) {
        b->sized_by_option = 1;
        read_winsize(b);
    } else {
        query_size(b);
        b->width = b->pane_w;
        b->height = b->pane_h;
    }
    if (b->width <= 0 || b->height <= 0 || b->width > 4096 || b->height > 4096) {
        tty_restore(b);
        fprintf(stderr, "blit-gamescope: the pane's size in pixels is unknown (%dx%d); give -W and -H\n",
                b->width, b->height);
        return 1;
    }

    pw_init(&argc, &argv);
    b->loop = pw_main_loop_new(NULL);
    loop = pw_main_loop_get_loop(b->loop);
    b->context = pw_context_new(loop, NULL, 0);
    b->core = b->context ? pw_context_connect(b->context, NULL, 0) : NULL;
    if (!b->core) {
        tty_restore(b);
        fprintf(stderr, "blit-gamescope: cannot connect to PipeWire\n");
        return 1;
    }
    pw_core_add_listener(b->core, &b->core_listener, &core_events, b);
    if (spawn_gamescope(b) != 0) {
        tty_restore(b);
        fprintf(stderr, "blit-gamescope: cannot start gamescope: %s\n", strerror(errno));
        return 1;
    }
    b->registry = pw_core_get_registry(b->core, PW_VERSION_REGISTRY, 0);
    pw_registry_add_listener(b->registry, &b->registry_listener, &registry_events, b);
    b->initial_sync = pw_core_sync(b->core, PW_ID_CORE, 0);

    fcntl(STDIN_FILENO, F_SETFL, fcntl(STDIN_FILENO, F_GETFL) | O_NONBLOCK);
    pw_loop_add_io(loop, STDIN_FILENO, SPA_IO_IN | SPA_IO_HUP, false, on_stdin, b);
    pw_loop_add_io(loop, b->frames.fd, SPA_IO_IN | SPA_IO_HUP, false, on_frame_socket, b);
    pw_loop_add_io(loop, b->child_err, SPA_IO_IN | SPA_IO_HUP, false, on_child_output, b);
    pw_loop_add_signal(loop, SIGCHLD, on_signal, b);
    pw_loop_add_signal(loop, SIGWINCH, on_signal, b);
    pw_loop_add_signal(loop, SIGTERM, on_signal, b);
    pw_loop_add_signal(loop, SIGINT, on_signal, b);
    pw_loop_add_signal(loop, SIGHUP, on_signal, b);
    pw_loop_add_signal(loop, SIGQUIT, on_signal, b);
    timer = pw_loop_add_timer(loop, on_tick, b);
    pw_loop_update_timer(loop, timer, &interval, &interval, false);

    pw_main_loop_run(b->loop);

    logf_(b, "exit: frames in %lu (dropped %lu), FRM3 %lu, FRM1 %lu, released %lu, errors %lu, requeued %lu",
          b->frames_in, b->frames_dropped, b->frames.sent_dmabuf, b->frames.sent_memfd,
          b->frames.released, b->frames.errors, b->frames.requeued);
    /* Blit keeps nothing of ours once the picture is deleted (a=d releases
       what the image holds), so the picture goes first, then the stream. */
    tty_restore(b);
    if (b->stream) {
        pw_stream_disconnect(b->stream);
        pw_stream_destroy(b->stream);
        b->stream = NULL;
    }
    end_children(b);
    if (b->child_exited && b->exit_code == 0)
        b->exit_code = WIFEXITED(b->child_status) ? WEXITSTATUS(b->child_status)
                                                   : 128 + WTERMSIG(b->child_status);
    if (b->frames.fd >= 0) close(b->frames.fd);
    pw_core_disconnect(b->core);
    pw_context_destroy(b->context);
    pw_main_loop_destroy(b->loop);
    pw_deinit();
    if (b->log) fclose(b->log);
    return b->exit_code;
}
