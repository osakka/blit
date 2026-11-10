/* blit-compositor: Blit's Wayland compositor for programs (ADR 0228,
   phase 1, milestones A, B and C).

     blit-compositor [--display name] [--log path]

   One per desktop session, started by the desktop with the
   BLIT_FRAME_SOCKET/BLIT_FRAME_TOKEN of the frame channel it serves. It
   connects there as a version 5 producer (blit_compositor_wire.h), listens
   on $XDG_RUNTIME_DIR/<name> (--display's, else blit-<n>, the first free n)
   and says that name with `WINDOW display <name>`; the desktop puts it in
   its programs' environment as WAYLAND_DISPLAY.

   Its programs survive a desktop restart (the restart section): when the
   channel closes, the compositor keeps every client and window, reconnects
   to the same socket and token with backoff for a bounded time, and
   announces every window again to the desktop that answers.

   It composites nothing. Each xdg_toplevel and each xdg_popup is one
   picture by descriptor whose id is a WINDOW id (docs/api.md, "Program
   windows"): the compositor tells the desktop about the window and its
   hints, the desktop answers `WINDOW size`, and the compositor configures
   the client at exactly that size. A committed wl_shm buffer becomes one
   FRM1 frame of that picture, copied, and is released at once; a dmabuf
   one becomes one FRM3 of its own planes, released at Blit's
   `FRAME release` (the dmabuf section). Frame callbacks and presentation
   feedback follow when Blit has taken the frame, at most once per output
   refresh (the frame pacing section). Input comes straight from Blit to the compositor's
   input socket as lines and becomes wl_seat events (the input section).

   Globals: wl_compositor, wl_subcompositor (each subsurface a picture of
   its own, `WINDOW sub`: the subsurface section), wl_shm (ARGB8888, XRGB8888, libwayland's own),
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
#include "primary-selection-unstable-v1-server-protocol.h"
#include "cursor-shape-v1-server-protocol.h"
#include "blit_compositor_wire.h"

#ifndef BLIT_BUILD_VERSION
#define BLIT_BUILD_VERSION "dev"
#endif

/* The version offered: the one ADR 0228 names, bounded by the generated
   interface (the build host's XML). */
#define BC_VERSION(want, iface) ((want) < (iface).version ? (want) : (iface).version)
#define BC_SOCKETS 32

enum { BC_ROLE_NONE = 0, BC_ROLE_XDG, BC_ROLE_SUBSURFACE };
enum { BC_LINK_UP = 0, BC_LINK_DOWN };

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

/* A wl_region: its add and subtract rectangles in order. A point is in it
   when the last rectangle holding it was added. */
struct bc_rect_op {
    int32_t x, y, w, h;
    int add;
};

struct bc_region {
    struct bc_rect_op *ops;
    int count;
};

struct bc_surface {
    struct wl_resource *resource;
    struct bc_server *server;
    int role;
    struct bc_xdg *xdg;               /* while it has an xdg_surface */
    struct bc_xdg *sub;               /* while it is a subsurface (its picture) */
    /* Its subsurfaces, bottom to top, with `self` among them where this
       surface's own picture is (the subsurface section). */
    struct wl_list subs;
    struct wl_list self;
    /* The input region, applied at commit: all of the surface when `input_all`. */
    struct bc_region input, pending_input;
    int input_all, pending_input_all, pending_input_set;
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
    int cursor_logged;                /* a cursor image from it was noted once */
    struct bc_constraint *constraint; /* its pointer lock or confinement, one at most */
};

struct bc_positioner {
    int32_t width, height;
    int32_t ax, ay, aw, ah;
    uint32_t anchor, gravity;
    int32_t dx, dy;
};

struct bc_xdg {
    /* A toplevel's own input socket, <display>-input-<id> (the input
       section): -1 until it is announced, closed when it goes. */
    int input_fd;
    struct wl_event_source *input_source;
    char input_path[108];
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
    int hidden;                       /* unmapped by its client (WINDOW hide) */
    uint32_t serial;
    /* window geometry, double-buffered */
    int32_t gx, gy, gw, gh, pgx, pgy, pgw, pgh;
    int pending_geometry;
    /* toplevel */
    struct bc_xdg *parent;
    char title[BC_TEXT_BYTES + 1], appid[BC_TEXT_BYTES + 1];
    char cursor[24];                  /* the pointer shape last sent (WINDOW cursor), "" none yet */
    int32_t min_w, min_h, max_w, max_h;           /* committed */
    int32_t pmin_w, pmin_h, pmax_w, pmax_h;       /* pending */
    int limits_sent;
    int fullscreen;                   /* the client's own request */
    int maximize;                     /* the client's own request */
    int minimize;                     /* asked before WINDOW new */
    uint32_t decor_mode;              /* 0 none (client draws), else zxdg mode */
    int decor_dropped;                /* the decoration object went: the
                                         client draws from its next commit */
    int32_t size_w, size_h;
    int size_fullscreen, size_maximized, size_activated;
    unsigned size_tiled;              /* BC_TILED_* */
    struct bc_output *output;         /* the display it is on */
    uint64_t last_copy;               /* the sequence of its newest FRM1 */
    BcCopy kept;                      /* its newest frame when it is a copy (fd -1
                                         none): sent again on a new channel */
    int replay;                       /* the re-announce pass: 1 announced, 2 its
                                         parent still to say (WINDOW parent) */
    int32_t buffer_w, buffer_h;       /* its committed buffer's size */
    /* popup */
    struct bc_positioner positioner;
    int32_t px, py;                   /* relative to the parent's geometry */
    /* subsurface (the subsurface section): its picture, `WINDOW sub` */
    struct wl_resource *subsurface;
    struct bc_surface *sub_parent;    /* NULL once the parent surface is gone */
    struct wl_list sub_link;          /* in sub_parent->subs */
    int32_t sx, sy, psx, psy;         /* position in the parent: committed, pending */
    int sub_moved;                    /* a position waits for the parent's commit */
    int sync;                         /* wl_subsurface's synchronized mode */
    int sub_shown;                    /* its WINDOW sub line describes it now */
    int32_t sent_x, sent_y, sent_w, sent_h;
    int sent_above, sent_index;
    /* synchronized mode's cached state, applied at the parent's commit */
    struct wl_resource *cached_buffer;
    int cached_attach, cached;
    struct wl_listener cached_buffer_destroy;
    struct wl_list cached_frames, cached_feedback;
};

/* The clipboard (ADR 0229): a program's selection source. */
struct bc_source {
    struct wl_resource *resource;
    struct bc_server *server;
    int primary;                      /* zwp_primary_selection_source_v1, else wl_data_source */
    char *types[BC_CLIP_TYPES];
    int ntypes;
};

/* One selection, the clipboard's or the primary: a program's source, or the
   desktop's offer `desk_seq` in `desk_types`; never the bytes. */
struct bc_selection {
    struct bc_source *source;
    uint32_t source_seq;              /* the source's offer to the desktop */
    uint32_t desk_seq;                /* 0: the desktop offers nothing */
    int desk_text, desk_secret;
    uint32_t generation;              /* bumped at every change: offers made before are stale */
};

/* A wl_data_offer or zwp_primary_selection_offer_v1 made to a program. */
struct bc_offer {
    struct bc_server *server;
    int primary;
    uint32_t generation;
};

