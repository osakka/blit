/* ADR 0228 phase 1: an in-repo Wayland client for blit-compositor's gates,
   on libwayland-client (the frame_dmabuf_producer.c pattern: a stand-in
   for the real programs the gate cannot run).

     wl_standin_client [--popup] [--states] [--dmabuf] [--animate n] [--lock] [--sub] [title]

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

   With --sub, each configure that carries the desktop's size runs one
   step of a wl_subsurface script after its own frame (printed `STANDIN
   sub step <n>`): 1 a synchronized subsurface at 20,30, its 64x40 frame
   committed and only then the parent's commit; 2 desynchronized, moved
   to 40,10 and a 48x24 frame committed before the parent's commit; 3 an
   empty input region; 4 the whole surface again; 5 placed below the
   parent; 6 the wl_subsurface destroyed. Its pointer events name it `sub`.

   With --popup, after its first frame it opens one xdg_popup of 80x60
   (anchor rectangle 10,20 30x40, anchor bottom-left, gravity bottom-right,
   offset 2,3: at 12,63 of the toplevel), draws the same pattern in it on
   its configure, and destroys it on popup_done.

   With --states, after its first frame it asks to be maximized, minimized
   and unmaximized, then opens a second toplevel, "Child", gives it this
   one as its transient parent after its initial commit, unmaps it after
   its first frame (attach NULL) and maps it again at once, and after that
   second frame clears its parent. Every surface prints the outputs it
   enters and leaves (`STANDIN [child ]enter <name> <w>x<h>@<mHz>`).

   With --dmabuf, every buffer is a zwp_linux_dmabuf_v1 one: XRGB8888,
   LINEAR, one plane. This host has no DRM device and no /dev/udmabuf, so
   the plane is a memfd -- descriptor passing and the wire are what is
   tested; a GPU's dmabuf is the live check. It prints each buffer's plane
   as `STANDIN dmabuf <w>x<h> file <dev>:<inode>`, what the default
   feedback offered, and each release as `STANDIN release <w>x<h>`.

   With --lock, after its first frame it takes zwp_pointer_constraints_v1
   and locks the pointer to its surface, oneshot; each Space pressed (key
   57) destroys the constraint it has and takes the next: a persistent
   lock, then a persistent confinement, then nothing more. It prints
   `STANDIN lock oneshot|persistent`, `STANDIN confine persistent`, and the
   events `STANDIN locked|unlocked|confined|unconfined`; at exit it destroys
   its toplevel and surface before the constraint, so the window goes with
   the constraint still there.

   With --animate n, it draws again on each of the next n frame callbacks
   once a configure has carried the desktop's size, as games do, and
   prints `STANDIN animated <n> in <ms> ms` from the draw before them. Every draw asks for wp_presentation feedback when it is offered;
   the exit line before `done` counts it: `STANDIN presentation
   presented=<n> discarded=<n> refresh=<ns>`.

   It prints its keyboard's and pointer's events: `STANDIN keymap
   format=<n> size=<bytes> xkb=<1 if the text is an xkb keymap>`,
   `STANDIN repeat <rate> <delay>`, `STANDIN keyboard enter|leave
   <main|popup|child>`, `STANDIN key <code> <state>`, `STANDIN modifiers
   <depressed> <latched> <locked> <group>`, `STANDIN pointer enter <surface>
   <x> <y>`, `STANDIN pointer leave <surface>`, `STANDIN pointer motion <x>
   <y>`, `STANDIN button <code> <state>`, `STANDIN axis <axis> <value>`,
   `STANDIN axis120 <axis> <v120>`, `STANDIN axis source <n>`, `STANDIN
   pointer frame` and, from zwp_relative_pointer_v1 when it is offered,
   `STANDIN relative <dx> <dy>`.

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

#include <sys/stat.h>

#include <wayland-client.h>
#include "xdg-shell-client-protocol.h"
#include "xdg-decoration-unstable-v1-client-protocol.h"
#include "linux-dmabuf-unstable-v1-client-protocol.h"
#include "presentation-time-client-protocol.h"
#include "relative-pointer-unstable-v1-client-protocol.h"
#include "pointer-constraints-unstable-v1-client-protocol.h"
#include <time.h>

#define FOURCC_XR24 0x34325258u

struct standin {
    int surface_configures;           /* xdg_surface configures received */
    int sized, child_sized;           /* the last configure carried a size */
    struct wl_display *display;
    struct wl_compositor *compositor;
    struct wl_shm *shm;
    struct xdg_wm_base *wm_base;
    struct zxdg_decoration_manager_v1 *decoration_manager;
    struct zwp_linux_dmabuf_v1 *dmabuf;
    struct zwp_linux_dmabuf_feedback_v1 *feedback;
    int use_dmabuf;
    unsigned feedback_pairs, feedback_tranche;
    int feedback_xr24_linear;
    struct wp_presentation *presentation;
    struct zwp_relative_pointer_manager_v1 *relative_manager;
    struct zwp_relative_pointer_v1 *relative;
    /* --lock: the constraints and the step reached (1 oneshot lock, 2
       persistent lock, 3 persistent confinement) */
    struct zwp_pointer_constraints_v1 *constraints;
    struct zwp_locked_pointer_v1 *locked;
    struct zwp_confined_pointer_v1 *confined;
    int lock, lock_step;
    unsigned presented, discarded;
    uint32_t refresh;
    int animate, animated;
    struct wl_callback *callbacks[16];   /* frame callbacks not yet answered */
    struct timespec first_draw;
    struct wl_seat *seat;
    struct wl_keyboard *keyboard;
    struct wl_pointer *pointer;
    struct standin_output {
        struct wl_output *output;
        char name[32];
        int32_t mode_w, mode_h, refresh;
    } outputs[8];
    int output_count;
    struct wl_surface *surface;
    struct xdg_surface *xdg_surface;
    struct xdg_toplevel *toplevel;
    struct zxdg_toplevel_decoration_v1 *decoration;
    int32_t want_w, want_h;
    int closed;
    int want_popup;
    struct wl_surface *popup_surface;
    /* --sub */
    int sub_script, sub_step;
    struct wl_subcompositor *subcompositor;
    struct wl_surface *sub_surface;
    struct wl_subsurface *subsurface;
    struct xdg_surface *popup_xdg;
    struct xdg_popup *popup;
    /* --states: maximize/minimize requests, a child toplevel that is given
       a parent, hidden, shown again and orphaned */
    int states, states_step;
    struct wl_surface *child_surface;
    struct xdg_surface *child_xdg;
    struct xdg_toplevel *child;
    int32_t child_w, child_h;
    unsigned draws, releases, frames;
};

