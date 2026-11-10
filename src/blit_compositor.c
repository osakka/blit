/* blit-compositor: Blit's Wayland compositor for programs (ADR 0228,
   phase 1, milestones A, B and C).

     blit-compositor [--log path]

   One per desktop session, started by the desktop with the
   BLIT_FRAME_SOCKET/BLIT_FRAME_TOKEN of the frame channel it serves. It
   connects there as a version 4 producer (blit_compositor_wire.h), listens
   on $XDG_RUNTIME_DIR/blit-<n> (the first free n) and says that name with
   `WINDOW display <name>`; the desktop puts it in its programs'
   environment as WAYLAND_DISPLAY.

   It composites nothing. Each xdg_toplevel and each xdg_popup is one
   picture by descriptor whose id is a WINDOW id (docs/api.md, "Program
   windows"): the compositor tells the desktop about the window and its
   hints, the desktop answers `WINDOW size`, and the compositor configures
   the client at exactly that size. A committed wl_shm buffer becomes one
   FRM1 frame of that picture, copied, and is released at once; a dmabuf
   one becomes one FRM3 of its own planes, released at Blit's RELEASE (the
   dmabuf section). Frame callbacks and presentation feedback follow when
   Blit has taken the frame, at most once per output refresh (the frame
   pacing section). Input comes straight from Blit to the compositor's
   input socket as lines and becomes wl_seat events (the input section).

   Globals: wl_compositor, wl_subcompositor (subsurfaces accepted and
   logged, not composited), wl_shm (ARGB8888, XRGB8888, libwayland's own),
   wl_output (one global per display mode the desktop names with `WINDOW
   output`; a window is on the first, 1920x1080 at 60 Hz, until then, and
   its surface enters and leaves them as the desktop moves it),
   xdg_wm_base, zxdg_decoration_manager_v1 (server-side: the desktop draws
   frames; a client that insists on its own gets `WINDOW decor <id>
   client`), wl_seat (keyboard with Blit's xkb keymap, pointer),
   zwp_linux_dmabuf_v1 (only for a peer that imports: dmabuf=1),
   wp_presentation, zwp_relative_pointer_manager_v1 and
   zwp_pointer_constraints_v1 (asked of the desktop, `WINDOW lock`). Each
   is offered at the version the task names or the one the build host's
   protocol XML carries, whichever is lower (BC_VERSION).

   Against a peer that answers the hello without window=1 (Blit itself) it
   keeps running, shows nothing and says so in its log: no WINDOW line, no
   frame, and so no configure, which waits for the desktop's size.

   Built with tools/build-tcc.sh compositor, linked against
   libwayland-server.so.0 and libxkbcommon.so.0. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <netinet/in.h>
#include <arpa/inet.h>
#include <linux/dma-buf.h>
#include <xkbcommon/xkbcommon.h>

#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
#include "linux-dmabuf-unstable-v1-server-protocol.h"
#include "presentation-time-server-protocol.h"
#include "relative-pointer-unstable-v1-server-protocol.h"
#include "pointer-constraints-unstable-v1-server-protocol.h"
#include "blit_compositor_wire.h"

#ifndef BLIT_BUILD_VERSION
#define BLIT_BUILD_VERSION "dev"
#endif

/* The version offered: the one ADR 0228 names, bounded by the generated
   interface (the build host's XML). */
#define BC_VERSION(want, iface) ((want) < (iface).version ? (want) : (iface).version)
#define BC_SOCKETS 32

enum { BC_ROLE_NONE = 0, BC_ROLE_XDG, BC_ROLE_SUBSURFACE };

struct bc_server;
struct bc_xdg;
struct bc_constraint;

/* One wl_output global per display mode the desktop has named with
   `WINDOW output` (the line carries the mode, not the display, so two
   displays with one mode are one output). An output no window is on is
   given the next new mode, so globals never come and go. */
struct bc_output {
    struct wl_list link;              /* bc_server.outputs */
    struct wl_global *global;
    struct wl_list resources;         /* wl_output resources */
    int32_t width, height;
    uint32_t refresh_mhz;
    int windows;                      /* windows on it */
    int number;                       /* its name, BLIT-<number>, from 1 */
};

struct bc_surface {
    struct wl_resource *resource;
    struct bc_server *server;
    int role;
    struct bc_xdg *xdg;               /* while it has an xdg_surface */
    /* pending state, applied at commit */
    struct wl_resource *pending_buffer;
    int pending_attach;               /* attach seen since the last commit */
    struct wl_listener pending_buffer_destroy;
    struct wl_list pending_frames;    /* wl_callback resources */
    struct wl_list frames;            /* committed, waiting for Blit to take the frame */
    struct wl_list due;               /* taken, waiting for the output's next refresh */
    struct wl_list pending_feedback;  /* wp_presentation_feedback resources */
    struct wl_list feedback;          /* committed, waiting for Blit to take the frame */
    struct wl_event_source *pace;     /* the refresh timer for `due` */
    uint64_t last_done_us;            /* when frame callbacks were last answered */
    int scale_warned;
    struct bc_constraint *constraint; /* its pointer lock or confinement, one at most */
};

struct bc_positioner {
    int32_t width, height;
    int32_t ax, ay, aw, ah;
    uint32_t anchor, gravity;
    int32_t dx, dy;
};

struct bc_xdg {
    struct wl_resource *resource;     /* xdg_surface */
    struct bc_server *server;
    struct bc_surface *surface;       /* NULL once the wl_surface is gone */
    struct wl_resource *toplevel;
    struct wl_resource *popup;
    struct wl_resource *decoration;
    struct wl_list link;              /* bc_server.windows */
    uint32_t id;                      /* picture id, 0 until announced */
    int initial;                      /* the initial commit was seen */
    int acked;                        /* a configure was acknowledged */
    int configured;                   /* a configure was sent */
    int mapped;                       /* a buffer is committed */
    uint32_t serial;
    /* window geometry, double-buffered */
    int32_t gx, gy, gw, gh, pgx, pgy, pgw, pgh;
    int pending_geometry;
    /* toplevel */
    struct bc_xdg *parent;
    char title[BC_TEXT_BYTES + 1], appid[BC_TEXT_BYTES + 1];
    int32_t min_w, min_h, max_w, max_h;           /* committed */
    int32_t pmin_w, pmin_h, pmax_w, pmax_h;       /* pending */
    int limits_sent;
    int fullscreen;                   /* the client's own request */
    int maximize;                     /* the client's own request */
    int minimize;                     /* asked before WINDOW new */
    uint32_t decor_mode;              /* 0 none (client draws), else zxdg mode */
    int decor_dropped;                /* the decoration object went: the
                                         client draws from its next commit */
    int have_size;
    int32_t size_w, size_h;
    int size_fullscreen, size_maximized, size_activated;
    struct bc_output *output;         /* the display it is on */
    uint64_t last_copy;               /* the sequence of its newest FRM1 */
    int32_t buffer_w, buffer_h;       /* its committed buffer's size */
    /* popup */
    struct bc_positioner positioner;
    int32_t px, py;                   /* relative to the parent's geometry */
};

struct bc_server {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wl_event_source *frame_source, *sigterm_source, *sigint_source;
    BcWire wire;
    char socket_name[32];
    struct wl_list windows;           /* bc_xdg */
    struct wl_list outputs;           /* bc_output */
    struct wl_list holds;             /* bc_hold: FRM3 frames Blit holds */
    int format_table_fd;              /* linux-dmabuf v4's format table */
    dev_t main_device;
    /* input (the seat section) */
    char input_path[108];             /* the input socket, "" without one */
    int input_fd;
    struct wl_event_source *input_source;
    struct wl_list input_conns;       /* bc_input_conn: Blit's routes */
    struct wl_list keyboards, pointers;   /* wl_keyboard, wl_pointer resources */
    struct wl_list relative_pointers; /* zwp_relative_pointer_v1 resources */
    struct wl_list constraints;       /* bc_constraint: pointer locks and confinements */
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    int keymap_fd;
    uint32_t keymap_size;
    char keymap_etag[96];
    int keymap_warned;
    uint64_t keymap_revision;         /* the last `INPUT keymap` revision read for */
    int keymap_stale;                 /* a notice's read failed: read again at the next enter */
    int32_t repeat_rate, repeat_delay;    /* Blit's, from the keymap's headers; 0 0 none */
    uint32_t mods[4];
    int mods_sent;
    unsigned char keys_down[96];      /* evdev codes down, through KEY_MAX */
    struct bc_surface *kbd_focus, *ptr_focus;
    double ptr_x, ptr_y;
    uint32_t buttons_down;            /* BTN_LEFT + n */
    int pointer_batch;                /* a pointer event since the last frame */
    FILE *log;
    int status;
};

static struct bc_server *g_server;

static void keyboard_send_keymap(struct bc_server *s, struct wl_resource *r);
static void keyboard_send_modifiers(struct bc_server *s, int force);
static void keyboard_focus(struct bc_server *s, struct bc_surface *surface);
static void constraints_window_gone(struct bc_xdg *x);
static void constraint_ask_surface(struct bc_surface *surface);
static void constraint_surface_gone(struct bc_surface *surface);

static void logf_(const char *fmt, ...)
{
    FILE *out = g_server && g_server->log ? g_server->log : stderr;
    struct timespec ts;
    va_list ap;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    fprintf(out, "blit-compositor %ld.%03ld ", (long)ts.tv_sec, ts.tv_nsec / 1000000);
    va_start(ap, fmt);
    vfprintf(out, fmt, ap);
    va_end(ap);
    fputc('\n', out);
    fflush(out);
}

static uint32_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint32_t)((uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u);
}

/* A resource whose interface has only a destroy request. */
static void bc_destroy_request(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    wl_resource_destroy(resource);
}

static void unlink_resource(struct wl_resource *resource)
{
    wl_list_remove(wl_resource_get_link(resource));
}

/* ---- WINDOW lines ---- */

static void window_set_output(struct bc_xdg *x, struct bc_output *o);
static struct bc_output *output_default(struct bc_server *s);
static struct wl_resource *output_resource(struct bc_output *o, struct wl_client *client);

static int xdg_parent_id(struct bc_xdg *x)
{
    return x->parent && x->parent->id ? (int)x->parent->id : 0;
}

static void send_limits(struct bc_xdg *x)
{
    bc_wire_line(&x->server->wire, "WINDOW limits %u %d %d %d %d", x->id, x->min_w, x->min_h,
                 x->max_w, x->max_h);
    x->limits_sent = 1;
}

static void send_decor(struct bc_xdg *x)
{
    bc_wire_line(&x->server->wire, "WINDOW decor %u %s", x->id,
                 x->decor_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE ? "server" : "client");
}

/* The toplevel's first appearance, at its initial commit: new, then every
   hint it set before it. */
static void announce_toplevel(struct bc_xdg *x)
{
    BcWire *w = &x->server->wire;
    x->id = bc_wire_new_id(w);
    bc_wire_line(w, "WINDOW new %u %d", x->id, xdg_parent_id(x));
    if (x->title[0]) bc_wire_line(w, "WINDOW title %u %s", x->id, x->title);
    if (x->appid[0]) bc_wire_line(w, "WINDOW appid %u %s", x->id, x->appid);
    if (x->min_w || x->min_h || x->max_w || x->max_h) send_limits(x);
    send_decor(x);
    if (x->fullscreen) bc_wire_line(w, "WINDOW fullscreen %u on", x->id);
    if (x->maximize) bc_wire_line(w, "WINDOW maximize %u on", x->id);
    /* Its input, straight from Blit to the input socket (one route per
       toplevel); the desktop relays the request. */
    if (x->server->input_path[0])
        bc_wire_line(w, "INPUT sock %s %u", x->server->input_path, x->id);
    if (x->minimize) bc_wire_line(w, "WINDOW minimize %u", x->id);
    x->minimize = 0;
    logf_("window %u: new parent %d title \"%s\" appid \"%s\"", x->id, xdg_parent_id(x),
          x->title, x->appid);
    /* On the first output until the desktop says which display it is on. */
    window_set_output(x, output_default(x->server));
    /* A lock asked for before the window was announced is asked for now. */
    if (x->surface) constraint_ask_surface(x->surface);
}

/* `WINDOW gone`, once, after everything of the id is released: copied
   frames hold nothing, and the dmabuf frames Blit still holds are given
   back to the client first. */
static void holds_end_id(struct bc_server *s, uint32_t id);

static void window_gone(struct bc_xdg *x)
{
    window_set_output(x, NULL);
    /* Its pointer lock let go first: the desktop's hold ends with it. */
    constraints_window_gone(x);
    if (x->id) {
        holds_end_id(x->server, x->id);
        bc_wire_line(&x->server->wire, "WINDOW gone %u", x->id);
        logf_("window %u: gone", x->id);
    }
    x->id = 0;
    x->mapped = 0;
}

/* ---- configure ---- */

static void send_toplevel_configure(struct bc_xdg *x)
{
    struct wl_array states;
    uint32_t *s;
    wl_array_init(&states);
    if (x->size_fullscreen && (s = wl_array_add(&states, sizeof(*s))) != NULL)
        *s = XDG_TOPLEVEL_STATE_FULLSCREEN;
    if (x->size_maximized && (s = wl_array_add(&states, sizeof(*s))) != NULL)
        *s = XDG_TOPLEVEL_STATE_MAXIMIZED;
    if (x->size_activated && (s = wl_array_add(&states, sizeof(*s))) != NULL)
        *s = XDG_TOPLEVEL_STATE_ACTIVATED;
    if (x->decoration)
        zxdg_toplevel_decoration_v1_send_configure(x->decoration, x->decor_mode);
    xdg_toplevel_send_configure(x->toplevel, x->size_w, x->size_h, &states);
    wl_array_release(&states);
    x->serial = wl_display_next_serial(x->server->display);
    xdg_surface_send_configure(x->resource, x->serial);
    x->configured = 1;
}

/* The desktop's size, applied at once when the toplevel is ready for it. */
static void configure_if_ready(struct bc_xdg *x)
{
    if (x->toplevel && x->initial && x->have_size)
        send_toplevel_configure(x);
}

/* ---- zwp_linux_dmabuf_v1: FRM3, zero copy ---- */

/* A wl_buffer made from dmabuf planes. The planes' descriptors are the
   client's, kept open for the buffer's life: each frame sends them as they
   are, and Blit imports them (ADR 0223). */
struct bc_dmabuf {
    struct wl_resource *resource;
    int32_t width, height;
    uint32_t format;
    uint64_t modifier;
    int planes;
    int fds[BC_PLANES_MAX];
    uint32_t offsets[BC_PLANES_MAX], strides[BC_PLANES_MAX];
    int holds;                        /* frames of it Blit still holds */
};

/* A frame sent as FRM3 and not yet released by Blit. */
struct bc_hold {
    struct wl_list link;              /* bc_server.holds */
    uint32_t id;
    uint64_t sequence;
    struct bc_dmabuf *buffer;         /* NULL once the client destroyed it */
};

struct bc_params {
    struct wl_resource *resource;
    int used;
    int fds[BC_PLANES_MAX];
    uint32_t offsets[BC_PLANES_MAX], strides[BC_PLANES_MAX];
    uint64_t modifier;
};

static void dmabuf_buffer_destroyed(struct wl_resource *resource)
{
    struct bc_dmabuf *b = wl_resource_get_user_data(resource);
    struct bc_hold *h;
    int i;
    wl_list_for_each(h, &g_server->holds, link)
        if (h->buffer == b) h->buffer = NULL;
    for (i = 0; i < b->planes; ++i) close(b->fds[i]);
    free(b);
}

static const struct wl_buffer_interface dmabuf_buffer_impl = {
    .destroy = bc_destroy_request,
};

static struct bc_dmabuf *dmabuf_from_buffer(struct wl_resource *buffer)
{
    if (buffer == NULL || !wl_resource_instance_of(buffer, &wl_buffer_interface, &dmabuf_buffer_impl))
        return NULL;
    return wl_resource_get_user_data(buffer);
}

/* The client may reuse a buffer when no frame of it is held. */
static void hold_end(struct bc_hold *h)
{
    if (h->buffer && --h->buffer->holds == 0)
        wl_buffer_send_release(h->buffer->resource);
    wl_list_remove(&h->link);
    free(h);
}