struct bc_server {
    struct wl_display *display;
    struct wl_event_loop *loop;
    struct wl_event_source *frame_source, *sigterm_source, *sigint_source;
    BcWire wire;
    char socket_name[32];
    /* the frame channel's link (the restart section) */
    const char *frame_socket, *frame_token;
    int link;                         /* BC_LINK_UP, or BC_LINK_DOWN while reconnecting */
    int quit;                         /* WINDOW quit 0: ending at once, no reconnect */
    struct wl_event_source *redial;   /* the next attempt, or a hello's answer overdue */
    struct wl_event_source *dial_source;
    int dial_fd;                      /* a hello waiting for its answer, -1 none */
    uint64_t lost_us;                 /* when the channel was lost */
    uint32_t bound_ms, backoff_ms;
    unsigned attempts;
    char dial_error[96];              /* the last attempt's failure, logged once a kind */
    struct wl_list windows;           /* bc_xdg */
    struct wl_list outputs;           /* bc_output */
    struct wl_list holds;             /* bc_hold: FRM3 frames Blit holds */
    int format_table_fd;              /* linux-dmabuf v4's format table */
    dev_t main_device;
    /* input (the seat section) */
    struct wl_list input_conns;       /* bc_input_conn: Blit's routes */
    struct wl_list keyboards, pointers;   /* wl_keyboard, wl_pointer resources */
    struct wl_list relative_pointers; /* zwp_relative_pointer_v1 resources */
    struct wl_list constraints;       /* bc_constraint: pointer locks and confinements */
    /* the clipboard (ADR 0229): [0] Ctrl-C/V, [1] the primary selection */
    struct bc_selection sel[2];
    struct wl_list devices[2];        /* wl_data_device, zwp_primary_selection_device_v1 */
    uint32_t clip_seq;                /* the offers made to the desktop */
    int line_fd;                      /* a descriptor passed with the line being handled, -1 */
    struct xkb_context *xkb;
    struct xkb_keymap *keymap;
    struct xkb_state *xkb_state;
    int keymap_fd;
    uint32_t keymap_size;
    int32_t repeat_rate, repeat_delay;    /* Blit's, from `INPUT keymap`; 0 0 none */
    uint32_t mods[4];
    int mods_sent;
    unsigned char keys_down[96];      /* evdev codes down, through KEY_MAX */
    struct bc_surface *kbd_focus, *ptr_focus;
    double ptr_x, ptr_y;
    uint32_t buttons_down;            /* BTN_LEFT + n */
    uint32_t button_serial;           /* the serial of the newest press, sent to every pointer */
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

/* `WINDOW geometry`: the window's rectangle in its surface pixels. */
static void send_geometry(struct bc_xdg *x)
{
    bc_wire_line(&x->server->wire, "WINDOW geometry %u %d %d %d %d", x->id, x->gx, x->gy, x->gw,
                 x->gh);
}

static void send_decor(struct bc_xdg *x)
{
    bc_wire_line(&x->server->wire, "WINDOW decor %u %s", x->id,
                 x->decor_mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE ? "server" : "client");
}

static int window_input_listen(struct bc_xdg *x);
static void window_input_close(struct bc_xdg *x);

/* `WINDOW new` and every hint the toplevel has, then its input route: at
   its first appearance, and again on a new channel (the restart section). */
static void announce_hints(struct bc_xdg *x, int parent)
{
    BcWire *w = &x->server->wire;
    bc_wire_line(w, "WINDOW new %u %d", x->id, parent);
    if (x->title[0]) bc_wire_line(w, "WINDOW title %u %s", x->id, x->title);
    if (x->appid[0]) bc_wire_line(w, "WINDOW appid %u %s", x->id, x->appid);
    if (x->min_w || x->min_h || x->max_w || x->max_h) send_limits(x);
    send_decor(x);
    if (x->gw > 0 && x->gh > 0) send_geometry(x);
    if (x->fullscreen) bc_wire_line(w, "WINDOW fullscreen %u on", x->id);
    if (x->maximize) bc_wire_line(w, "WINDOW maximize %u on", x->id);
    if (x->cursor[0]) bc_wire_line(w, "WINDOW cursor %u %s", x->id, x->cursor);
    /* Its input, straight from Blit to its own input socket (one route per
       toplevel); the desktop relays the request under its own id. */
    if (window_input_listen(x) == 0)
        bc_wire_input_sock(w, x->id, x->input_path);
}

/* The toplevel's first appearance, at its initial commit: new, then every
   hint it set before it. */
static void announce_toplevel(struct bc_xdg *x)
{
    BcWire *w = &x->server->wire;
    x->id = bc_wire_new_id(w);
    announce_hints(x, xdg_parent_id(x));
    if (x->minimize) bc_wire_line(w, "WINDOW minimize %u", x->id);
    x->minimize = 0;
    logf_("window %u: new parent %d title \"%s\" appid \"%s\"", x->id, xdg_parent_id(x),
          x->title, x->appid);
    /* On the first output until the desktop says which display it is on. */
    window_set_output(x, output_default(x->server));
    /* A lock asked for before the window was announced is asked for now. */
    if (x->surface) constraint_ask_surface(x->surface);
}

/* A popup's appearance, at its initial commit once its parent has an id:
   its own picture at its offset in the parent's surface pixels. */
static void announce_popup(struct bc_xdg *x)
{
    struct bc_xdg *p = x->parent;
    if (p == NULL || p->id == 0) return;
    x->id = bc_wire_new_id(&x->server->wire);
    bc_wire_line(&x->server->wire, "WINDOW popup %u %u %d %d", x->id, p->id, p->gx + x->px,
                 p->gy + x->py);
    logf_("popup %u: parent %u at %d,%d %dx%d", x->id, p->id, p->gx + x->px, p->gy + x->py,
          x->positioner.width, x->positioner.height);
    window_set_output(x, p->output);
}

/* `WINDOW gone`, once, after everything of the id is released: copied
   frames hold nothing, and the dmabuf frames Blit still holds are given
   back to the client first. */
static void holds_end_id(struct bc_server *s, uint32_t id);

/* The window's newest frame, when a copy: replaced by `c` (NULL to drop). */
static void kept_set(struct bc_xdg *x, const BcCopy *c)
{
    if (x->kept.fd >= 0) close(x->kept.fd);
    if (c) x->kept = *c;
    else x->kept.fd = -1;
}

static void subs_children_gone(struct bc_surface *surface);

static void window_gone(struct bc_xdg *x)
{
    /* Its subsurfaces' pictures go first: they are drawn with it. */
    if (x->surface) subs_children_gone(x->surface);
    x->sub_shown = 0;
    kept_set(x, NULL);
    window_set_output(x, NULL);
    /* Its pointer lock let go first: the desktop's hold ends with it. */
    constraints_window_gone(x);
    if (x->id) {
        holds_end_id(x->server, x->id);
        bc_wire_line(&x->server->wire, "WINDOW gone %u", x->id);
        logf_("window %u: gone", x->id);
    }
    window_input_close(x);
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
    /* Tiled (xdg_toplevel v2): a client-side frame drops its shadow there. */
    if (wl_resource_get_version(x->toplevel) >= 2) {
        static const uint32_t tiled[] = { XDG_TOPLEVEL_STATE_TILED_LEFT, XDG_TOPLEVEL_STATE_TILED_RIGHT,
                                          XDG_TOPLEVEL_STATE_TILED_TOP, XDG_TOPLEVEL_STATE_TILED_BOTTOM };
        unsigned k;
        for (k = 0; k < 4; ++k)
            if ((x->size_tiled & (1u << k)) && (s = wl_array_add(&states, sizeof(*s))) != NULL)
                *s = tiled[k];
    }
    if (x->decoration)
        zxdg_toplevel_decoration_v1_send_configure(x->decoration, x->decor_mode);
    xdg_toplevel_send_configure(x->toplevel, x->size_w, x->size_h, &states);
    wl_array_release(&states);
    x->serial = wl_display_next_serial(x->server->display);
    xdg_surface_send_configure(x->resource, x->serial);
    x->configured = 1;
}

/* A toplevel is configured from its initial commit on. xdg-shell's
   compositor *replies* to the initial commit with a configure, and clients
   lean on that: vkcube commits, does one roundtrip and presents. Waiting for
   the desktop's size made that first buffer a protocol error, because the
   size crosses the wire and comes back later than one roundtrip. So the
   reply goes at once -- the desktop's size when it has sent one, else 0x0,
   which xdg_toplevel defines as "the client picks" -- and the desktop's
   size, when it lands, is an ordinary reconfigure. */
static void configure_if_ready(struct bc_xdg *x)
{
    if (x->toplevel && x->initial)
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

/* A frame sent as FRM3 and not yet released by Blit; or, sequence 0, a
   window's newest dmabuf frame kept while the channel is down, sent on the
   next (the restart section). */
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
   held (docs/api.md). Blit's later `FRAME release`s for them are stale. */
static void holds_end_id(struct bc_server *s, uint32_t id)
{
    struct bc_hold *h, *next;
    wl_list_for_each_safe(h, next, &s->holds, link)
        if (h->id == id) hold_end(h);
}

/* Pixels copied as the window's newest frame: sent as FRM1 while the
   channel is up, and kept either way, to be sent again on a new channel.
   Returns 0 when sent (or kept, the channel down), -1 with last_error set
   when the copy or its send failed (a copy made is kept all the same). */
static int window_copy(struct bc_xdg *x, const void *pixels, uint32_t width, uint32_t height,
                       uint32_t stride, uint32_t format)
{
    struct bc_server *s = x->server;
    BcCopy c;
    int rc = 0;
    if (bc_frame_copy(&s->wire, pixels, width, height, stride, format, &c) != 0) return -1;
    if (s->link == BC_LINK_UP) {
        rc = bc_wire_send_frm1(&s->wire, x->id, &c);
        if (rc == 0) x->last_copy = s->wire.sequence;
    }
    kept_set(x, &c);
    return rc;
}

/* A dmabuf's pixels copied as FRM1, for a peer that cannot import it:
   only one linear plane of an 8-bit RGB format can be mapped and read. */
static int dmabuf_copy(struct bc_xdg *x, struct bc_dmabuf *b)
{
    struct bc_server *s = x->server;
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
    rc = window_copy(x, map + b->offsets[0], (uint32_t)b->width, (uint32_t)b->height,
                     b->strides[0], shm_format);
    sync.flags = DMA_BUF_SYNC_END | DMA_BUF_SYNC_READ;
    ioctl(b->fds[0], DMA_BUF_IOCTL_SYNC, &sync);
    munmap(map, bytes);
    if (rc == 0 && s->link == BC_LINK_UP) ++s->wire.dmabuf_copied;
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
     `FRAME release` of the frame before it -- the one that says Blit drew the
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

static int region_add_op(struct bc_region *r, int32_t x, int32_t y, int32_t w, int32_t h, int add)
{
    struct bc_rect_op *grown;
    if (r->count >= 4096) return -1;
    if ((grown = realloc(r->ops, sizeof(*grown) * (size_t)(r->count + 1))) == NULL) return -1;
    r->ops = grown;
    r->ops[r->count++] = (struct bc_rect_op){ x, y, w, h, add };
    return 0;
}

static void region_clear(struct bc_region *r)
{
    free(r->ops);
    r->ops = NULL;
    r->count = 0;
}

static int region_copy(struct bc_region *to, const struct bc_region *from)
{
    struct bc_rect_op *ops = NULL;
    if (from->count && (ops = malloc(sizeof(*ops) * (size_t)from->count)) == NULL) return -1;
    if (from->count) memcpy(ops, from->ops, sizeof(*ops) * (size_t)from->count);
    region_clear(to);
    to->ops = ops;
    to->count = from->count;
    return 0;
}

static int region_contains(const struct bc_region *r, double x, double y)
{
    int i, in = 0;
    for (i = 0; i < r->count; ++i) {
        const struct bc_rect_op *o = &r->ops[i];
        if (x >= o->x && y >= o->y && x < (double)o->x + o->w && y < (double)o->y + o->h)
            in = o->add;
    }
    return in;
}

/* Whether the surface takes the pointer at (x, y), its own pixels: inside
   its buffer and its input region. */
static int surface_takes_pointer(const struct bc_surface *surface, int32_t w, int32_t h, double x,
                                 double y)
{
    if (x < 0 || y < 0 || x >= w || y >= h) return 0;
    return surface->input_all || region_contains(&surface->input, x, y);
}

/* ---- the subsurface section: helpers ----

   A subsurface is a picture of its own, as a popup is: `WINDOW sub <id>
   <parent> <x> <y> <w> <h> above|below`, its frames under <id>, the
   desktop placing it at its offset in the top-level's surface pixels
   (docs/api.md, "Subsurfaces"). The tree under a top-level is flattened:
   <parent> is always the top-level, the offset the sum of the positions
   down to it, and the lines go bottom to top, `below` for those under the
   top-level's own picture. */

/* The picture a surface's buffers are frames of: its window, popup or
   subsurface; NULL for none (a cursor). */
static struct bc_xdg *surface_pic(struct bc_surface *surface)
{
    return surface->xdg ? surface->xdg : surface->sub;
}

/* The top-level a subsurface belongs to and its offset in that top-level's
   surface pixels; NULL when its chain does not reach a top-level (a parent
   gone, no role yet, or a popup's: not shown in this release). */
static struct bc_xdg *sub_root(const struct bc_xdg *sub, int32_t *ox, int32_t *oy)
{
    int32_t x = 0, y = 0;
    while (sub && sub->subsurface) {
        struct bc_surface *p = sub->sub_parent;
        x += sub->sx;
        y += sub->sy;
        if (p == NULL) return NULL;
        if (p->xdg) {
            if (ox) *ox = x;
            if (oy) *oy = y;
            return p->xdg->toplevel ? p->xdg : NULL;
        }
        sub = p->sub;
    }
    return NULL;
}

/* The top-level a surface's input belongs to: its own, a popup's or a
   subsurface's top-level; NULL for none. */
static struct bc_xdg *surface_window(struct bc_surface *surface)
{
    struct bc_xdg *x;
    if (surface == NULL) return NULL;
    if (surface->sub) return sub_root(surface->sub, NULL, NULL);
    x = surface->xdg;
    while (x && x->popup && x->parent) x = x->parent;
    return x && x->toplevel ? x : NULL;
}

/* Synchronized, by its own mode or an ancestor's (wl_subsurface). */
static int sub_effectively_sync(const struct bc_xdg *sub)
{
    while (sub) {
        if (sub->sync) return 1;
        sub = sub->sub_parent ? sub->sub_parent->sub : NULL;
    }
    return 0;
}

/* One subsurface of a top-level's flattened tree, bottom to top. */
struct bc_sub_at {
    struct bc_xdg *sub;
    int32_t x, y;
    int above;
};
#define BC_SUBS_MAX 64

static int subs_walk(struct bc_surface *surface, int32_t ox, int32_t oy, int above, int top,
                     struct bc_sub_at *out, int n)
{
    struct wl_list *node;
    for (node = surface->subs.next; node != &surface->subs; node = node->next) {
        struct bc_xdg *c;
        if (node == &surface->self) {
            if (top) above = 1;
            continue;
        }
        c = wl_container_of(node, c, sub_link);
        /* A subsurface shows only while it and every one above it in the
           tree is mapped. */
        if (!c->mapped || c->surface == NULL) continue;
        if (n < BC_SUBS_MAX) {
            out[n].sub = c;
            out[n].x = ox + c->sx;
            out[n].y = oy + c->sy;
            out[n].above = above;
        }
        ++n;
        n = subs_walk(c->surface, ox + c->sx, oy + c->sy, above, 0, out, n);
    }
    return n;
}

/* The top-level's shown subsurfaces, bottom to top. */
static int subs_collect(struct bc_xdg *root, struct bc_sub_at *out)
{
    int n;
    if (root == NULL || root->surface == NULL) return 0;
    n = subs_walk(root->surface, 0, 0, 0, 1, out, 0);
    return n < BC_SUBS_MAX ? n : BC_SUBS_MAX;
}

static uint32_t surface_refresh_mhz(struct bc_surface *surface)
{
    struct bc_xdg *x = surface_pic(surface);
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
    struct bc_output *o = surface_pic(surface) ? surface_pic(surface)->output : NULL;
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

/* Blit has taken the surface's committed frame. While the channel is down
   nothing is shown: the frame is kept (the restart section), its feedback
   discarded, and its callbacks are answered as they would be, at the
   output's refresh, so the program does not freeze. */
static void frames_taken(struct bc_surface *surface)
{
    if (surface == NULL) return;
    if (surface->server->link == BC_LINK_UP) feedback_presented(surface);
    else feedback_discard(&surface->feedback);
    wl_list_insert_list(surface->due.prev, &surface->frames);
    wl_list_init(&surface->frames);
    frames_pace(surface);
}

enum { BC_SENT_NONE = 0, BC_SENT_COPY, BC_SENT_HELD };

/* A dmabuf commit: FRM3, held until Blit's `FRAME release`, unless the peer
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
            /* The newest frame is this one, not a copy. */
            kept_set(x, NULL);
            /* Taken at once when Blit holds nothing else of the window,
               else at the `FRAME release` of the frame before it. */
            if (!older) frames_taken(x->surface);
            return BC_SENT_HELD;
        }
        free(h);
        logf_("window %u: FRM3 not sent (%s); copying", x->id, s->wire.last_error);
    }
    if (dmabuf_copy(x, b) != 0) {
        logf_("window %u: a dmabuf frame not shown: %s", x->id, s->wire.last_error);
        return BC_SENT_NONE;
    }
    frames_taken(x->surface);
    return BC_SENT_COPY;
}

/* A commit while the channel is down: kept as the window's newest frame,
   and only it -- a copy for wl_shm, the buffer itself for a dmabuf (a hold
   of sequence 0), the frame before given back to the client at once so a
   program drawing on into a full pool does not stall. */
static int keep_buffer(struct bc_xdg *x, struct wl_shm_buffer *shm, struct bc_dmabuf *dmabuf)
{
    struct bc_server *s = x->server;
    int rc;
    if (dmabuf) {
        struct bc_hold *h = calloc(1, sizeof(*h)), *other, *next;
        if (h == NULL) return BC_SENT_NONE;
        h->id = x->id;
        h->buffer = dmabuf;
        ++dmabuf->holds;
        /* Held before the one it replaces is given back: a buffer committed
           again is not released while it is kept. */
        wl_list_for_each_safe(other, next, &s->holds, link)
            if (other->id == x->id) hold_end(other);
        wl_list_insert(s->holds.prev, &h->link);
        kept_set(x, NULL);
        frames_taken(x->surface);
        return BC_SENT_HELD;
    }
    wl_shm_buffer_begin_access(shm);
    rc = window_copy(x, wl_shm_buffer_get_data(shm), (uint32_t)wl_shm_buffer_get_width(shm),
                     (uint32_t)wl_shm_buffer_get_height(shm), (uint32_t)wl_shm_buffer_get_stride(shm),
                     wl_shm_buffer_get_format(shm));
    wl_shm_buffer_end_access(shm);
    if (rc != 0) {
        logf_("window %u: a frame not kept: %s", x->id, s->wire.last_error);
        return BC_SENT_NONE;
    }
    holds_end_id(s, x->id);
    frames_taken(x->surface);
    return BC_SENT_COPY;
}

/* The committed buffer as one frame of the window's picture: BC_SENT_HELD
   for an FRM3 Blit now holds, BC_SENT_COPY for a copy (the buffer is the
   client's again), BC_SENT_NONE when nothing was sent. */
static int send_buffer(struct bc_xdg *x, struct bc_surface *surface, struct wl_resource *buffer)
{
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    struct bc_dmabuf *dmabuf = dmabuf_from_buffer(buffer);
    int rc;
    if (shm == NULL && dmabuf == NULL) {
        logf_("window %u: a buffer of an unknown kind, not shown", x ? x->id : 0);
        return BC_SENT_NONE;
    }
    if (x == NULL || x->id == 0 || !x->server->wire.window)
        return BC_SENT_NONE;
    if (x->server->link != BC_LINK_UP) return keep_buffer(x, shm, dmabuf);
    if (dmabuf) return send_dmabuf(x, dmabuf);
    wl_shm_buffer_begin_access(shm);
    rc = window_copy(x, wl_shm_buffer_get_data(shm), (uint32_t)wl_shm_buffer_get_width(shm),
                     (uint32_t)wl_shm_buffer_get_height(shm), (uint32_t)wl_shm_buffer_get_stride(shm),
                     wl_shm_buffer_get_format(shm));
    wl_shm_buffer_end_access(shm);
    if (rc != 0) {
        logf_("window %u: frame not sent: %s", x->id, x->server->wire.last_error);
        return BC_SENT_NONE;
    }
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

static void surface_opaque_region(struct wl_client *client, struct wl_resource *resource,
                                  struct wl_resource *region)
{
    (void)client; (void)resource; (void)region;
}

/* Pending until the surface's state is applied (its commit, or its
   parent's for a synchronized subsurface). NULL is all of the surface. */
static void surface_input_region(struct wl_client *client, struct wl_resource *resource,
                                 struct wl_resource *region)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    surface->pending_input_set = 1;
    surface->pending_input_all = region == NULL;
    if (region == NULL) region_clear(&surface->pending_input);
    else if (region_copy(&surface->pending_input, wl_resource_get_user_data(region)) != 0)
        wl_client_post_no_memory(client);
}

static int sub_apply_buffer(struct bc_xdg *sub, struct wl_resource *buffer, int attached);
static void subs_parent_applied(struct bc_surface *parent);
static void sub_cache(struct bc_xdg *sub, struct wl_resource *buffer, int attached);
static void sub_apply_cached(struct bc_xdg *sub);

/* A surface's new state applied: at its commit, or at its parent's for a
   synchronized subsurface (the state cached until then). `frames` and
   `feedback` are the callbacks and feedback committed with it. */
static void surface_apply(struct bc_surface *surface, struct wl_resource *buffer, int attached,
                          struct wl_list *frames, struct wl_list *feedback)
{
    struct bc_xdg *x = surface->xdg, *sub = surface->sub, *pic = surface_pic(surface);
    int sent = 0;