struct standin_buffer {
    struct standin *s;
    struct wl_buffer *buffer;
    void *data;
    size_t size;
    int32_t w, h;
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
    if (b->s->use_dmabuf) printf("STANDIN release %dx%d\n", b->w, b->h);
    wl_buffer_destroy(buffer);
    munmap(b->data, b->size);
    free(b);
}

static const struct wl_buffer_listener buffer_listener = { buffer_release };

static int draw(struct standin *s, struct wl_surface *surface, int32_t w, int32_t h);

static void frame_done(void *data, struct wl_callback *cb, uint32_t time)
{
    struct standin *s = data;
    size_t i;
    (void)time;
    ++s->frames;
    for (i = 0; i < sizeof(s->callbacks) / sizeof(s->callbacks[0]); ++i)
        if (s->callbacks[i] == cb) s->callbacks[i] = NULL;
    wl_callback_destroy(cb);
    /* --animate runs from the desktop's size on, like the other steps. */
    if (s->animate > 0 && s->sized) {
        struct timespec now;
        --s->animate;
        ++s->animated;
        if (s->animate == 0) {
            clock_gettime(CLOCK_MONOTONIC, &now);
            printf("STANDIN animated %d in %ld ms\n", s->animated,
                   (long)((now.tv_sec - s->first_draw.tv_sec) * 1000 +
                          (now.tv_nsec - s->first_draw.tv_nsec) / 1000000));
        }
        draw(s, s->surface, s->want_w, s->want_h);
    }
}

static const struct wl_callback_listener frame_listener = { frame_done };

static void presentation_sync_output(void *data, struct wp_presentation_feedback *f,
                                     struct wl_output *output)
{
    (void)data; (void)f; (void)output;
}

static void presentation_presented(void *data, struct wp_presentation_feedback *f, uint32_t sec_hi,
                                   uint32_t sec_lo, uint32_t nsec, uint32_t refresh, uint32_t seq_hi,
                                   uint32_t seq_lo, uint32_t flags)
{
    struct standin *s = data;
    (void)sec_hi; (void)sec_lo; (void)nsec; (void)seq_hi; (void)seq_lo; (void)flags;
    ++s->presented;
    s->refresh = refresh;
    wp_presentation_feedback_destroy(f);
}

static void presentation_discarded(void *data, struct wp_presentation_feedback *f)
{
    struct standin *s = data;
    ++s->discarded;
    wp_presentation_feedback_destroy(f);
}

static const struct wp_presentation_feedback_listener presentation_feedback_listener = {
    presentation_sync_output, presentation_presented, presentation_discarded,
};

