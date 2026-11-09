/* ADR 0228 phase 1: an in-repo Wayland client for blit-compositor's gates,
   on libwayland-client (the frame_dmabuf_producer.c pattern: a stand-in
   for the real programs the gate cannot run).

     wl_standin_client [--popup] [title]

   It binds wl_compositor, wl_shm, xdg_wm_base, wl_seat (taking a keyboard
   and a pointer, as programs do), wl_output (printing each mode it is
   told) and, when offered, zxdg_decoration_manager_v1 (asking for
   server-side decorations), creates
   one xdg_toplevel with the title (default "Blit stand-in"), app id
   "blit.standin" and a minimum size of 64x48, and commits without a buffer.
   On each configure it draws the pattern below at exactly the configured
   size into a fresh XRGB8888 wl_shm buffer and commits it, asking for a
   frame callback; a 0x0 configure means it chooses, 256x160. It exits 0 on
   xdg_toplevel.close.

   With --popup, after its first frame it opens one xdg_popup of 80x60
   (anchor rectangle 10,20 30x40, anchor bottom-left, gravity bottom-right,
   offset 2,3: at 12,63 of the toplevel), draws the same pattern in it on
   its configure, and destroys it on popup_done.

   The pattern, which compositor_gate.py checks pixel for pixel: at (x, y)
   red = x & 255, green = y & 255,
   blue = 0x80 | ((x >> 8) & 7) << 3 | ((y >> 8) & 7).

   It prints one line per thing it did, `STANDIN <what> ...`, and at exit
   `STANDIN done draws=<n> releases=<n> frames=<n>`. */
#define _GNU_SOURCE
#include <errno.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <unistd.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"

struct standin {
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct wl_output *output;
    int32_t mode_w, mode_h;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    int32_t want_w, want_h;
    int closed;
    int want_popup;
    struct wl_surface *popup_surface;
    struct xdg_surface *popup_xdg;
    struct xdg_popup *popup;
    unsigned draws, releases, frames;
};

struct standin_buffer {
    struct standin *s;
    struct wl_buffer *buffer;
    void *data;
    size_t size;
};

static uint32_t pattern(uint32_t x, uint32_t y)
{
    uint32_t r = x & 255, g = y & 255, b = 0x80 | ((x >> 8) & 7) << 3 | ((y >> 8) & 7);
    return r << 16 | g << 8 | b;
}

static void buffer_release(void *data, struct wl_buffer *buffer)
{
    struct standin_buffer *b = data;
    ++b->s->releases;
    wl_buffer_destroy(buffer);
    munmap(b->data, b->size);
    free(b);
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct standin *s = data;
    (void)time;
    ++s->frames;
    wl_callback_destroy(cb);
}

static const struct wl_callback_listener frame_listener = { frame_done };

static int draw(struct standin *s, struct wl_surface *surface, int32_t w, int32_t h)
{
    struct standin_buffer *b = calloc(1, sizeof(*b));
    struct wl_shm_pool *pool;
    struct wl_callback *cb;
    int32_t stride = w * 4, x, y;
    int fd;
    if (b == NULL) return -1;
    b->s = s;
    b->size = (size_t)stride * (size_t)h;
    fd = memfd_create("wl-standin", MFD_CLOEXEC);
    if (fd < 0 || ftruncate(fd, (off_t)b->size) != 0) {
        perror("STANDIN memfd");
        return -1;
    }
    b->data = mmap(NULL, b->size, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (b->data == MAP_FAILED) {
        perror("STANDIN mmap");
        return -1;
    }
    for (y = 0; y < h; ++y) {
        uint32_t *row = (uint32_t *)((char *)b->data + (size_t)y * stride);
        for (x = 0; x < w; ++x) row[x] = pattern((uint32_t)x, (uint32_t)y);
    }
    pool = wl_shm_create_pool(s->shm, fd, (int32_t)b->size);
    b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
    wl_shm_pool_destroy(pool);
    close(fd);
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    wl_surface_attach(surface, b->buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, w, h);
    cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, s);
    wl_surface_commit(surface);
    ++s->draws;
    printf("STANDIN drew %s%dx%d\n", surface == s->surface ? "" : "popup ", w, h);
    fflush(stdout);
    return 0;
}

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
                               struct wl_array *states)
{
    struct standin *s = data;
    uint32_t *state;
    int fullscreen = 0, activated = 0;
    (void)t;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen = 1;
        if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) activated = 1;
    }
    printf("STANDIN configure %dx%d%s%s\n", w, h, fullscreen ? " fullscreen" : "",
           activated ? " activated" : "");
    s->want_w = w > 0 ? w : 256;
    s->want_h = h > 0 ? h : 160;
}

static void toplevel_close(void *data, struct xdg_toplevel *t)
{
    struct standin *s = data;
    (void)t;
    printf("STANDIN close\n");
    s->closed = 1;
}

static void toplevel_bounds(void *data, struct xdg_toplevel *t, int32_t w, int32_t h)
{
    (void)data; (void)t; (void)w; (void)h;
}