    /* The frame callbacks and feedback wait for Blit to take the next
       frame of this surface; feedback for a frame it never took is
       discarded once a newer buffer comes. */
    if (attached && buffer) feedback_discard(&surface->feedback);
    wl_list_insert_list(surface->frames.prev, frames);
    wl_list_init(frames);
    wl_list_insert_list(surface->feedback.prev, feedback);
    wl_list_init(feedback);
    if (surface->pending_input_set) {
        surface->pending_input_set = 0;
        surface->input_all = surface->pending_input_all;
        if (!surface->input_all && region_copy(&surface->input, &surface->pending_input) != 0)
            surface->input_all = 1;
    }

    if (x && (x->toplevel || x->popup)) {
        if (x->pending_geometry) {
            int changed = x->gx != x->pgx || x->gy != x->pgy || x->gw != x->pgw || x->gh != x->pgh;
            x->gx = x->pgx; x->gy = x->pgy; x->gw = x->pgw; x->gh = x->pgh;
            x->pending_geometry = 0;
            /* The window inside its buffer (ADR 0229): its shadows outside. */
            if (changed && x->toplevel && x->id) send_geometry(x);
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
            /* A hidden window has no frame to show again on a new channel. */
            kept_set(x, NULL);
            x->mapped = 0;
            x->hidden = 1;
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
                if (x->id == 0 && x->server->wire.window) announce_popup(x);
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
            sent = send_buffer(x, surface, buffer);
            x->mapped = 1;
            x->hidden = 0;
            x->buffer_w = shm ? wl_shm_buffer_get_width(shm) : dmabuf ? dmabuf->width : 0;
            x->buffer_h = shm ? wl_shm_buffer_get_height(shm) : dmabuf ? dmabuf->height : 0;
        }
    } else if (sub) {
        sent = sub_apply_buffer(sub, buffer, attached);
    }
    /* Copied or not shown (a cursor): the client's again. An FRM3 frame is
       the client's again at Blit's `FRAME release`. A frame nothing took
       still has its callbacks answered at the refresh, so a program
       waiting on them (EGL's swap) does not stall. */
    if (attached && buffer && sent != BC_SENT_HELD)
        wl_buffer_send_release(buffer);
    if (attached && buffer && sent == BC_SENT_NONE) {
        feedback_discard(&surface->feedback);
        frames_taken(surface);
    } else if (!attached && pic && pic->mapped && pic->id) {
        frames_taken(surface);
    }
    /* Its subsurfaces' positions and synchronized state are applied with it. */
    subs_parent_applied(surface);
}

static void surface_commit(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_surface *surface = wl_resource_get_user_data(resource);
    struct bc_xdg *sub = surface->sub;
    struct wl_resource *buffer = surface->pending_buffer;
    int attached = surface->pending_attach;
    (void)client;
    set_pending_buffer(surface, NULL);
    surface->pending_attach = 0;
    if (sub && sub_effectively_sync(sub)) {
        /* Synchronized: kept until the parent's commit applies it. */
        sub_cache(sub, buffer, attached);
        return;
    }
    if (sub && sub->cached) {
        /* Desynchronized with state still cached: applied as a whole. */
        sub_cache(sub, buffer, attached);
        sub_apply_cached(sub);
        return;
    }
    surface_apply(surface, buffer, attached, &surface->pending_frames, &surface->pending_feedback);
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
    .set_opaque_region = surface_opaque_region,
    .set_input_region = surface_input_region,
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
    if (surface->sub) {
        /* The wl_subsurface outlived its surface: unmapped, surfaceless. */
        window_gone(surface->sub);
        surface->sub->surface = NULL;
    }
    {
        /* Its subsurfaces lose their parent: unmapped (a parent's
           destruction unmaps its children). */
        struct wl_list *node, *next;
        for (node = surface->subs.next; node != &surface->subs; node = next) {
            struct bc_xdg *c;
            next = node->next;
            if (node == &surface->self) continue;
            c = wl_container_of(node, c, sub_link);
            if (c->mapped || c->id) window_gone(c);
            c->sub_parent = NULL;
            wl_list_remove(&c->sub_link);
            wl_list_init(&c->sub_link);
        }
    }
    region_clear(&surface->input);
    region_clear(&surface->pending_input);
    free(surface);
}

/* ---- wl_compositor ---- */

/* Regions: the input region decides which surface the pointer is over (a
   program routes a subsurface's input to its parent with an empty one,
   as Firefox does). Opacity is not used. */
static void region_add(struct wl_client *client, struct wl_resource *resource, int32_t x, int32_t y,
                       int32_t w, int32_t h)
{
    if (region_add_op(wl_resource_get_user_data(resource), x, y, w, h, 1) != 0)
        wl_client_post_no_memory(client);
}

static void region_subtract(struct wl_client *client, struct wl_resource *resource, int32_t x,
                            int32_t y, int32_t w, int32_t h)
{
    if (region_add_op(wl_resource_get_user_data(resource), x, y, w, h, 0) != 0)
        wl_client_post_no_memory(client);
}

static void region_destroyed(struct wl_resource *resource)
{
    struct bc_region *r = wl_resource_get_user_data(resource);
    region_clear(r);
    free(r);
}

static const struct wl_region_interface region_impl = {
    .destroy = bc_destroy_request,
    .add = region_add,
    .subtract = region_subtract,
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
    wl_list_init(&surface->subs);
    wl_list_insert(&surface->subs, &surface->self);
    surface->input_all = 1;
    surface->pace = wl_event_loop_add_timer(surface->server->loop, frames_pace_timer, surface);
    surface->pending_buffer_destroy.notify = pending_buffer_gone;
    wl_list_init(&surface->pending_buffer_destroy.link);
    wl_resource_set_implementation(surface->resource, &surface_impl, surface, surface_destroyed);
}

static void compositor_create_region(struct wl_client *client, struct wl_resource *resource,
                                     uint32_t id)
{
    struct bc_region *r = calloc(1, sizeof(*r));
    struct wl_resource *region = r ? wl_resource_create(client, &wl_region_interface, 1, id) : NULL;
    (void)resource;
    if (region == NULL) {
        free(r);
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(region, &region_impl, r, region_destroyed);
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

/* ---- the clipboard (ADR 0229) ----

   Two selections, the clipboard (Ctrl-C/V, wl_data_device) and the primary
   (middle click, zwp_primary_selection_v1), each either a program's source
   or the desktop's offer. Hibr's clipboard is the truth: the compositor
   keeps which side owns a selection and streams a read from the owner to
   the reader through the reader's pipe, and never holds the bytes.

   - A program sets a selection (it must have the keyboard): its source is
     the selection, and the desktop is told `CLIP offer <seq> clipboard|
     primary [text/plain;charset=utf-8] [x-kde-passwordManagerHint]` -- any
     text type the source offers is the one text type, and the secret hint
     if it carries one; no text, no types. The desktop's `CLIP read <seq>
     <type>` with a pipe is answered by the source's best text type.
   - The desktop sets one (`CLIP offer`): the program with the keyboard is
     offered the text aliases (and the secret hint); a program's read
     becomes `CLIP read <seq> text/plain;charset=utf-8` with its pipe.
   - `CLIP clear <seq>` ends an offer either way: a program's source going,
     the desktop's clipboard cleared (the Vault's timeout).
   - Between programs, a paste goes straight from the source.
   - The program with the keyboard is offered each selection when it gets
     the keyboard and whenever a selection changes. A desktop's offer lapses
     with its channel; a program's is offered again on a new one.
   - Drag and drop is not here yet (ADR 0229, later): a drag is cancelled. */

static const char *const bc_text_types[] = {
    "text/plain;charset=utf-8", "UTF8_STRING", "text/plain", "STRING", "TEXT",
};
#define BC_TEXT_TYPE "text/plain;charset=utf-8"
#define BC_SECRET_TYPE "x-kde-passwordManagerHint"

static int is_text_type(const char *type)
{
    size_t i;
    for (i = 0; i < sizeof(bc_text_types) / sizeof(bc_text_types[0]); ++i)
        if (strcmp(type, bc_text_types[i]) == 0) return 1;
    return 0;
}

static int source_has(const struct bc_source *src, const char *type)
{
    int i;
    for (i = 0; i < src->ntypes; ++i)
        if (strcmp(src->types[i], type) == 0) return 1;
    return 0;
}

/* The source's best text type, in bc_text_types' order; NULL for none. */
static const char *source_text_type(const struct bc_source *src)
{
    size_t i;
    for (i = 0; i < sizeof(bc_text_types) / sizeof(bc_text_types[0]); ++i)
        if (source_has(src, bc_text_types[i])) return bc_text_types[i];
    return NULL;
}

static void source_send(struct bc_source *src, const char *type, int fd)
{
    if (src->primary) zwp_primary_selection_source_v1_send_send(src->resource, type, fd);
    else wl_data_source_send_send(src->resource, type, fd);
}

static void source_cancel(struct bc_source *src)
{
    if (src->primary) zwp_primary_selection_source_v1_send_cancelled(src->resource);
    else wl_data_source_send_cancelled(src->resource);
}

/* A program's source, offered to the desktop under a new seq. */
static void clip_offer_to_desktop(struct bc_server *s, int k)
{
    struct bc_selection *sel = &s->sel[k];
    const char *text, *secret;
    if (sel->source == NULL) return;
    sel->source_seq = ++s->clip_seq;
    text = source_text_type(sel->source) ? " " BC_TEXT_TYPE : "";
    secret = source_has(sel->source, BC_SECRET_TYPE) ? " " BC_SECRET_TYPE : "";
    bc_wire_line(&s->wire, "CLIP offer %u %s%s%s", sel->source_seq, k ? "primary" : "clipboard", text,
                 secret);
}

static void offer_receive(struct bc_offer *o, const char *type, int fd)
{
    struct bc_server *s = o->server;
    struct bc_selection *sel = &s->sel[o->primary];
    if (o->generation == sel->generation) {
        if (sel->source) {
            if (source_has(sel->source, type)) source_send(sel->source, type, fd);
        } else if (sel->desk_seq) {
            const char *want = is_text_type(type) && sel->desk_text ? BC_TEXT_TYPE
                             : strcmp(type, BC_SECRET_TYPE) == 0 && sel->desk_secret ? BC_SECRET_TYPE
                             : NULL;
            if (want && bc_wire_line_fd(&s->wire, fd, "CLIP read %u %s", sel->desk_seq, want) != 0)
                logf_("clipboard: a paste from the desktop not asked: %s", s->wire.last_error);
        }
    }
    close(fd);
}

static void offer_destroyed(struct wl_resource *resource)
{
    free(wl_resource_get_user_data(resource));
}

static void data_offer_accept(struct wl_client *client, struct wl_resource *resource, uint32_t serial,
                              const char *type)
{
    (void)client; (void)resource; (void)serial; (void)type;
}

static void data_offer_receive(struct wl_client *client, struct wl_resource *resource, const char *type,
                               int32_t fd)
{
    (void)client;
    offer_receive(wl_resource_get_user_data(resource), type, fd);
}

static void data_offer_finish(struct wl_client *client, struct wl_resource *resource)
{
    (void)client; (void)resource;
}

static void data_offer_set_actions(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t actions, uint32_t preferred)
{
    (void)client; (void)resource; (void)actions; (void)preferred;
}

static const struct wl_data_offer_interface data_offer_impl = {
    .accept = data_offer_accept,
    .receive = data_offer_receive,
    .destroy = bc_destroy_request,
    .finish = data_offer_finish,
    .set_actions = data_offer_set_actions,
};

static void primary_offer_receive(struct wl_client *client, struct wl_resource *resource,
                                  const char *type, int32_t fd)
{
    (void)client;
    offer_receive(wl_resource_get_user_data(resource), type, fd);
}

static const struct zwp_primary_selection_offer_v1_interface primary_offer_impl = {
    .receive = primary_offer_receive,
    .destroy = bc_destroy_request,
};

static void offer_type(struct wl_resource *offer, int k, const char *type)
{
    if (k) zwp_primary_selection_offer_v1_send_offer(offer, type);
    else wl_data_offer_send_offer(offer, type);
}

/* Selection k to every device of `client`: a new offer with its types, or
   none. */
static void clip_send_to_client(struct bc_server *s, int k, struct wl_client *client)
{
    struct bc_selection *sel = &s->sel[k];
    struct wl_resource *dev;
    wl_resource_for_each(dev, &s->devices[k]) {
        struct wl_resource *offer = NULL;
        struct bc_offer *o;
        size_t i;
        if (wl_resource_get_client(dev) != client) continue;
        if ((sel->source || sel->desk_seq) && (o = calloc(1, sizeof(*o))) != NULL) {
            o->server = s;
            o->primary = k;
            o->generation = sel->generation;
            offer = k ? wl_resource_create(client, &zwp_primary_selection_offer_v1_interface, 1, 0)
                      : wl_resource_create(client, &wl_data_offer_interface, wl_resource_get_version(dev), 0);
            if (offer == NULL) {
                free(o);
                wl_client_post_no_memory(client);
                continue;
            }
            wl_resource_set_implementation(offer, k ? (const void *)&primary_offer_impl
                                                    : (const void *)&data_offer_impl, o, offer_destroyed);
            if (k) zwp_primary_selection_device_v1_send_data_offer(dev, offer);
            else wl_data_device_send_data_offer(dev, offer);
            if (sel->source) {
                int t;
                for (t = 0; t < sel->source->ntypes; ++t) offer_type(offer, k, sel->source->types[t]);
            } else {
                if (sel->desk_text)
                    for (i = 0; i < sizeof(bc_text_types) / sizeof(bc_text_types[0]); ++i)
                        offer_type(offer, k, bc_text_types[i]);
                if (sel->desk_secret) offer_type(offer, k, BC_SECRET_TYPE);
            }
        }
        if (k) zwp_primary_selection_device_v1_send_selection(dev, offer);
        else wl_data_device_send_selection(dev, offer);
    }
}

/* A selection changed: older offers go stale; the keyboard's program is
   offered the new one. */
static void clip_changed(struct bc_server *s, int k)
{
    ++s->sel[k].generation;
    if (s->kbd_focus) clip_send_to_client(s, k, wl_resource_get_client(s->kbd_focus->resource));
}

/* The program with the keyboard set selection k to `src` (NULL: none). A
   NULL ends only a program's selection: the desktop's is Hibr's. */
static void clip_set_source(struct bc_server *s, int k, struct bc_source *src)
{
    struct bc_selection *sel = &s->sel[k];
    struct bc_source *old = sel->source;
    if (src == NULL && old == NULL) return;
    if (old && old != src) source_cancel(old);
    if (old && src == NULL && sel->source_seq)
        bc_wire_line(&s->wire, "CLIP clear %u", sel->source_seq);
    sel->source = src;
    sel->source_seq = 0;
    if (src) sel->desk_seq = 0;
    clip_offer_to_desktop(s, k);
    clip_changed(s, k);
}

static void source_offer(struct wl_client *client, struct wl_resource *resource, const char *type)
{
    struct bc_source *src = wl_resource_get_user_data(resource);
    (void)client;
    if (src->ntypes >= BC_CLIP_TYPES || strlen(type) > BC_CLIP_TYPE_BYTES || source_has(src, type)) return;
    if ((src->types[src->ntypes] = strdup(type)) != NULL) ++src->ntypes;
}

static void source_destroyed(struct wl_resource *resource)
{
    struct bc_source *src = wl_resource_get_user_data(resource);
    struct bc_server *s = src->server;
    int k = src->primary, i;
    if (s->sel[k].source == src) {
        if (s->sel[k].source_seq) bc_wire_line(&s->wire, "CLIP clear %u", s->sel[k].source_seq);
        s->sel[k].source = NULL;
        s->sel[k].source_seq = 0;
        clip_changed(s, k);
    }
    for (i = 0; i < src->ntypes; ++i) free(src->types[i]);
    free(src);
}

static void data_source_set_actions(struct wl_client *client, struct wl_resource *resource,
                                    uint32_t actions)
{
    (void)client; (void)resource; (void)actions;
}

static const struct wl_data_source_interface data_source_impl = {
    .offer = source_offer,
    .destroy = bc_destroy_request,
    .set_actions = data_source_set_actions,
};

static const struct zwp_primary_selection_source_v1_interface primary_source_impl = {
    .offer = source_offer,
    .destroy = bc_destroy_request,
};

static void source_create(struct wl_client *client, struct wl_resource *manager, uint32_t id, int k)
{
    struct bc_source *src = calloc(1, sizeof(*src));
    struct wl_resource *r = src ? wl_resource_create(client, k ? &zwp_primary_selection_source_v1_interface
                                                               : &wl_data_source_interface,
                                                     wl_resource_get_version(manager), id)
                                : NULL;
    if (r == NULL) {
        free(src);
        wl_client_post_no_memory(client);
        return;
    }
    src->resource = r;
    src->server = wl_resource_get_user_data(manager);
    src->primary = k;
    wl_resource_set_implementation(r, k ? (const void *)&primary_source_impl : (const void *)&data_source_impl,
                                   src, source_destroyed);
}

/* set_selection: honoured from the program with the keyboard only, as
   Wayland requires a serial of its own input. */
static void device_set_selection(struct wl_resource *device, struct wl_resource *source, int k)
{
    struct bc_server *s = wl_resource_get_user_data(device);
    struct wl_client *client = wl_resource_get_client(device);
    if (s->kbd_focus == NULL || wl_resource_get_client(s->kbd_focus->resource) != client) {
        logf_("clipboard: a selection from a program without the keyboard is ignored");
        return;
    }
    clip_set_source(s, k, source ? wl_resource_get_user_data(source) : NULL);
}

static void data_device_start_drag(struct wl_client *client, struct wl_resource *resource,
                                   struct wl_resource *source, struct wl_resource *origin,
                                   struct wl_resource *icon, uint32_t serial)
{
    /* Drag and drop is ADR 0229's later row: the drag ends at once. */
    (void)client; (void)resource; (void)origin; (void)icon; (void)serial;
    if (source) wl_data_source_send_cancelled(source);
}

static void data_device_set_selection(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *source, uint32_t serial)
{
    (void)client; (void)serial;
    device_set_selection(resource, source, 0);
}

static const struct wl_data_device_interface data_device_impl = {
    .start_drag = data_device_start_drag,
    .set_selection = data_device_set_selection,
    .release = bc_destroy_request,
};

static void primary_device_set_selection(struct wl_client *client, struct wl_resource *resource,
                                         struct wl_resource *source, uint32_t serial)
{
    (void)client; (void)serial;
    device_set_selection(resource, source, 1);
}

static const struct zwp_primary_selection_device_v1_interface primary_device_impl = {
    .set_selection = primary_device_set_selection,
    .destroy = bc_destroy_request,
};

static void device_create(struct wl_client *client, struct wl_resource *manager, uint32_t id, int k)
{
    struct bc_server *s = wl_resource_get_user_data(manager);
    struct wl_resource *r = wl_resource_create(client, k ? &zwp_primary_selection_device_v1_interface
                                                         : &wl_data_device_interface,
                                               wl_resource_get_version(manager), id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, k ? (const void *)&primary_device_impl : (const void *)&data_device_impl,
                                   s, unlink_resource);
    wl_list_insert(&s->devices[k], wl_resource_get_link(r));
    if (s->kbd_focus && wl_resource_get_client(s->kbd_focus->resource) == client)
        clip_send_to_client(s, k, client);
}

static void ddm_create_data_source(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    source_create(client, resource, id, 0);
}

static void ddm_get_data_device(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                struct wl_resource *seat)
{
    (void)seat;
    device_create(client, resource, id, 0);
}

static const struct wl_data_device_manager_interface data_device_manager_impl = {
    .create_data_source = ddm_create_data_source,
    .get_data_device = ddm_get_data_device,
};

static void psm_create_source(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    source_create(client, resource, id, 1);
}

static void psm_get_device(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                           struct wl_resource *seat)
{
    (void)seat;
    device_create(client, resource, id, 1);
}

static const struct zwp_primary_selection_device_manager_v1_interface primary_manager_impl = {
    .create_source = psm_create_source,
    .get_device = psm_get_device,
    .destroy = bc_destroy_request,
};

static void bind_data_device_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wl_data_device_manager_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &data_device_manager_impl, data, NULL);
}

static void bind_primary_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &zwp_primary_selection_device_manager_v1_interface,
                                               (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &primary_manager_impl, data, NULL);
}