/* Every frame of picture `id` still held, given back to its client: the
   picture is going, and WINDOW gone follows only once nothing of it is
   held (docs/api.md). Blit's later RELEASEs for them are stale. */
static void holds_end_id(struct bc_server *s, uint32_t id)
{
    struct bc_hold *h, *next;
    wl_list_for_each_safe(h, next, &s->holds, link)
        if (h->id == id) hold_end(h);
}

/* A dmabuf's pixels copied as FRM1, for a peer that cannot import it:
   only one linear plane of an 8-bit RGB format can be mapped and read. */
static int dmabuf_copy(struct bc_server *s, uint32_t id, struct bc_dmabuf *b)
{
    struct dma_buf_sync sync;
    size_t bytes;
    unsigned char *map;
    uint32_t shm_format;
    int rc;
    if (b->planes != 1 || b->modifier != BC_MODIFIER_LINEAR) {
        snprintf(s->wire.last_error, sizeof(s->wire.last_error),
                 "a dmabuf of %d planes, modifier %#llx, cannot be copied", b->planes,
                 (unsigned long long)b->modifier);
        return -1;
    }
    switch (b->format) {
    case BC_FOURCC_XR24: shm_format = BC_SHM_XRGB8888; break;
    case BC_FOURCC_AR24: shm_format = BC_SHM_ARGB8888; break;
    case BC_FOURCC_XB24: case BC_FOURCC_AB24: shm_format = b->format; break;
    default:
        snprintf(s->wire.last_error, sizeof(s->wire.last_error), "format %.4s cannot be copied",
                 (const char *)&b->format);
        return -1;
    }
    bytes = (size_t)b->offsets[0] + (size_t)b->strides[0] * (size_t)b->height;
    map = mmap(NULL, bytes, PROT_READ, MAP_SHARED, b->fds[0], 0);
    if (map == MAP_FAILED) {
        snprintf(s->wire.last_error, sizeof(s->wire.last_error), "the dmabuf cannot be mapped: %s",
                 strerror(errno));
        return -1;
    }
    /* A dmabuf is read between the CPU-access fences; a memfd has none. */
    sync.flags = DMA_BUF_SYNC_START | DMA_BUF_SYNC_READ;
    ioctl(b->fds[0], DMA_BUF_IOCTL_SYNC, &sync);
    rc = bc_wire_send_shm(&s->wire, id, map + b->offsets[0], (uint32_t)b->width,
                          (uint32_t)b->height, b->strides[0], shm_format);
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(b->fds[0], DMA_BUF_IOCTL_SYNC, &sync);
    munmap(map, bytes);
    if (rc == 0) ++s->wire.dmabuf_copied;
    return rc;
}

static void params_destroyed(struct wl_resource *resource)
{
    struct bc_params *p = wl_resource_get_user_data(resource);
    int i;
    for (i = 0; i < BC_PLANES_MAX; ++i)
        if (p->fds[i] >= 0) close(p->fds[i]);
    free(p);
}

static void params_add(struct wl_client *client, struct wl_resource *resource, int32_t fd,
                       uint32_t plane, uint32_t offset, uint32_t stride, uint32_t mod_hi,
                       uint32_t mod_lo)
{
    struct bc_params *p = wl_resource_get_user_data(resource);
    (void)client;
    if (p->used) {
        close(fd);
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "used");
        return;
    }
    if (plane >= BC_PLANES_MAX) {
        close(fd);
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_IDX, "plane %u", plane);
        return;
    }
    if (p->fds[plane] >= 0) {
        close(fd);
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_PLANE_SET, "plane %u set", plane);
        return;
    }
    p->fds[plane] = fd;
    p->offsets[plane] = offset;
    p->strides[plane] = stride;
    p->modifier = (uint64_t)mod_hi << 32 | mod_lo;
}

/* Checks the planes against what Blit imports; on success the buffer
   takes the descriptors. Returns 0, or the protocol error with `why`. */
static int params_check(struct bc_params *p, int32_t width, int32_t height, uint32_t format,
                        uint32_t flags, struct bc_dmabuf *b, char *why, size_t cap)
{
    int planes = 0, i;
    off_t size;
    while (planes < BC_PLANES_MAX && p->fds[planes] >= 0) ++planes;
    for (i = planes; i < BC_PLANES_MAX; ++i)
        if (p->fds[i] >= 0) {
            snprintf(why, cap, "plane %d without plane %d", i, planes);
            return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE;
        }
    if (planes == 0) {
        snprintf(why, cap, "no planes");
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE;
    }
    if (width <= 0 || height <= 0) {
        snprintf(why, cap, "size %dx%d", width, height);
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS;
    }
    if (!bc_wire_format_ok(&g_server->wire, format, p->modifier) || flags != 0) {
        snprintf(why, cap, "format %.4s modifier %#llx flags %u is not one Blit imports",
                 (const char *)&format, (unsigned long long)p->modifier, flags);
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT;
    }
    /* Plane 0 holds its rows (ADR 0223's own check); a descriptor that
       cannot say its size is left to Blit's import. */
    size = lseek(p->fds[0], 0, SEEK_END);
    if (size >= 0 && (uint64_t)p->offsets[0] + (uint64_t)p->strides[0] * (uint64_t)(height - 1) +
                         4ull * (uint64_t)width > (uint64_t)size) {
        snprintf(why, cap, "plane 0 of %lld bytes cannot hold %dx%d at stride %u",
                 (long long)size, width, height, p->strides[0]);
        return ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_OUT_OF_BOUNDS;
    }
    b->width = width;
    b->height = height;
    b->format = format;
    b->modifier = p->modifier;
    b->planes = planes;
    for (i = 0; i < planes; ++i) {
        b->fds[i] = p->fds[i];
        b->offsets[i] = p->offsets[i];
        b->strides[i] = p->strides[i];
        p->fds[i] = -1;
    }
    return -1;
}

static struct wl_resource *params_make(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t buffer_id, int32_t width, int32_t height,
                                       uint32_t format, uint32_t flags, int immediate)
{
    struct bc_params *p = wl_resource_get_user_data(resource);
    struct bc_dmabuf *b;
    char why[160];
    int error;
    if (p->used) {
        wl_resource_post_error(resource, ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_ALREADY_USED, "used");
        return NULL;
    }
    p->used = 1;
    b = calloc(1, sizeof(*b));
    if (b == NULL) {
        wl_client_post_no_memory(client);
        return NULL;
    }
    error = params_check(p, width, height, format, flags, b, why, sizeof(why));
    if (error >= 0) {
        free(b);
        logf_("a dmabuf buffer refused: %s", why);
        if (immediate || error == ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INCOMPLETE ||
            error == ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_DIMENSIONS)
            wl_resource_post_error(resource, immediate && error == ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_FORMAT
                                                 ? ZWP_LINUX_BUFFER_PARAMS_V1_ERROR_INVALID_WL_BUFFER
                                                 : (uint32_t)error, "%s", why);
        else
            zwp_linux_buffer_params_v1_send_failed(resource);
        return NULL;
    }
    b->resource = wl_resource_create(client, &wl_buffer_interface, 1, buffer_id);
    if (b->resource == NULL) {
        int i;
        for (i = 0; i < b->planes; ++i) close(b->fds[i]);
        free(b);
        wl_client_post_no_memory(client);
        return NULL;
    }
    wl_resource_set_implementation(b->resource, &dmabuf_buffer_impl, b, dmabuf_buffer_destroyed);
    return b->resource;
}

static void params_create(struct wl_client *client, struct wl_resource *resource, int32_t width,
                          int32_t height, uint32_t format, uint32_t flags)
{
    struct wl_resource *buffer = params_make(client, resource, 0, width, height, format, flags, 0);
    if (buffer) zwp_linux_buffer_params_v1_send_created(resource, buffer);
}

static void params_create_immed(struct wl_client *client, struct wl_resource *resource,
                                uint32_t buffer_id, int32_t width, int32_t height, uint32_t format,
                                uint32_t flags)
{
    params_make(client, resource, buffer_id, width, height, format, flags, 1);
}

static const struct zwp_linux_buffer_params_v1_interface params_impl = {
    .destroy = bc_destroy_request,
    .add = params_add,
    .create = params_create,
    .create_immed = params_create_immed,
};

static void dmabuf_create_params(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_params *p = calloc(1, sizeof(*p));
    int i;
    if (p == NULL) { wl_client_post_no_memory(client); return; }
    for (i = 0; i < BC_PLANES_MAX; ++i) p->fds[i] = -1;
    p->resource = wl_resource_create(client, &zwp_linux_buffer_params_v1_interface,
                                     wl_resource_get_version(resource), id);
    if (p->resource == NULL) { free(p); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(p->resource, &params_impl, p, params_destroyed);
}

static const struct zwp_linux_dmabuf_feedback_v1_interface feedback_impl = {
    .destroy = bc_destroy_request,
};

/* Version 4's feedback, one tranche: every pair Blit listed, on the main
   device. The same for every surface: Blit imports any window's frames
   alike. */
static void dmabuf_feedback(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct wl_resource *r = wl_resource_create(client, &zwp_linux_dmabuf_feedback_v1_interface,
                                               wl_resource_get_version(resource), id);
    struct wl_array device, indices;
    uint16_t *index;
    dev_t *dev;
    int i;
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &feedback_impl, NULL, NULL);
    zwp_linux_dmabuf_feedback_v1_send_format_table(r, s->format_table_fd,
                                                   (uint32_t)(s->wire.format_count * 16));
    wl_array_init(&device);
    if ((dev = wl_array_add(&device, sizeof(*dev))) != NULL) *dev = s->main_device;
    zwp_linux_dmabuf_feedback_v1_send_main_device(r, &device);
    zwp_linux_dmabuf_feedback_v1_send_tranche_target_device(r, &device);
    wl_array_init(&indices);
    for (i = 0; i < s->wire.format_count; ++i)
        if ((index = wl_array_add(&indices, sizeof(*index))) != NULL) *index = (uint16_t)i;
    zwp_linux_dmabuf_feedback_v1_send_tranche_formats(r, &indices);
    zwp_linux_dmabuf_feedback_v1_send_tranche_flags(r, 0);
    zwp_linux_dmabuf_feedback_v1_send_tranche_done(r);
    zwp_linux_dmabuf_feedback_v1_send_done(r);
    wl_array_release(&indices);
    wl_array_release(&device);
}

static void dmabuf_get_surface_feedback(struct wl_client *client, struct wl_resource *resource,
                                        uint32_t id, struct wl_resource *surface)
{
    (void)surface;
    dmabuf_feedback(client, resource, id);
}

static const struct zwp_linux_dmabuf_v1_interface dmabuf_impl = {
    .destroy = bc_destroy_request,
    .create_params = dmabuf_create_params,
    .get_default_feedback = dmabuf_feedback,
    .get_surface_feedback = dmabuf_get_surface_feedback,
};

static void bind_dmabuf(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct bc_server *s = data;
    struct wl_resource *r = wl_resource_create(client, &zwp_linux_dmabuf_v1_interface, (int)version, id);
    int i, j;
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &dmabuf_impl, s, NULL);
    if (version >= ZWP_LINUX_DMABUF_V1_GET_DEFAULT_FEEDBACK_SINCE_VERSION) return;
    /* Before version 4: the formats, each once, and from version 3 each
       modifier. */
    for (i = 0; i < s->wire.format_count; ++i) {
        const BcFormat *f = &s->wire.formats[i];
        for (j = 0; j < i; ++j)
            if (s->wire.formats[j].fourcc == f->fourcc) break;
        if (j == i) zwp_linux_dmabuf_v1_send_format(r, f->fourcc);
        if (version >= ZWP_LINUX_DMABUF_V1_MODIFIER_SINCE_VERSION)
            zwp_linux_dmabuf_v1_send_modifier(r, f->fourcc, (uint32_t)(f->modifier >> 32),
                                              (uint32_t)f->modifier);
    }
}

/* The format table of version 4's feedback: 16 bytes a pair (format,
   padding, modifier), sealed, shared read-only by every client. */
static int dmabuf_format_table(const BcWire *w)
{
    size_t bytes = (size_t)w->format_count * 16;
    unsigned char *map;
    int fd = memfd_create("blit-compositor-formats", MFD_CLOEXEC | MFD_ALLOW_SEALING), i;
    if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0) {
        if (fd >= 0) close(fd);
        return -1;
    }
    map = mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        close(fd);
        return -1;
    }
    memset(map, 0, bytes);
    for (i = 0; i < w->format_count; ++i) {
        memcpy(map + 16 * i, &w->formats[i].fourcc, 4);
        memcpy(map + 16 * i + 8, &w->formats[i].modifier, 8);
    }
    munmap(map, bytes);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    return fd;
}

/* The device clients allocate on: the first DRM render node. The hello
   does not name Blit's GPU, so one GPU is assumed; with none, 0. */
static dev_t dmabuf_main_device(void)
{
    char path[32];
    struct stat st;
    int i;
    for (i = 128; i < 192; ++i) {
        snprintf(path, sizeof(path), "/dev/dri/renderD%d", i);
        if (stat(path, &st) == 0 && S_ISCHR(st.st_mode)) {
            logf_("dmabuf main device %s", path);
            return st.st_rdev;
        }
    }
    logf_("no DRM render node: the dmabuf feedback names device 0");
    return 0;
}

/* ---- frames ---- */

static void callbacks_done(struct wl_list *list)
{
    struct wl_resource *cb, *next;
    uint32_t t = now_ms();
    wl_resource_for_each_safe(cb, next, list) {
        wl_callback_send_done(cb, t);
        wl_resource_destroy(cb);
    }
}

static void callbacks_destroy(struct wl_list *list)
{
    struct wl_resource *cb, *next;
    wl_resource_for_each_safe(cb, next, list)
        wl_resource_destroy(cb);
}

/* ---- frame pacing ----

   A frame callback says "draw the next one now". It is answered when Blit
   has taken the frame the commit brought, and at most once per refresh of
   the window's output (WINDOW output; 60 Hz until the first), so a client
   that draws on every callback runs at the display's rate, not as fast as
   the copy:

   - wl_shm, or a dmabuf copied: taken when its FRM1 is sent;
   - a dmabuf sent as FRM3: taken when it is the window's only frame Blit
     holds, which is at once for the first and, after that, at Blit's
     RELEASE of the frame before it -- the RELEASE that says Blit drew the
     newer frame (or let it supersede the older one), so a client is never
     more than one frame ahead of Blit;
   - a commit with no new buffer on a shown window: taken at once (a client
     that only asks for a callback is paced the same way).

   wp_presentation feedback is `presented` when Blit takes the frame,
   CLOCK_MONOTONIC, with the output's refresh and no flags: the compositor
   does not see Blit's vblank, so it claims neither vsync nor a hardware
   clock. It is `discarded` when the next commit with a buffer comes before
   Blit took the frame, when the frame was not sent, or when the surface
   goes. */

static uint64_t now_us(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000000u + (uint64_t)ts.tv_nsec / 1000u;
}

static uint32_t surface_refresh_mhz(struct bc_surface *surface)
{
    struct bc_xdg *x = surface->xdg;
    return x && x->output ? x->output->refresh_mhz : 60000u;
}

static void feedback_discard(struct wl_list *list)
{
    struct wl_resource *r, *next;
    wl_resource_for_each_safe(r, next, list) {
        wp_presentation_feedback_send_discarded(r);
        wl_resource_destroy(r);
    }
}

static void feedback_presented(struct bc_surface *surface)
{
    struct wl_resource *r, *next, *out;
    struct timespec ts;
    uint32_t mhz = surface_refresh_mhz(surface);
    struct bc_output *o = surface->xdg ? surface->xdg->output : NULL;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    wl_resource_for_each_safe(r, next, &surface->feedback) {
        if (o && (out = output_resource(o, wl_resource_get_client(r))) != NULL)
            wp_presentation_feedback_send_sync_output(r, out);
        wp_presentation_feedback_send_presented(r, (uint32_t)((uint64_t)ts.tv_sec >> 32),
                                                (uint32_t)ts.tv_sec, (uint32_t)ts.tv_nsec,
                                                (uint32_t)(1000000000000ull / mhz), 0, 0, 0);
        wl_resource_destroy(r);
    }
}