static void toplevel_capabilities(void *data, struct xdg_toplevel *t, struct wl_array *caps)
{
    uint32_t *cap;
    (void)data; (void)t;
    printf("STANDIN capabilities");
    wl_array_for_each(cap, caps) printf(" %u", *cap);
    printf("\n");
}

static const struct xdg_toplevel_listener toplevel_listener = {
    toplevel_configure, toplevel_close, toplevel_bounds, toplevel_capabilities,
};

static void popup_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
    struct standin *s = data;
    xdg_surface_ack_configure(xs, serial);
    draw(s, s->popup_surface, 80, 60);
}

static const struct xdg_surface_listener popup_surface_listener = { popup_surface_configure };

static void popup_configure(void *data, struct xdg_popup *p, int32_t x, int32_t y, int32_t w, int32_t h)
{
    (void)data; (void)p;
    printf("STANDIN popup configure %d,%d %dx%d\n", x, y, w, h);
}

static void popup_done(void *data, struct xdg_popup *p)
{
    struct standin *s = data;
    (void)p;
    printf("STANDIN popup done\n");
    xdg_popup_destroy(s->popup);
    xdg_surface_destroy(s->popup_xdg);
    wl_surface_destroy(s->popup_surface);
    s->popup = NULL;
    s->popup_xdg = NULL;
    s->popup_surface = NULL;
}

static void popup_repositioned(void *data, struct xdg_popup *p, uint32_t token)
{
    (void)data; (void)p; (void)token;
}

static const struct xdg_popup_listener popup_listener = {
    popup_configure, popup_done, popup_repositioned,
};

static void open_popup(struct standin *s)
{
    struct xdg_positioner *pos = xdg_wm_base_create_positioner(s->wm_base);
    xdg_positioner_set_size(pos, 80, 60);
    xdg_positioner_set_anchor_rect(pos, 10, 20, 30, 40);
    xdg_positioner_set_anchor(pos, XDG_POSITIONER_ANCHOR_BOTTOM_LEFT);
    xdg_positioner_set_gravity(pos, XDG_POSITIONER_GRAVITY_BOTTOM_RIGHT);
    xdg_positioner_set_offset(pos, 2, 3);
    s->popup_surface = wl_compositor_create_surface(s->compositor);
    s->popup_xdg = xdg_wm_base_get_xdg_surface(s->wm_base, s->popup_surface);
    xdg_surface_add_listener(s->popup_xdg, &popup_surface_listener, s);
    s->popup = xdg_surface_get_popup(s->popup_xdg, s->xdg_surface, pos);
    xdg_popup_add_listener(s->popup, &popup_listener, s);
    xdg_positioner_destroy(pos);
    wl_surface_commit(s->popup_surface);
    printf("STANDIN popup opened\n");
}

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
    struct standin *s = data;
    xdg_surface_ack_configure(xs, serial);
    draw(s, s->surface, s->want_w, s->want_h);
    if (s->want_popup) {
        s->want_popup = 0;
        open_popup(s);
    }
}

static const struct xdg_surface_listener xdg_surface_listener = { xdg_surface_configure };

static void decoration_configure(void *data, struct zxdg_toplevel_decoration_v1 *d, uint32_t mode)
{
    (void)data; (void)d;
    printf("STANDIN decoration %s\n",
           mode == ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE ? "server" : "client");
}

static const struct zxdg_toplevel_decoration_v1_listener decoration_listener = { decoration_configure };

static void wm_ping(void *data, struct xdg_wm_base *wm, uint32_t serial)
{
    (void)data;
    xdg_wm_base_pong(wm, serial);
}

static const struct xdg_wm_base_listener wm_listener = { wm_ping };

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct standin *s = data;
    printf("STANDIN seat capabilities %u\n", caps);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) s->keyboard = wl_seat_get_keyboard(seat);
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) s->pointer = wl_seat_get_pointer(seat);
}

static void seat_name(void *data, struct wl_seat *seat, const char *name)
{
    (void)data; (void)seat;
    printf("STANDIN seat name %s\n", name);
}

static const struct wl_seat_listener seat_listener = { seat_capabilities, seat_name };

static void output_geometry(void *data, struct wl_output *o, int32_t x, int32_t y, int32_t pw,
                            int32_t ph, int32_t subpixel, const char *make, const char *model,
                            int32_t transform)
{
    (void)data; (void)o; (void)x; (void)y; (void)pw; (void)ph; (void)subpixel; (void)transform;
    printf("STANDIN output geometry %s %s\n", make, model);
}

static void output_mode(void *data, struct wl_output *o, uint32_t flags, int32_t w, int32_t h,
                        int32_t refresh)
{
    struct standin *s = data;
    (void)o; (void)flags; (void)refresh;
    s->mode_w = w;
    s->mode_h = h;
}