/* The desktop's lines. */
static void clip_desktop_line(struct bc_server *s, const BcLine *l)
{
    int k;
    if (l->kind == BC_LINE_CLIP_OFFER) {
        struct bc_selection *sel = &s->sel[l->primary];
        char types[BC_TEXT_BYTES + 1], *t, *save = NULL;
        if (sel->source) {
            source_cancel(sel->source);
            sel->source = NULL;
            sel->source_seq = 0;
        }
        sel->desk_seq = l->id;
        sel->desk_text = sel->desk_secret = 0;
        snprintf(types, sizeof(types), "%s", l->types);
        for (t = strtok_r(types, " ", &save); t; t = strtok_r(NULL, " ", &save)) {
            if (is_text_type(t)) sel->desk_text = 1;
            if (strcmp(t, BC_SECRET_TYPE) == 0) sel->desk_secret = 1;
        }
        clip_changed(s, l->primary);
        return;
    }
    if (l->kind == BC_LINE_CLIP_CLEAR) {
        for (k = 0; k < 2; ++k)
            if (s->sel[k].desk_seq == l->id) {
                s->sel[k].desk_seq = 0;
                clip_changed(s, k);
            }
        return;
    }
    /* CLIP read: the desktop wants a program's selection, into its pipe. */
    {
        int fd = s->line_fd;
        s->line_fd = -1;
        if (fd < 0) {
            logf_("clipboard: CLIP read %u came with no pipe", l->id);
            return;
        }
        for (k = 0; k < 2; ++k) {
            struct bc_source *src = s->sel[k].source;
            const char *type;
            if (src == NULL || s->sel[k].source_seq != l->id) continue;
            type = strcmp(l->types, BC_TEXT_TYPE) == 0 ? source_text_type(src)
                 : source_has(src, l->types) ? l->types : NULL;
            if (type) source_send(src, type, fd);
        }
        close(fd);
    }
}

/* The channel lost: the desktop's offers lapse with it. A new one: the
   programs' selections are offered again. */
static void clip_channel(struct bc_server *s, int up)
{
    int k;
    for (k = 0; k < 2; ++k) {
        if (up) clip_offer_to_desktop(s, k);
        else if (s->sel[k].desk_seq) {
            s->sel[k].desk_seq = 0;
            clip_changed(s, k);
        }
    }
}

/* ---- wl_subcompositor: each subsurface a picture (WINDOW sub) ---- */

static void window_resend(struct bc_xdg *x);

/* The top-level's subsurfaces to the desktop: `WINDOW sub` for each shown
   one, bottom to top, all of them again when any is new or its offset,
   size or place in the order changed. A subsurface newly described whose
   newest frame was kept (it drew before its top-level was announced, or
   with the channel down) has that frame sent; `fresh` is about to send its
   own; on a new channel (`resend` 0) the re-announcement sends every
   frame after the lines. Nothing while the top-level has no id or the
   desktop places no windows. Returns the lines written. */
static int subs_show(struct bc_xdg *root, struct bc_xdg *fresh, int resend)
{
    struct bc_sub_at at[BC_SUBS_MAX];
    struct bc_server *s;
    int n, i, changed = 0;
    if (root == NULL || root->id == 0) return 0;
    s = root->server;
    if (s->link != BC_LINK_UP || !s->wire.window) return 0;
    n = subs_collect(root, at);
    for (i = 0; i < n; ++i) {
        struct bc_xdg *c = at[i].sub;
        if (!c->sub_shown || c->sent_x != at[i].x || c->sent_y != at[i].y ||
            c->sent_w != c->buffer_w || c->sent_h != c->buffer_h || c->sent_above != at[i].above ||
            c->sent_index != i)
            changed = 1;
    }
    if (!changed) return 0;
    for (i = 0; i < n; ++i) {
        struct bc_xdg *c = at[i].sub;
        int was = c->sub_shown;
        if (c->id == 0) c->id = bc_wire_new_id(&s->wire);
        bc_wire_line(&s->wire, "WINDOW sub %u %u %d %d %d %d %s", c->id, root->id, at[i].x, at[i].y,
                     c->buffer_w, c->buffer_h, at[i].above ? "above" : "below");
        c->sub_shown = 1;
        c->sent_x = at[i].x;
        c->sent_y = at[i].y;
        c->sent_w = c->buffer_w;
        c->sent_h = c->buffer_h;
        c->sent_above = at[i].above;
        c->sent_index = i;
        window_set_output(c, root->output);
        if (!was) {
            logf_("subsurface %u: window %u at %d,%d %dx%d %s", c->id, root->id, at[i].x, at[i].y,
                  c->buffer_w, c->buffer_h, at[i].above ? "above" : "below");
            if (c != fresh && resend) window_resend(c);
        }
    }
    return n;
}

/* A committed buffer of a subsurface, or its unmapping (a NULL buffer). */
static int sub_apply_buffer(struct bc_xdg *sub, struct wl_resource *buffer, int attached)
{
    struct bc_server *s = sub->server;
    struct wl_shm_buffer *shm;
    struct bc_dmabuf *dmabuf;
    struct bc_xdg *root;
    if (!attached) return BC_SENT_NONE;
    if (buffer == NULL) {
        if (sub->mapped) {
            logf_("subsurface %u: unmapped by its client", sub->id);
            window_gone(sub);
        }
        return BC_SENT_NONE;
    }
    shm = wl_shm_buffer_get(buffer);
    dmabuf = dmabuf_from_buffer(buffer);
    if (shm == NULL && dmabuf == NULL) {
        logf_("subsurface: a buffer of an unknown kind, not shown");
        return BC_SENT_NONE;
    }
    if (sub->id == 0) sub->id = bc_wire_new_id(&s->wire);
    sub->mapped = 1;
    sub->buffer_w = shm ? wl_shm_buffer_get_width(shm) : dmabuf->width;
    sub->buffer_h = shm ? wl_shm_buffer_get_height(shm) : dmabuf->height;
    root = sub_root(sub, NULL, NULL);
    if (root == NULL && sub->sub_parent && sub->sub_parent->xdg && sub->sub_parent->xdg->popup &&
        !sub->sub_shown)
        logf_("subsurface %u: a popup's, not shown in this release", sub->id);
    subs_show(root, sub, 1);
    /* Shown now: a frame of its picture. Not (yet): kept, its callbacks
       answered, and sent when it is described. */
    if (sub->sub_shown && s->link == BC_LINK_UP) return send_buffer(sub, sub->surface, buffer);
    return keep_buffer(sub, shm, dmabuf);
}