/* Answers the due callbacks if a refresh has passed since the last answer,
   else arms the timer for the rest of it. */
static void frames_pace(struct bc_surface *surface)
{
    uint64_t period = 1000000000ull / surface_refresh_mhz(surface), now = now_us();
    if (wl_list_empty(&surface->due)) return;
    if (now - surface->last_done_us >= period) {
        surface->last_done_us = now;
        callbacks_done(&surface->due);
        return;
    }
    if (surface->pace == NULL)
        return;
    wl_event_source_timer_update(surface->pace,
                                 (int)((period - (now - surface->last_done_us) + 999) / 1000));
}

static int frames_pace_timer(void *data)
{
    frames_pace(data);
    return 0;
}

/* Blit has taken the surface's committed frame. */
static void frames_taken(struct bc_surface *surface)
{
    if (surface == NULL) return;
    feedback_presented(surface);
    wl_list_insert_list(surface->due.prev, &surface->frames);
    wl_list_init(&surface->frames);
    frames_pace(surface);
}

enum { BC_SENT_NONE = 0, BC_SENT_COPY, BC_SENT_HELD };

/* A dmabuf commit: FRM3, held until Blit's RELEASE, unless the peer
   refused dmabuf on this connection, when it is copied as FRM1. */
static int send_dmabuf(struct bc_xdg *x, struct bc_dmabuf *b)
{
    struct bc_server *s = x->server;
    struct bc_hold *h;
    uint64_t seq;
    if (s->wire.dmabuf && !s->wire.refused) {
        int older = 0;
        wl_list_for_each(h, &s->holds, link)
            if (h->id == x->id) older = 1;
        h = calloc(1, sizeof(*h));
        seq = h ? bc_wire_send_dmabuf(&s->wire, x->id, (uint32_t)b->width, (uint32_t)b->height,
                                      b->format, b->modifier, b->planes, b->fds, b->offsets,
                                      b->strides)
                : 0;
        if (seq != 0) {
            h->id = x->id;
            h->sequence = seq;
            h->buffer = b;
            ++b->holds;
            wl_list_insert(s->holds.prev, &h->link);
            /* Taken at once when Blit holds nothing else of the window,
               else at the RELEASE of the frame before it. */
            if (!older) frames_taken(x->surface);
            return BC_SENT_HELD;
        }
        free(h);
        logf_("window %u: FRM3 not sent (%s); copying", x->id, s->wire.last_error);
    }
    if (dmabuf_copy(s, x->id, b) != 0) {
        logf_("window %u: a dmabuf frame not shown: %s", x->id, s->wire.last_error);
        return BC_SENT_NONE;
    }
    x->last_copy = s->wire.sequence;
    frames_taken(x->surface);
    return BC_SENT_COPY;
}

/* The committed buffer as one frame of the window's picture: BC_SENT_HELD
   for an FRM3 Blit now holds, BC_SENT_COPY for a copy (the buffer is the
   client's again), BC_SENT_NONE when nothing was sent. */
static int send_buffer(struct bc_surface *surface, struct wl_resource *buffer)
{
    struct bc_xdg *x = surface->xdg;
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    struct bc_dmabuf *dmabuf = dmabuf_from_buffer(buffer);
    int rc;
    if (shm == NULL && dmabuf == NULL) {
        logf_("window %u: a buffer of an unknown kind, not shown", x ? x->id : 0);
        return BC_SENT_NONE;
    }
    if (x == NULL || x->id == 0 || !x->server->wire.window)
        return BC_SENT_NONE;
    if (dmabuf) return send_dmabuf(x, dmabuf);
    wl_shm_buffer_begin_access(shm);
    rc = bc_wire_send_shm(&x->server->wire, x->id, wl_shm_buffer_get_data(shm),
                          (uint32_t)wl_shm_buffer_get_width(shm),
                          (uint32_t)wl_shm_buffer_get_height(shm),
                          (uint32_t)wl_shm_buffer_get_stride(shm),
                          wl_shm_buffer_get_format(shm));
    wl_shm_buffer_end_access(shm);
    if (rc != 0) {
        logf_("window %u: frame not sent: %s", x->id, x->server->wire.last_error);
        return BC_SENT_NONE;
    }
    x->last_copy = x->server->wire.sequence;
    frames_taken(surface);
    return BC_SENT_COPY;
}

/* ---- wl_surface ---- */

static void pending_buffer_gone(struct wl_listener *listener, void *data)
{
    struct bc_surface *surface = wl_container_of(listener, surface, pending_buffer_destroy);
    (void)data;
    wl_list_remove(&surface->pending_buffer_destroy.link);
    wl_list_init(&surface->pending_buffer_destroy.link);
    surface->pending_buffer = NULL;
}

static void set_pending_buffer(struct bc_surface *surface, struct wl_resource *buffer)
{
    if (surface->pending_buffer) {
        wl_list_remove(&surface->pending_buffer_destroy.link);
        wl_list_init(&surface->pending_buffer_destroy.link);
    }
    surface->pending_buffer = buffer;
    if (buffer)
        wl_resource_add_destroy_listener(buffer, &surface->pending_buffer_destroy);
}

static void surface_attach(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *buffer, int32_t x, int32_t y)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    (void)client; (void)x; (void)y;
    set_pending_buffer(surface, buffer);
    surface->pending_attach = 1;
}

static void surface_damage(struct wl_client *client, struct wl_resource *resource, int32_t x,
                           int32_t y, int32_t w, int32_t h)
{
    /* Every frame is a whole one: damage is not needed. */
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static void callback_unlink(struct wl_resource *resource)
{
    unlink_resource(resource);
}

static void surface_frame(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    struct wl_resource *cb = wl_resource_create(client, &wl_callback_interface, 1, id);
    if (cb == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(cb, NULL, NULL, callback_unlink);
    wl_list_insert(surface->pending_frames.prev, wl_resource_get_link(cb));
}

static void surface_region(struct wl_client *client, struct wl_resource *resource,
                           struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    struct bc_xdg *x = surface->xdg;
    struct wl_resource *buffer = surface->pending_buffer;
    int attached = surface->pending_attach, sent = 0;
    (void)client;

    /* Pending frame callbacks and feedback wait for Blit to take the next
       frame of this surface; feedback for a frame it never took is
       discarded once a newer buffer comes. */
    if (attached && buffer) feedback_discard(&surface->feedback);
    wl_list_insert_list(surface->frames.prev, &surface->pending_frames);
    wl_list_init(&surface->pending_frames);
    wl_list_insert_list(surface->feedback.prev, &surface->pending_feedback);
    wl_list_init(&surface->pending_feedback);
    set_pending_buffer(surface, NULL);
    surface->pending_attach = 0;

    if (x && (x->toplevel || x->popup)) {
        if (x->pending_geometry) {
            x->gx = x->pgx; x->gy = x->pgy; x->gw = x->pgw; x->gh = x->pgh;
            x->pending_geometry = 0;
        }
        if (x->toplevel && x->decor_dropped && !x->decoration) {
            x->decor_dropped = 0;
            if (x->decor_mode != 0 && x->decor_mode != ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE) {
                x->decor_mode = 0;
                if (x->id) send_decor(x);
            }
        }
        if (x->toplevel && (x->pmin_w != x->min_w || x->pmin_h != x->min_h ||
                            x->pmax_w != x->max_w || x->pmax_h != x->max_h)) {
            x->min_w = x->pmin_w; x->min_h = x->pmin_h;
            x->max_w = x->pmax_w; x->max_h = x->pmax_h;
            if (x->id) send_limits(x);
        }
        if (attached && buffer && !x->acked) {
            wl_resource_post_error(x->resource, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                                   "a buffer committed before the first configure was acknowledged");
            return;
        }
        if (attached && buffer == NULL && x->mapped) {
            /* Unmapped: it starts again from an initial commit, configured
               again at the desktop's last size; the picture keeps its id,
               and its next frame shows it again. */
            logf_("window %u: unmapped by its client", x->id);
            if (x->id) bc_wire_line(&x->server->wire, "WINDOW hide %u", x->id);
            x->mapped = 0;
            x->initial = 0;
            x->acked = 0;
            x->configured = 0;
        } else if (!x->initial) {
            x->initial = 1;
            if (x->toplevel) {
                if (x->id == 0 && x->server->wire.window)
                    announce_toplevel(x);
                else if (!x->server->wire.window)
                    logf_("a toplevel \"%s\" is not shown: the peer has no window=1", x->title);
                configure_if_ready(x);
            } else if (x->popup) {
                struct bc_xdg *p = x->parent;
                if (x->id == 0 && x->server->wire.window && p && p->id) {
                    x->id = bc_wire_new_id(&x->server->wire);
                    bc_wire_line(&x->server->wire, "WINDOW popup %u %u %d %d", x->id, p->id,
                                 p->gx + x->px, p->gy + x->py);
                    logf_("popup %u: parent %u at %d,%d %dx%d", x->id, p->id, p->gx + x->px,
                          p->gy + x->py, x->positioner.width, x->positioner.height);
                    window_set_output(x, p->output);
                }
                xdg_popup_send_configure(x->popup, x->px, x->py, x->positioner.width,
                                         x->positioner.height);
                x->serial = wl_display_next_serial(x->server->display);
                xdg_surface_send_configure(x->resource, x->serial);
                x->configured = 1;
            }
        }
        if (attached && buffer) {
            struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
            struct bc_dmabuf *dmabuf = dmabuf_from_buffer(buffer);
            sent = send_buffer(surface, buffer);
            x->mapped = 1;
            x->buffer_w = shm ? wl_shm_buffer_get_width(shm) : dmabuf ? dmabuf->width : 0;
            x->buffer_h = shm ? wl_shm_buffer_get_height(shm) : dmabuf ? dmabuf->height : 0;
        }
    }
    /* Copied or not shown (a subsurface, a cursor): the client's again. An
       FRM3 frame is the client's again at Blit's RELEASE. */
    if (attached && buffer && sent != BC_SENT_HELD)
        wl_buffer_send_release(buffer);
    if (attached && buffer && sent == BC_SENT_NONE)
        feedback_discard(&surface->feedback);
    else if (!attached && x && x->mapped && x->id)
        frames_taken(surface);
}

static void surface_set_buffer_transform(struct wl_client *client, struct wl_resource *resource,
                                         int32_t transform)
{
    (void)client; (void)resource;
    if (transform != WL_OUTPUT_TRANSFORM_NORMAL)
        logf_("a buffer transform %d is ignored (not composited)", transform);
}

static void surface_set_buffer_scale(struct wl_client *client, struct wl_resource *resource,
                                     int32_t scale)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    (void)client;
    if (scale < 1) {
        wl_resource_post_error(resource, WL_SURFACE_ERROR_INVALID_SCALE, "scale %d", scale);
        return;
    }
    if (scale != 1 && !surface->scale_warned) {
        surface->scale_warned = 1;
        logf_("a buffer scale %d is shown as pixels (scale 1)", scale);
    }
}

static void surface_offset(struct wl_client *client, struct wl_resource *resource, int32_t x,
                           int32_t y)
{
    (void)client; (void)resource; (void)x; (void)y;
}

static const struct wl_surface_interface surface_impl = {
    .destroy = bc_destroy_request,
    .attach = surface_attach,
    .damage = surface_damage,
    .frame = surface_frame,
    .set_opaque_region = surface_region,
    .set_input_region = surface_region,
    .commit = surface_commit,
    .set_buffer_transform = surface_set_buffer_transform,
    .set_buffer_scale = surface_set_buffer_scale,
    .damage_buffer = surface_damage,
    .offset = surface_offset,
};

static void surface_destroyed(struct wl_resource *resource)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    set_pending_buffer(surface, NULL);
    callbacks_destroy(&surface->pending_frames);
    callbacks_destroy(&surface->frames);
    callbacks_destroy(&surface->due);
    feedback_discard(&surface->pending_feedback);
    feedback_discard(&surface->feedback);
    if (surface->pace) wl_event_source_remove(surface->pace);
    if (surface->server->kbd_focus == surface) surface->server->kbd_focus = NULL;
    constraint_surface_gone(surface);
    if (surface->server->ptr_focus == surface) {
        surface->server->ptr_focus = NULL;
        surface->server->buttons_down = 0;
    }
    if (surface->xdg) {
        /* The role object outlived its surface (a client error, or the
           client's teardown order): the window is gone either way. */
        window_gone(surface->xdg);
        surface->xdg->surface = NULL;
    }
    free(surface);
}

/* ---- wl_compositor ---- */

/* Regions are not used: input and opacity are milestone B's at most. */
static void region_rect(struct wl_client *client, struct wl_resource *resource, int32_t x,
                        int32_t y, int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)x; (void)y; (void)w; (void)h;
}

static const struct wl_region_interface region_impl = {
    .destroy = bc_destroy_request,
    .add = region_rect,
    .subtract = region_rect,
};

static void compositor_create_surface(struct wl_client *client, struct wl_resource *resource,
                                      uint32_t id)
{
    struct bc_surface *surface = calloc(1, sizeof(*surface));
    if (surface == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->server = wl_resource_get_user_data(resource);
    surface->resource = wl_resource_create(client, &wl_surface_interface,
                                           wl_resource_get_version(resource), id);
    if (surface->resource == NULL) {
        free(surface);
        wl_client_post_no_memory(client);
        return;
    }
    wl_list_init(&surface->pending_frames);
    wl_list_init(&surface->frames);
    wl_list_init(&surface->due);
    wl_list_init(&surface->pending_feedback);
    wl_list_init(&surface->feedback);
    surface->pace = wl_event_loop_add_timer(surface->server->loop, frames_pace_timer, surface);
    surface->pending_buffer_destroy.notify = pending_buffer_gone;
    wl_list_init(&surface->pending_buffer_destroy.link);
    wl_resource_set_implementation(surface->resource, &surface_impl, surface, surface_destroyed);
}

static void compositor_create_region(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t id)
{
    struct wl_resource *region = wl_resource_create(client, &wl_region_interface, 1, id);
    (void)resource;
    if (region == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region, &region_impl, NULL, NULL);
}

static const struct wl_compositor_interface compositor_impl = {
    .create_surface = compositor_create_surface,
    .create_region = compositor_create_region,
};

static void bind_compositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wl_compositor_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &compositor_impl, data, NULL);
}

/* ---- wl_subcompositor: accepted, not composited ---- */

static void subsurface_set_position(struct wl_client *client, struct wl_resource *resource,
                                    int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)x; (void)y;
}

static void subsurface_place(struct wl_client *client, struct wl_resource *resource,
                             struct wl_resource *sibling)
{
    (void)client; (void)resource; (void)sibling;
}

static void subsurface_sync(struct wl_client *client, struct wl_resource *resource)
{
    (void)client; (void)resource;
}

static const struct wl_subsurface_interface subsurface_impl = {
    .destroy = bc_destroy_request,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place,
    .place_below = subsurface_place,
    .set_sync = subsurface_sync,
    .set_desync = subsurface_sync,
};

static void subcompositor_get_subsurface(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t id, struct wl_resource *surface_resource,
                                         struct wl_resource *parent)
{
    struct bc_surface *surface = wl_resource_get_user_data(surface_resource);
    struct wl_resource *r;
    (void)parent;
    if (surface->role != BC_ROLE_NONE && surface->role != BC_ROLE_SUBSURFACE) {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "the surface already has another role");
        return;
    }
    r = wl_resource_create(client, &wl_subsurface_interface, 1, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    surface->role = BC_ROLE_SUBSURFACE;
    wl_resource_set_implementation(r, &subsurface_impl, NULL, NULL);
    logf_("a subsurface is accepted but not shown: subsurfaces are not composited yet");
}

static const struct wl_subcompositor_interface subcompositor_impl = {
    .destroy = bc_destroy_request,
    .get_subsurface = subcompositor_get_subsurface,
};

static void bind_subcompositor(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wl_subcompositor_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &subcompositor_impl, data, NULL);
}

/* ---- wl_output: one per display mode (WINDOW output) ---- */