static void output_done(void *data, struct wl_output *o)
{
    struct standin *s = data;
    (void)o;
    printf("STANDIN output mode %dx%d\n", s->mode_w, s->mode_h);
}

static void output_scale(void *data, struct wl_output *o, int32_t factor)
{
    (void)data; (void)o;
    printf("STANDIN output scale %d\n", factor);
}

static void output_name(void *data, struct wl_output *o, const char *name)
{
    (void)data; (void)o;
    printf("STANDIN output name %s\n", name);
}

static void output_description(void *data, struct wl_output *o, const char *description)
{
    (void)data; (void)o; (void)description;
}

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    struct standin *s = data;
    printf("STANDIN global %s %u\n", interface, version);
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        s->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                      version < 5 ? version : 5);
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        s->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 8 ? version : 8);
        wl_seat_add_listener(s->seat, &seat_listener, s);
    } else if (strcmp(interface, wl_output_interface.name) == 0) {
        s->output = wl_registry_bind(registry, name, &wl_output_interface, version < 4 ? version : 4);
        wl_output_add_listener(s->output, &output_listener, s);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        s->decoration_manager = wl_registry_bind(registry, name,
                                                 &zxdg_decoration_manager_v1_interface, 1);
}

static void global_remove(void *data, struct wl_registry *registry, uint32_t name)
{
    (void)data; (void)registry; (void)name;
}

static const struct wl_registry_listener registry_listener = { global, global_remove };

int main(int argc, char **argv)
{
    struct standin s;
    struct wl_registry *registry;
    const char *title = "Blit stand-in";
    int i;

    memset(&s, 0, sizeof(s));
    for (i = 1; i < argc; ++i) {
        if (strcmp(argv[i], "--popup") == 0) s.want_popup = 1;
        else title = argv[i];
    }
    s.display = wl_display_connect(NULL);
    if (s.display == NULL) {
        fprintf(stderr, "STANDIN cannot connect to %s: %s\n",
                getenv("WAYLAND_DISPLAY") ? getenv("WAYLAND_DISPLAY") : "(unset)", strerror(errno));
        return 1;
    }
    registry = wl_display_get_registry(s.display);
    wl_registry_add_listener(registry, &registry_listener, &s);
    wl_display_roundtrip(s.display);
    wl_display_roundtrip(s.display); /* the seat's and the output's events */
    if (!s.compositor || !s.shm || !s.wm_base) {
        fprintf(stderr, "STANDIN missing wl_compositor, wl_shm or xdg_wm_base\n");
        return 1;
    }
    xdg_wm_base_add_listener(s.wm_base, &wm_listener, &s);
    s.surface = wl_compositor_create_surface(s.compositor);
    s.xdg_surface = xdg_wm_base_get_xdg_surface(s.wm_base, s.surface);
    xdg_surface_add_listener(s.xdg_surface, &xdg_surface_listener, &s);
    s.toplevel = xdg_surface_get_toplevel(s.xdg_surface);
    xdg_toplevel_add_listener(s.toplevel, &toplevel_listener, &s);
    xdg_toplevel_set_title(s.toplevel, title);
    xdg_toplevel_set_app_id(s.toplevel, "blit.standin");
    xdg_toplevel_set_min_size(s.toplevel, 64, 48);
    if (s.decoration_manager) {
        s.decoration = zxdg_decoration_manager_v1_get_toplevel_decoration(s.decoration_manager,
                                                                           s.toplevel);
        zxdg_toplevel_decoration_v1_add_listener(s.decoration, &decoration_listener, &s);
        zxdg_toplevel_decoration_v1_set_mode(s.decoration,
                                             ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE);
    }
    wl_surface_commit(s.surface);
    printf("STANDIN toplevel \"%s\" committed, waiting for a configure\n", title);
    fflush(stdout);
    while (!s.closed && wl_display_dispatch(s.display) >= 0)
        fflush(stdout);
    if (!s.closed) {
        fprintf(stderr, "STANDIN the connection ended before close\n");
        return 1;
    }
    if (s.popup) popup_done(&s, s.popup);
    if (s.decoration) zxdg_toplevel_decoration_v1_destroy(s.decoration);
    xdg_toplevel_destroy(s.toplevel);
    xdg_surface_destroy(s.xdg_surface);
    wl_surface_destroy(s.surface);
    if (s.keyboard) wl_keyboard_release(s.keyboard);
    if (s.pointer) wl_pointer_release(s.pointer);
    if (s.seat) wl_seat_release(s.seat);
    if (s.output) wl_output_release(s.output);
    if (s.decoration_manager) zxdg_decoration_manager_v1_destroy(s.decoration_manager);
    xdg_wm_base_destroy(s.wm_base);
    wl_shm_destroy(s.shm);
    wl_compositor_destroy(s.compositor);
    wl_registry_destroy(registry);
    wl_display_roundtrip(s.display);
    printf("STANDIN done draws=%u releases=%u frames=%u\n", s.draws, s.releases, s.frames);
    wl_display_disconnect(s.display);
    return 0;
}