static void cached_buffer_gone(struct wl_listener *listener, void *data)
{
    struct bc_xdg *sub = wl_container_of(listener, sub, cached_buffer_destroy);
    (void)data;
    wl_list_remove(&sub->cached_buffer_destroy.link);
    wl_list_init(&sub->cached_buffer_destroy.link);
    sub->cached_buffer = NULL;
}

static void set_cached_buffer(struct bc_xdg *sub, struct wl_resource *buffer)
{
    if (sub->cached_buffer) {
        wl_list_remove(&sub->cached_buffer_destroy.link);
        wl_list_init(&sub->cached_buffer_destroy.link);
    }
    sub->cached_buffer = buffer;
    if (buffer) wl_resource_add_destroy_listener(buffer, &sub->cached_buffer_destroy);
}

/* A synchronized subsurface's commit: its state cached for the parent's.
   A cached buffer replaced before it was applied is the client's again
   (unless Blit holds it as an earlier frame). */
static void sub_cache(struct bc_xdg *sub, struct wl_resource *buffer, int attached)
{
    struct bc_surface *surface = sub->surface;
    if (attached) {
        struct wl_resource *old = sub->cached_buffer;
        if (old && old != buffer) {
            struct bc_dmabuf *b = dmabuf_from_buffer(old);
            if (b == NULL || b->holds == 0) wl_buffer_send_release(old);
        }
        set_cached_buffer(sub, buffer);
        sub->cached_attach = 1;
    }
    wl_list_insert_list(sub->cached_frames.prev, &surface->pending_frames);
    wl_list_init(&surface->pending_frames);
    wl_list_insert_list(sub->cached_feedback.prev, &surface->pending_feedback);
    wl_list_init(&surface->pending_feedback);
    sub->cached = 1;
}

static void sub_apply_cached(struct bc_xdg *sub)
{
    struct wl_resource *buffer = sub->cached_buffer;
    int attached = sub->cached_attach;
    set_cached_buffer(sub, NULL);
    sub->cached_attach = 0;
    sub->cached = 0;
    if (sub->surface)
        surface_apply(sub->surface, buffer, attached, &sub->cached_frames, &sub->cached_feedback);
}

/* The parent's state applied: its subsurfaces' new positions, and the
   cached state of the synchronized ones, then the top-level's lines. */
static void subs_parent_applied(struct bc_surface *parent)
{
    struct wl_list *node, *next;
    struct bc_xdg *root;
    for (node = parent->subs.next; node != &parent->subs; node = next) {
        struct bc_xdg *c;
        next = node->next;
        if (node == &parent->self) continue;
        c = wl_container_of(node, c, sub_link);
        if (c->sub_moved) {
            c->sx = c->psx;
            c->sy = c->psy;
            c->sub_moved = 0;
        }
        if (c->cached && sub_effectively_sync(c)) sub_apply_cached(c);
    }
    root = parent->sub ? sub_root(parent->sub, NULL, NULL)
                       : parent->xdg && parent->xdg->toplevel ? parent->xdg : NULL;
    subs_show(root, NULL, 1);
}

/* Every subsurface under `surface` gone with it (its picture and theirs). */
static void subs_children_gone(struct bc_surface *surface)
{
    struct wl_list *node;
    for (node = surface->subs.next; node != &surface->subs; node = node->next) {
        struct bc_xdg *c;
        if (node == &surface->self) continue;
        c = wl_container_of(node, c, sub_link);
        if (c->mapped || c->id) window_gone(c);
    }
}

static void subsurface_set_position(struct wl_client *client, struct wl_resource *resource,
                                    int32_t x, int32_t y)
{
    struct bc_xdg *sub = wl_resource_get_user_data(resource);
    (void)client;
    if (sub == NULL) return;
    sub->psx = x;
    sub->psy = y;
    sub->sub_moved = 1;
}

/* place_above and place_below: the sibling is the parent or another
   subsurface of it. The order is shown at the parent's next commit. */
static void subsurface_restack(struct wl_resource *resource, struct wl_resource *sibling, int above)
{
    struct bc_xdg *sub = wl_resource_get_user_data(resource);
    struct bc_surface *other = wl_resource_get_user_data(sibling), *parent;
    struct wl_list *ref;
    if (sub == NULL || (parent = sub->sub_parent) == NULL) return;
    if (other == parent) ref = &parent->self;
    else if (other != sub->surface && other->sub && other->sub->sub_parent == parent)
        ref = &other->sub->sub_link;
    else {
        wl_resource_post_error(resource, WL_SUBSURFACE_ERROR_BAD_SURFACE,
                               "the sibling is neither the parent nor a sibling");
        return;
    }
    wl_list_remove(&sub->sub_link);
    wl_list_insert(above ? ref : ref->prev, &sub->sub_link);
}

static void subsurface_place_above(struct wl_client *client, struct wl_resource *resource,
                                   struct wl_resource *sibling)
{
    (void)client;
    subsurface_restack(resource, sibling, 1);
}

static void subsurface_place_below(struct wl_client *client, struct wl_resource *resource,
                                   struct wl_resource *sibling)
{
    (void)client;
    subsurface_restack(resource, sibling, 0);
}

static void subsurface_set_sync(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *sub = wl_resource_get_user_data(resource);
    (void)client;
    if (sub) sub->sync = 1;
}

static void subsurface_set_desync(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *sub = wl_resource_get_user_data(resource);
    (void)client;
    if (sub) sub->sync = 0;
}

static const struct wl_subsurface_interface subsurface_impl = {
    .destroy = bc_destroy_request,
    .set_position = subsurface_set_position,
    .place_above = subsurface_place_above,
    .place_below = subsurface_place_below,
    .set_sync = subsurface_set_sync,
    .set_desync = subsurface_set_desync,
};

/* The wl_subsurface goes: its surface is unmapped at once (and every one
   under it), its cached state dropped. */
static void subsurface_destroyed(struct wl_resource *resource)
{
    struct bc_xdg *sub = wl_resource_get_user_data(resource);
    if (sub == NULL) return;
    window_gone(sub);
    kept_set(sub, NULL);
    if (sub->cached_buffer) {
        struct bc_dmabuf *b = dmabuf_from_buffer(sub->cached_buffer);
        if (b == NULL || b->holds == 0) wl_buffer_send_release(sub->cached_buffer);
    }
    set_cached_buffer(sub, NULL);
    callbacks_destroy(&sub->cached_frames);
    feedback_discard(&sub->cached_feedback);
    if (sub->sub_parent) wl_list_remove(&sub->sub_link);
    if (sub->surface) sub->surface->sub = NULL;
    wl_list_remove(&sub->link);
    free(sub);
}

static void subcompositor_get_subsurface(struct wl_client *client, struct wl_resource *resource,
                                         uint32_t id, struct wl_resource *surface_resource,
                                         struct wl_resource *parent_resource)
{
    struct bc_surface *surface = wl_resource_get_user_data(surface_resource);
    struct bc_surface *parent = wl_resource_get_user_data(parent_resource), *p;
    struct bc_xdg *sub;
    struct wl_resource *r;
    if (surface == parent) {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "a surface cannot be its own parent");
        return;
    }
    if (surface->sub || (surface->role != BC_ROLE_NONE && surface->role != BC_ROLE_SUBSURFACE)) {
        wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                               "the surface already has a role object");
        return;
    }
    for (p = parent; p && p->sub; p = p->sub->sub_parent)
        if (p == surface) {
            /* bad_parent is wayland 1.22's; 1.21's bad_surface covers it. */
            wl_resource_post_error(resource, WL_SUBCOMPOSITOR_ERROR_BAD_SURFACE,
                                   "the parent is the surface's own descendant");
            return;
        }
    sub = calloc(1, sizeof(*sub));
    r = sub ? wl_resource_create(client, &wl_subsurface_interface, 1, id) : NULL;
    if (r == NULL) {
        free(sub);
        wl_client_post_no_memory(client);
        return;
    }
    sub->server = surface->server;
    sub->surface = surface;
    sub->subsurface = r;
    sub->sub_parent = parent;
    sub->sync = 1;
    sub->input_fd = -1;
    sub->kept.fd = -1;
    wl_list_init(&sub->cached_frames);
    wl_list_init(&sub->cached_feedback);
    sub->cached_buffer_destroy.notify = cached_buffer_gone;
    wl_list_init(&sub->cached_buffer_destroy.link);
    /* On top of the parent's stack. */
    wl_list_insert(parent->subs.prev, &sub->sub_link);
    wl_list_insert(&surface->server->windows, &sub->link);
    surface->role = BC_ROLE_SUBSURFACE;
    surface->sub = sub;
    wl_resource_set_implementation(r, &subsurface_impl, sub, subsurface_destroyed);
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

/* ---- the pointer's shape (ADR 0229) ----

   One source of cursors: Blit's own pointer art, chosen by name
   (blit_pointer_name_shape, OSC 22). A program names its shape with
   wp_cursor_shape_v1 -- the shapes are CSS cursor names, the vocabulary
   Blit's table speaks -- and the compositor tells the desktop `WINDOW
   cursor <id> <name>` for the top-level whose surface has the pointer; the
   desktop relays it to Blit as that window's OSC 22 shape. A program's own
   cursor images (wl_pointer.set_cursor with a surface) are never drawn:
   that is `default`, the display's own pointer. set_cursor with no
   surface hides the pointer: `none`. Only the program with the pointer
   sets it. */

static const char *const bc_cursor_names[] = {
    NULL, "default", "context-menu", "help", "pointer", "progress", "wait", "cell", "crosshair",
    "text", "vertical-text", "alias", "copy", "move", "no-drop", "not-allowed", "grab", "grabbing",
    "e-resize", "n-resize", "ne-resize", "nw-resize", "s-resize", "se-resize", "sw-resize",
    "w-resize", "ew-resize", "ns-resize", "nesw-resize", "nwse-resize", "col-resize", "row-resize",
    "all-scroll", "zoom-in", "zoom-out", "dnd-ask", "all-resize",
};

/* The window of the program with the pointer is given `name`, once per
   change. */
static void cursor_set(struct bc_server *s, struct wl_client *client, const char *name)
{
    struct bc_xdg *x;
    if (s->ptr_focus == NULL || wl_resource_get_client(s->ptr_focus->resource) != client) return;
    x = surface_window(s->ptr_focus);
    if (x == NULL || strcmp(x->cursor, name) == 0) return;
    snprintf(x->cursor, sizeof(x->cursor), "%s", name);
    if (x->id) bc_wire_line(&s->wire, "WINDOW cursor %u %s", x->id, name);
}

static void pointer_set_cursor(struct wl_client *client, struct wl_resource *resource,
                               uint32_t serial, struct wl_resource *surface, int32_t x, int32_t y)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    (void)serial; (void)x; (void)y;
    if (surface) {
        struct bc_surface *cs = wl_resource_get_user_data(surface);
        if (!cs->cursor_logged) {
            cs->cursor_logged = 1;
            logf_("cursor: a program's own cursor image is shown as the display's pointer "
                  "(it names no shape: wp_cursor_shape_v1 is the way)");
        }
    }
    cursor_set(s, client, surface ? "default" : "none");
}

static void shape_set(struct wl_client *client, struct wl_resource *resource, uint32_t serial,
                      uint32_t shape)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    (void)serial;
    if (shape == 0 || shape >= sizeof(bc_cursor_names) / sizeof(bc_cursor_names[0]) ||
        (shape >= 35 && wl_resource_get_version(resource) < 2)) {
        wl_resource_post_error(resource, WP_CURSOR_SHAPE_DEVICE_V1_ERROR_INVALID_SHAPE, "shape %u", shape);
        return;
    }
    if (s) cursor_set(s, client, bc_cursor_names[shape]);
}

static const struct wp_cursor_shape_device_v1_interface shape_device_impl = {
    .destroy = bc_destroy_request,
    .set_shape = shape_set,
};

/* A device for a pointer carries the server; one for a tablet tool (Blit
   has no tablets yet) carries none and changes nothing. */
static void shape_device_create(struct wl_client *client, struct wl_resource *manager, uint32_t id,
                                struct bc_server *s)
{
    struct wl_resource *r = wl_resource_create(client, &wp_cursor_shape_device_v1_interface,
                                               wl_resource_get_version(manager), id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &shape_device_impl, s, NULL);
}

static void shape_get_pointer(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                              struct wl_resource *pointer)
{
    (void)pointer;
    shape_device_create(client, resource, id, wl_resource_get_user_data(resource));
}

static void shape_get_tablet_tool(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                                  struct wl_resource *tool)
{
    (void)tool;
    shape_device_create(client, resource, id, NULL);
}

static const struct wp_cursor_shape_manager_v1_interface shape_manager_impl = {
    .destroy = bc_destroy_request,
    .get_pointer = shape_get_pointer,
    .get_tablet_tool_v2 = shape_get_tablet_tool,
};

static void bind_shape_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &wp_cursor_shape_manager_v1_interface,
                                               (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &shape_manager_impl, data, NULL);
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