static void output_send_mode(struct bc_output *o, struct wl_resource *r)
{
    wl_output_send_mode(r, WL_OUTPUT_MODE_CURRENT | WL_OUTPUT_MODE_PREFERRED, o->width,
                        o->height, (int32_t)o->refresh_mhz);
    if (wl_resource_get_version(r) >= WL_OUTPUT_DONE_SINCE_VERSION)
        wl_output_send_done(r);
}

/* The client's resource for output `o`, or NULL if it has not bound it. */
static struct wl_resource *output_resource(struct bc_output *o, struct wl_client *client)
{
    struct wl_resource *r;
    wl_resource_for_each(r, &o->resources)
        if (wl_resource_get_client(r) == client) return r;
    return NULL;
}

/* wl_surface.enter or leave for every output resource the surface's client
   holds of `o` (a client may bind one global more than once). */
static void output_surface_event(struct bc_output *o, struct bc_surface *surface, int enter)
{
    struct wl_client *client = wl_resource_get_client(surface->resource);
    struct wl_resource *r;
    wl_resource_for_each(r, &o->resources) {
        if (wl_resource_get_client(r) != client) continue;
        if (enter) wl_surface_send_enter(surface->resource, r);
        else wl_surface_send_leave(surface->resource, r);
    }
}

/* Moves a window (and so its surface) onto output `o`, NULL for none. */
static void window_set_output(struct bc_xdg *x, struct bc_output *o)
{
    if (x->output == o) return;
    if (x->output) {
        if (x->surface) output_surface_event(x->output, x->surface, 0);
        --x->output->windows;
    }
    x->output = o;
    if (o) {
        ++o->windows;
        if (x->surface) output_surface_event(o, x->surface, 1);
    }
}

static const struct wl_output_interface output_impl = {
    .release = bc_destroy_request,
};

static void bind_output(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct bc_output *o = data;
    struct bc_xdg *x;
    struct wl_resource *r = wl_resource_create(client, &wl_output_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &output_impl, o, unlink_resource);
    wl_list_insert(&o->resources, wl_resource_get_link(r));
    wl_output_send_geometry(r, 0, 0, 0, 0, WL_OUTPUT_SUBPIXEL_UNKNOWN, "Blit", "blit-compositor",
                            WL_OUTPUT_TRANSFORM_NORMAL);
    if (version >= WL_OUTPUT_SCALE_SINCE_VERSION)
        wl_output_send_scale(r, 1);
    if (version >= WL_OUTPUT_NAME_SINCE_VERSION) {
        char name[32];
        snprintf(name, sizeof(name), "BLIT-%d", o->number);
        wl_output_send_name(r, name);
    }
    if (version >= WL_OUTPUT_DESCRIPTION_SINCE_VERSION)
        wl_output_send_description(r, "Blit desktop");
    output_send_mode(o, r);
    /* The client's windows already on this output enter it now. */
    wl_list_for_each(x, &g_server->windows, link)
        if (x->output == o && x->surface && wl_resource_get_client(x->surface->resource) == client)
            wl_surface_send_enter(x->surface->resource, r);
}

static struct bc_output *output_create(struct bc_server *s, int32_t w, int32_t h, uint32_t mhz)
{
    struct bc_output *o = calloc(1, sizeof(*o));
    if (o == NULL) return NULL;
    wl_list_init(&o->resources);
    o->width = w;
    o->height = h;
    o->refresh_mhz = mhz;
    o->number = wl_list_length(&s->outputs) + 1;
    /* At the tail: the first is BLIT-1, the default output. */
    wl_list_insert(s->outputs.prev, &o->link);
    o->global = wl_global_create(s->display, &wl_output_interface,
                                 BC_VERSION(4, wl_output_interface), o, bind_output);
    if (o->global == NULL) {
        wl_list_remove(&o->link);
        free(o);
        return NULL;
    }
    return o;
}

/* The output of mode w x h at mhz: one that has it, else one no window is
   on given it (its clients see the mode change), else a new global. */
static struct bc_output *output_for_mode(struct bc_server *s, int32_t w, int32_t h, uint32_t mhz)
{
    struct bc_output *o, *idle = NULL;
    struct wl_resource *r;
    wl_list_for_each(o, &s->outputs, link) {
        if (o->width == w && o->height == h && o->refresh_mhz == mhz) return o;
        if (o->windows == 0 && idle == NULL) idle = o;
    }
    if (idle == NULL) return output_create(s, w, h, mhz);
    idle->width = w;
    idle->height = h;
    idle->refresh_mhz = mhz;
    wl_resource_for_each(r, &idle->resources)
        output_send_mode(idle, r);
    return idle;
}

/* The first output: a window the desktop has not yet placed on a display
   is on it. */
static struct bc_output *output_default(struct bc_server *s)
{
    struct bc_output *o;
    wl_list_for_each(o, &s->outputs, link)
        return o;
    return NULL;
}

/* A window and the popups opened from it go to output `o`. */
static void window_move_output(struct bc_server *s, struct bc_xdg *x, struct bc_output *o)
{
    struct bc_xdg *other;
    window_set_output(x, o);
    wl_list_for_each(other, &s->windows, link)
        if (other->popup && other->parent == x && other->id) window_set_output(other, o);
}

/* ---- wl_seat: keyboard and pointer (their events: the input section) ---- */

static void pointer_set_cursor(struct wl_client *client, struct wl_resource *resource,
                               uint32_t serial, struct wl_resource *surface, int32_t x, int32_t y)
{
    (void)client; (void)resource; (void)serial; (void)surface; (void)x; (void)y;
}

static const struct wl_pointer_interface pointer_impl = {
    .set_cursor = pointer_set_cursor,
    .release = bc_destroy_request,
};

static const struct wl_keyboard_interface keyboard_impl = {
    .release = bc_destroy_request,
};

static const struct wl_touch_interface touch_impl = {
    .release = bc_destroy_request,
};

static void seat_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct wl_resource *r = wl_resource_create(client, &wl_pointer_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &pointer_impl, s, unlink_resource);
    wl_list_insert(&s->pointers, wl_resource_get_link(r));
    if (s->ptr_focus && wl_resource_get_client(s->ptr_focus->resource) == client)
        wl_pointer_send_enter(r, wl_display_next_serial(s->display), s->ptr_focus->resource,
                              wl_fixed_from_double(s->ptr_x), wl_fixed_from_double(s->ptr_y));
}

static void seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct wl_resource *r = wl_resource_create(client, &wl_keyboard_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &keyboard_impl, s, unlink_resource);
    wl_list_insert(&s->keyboards, wl_resource_get_link(r));
    keyboard_send_keymap(s, r);
    if (wl_resource_get_version(r) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
        wl_keyboard_send_repeat_info(r, s->repeat_rate, s->repeat_delay);
    if (s->kbd_focus && wl_resource_get_client(s->kbd_focus->resource) == client) {
        struct wl_array keys;
        wl_array_init(&keys);
        wl_keyboard_send_enter(r, wl_display_next_serial(s->display), s->kbd_focus->resource, &keys);
        wl_array_release(&keys);
        keyboard_send_modifiers(s, 1);
    }
}

static void seat_get_touch(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    /* Not advertised; created only so a client that asks anyway is not
       killed for it. */
    struct wl_resource *r = wl_resource_create(client, &wl_touch_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &touch_impl, NULL, NULL);
}

static const struct wl_seat_interface seat_impl = {
    .get_pointer = seat_get_pointer,
    .get_keyboard = seat_get_keyboard,
    .get_touch = seat_get_touch,
    .release = bc_destroy_request,
};

static void bind_seat(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wl_seat_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &seat_impl, data, NULL);
    wl_seat_send_capabilities(r, WL_SEAT_CAPABILITY_POINTER | WL_SEAT_CAPABILITY_KEYBOARD);
    if (version >= WL_SEAT_NAME_SINCE_VERSION)
        wl_seat_send_name(r, "seat0");
}

/* ---- xdg_positioner ---- */

static void positioner_set_size(struct wl_client *client, struct wl_resource *resource,
                                int32_t w, int32_t h)
{
    struct bc_positioner *p = wl_resource_get_user_data(resource);
    (void)client;
    if (w <= 0 || h <= 0) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT, "size %dx%d", w, h);
        return;
    }
    p->width = w;
    p->height = h;
}

static void positioner_set_anchor_rect(struct wl_client *client, struct wl_resource *resource,
                                       int32_t x, int32_t y, int32_t w, int32_t h)
{
    struct bc_positioner *p = wl_resource_get_user_data(resource);
    (void)client;
    if (w < 0 || h < 0) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT, "anchor %dx%d", w, h);
        return;
    }
    p->ax = x; p->ay = y; p->aw = w; p->ah = h;
}

static void positioner_set_anchor(struct wl_client *client, struct wl_resource *resource,
                                  uint32_t anchor)
{
    struct bc_positioner *p = wl_resource_get_user_data(resource);
    (void)client;
    if (anchor > XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT, "anchor %u", anchor);
        return;
    }
    p->anchor = anchor;
}

static void positioner_set_gravity(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t gravity)
{
    struct bc_positioner *p = wl_resource_get_user_data(resource);
    (void)client;
    if (gravity > XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT) {
        wl_resource_post_error(resource, XDG_POSITIONER_ERROR_INVALID_INPUT, "gravity %u", gravity);
        return;
    }
    p->gravity = gravity;
}

static void positioner_set_constraint_adjustment(struct wl_client *client,
                                                 struct wl_resource *resource, uint32_t adj)
{
    /* The desktop may draw a popup past its parent's window: nothing is
       constrained here. */
    (void)client; (void)resource; (void)adj;
}

static void positioner_set_offset(struct wl_client *client, struct wl_resource *resource,
                                  int32_t x, int32_t y)
{
    struct bc_positioner *p = wl_resource_get_user_data(resource);
    (void)client;
    p->dx = x;
    p->dy = y;
}

static void positioner_set_reactive(struct wl_client *client, struct wl_resource *resource)
{
    (void)client; (void)resource;
}

static void positioner_set_parent_size(struct wl_client *client, struct wl_resource *resource,
                                       int32_t w, int32_t h)
{
    (void)client; (void)resource; (void)w; (void)h;
}

static void positioner_set_parent_configure(struct wl_client *client,
                                            struct wl_resource *resource, uint32_t serial)
{
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_positioner_interface positioner_impl = {
    .destroy = bc_destroy_request,
    .set_size = positioner_set_size,
    .set_anchor_rect = positioner_set_anchor_rect,
    .set_anchor = positioner_set_anchor,
    .set_gravity = positioner_set_gravity,
    .set_constraint_adjustment = positioner_set_constraint_adjustment,
    .set_offset = positioner_set_offset,
    .set_reactive = positioner_set_reactive,
    .set_parent_size = positioner_set_parent_size,
    .set_parent_configure = positioner_set_parent_configure,
};

static void positioner_destroyed(struct wl_resource *resource)
{
    free(wl_resource_get_user_data(resource));
}

/* The popup's top-left relative to its parent's window geometry: the
   anchor point on the anchor rectangle, the popup placed from it toward
   the gravity, then the offset; unconstrained (xdg-shell's rules). */
static void positioner_place(const struct bc_positioner *p, int32_t *x, int32_t *y)
{
    int32_t px, py;
    switch (p->anchor) {
    case XDG_POSITIONER_ANCHOR_TOP: case XDG_POSITIONER_ANCHOR_TOP_LEFT:
    case XDG_POSITIONER_ANCHOR_TOP_RIGHT: py = p->ay; break;
    case XDG_POSITIONER_ANCHOR_BOTTOM: case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: py = p->ay + p->ah; break;
    default: py = p->ay + p->ah / 2; break;
    }
    switch (p->anchor) {
    case XDG_POSITIONER_ANCHOR_LEFT: case XDG_POSITIONER_ANCHOR_TOP_LEFT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_LEFT: px = p->ax; break;
    case XDG_POSITIONER_ANCHOR_RIGHT: case XDG_POSITIONER_ANCHOR_TOP_RIGHT:
    case XDG_POSITIONER_ANCHOR_BOTTOM_RIGHT: px = p->ax + p->aw; break;
    default: px = p->ax + p->aw / 2; break;
    }
    switch (p->gravity) {
    case XDG_POSITIONER_GRAVITY_TOP: case XDG_POSITIONER_GRAVITY_TOP_LEFT:
    case XDG_POSITIONER_GRAVITY_TOP_RIGHT: py -= p->height; break;
    case XDG_POSITIONER_GRAVITY_BOTTOM: case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT:
    case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT: break;
    default: py -= p->height / 2; break;
    }
    switch (p->gravity) {
    case XDG_POSITIONER_GRAVITY_LEFT: case XDG_POSITIONER_GRAVITY_TOP_LEFT:
    case XDG_POSITIONER_GRAVITY_BOTTOM_LEFT: px -= p->width; break;
    case XDG_POSITIONER_GRAVITY_RIGHT: case XDG_POSITIONER_GRAVITY_TOP_RIGHT:
    case XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT: break;
    default: px -= p->width / 2; break;
    }
    *x = px + p->dx;
    *y = py + p->dy;
}

/* ---- xdg_toplevel ---- */

static struct bc_xdg *toplevel_xdg(struct wl_resource *resource)
{
    return wl_resource_get_user_data(resource);
}

static void toplevel_set_parent(struct wl_client *client, struct wl_resource *resource,
                                struct wl_resource *parent)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x == NULL) return;
    {
        struct bc_xdg *p = parent ? toplevel_xdg(parent) : NULL;
        int before = xdg_parent_id(x);
        if (p == x) p = NULL;
        x->parent = p;
        if (x->id && xdg_parent_id(x) != before)
            bc_wire_line(&x->server->wire, "WINDOW parent %u %d", x->id, xdg_parent_id(x));
    }
}

static void toplevel_set_title(struct wl_client *client, struct wl_resource *resource,
                               const char *title)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    char clean[BC_TEXT_BYTES + 1];
    (void)client;
    if (x == NULL) return;
    bc_text_clean(clean, title);
    if (strcmp(clean, x->title) == 0) return;
    memcpy(x->title, clean, sizeof(clean));
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW title %u %s", x->id, x->title);
}

static void toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource,
                                const char *appid)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    char clean[BC_TEXT_BYTES + 1];
    (void)client;
    if (x == NULL) return;
    bc_text_clean(clean, appid);
    if (strcmp(clean, x->appid) == 0) return;
    memcpy(x->appid, clean, sizeof(clean));
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW appid %u %s", x->id, x->appid);
}

static void toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *seat, uint32_t serial, int32_t x,
                                      int32_t y)
{
    (void)client; (void)resource; (void)seat; (void)serial; (void)x; (void)y;
}

static void toplevel_move(struct wl_client *client, struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial)
{
    /* Moving and resizing are the desktop's, through its own frame. */
    (void)client; (void)resource; (void)seat; (void)serial;
}

static void toplevel_resize(struct wl_client *client, struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
    (void)client; (void)resource; (void)seat; (void)serial; (void)edges;
}

static void toplevel_set_max_size(struct wl_client *client, struct wl_resource *resource,
                                  int32_t w, int32_t h)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x == NULL) return;
    if (w < 0 || h < 0) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "max %dx%d", w, h);
        return;
    }
    x->pmax_w = w;
    x->pmax_h = h;
}

static void toplevel_set_min_size(struct wl_client *client, struct wl_resource *resource,
                                  int32_t w, int32_t h)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x == NULL) return;
    if (w < 0 || h < 0) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_SIZE, "min %dx%d", w, h);
        return;
    }
    x->pmin_w = w;
    x->pmin_h = h;
}

/* Maximize and minimize are the program's requests: the desktop decides,
   and answers with WINDOW size (xdg-shell lets a compositor ignore them).
   Until the desktop answers, the current configure stands. */
static void toplevel_maximize(struct bc_xdg *x, int on)
{
    if (x->maximize == on) return;
    x->maximize = on;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW maximize %u %s", x->id, on ? "on" : "off");
}

static void toplevel_set_maximized(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) toplevel_maximize(x, 1);
}

static void toplevel_unset_maximized(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) toplevel_maximize(x, 0);
}

static void toplevel_set_minimized(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x == NULL) return;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW minimize %u", x->id);
    else x->minimize = 1;
}

static void toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
                                    struct wl_resource *output)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client; (void)output;
    if (x == NULL) return;
    x->fullscreen = 1;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW fullscreen %u on", x->id);
}