static int draw(struct standin *s, struct wl_surface *surface, int32_t w, int32_t h)
{
    struct standin_buffer *b = calloc(1, sizeof(*b));
    struct wl_shm_pool *pool;
    struct wl_callback *cb;
    int32_t stride = w * 4, x, y;
    int fd;
    if (b == NULL) return -1;
    b->s = s;
    b->w = w;
    b->h = h;
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
    if (s->use_dmabuf) {
        struct zwp_linux_buffer_params_v1 *params = zwp_linux_dmabuf_v1_create_params(s->dmabuf);
        struct stat st;
        fstat(fd, &st);
        zwp_linux_buffer_params_v1_add(params, fd, 0, 0, (uint32_t)stride, 0, 0);
        b->buffer = zwp_linux_buffer_params_v1_create_immed(params, w, h, FOURCC_XR24, 0);
        zwp_linux_buffer_params_v1_destroy(params);
        printf("STANDIN dmabuf %dx%d file %lu:%lu\n", w, h, (unsigned long)st.st_dev,
               (unsigned long)st.st_ino);
    } else {
        pool = wl_shm_create_pool(s->shm, fd, (int32_t)b->size);
        b->buffer = wl_shm_pool_create_buffer(pool, 0, w, h, stride, WL_SHM_FORMAT_XRGB8888);
        wl_shm_pool_destroy(pool);
    }
    close(fd);
    wl_buffer_add_listener(b->buffer, &buffer_listener, b);
    wl_surface_attach(surface, b->buffer, 0, 0);
    wl_surface_damage_buffer(surface, 0, 0, w, h);
    cb = wl_surface_frame(surface);
    wl_callback_add_listener(cb, &frame_listener, s);
    for (x = 0; x < (int32_t)(sizeof(s->callbacks) / sizeof(s->callbacks[0])); ++x)
        if (s->callbacks[x] == NULL) {
            s->callbacks[x] = cb;
            break;
        }
    if (s->presentation)
        wp_presentation_feedback_add_listener(wp_presentation_feedback(s->presentation, surface),
                                              &presentation_feedback_listener, s);
    if (s->animated == 0) clock_gettime(CLOCK_MONOTONIC, &s->first_draw);
    wl_surface_commit(surface);
    ++s->draws;
    printf("STANDIN drew %s%dx%d\n", surface == s->surface ? "" :
           surface == s->child_surface ? "child " : surface == s->sub_surface ? "sub " : "popup ", w, h);
    fflush(stdout);
    return 0;
}

static void toplevel_configure(void *data, struct xdg_toplevel *t, int32_t w, int32_t h,
                               struct wl_array *states)
{
    struct standin *s = data;
    uint32_t *state;
    int fullscreen = 0, maximized = 0, activated = 0;
    wl_array_for_each(state, states) {
        if (*state == XDG_TOPLEVEL_STATE_FULLSCREEN) fullscreen = 1;
        if (*state == XDG_TOPLEVEL_STATE_MAXIMIZED) maximized = 1;
        if (*state == XDG_TOPLEVEL_STATE_ACTIVATED) activated = 1;
    }
    printf("STANDIN %sconfigure %dx%d%s%s%s\n", t == s->child ? "child " : "", w, h,
           fullscreen ? " fullscreen" : "", maximized ? " maximized" : "",
           activated ? " activated" : "");
    /* 0x0 is the compositor's reply before the desktop has sized the
       window: the client picks. The scripted steps wait for a sized one. */
    if (t == s->child) {
        s->child_w = w > 0 ? w : 50;
        s->child_h = h > 0 ? h : 40;
        s->child_sized = w > 0 && h > 0;
        return;
    }
    s->want_w = w > 0 ? w : 256;
    s->want_h = h > 0 ? h : 160;
    s->sized = w > 0 && h > 0;
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

static const struct xdg_toplevel_listener toplevel_listener;
static const struct wl_surface_listener surface_listener;

static void child_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
    struct standin *s = data;
    xdg_surface_ack_configure(xs, serial);
    draw(s, s->child_surface, s->child_w, s->child_h);
    if (!s->child_sized) return;
    if (s->states_step == 1) {
        /* Unmapped (WINDOW hide), then an initial commit again: configured
           at the desktop's last size, it draws and shows again. */
        s->states_step = 2;
        wl_surface_attach(s->child_surface, NULL, 0, 0);
        wl_surface_commit(s->child_surface);
        printf("STANDIN child hidden\n");
        wl_surface_commit(s->child_surface);
    } else if (s->states_step == 2) {
        s->states_step = 3;
        xdg_toplevel_set_parent(s->child, NULL);
        printf("STANDIN child orphaned\n");
    }
}

static const struct xdg_surface_listener child_surface_listener = { child_surface_configure };

/* --states, after the first frame: the window-state requests, then a
   child toplevel given this one as its parent after its WINDOW new. */