/* A program's own frame asks the desktop to run a drag or show the window
   menu (ADR 0229): only with the serial of a press still down on that
   window, so no program starts one by itself. */
static struct bc_xdg *toplevel_grab_window(struct wl_resource *resource, uint32_t serial)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    struct bc_server *s;
    if (x == NULL || x->id == 0) return NULL;
    s = x->server;
    if (s->link != BC_LINK_UP || !s->wire.window || s->buttons_down == 0 ||
        serial != s->button_serial || surface_window(s->ptr_focus) != x) {
        logf_("window %u: a move, resize or menu without a press on it is ignored", x->id);
        return NULL;
    }
    return x;
}

static void toplevel_show_window_menu(struct wl_client *client, struct wl_resource *resource,
                                      struct wl_resource *seat, uint32_t serial, int32_t mx,
                                      int32_t my)
{
    struct bc_xdg *x = toplevel_grab_window(resource, serial);
    (void)client; (void)seat;
    /* x, y are in window geometry coordinates: the surface's are the
       geometry's origin plus them. */
    if (x) bc_wire_line(&x->server->wire, "WINDOW menu %u %d %d", x->id, x->gx + mx, x->gy + my);
}

static void toplevel_move(struct wl_client *client, struct wl_resource *resource,
                          struct wl_resource *seat, uint32_t serial)
{
    struct bc_xdg *x = toplevel_grab_window(resource, serial);
    (void)client; (void)seat;
    if (x) bc_wire_line(&x->server->wire, "WINDOW move %u", x->id);
}

static void toplevel_resize(struct wl_client *client, struct wl_resource *resource,
                            struct wl_resource *seat, uint32_t serial, uint32_t edges)
{
    static const char *const names[] = {
        [XDG_TOPLEVEL_RESIZE_EDGE_TOP] = "top", [XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM] = "bottom",
        [XDG_TOPLEVEL_RESIZE_EDGE_LEFT] = "left", [XDG_TOPLEVEL_RESIZE_EDGE_TOP_LEFT] = "top-left",
        [XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_LEFT] = "bottom-left", [XDG_TOPLEVEL_RESIZE_EDGE_RIGHT] = "right",
        [XDG_TOPLEVEL_RESIZE_EDGE_TOP_RIGHT] = "top-right",
        [XDG_TOPLEVEL_RESIZE_EDGE_BOTTOM_RIGHT] = "bottom-right",
    };
    struct bc_xdg *x;
    (void)client; (void)seat;
    if (edges >= sizeof(names) / sizeof(names[0]) || names[edges] == NULL) {
        wl_resource_post_error(resource, XDG_TOPLEVEL_ERROR_INVALID_RESIZE_EDGE, "edge %u", edges);
        return;
    }
    if ((x = toplevel_grab_window(resource, serial)) != NULL)
        bc_wire_line(&x->server->wire, "WINDOW resize %u %s", x->id, names[edges]);
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
    kept_set(x, NULL);
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
    x->input_fd = -1;
    x->kept.fd = -1;
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
   (`INPUT sock <id> <path>`, sent at WINDOW new) and writes that window's
   input as lines (blit_compositor_wire.h, bc_parse_input). They become:

   - keys: wl_keyboard.key with the evdev code as it came, the xkb state
     (the keymap Blit sends on the route, compiled here as every client
     compiles it) giving wl_keyboard.modifiers. The keyboard enters
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

   The keymap comes on the route: `INPUT keymap <id> <revision> <size>
   <rate> <delay>` with a sealed memfd of Blit's xkb text (and a NUL),
   sent when the route goes live and whenever the keyboard whose keys
   reach the window changes -- configured, plugged or gone, or a key from
   another keyboard, before that key -- so the key that follows is read
   with the new keymap. Blit's own keymap and repeat, one source
   (GET /v1/input/keymap serves the same); the compositor needs no API
   and no token for it. A keymap the same as the one installed (another
   route sends it too, or only the repeat changed) is not installed again:
   compared by its bytes, since a restarted Blit counts revisions afresh. Clients are sent the new keymap at once, and new
   repeat info when the repeat changes (0 0: no repeat). Until a route's
   first keymap the keyboard is sent `no_keymap` and keys are still
   delivered. */

/* The longest keymap taken from Blit. */
#define BC_KEYMAP_BYTES (512 * 1024)
/* Descriptors a route may have passed ahead of the line they go with. */
#define BC_INPUT_FDS 4

struct bc_input_conn {
    struct wl_list link;              /* bc_server.input_conns */
    struct bc_server *server;
    int fd;
    struct wl_event_source *source;
    uint32_t id;                      /* the window its lines name */
    char buf[1024];
    size_t len;
    int fds[BC_INPUT_FDS];            /* passed with lines not yet read, oldest first */
    int nfds;
};

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

/* Blit's repeat for the keyboard whose keys reach the windows: sent to
   every keyboard when it changes. */
static void repeat_apply(struct bc_server *s, int32_t rate, int32_t delay)
{
    struct wl_resource *r;
    if (rate < 0 || rate > 1000 || delay < 0 || delay > 60000 || rate == 0) rate = delay = 0;
    if (rate == s->repeat_rate && delay == s->repeat_delay) return;
    s->repeat_rate = rate;
    s->repeat_delay = delay;
    logf_("keymap: repeat %d a second after %d ms, Blit's", rate, delay);
    wl_resource_for_each(r, &s->keyboards)
        if (wl_resource_get_version(r) >= WL_KEYBOARD_REPEAT_INFO_SINCE_VERSION)
            wl_keyboard_send_repeat_info(r, rate, delay);
}

/* Blit's `INPUT keymap`: the memfd `fd` (this function's to close) holds
   `size` bytes, xkb text and a NUL. Compiled, copied into a memfd of the
   compositor's own, sealed, and sent to every keyboard, with the repeat.
   The same bytes as the installed keymap change only the repeat. */
static void keymap_from_route(struct bc_server *s, const BcInput *in, int fd)
{
    struct xkb_keymap *keymap;
    struct wl_resource *r;
    unsigned char *text, *map;
    int own;
    if (s->xkb == NULL || in->size < 2 || in->size > BC_KEYMAP_BYTES ||
        (text = mmap(NULL, in->size, PROT_READ, MAP_PRIVATE, fd, 0)) == MAP_FAILED) {
        logf_("keymap: Blit's revision %llu (%u bytes) could not be read",
              (unsigned long long)in->revision, in->size);
        close(fd);
        return;
    }
    close(fd);
    if (s->keymap_fd >= 0 && in->size == s->keymap_size &&
        (map = mmap(NULL, s->keymap_size, PROT_READ, MAP_PRIVATE, s->keymap_fd, 0)) != MAP_FAILED) {
        int same = memcmp(map, text, in->size) == 0;
        munmap(map, s->keymap_size);
        if (same) {
            munmap(text, in->size);
            logf_("keymap: Blit's revision %llu, unchanged", (unsigned long long)in->revision);
            repeat_apply(s, in->rate, in->delay);
            return;
        }
    }
    if (text[in->size - 1] != '\0' ||
        (keymap = xkb_keymap_new_from_buffer(s->xkb, (const char *)text, in->size - 1,
                                             XKB_KEYMAP_FORMAT_TEXT_V1,
                                             XKB_KEYMAP_COMPILE_NO_FLAGS)) == NULL) {
        logf_("keymap: Blit's revision %llu did not compile (%u bytes)",
              (unsigned long long)in->revision, in->size);
        munmap(text, in->size);
        repeat_apply(s, in->rate, in->delay);
        return;
    }
    own = memfd_create("blit-compositor-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (own < 0 || ftruncate(own, (off_t)in->size) != 0 ||
        (map = mmap(NULL, in->size, PROT_READ | PROT_WRITE, MAP_SHARED, own, 0)) == MAP_FAILED) {
        if (own >= 0) close(own);
        xkb_keymap_unref(keymap);
        munmap(text, in->size);
        return;
    }
    memcpy(map, text, in->size);
    munmap(map, in->size);
    munmap(text, in->size);
    fcntl(own, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL);
    if (s->keymap_fd >= 0) close(s->keymap_fd);
    if (s->xkb_state) xkb_state_unref(s->xkb_state);
    if (s->keymap) xkb_keymap_unref(s->keymap);
    s->keymap = keymap;
    s->xkb_state = xkb_state_new(keymap);
    s->keymap_fd = own;
    s->keymap_size = in->size;
    s->mods_sent = 0;
    logf_("keymap: %u bytes from Blit, revision %llu", in->size - 1,
          (unsigned long long)in->revision);
    wl_resource_for_each(r, &s->keyboards)
        keyboard_send_keymap(s, r);
    repeat_apply(s, in->rate, in->delay);
    /* Keys down stay down in the new state (a modifier held across a
       keyboard switch), and the focused client is told its modifiers. */
    if (s->xkb_state) {
        uint32_t code;
        for (code = 0; code < sizeof(s->keys_down) * 8; ++code)
            if (s->keys_down[code / 8] & (1u << (code % 8)))
                xkb_state_update_key(s->xkb_state, code + 8, XKB_KEY_DOWN);
    }
    keyboard_send_modifiers(s, 1);
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
    /* The selections, before the keyboard enters (wl_data_device). */
    clip_send_to_client(s, 0, wl_resource_get_client(surface->resource));
    clip_send_to_client(s, 1, wl_resource_get_client(surface->resource));
    wl_array_init(&keys);
    seat_for_client(r, &s->keyboards, surface)
        wl_keyboard_send_enter(r, wl_display_next_serial(s->display), surface->resource, &keys);
    wl_array_release(&keys);
    keyboard_send_modifiers(s, 1);
    logf_("keyboard: enter window %u", surface->xdg ? surface->xdg->id : 0);
}

/* The surface under (x, y) of toplevel `x`, in that surface's pixels: a
   mapped popup of it first, then its subsurfaces above it (topmost
   first), the toplevel, those below it -- each only where its input
   region takes the pointer (a program routes a subsurface's input to its
   parent with an empty one) -- else the toplevel. */
static struct bc_surface *pointer_target(struct bc_server *s, struct bc_xdg *x, double px,
                                         double py, double *lx, double *ly)
{
    struct bc_sub_at at[BC_SUBS_MAX];
    int n, i, pass;
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
    n = subs_collect(x, at);
    for (pass = 0; pass < 3; ++pass) {
        if (pass == 1) {
            if (x->surface && surface_takes_pointer(x->surface, x->buffer_w, x->buffer_h, px, py)) break;
            continue;
        }
        for (i = n - 1; i >= 0; --i) {
            struct bc_xdg *c = at[i].sub;
            if (at[i].above != (pass == 0) || c->surface == NULL) continue;
            if (surface_takes_pointer(c->surface, c->buffer_w, c->buffer_h, px - at[i].x, py - at[i].y)) {
                *lx = px - at[i].x;
                *ly = py - at[i].y;
                return c->surface;
            }
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
    if (surface->sub) {
        int32_t sx = 0, sy = 0;
        if (sub_root(surface->sub, &sx, &sy)) {
            *ox = sx;
            *oy = sy;
        }
        return;
    }
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
    if (surface_window(s->ptr_focus) != x)
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
    struct bc_xdg *x = surface_window(surface);
    return x && x->id ? x : NULL;
}

static void constraint_ask(struct bc_constraint *c)
{
    struct bc_xdg *x;
    if (c->window || c->defunct || (x = surface_toplevel(c->surface)) == NULL) return;
    /* With the channel down it is asked when a desktop is back. */
    if (c->server->link != BC_LINK_UP) return;
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

/* The desktop's hold ended: unlocked or unconfined; a oneshot constraint
   is then spent. */
static void constraint_deactivate(struct bc_constraint *c)
{
    uint32_t id = c->window;
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
            constraint_deactivate(c);
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
    /* conn->id is the window whose socket Blit connected to; in.id is the
       desktop's picture id for it, Blit's name, not the compositor's. */
    if (in.kind == BC_INPUT_KEYMAP) {
        int fd;
        if (conn->nfds == 0) {
            logf_("input: a keymap line with no memfd passed with it");
            return;
        }
        fd = conn->fds[0];
        memmove(conn->fds, conn->fds + 1, (size_t)(--conn->nfds) * sizeof(int));
        keymap_from_route(s, &in, fd);
        return;
    }
    x = window_by_id(s, conn->id);
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
        {
            /* One serial for the event: a press's is what a move, resize or
               menu request must name (WINDOW move). */
            uint32_t serial = wl_display_next_serial(s->display);
            if (in.pressed) s->button_serial = serial;
            seat_for_client(r, &s->pointers, s->ptr_focus)
                wl_pointer_send_button(r, serial, now_ms(), in.code,
                                       in.pressed ? WL_POINTER_BUTTON_STATE_PRESSED
                                                  : WL_POINTER_BUTTON_STATE_RELEASED);
        }
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
    while (conn->nfds > 0) close(conn->fds[--conn->nfds]);
    wl_event_source_remove(conn->source);
    close(conn->fd);
    wl_list_remove(&conn->link);
    free(conn);
}

static int on_input_conn(int fd, uint32_t mask, void *data)
{
    struct bc_input_conn *conn = data;
    for (;;) {
        /* Lines, and the memfds Blit passes with `INPUT keymap`, queued in
           order for the lines that name them. */
        char control[CMSG_SPACE(BC_INPUT_FDS * sizeof(int))];
        struct iovec iov = { conn->buf + conn->len, sizeof(conn->buf) - 1 - conn->len };
        struct msghdr msg;
        struct cmsghdr *cmsg;
        ssize_t got;
        char *line, *nl;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        got = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        for (cmsg = got > 0 ? CMSG_FIRSTHDR(&msg) : NULL; cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            int k, n;
            if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
            n = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (k = 0; k < n; ++k) {
                int passed;
                memcpy(&passed, CMSG_DATA(cmsg) + (size_t)k * sizeof(int), sizeof(int));
                if (conn->nfds < BC_INPUT_FDS) conn->fds[conn->nfds++] = passed;
                else close(passed);
            }
        }
        if (got > 0 && (msg.msg_flags & MSG_CTRUNC))
            logf_("input: descriptors passed beyond %d were dropped", BC_INPUT_FDS);
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
    struct bc_xdg *x = data;
    struct bc_server *s = x->server;
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
        conn->id = x->id;
        conn->source = wl_event_loop_add_fd(s->loop, c, WL_EVENT_READABLE, on_input_conn, conn);
        wl_list_insert(&s->input_conns, &conn->link);
    }
    return 0;
}

/* The toplevel's input socket, $XDG_RUNTIME_DIR/<display>-input-<id>, a
   stream socket only this user (and root) can reach: the directory is the
   user's alone. One per toplevel, because the desktop relays `INPUT sock`
   to Blit under its own picture id and Blit writes that id on the lines:
   the connection, not the line, says which window the input is for. Made
   once, at the toplevel's first announcement; a new channel's
   re-announcement names the same socket. Returns 0 when it listens. */
static int window_input_listen(struct bc_xdg *x)
{
    struct bc_server *s = x->server;
    struct sockaddr_un addr;
    int fd;
    if (x->input_fd >= 0) return 0;
    if (snprintf(x->input_path, sizeof(x->input_path), "%s/%s-input-%u", getenv("XDG_RUNTIME_DIR"),
                 s->socket_name, x->id) >= (int)sizeof(x->input_path)) {
        x->input_path[0] = '\0';
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path, x->input_path, strlen(x->input_path));
    unlink(x->input_path);
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || bind(fd, (struct sockaddr *)&addr, sizeof(addr)) != 0 || listen(fd, 4) != 0) {
        logf_("input socket %s: %s", x->input_path, strerror(errno));
        if (fd >= 0) close(fd);
        x->input_path[0] = '\0';
        return -1;
    }
    chmod(x->input_path, 0600);
    x->input_fd = fd;
    x->input_source = wl_event_loop_add_fd(s->loop, fd, WL_EVENT_READABLE, on_input_listen, x);
    return 0;
}

static void window_input_close(struct bc_xdg *x)
{
    if (x->input_fd < 0) return;
    if (x->input_source) wl_event_source_remove(x->input_source);
    x->input_source = NULL;
    close(x->input_fd);
    unlink(x->input_path);
    x->input_fd = -1;
    x->input_path[0] = '\0';
}

/* The display's lock is ours: any <display>-input-<n> left in the runtime
   directory is a dead compositor's. */
static void input_sockets_clear(struct bc_server *s)
{
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    char prefix[64], path[160];
    struct dirent *e;
    DIR *d;
    if (runtime == NULL || (d = opendir(runtime)) == NULL) return;
    snprintf(prefix, sizeof(prefix), "%s-input-", s->socket_name);
    while ((e = readdir(d)) != NULL)
        if (strncmp(e->d_name, prefix, strlen(prefix)) == 0 &&
            snprintf(path, sizeof(path), "%s/%s", runtime, e->d_name) < (int)sizeof(path))
            unlink(path);
    closedir(d);
}

/* `FRAME error <id> <code>`. Replies come in the order frames are
   read and an accepted frame gets none, so a refusal is the newest frame
   of the picture -- an FRM3 still held, unless an FRM1 went after it
   (a producer's rule, ADR 0223). ENOTSUP (Blit cannot import now) copies that
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
            if (dmabuf_copy(x, newest->buffer) != 0)
                logf_("window %u: the refused dmabuf frame is not shown: %s", id, s->wire.last_error);
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
        x->size_w = l.width;
        x->size_h = l.height;
        x->size_fullscreen = l.fullscreen;
        x->size_maximized = l.maximized;
        x->size_activated = l.activated;
        x->size_tiled = l.tiled;
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
    case BC_LINE_QUIT:
        /* The person quit the desktop: every program ends now, and no
           desktop is waited for. */
        logf_("WINDOW quit 0: the desktop is quitting; ending every program");
        s->quit = 1;
        s->status = 0;
        wl_display_terminate(s->display);
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
        if (strncmp(line, "INPUT error ", 12) == 0)
            logf_("Blit refused or ended an input route: %.60s", line);
        return;
    case BC_LINE_OK_WINDOW:
        return;
    case BC_LINE_CLIP_OFFER:
    case BC_LINE_CLIP_READ:
    case BC_LINE_CLIP_CLEAR:
        clip_desktop_line(s, &l);
        return;
    default:
        logf_("an unknown line from the channel: %.80s", line);
        return;
    }
}

static void channel_lost(struct bc_server *s, const char *why);

static int on_frame_socket(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    char buf[1024];
    for (;;) {
        /* A packet's descriptor (CLIP read's pipe) is its line's. */
        char control[CMSG_SPACE(4 * sizeof(int))];
        struct iovec iov = { buf, sizeof(buf) - 1 };
        struct msghdr msg;
        struct cmsghdr *cmsg;
        ssize_t got;
        char *line, *next;
        memset(&msg, 0, sizeof(msg));
        msg.msg_iov = &iov;
        msg.msg_iovlen = 1;
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        got = recvmsg(fd, &msg, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) break;
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            channel_lost(s, got == 0 ? "closed" : strerror(errno));
            return 0;
        }
        for (cmsg = CMSG_FIRSTHDR(&msg); cmsg; cmsg = CMSG_NXTHDR(&msg, cmsg)) {
            int n, k, passed;
            if (cmsg->cmsg_level != SOL_SOCKET || cmsg->cmsg_type != SCM_RIGHTS) continue;
            n = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            for (k = 0; k < n; ++k) {
                memcpy(&passed, CMSG_DATA(cmsg) + (size_t)k * sizeof(int), sizeof(int));
                if (s->line_fd < 0) s->line_fd = passed;
                else close(passed);
            }
        }
        buf[got] = '\0';
        for (line = buf; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next) *next++ = '\0';
            handle_line(s, line);
            if (s->quit) break;
        }
        /* A descriptor no line took is closed. */
        if (s->line_fd >= 0) {
            close(s->line_fd);
            s->line_fd = -1;
        }
        if (s->quit) return 0;
    }
    if (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR)) channel_lost(s, "hung up");
    return 0;
}

