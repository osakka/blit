/* blit-compositor: Blit's Wayland compositor for programs (ADR 0228,
   phase 1, milestone A).

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
   the client at exactly that size; every committed wl_shm buffer becomes
   one FRM1 frame of that picture, and the buffer is released at once
   because it was copied. Frame callbacks are answered when the frame was
   sent.

   Globals: wl_compositor, wl_subcompositor (subsurfaces accepted and
   logged, not composited), wl_shm (ARGB8888, XRGB8888, libwayland's own),
   wl_output (one global per display mode the desktop names with `WINDOW
   output`; a window is on the first, 1920x1080 at 60 Hz, until then, and
   its surface enters and leaves them as the desktop moves it),
   xdg_wm_base, zxdg_decoration_manager_v1 (server-side: the desktop draws
   frames; a client that insists on its own gets `WINDOW decor <id>
   client`) and wl_seat (keyboard and pointer advertised; input is
   milestone B, so no keymap, focus or events are sent yet). Each is
   offered at the version the task names or the one the build host's
   protocol XML carries, whichever is lower (BC_VERSION).

   Against a peer that answers the hello without window=1 (Blit itself) it
   keeps running, shows nothing and says so in its log: no WINDOW line, no
   frame, and so no configure, which waits for the desktop's size.

   Built with tools/build-tcc.sh compositor, linked against
   libwayland-server.so.0. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>

#include <wayland-server.h>
#include <wayland-server-protocol.h>

#include "xdg-shell-server-protocol.h"
#include "xdg-decoration-unstable-v1-server-protocol.h"
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
    struct wl_list frames;            /* committed, waiting for a frame sent */
    int scale_warned;
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
    FILE *log;
    int status;
};

static struct bc_server *g_server;

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
    if (x->minimize) bc_wire_line(w, "WINDOW minimize %u", x->id);
    x->minimize = 0;
    logf_("window %u: new parent %d title \"%s\" appid \"%s\"", x->id, xdg_parent_id(x),
          x->title, x->appid);
    /* On the first output until the desktop says which display it is on. */
    window_set_output(x, output_default(x->server));
}

/* `WINDOW gone`, once, after everything of the id is released: frames are
   copied, so nothing of it is held when its role object goes. */
static void window_gone(struct bc_xdg *x)
{
    window_set_output(x, NULL);
    if (x->id) {
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

/* The committed buffer as one FRM1 frame of the window's picture. Returns
   1 when a frame was sent. */
static int send_buffer(struct bc_surface *surface, struct wl_resource *buffer)
{
    struct bc_xdg *x = surface->xdg;
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    int rc;
    if (shm == NULL) {
        logf_("window %u: a buffer that is not wl_shm, not shown (dmabuf is milestone B)",
              x ? x->id : 0);
        return 0;
    }
    if (x == NULL || x->id == 0 || !x->server->wire.window)
        return 0;
    wl_shm_buffer_begin_access(shm);
    rc = bc_wire_send_shm(&x->server->wire, x->id, wl_shm_buffer_get_data(shm),
                          (uint32_t)wl_shm_buffer_get_width(shm),
                          (uint32_t)wl_shm_buffer_get_height(shm),
                          (uint32_t)wl_shm_buffer_get_stride(shm),
                          wl_shm_buffer_get_format(shm));
    wl_shm_buffer_end_access(shm);
    if (rc != 0) {
        logf_("window %u: frame not sent: %s", x->id, x->server->wire.last_error);
        return 0;
    }
    return 1;
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

    /* Pending frame callbacks wait for the next frame of this surface. */
    wl_list_insert_list(surface->frames.prev, &surface->pending_frames);
    wl_list_init(&surface->pending_frames);
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
            sent = send_buffer(surface, buffer);
            x->mapped = 1;
        }
    }
    /* Copied or not shown (a subsurface, a cursor): the client's again. */
    if (attached && buffer)
        wl_buffer_send_release(buffer);
    if (sent)
        callbacks_done(&surface->frames);
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

/* ---- wl_seat: advertised; input is milestone B ---- */

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
    struct wl_resource *r = wl_resource_create(client, &wl_pointer_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &pointer_impl, NULL, NULL);
}

static void seat_get_keyboard(struct wl_client *client, struct wl_resource *resource, uint32_t id)
{
    /* The xkb keymap (GET /v1/input/keymap), repeat info, focus and keys
       are milestone B. A keymap is needed only before the first enter,
       which is never sent yet. */
    struct wl_resource *r = wl_resource_create(client, &wl_keyboard_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) { wl_client_post_no_memory(client); return; }
    wl_resource_set_implementation(r, &keyboard_impl, NULL, NULL);
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

/* ---- the desktop's lines ---- */

static struct bc_xdg *window_by_id(struct bc_server *s, uint32_t id)
{
    struct bc_xdg *x;
    wl_list_for_each(x, &s->windows, link)
        if (x->id == id) return x;
    return NULL;
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
    case BC_LINE_ERROR_WINDOW:
        logf_("the desktop refused a WINDOW line for %u: %s", l.id, l.code);
        return;
    case BC_LINE_ERROR:
        logf_("a frame of %u was refused: %s", l.id, l.code);
        return;
    case BC_LINE_OK_WINDOW:
    case BC_LINE_RELEASE:
    case BC_LINE_OTHER:
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
                          bind_decoration_manager)) {
        logf_("could not create the globals");
        wl_display_destroy(server.display);
        return 1;
    }
    logf_("globals: wl_compositor %d, wl_subcompositor %d, wl_shm 1, wl_output %d, wl_seat %d, "
          "xdg_wm_base %d, zxdg_decoration_manager_v1 %d",
          BC_VERSION(6, wl_compositor_interface), BC_VERSION(1, wl_subcompositor_interface),
          BC_VERSION(4, wl_output_interface), BC_VERSION(8, wl_seat_interface),
          BC_VERSION(6, xdg_wm_base_interface), BC_VERSION(1, zxdg_decoration_manager_v1_interface));
    server.frame_source = wl_event_loop_add_fd(server.loop, server.wire.fd, WL_EVENT_READABLE,
                                               on_frame_socket, &server);
    server.sigterm_source = wl_event_loop_add_signal(server.loop, SIGTERM, on_signal, &server);
    server.sigint_source = wl_event_loop_add_signal(server.loop, SIGINT, on_signal, &server);
    logf_("listening on %s/%s", getenv("XDG_RUNTIME_DIR"), server.socket_name);
    bc_wire_line(&server.wire, "WINDOW display %s", server.socket_name);

    wl_display_run(server.display);

    wl_display_destroy_clients(server.display);
    if (server.frame_source) wl_event_source_remove(server.frame_source);
    if (server.sigterm_source) wl_event_source_remove(server.sigterm_source);
    if (server.sigint_source) wl_event_source_remove(server.sigint_source);
    wl_display_destroy(server.display);
    while (!wl_list_empty(&server.outputs)) {
        struct bc_output *o = output_default(&server);
        wl_list_remove(&o->link);
        free(o);
    }
    logf_("stopped: %lu frames sent, %lu failed, %lu WINDOW lines", server.wire.frames_sent,
          server.wire.frames_failed, server.wire.lines_sent);
    if (server.wire.fd >= 0) close(server.wire.fd);
    if (server.log) fclose(server.log);
    return server.status;
}