static void toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x == NULL) return;
    x->fullscreen = 0;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW fullscreen %u off", x->id);
}

static const struct xdg_toplevel_interface toplevel_impl = {
    .destroy = bc_destroy_request,
    .set_parent = toplevel_set_parent,
    .set_title = toplevel_set_title,
    .set_app_id = toplevel_set_app_id,
    .show_window_menu = toplevel_show_window_menu,
    .move = toplevel_move,
    .resize = toplevel_resize,
    .set_max_size = toplevel_set_max_size,
    .set_min_size = toplevel_set_min_size,
    .set_maximized = toplevel_set_maximized,
    .unset_maximized = toplevel_unset_maximized,
    .set_fullscreen = toplevel_set_fullscreen,
    .unset_fullscreen = toplevel_unset_fullscreen,
    .set_minimized = toplevel_set_minimized,
};

static void toplevel_destroyed(struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    struct bc_xdg *other;
    if (x == NULL) return;
    window_gone(x);
    /* Transients name their parent by id only at WINDOW new. */
    wl_list_for_each(other, &x->server->windows, link)
        if (other->parent == x) other->parent = NULL;
    if (x->decoration) wl_resource_set_user_data(x->decoration, NULL);
    x->decoration = NULL;
    x->toplevel = NULL;
}

/* ---- xdg_popup ---- */

static void popup_grab(struct wl_client *client, struct wl_resource *resource,
                       struct wl_resource *seat, uint32_t serial)
{
    /* The desktop sends `WINDOW close` for a click outside; input is
       milestone B. */
    (void)client; (void)resource; (void)seat; (void)serial;
}

static void popup_reposition(struct wl_client *client, struct wl_resource *resource,
                             struct wl_resource *positioner, uint32_t token)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    (void)client;
    if (x == NULL) return;
    x->positioner = *(struct bc_positioner *)wl_resource_get_user_data(positioner);
    positioner_place(&x->positioner, &x->px, &x->py);
    logf_("popup %u: repositioned to %d,%d; the draft has no line for a popup that moves",
          x->id, x->px, x->py);
    if (!x->configured) return;
    xdg_popup_send_repositioned(resource, token);
    xdg_popup_send_configure(resource, x->px, x->py, x->positioner.width, x->positioner.height);
    x->serial = wl_display_next_serial(x->server->display);
    xdg_surface_send_configure(x->resource, x->serial);
}

static const struct xdg_popup_interface popup_impl = {
    .destroy = bc_destroy_request,
    .grab = popup_grab,
    .reposition = popup_reposition,
};

static void popup_destroyed(struct wl_resource *resource)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    if (x == NULL) return;
    window_gone(x);
    x->popup = NULL;
}

/* ---- xdg_surface ---- */

static void xdg_get_toplevel(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    struct wl_array caps;
    uint32_t *cap;
    if (x->toplevel || x->popup) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "already has a role");
        return;
    }
    x->toplevel = wl_resource_create(client, &xdg_toplevel_interface,
                                     wl_resource_get_version(resource), id);
    if (x->toplevel == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(x->toplevel, &toplevel_impl, x, toplevel_destroyed);
    if (wl_resource_get_version(x->toplevel) >= XDG_TOPLEVEL_WM_CAPABILITIES_SINCE_VERSION) {
        /* The window states the draft carries to the desktop. */
        wl_array_init(&caps);
        if ((cap = wl_array_add(&caps, sizeof(*cap))) != NULL)
            *cap = XDG_TOPLEVEL_WM_CAPABILITIES_MAXIMIZE;
        if ((cap = wl_array_add(&caps, sizeof(*cap))) != NULL)
            *cap = XDG_TOPLEVEL_WM_CAPABILITIES_FULLSCREEN;
        if ((cap = wl_array_add(&caps, sizeof(*cap))) != NULL)
            *cap = XDG_TOPLEVEL_WM_CAPABILITIES_MINIMIZE;
        xdg_toplevel_send_wm_capabilities(x->toplevel, &caps);
        wl_array_release(&caps);
    }
}

static void xdg_get_popup(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                          struct wl_resource *parent, struct wl_resource *positioner)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    struct bc_positioner *p = wl_resource_get_user_data(positioner);
    if (x->toplevel || x->popup) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_ALREADY_CONSTRUCTED, "already has a role");
        return;
    }
    if (p->width <= 0 || p->height <= 0 || p->aw <= 0 || p->ah <= 0) {
        wl_resource_post_error(resource, XDG_WM_BASE_ERROR_INVALID_POSITIONER,
                               "a positioner without a size or anchor rectangle");
        return;
    }
    x->popup = wl_resource_create(client, &xdg_popup_interface, wl_resource_get_version(resource), id);
    if (x->popup == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(x->popup, &popup_impl, x, popup_destroyed);
    x->parent = parent ? wl_resource_get_user_data(parent) : NULL;
    x->positioner = *p;
    positioner_place(p, &x->px, &x->py);
    if (x->parent == NULL)
        logf_("a popup with no parent is not shown");
}

static void xdg_set_window_geometry(struct wl_client *client, struct wl_resource *resource,
                                    int32_t gx, int32_t gy, int32_t gw, int32_t gh)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    (void)client;
    if (gw <= 0 || gh <= 0) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_INVALID_SIZE, "geometry %dx%d", gw, gh);
        return;
    }
    x->pgx = gx; x->pgy = gy; x->pgw = gw; x->pgh = gh;
    x->pending_geometry = 1;
}

static void xdg_ack_configure(struct wl_client *client, struct wl_resource *resource,
                              uint32_t serial)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    (void)client;
    /* Any configure since the last (un)map counts: a client mid-resize may
       ack an older one. An ack while none is outstanding (one sent before
       an unmap) is late, not an error. */
    (void)serial;
    if (x->configured)
        x->acked = 1;
}

static const struct xdg_surface_interface xdg_surface_impl = {
    .destroy = bc_destroy_request,
    .get_toplevel = xdg_get_toplevel,
    .get_popup = xdg_get_popup,
    .set_window_geometry = xdg_set_window_geometry,
    .ack_configure = xdg_ack_configure,
};

static void xdg_surface_destroyed(struct wl_resource *resource)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    struct bc_xdg *other;
    window_gone(x);
    if (x->toplevel) {
        struct wl_resource *toplevel = x->toplevel;
        toplevel_destroyed(toplevel);
        wl_resource_set_user_data(toplevel, NULL);
    }
    if (x->popup) wl_resource_set_user_data(x->popup, NULL);
    if (x->decoration) wl_resource_set_user_data(x->decoration, NULL);
    if (x->surface) x->surface->xdg = NULL;
    wl_list_for_each(other, &x->server->windows, link)
        if (other->parent == x) other->parent = NULL;
    wl_list_remove(&x->link);
    free(x);
}

/* ---- xdg_wm_base ---- */

static void wm_create_positioner(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    struct bc_positioner *p = calloc(1, sizeof(*p));
    struct wl_resource *r;
    if (p == NULL) { wl_client_post_no_memory(client); return; }
    r = wl_resource_create(client, &xdg_positioner_interface, wl_resource_get_version(resource), id);
    if (r == NULL) { free(p); wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &positioner_impl, p, positioner_destroyed);
}

static void wm_get_xdg_surface(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                               struct wl_resource *surface_resource)
{
    struct bc_surface *surface = wl_resource_get_user_data(surface_resource);
    struct bc_xdg *x;
    if (surface->role == BC_ROLE_SUBSURFACE || surface->xdg) {
        wl_resource_post_error(resource, XDG_WM_BASE_ERROR_ROLE, "the surface already has a role");
        return;
    }
    if (surface->pending_buffer) {
        wl_resource_post_error(resource, XDG_SURFACE_ERROR_UNCONFIGURED_BUFFER,
                               "the surface has a buffer attached");
        return;
    }
    x = calloc(1, sizeof(*x));
    if (x == NULL) { wl_client_post_no_memory(client); return; }
    x->resource = wl_resource_create(client, &xdg_surface_interface,
                                     wl_resource_get_version(resource), id);
    if (x->resource == NULL) { free(x); wl_client_post_no_memory(client); return; }
    x->server = surface->server;
    x->surface = surface;
    surface->xdg = x;
    surface->role = BC_ROLE_XDG;
    wl_list_insert(&x->server->windows, &x->link);
    wl_resource_set_implementation(x->resource, &xdg_surface_impl, x, xdg_surface_destroyed);
}

static void wm_pong(struct wl_client *client, struct wl_resource *resource, uint32_t serial)
{
    (void)client; (void)resource; (void)serial;
}

static const struct xdg_wm_base_interface wm_base_impl = {
    .destroy = bc_destroy_request,
    .create_positioner = wm_create_positioner,
    .get_xdg_surface = wm_get_xdg_surface,
    .pong = wm_pong,
};

static void bind_wm_base(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &xdg_wm_base_interface, (int)version, id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &wm_base_impl, data, NULL);
}

/* ---- zxdg_decoration_manager_v1: server-side ---- */

static void decoration_mode(struct wl_resource *resource, uint32_t mode)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    if (x == NULL) return;
    if (x->decor_mode != mode) {
        x->decor_mode = mode;
        if (x->id) send_decor(x);
    }
    /* A decoration configure is followed by an xdg_surface configure: at
       once when the toplevel has its size, else with its first one. */
    if (x->configured) send_toplevel_configure(x);
}