/* ---- restart: surviving the loss of the frame channel ----

   docs/api.md, "Programs survive a desktop restart". When the channel
   closes (a Restart Desktop execs the desktop, which serves the same socket
   and token again), the compositor keeps every client and window, the
   Wayland socket and the input socket, and:

   - stops sending: each window's newest frame is kept and only it -- the
     copy of a wl_shm (or copied dmabuf) frame, or the dmabuf buffer itself,
     held (a hold of sequence 0) while every older one is given back at
     once, so a program drawing on does not stall on a full pool;
   - answers frame callbacks at the window's output refresh, as if each
     frame were taken (presentation feedback `discarded`: nothing is shown);
   - ends what the desktop's holds gave: an active pointer lock or
     confinement is unlocked or unconfined, as at the desktop's `off` (a
     oneshot one spent); Blit's input routes end with the desktop's channel
     and their sockets close (each releasing what it held);
   - dials the same socket and token again after 50 ms, then 100, 200 ...
     at most a second apart, never blocking the event loop (the hello's
     answer is read from it, waited for at most BC_HELLO_MS), until a peer
     answers `OK` with window=1 (one that answers without it, Blit reached
     directly, is let go and dialed again), for at most BC_RECONNECT_MS --
     BLIT_COMPOSITOR_RECONNECT_MS, for gates only, shortens it -- after
     which it ends its programs as before, status 1.

   On a new channel it says `WINDOW display` again, then announces every
   window again with the same id and its last state, in creation order:
   each toplevel's `WINDOW new` (its parent when that was announced before
   it, else `WINDOW parent` after every toplevel), `title`, `appid`,
   `limits`, `decor`, `fullscreen`, `maximize` and `INPUT sock`, and `WINDOW
   hide` for one its program unmapped; each popup's `WINDOW popup`; then
   each shown window's newest frame; then every pointer constraint still
   wanted, `WINDOW lock`. A window keeps its configure until the desktop
   sizes it again: the next comes with the desktop's `WINDOW size`. */

#define BC_RECONNECT_MS 60000u
#define BC_BACKOFF_FIRST_MS 50u
#define BC_BACKOFF_MAX_MS 1000u
#define BC_HELLO_MS 3000u

static uint32_t reconnect_elapsed_ms(struct bc_server *s)
{
    return (uint32_t)((now_us() - s->lost_us) / 1000u);
}

static void dial_end(struct bc_server *s)
{
    if (s->dial_source) wl_event_source_remove(s->dial_source);
    s->dial_source = NULL;
    if (s->dial_fd >= 0) close(s->dial_fd);
    s->dial_fd = -1;
}

/* The next attempt after the backoff, no later than the bound. */
static void redial_later(struct bc_server *s)
{
    uint32_t elapsed = reconnect_elapsed_ms(s), wait = s->backoff_ms;
    if (elapsed < s->bound_ms && wait > s->bound_ms - elapsed) wait = s->bound_ms - elapsed;
    s->backoff_ms = s->backoff_ms * 2 > BC_BACKOFF_MAX_MS ? BC_BACKOFF_MAX_MS : s->backoff_ms * 2;
    wl_event_source_timer_update(s->redial, wait > 0 && elapsed < s->bound_ms ? (int)wait : 1);
}

static void dial_failed(struct bc_server *s, const char *why)
{
    ++s->attempts;
    if (strcmp(why, s->dial_error) != 0) {
        logf_("reconnect: %s (attempt %u, %u ms after the loss); trying again", why, s->attempts,
              reconnect_elapsed_ms(s));
        snprintf(s->dial_error, sizeof(s->dial_error), "%s", why);
    }
    redial_later(s);
}

/* The newest frame of window `x`, sent again on the new channel: the kept
   dmabuf as FRM3 (or a copy, for a peer that does not import), else the
   kept copy as FRM1. */
static void window_resend(struct bc_xdg *x)
{
    struct bc_server *s = x->server;
    struct bc_hold *h, *kept = NULL;
    wl_list_for_each(h, &s->holds, link)
        if (h->id == x->id && h->sequence == 0) kept = h;
    if (kept) {
        struct bc_dmabuf *b = kept->buffer;
        if (b && s->wire.dmabuf && !s->wire.refused) {
            uint64_t seq = bc_wire_send_dmabuf(&s->wire, x->id, (uint32_t)b->width, (uint32_t)b->height,
                                               b->format, b->modifier, b->planes, b->fds,
                                               b->offsets, b->strides);
            if (seq != 0) {
                kept->sequence = seq;
                return;
            }
            logf_("window %u: its newest FRM3 not sent again (%s); copying", x->id, s->wire.last_error);
        }
        if (b && dmabuf_copy(x, b) != 0)
            logf_("window %u: its newest dmabuf frame not shown again: %s", x->id, s->wire.last_error);
        hold_end(kept);
        return;
    }
    if (x->kept.fd < 0) return;
    if (bc_wire_send_frm1(&s->wire, x->id, &x->kept) == 0) x->last_copy = s->wire.sequence;
    else logf_("window %u: its newest frame not sent again: %s", x->id, s->wire.last_error);
}

/* Every window again, with its last state, to the desktop that answered. */
static void reannounce(struct bc_server *s)
{
    BcWire *w = &s->wire;
    struct bc_xdg *x;
    struct bc_constraint *c;
    int windows = 0, frames = 0;
    bc_wire_line(w, "WINDOW display %s", s->socket_name);
    wl_list_for_each(x, &s->windows, link) x->replay = 0;
    /* Toplevels in creation order (the list is newest first). */
    wl_list_for_each_reverse(x, &s->windows, link) {
        if (x->toplevel == NULL) continue;
        if (x->id == 0) {
            /* Never announced: the peer before had no window=1. */
            if (x->initial) announce_toplevel(x);
            if (x->id) x->replay = 1;
            continue;
        }
        if (x->parent && x->parent->id && !x->parent->replay) {
            announce_hints(x, 0);
            x->replay = 2;
        } else {
            announce_hints(x, xdg_parent_id(x));
            x->replay = 1;
        }
        if (x->hidden) bc_wire_line(w, "WINDOW hide %u", x->id);
        ++windows;
    }
    wl_list_for_each_reverse(x, &s->windows, link)
        if (x->replay == 2) bc_wire_line(w, "WINDOW parent %u %d", x->id, xdg_parent_id(x));
    /* Popups, after their parents. */
    wl_list_for_each_reverse(x, &s->windows, link) {
        struct bc_xdg *p = x->parent;
        if (x->popup == NULL || p == NULL || p->id == 0) continue;
        if (x->id == 0) {
            if (x->initial) announce_popup(x);
            continue;
        }
        bc_wire_line(w, "WINDOW popup %u %u %d %d", x->id, p->id, p->gx + x->px, p->gy + x->py);
        if (x->hidden) bc_wire_line(w, "WINDOW hide %u", x->id);
        ++windows;
    }
    /* Each top-level's subsurfaces, after its popups. */
    wl_list_for_each(x, &s->windows, link) x->sub_shown = x->subsurface ? 0 : x->sub_shown;
    wl_list_for_each_reverse(x, &s->windows, link)
        if (x->toplevel && x->id) windows += subs_show(x, NULL, 0);
    /* Each shown window's newest frame. */
    wl_list_for_each_reverse(x, &s->windows, link)
        if (x->id && x->mapped && (!x->subsurface || x->sub_shown)) {
            uint64_t before = w->sequence;
            window_resend(x);
            if (w->sequence != before) ++frames;
        }
    /* The pointer constraints still wanted, asked again. */
    wl_list_for_each(c, &s->constraints, link) constraint_ask(c);
    logf_("reconnect: %d windows announced again, %d frames sent again", windows, frames);
    /* The programs' selections, offered to the new desktop. */
    clip_channel(s, 1);
}