static void run_states(struct standin *s)
{
    xdg_toplevel_set_maximized(s->toplevel);
    xdg_toplevel_set_minimized(s->toplevel);
    xdg_toplevel_unset_maximized(s->toplevel);
    s->child_surface = wl_compositor_create_surface(s->compositor);
    wl_surface_add_listener(s->child_surface, &surface_listener, s);
    s->child_xdg = xdg_wm_base_get_xdg_surface(s->wm_base, s->child_surface);
    xdg_surface_add_listener(s->child_xdg, &child_surface_listener, s);
    s->child = xdg_surface_get_toplevel(s->child_xdg);
    xdg_toplevel_add_listener(s->child, &toplevel_listener, s);
    xdg_toplevel_set_title(s->child, "Child");
    wl_surface_commit(s->child_surface);
    xdg_toplevel_set_parent(s->child, s->toplevel);
    s->states_step = 1;
    printf("STANDIN states requested\n");
}

static void locked_locked(void *data, struct zwp_locked_pointer_v1 *l)
{
    (void)data; (void)l;
    printf("STANDIN locked\n");
}

static void locked_unlocked(void *data, struct zwp_locked_pointer_v1 *l)
{
    (void)data; (void)l;
    printf("STANDIN unlocked\n");
}

static const struct zwp_locked_pointer_v1_listener locked_listener = { locked_locked, locked_unlocked };

static void confined_confined(void *data, struct zwp_confined_pointer_v1 *c)
{
    (void)data; (void)c;
    printf("STANDIN confined\n");
}

static void confined_unconfined(void *data, struct zwp_confined_pointer_v1 *c)
{
    (void)data; (void)c;
    printf("STANDIN unconfined\n");
}

static const struct zwp_confined_pointer_v1_listener confined_listener = {
    confined_confined, confined_unconfined,
};