static void decoration_set_mode(struct wl_client *client, struct wl_resource *resource, uint32_t mode)
{
    (void)client;
    decoration_mode(resource, mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                  ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                  : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static void decoration_unset_mode(struct wl_client *client, struct wl_resource *resource)
{
    (void)client;
    decoration_mode(resource, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_toplevel_decoration_v1_interface decoration_impl = {
    .destroy = bc_destroy_request,
    .set_mode = decoration_set_mode,
    .unset_mode = decoration_unset_mode,
};

static void decoration_destroyed(struct wl_resource *resource)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    if (x == NULL) return;
    x->decoration = NULL;
    /* Without the object the client decorates itself again, from its next
       commit (xdg-decoration): a toplevel destroyed right after sends no
       such line. */
    x->decor_dropped = 1;
}

static void decoration_manager_get(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t id, struct wl_resource *toplevel)
{
    struct bc_xdg *x = toplevel_xdg(toplevel);
    struct wl_resource *r;
    if (x == NULL) {
        wl_resource_post_error(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ORPHANED, "no toplevel");
        return;
    }
    if (x->decoration) {
        wl_resource_post_error(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_ALREADY_CONSTRUCTED,
                               "the toplevel already has a decoration object");
        return;
    }
    if (x->mapped) {
        wl_resource_post_error(resource, ZXDG_TOPLEVEL_DECORATION_V1_ERROR_UNCONFIGURED_BUFFER,
                               "the toplevel already has a buffer");
        return;
    }
    r = wl_resource_create(client, &zxdg_toplevel_decoration_v1_interface,
                           wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &decoration_impl, x, decoration_destroyed);
    x->decoration = r;
    decoration_mode(r, ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
}

static const struct zxdg_decoration_manager_v1_interface decoration_manager_impl = {
    .destroy = bc_destroy_request,
    .get_toplevel_decoration = decoration_manager_get,
};

static void bind_decoration_manager(struct wl_client *client, void *data, uint32_t version,
                                    uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &zxdg_decoration_manager_v1_interface,
                                               (int)version, id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &decoration_manager_impl, data, NULL);
}

/* ---- wp_presentation ---- */

static void presentation_feedback(struct wl_client *client, struct wl_resource *resource,
                                  struct wl_resource *surface_resource, uint32_t id)
{
    struct bc_surface *surface = wl_resource_get_user_data(surface_resource);
    struct wl_resource *r = wl_resource_create(client, &wp_presentation_feedback_interface, 1, id);
    (void)resource;
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, NULL, NULL, unlink_resource);
    wl_list_insert(surface->pending_feedback.prev, wl_resource_get_link(r));
}

static const struct wp_presentation_interface presentation_impl = {
    .destroy = bc_destroy_request,
    .feedback = presentation_feedback,
};

static void bind_presentation(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wp_presentation_interface, (int)version, id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &presentation_impl, data, NULL);
    wp_presentation_send_clock_id(r, CLOCK_MONOTONIC);
}

/* ---- the desktop's lines ---- */

static struct bc_xdg *window_by_id(struct bc_server *s, uint32_t id)
{
    struct bc_xdg *x;
    wl_list_for_each(x, &s->windows, link)
        if (x->id == id) return x;
    return NULL;
}

/* ---- input: Blit's lines on the input socket, as wl_seat events ----

   Blit connects to the compositor's input socket once per toplevel route
   (`INPUT sock <path> <id>`, sent at WINDOW new) and writes that window's
   input as lines (blit_compositor_wire.h, bc_parse_input). They become:

   - keys: wl_keyboard.key with the evdev code as it came, the xkb state
     (the keymap from Blit's GET /v1/input/keymap, compiled here as every
     client compiles it) giving wl_keyboard.modifiers. The keyboard enters
     the window whose route sends a line, and leaves it when another
     window's route sends one, when the desktop's WINDOW size drops
     `activated` for it, or when its route's socket ends (every key and
     button still down released first: Blit forgets them when a socket
     breaks).
   - the pointer: its position is in the toplevel's surface pixels; a popup
     of that toplevel under it (WINDOW popup's offset, its buffer's size)
     gets it instead, in its own pixels. While a button is down the surface
     it went down on keeps the pointer (an implicit grab). Buttons, the
     wheel (axis, value120 and discrete by the client's version) and
     wl_pointer.frame at Blit's INPUT frame.
   - relative motion: zwp_relative_pointer_v1 (below).

   The keymap is read at start, and again, conditionally (If-None-Match),
   on Blit's notice `INPUT keymap <id> <revision>` (a revision not read for
   yet): Blit sends it on a live route when what GET /v1/input/keymap
   serves has changed -- a keyboard configured, plugged or gone, or a key
   from another keyboard, before that key -- so the key that follows is
   read with the new keymap. The read is synchronous with a short bound
   (BC_KEYMAP_NOTICE_MS on loopback, only on a notice; a read that fails
   keeps the last keymap and is tried again at the next keyboard enter).
   Clients are sent a new keymap at once, and new repeat info when Blit's
   X-Blit-Repeat-Rate and X-Blit-Repeat-Delay change (on a 200 or a 304:
   repeat is not part of the keymap's text); those are the active
   keyboard's repeat, its kernel's (0 0: no repeat). It needs
   BLIT_SERVER_PORT (and BLIT_API_TOKEN when Blit has a token), which every
   program in a Blit session has; without a keymap the keyboard is sent
   `no_keymap` and no repeat, and keys are still delivered. */

#define BC_KEYMAP_BYTES (512 * 1024)
/* How long the keymap is read for: at start, before any client, and on a
   notice, when the event loop waits for it. */
#define BC_KEYMAP_START_MS 1000
#define BC_KEYMAP_NOTICE_MS 250

struct bc_input_conn {
    struct wl_list link;              /* bc_server.input_conns */
    struct bc_server *server;
    int fd;
    struct wl_event_source *source;
    uint32_t id;                      /* the window its lines name */
    char buf[1024];
    size_t len;
};

/* A response header's value into `out` ("" without one). */
static void bc_http_header(const char *head, const char *name, char *out, size_t cap)
{
    const char *p = head;
    size_t n = strlen(name);
    out[0] = '\0';
    while ((p = strstr(p, "\r\n")) != NULL) {
        p += 2;
        if (strncasecmp(p, name, n) == 0 && p[n] == ':') {
            size_t m;
            p += n + 1;
            while (*p == ' ') ++p;
            m = strcspn(p, "\r\n");
            if (m < cap) {
                memcpy(out, p, m);
                out[m] = '\0';
            }
            return;
        }
    }
}

/* A minimal HTTP/1.0 GET of Blit's API on loopback, bounded in size and,
   all of it, by `timeout_ms`. Returns the status (0 when unreachable or
   late), the body (NUL ended, malloc'd, the caller frees) and the header
   block (the status line and headers, NUL ended, cut to head_cap). */
static int bc_http_get(const char *path, const char *if_none_match, int timeout_ms, char **body,
                       size_t *length, char *head, size_t head_cap)
{
    const char *port = getenv("BLIT_SERVER_PORT"), *token = getenv("BLIT_API_TOKEN");
    struct sockaddr_in addr;
    struct timeval tv;
    char request[1024], *buffer = NULL, *head_end;
    size_t used = 0, cap = 0;
    uint64_t deadline = now_us() + (uint64_t)timeout_ms * 1000u;
    int fd, status = 0, n;
    *body = NULL;
    *length = 0;
    head[0] = '\0';
    if (port == NULL || atoi(port) <= 0 || atoi(port) > 65535) return 0;
    fd = socket(AF_INET, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return 0;
    /* The connect and the request on loopback, each within the bound. */
    tv.tv_sec = timeout_ms / 1000;
    tv.tv_usec = (timeout_ms % 1000) * 1000;
    setsockopt(fd, SOL_SOCKET, SO_SNDTIMEO, &tv, sizeof(tv));
    memset(&addr, 0, sizeof(addr));
    addr.sin_family = AF_INET;
    addr.sin_port = htons((uint16_t)atoi(port));
    addr.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
    n = snprintf(request, sizeof(request),
                 "GET %s HTTP/1.0\r\nHost: 127.0.0.1\r\n%s%s%s%s%s%sConnection: close\r\n\r\n", path,
                 token && token[0] ? "Authorization: Bearer " : "", token && token[0] ? token : "",
                 token && token[0] ? "\r\n" : "", if_none_match ? "If-None-Match: " : "",
                 if_none_match ? if_none_match : "", if_none_match ? "\r\n" : "");
    if (n <= 0 || (size_t)n >= sizeof(request) ||
        connect(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 ||
        send(fd, request, (size_t)n, MSG_NOSIGNAL) != n) {
        close(fd);
        return 0;
    }
    /* The answer, until Blit closes the connection or the bound passes. */
    for (;;) {
        struct pollfd pfd = { fd, POLLIN, 0 };
        uint64_t now = now_us();
        ssize_t got;
        if (used + 4096 + 1 > cap) {
            char *grown;
            if (cap >= BC_KEYMAP_BYTES) break;
            cap = cap ? cap * 2 : 65536;
            if ((grown = realloc(buffer, cap)) == NULL) break;
            buffer = grown;
        }
        if (now >= deadline || poll(&pfd, 1, (int)((deadline - now + 999) / 1000)) <= 0) {
            free(buffer);
            close(fd);
            return 0;
        }
        got = recv(fd, buffer + used, cap - used - 1, MSG_DONTWAIT);
        if (got < 0 && (errno == EAGAIN || errno == EINTR)) continue;
        if (got <= 0) break;
        used += (size_t)got;
    }
    close(fd);
    if (buffer == NULL) return 0;
    buffer[used] = '\0';
    if (sscanf(buffer, "HTTP/1.%*d %d", &status) != 1 || (head_end = strstr(buffer, "\r\n\r\n")) == NULL) {
        free(buffer);
        return 0;
    }
    *head_end = '\0';
    snprintf(head, head_cap, "%s\r\n", buffer);
    *length = used - (size_t)(head_end + 4 - buffer);
    memmove(buffer, head_end + 4, *length + 1);
    *body = buffer;
    return status;
}

/* The keymap to every bound keyboard, or `no_keymap` without one. */
static void keyboard_send_keymap(struct bc_server *s, struct wl_resource *r)
{
    if (s->keymap_fd >= 0)
        wl_keyboard_send_keymap(r, WL_KEYBOARD_KEYMAP_FORMAT_XKB_V1, s->keymap_fd, s->keymap_size);
    else {
        int fd = memfd_create("blit-compositor-no-keymap", MFD_CLOEXEC);
        wl_keyboard_send_keymap(r, WL_KEYBOARD_KEYMAP_FORMAT_NO_KEYMAP, fd >= 0 ? fd : 0, 0);
        if (fd >= 0) close(fd);
    }
}

/* Blit's repeat for the active keyboard, from the keymap's headers: sent
   to every keyboard when it changes. */
static void repeat_apply(struct bc_server *s, const char *head)
{
    char rate_text[16], delay_text[16];
    struct wl_resource *r;
    int32_t rate, delay;
    bc_http_header(head, "X-Blit-Repeat-Rate", rate_text, sizeof(rate_text));
    bc_http_header(head, "X-Blit-Repeat-Delay", delay_text, sizeof(delay_text));
    rate = atoi(rate_text);
    delay = atoi(delay_text);
    if (rate < 0 || rate > 1000 || delay < 0 || delay > 60000 || rate == 0) rate = delay = 0;
    if (rate == s->repeat_rate && delay == s->repeat_delay) return;
    s->repeat_rate = rate;
    s->repeat_delay = delay;
    logf_("keymap: repeat %d a second after %d ms, Blit's", rate, delay);
    wl_resource_for_each(r, &s->keyboards)
        if (wl_resource_get_version(r) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
            wl_keyboard_send_repeat_info(r, rate, delay);
}

/* Blit's keymap, read again if it changed (304 otherwise): compiled, put
   in a sealed memfd, and sent to every keyboard; and its repeat. Returns
   0 when Blit answered (200 or 304), -1 otherwise. */
static int keymap_refresh(struct bc_server *s, int timeout_ms)
{
    char *body = NULL, etag[96], head[2048];
    size_t length = 0;
    struct xkb_keymap *keymap;
    struct wl_resource *r;
    unsigned char *map;
    int status, fd;
    if (s->xkb == NULL) return -1;
    status = bc_http_get("/v1/input/keymap", s->keymap_etag[0] ? s->keymap_etag : NULL, timeout_ms,
                         &body, &length, head, sizeof(head));
    bc_http_header(head, "ETag", etag, sizeof(etag));
    if (status == 304) {
        free(body);
        logf_("keymap: unchanged (304)");
        repeat_apply(s, head);
        return 0;
    }
    if (status != 200 || length == 0) {
        if (!s->keymap_warned)
            logf_("keymap: GET /v1/input/keymap answered %d (BLIT_SERVER_PORT %s): %s", status,
                  getenv("BLIT_SERVER_PORT") ? getenv("BLIT_SERVER_PORT") : "unset",
                  s->keymap_fd >= 0 ? "keeping the last one" : "clients get no_keymap");
        s->keymap_warned = 1;
        free(body);
        return -1;
    }
    keymap = xkb_keymap_new_from_string(s->xkb, body, XKB_KEYMAP_FORMAT_TEXT_V1,
                                        XKB_KEYMAP_COMPILE_NO_FLAGS);
    if (keymap == NULL) {
        logf_("keymap: Blit's keymap did not compile (%zu bytes)", length);
        free(body);
        repeat_apply(s, head);
        return 0;
    }
    fd = memfd_create("blit-compositor-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)length + 1) != 0 ||
        (map = mmap(NULL, length + 1, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0)) == MAP_FAILED) {
        if (fd >= 0) close(fd);
        xkb_keymap_unref(keymap);
        free(body);
        return -1;
    }
    memcpy(map, body, length + 1);
    munmap(map, length + 1);
    fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    free(body);
    if (s->keymap_fd >= 0) close(s->keymap_fd);
    if (s->xkb_state) xkb_state_unref(s->xkb_state);
    if (s->keymap) xkb_keymap_unref(s->keymap);
    s->keymap = keymap;
    s->xkb_state = xkb_state_new(keymap);
    s->keymap_fd = fd;
    s->keymap_size = (uint32_t)length + 1;
    snprintf(s->keymap_etag, sizeof(s->keymap_etag), "%s", etag);
    s->mods_sent = 0;
    logf_("keymap: %zu bytes from Blit, ETag %s", length, etag);
    wl_resource_for_each(r, &s->keyboards)
        keyboard_send_keymap(s, r);
    repeat_apply(s, head);
    /* Keys down stay down in the new state (a modifier held across a
       keyboard switch), and the focused client is told its modifiers. */
    if (s->xkb_state) {
        uint32_t code;
        for (code = 0; code < sizeof(s->keys_down) * 8; ++code)
            if (s->keys_down[code / 8] & (1u << (code % 8)))
                xkb_state_update_key(s->xkb_state, code + 8, XKB_KEY_DOWN);
    }
    keyboard_send_modifiers(s, 1);
    return 0;
}

/* Blit's `INPUT keymap <id> <revision>`: what GET /v1/input/keymap serves
   has changed. Read once per revision, whichever route says it first. */
static void keymap_notice(struct bc_server *s, uint64_t revision)
{
    if (revision == s->keymap_revision && !s->keymap_stale) return;
    logf_("keymap: Blit's revision %llu", (unsigned long long)revision);
    if (keymap_refresh(s, BC_KEYMAP_NOTICE_MS) == 0) {
        s->keymap_revision = revision;
        s->keymap_stale = 0;
    } else {
        s->keymap_stale = 1;
    }
}

/* Every resource in `list` (keyboards, pointers) of the surface's client. */
#define seat_for_client(r, list, surface) \
    wl_resource_for_each(r, list) if (wl_resource_get_client(r) == wl_resource_get_client((surface)->resource))

static void keyboard_send_modifiers(struct bc_server *s, int force)
{
    struct wl_resource *r;
    uint32_t dep, lat, lock, group;
    if (s->kbd_focus == NULL || s->xkb_state == NULL) return;
    dep = xkb_state_serialize_mods(s->xkb_state, XKB_STATE_MODS_DEPRESSED);
    lat = xkb_state_serialize_mods(s->xkb_state, XKB_STATE_MODS_LATCHED);
    lock = xkb_state_serialize_mods(s->xkb_state, XKB_STATE_MODS_LOCKED);
    group = xkb_state_serialize_layout(s->xkb_state, XKB_STATE_LAYOUT_EFFECTIVE);
    if (!force && s->mods_sent && dep == s->mods[0] && lat == s->mods[1] && lock == s->mods[2] &&
        group == s->mods[3])
        return;
    s->mods[0] = dep; s->mods[1] = lat; s->mods[2] = lock; s->mods[3] = group;
    s->mods_sent = 1;
    seat_for_client(r, &s->keyboards, s->kbd_focus)
        wl_keyboard_send_modifiers(r, wl_display_next_serial(s->display), dep, lat, lock, group);
}

/* Every key still down released, to the focused client and the xkb state. */
static void keyboard_release_all(struct bc_server *s)
{
    struct wl_resource *r;
    uint32_t code;
    for (code = 0; code < sizeof(s->keys_down) * 8; ++code) {
        if (!(s->keys_down[code / 8] & (1u << (code % 8)))) continue;
        s->keys_down[code / 8] &= (unsigned char)~(1u << (code % 8));
        if (s->xkb_state) xkb_state_update_key(s->xkb_state, code + 8, XKB_KEY_UP);
        if (s->kbd_focus)
            seat_for_client(r, &s->keyboards, s->kbd_focus)
                wl_keyboard_send_key(r, wl_display_next_serial(s->display), now_ms(), code,
                                     WL_KEYBOARD_KEY_STATE_RELEASED);
    }
    keyboard_send_modifiers(s, 0);
}

static void keyboard_focus(struct bc_server *s, struct bc_surface *surface)
{
    struct wl_resource *r;
    struct wl_array keys;
    if (s->kbd_focus == surface) return;
    if (s->kbd_focus) {
        keyboard_release_all(s);
        seat_for_client(r, &s->keyboards, s->kbd_focus)
            wl_keyboard_send_leave(r, wl_display_next_serial(s->display), s->kbd_focus->resource);
        logf_("keyboard: leave window %u", s->kbd_focus->xdg ? s->kbd_focus->xdg->id : 0);
    }
    s->kbd_focus = surface;
    if (surface == NULL) return;
    /* Only after a notice whose read failed: the keymap is read on Blit's
       notice, not at every enter. */
    if (s->keymap_stale && keymap_refresh(s, BC_KEYMAP_NOTICE_MS) == 0) s->keymap_stale = 0;
    wl_array_init(&keys);
    seat_for_client(r, &s->keyboards, surface)
        wl_keyboard_send_enter(r, wl_display_next_serial(s->display), surface->resource, &keys);
    wl_array_release(&keys);
    keyboard_send_modifiers(s, 1);
    logf_("keyboard: enter window %u", surface->xdg ? surface->xdg->id : 0);
}

/* The surface under (x, y) of toplevel `x`, in that surface's pixels: a
   mapped popup of it first, else the toplevel. */
static struct bc_surface *pointer_target(struct bc_server *s, struct bc_xdg *x, double px,
                                         double py, double *lx, double *ly)
{
    struct bc_xdg *p;
    wl_list_for_each(p, &s->windows, link) {
        double ox, oy;
        if (!p->popup || p->parent != x || !p->mapped || !p->id || p->surface == NULL) continue;
        ox = x->gx + p->px - p->gx;
        oy = x->gy + p->py - p->gy;
        if (px >= ox && py >= oy && px < ox + p->buffer_w && py < oy + p->buffer_h) {
            *lx = px - ox;
            *ly = py - oy;
            return p->surface;
        }
    }
    *lx = px;
    *ly = py;
    return x->surface;
}

/* A surface's origin in its toplevel's pixels (the toplevel's is 0, 0). */
static void surface_origin(struct bc_surface *surface, double *ox, double *oy)
{
    struct bc_xdg *p = surface->xdg;
    *ox = *oy = 0;
    if (p && p->popup && p->parent) {
        *ox = p->parent->gx + p->px - p->gx;
        *oy = p->parent->gy + p->py - p->gy;
    }
}

static void pointer_focus(struct bc_server *s, struct bc_surface *surface, double lx, double ly)
{
    struct wl_resource *r;
    if (s->ptr_focus == surface) return;
    if (s->ptr_focus) {
        seat_for_client(r, &s->pointers, s->ptr_focus) {
            wl_pointer_send_leave(r, wl_display_next_serial(s->display), s->ptr_focus->resource);
            if (wl_resource_get_version(r) >= WL_POINTER_FRAME_SINCE_VERSION) wl_pointer_send_frame(r);
        }
    }
    s->ptr_focus = surface;
    if (surface == NULL) return;
    seat_for_client(r, &s->pointers, surface)
        wl_pointer_send_enter(r, wl_display_next_serial(s->display), surface->resource,
                              wl_fixed_from_double(lx), wl_fixed_from_double(ly));
}

/* Relative motion, Blit's raw counts under its relative hold (the
   desktop's ?7701 with `INPUT hold <id> relative`), to the
   zwp_relative_pointer_v1 objects of the client with the pointer -- the
   window's surface, entered where the pointer last was if it has not got
   it. The counts are not accelerated, so both pairs are the same. The
   lock itself is the desktop's (ADR 0198; zwp_pointer_constraints_v1
   below). */
static void relative_motion(struct bc_server *s, struct bc_xdg *x, double dx, double dy,
                            uint64_t time_us)
{
    struct wl_resource *r;
    uint64_t us = time_us ? time_us : (uint64_t)now_ms() * 1000u;
    if (s->ptr_focus == NULL || s->ptr_focus->xdg == NULL ||
        (s->ptr_focus->xdg != x && s->ptr_focus->xdg->parent != x))
        pointer_focus(s, x->surface, s->ptr_x, s->ptr_y);
    if (s->ptr_focus == NULL) return;
    seat_for_client(r, &s->relative_pointers, s->ptr_focus)
        zwp_relative_pointer_v1_send_relative_motion(r, (uint32_t)(us >> 32), (uint32_t)us,
                                                     wl_fixed_from_double(dx), wl_fixed_from_double(dy),
                                                     wl_fixed_from_double(dx), wl_fixed_from_double(dy));
}

static const struct zwp_relative_pointer_v1_interface relative_pointer_impl = {
    .destroy = bc_destroy_request,
};

static void relative_manager_get(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                 struct wl_resource *pointer)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct wl_resource *r = wl_resource_create(client, &zwp_relative_pointer_v1_interface,
                                               wl_resource_get_version(resource), id);
    (void)pointer;
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &relative_pointer_impl, s, unlink_resource);
    wl_list_insert(&s->relative_pointers, wl_resource_get_link(r));
}

static const struct zwp_relative_pointer_manager_v1_interface relative_manager_impl = {
    .destroy = bc_destroy_request,
    .get_relative_pointer = relative_manager_get,
};

static void bind_relative_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &zwp_relative_pointer_manager_v1_interface,
                                               (int)version, id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &relative_manager_impl, data, NULL);
}