/* The new channel is up: the desktop's (window=1). */
static void channel_up(struct bc_server *s, BcWire *answered)
{
    /* The dmabuf formats offered to clients are the first hello's: a desktop
       that answers others gets copies (FRM1), as a peer that does not
       import would. */
    if (answered->dmabuf && (s->format_table_fd < 0 || answered->format_count != s->wire.format_count ||
                             memcmp(answered->formats, s->wire.formats,
                                    sizeof(BcFormat) * (size_t)s->wire.format_count) != 0)) {
        if (s->format_table_fd >= 0)
            logf_("reconnect: the desktop's dmabuf formats are not the first hello's: copying dmabuf frames");
        answered->dmabuf = 0;
    }
    if (s->format_table_fd >= 0) {
        memcpy(answered->formats, s->wire.formats, sizeof(answered->formats));
        answered->format_count = s->wire.format_count;
    }
    s->wire = *answered;
    s->link = BC_LINK_UP;
    s->frame_source = wl_event_loop_add_fd(s->loop, s->wire.fd, WL_EVENT_READABLE, on_frame_socket, s);
    wl_event_source_timer_update(s->redial, 0);
    logf_("reconnect: frame channel %s again after %u ms, %u attempts: %s", s->frame_socket,
          reconnect_elapsed_ms(s), s->attempts + 1, s->wire.answer);
    reannounce(s);
}

static int on_dial_answer(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    BcWire answered = s->wire;
    (void)mask;
    wl_event_source_remove(s->dial_source);
    s->dial_source = NULL;
    s->dial_fd = -1;
    /* Read into a copy: an answer that is not taken changes nothing. */
    if (bc_wire_answer(&answered, fd) != 0) {
        dial_failed(s, answered.last_error);
        return 0;
    }
    if (!answered.window) {
        close(fd);
        dial_failed(s, "the peer answered without window=1");
        return 0;
    }
    channel_up(s, &answered);
    return 0;
}

static int on_redial(void *data)
{
    struct bc_server *s = data;
    uint32_t elapsed, wait = BC_HELLO_MS;
    int fd;
    if (s->link == BC_LINK_UP || s->quit) return 0;
    if (s->dial_fd >= 0) {
        dial_end(s);
        dial_failed(s, "no answer to the hello");
        return 0;
    }
    elapsed = reconnect_elapsed_ms(s);
    if (elapsed >= s->bound_ms) {
        logf_("reconnect: no desktop answered with window=1 within %u ms of the frame channel's loss "
              "(%u attempts): ending its programs", s->bound_ms, s->attempts);
        s->status = 1;
        wl_display_terminate(s->display);
        return 0;
    }
    fd = bc_wire_dial(&s->wire, s->frame_socket, s->frame_token);
    if (fd < 0) {
        dial_failed(s, s->wire.last_error);
        return 0;
    }
    s->dial_fd = fd;
    s->dial_source = wl_event_loop_add_fd(s->loop, fd, WL_EVENT_READABLE, on_dial_answer, s);
    /* The answer's wait, no later than the bound. */
    if (wait > s->bound_ms - elapsed) wait = s->bound_ms - elapsed;
    wl_event_source_timer_update(s->redial, (int)wait);
    return 0;
}

static void channel_lost(struct bc_server *s, const char *why)
{
    struct bc_xdg *x;
    struct bc_hold *h, *next;
    struct bc_constraint *c;
    if (s->link != BC_LINK_UP || s->quit) return;
    clip_channel(s, 0);
    logf_("the frame channel %s: the desktop is gone; keeping %d windows and reconnecting to %s "
          "for at most %u ms", why, wl_list_length(&s->windows), s->frame_socket, s->bound_ms);
    wl_event_source_remove(s->frame_source);
    s->frame_source = NULL;
    close(s->wire.fd);
    s->wire.fd = -1;
    s->link = BC_LINK_DOWN;
    /* Each shown window's newest frame, when Blit held it as FRM3, is kept
       (sequence 0); every other frame Blit held is given back now. */
    wl_list_for_each(x, &s->windows, link) {
        struct bc_hold *newest = NULL;
        if (x->id == 0) continue;
        wl_list_for_each(h, &s->holds, link)
            if (h->id == x->id && (newest == NULL || h->sequence > newest->sequence)) newest = h;
        if (newest && x->mapped && newest->sequence > x->last_copy) {
            newest->sequence = 0;
            kept_set(x, NULL);
        }
    }
    wl_list_for_each_safe(h, next, &s->holds, link)
        if (h->sequence != 0) hold_end(h);
    /* The desktop's pointer holds ended with it. */
    wl_list_for_each(c, &s->constraints, link) {
        if (c->active) constraint_deactivate(c);
        c->window = 0;
    }
    /* Callbacks that waited for Blit to take a frame are answered now, at
       the refresh; from here every commit is taken as it comes. */
    wl_list_for_each(x, &s->windows, link)
        if (x->surface) frames_taken(x->surface);
    s->lost_us = now_us();
    s->attempts = 0;
    s->dial_error[0] = '\0';
    s->backoff_ms = BC_BACKOFF_FIRST_MS;
    redial_later(s);
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
    fprintf(out, "usage: blit-compositor [--display name] [--log path]\n"
                 "  Blit's Wayland compositor for programs (ADR 0228). Needs BLIT_FRAME_SOCKET,\n"
                 "  BLIT_FRAME_TOKEN and XDG_RUNTIME_DIR; listens on $XDG_RUNTIME_DIR/<name>,\n"
                 "  --display's (refused, status 2, when in use or invalid), else blit-<n>.\n");
}

/* Why `name` cannot be the display --display asks for, or NULL: it is one
   file in $XDG_RUNTIME_DIR, beside `<name>-input-<id>`, and one word on the
   `WINDOW display <name>` line, so printable ASCII with no space or slash,
   at most 31 bytes, and short enough for the input socket's path. */
static const char *display_name_problem(const char *name, const char *runtime)
{
    const char *p;
    if (name[0] == '\0') return "is empty";
    if (strcmp(name, ".") == 0 || strcmp(name, "..") == 0) return "is a directory";
    for (p = name; *p; ++p) {
        if (*p == '/') return "has a slash";
        if ((unsigned char)*p <= ' ' || (unsigned char)*p >= 0x7f)
            return "has a space, a control character or a byte past ASCII";
    }
    if (strlen(name) >= sizeof(((struct bc_server *)0)->socket_name)) return "is longer than 31 bytes";
    if (strlen(runtime) + 1 + strlen(name) + strlen("-input-4294967295") >=
        sizeof(((struct sockaddr_un *)0)->sun_path))
        return "is too long: $XDG_RUNTIME_DIR/<name>-input-<id> passes 107 bytes";
    return NULL;
}

/* BLIT_COMPOSITOR_RECONNECT_MS, for gates only: the reconnect bound in
   milliseconds, 1 to 60000; anything else is the 60 seconds. */
static uint32_t reconnect_bound_ms(void)
{
    const char *text = getenv("BLIT_COMPOSITOR_RECONNECT_MS");
    char *end;
    unsigned long v;
    if (text == NULL || text[0] < '0' || text[0] > '9') return BC_RECONNECT_MS;
    v = strtoul(text, &end, 10);
    if (*end != '\0' || v < 1 || v > BC_RECONNECT_MS) return BC_RECONNECT_MS;
    return (uint32_t)v;
}

int main(int argc, char **argv)
{
    struct bc_server server;
    const char *socket_env = getenv("BLIT_FRAME_SOCKET");
    const char *token = getenv("BLIT_FRAME_TOKEN");
    const char *runtime = getenv("XDG_RUNTIME_DIR");
    const char *log_path = NULL, *display_name = NULL, *problem;
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
    wl_list_init(&server.devices[0]);
    wl_list_init(&server.devices[1]);
    server.line_fd = -1;
    server.keymap_fd = -1;
    server.dial_fd = -1;
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--log") == 0 && i + 1 < argc) {
            log_path = argv[++i];
        } else if (strcmp(argv[i], "--display") == 0 && i + 1 < argc) {
            display_name = argv[++i];
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
    if (display_name && runtime && (problem = display_name_problem(display_name, runtime)) != NULL) {
        fprintf(stderr, "blit-compositor: --display %s %s\n", display_name, problem);
        return 2;
    }
    if (log_path && (server.log = fopen(log_path, "a")) == NULL) {
        fprintf(stderr, "blit-compositor: --log %s: %s\n", log_path, strerror(errno));
        return 2;
    }
    signal(SIGPIPE, SIG_IGN);
    logf_("version %s, libwayland %s", BLIT_BUILD_VERSION, WAYLAND_VERSION);
    if (!socket_env || !token || !runtime) {
        logf_("BLIT_FRAME_SOCKET, BLIT_FRAME_TOKEN and XDG_RUNTIME_DIR are required");
        return 1;
    }
    server.frame_socket = socket_env;
    server.frame_token = token;
    server.bound_ms = reconnect_bound_ms();

    /* The Wayland socket first: a name the desktop chose and cannot have is
       refused before the channel hears of it. */
    server.display = wl_display_create();
    if (server.display == NULL) {
        logf_("wl_display_create failed");
        return 1;
    }
    if (display_name) {
        snprintf(server.socket_name, sizeof(server.socket_name), "%s", display_name);
        if (wl_display_add_socket(server.display, server.socket_name) != 0) {
            int e = errno;
            fprintf(stderr, "blit-compositor: --display %s %s\n", display_name,
                    e == EWOULDBLOCK || e == EAGAIN
                        ? "is in use: another compositor holds its lock"
                        : "is in use or cannot be created in $XDG_RUNTIME_DIR");
            logf_("--display %s: refused (%s)", display_name, strerror(e));
            wl_display_destroy(server.display);
            if (server.log) fclose(server.log);
            return 2;
        }
    } else {
        for (i = 0; i < BC_SOCKETS; ++i) {
            snprintf(server.socket_name, sizeof(server.socket_name), "blit-%d", i);
            if (wl_display_add_socket(server.display, server.socket_name) == 0) break;
        }
        if (i == BC_SOCKETS) {
            logf_("no free socket blit-0 to blit-%d in %s", BC_SOCKETS - 1, runtime);
            wl_display_destroy(server.display);
            return 1;
        }
    }
    if (bc_wire_connect(&server.wire, socket_env, token, (int)BC_HELLO_MS) != 0) {
        logf_("frame channel %s: %s", socket_env, server.wire.last_error);
        wl_display_destroy(server.display);
        return 1;
    }
    logf_("frame channel %s: %s", socket_env, server.wire.answer);
    if (!server.wire.window)
        logf_("the peer answered without window=1: it places no windows, so none is shown "
              "(Blit reached directly answers window=0; the desktop's channel is needed)");

    server.loop = wl_display_get_event_loop(server.display);
    server.xkb = xkb_context_new(XKB_CONTEXT_NO_DEFAULT_INCLUDES | XKB_CONTEXT_NO_ENVIRONMENT_NAMES);
    input_sockets_clear(&server);
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
                          bind_constraints) ||
        !wl_global_create(server.display, &wl_data_device_manager_interface,
                          BC_VERSION(3, wl_data_device_manager_interface), &server,
                          bind_data_device_manager) ||
        !wl_global_create(server.display, &zwp_primary_selection_device_manager_v1_interface,
                          BC_VERSION(1, zwp_primary_selection_device_manager_v1_interface), &server,
                          bind_primary_manager) ||
        !wl_global_create(server.display, &wp_cursor_shape_manager_v1_interface,
                          BC_VERSION(2, wp_cursor_shape_manager_v1_interface), &server,
                          bind_shape_manager)) {
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
          "zwp_relative_pointer_manager_v1 %d, zwp_pointer_constraints_v1 %d, "
          "wl_data_device_manager %d, zwp_primary_selection_device_manager_v1 %d, "
          "wp_cursor_shape_manager_v1 %d",
          BC_VERSION(6, wl_compositor_interface), BC_VERSION(1, wl_subcompositor_interface),
          BC_VERSION(4, wl_output_interface), BC_VERSION(8, wl_seat_interface),
          BC_VERSION(6, xdg_wm_base_interface), BC_VERSION(1, zxdg_decoration_manager_v1_interface),
          BC_VERSION(1, wp_presentation_interface),
          BC_VERSION(1, zwp_relative_pointer_manager_v1_interface),
          BC_VERSION(1, zwp_pointer_constraints_v1_interface),
          BC_VERSION(3, wl_data_device_manager_interface),
          BC_VERSION(1, zwp_primary_selection_device_manager_v1_interface),
          BC_VERSION(2, wp_cursor_shape_manager_v1_interface));
    server.frame_source = wl_event_loop_add_fd(server.loop, server.wire.fd, WL_EVENT_READABLE,
                                               on_frame_socket, &server);
    server.sigterm_source = wl_event_loop_add_signal(server.loop, SIGTERM, on_signal, &server);
    server.sigint_source = wl_event_loop_add_signal(server.loop, SIGINT, on_signal, &server);
    server.redial = wl_event_loop_add_timer(server.loop, on_redial, &server);
    logf_("listening on %s/%s", runtime, server.socket_name);
    bc_wire_line(&server.wire, "WINDOW display %s", server.socket_name);

    wl_display_run(server.display);

    while (!wl_list_empty(&server.input_conns)) {
        struct bc_input_conn *conn = wl_container_of(server.input_conns.next, conn, link);
        wl_event_source_remove(conn->source);
        close(conn->fd);
        wl_list_remove(&conn->link);
        free(conn);
    }
    /* Each window's input socket goes with its window; any left (a
       window never destroyed) go here. */
    wl_display_destroy_clients(server.display);
    input_sockets_clear(&server);
    if (server.frame_source) wl_event_source_remove(server.frame_source);
    if (server.sigterm_source) wl_event_source_remove(server.sigterm_source);
    if (server.sigint_source) wl_event_source_remove(server.sigint_source);
    if (server.redial) wl_event_source_remove(server.redial);
    dial_end(&server);
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