/* --lock: the constraint it has destroyed, the next one taken. */
static void lock_next(struct standin *s)
{
    if (!s->constraints || !s->pointer || s->lock_step >= 3) return;
    if (s->locked) zwp_locked_pointer_v1_destroy(s->locked);
    if (s->confined) zwp_confined_pointer_v1_destroy(s->confined);
    s->locked = NULL;
    s->confined = NULL;
    ++s->lock_step;
    if (s->lock_step < 3) {
        s->locked = zwp_pointer_constraints_v1_lock_pointer(
            s->constraints, s->surface, s->pointer, NULL,
            s->lock_step == 1 ? ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_ONESHOT
                              : ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        zwp_locked_pointer_v1_add_listener(s->locked, &locked_listener, s);
        printf("STANDIN lock %s\n", s->lock_step == 1 ? "oneshot" : "persistent");
    } else {
        s->confined = zwp_pointer_constraints_v1_confine_pointer(
            s->constraints, s->surface, s->pointer, NULL, ZWP_POINTER_CONSTRAINTS_V1_LIFETIME_PERSISTENT);
        zwp_confined_pointer_v1_add_listener(s->confined, &confined_listener, s);
        printf("STANDIN confine persistent\n");
    }
}

/* --sub: one step per sized configure, after the parent's own frame. */
static void sub_next(struct standin *s)
{
    struct wl_region *empty;
    switch (++s->sub_step) {
    case 1:
        s->sub_surface = wl_compositor_create_surface(s->compositor);
        wl_surface_add_listener(s->sub_surface, &surface_listener, s);
        s->subsurface = wl_subcompositor_get_subsurface(s->subcompositor, s->sub_surface, s->surface);
        wl_subsurface_set_position(s->subsurface, 20, 30);
        draw(s, s->sub_surface, 64, 40);
        /* Synchronized: nothing until the parent's commit, which comes
           only after the compositor has seen this one. */
        wl_display_roundtrip(s->display);
        printf("STANDIN sub committed, parent next\n");
        fflush(stdout);
        usleep(400000);
        wl_surface_commit(s->surface);
        break;
    case 2:
        wl_subsurface_set_desync(s->subsurface);
        wl_subsurface_set_position(s->subsurface, 40, 10);
        draw(s, s->sub_surface, 48, 24);
        wl_display_roundtrip(s->display);
        printf("STANDIN sub desync drawn, parent next\n");
        fflush(stdout);
        usleep(400000);
        wl_surface_commit(s->surface);
        break;
    case 3:
        empty = wl_compositor_create_region(s->compositor);
        wl_surface_set_input_region(s->sub_surface, empty);
        wl_region_destroy(empty);
        wl_surface_commit(s->sub_surface);
        wl_surface_commit(s->surface);
        break;
    case 4:
        wl_surface_set_input_region(s->sub_surface, NULL);
        wl_surface_commit(s->sub_surface);
        wl_surface_commit(s->surface);
        break;
    case 5:
        wl_subsurface_place_below(s->subsurface, s->surface);
        wl_surface_commit(s->surface);
        break;
    case 6:
        wl_subsurface_destroy(s->subsurface);
        s->subsurface = NULL;
        wl_surface_commit(s->surface);
        break;
    default:
        return;
    }
    printf("STANDIN sub step %d\n", s->sub_step);
    fflush(stdout);
}

static void xdg_surface_configure(void *data, struct xdg_surface *xs, uint32_t serial)
{
    struct standin *s = data;
    ++s->surface_configures;
    xdg_surface_ack_configure(xs, serial);
    draw(s, s->surface, s->want_w, s->want_h);
    if (!s->sized) return;
    if (s->sub_script) sub_next(s);
    if (s->lock && s->lock_step == 0) lock_next(s);
    if (s->want_popup) {
        s->want_popup = 0;
        open_popup(s);
    }
    if (s->states && s->states_step == 0) run_states(s);
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

static const char *surface_name(struct standin *s, struct wl_surface *surface)
{
    return surface == NULL ? "none" : surface == s->surface ? "main" :
           surface == s->popup_surface ? "popup" : surface == s->child_surface ? "child" :
           surface == s->sub_surface ? "sub" : "other";
}

static void keyboard_keymap(void *data, struct wl_keyboard *k, uint32_t format, int32_t fd,
                            uint32_t size)
{
    char *map = size > 0 ? mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0) : MAP_FAILED;
    (void)data; (void)k;
    printf("STANDIN keymap format=%u size=%u xkb=%d\n", format, size,
           map != MAP_FAILED && size > 12 && memcmp(map, "xkb_keymap {", 12) == 0 &&
               map[size - 1] == '\0');
    if (map != MAP_FAILED) munmap(map, size);
    close(fd);
}

static void keyboard_enter(void *data, struct wl_keyboard *k, uint32_t serial,
                           struct wl_surface *surface, struct wl_array *keys)
{
    (void)k; (void)serial; (void)keys;
    printf("STANDIN keyboard enter %s\n", surface_name(data, surface));
}

static void keyboard_leave(void *data, struct wl_keyboard *k, uint32_t serial,
                           struct wl_surface *surface)
{
    (void)k; (void)serial;
    printf("STANDIN keyboard leave %s\n", surface_name(data, surface));
}

static void keyboard_key(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t time,
                         uint32_t key, uint32_t state)
{
    struct standin *s = data;
    (void)k; (void)serial; (void)time;
    printf("STANDIN key %u %u\n", key, state);
    if (s->lock && key == 57 && state == WL_KEYBOARD_KEY_STATE_PRESSED) lock_next(s);
}

static void keyboard_modifiers(void *data, struct wl_keyboard *k, uint32_t serial, uint32_t dep,
                               uint32_t lat, uint32_t lock, uint32_t group)
{
    (void)data; (void)k; (void)serial;
    printf("STANDIN modifiers %u %u %u %u\n", dep, lat, lock, group);
}

static void keyboard_repeat(void *data, struct wl_keyboard *k, int32_t rate, int32_t delay)
{
    (void)data; (void)k;
    printf("STANDIN repeat %d %d\n", rate, delay);
}

static const struct wl_keyboard_listener keyboard_listener = {
    keyboard_keymap, keyboard_enter, keyboard_leave, keyboard_key, keyboard_modifiers,
    keyboard_repeat,
};

static void pointer_enter(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface, wl_fixed_t x, wl_fixed_t y)
{
    (void)p; (void)serial;
    printf("STANDIN pointer enter %s %.2f %.2f\n", surface_name(data, surface),
           wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void pointer_leave(void *data, struct wl_pointer *p, uint32_t serial,
                          struct wl_surface *surface)
{
    (void)p; (void)serial;
    printf("STANDIN pointer leave %s\n", surface_name(data, surface));
}

static void pointer_motion(void *data, struct wl_pointer *p, uint32_t time, wl_fixed_t x,
                           wl_fixed_t y)
{
    (void)data; (void)p; (void)time;
    printf("STANDIN pointer motion %.2f %.2f\n", wl_fixed_to_double(x), wl_fixed_to_double(y));
}

static void pointer_button(void *data, struct wl_pointer *p, uint32_t serial, uint32_t time,
                           uint32_t button, uint32_t state)
{
    (void)data; (void)p; (void)serial; (void)time;
    printf("STANDIN button %u %u\n", button, state);
}

static void pointer_axis(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis,
                         wl_fixed_t value)
{
    (void)data; (void)p; (void)time;
    printf("STANDIN axis %u %.2f\n", axis, wl_fixed_to_double(value));
}

static void pointer_frame(void *data, struct wl_pointer *p)
{
    (void)data; (void)p;
    printf("STANDIN pointer frame\n");
}

static void pointer_axis_source(void *data, struct wl_pointer *p, uint32_t source)
{
    (void)data; (void)p;
    printf("STANDIN axis source %u\n", source);
}

static void pointer_axis_stop(void *data, struct wl_pointer *p, uint32_t time, uint32_t axis)
{
    (void)data; (void)p; (void)time; (void)axis;
}

static void pointer_axis_discrete(void *data, struct wl_pointer *p, uint32_t axis, int32_t discrete)
{
    (void)data; (void)p;
    printf("STANDIN axis discrete %u %d\n", axis, discrete);
}

static void pointer_axis_value120(void *data, struct wl_pointer *p, uint32_t axis, int32_t v120)
{
    (void)data; (void)p;
    printf("STANDIN axis120 %u %d\n", axis, v120);
}

static const struct wl_pointer_listener pointer_listener = {
    pointer_enter, pointer_leave, pointer_motion, pointer_button, pointer_axis, pointer_frame,
    pointer_axis_source, pointer_axis_stop, pointer_axis_discrete, pointer_axis_value120,
};

static void relative_motion(void *data, struct zwp_relative_pointer_v1 *r, uint32_t hi, uint32_t lo,
                            wl_fixed_t dx, wl_fixed_t dy, wl_fixed_t udx, wl_fixed_t udy)
{
    (void)data; (void)r; (void)hi; (void)lo; (void)udx; (void)udy;
    printf("STANDIN relative %.2f %.2f\n", wl_fixed_to_double(dx), wl_fixed_to_double(dy));
}

static const struct zwp_relative_pointer_v1_listener relative_listener = { relative_motion };

static void seat_capabilities(void *data, struct wl_seat *seat, uint32_t caps)
{
    struct standin *s = data;
    printf("STANDIN seat capabilities %u\n", caps);
    if ((caps & WL_SEAT_CAPABILITY_KEYBOARD) && !s->keyboard) {
        s->keyboard = wl_seat_get_keyboard(seat);
        wl_keyboard_add_listener(s->keyboard, &keyboard_listener, s);
    }
    if ((caps & WL_SEAT_CAPABILITY_POINTER) && !s->pointer) {
        s->pointer = wl_seat_get_pointer(seat);
        wl_pointer_add_listener(s->pointer, &pointer_listener, s);
    }
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
    struct standin_output *out = data;
    (void)o; (void)flags;
    out->mode_w = w;
    out->mode_h = h;
    out->refresh = refresh;
}

static void output_done(void *data, struct wl_output *o)
{
    struct standin_output *out = data;
    (void)o;
    printf("STANDIN output mode %dx%d\n", out->mode_w, out->mode_h);
    printf("STANDIN output %s mode %dx%d@%d\n", out->name, out->mode_w, out->mode_h, out->refresh);
}

static void output_scale(void *data, struct wl_output *o, int32_t factor)
{
    (void)data; (void)o;
    printf("STANDIN output scale %d\n", factor);
}

static void output_name(void *data, struct wl_output *o, const char *name)
{
    struct standin_output *out = data;
    (void)o;
    snprintf(out->name, sizeof(out->name), "%s", name);
    printf("STANDIN output name %s\n", name);
}

static void output_description(void *data, struct wl_output *o, const char *description)
{
    (void)data; (void)o; (void)description;
}

static const struct wl_output_listener output_listener = {
    output_geometry, output_mode, output_done, output_scale, output_name, output_description,
};

static struct standin_output *find_output(struct standin *s, struct wl_output *o)
{
    int i;
    for (i = 0; i < s->output_count; ++i)
        if (s->outputs[i].output == o) return &s->outputs[i];
    return NULL;
}

static void surface_enter(void *data, struct wl_surface *surface, struct wl_output *o)
{
    struct standin *s = data;
    struct standin_output *out = find_output(s, o);
    printf("STANDIN %senter %s %dx%d@%d\n", surface == s->surface ? "" : "child ",
           out ? out->name : "?", out ? out->mode_w : 0, out ? out->mode_h : 0,
           out ? out->refresh : 0);
}

static void surface_leave(void *data, struct wl_surface *surface, struct wl_output *o)
{
    struct standin *s = data;
    struct standin_output *out = find_output(s, o);
    printf("STANDIN %sleave %s\n", surface == s->surface ? "" : "child ", out ? out->name : "?");
}

static const struct wl_surface_listener surface_listener = { surface_enter, surface_leave };

static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener;

static void global(void *data, struct wl_registry *registry, uint32_t name, const char *interface,
                   uint32_t version)
{
    struct standin *s = data;
    printf("STANDIN global %s %u\n", interface, version);
    if (strcmp(interface, wl_compositor_interface.name) == 0)
        s->compositor = wl_registry_bind(registry, name, &wl_compositor_interface, 4);
    else if (strcmp(interface, wl_subcompositor_interface.name) == 0)
        s->subcompositor = wl_registry_bind(registry, name, &wl_subcompositor_interface, 1);
    else if (strcmp(interface, wl_shm_interface.name) == 0)
        s->shm = wl_registry_bind(registry, name, &wl_shm_interface, 1);
    else if (strcmp(interface, xdg_wm_base_interface.name) == 0)
        s->wm_base = wl_registry_bind(registry, name, &xdg_wm_base_interface,
                                      version < 5 ? version : 5);
    else if (strcmp(interface, wl_seat_interface.name) == 0) {
        s->seat = wl_registry_bind(registry, name, &wl_seat_interface, version < 8 ? version : 8);
        wl_seat_add_listener(s->seat, &seat_listener, s);
    } else if (strcmp(interface, wl_output_interface.name) == 0 &&
               s->output_count < (int)(sizeof(s->outputs) / sizeof(s->outputs[0]))) {
        struct standin_output *out = &s->outputs[s->output_count++];
        out->output = wl_registry_bind(registry, name, &wl_output_interface,
                                       version < 4 ? version : 4);
        wl_output_add_listener(out->output, &output_listener, out);
    } else if (strcmp(interface, zwp_relative_pointer_manager_v1_interface.name) == 0) {
        s->relative_manager = wl_registry_bind(registry, name,
                                               &zwp_relative_pointer_manager_v1_interface, 1);
    } else if (strcmp(interface, zwp_pointer_constraints_v1_interface.name) == 0) {
        s->constraints = wl_registry_bind(registry, name, &zwp_pointer_constraints_v1_interface, 1);
    } else if (strcmp(interface, wp_presentation_interface.name) == 0) {
        s->presentation = wl_registry_bind(registry, name, &wp_presentation_interface, 1);
    } else if (strcmp(interface, zwp_linux_dmabuf_v1_interface.name) == 0 && version >= 4) {
        s->dmabuf = wl_registry_bind(registry, name, &zwp_linux_dmabuf_v1_interface, 4);
        s->feedback = zwp_linux_dmabuf_v1_get_default_feedback(s->dmabuf);
        zwp_linux_dmabuf_feedback_v1_add_listener(s->feedback, &feedback_listener, s);
    } else if (strcmp(interface, zxdg_decoration_manager_v1_interface.name) == 0)
        s->decoration_manager = wl_registry_bind(registry, name,
                                                 &zxdg_decoration_manager_v1_interface, 1);
}

/* The default feedback: the table's pairs, the tranche's, and whether
   XRGB8888 LINEAR is among them. */
static void feedback_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *f)
{
    struct standin *s = data;
    (void)f;
    printf("STANDIN dmabuf feedback pairs=%u tranche=%u xr24-linear=%d\n", s->feedback_pairs,
           s->feedback_tranche, s->feedback_xr24_linear);
}

static void feedback_format_table(void *data, struct zwp_linux_dmabuf_feedback_v1 *f, int32_t fd,
                                  uint32_t size)
{
    struct standin *s = data;
    unsigned char *map = mmap(NULL, size, PROT_READ, MAP_PRIVATE, fd, 0);
    uint32_t i;
    (void)f;
    s->feedback_pairs = size / 16;
    if (map != MAP_FAILED) {
        for (i = 0; i < size / 16; ++i) {
            uint32_t format;
            uint64_t modifier;
            memcpy(&format, map + 16 * i, 4);
            memcpy(&modifier, map + 16 * i + 8, 8);
            if (format == FOURCC_XR24 && modifier == 0) s->feedback_xr24_linear = 1;
        }
        munmap(map, size);
    }
    close(fd);
}

static void feedback_main_device(void *data, struct zwp_linux_dmabuf_feedback_v1 *f,
                                 struct wl_array *device)
{
    (void)data; (void)f; (void)device;
}

static void feedback_tranche_done(void *data, struct zwp_linux_dmabuf_feedback_v1 *f)
{
    (void)data; (void)f;
}

static void feedback_tranche_target_device(void *data, struct zwp_linux_dmabuf_feedback_v1 *f,
                                           struct wl_array *device)
{
    (void)data; (void)f; (void)device;
}

static void feedback_tranche_formats(void *data, struct zwp_linux_dmabuf_feedback_v1 *f,
                                     struct wl_array *indices)
{
    struct standin *s = data;
    (void)f;
    s->feedback_tranche += (unsigned)(indices->size / sizeof(uint16_t));
}

static void feedback_tranche_flags(void *data, struct zwp_linux_dmabuf_feedback_v1 *f, uint32_t flags)
{
    (void)data; (void)f; (void)flags;
}

static const struct zwp_linux_dmabuf_feedback_v1_listener feedback_listener = {
    feedback_done, feedback_format_table, feedback_main_device, feedback_tranche_done,
    feedback_tranche_target_device, feedback_tranche_formats, feedback_tranche_flags,
};

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
        else if (strcmp(argv[i], "--sub") == 0) s.sub_script = 1;
        else if (strcmp(argv[i], "--states") == 0) s.states = 1;
        else if (strcmp(argv[i], "--dmabuf") == 0) s.use_dmabuf = 1;
        else if (strcmp(argv[i], "--lock") == 0) s.lock = 1;
        else if (strcmp(argv[i], "--animate") == 0 && i + 1 < argc) s.animate = atoi(argv[++i]);
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
    if (s.relative_manager && s.pointer) {
        s.relative = zwp_relative_pointer_manager_v1_get_relative_pointer(s.relative_manager, s.pointer);
        zwp_relative_pointer_v1_add_listener(s.relative, &relative_listener, &s);
    }
    if (s.lock && !s.constraints) {
        fprintf(stderr, "STANDIN --lock: no zwp_pointer_constraints_v1\n");
        return 1;
    }
    if (s.use_dmabuf && !s.dmabuf) {
        fprintf(stderr, "STANDIN --dmabuf: no zwp_linux_dmabuf_v1 version 4\n");
        return 1;
    }
    xdg_wm_base_add_listener(s.wm_base, &wm_listener, &s);
    s.surface = wl_compositor_create_surface(s.compositor);
    wl_surface_add_listener(s.surface, &surface_listener, &s);
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
    /* xdg-shell: the compositor replies to the initial commit with a
       configure, and real clients (vkcube) present after one roundtrip. */
    wl_display_roundtrip(s.display);
    if (s.surface_configures == 0) {
        fprintf(stderr, "STANDIN no configure answered the initial commit within one roundtrip\n");
        return 1;
    }
    printf("STANDIN initial commit answered in one roundtrip\n");
    fflush(stdout);
    while (!s.closed && wl_display_dispatch(s.display) >= 0)
        fflush(stdout);
    if (!s.closed) {
        fprintf(stderr, "STANDIN the connection ended before close\n");
        return 1;
    }
    if (s.popup) popup_done(&s, s.popup);
    if (s.child) {
        xdg_toplevel_destroy(s.child);
        xdg_surface_destroy(s.child_xdg);
        wl_surface_destroy(s.child_surface);
    }
    if (s.decoration) zxdg_toplevel_decoration_v1_destroy(s.decoration);
    xdg_toplevel_destroy(s.toplevel);
    xdg_surface_destroy(s.xdg_surface);
    wl_surface_destroy(s.surface);
    /* After the window: it goes with its constraint still there. */
    if (s.locked) zwp_locked_pointer_v1_destroy(s.locked);
    if (s.confined) zwp_confined_pointer_v1_destroy(s.confined);
    if (s.constraints) zwp_pointer_constraints_v1_destroy(s.constraints);
    if (s.keyboard) wl_keyboard_release(s.keyboard);
    if (s.pointer) wl_pointer_release(s.pointer);
    if (s.seat) wl_seat_release(s.seat);
    for (i = 0; i < s.output_count; ++i) wl_output_release(s.outputs[i].output);
    if (s.decoration_manager) zxdg_decoration_manager_v1_destroy(s.decoration_manager);
    if (s.feedback) zwp_linux_dmabuf_feedback_v1_destroy(s.feedback);
    if (s.dmabuf) zwp_linux_dmabuf_v1_destroy(s.dmabuf);
    if (s.presentation) wp_presentation_destroy(s.presentation);
    if (s.relative) zwp_relative_pointer_v1_destroy(s.relative);
    if (s.relative_manager) zwp_relative_pointer_manager_v1_destroy(s.relative_manager);
    /* A callback the compositor never answered (the last frame's, at
       close) is still the client's to free. */
    for (i = 0; i < (int)(sizeof(s.callbacks) / sizeof(s.callbacks[0])); ++i)
        if (s.callbacks[i]) wl_callback_destroy(s.callbacks[i]);
    xdg_wm_base_destroy(s.wm_base);
    wl_shm_destroy(s.shm);
    if (s.sub_surface) wl_surface_destroy(s.sub_surface);
    if (s.subcompositor) wl_subcompositor_destroy(s.subcompositor);
    wl_compositor_destroy(s.compositor);
    wl_registry_destroy(registry);
    wl_display_roundtrip(s.display);
    if (s.presentation)
        printf("STANDIN presentation presented=%u discarded=%u refresh=%u\n", s.presented,
               s.discarded, s.refresh);
    printf("STANDIN done draws=%u releases=%u frames=%u\n", s.draws, s.releases, s.frames);
    wl_display_disconnect(s.display);
    return 0;
}