/* ---- zwp_pointer_constraints_v1: the lock is the desktop's ----

   docs/api.md, "Pointer lock". A program's lock_pointer or confine_pointer
   is asked of the desktop, `WINDOW lock <id> relative|confine`, with the id
   of the toplevel the surface belongs to (a popup's is its toplevel's: one
   route per toplevel); the constraint waits until the desktop engages its
   hold (ADR 0198: within its gesture window after a click or key in that
   window) and says `WINDOW lock <id> on`, then `locked` or `confined`; the
   desktop's `off` (Release Pointer, a focus change, no gesture yet) sends
   `unlocked` or `unconfined` to an active one and leaves a pending one
   waiting. A oneshot constraint deactivated is defunct (it never activates
   again, and its request is spent: destroying it says nothing); a
   persistent one waits for the next `on`. The program destroying a
   constraint still asked for, or its window going, is `WINDOW lock <id>
   off`. Regions and the cursor hint are not carried: Blit confines to the
   picture's drawn rectangle and keeps a locked pointer still. While a lock
   is active the window is sent no absolute motion (Blit's relative hold
   sends only `INPUT rel`, which reaches zwp_relative_pointer_v1). */

enum { BC_CONSTRAINT_LOCK = 1, BC_CONSTRAINT_CONFINE };

struct bc_constraint {
    struct wl_resource *resource;
    struct bc_server *server;
    struct wl_list link;              /* bc_server.constraints */
    struct bc_surface *surface;       /* NULL once the surface is gone */
    int kind;
    int persistent;
    uint32_t window;                  /* the id `WINDOW lock` asked for, 0 when not asked */
    int active;                       /* locked or confined sent */
    int defunct;                      /* a oneshot deactivated */
};

/* The toplevel a surface belongs to, announced, or NULL. */
static struct bc_xdg *surface_toplevel(struct bc_surface *surface)
{
    struct bc_xdg *x = surface ? surface->xdg : NULL;
    while (x && x->popup && x->parent) x = x->parent;
    return x && x->toplevel && x->id ? x : NULL;
}

static void constraint_ask(struct bc_constraint *c)
{
    struct bc_xdg *x;
    if (c->window || c->defunct || (x = surface_toplevel(c->surface)) == NULL) return;
    c->window = x->id;
    bc_wire_line(&c->server->wire, "WINDOW lock %u %s", c->window,
                 c->kind == BC_CONSTRAINT_LOCK ? "relative" : "confine");
    logf_("window %u: the program asks for a pointer %s (%s)", c->window,
          c->kind == BC_CONSTRAINT_LOCK ? "lock" : "confinement",
          c->persistent ? "persistent" : "oneshot");
}

static void constraint_ask_surface(struct bc_surface *surface)
{
    if (surface->constraint) constraint_ask(surface->constraint);
}

/* The request withdrawn: `WINDOW lock <id> off`, once. */
static void constraint_withdraw(struct bc_constraint *c)
{
    if (c->window == 0) return;
    bc_wire_line(&c->server->wire, "WINDOW lock %u off", c->window);
    logf_("window %u: the program let the pointer go", c->window);
    c->window = 0;
    c->active = 0;
}

/* The surface gone: its constraint, inert until the client destroys it. */
static void constraint_surface_gone(struct bc_surface *surface)
{
    if (surface->constraint) {
        constraint_withdraw(surface->constraint);
        surface->constraint->surface = NULL;
        surface->constraint = NULL;
    }
}

/* A window going ends the requests of every constraint asked for it, and of
   the surface's own (a popup's). */
static void constraints_window_gone(struct bc_xdg *x)
{
    struct bc_constraint *c;
    wl_list_for_each(c, &x->server->constraints, link)
        if ((x->id && c->window == x->id) || (x->surface && c->surface == x->surface))
            constraint_withdraw(c);
}

/* Whether window `x` has an active lock: it is sent no absolute motion. */
static int window_locked(struct bc_server *s, struct bc_xdg *x)
{
    struct bc_constraint *c;
    wl_list_for_each(c, &s->constraints, link)
        if (c->active && c->kind == BC_CONSTRAINT_LOCK && c->window == x->id) return 1;
    return 0;
}

static void pointer_focus(struct bc_server *s, struct bc_surface *surface, double lx, double ly);

/* `WINDOW lock <id> on|off`, the desktop's hold engaged or ended. */
static void constraints_hold(struct bc_server *s, uint32_t id, int on)
{
    struct bc_constraint *c;
    int found = 0;
    wl_list_for_each(c, &s->constraints, link) {
        if (c->window != id || c->defunct) continue;
        found = 1;
        if (on && !c->active && c->surface) {
            c->active = 1;
            /* A lock is the surface's with the pointer on it. */
            if (c->kind == BC_CONSTRAINT_LOCK && s->ptr_focus != c->surface)
                pointer_focus(s, c->surface, s->ptr_x, s->ptr_y);
            if (c->kind == BC_CONSTRAINT_LOCK) zwp_locked_pointer_v1_send_locked(c->resource);
            else zwp_confined_pointer_v1_send_confined(c->resource);
            logf_("window %u: pointer %s", id, c->kind == BC_CONSTRAINT_LOCK ? "locked" : "confined");
        } else if (!on && c->active) {
            c->active = 0;
            if (c->kind == BC_CONSTRAINT_LOCK) zwp_locked_pointer_v1_send_unlocked(c->resource);
            else zwp_confined_pointer_v1_send_unconfined(c->resource);
            if (!c->persistent) {
                /* Spent: the desktop's hold has ended, and so has the request. */
                c->defunct = 1;
                c->window = 0;
            }
            logf_("window %u: pointer %s%s", id, c->kind == BC_CONSTRAINT_LOCK ? "unlocked" : "unconfined",
                  c->defunct ? ", the oneshot constraint is defunct" : "");
        }
    }
    if (!found) logf_("WINDOW lock %u %s with no constraint asked for", id, on ? "on" : "off");
}

static void constraint_destroyed(struct wl_resource *resource)
{
    struct bc_constraint *c = wl_resource_get_user_data(resource);
    constraint_withdraw(c);
    if (c->surface && c->surface->constraint == c) c->surface->constraint = NULL;
    wl_list_remove(&c->link);
    free(c);
}

static void constraint_set_region(struct wl_client *client, struct wl_resource *resource,
                                  struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

static void locked_set_hint(struct wl_client *client, struct wl_resource *resource, wl_fixed_t x,
                            wl_fixed_t y)
{
    (void)client; (void)resource; (void)x; (void)y;
}

static const struct zwp_locked_pointer_v1_interface locked_pointer_impl = {
    .destroy = bc_destroy_request,
    .set_cursor_position_hint = locked_set_hint,
    .set_region = constraint_set_region,
};

static const struct zwp_confined_pointer_v1_interface confined_pointer_impl = {
    .destroy = bc_destroy_request,
    .set_region = constraint_set_region,
};

static void constraint_create(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                              struct wl_resource *surface_resource, uint32_t lifetime, int kind)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct bc_surface *surface = wl_resource_get_user_data(surface_resource);
    struct bc_constraint *c;
    if (surface->constraint) {
        wl_resource_post_error(resource, ZWP_POINTER_CONSTRAINTS_V1_ERROR_ALREADY_CONSTRAINED,
                               "the surface already has a pointer constraint");
        return;
    }
    c = calloc(1, sizeof(*c));
    if (c == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    c->resource = kind == BC_CONSTRAINT_LOCK
        ? wl_resource_create(client, &zwp_locked_pointer_v1_interface, wl_resource_get_version(resource), id)
        : wl_resource_create(client, &zwp_confined_pointer_v1_interface, wl_resource_get_version(resource), id);
    if (c->resource == NULL) {
        free(c);
        wl_client_post_no_memory(client);
        return;
    }
    c->server = s;
    c->surface = surface;
    c->kind = kind;
    c->persistent = lifetime == ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT;
    wl_list_insert(&s->constraints, &c->link);
    surface->constraint = c;
    if (kind == BC_CONSTRAINT_LOCK)
        wl_resource_set_implementation(c->resource, &locked_pointer_impl, c, constraint_destroyed);
    else
        wl_resource_set_implementation(c->resource, &confined_pointer_impl, c, constraint_destroyed);
    constraint_ask(c);
}

static void constraints_lock(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                             struct wl_resource *surface, struct wl_resource *pointer,
                             struct wl_resource *region, uint32_t lifetime)
{
    (void)pointer; (void)region;
    constraint_create(client, resource, id, surface, lifetime, BC_CONSTRAINT_LOCK);
}

static void constraints_confine(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                struct wl_resource *surface, struct wl_resource *pointer,
                                struct wl_resource *region, uint32_t lifetime)
{
    (void)pointer; (void)region;
    constraint_create(client, resource, id, surface, lifetime, BC_CONSTRAINT_CONFINE);
}

static const struct zwp_pointer_constraints_v1_interface constraints_impl = {
    .destroy = bc_destroy_request,
    .lock_pointer = constraints_lock,
    .confine_pointer = constraints_confine,
};

static void bind_constraints(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &zwp_pointer_constraints_v1_interface,
                                               (int)version, id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &constraints_impl, data, NULL);
}

static void input_line(struct bc_server *s, struct bc_input_conn *conn, const char *line)
{
    BcInput in;
    struct bc_xdg *x;
    struct wl_resource *r;
    if (bc_parse_input(line, &in) == BC_INPUT_UNKNOWN) {
        logf_("input: a line that is not one: %.80s", line);
        return;
    }
    conn->id = in.id;
    if (in.kind == BC_INPUT_KEYMAP) {
        keymap_notice(s, in.revision);
        return;
    }
    x = window_by_id(s, in.id);
    if (x == NULL || x->toplevel == NULL || x->surface == NULL) return;  /* gone meanwhile */
    switch (in.kind) {
    case BC_INPUT_KEY: {
        uint32_t bit = 1u << (in.code % 8);
        int down = (s->keys_down[in.code / 8] & bit) != 0;
        keyboard_focus(s, x->surface);
        if (in.pressed == down) return;  /* a release of a key never pressed here */
        if (in.pressed) s->keys_down[in.code / 8] |= (unsigned char)bit;
        else s->keys_down[in.code / 8] &= (unsigned char)~bit;
        if (s->xkb_state)
            xkb_state_update_key(s->xkb_state, in.code + 8, in.pressed ? XKB_KEY_DOWN : XKB_KEY_UP);
        seat_for_client(r, &s->keyboards, x->surface)
            wl_keyboard_send_key(r, wl_display_next_serial(s->display), now_ms(), in.code,
                                 in.pressed ? WL_KEYBOARD_KEY_STATE_PRESSED
                                            : WL_KEYBOARD_KEY_STATE_RELEASED);
        keyboard_send_modifiers(s, 0);
        return;
    }
    case BC_INPUT_PTR: {
        double lx, ly, ox, oy;
        struct bc_surface *target;
        /* A locked pointer does not move: only relative motion. */
        if (window_locked(s, x)) return;
        if (s->buttons_down && s->ptr_focus) {
            /* The implicit grab: the surface a button went down on. */
            target = s->ptr_focus;
            surface_origin(target, &ox, &oy);
            lx = in.x - ox;
            ly = in.y - oy;
        } else {
            target = pointer_target(s, x, in.x, in.y, &lx, &ly);
        }
        if (target != s->ptr_focus) {
            pointer_focus(s, target, lx, ly);
        } else {
            seat_for_client(r, &s->pointers, target)
                wl_pointer_send_motion(r, now_ms(), wl_fixed_from_double(lx), wl_fixed_from_double(ly));
        }
        s->ptr_x = lx;
        s->ptr_y = ly;
        s->pointer_batch = 1;
        return;
    }
    case BC_INPUT_BTN:
        if (in.code < 0x110 || in.code >= 0x110 + 32) return;
        if (in.pressed) s->buttons_down |= 1u << (in.code - 0x110);
        else s->buttons_down &= ~(1u << (in.code - 0x110));
        if (s->ptr_focus == NULL) return;
        seat_for_client(r, &s->pointers, s->ptr_focus)
            wl_pointer_send_button(r, wl_display_next_serial(s->display), now_ms(), in.code,
                                   in.pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                              : WL_POINTER_BUTTON_STATE_RELEASED);
        s->pointer_batch = 1;
        return;
    case BC_INPUT_WHEEL: {
        int axis;
        if (s->ptr_focus == NULL) return;
        for (axis = 0; axis < 2; ++axis) {
            double notches = axis == 0 ? in.y : in.x;
            uint32_t which = axis == 0 ? WL_POINTER_AXIS_VERTICAL_SCROLL : WL_POINTER_AXIS_HORIZONTAL_SCROLL;
            if (notches == 0) continue;
            seat_for_client(r, &s->pointers, s->ptr_focus) {
                int v = wl_resource_get_version(r);
                if (v >= WL_POINTER_AXIS_SOURCE_SINCE_VERSION)
                    wl_pointer_send_axis_source(r, WL_POINTER_AXIS_SOURCE_WHEEL);
                if (v >= WL_POINTER_AXIS_VALUE120_SINCE_VERSION)
                    wl_pointer_send_axis_value120(r, which, (int32_t)(notches * 120));
                else if (v >= WL_POINTER_AXIS_DISCRETE_SINCE_VERSION)
                    wl_pointer_send_axis_discrete(r, which, (int32_t)notches);
                wl_pointer_send_axis(r, now_ms(), which, wl_fixed_from_double(notches * 15.0));
            }
        }
        s->pointer_batch = 1;
        return;
    }
    case BC_INPUT_REL:
        relative_motion(s, x, in.x, in.y, in.time_us);
        s->pointer_batch = 1;
        return;
    case BC_INPUT_FRAME:
        if (s->pointer_batch && s->ptr_focus)
            seat_for_client(r, &s->pointers, s->ptr_focus)
                if (wl_resource_get_version(r) >= WL_POINTER_FRAME_SINCE_VERSION)
                    wl_pointer_send_frame(r);
        s->pointer_batch = 0;
        return;
    default:
        return;
    }
}

static void input_conn_close(struct bc_input_conn *conn)
{
    struct bc_server *s = conn->server;
    struct bc_xdg *x = conn->id ? window_by_id(s, conn->id) : NULL;
    /* Blit forgets what a broken route held: it is released here. */
    if (x && x->surface && s->kbd_focus == x->surface) keyboard_focus(s, NULL);
    if (x && x->surface && s->buttons_down) {
        struct wl_resource *r;
        uint32_t b;
        for (b = 0; b < 32; ++b)
            if ((s->buttons_down & (1u << b)) && s->ptr_focus)
                seat_for_client(r, &s->pointers, s->ptr_focus)
                    wl_pointer_send_button(r, wl_display_next_serial(s->display), now_ms(), 0x110 + b,
                                           WL_POINTER_BUTTON_STATE_RELEASED);
        s->buttons_down = 0;
    }
    logf_("input: Blit's route for window %u ended", conn->id);
    wl_event_source_remove(conn->source);
    close(conn->fd);
    wl_list_remove(&conn->link);
    free(conn);
}

static int on_input_conn(int fd, uint32_t mask, void *data)
{
    struct bc_input_conn *conn = data;
    for (;;) {
        ssize_t got = recv(fd, conn->buf + conn->len, sizeof(conn->buf) - 1 - conn->len, MSG_DONTWAIT);
        char *line, *nl;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            input_conn_close(conn);
            return 0;
        }
        conn->len += (size_t)got;
        conn->buf[conn->len] = '\0';
        line = conn->buf;
        while ((nl = strchr(line, '\n')) != NULL) {
            *nl = '\0';
            input_line(conn->server, conn, line);
            line = nl + 1;
        }
        conn->len -= (size_t)(line - conn->buf);
        memmove(conn->buf, line, conn->len);
        if (conn->len == sizeof(conn->buf) - 1) {
            logf_("input: a line longer than %zu bytes; the route is dropped", sizeof(conn->buf));
            input_conn_close(conn);
            return 0;
        }
    }
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) input_conn_close(conn);
    return 0;
}

static int on_input_listen(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    struct bc_input_conn *conn;
    int c;
    (void)mask;
    while ((c = accept4(fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC)) >= 0) {
        conn = calloc(1, sizeof(*conn));
        if (conn == NULL) {
            close(c);
            continue;
        }
        conn->server = s;
        conn->fd = c;
        conn->source = wl_event_loop_add_fd(s->loop, c, WL_EVENT_READABLE, on_input_conn, conn);
        wl_list_insert(&s->input_conns, &conn->link);
    }
    return 0;
}

/* $XDG_RUNTIME_DIR/<display>-input, a stream socket only this user (and
   root) can reach: the directory is the user's alone. */
static int input_listen(struct bc_server *s)
{
    struct sockaddr_un addr;
    int fd;
    if (snprintf(s->input_path, sizeof(s->input_path), "%s/%s-input", getenv("XDG_RUNTIME_DIR"),
                 s->socket_name) >= (int)sizeof(s->input_path)) {
        s->input_path[0] = '\0';
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, s->input_path, strlen(s->input_path));
    /* The display's lock is ours: a socket left at this name is a dead
       compositor's. */
    unlink(s->input_path);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 16) != 0) {
        logf_("input socket %s: %s", s->input_path, strerror(errno));
        if (fd >= 0) close(fd);
        s->input_path[0] = '\0';
        return -1;
    }
    chmod(s->input_path, 0600);
    s->input_fd = fd;
    s->input_source = wl_event_loop_add_fd(s->loop, fd, WL_EVENT_READABLE, on_input_listen, s);
    return 0;
}

/* `ERROR <id> <code>` for a frame. Replies come in the order frames are
   read and an accepted frame gets none, so a refusal is the newest frame
   of the picture -- an FRM3 still held, unless an FRM1 went after it
   (blit-gamescope's rule). ENOTSUP (Blit cannot import now) copies that
   frame as FRM1 and every later dmabuf frame of the connection (ADR
   0223); any other code gives the buffer back unshown. */
static void frame_refused(struct bc_server *s, uint32_t id, const char *code)
{
    struct bc_hold *h, *newest = NULL;
    struct bc_xdg *x = window_by_id(s, id);
    wl_list_for_each(h, &s->holds, link)
        if (h->id == id && (newest == NULL || h->sequence > newest->sequence)) newest = h;
    if (newest == NULL || (x && newest->sequence < x->last_copy)) {
        logf_("a frame of %u was refused: %s", id, code);
        return;
    }
    ++s->wire.dmabuf_refused;
    if (strcmp(code, "ENOTSUP") == 0) {
        if (!s->wire.refused)
            logf_("Blit cannot import dmabuf frames now (ENOTSUP): copying them from here on");
        s->wire.refused = 1;
        if (x && newest->buffer) {
            if (dmabuf_copy(s, id, newest->buffer) == 0) x->last_copy = s->wire.sequence;
            else logf_("window %u: the refused dmabuf frame is not shown: %s", id, s->wire.last_error);
        }
    } else {
        logf_("window %u: a dmabuf frame was refused %s, not shown", id, code);
    }
    hold_end(newest);
    /* The newest frame is settled either way: the client may draw again. */
    if (x) frames_taken(x->surface);
}

static void handle_line(struct bc_server *s, const char *line)
{
    BcLine l;
    struct bc_xdg *x;
    switch (bc_parse_line(line, &l)) {
    case BC_LINE_SIZE:
        x = window_by_id(s, l.id);
        if (x == NULL || x->toplevel == NULL) {
            logf_("WINDOW size for %u, which is no toplevel here", l.id);
            return;
        }
        x->have_size = 1;
        x->size_w = l.width;
        x->size_h = l.height;
        x->size_fullscreen = l.fullscreen;
        x->size_maximized = l.maximized;
        x->size_activated = l.activated;
        /* The desktop took the window's focus away: so does the keyboard. */
        if (!l.activated && x->surface && s->kbd_focus == x->surface) keyboard_focus(s, NULL);
        logf_("window %u: size %dx%d%s%s%s", l.id, l.width, l.height,
              l.fullscreen ? " fullscreen" : "", l.maximized ? " maximized" : "",
              l.activated ? " activated" : "");
        configure_if_ready(x);
        return;
    case BC_LINE_OUTPUT:
        x = window_by_id(s, l.id);
        if (x == NULL || x->toplevel == NULL) {
            logf_("WINDOW output for %u, which is no toplevel here", l.id);
            return;
        }
        {
            struct bc_output *o = output_for_mode(s, l.width, l.height, l.refresh_mhz);
            if (o == NULL) {
                logf_("window %u: no output for %dx%d at %u mHz", l.id, l.width, l.height,
                      l.refresh_mhz);
                return;
            }
            logf_("window %u: output %dx%d at %u mHz", l.id, l.width, l.height, l.refresh_mhz);
            window_move_output(s, x, o);
        }
        return;
    case BC_LINE_CLOSE:
        x = window_by_id(s, l.id);
        if (x && x->toplevel) {
            logf_("window %u: close", l.id);
            xdg_toplevel_send_close(x->toplevel);
        } else if (x && x->popup) {
            logf_("popup %u: done", l.id);
            xdg_popup_send_popup_done(x->popup);
        } else {
            logf_("WINDOW close for %u, which is no window here", l.id);
        }
        return;
    case BC_LINE_LOCK:
        constraints_hold(s, l.id, l.on);
        return;
    case BC_LINE_ERROR_WINDOW:
        logf_("the desktop refused a WINDOW line for %u: %s", l.id, l.code);
        return;
    case BC_LINE_ERROR:
        frame_refused(s, l.id, l.code);
        return;
    case BC_LINE_RELEASE: {
        struct bc_hold *h;
        wl_list_for_each(h, &s->holds, link)
            if (h->id == l.id && h->sequence == l.sequence) {
                struct bc_hold *other;
                ++s->wire.dmabuf_released;
                hold_end(h);
                /* Blit drew a newer frame of the window: it is taken. */
                wl_list_for_each(other, &s->holds, link)
                    if (other->id == l.id && other->sequence > l.sequence) {
                        x = window_by_id(s, l.id);
                        if (x) frames_taken(x->surface);
                        break;
                    }
                return;
            }
        /* A frame given back already, its picture gone. */
        return;
    }
    case BC_LINE_OTHER:
        if (strncmp(line, "ERROR INPUT ", 12) == 0)
            logf_("Blit refused or ended an input route: %.60s", line);
        return;
    case BC_LINE_OK_WINDOW:
        return;
    default:
        logf_("an unknown line from the channel: %.80s", line);
        return;
    }
}

static int on_frame_socket(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    char buf[1024];
    for (;;) {
        ssize_t got = recv(fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        char *line, *next;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            logf_("the frame channel closed: the desktop is gone");
            s->status = 1;
            wl_display_terminate(s->display);
            return 0;
        }
        buf[got] = '\0';
        for (line = buf; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next) *next++ = '\0';
            handle_line(s, line);
        }
    }
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) {
        logf_("the frame channel hung up");
        s->status = 1;
        wl_display_terminate(s->display);
    }
    return 0;
}

static int on_signal(int signal_number, void *data)
{
    struct bc_server *s = data;
    logf_("signal %d: stopping", signal_number);
    wl_display_terminate(s->display);
    return 0;
}

static void usage(FILE *out)
{
    fprintf(out, "usage: blit-compositor [--log path]\n"
                 "  Blit's Wayland compositor for programs (ADR 0228). Needs BLIT_FRAME_SOCKET,\n"
                 "  BLIT_FRAME_TOKEN and XDG_RUNTIME_DIR; listens on $XDG_RUNTIME_DIR/blit-<n>.\n");
}

int main(int argc, char **argv)
{
    struct bc_server server;
    const char *socket_env = getenv("BLIT_FRAME_SOCKET");
    const char *token = getenv("BLIT_FRAME_TOKEN");
    const char *log_path = NULL;
    int i;

    memset(&server, 0, sizeof(server));
    g_server = &server;
    bc_wire_init(&server.wire);
    wl_list_init(&server.windows);
    wl_list_init(&server.outputs);
    wl_list_init(&server.holds);
    server.format_table_fd = -1;
    wl_list_init(&server.input_conns);
    wl_list_init(&server.keyboards);
    wl_list_init(&server.pointers);
    wl_list_init(&server.relative_pointers);
    wl_list_init(&server.constraints);
    server.input_fd = -1;
    server.keymap_fd = -1;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (strcmp(argv[i], "--help") == 0 || strcmp(argv[i], "-h") == 0) {
            usage(stdout);
            return 0;
        } else if (strcmp(argv[i], "--version") == 0) {
            printf("blit-compositor %s\n", BLIT_BUILD_VERSION);
            return 0;
        } else {
            usage(stderr);
            return 2;
        }
    }
    if (log_path && (server.log = fopen(log_path, "a")) == NULL) {
        fprintf(stderr, "blit-compositor: --log %s: %s\n", log_path, strerror(errno));
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    logf_("version %s, libwayland %s", BLIT_BUILD_VERSION, WAYLAND_VERSION);
    if (!socket_env || !token || !getenv("XDG_RUNTIME_DIR")) {
        logf_("BLIT_FRAME_SOCKET, BLIT_FRAME_TOKEN and XDG_RUNTIME_DIR are required");
        return 1;
    }
    if (bc_wire_connect(&server.wire, socket_env, token) != 0) {
        logf_("frame channel %s: %s", socket_env, server.wire.last_error);
        return 1;
    }
    logf_("frame channel %s: %s", socket_env, server.wire.answer);
    if (!server.wire.window)
        logf_("the peer answered without window=1: it places no windows, so none is shown "
              "(Blit reached directly answers window=0; the desktop's channel is needed)");

    server.display = wl_display_create();
    if (server.display == NULL) {
        logf_("wl_display_create failed");
        return 1;
    }
    for (i = 0; i < BC_SOCKETS; ++i) {
        snprintf(server.socket_name, sizeof(server.socket_name), "blit-%d", i);
        if (wl_display_add_socket(server.display, server.socket_name) == 0) break;
    }
    if (i == BC_SOCKETS) {
        logf_("no free socket blit-0 to blit-%d in %s", BC_SOCKETS - 1, getenv("XDG_RUNTIME_DIR"));
        wl_display_destroy(server.display);
        return 1;
    }
    server.loop = wl_display_get_event_loop(server.display);
    server.xkb = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
    keymap_refresh(&server, BC_KEYMAP_START_MS);
    input_listen(&server);
    if (wl_display_init_shm(server.display) != 0 ||
        !wl_global_create(server.display, &wl_compositor_interface,
                          BC_VERSION(6, wl_compositor_interface), &server, bind_compositor) ||
        !wl_global_create(server.display, &wl_subcompositor_interface,
                          BC_VERSION(1, wl_subcompositor_interface), &server, bind_subcompositor) ||
        output_create(&server, 1920, 1080, 60000) == NULL ||
        !wl_global_create(server.display, &wl_seat_interface,
                          BC_VERSION(8, wl_seat_interface), &server, bind_seat) ||
        !wl_global_create(server.display, &xdg_wm_base_interface,
                          BC_VERSION(6, xdg_wm_base_interface), &server, bind_wm_base) ||
        !wl_global_create(server.display, &zxdg_decoration_manager_v1_interface,
                          BC_VERSION(1, zxdg_decoration_manager_v1_interface), &server,
                          bind_decoration_manager) ||
        !wl_global_create(server.display, &wp_presentation_interface,
                          BC_VERSION(1, wp_presentation_interface), &server, bind_presentation) ||
        !wl_global_create(server.display, &zwp_relative_pointer_manager_v1_interface,
                          BC_VERSION(1, zwp_relative_pointer_manager_v1_interface), &server,
                          bind_relative_manager) ||
        !wl_global_create(server.display, &zwp_pointer_constraints_v1_interface,
                          BC_VERSION(1, zwp_pointer_constraints_v1_interface), &server,
                          bind_constraints)) {
        logf_("could not create the globals");
        wl_display_destroy(server.display);
        return 1;
    }
    /* linux-dmabuf only for a peer that imports: dmabuf=1 and its list. */
    if (server.wire.dmabuf) {
        server.format_table_fd = dmabuf_format_table(&server.wire);
        server.main_device = dmabuf_main_device();
        if (server.format_table_fd < 0 ||
            !wl_global_create(server.display, &zwp_linux_dmabuf_v1_interface,
                              BC_VERSION(4, zwp_linux_dmabuf_v1_interface), &server, bind_dmabuf)) {
            logf_("could not offer linux-dmabuf: frames by wl_shm only");
        } else {
            logf_("zwp_linux_dmabuf_v1 %d: %d format and modifier pairs from the hello",
                  BC_VERSION(4, zwp_linux_dmabuf_v1_interface), server.wire.format_count);
        }
    } else {
        logf_("no linux-dmabuf: the peer imports no dmabuf (no dmabuf=1 in its answer)");
    }
    logf_("globals: wl_compositor %d, wl_subcompositor %d, wl_shm 1, wl_output %d, wl_seat %d, "
          "xdg_wm_base %d, zxdg_decoration_manager_v1 %d, wp_presentation %d, "
          "zwp_relative_pointer_manager_v1 %d, zwp_pointer_constraints_v1 %d",
          BC_VERSION(6, wl_compositor_interface), BC_VERSION(1, wl_subcompositor_interface),
          BC_VERSION(4, wl_output_interface), BC_VERSION(8, wl_seat_interface),
          BC_VERSION(6, xdg_wm_base_interface), BC_VERSION(1, zxdg_decoration_manager_v1_interface),
          BC_VERSION(1, wp_presentation_interface),
          BC_VERSION(1, zwp_relative_pointer_manager_v1_interface),
          BC_VERSION(1, zwp_pointer_constraints_v1_interface));
    server.frame_source = wl_event_loop_add_fd(server.loop, server.wire.fd, WL_EVENT_READABLE,
                                               on_frame_socket, &server);
    server.sigterm_source = wl_event_loop_add_signal(server.loop, SIGTERM, on_signal, &server);
    server.sigint_source = wl_event_loop_add_signal(server.loop, SIGINT, on_signal, &server);
    logf_("listening on %s/%s", getenv("XDG_RUNTIME_DIR"), server.socket_name);
    bc_wire_line(&server.wire, "WINDOW display %s", server.socket_name);

    wl_display_run(server.display);

    while (!wl_list_empty(&server.input_conns)) {
        struct bc_input_conn *conn = wl_container_of(server.input_conns.next, conn, link);
        wl_event_source_remove(conn->source);
        close(conn->fd);
        wl_list_remove(&conn->link);
        free(conn);
    }
    if (server.input_source) wl_event_source_remove(server.input_source);
    if (server.input_fd >= 0) {
        close(server.input_fd);
        unlink(server.input_path);
    }
    wl_display_destroy_clients(server.display);
    if (server.frame_source) wl_event_source_remove(server.frame_source);
    if (server.sigterm_source) wl_event_source_remove(server.sigterm_source);
    if (server.sigint_source) wl_event_source_remove(server.sigint_source);
    wl_display_destroy(server.display);
    while (!wl_list_empty(&server.holds)) {
        struct bc_hold *h = wl_container_of(server.holds.next, h, link);
        wl_list_remove(&h->link);
        free(h);
    }
    if (server.format_table_fd >= 0) close(server.format_table_fd);
    if (server.keymap_fd >= 0) close(server.keymap_fd);
    if (server.xkb_state) xkb_state_unref(server.xkb_state);
    if (server.keymap) xkb_keymap_unref(server.keymap);
    if (server.xkb) xkb_context_unref(server.xkb);
    while (!wl_list_empty(&server.outputs)) {
        struct bc_output *o = output_default(&server);
        wl_list_remove(&o->link);
        free(o);
    }
    logf_("stopped: %lu frames sent, %lu failed, %lu WINDOW lines; dmabuf %lu sent, %lu released, "
          "%lu refused, %lu copied", server.wire.frames_sent, server.wire.frames_failed,
          server.wire.lines_sent, server.wire.dmabuf_sent, server.wire.dmabuf_released,
          server.wire.dmabuf_refused, server.wire.dmabuf_copied);
    if (server.wire.fd >= 0) close(server.wire.fd);
    if (server.log) fclose(server.log);
    return server.status;
}
