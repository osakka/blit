/* blit-compositor: Blit's Wayland compositor for programs (ADR 0228,
   phase 1, milestones A, B and C), and the X window manager of the
   Xwayland it runs for X programs (phase 2, milestones A and C: the X
   section).

     blit-compositor [--display name] [--log path]

   One per desktop session, started by the desktop with the
   BLIT_FRAME_SOCKET/BLIT_FRAME_TOKEN of the frame channel it serves. It
   connects there as a version 5 producer (blit_compositor_wire.h), listens
   on $XDG_RUNTIME_DIR/<name> (--display's, else blit-<n>, the first free n)
   and says that name with `WINDOW display <name> <x-display>`; the desktop
   puts them in its programs' environment as WAYLAND_DISPLAY and DISPLAY
   (the X display `:<n>` of its Xwayland, `-` when X is off).

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
   libwayland-server.so.0, libxkbcommon.so.0 and libxcb.so.1 with its
   composite, xfixes and res extensions. */
#define _GNU_SOURCE
#include <errno.h>
#include <signal.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <unistd.h>
#include <poll.h>
#include <sys/ioctl.h>
#include <sys/stat.h>
#include <sys/wait.h>
#include <linux/dma-buf.h>
#include <xkbcommon/xkbcommon.h>
#include <xcb/xcb.h>
#include <xcb/composite.h>
#include <xcb/xfixes.h>
#include <xcb/res.h>

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
#include "appmenu-server-protocol.h"
#include "xwayland-shell-v1-server-protocol.h"
#include "blit_compositor_wire.h"
#include "blit_pointer_names.h"

#ifndef BLIT_BUILD_VERSION
#define BLIT_BUILD_VERSION "dev"
#endif

/* The version offered: the one ADR 0228 names, bounded by the generated
   interface (the build host's XML). */
#define BC_VERSION(want, iface) ((want) < (iface).version ? (want) : (iface).version)
#define BC_SOCKETS 32

enum { BC_ROLE_NONE = 0, BC_ROLE_XDG, BC_ROLE_SUBSURFACE, BC_ROLE_XWAYLAND };
enum { BC_LINK_UP = 0, BC_LINK_DOWN };

struct bc_server;
struct bc_xdg;
struct bc_constraint;
struct bc_xwin;

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
    /* The global menu (org_kde_kwin_appmenu): where the program serves its
       menus over D-Bus, "" none. */
    struct wl_resource *appmenu;
    char appmenu_service[256], appmenu_path[256];
    struct bc_constraint *constraint; /* its pointer lock or confinement, one at most */
    /* An Xwayland surface (the X section): xwayland_surface_v1's serial,
       pending and then committed (0 none), and its newest buffer, held
       until the surface is matched to its X window; `xwl_matched` once it
       is (shown, or of a window Blit does not show). */
    struct wl_resource *xwl;
    struct wl_list xwl_link;          /* bc_x.surfaces */
    uint64_t xwl_serial, xwl_pending;
    int xwl_matched;
    struct wl_resource *xwl_held;
    struct wl_listener xwl_held_destroy;
    uint64_t commit_us;               /* when its newest commit came */
};

struct bc_positioner {
    int32_t width, height;
    int32_t ax, ay, aw, ah;
    uint32_t anchor, gravity;
    int32_t dx, dy;
};

/* One picture: a window of one of two kinds -- an xdg toplevel, or an X
   toplevel (`xwin`, the X section) -- or an xdg popup, or a subsurface.
   A toplevel of either kind is announced, framed, routed and announced
   again by the same code (window_is_toplevel); what only a kind can do
   goes through window_configure, window_close and the keyboard's focus. */
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
    /* An X window (the X section): its X window; NULL for every other kind.
       `xpopup`: an override-redirect one, shown as a popup of `parent`
       at px, py. */
    struct bc_xwin *xwin;
    int xpopup;
};

/* Xwayland and its window manager (the X section). The atoms the window
   manager names, in bc_atom_names' order. */
enum {
    BC_ATOM_WL_SURFACE_SERIAL, BC_ATOM_WL_SURFACE_ID, BC_ATOM_WM_PROTOCOLS,
    BC_ATOM_WM_DELETE_WINDOW, BC_ATOM_WM_TAKE_FOCUS, BC_ATOM_WM_STATE, BC_ATOM_WM_CHANGE_STATE,
    BC_ATOM_UTF8_STRING, BC_ATOM_NET_WM_NAME, BC_ATOM_NET_SUPPORTING_WM_CHECK,
    BC_ATOM_NET_SUPPORTED, BC_ATOM_NET_ACTIVE_WINDOW, BC_ATOM_NET_WM_STATE,
    BC_ATOM_NET_WM_STATE_FULLSCREEN, BC_ATOM_NET_WM_STATE_MAXIMIZED_VERT,
    BC_ATOM_NET_WM_STATE_MAXIMIZED_HORZ, BC_ATOM_NET_WM_MOVERESIZE, BC_ATOM_NET_WM_WINDOW_TYPE,
    BC_ATOM_WM_NORMAL_HINTS, BC_ATOM_MOTIF_WM_HINTS, BC_ATOM_WM_S0, BC_ATOM_NET_WM_CM_S0,
    BC_ATOM_NET_WM_STATE_FOCUSED,
    BC_ATOMS
};

struct bc_x {
    int on;                           /* X programs are served: the display is ours */
    int stopping;                     /* the compositor is ending: no restart */
    char path[256];                   /* the Xwayland program */
    char display[16];                 /* ":<number>", "-" when X is off */
    int number;                       /* the display's number, -1 none */
    int listen_fd[2];                 /* @/tmp/.X11-unix/X<n> and /tmp/.X11-unix/X<n> */
    pid_t pid;                        /* Xwayland, 0 none */
    struct wl_client *client;         /* its Wayland connection, the one it was given */
    struct wl_listener client_gone;
    int wm_fd;                        /* our end of -wm, until the window manager takes it */
    int ready_fd;                     /* -displayfd: a line when it is ready */
    struct wl_event_source *ready_source, *restart, *child;
    uint64_t started_us;
    int quick_deaths;                 /* deaths in a row within BC_X_QUICK_US of a start */
    struct wl_global *shell;          /* xwayland_shell_v1, for that client only */
    struct wl_list surfaces;          /* bc_surface with an xwayland_surface_v1 role */
    /* the window manager, on the -wm connection */
    xcb_connection_t *conn;
    struct wl_event_source *wm_source;
    xcb_screen_t *screen;
    xcb_window_t wm_window;
    xcb_atom_t atoms[BC_ATOMS];
    int xres;                         /* X-Resource 1.2: a window's client pid */
    struct wl_list windows;           /* bc_xwin: the root's children */
    xcb_window_t focus;               /* the window given X focus, XCB_WINDOW_NONE none */
    uint32_t stack_seq;               /* the newest stacking count (bc_xwin.stack) */
    int32_t root_w, root_h;           /* the root's size: X's pointer is confined to it */
    /* X's cursor (XFixes): the name of the cursor X shows, as `WINDOW
       cursor` says it ("" none known), whether Xwayland's last
       wl_pointer.set_cursor showed one, and the names of cursor atoms. */
    uint8_t xfixes_event;             /* XFixes' first event, 0: no cursor names */
    char cursor[24];
    int cursor_shown;
    struct { xcb_atom_t atom; char name[24]; } cursor_names[16];
    unsigned cursor_next;
    unsigned logged;                  /* BC_X_LOGGED_*: a later milestone's request, noted once */
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
    struct bc_x x;                    /* Xwayland and its window manager (the X section) */
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
static int x_hold(struct bc_surface *surface, struct wl_resource *buffer, int attached);
static void x_surface_committed(struct bc_surface *surface);
static void x_surface_gone(struct bc_surface *surface);
static void x_frame_measure(struct bc_xdg *x, struct bc_surface *surface);
static void x_configure(struct bc_xdg *x);
static void x_close(struct bc_xdg *x);
static void x_popup_close(struct bc_xdg *x);
static void x_focus_follows(struct bc_server *s, struct bc_surface *surface);
static void x_pointer_over(struct bc_server *s, struct bc_surface *surface);
static const char *x_cursor_of(struct bc_server *s, struct wl_client *client, int shown);
static void x_pointer_entered(struct bc_server *s, struct bc_surface *surface);

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

/* A toplevel of either kind: an xdg toplevel or an X one. */
static int window_is_toplevel(const struct bc_xdg *x)
{
    return x && (x->toplevel != NULL || (x->xwin != NULL && !x->xpopup));
}

/* A popup of either kind: an xdg popup, or an X override-redirect window
   (a menu, a dropdown, a tooltip: the X section). */
static int window_is_popup(const struct bc_xdg *x)
{
    return x && (x->popup != NULL || x->xpopup);
}

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

/* `WINDOW limits` when they change (xdg's at the commit that applies them,
   X's WM_NORMAL_HINTS when the property does): pixels, 0 for none. */
static void window_set_limits(struct bc_xdg *x, int32_t min_w, int32_t min_h, int32_t max_w, int32_t max_h)
{
    if (x->min_w == min_w && x->min_h == min_h && x->max_w == max_w && x->max_h == max_h) return;
    x->min_w = min_w; x->min_h = min_h;
    x->max_w = max_w; x->max_h = max_h;
    if (x->id) send_limits(x);
}

/* `WINDOW appmenu`: where the window's program serves its menus (the
   global menu, ADR 0229), `- -` for none. */
static void send_appmenu(struct bc_xdg *x)
{
    struct bc_surface *sf = x->surface;
    if (sf == NULL || !x->id) return;
    if (sf->appmenu_service[0])
        bc_wire_line(&x->server->wire, "WINDOW appmenu %u %s %s", x->id, sf->appmenu_service,
                     sf->appmenu_path);
    else
        bc_wire_line(&x->server->wire, "WINDOW appmenu %u - -", x->id);
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

/* A toplevel's title and app id, from its program in whatever kind of
   window: cleaned (bc_text_clean), kept, and said when changed. */
static void window_set_title(struct bc_xdg *x, const char *text)
{
    char clean[BC_TEXT_BYTES + 1];
    bc_text_clean(clean, text);
    if (strcmp(clean, x->title) == 0) return;
    memcpy(x->title, clean, sizeof(clean));
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW title %u %s", x->id, x->title);
}

static void window_set_appid(struct bc_xdg *x, const char *text)
{
    char clean[BC_TEXT_BYTES + 1];
    bc_text_clean(clean, text);
    if (strcmp(clean, x->appid) == 0) return;
    memcpy(x->appid, clean, sizeof(clean));
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW appid %u %s", x->id, x->appid);
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
    if (x->surface && x->surface->appmenu_service[0]) send_appmenu(x);
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
    logf_("popup %u: parent %u at %d,%d%s", x->id, p->id, p->gx + x->px, p->gy + x->py,
          x->xpopup ? " (an X override-redirect window)" : "");
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

/* The desktop's `WINDOW size` to the window's program, by its kind: an xdg
   configure, or the X window resized and focused (the X section). */
static void window_configure(struct bc_xdg *x)
{
    if (x->xwin) x_configure(x);
    else configure_if_ready(x);
}

/* The desktop's `WINDOW close`, by the window's kind. */
static void window_close(struct bc_xdg *x)
{
    if (x->xwin) x_close(x);
    else if (x->toplevel) xdg_toplevel_send_close(x->toplevel);
}

/* The desktop's `WINDOW close` for a popup: the person went elsewhere.
   xdg_popup's popup_done; an X menu has no such request (the X section). */
static void popup_close(struct bc_xdg *x)
{
    if (x->xwin) {
        x_popup_close(x);
    } else if (x->popup) {
        logf_("popup %u: done", x->id);
        xdg_popup_send_popup_done(x->popup);
    }
}

/* A popup its program moved after it was shown (xdg_popup.reposition, or
   an X menu moved by its program): its new offset, and `WINDOW popup`
   again with it, which the desktop takes as a move of the popup it has
   (docs/api.md). The move shows with the popup's next frame. Either kind. */
static void popup_moved(struct bc_xdg *x, int32_t px, int32_t py)
{
    struct bc_xdg *p = x->parent;
    x->px = px;
    x->py = py;
    if (x->id == 0 || p == NULL || p->id == 0) return;
    bc_wire_line(&x->server->wire, "WINDOW popup %u %u %d %d", x->id, p->id, p->gx + px, p->gy + py);
    logf_("popup %u: moved by its program to %d,%d", x->id, p->gx + px, p->gy + py);
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
            return window_is_toplevel(p->xdg) ? p->xdg : NULL;
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
    while (window_is_popup(x) && x->parent) x = x->parent;
    return window_is_toplevel(x) ? x : NULL;
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

/* A committed buffer as a frame of a window or popup of either kind: sent
   (send_buffer's result), and the picture mapped at the buffer's size. */
static int picture_frame(struct bc_xdg *x, struct bc_surface *surface, struct wl_resource *buffer)
{
    struct wl_shm_buffer *shm = wl_shm_buffer_get(buffer);
    struct bc_dmabuf *dmabuf = dmabuf_from_buffer(buffer);
    int sent = send_buffer(x, surface, buffer);
    x->mapped = 1;
    x->hidden = 0;
    x->buffer_w = shm ? wl_shm_buffer_get_width(shm) : dmabuf ? dmabuf->width : 0;
    x->buffer_h = shm ? wl_shm_buffer_get_height(shm) : dmabuf ? dmabuf->height : 0;
    return sent;
}
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
        if (x->toplevel) window_set_limits(x, x->pmin_w, x->pmin_h, x->pmax_w, x->pmax_h);
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
        if (attached && buffer) sent = picture_frame(x, surface, buffer);
    } else if (x && x->xwin) {
        /* An X toplevel: each buffer a frame. Xwayland unmaps a window by
           X's UnmapNotify (the X section), never by a NULL buffer. */
        if (attached && buffer) {
            sent = picture_frame(x, surface, buffer);
            if (sent != BC_SENT_NONE) x_frame_measure(x, surface);
        }
    } else if (sub) {
        sent = sub_apply_buffer(sub, buffer, attached);
    } else if (surface->role == BC_ROLE_XWAYLAND) {
        /* Not yet matched to its X window: held for its first frame. */
        sent = x_hold(surface, buffer, attached);
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
    surface->commit_us = now_us();
    set_pending_buffer(surface, NULL);
    surface->pending_attach = 0;
    /* An Xwayland surface's serial is applied with the commit, and may
       match it to its X window now (the X section). */
    if (surface->xwl_pending) x_surface_committed(surface);
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
    /* An X toplevel's picture goes with its surface (the X section). */
    x_surface_gone(surface);
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
    if (surface->appmenu) wl_resource_set_user_data(surface->appmenu, NULL);
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
    wl_list_init(&surface->xwl_link);
    wl_list_init(&surface->xwl_held_destroy.link);
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

/* ---- the global menu (ADR 0229) ----

   A program that serves its menus over D-Bus (com.canonical.dbusmenu: Qt
   and KDE programs, GTK 3 with the appmenu module) says where with
   org_kde_kwin_appmenu: a service name and an object path for one of its
   surfaces. The compositor tells the desktop `WINDOW appmenu <id>
   <service> <path>` for that surface's top-level, when it is set, when the
   window is announced (again), and `- -` when the program releases it; the
   desktop reads the menus from the session bus and shows the focused
   window's in its bar. Names that are not D-Bus syntax are refused (they go
   on the wire as words). */

static int dbus_word(const char *s, int path)
{
    size_t n = strlen(s), i;
    if (n == 0 || n > 255 || (path && s[0] != '/') || strcmp(s, "-") == 0) return 0;
    for (i = 0; i < n; ++i) {
        unsigned char c = (unsigned char)s[i];
        if (!((c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z') || (c >= '0' && c <= '9') || c == '_' ||
              (path ? c == '/' : (c == '.' || c == ':' || c == '-'))))
            return 0;
    }
    return 1;
}

static void appmenu_changed(struct bc_surface *sf)
{
    struct bc_xdg *x = sf->xdg && sf->xdg->toplevel ? sf->xdg : NULL;
    if (x) send_appmenu(x);
}

static void appmenu_set_address(struct wl_client *client, struct wl_resource *resource,
                                const char *service, const char *path)
{
    struct bc_surface *sf = wl_resource_get_user_data(resource);
    (void)client;
    if (sf == NULL) return;
    if (!dbus_word(service, 0) || !dbus_word(path, 1)) {
        logf_("appmenu: an address that is not D-Bus syntax is ignored");
        return;
    }
    if (strcmp(sf->appmenu_service, service) == 0 && strcmp(sf->appmenu_path, path) == 0) return;
    snprintf(sf->appmenu_service, sizeof(sf->appmenu_service), "%s", service);
    snprintf(sf->appmenu_path, sizeof(sf->appmenu_path), "%s", path);
    appmenu_changed(sf);
}

static void appmenu_destroyed(struct wl_resource *resource)
{
    struct bc_surface *sf = wl_resource_get_user_data(resource);
    if (sf == NULL) return;
    sf->appmenu = NULL;
    if (sf->appmenu_service[0]) {
        sf->appmenu_service[0] = sf->appmenu_path[0] = '\0';
        appmenu_changed(sf);
    }
}

static const struct org_kde_kwin_appmenu_interface appmenu_impl = {
    .set_address = appmenu_set_address,
    .release = bc_destroy_request,
};

static void appmenu_create(struct wl_client *client, struct wl_resource *resource, uint32_t id,
                           struct wl_resource *surface_resource)
{
    struct bc_surface *sf = wl_resource_get_user_data(surface_resource);
    struct wl_resource *r = wl_resource_create(client, &org_kde_kwin_appmenu_interface,
                                               wl_resource_get_version(resource), id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    /* One per surface: a second replaces the first's address. */
    if (sf->appmenu) wl_resource_set_user_data(sf->appmenu, NULL);
    sf->appmenu = r;
    wl_resource_set_implementation(r, &appmenu_impl, sf, appmenu_destroyed);
}

static const struct org_kde_kwin_appmenu_manager_interface appmenu_manager_impl = {
    .create = appmenu_create,
};

static void bind_appmenu_manager(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct wl_resource *r = wl_resource_create(client, &org_kde_kwin_appmenu_manager_interface,
                                               (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &appmenu_manager_impl, data, NULL);
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
                       : window_is_toplevel(parent->xdg) ? parent->xdg : NULL;
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
        if (window_is_popup(other) && other->parent == x && other->id) window_set_output(other, o);
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
    if (surface && client != s->x.client) {
        struct bc_surface *cs = wl_resource_get_user_data(surface);
        if (!cs->cursor_logged) {
            cs->cursor_logged = 1;
            logf_("cursor: a program's own cursor image is shown as the display's pointer "
                  "(it names no shape: wp_cursor_shape_v1 is the way)");
        }
    }
    /* Xwayland's image is X's cursor, which X names (the X section). */
    cursor_set(s, client, x_cursor_of(s, client, surface != NULL));
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

/* A toplevel's transient parent, of either kind (xdg_toplevel.set_parent,
   X's WM_TRANSIENT_FOR): `WINDOW parent` when it changes after `WINDOW new`. */
static void window_set_parent(struct bc_xdg *x, struct bc_xdg *p)
{
    int before = xdg_parent_id(x);
    if (p == x) p = NULL;
    x->parent = p;
    if (x->id && xdg_parent_id(x) != before)
        bc_wire_line(&x->server->wire, "WINDOW parent %u %d", x->id, xdg_parent_id(x));
}

static void toplevel_set_parent(struct wl_client *client, struct wl_resource *resource,
                                struct wl_resource *parent)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) window_set_parent(x, parent ? toplevel_xdg(parent) : NULL);
}

static void toplevel_set_title(struct wl_client *client, struct wl_resource *resource,
                               const char *title)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) window_set_title(x, title);
}

static void toplevel_set_app_id(struct wl_client *client, struct wl_resource *resource,
                                const char *appid)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) window_set_appid(x, appid);
}

/* A program's own frame asks the desktop to run a drag or show the window
   menu (ADR 0229): only while a press the compositor delivered to that
   window is still down, so no program starts one by itself. `press_ok` is
   what the request says of that press: xdg names its serial, X's
   _NET_WM_MOVERESIZE its button (the X section). One rule for both. */
static int window_grab_ok(struct bc_xdg *x, int press_ok)
{
    struct bc_server *s;
    if (x == NULL || x->id == 0) return 0;
    s = x->server;
    if (s->link != BC_LINK_UP || !s->wire.window || s->buttons_down == 0 || !press_ok ||
        surface_window(s->ptr_focus) != x) {
        logf_("window %u: a move, resize or menu without a press on it is ignored", x->id);
        return 0;
    }
    return 1;
}

static struct bc_xdg *toplevel_grab_window(struct wl_resource *resource, uint32_t serial)
{
    struct bc_xdg *x = wl_resource_get_user_data(resource);
    if (x == NULL) return NULL;
    return window_grab_ok(x, serial == x->server->button_serial) ? x : NULL;
}

/* `WINDOW move` and `WINDOW resize <edges>`, for a window window_grab_ok
   let through. */
static void window_move(struct bc_xdg *x)
{
    bc_wire_line(&x->server->wire, "WINDOW move %u", x->id);
}

static void window_resize(struct bc_xdg *x, const char *edges)
{
    bc_wire_line(&x->server->wire, "WINDOW resize %u %s", x->id, edges);
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
    if (x) window_move(x);
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
    if ((x = toplevel_grab_window(resource, serial)) != NULL) window_resize(x, names[edges]);
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

/* Maximize, full screen and minimize are the program's requests, of
   either kind (xdg_toplevel's; X's _NET_WM_STATE and WM_CHANGE_STATE): the
   desktop decides, and answers with WINDOW size (xdg-shell lets a
   compositor ignore them). Until the desktop answers, the current
   configure stands. */
static void toplevel_maximize(struct bc_xdg *x, int on)
{
    if (x->maximize == on) return;
    x->maximize = on;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW maximize %u %s", x->id, on ? "on" : "off");
}

static void window_set_fullscreen(struct bc_xdg *x, int on)
{
    x->fullscreen = on;
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW fullscreen %u %s", x->id, on ? "on" : "off");
}

static void window_minimize(struct bc_xdg *x)
{
    if (x->id) bc_wire_line(&x->server->wire, "WINDOW minimize %u", x->id);
    else x->minimize = 1;
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
    if (x) window_minimize(x);
}

static void toplevel_set_fullscreen(struct wl_client *client, struct wl_resource *resource,
                                    struct wl_resource *output)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client; (void)output;
    if (x) window_set_fullscreen(x, 1);
}

static void toplevel_unset_fullscreen(struct wl_client *client, struct wl_resource *resource)
{
    struct bc_xdg *x = toplevel_xdg(resource);
    (void)client;
    if (x) window_set_fullscreen(x, 0);
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
    popup_moved(x, x->px, x->py);
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
    if (surface->role == BC_ROLE_SUBSURFACE || surface->role == BC_ROLE_XWAYLAND || surface->xdg) {
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
    /* An X window's keyboard is X's focus too (the X section). */
    x_focus_follows(s, surface);
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
        if (!window_is_popup(p) || p->parent != x || !p->mapped || !p->id || p->surface == NULL) continue;
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
    if (window_is_popup(p) && p->parent) {
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
    x_pointer_entered(s, surface);
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
    if (!window_is_toplevel(x) || x->surface == NULL) return;  /* gone meanwhile */
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
        /* X's stacking first, so X finds the window Blit's pointer is over. */
        x_pointer_over(s, target);
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

/* ---- X programs: Xwayland and its window manager (ADR 0228, phase 2) ----

   X11 programs run under Xwayland, upstream's X server, started by the
   compositor with it and an ordinary Wayland client of it; the compositor
   is its X window manager, on libxcb. The server is upstream's, the
   behaviour is ours, as libwayland's is.

   Xwayland. On unless `--x11 off` (the desktop's X11 Programs setting),
   `Xwayland` on PATH (BLIT_COMPOSITOR_XWAYLAND names another program, for
   gates only), 23.1 or later (its `-version`;
   older has no xwayland_shell_v1 and is refused with the reason). Absent,
   refused, or with no free display, X is off, said once, and `WINDOW
   display` says `-` for it. Else the compositor takes the first free
   display n from 0 to 32 as X does -- /tmp/.X<n>-lock with its pid (a
   stale one, of a dead pid, taken over), the listening sockets
   @/tmp/.X11-unix/X<n> (abstract) and /tmp/.X11-unix/X<n> -- and runs

       Xwayland :<n> -rootless -listenfd 3 -listenfd 4 -wm 5 -displayfd 7

   with WAYLAND_SOCKET=6, one end of a socketpair the compositor made a
   wl_client of: that client, and only it, is shown xwayland_shell_v1 (a
   global filter) and may bind it. No -terminate: it lives as long as the
   compositor. Its output goes to the compositor's log. The display is
   `WINDOW display <wayland-name> :<n>` from the first line on, so the
   desktop exports DISPLAY with WAYLAND_DISPLAY; X clients that come before
   Xwayland is ready wait in the sockets' backlog. When Xwayland dies
   (SIGCHLD; its status logged) its windows go and it is started again a
   moment later, on the same display; dying within 5 s of its start three
   times in a row stops that: X is off until the compositor restarts, the
   sockets close so X programs fail at once, and a desktop that connects
   again is told `-`. At the compositor's end it is ended (SIGTERM, then
   SIGKILL after a second) and the sockets and lock removed.

   The window manager. When -displayfd's line comes, the compositor
   connects on its end of -wm: CompositeRedirectSubwindows(root, Manual)
   first (every top-level's pixels are the compositor's, by its surface),
   then SubstructureRedirect and SubstructureNotify on the root (refused
   when another window manager holds them), the atoms, a window of its
   own named by _NET_SUPPORTING_WM_CHECK, _NET_SUPPORTED, WM_S0 and
   _NET_WM_CM_S0 owned; a window an X program mapped before that is mapped
   again under it (x_adopt). Each child of the root is a bc_xwin from its
   CreateNotify; a MapRequest reads its properties (WM_NAME and
   _NET_WM_NAME, WM_CLASS, WM_PROTOCOLS, WM_HINTS), sets WM_STATE Normal
   and maps it; PropertyNotify keeps them current.

   The association: Xwayland gives each mapped top-level a wl_surface with
   the xwayland_surface_v1 role, sets a serial on it (applied at the
   surface's commit) and tells the window manager the same serial in a
   WL_SURFACE_SERIAL client message (l[0] its low 32 bits, l[1] its high)
   on the window. The two arrive on two sockets in either order; the
   second matches them. A buffer committed before the match is held (not
   released) and is the window's first frame. WL_SURFACE_ID, the older
   way, is never used: an Xwayland that sends it is too old, and its
   windows are not shown.

   Matched, an X toplevel is a picture as an xdg toplevel is (`xwin` set,
   window_is_toplevel): `WINDOW new`, `title` (_NET_WM_NAME, else WM_NAME),
   `appid` (WM_CLASS's class), `decor server` (the desktop draws its frame),
   its input socket; each commit of its surface one frame; `WINDOW gone`
   when it is unmapped or destroyed or its surface goes (a remap is a new
   surface, a new serial and a new id). The desktop's `WINDOW size`
   resizes it (ConfigureWindow, border 0, at its own X position; a
   synthetic ConfigureNotify when the size is unchanged, ICCCM 4.1.5) and,
   with `activated`, gives it X's focus; a ConfigureRequest is honoured
   before the desktop's first size and answered with that size after it.
   `WINDOW close` sends WM_DELETE_WINDOW when the window takes it, else
   kills its client. The keyboard entering an X window's surface gives
   that window X's focus too: SetInputFocus when its WM_HINTS take input,
   WM_TAKE_FOCUS when it asks for that (ICCCM 4.1.7), and
   _NET_ACTIVE_WINDOW; leaving it, the focus is PointerRoot. Keys and the
   pointer reach Xwayland as the wl_seat events any client gets on its
   surface.

   Milestone C: each X request onto the line its Wayland equivalent
   already produces, through the same setters. WM_TRANSIENT_FOR is the
   parent (`WINDOW new <id> <parent>`, `WINDOW parent`); WM_NORMAL_HINTS'
   minimum (else base) and maximum are `WINDOW limits`; _MOTIF_WM_HINTS
   with no decorations is `decor client`; _NET_WM_STATE full screen and
   maximized (the property before the map, requests after) are `WINDOW
   fullscreen` and `WINDOW maximize`, and the desktop's `WINDOW size`
   states are written back as the property; _NET_WM_MOVERESIZE is `WINDOW
   move` and `WINDOW resize` under xdg's press rule; WM_CHANGE_STATE
   Iconic is `WINDOW minimize`; XFixes' cursor names are `WINDOW cursor`.

   An override-redirect window (a menu, a dropdown, a tooltip) is a
   `WINDOW popup` of the toplevel it belongs to (x_popup_parent), at its
   offset from that toplevel in X space. Toplevels keep their X position
   (the program's, only kept inside the root, x_fit, and never moved after
   that): X space is a stable, virtual layout and Hibr places the
   pictures. Xwayland turns a pointer on any of its surfaces into a root
   position by that surface's window's X position, so a popup's pointer
   (Blit maps it into its toplevel's pixels, INPUT member; pointer_target
   gives it to the popup's surface) lands where X has the menu. Menus'
   grabs are X's own: a press anywhere in the program's windows reaches
   the grabbing menu, which closes on a press outside itself; when the
   desktop deactivates the window, X's focus leaves it and its menus close
   themselves. X's stacking follows Blit's pointer (x_raise) so X's hit
   test agrees with the picture the pointer is over, also when a new
   toplevel is shown under a pointer at rest.

   Not acted on, noted once in the log: _NET_ACTIVE_WINDOW requests and
   _NET_WM_WINDOW_TYPE; the selections are milestone D.

   Measured: per X window, the time from a commit of its surface to its
   frame sent, the minimum and median over its first 60 frames, logged. */

#define BC_X_DISPLAYS 33
#define BC_X_QUICK_US 5000000ull
#define BC_X_QUICK_LIMIT 3
#define BC_X_MEASURE 60
/* WM_STATE's states (ICCCM 4.1.3.1). */
#define BC_WM_STATE_WITHDRAWN 0u
#define BC_WM_STATE_NORMAL 1u
#define BC_WM_STATE_ICONIC 3u
/* _NET_WM_STATE's states the window manager keeps (bc_xwin.state). */
#define BC_XS_FULLSCREEN 1u
#define BC_XS_MAX_VERT 2u
#define BC_XS_MAX_HORZ 4u
#define BC_XS_FOCUSED 8u
#define BC_XS_MAXIMIZED (BC_XS_MAX_VERT | BC_XS_MAX_HORZ)
/* _NET_WM_STATE's actions and _NET_WM_MOVERESIZE's directions (EWMH). */
#define BC_NET_STATE_REMOVE 0u
#define BC_NET_STATE_ADD 1u
#define BC_NET_STATE_TOGGLE 2u
#define BC_MOVERESIZE_MOVE 8u
#define BC_MOVERESIZE_CANCEL 11u

enum { BC_X_LOGGED_ID = 1u << 0, BC_X_LOGGED_ACTIVE = 1u << 1, BC_X_LOGGED_TYPE = 1u << 2 };

static const char *const bc_atom_names[BC_ATOMS] = {
    "WL_SURFACE_SERIAL", "WL_SURFACE_ID", "WM_PROTOCOLS", "WM_DELETE_WINDOW", "WM_TAKE_FOCUS",
    "WM_STATE", "WM_CHANGE_STATE", "UTF8_STRING", "_NET_WM_NAME", "_NET_SUPPORTING_WM_CHECK",
    "_NET_SUPPORTED", "_NET_ACTIVE_WINDOW", "_NET_WM_STATE", "_NET_WM_STATE_FULLSCREEN",
    "_NET_WM_STATE_MAXIMIZED_VERT", "_NET_WM_STATE_MAXIMIZED_HORZ", "_NET_WM_MOVERESIZE",
    "_NET_WM_WINDOW_TYPE", "WM_NORMAL_HINTS", "_MOTIF_WM_HINTS", "WM_S0", "_NET_WM_CM_S0",
    "_NET_WM_STATE_FOCUSED",
};

/* One child of the root: an X top-level, from its CreateNotify to its
   DestroyNotify. */
struct bc_xwin {
    struct wl_list link;              /* bc_x.windows */
    xcb_window_t window;
    int override_redirect;
    int16_t x, y;
    uint16_t width, height;
    uint64_t serial;                  /* WL_SURFACE_SERIAL, 0 until Xwayland says */
    struct bc_surface *surface;       /* its surface, once matched */
    struct bc_xdg *pic;               /* its picture while shown */
    char title[BC_TEXT_BYTES + 1], appid[BC_TEXT_BYTES + 1];
    int title_net;                    /* the title is _NET_WM_NAME's: WM_NAME is not read */
    int deletes, takes_focus;         /* WM_PROTOCOLS: WM_DELETE_WINDOW, WM_TAKE_FOCUS */
    int input;                        /* WM_HINTS' input, 1 when it says nothing */
    int undecorated;                  /* _MOTIF_WM_HINTS: no decorations (it draws its own frame) */
    uint32_t pid;                     /* its client's process, 0 unknown (X-Resource) */
    int or_logged;                    /* an override-redirect window with no parent, noted once */
    xcb_window_t transient_for;       /* WM_TRANSIENT_FOR, XCB_WINDOW_NONE none */
    uint32_t stack;                   /* its place in X's stacking: higher is above */
    int32_t min_w, min_h, max_w, max_h;   /* WM_NORMAL_HINTS, pixels, 0 none */
    /* _NET_WM_STATE (BC_XS_*): `state` what the property says -- the
       program's before it is shown, the window manager's from the
       desktop's `WINDOW size` on, the base of a toggle -- and `state_set`
       once the window manager has written it. */
    unsigned state;
    int state_set;
    int remapping;                    /* unmapped by the window manager to be mapped again */
    uint32_t measure[BC_X_MEASURE];   /* commit to frame sent, us, its first frames */
    int measured, measure_said;
};

static int x_launch(struct bc_server *s);

/* A request of a later milestone, noted once for the whole display. */
static void x_note(struct bc_server *s, unsigned what, const char *text)
{
    if (s->x.logged & what) return;
    s->x.logged |= what;
    logf_("X: %s (a later milestone of ADR 0228 phase 2; noted once)", text);
}

static struct bc_xwin *x_window(struct bc_server *s, xcb_window_t window)
{
    struct bc_xwin *w;
    wl_list_for_each(w, &s->x.windows, link)
        if (w->window == window) return w;
    return NULL;
}

/* ---- the window manager's view of one window ---- */

/* The window's first frames: the time from each commit of its surface to
   its frame sent, said at the BC_X_MEASURE-th, or when it goes before. */
static void x_measure_say(struct bc_xwin *w, struct bc_xdg *x)
{
    uint32_t sorted[BC_X_MEASURE], t;
    int i, j, n = w->measured;
    if (n == 0 || w->measure_said) return;
    w->measure_said = 1;
    memcpy(sorted, w->measure, sizeof(uint32_t) * (size_t)n);
    for (i = 1; i < n; ++i)
        for (j = i; j > 0 && sorted[j - 1] > sorted[j]; --j) {
            t = sorted[j]; sorted[j] = sorted[j - 1]; sorted[j - 1] = t;
        }
    logf_("window %u: X commit to frame sent over its first %d frames: min %u us, median %u us",
          x->id, n, sorted[0], (sorted[(n - 1) / 2] + sorted[n / 2]) / 2);
}

static void x_frame_measure(struct bc_xdg *x, struct bc_surface *surface)
{
    struct bc_xwin *w = x->xwin;
    if (w == NULL || x->server->link != BC_LINK_UP || w->measured >= BC_X_MEASURE) return;
    w->measure[w->measured++] = (uint32_t)(now_us() - surface->commit_us);
    if (w->measured == BC_X_MEASURE) x_measure_say(w, x);
}

static void xwl_held_gone(struct wl_listener *listener, void *data)
{
    struct bc_surface *sf = wl_container_of(listener, sf, xwl_held_destroy);
    (void)data;
    wl_list_remove(&sf->xwl_held_destroy.link);
    wl_list_init(&sf->xwl_held_destroy.link);
    sf->xwl_held = NULL;
}

static void x_set_held(struct bc_surface *sf, struct wl_resource *buffer)
{
    if (sf->xwl_held) {
        wl_list_remove(&sf->xwl_held_destroy.link);
        wl_list_init(&sf->xwl_held_destroy.link);
    }
    sf->xwl_held = buffer;
    if (buffer) {
        sf->xwl_held_destroy.notify = xwl_held_gone;
        wl_resource_add_destroy_listener(buffer, &sf->xwl_held_destroy);
    }
}

/* A buffer of an Xwayland surface not yet matched to its window: the
   newest is held, unreleased, to be the window's first frame when the
   match comes; the one before it goes back to Xwayland. Its callbacks are
   answered at the refresh, as for a frame nothing took. A matched surface
   with no picture (a window Blit does not show) has its buffers back at
   once. */
static int x_hold(struct bc_surface *sf, struct wl_resource *buffer, int attached)
{
    struct wl_resource *old = sf->xwl_held;
    if (!attached) return BC_SENT_NONE;
    if (sf->xwl_matched || buffer == NULL) {
        x_set_held(sf, NULL);
        if (old && old != buffer) wl_buffer_send_release(old);
        return BC_SENT_NONE;
    }
    if (old && old != buffer) wl_buffer_send_release(old);
    x_set_held(sf, buffer);
    feedback_discard(&sf->feedback);
    frames_taken(sf);
    return BC_SENT_HELD;
}

/* Latin-1 (an X STRING) as UTF-8, NUL-terminated within `size`. */
static void x_latin1(char *out, size_t size, const unsigned char *in, size_t len)
{
    size_t n = 0, i;
    for (i = 0; i < len && in[i] && n + 3 <= size; ++i) {
        if (in[i] < 0x80) {
            out[n++] = (char)in[i];
        } else {
            out[n++] = (char)(0xc0 | in[i] >> 6);
            out[n++] = (char)(0x80 | (in[i] & 0x3f));
        }
    }
    out[n] = '\0';
}

static void x_set_title(struct bc_xwin *w, const char *text)
{
    bc_text_clean(w->title, text);
    if (w->pic) window_set_title(w->pic, w->title);
}

/* A property's value as text, by its type: UTF8_STRING as it is, STRING
   (Latin-1) converted; any other type (COMPOUND_TEXT) its ASCII. */
static void x_text(struct bc_server *s, xcb_get_property_reply_t *reply, char *out, size_t size)
{
    const unsigned char *v = xcb_get_property_value(reply);
    size_t len = (size_t)xcb_get_property_value_length(reply), i, n = 0;
    if (reply->format != 8) len = 0;
    if (reply->type == XCB_ATOM_STRING) {
        x_latin1(out, size, v, len);
        return;
    }
    for (i = 0; i < len && v[i] && n + 1 < size; ++i)
        if (reply->type == s->x.atoms[BC_ATOM_UTF8_STRING] || v[i] < 0x80) out[n++] = (char)v[i];
    out[n] = '\0';
}

/* An atom of _NET_WM_STATE as a BC_XS_* bit, 0 for one not kept. */
static unsigned x_state_bit(struct bc_server *s, xcb_atom_t atom)
{
    const xcb_atom_t *atoms = s->x.atoms;
    if (atom == XCB_ATOM_NONE) return 0;
    if (atom == atoms[BC_ATOM_NET_WM_STATE_FULLSCREEN]) return BC_XS_FULLSCREEN;
    if (atom == atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_VERT]) return BC_XS_MAX_VERT;
    if (atom == atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_HORZ]) return BC_XS_MAX_HORZ;
    if (atom == atoms[BC_ATOM_NET_WM_STATE_FOCUSED]) return BC_XS_FOCUSED;
    return 0;
}

/* A toplevel's transient parent from its WM_TRANSIENT_FOR: the shown X
   window it names (the toplevel of a menu it names); none for the root,
   or a window not shown. */
static struct bc_xdg *x_transient_parent(struct bc_server *s, struct bc_xwin *w)
{
    struct bc_xwin *o;
    if (w->transient_for == XCB_WINDOW_NONE || (o = x_window(s, w->transient_for)) == NULL || o->pic == NULL)
        return NULL;
    return o->pic->xpopup ? o->pic->parent : o->pic;
}

/* One of the window's properties, read (its reply) or changed. */
static void x_property(struct bc_server *s, struct bc_xwin *w, xcb_atom_t atom,
                       xcb_get_property_reply_t *reply)
{
    xcb_atom_t *atoms = s->x.atoms;
    char text[4 * BC_TEXT_BYTES + 1];
    if (atom == atoms[BC_ATOM_NET_WM_NAME]) {
        if (reply && reply->type == atoms[BC_ATOM_UTF8_STRING] &&
            xcb_get_property_value_length(reply) > 0) {
            x_text(s, reply, text, sizeof(text));
            w->title_net = 1;
            x_set_title(w, text);
        } else {
            /* Gone: WM_NAME's again, at its next change. */
            w->title_net = 0;
        }
    } else if (atom == XCB_ATOM_WM_NAME) {
        if (w->title_net || reply == NULL) return;
        x_text(s, reply, text, sizeof(text));
        x_set_title(w, text);
    } else if (atom == XCB_ATOM_WM_CLASS) {
        /* "instance\0class\0": the app id is the class. */
        const char *v;
        size_t len, first;
        if (reply == NULL || reply->format != 8) return;
        v = xcb_get_property_value(reply);
        len = (size_t)xcb_get_property_value_length(reply);
        first = strnlen(v, len);
        if (first + 1 < len) {
            v += first + 1;
            len -= first + 1;
        }
        len = strnlen(v, len);
        if (len >= sizeof(text)) len = sizeof(text) - 1;
        memcpy(text, v, len);
        text[len] = '\0';
        bc_text_clean(w->appid, text);
        if (w->pic) window_set_appid(w->pic, w->appid);
    } else if (atom == atoms[BC_ATOM_WM_PROTOCOLS]) {
        const xcb_atom_t *list;
        int i, n;
        w->deletes = w->takes_focus = 0;
        if (reply == NULL || reply->type != XCB_ATOM_ATOM || reply->format != 32) return;
        list = xcb_get_property_value(reply);
        n = xcb_get_property_value_length(reply) / 4;
        for (i = 0; i < n; ++i) {
            if (list[i] == atoms[BC_ATOM_WM_DELETE_WINDOW]) w->deletes = 1;
            if (list[i] == atoms[BC_ATOM_WM_TAKE_FOCUS]) w->takes_focus = 1;
        }
    } else if (atom == XCB_ATOM_WM_HINTS) {
        /* flags, then input when flags has InputHint (1). */
        const uint32_t *v;
        w->input = 1;
        if (reply == NULL || reply->format != 32 || xcb_get_property_value_length(reply) < 8) return;
        v = xcb_get_property_value(reply);
        if (v[0] & 1u) w->input = v[1] != 0;
    } else if (atom == atoms[BC_ATOM_MOTIF_WM_HINTS]) {
        /* flags, functions, decorations, ...: with MWM_HINTS_DECORATIONS
           (2) in flags and no decorations, the program draws its own frame
           or none (Steam), so the desktop draws none: `WINDOW decor <id>
           client`, as for a Wayland program that asks for client-side
           decoration. Any decoration, no such flag, or no property: the
           desktop's frame. */
        const uint32_t *v;
        uint32_t mode;
        w->undecorated = 0;
        if (reply && reply->format == 32 && xcb_get_property_value_length(reply) >= 12) {
            v = xcb_get_property_value(reply);
            w->undecorated = (v[0] & 2u) && v[2] == 0;
        }
        mode = w->undecorated ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                              : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        if (w->pic && w->pic->decor_mode != mode) {
            w->pic->decor_mode = mode;
            if (w->pic->id) send_decor(w->pic);
        }
    } else if (atom == atoms[BC_ATOM_WM_NORMAL_HINTS]) {
        /* WM_SIZE_HINTS (ICCCM 4.1.2.3): flags, x, y, width, height, the
           minimum (PMinSize, 16), the maximum (PMaxSize, 32), increments,
           aspects, the base size (PBaseSize, 256), gravity. Without a
           minimum the base size is the minimum (ICCCM; as wlroots).
           xdg_toplevel's set_min_size and set_max_size: `WINDOW limits`,
           0 for none -- so a maximum past X's 32767 is none. */
        const int32_t *v;
        int n = reply && reply->format == 32 ? xcb_get_property_value_length(reply) / 4 : 0;
        w->min_w = w->min_h = w->max_w = w->max_h = 0;
        if (n >= 9) {
            v = xcb_get_property_value(reply);
            if (v[0] & 16) {
                w->min_w = v[5];
                w->min_h = v[6];
            } else if ((v[0] & 256) && n >= 17) {
                w->min_w = v[15];
                w->min_h = v[16];
            }
            if (v[0] & 32) {
                w->max_w = v[7];
                w->max_h = v[8];
            }
        }
        if (w->min_w < 0 || w->min_w > 32767) w->min_w = 0;
        if (w->min_h < 0 || w->min_h > 32767) w->min_h = 0;
        if (w->max_w < 0 || w->max_w > 32767) w->max_w = 0;
        if (w->max_h < 0 || w->max_h > 32767) w->max_h = 0;
        if (w->pic && !w->pic->xpopup) window_set_limits(w->pic, w->min_w, w->min_h, w->max_w, w->max_h);
    } else if (atom == atoms[BC_ATOM_NET_WM_STATE]) {
        /* The program's own, before the window is shown (EWMH: a mapped
           window asks by a client message); from then on the property is
           the window manager's to write. */
        const xcb_atom_t *list;
        int i, n;
        if (w->pic || w->state_set) return;
        w->state = 0;
        if (reply == NULL || reply->type != XCB_ATOM_ATOM || reply->format != 32) return;
        list = xcb_get_property_value(reply);
        n = xcb_get_property_value_length(reply) / 4;
        for (i = 0; i < n; ++i) w->state |= x_state_bit(s, list[i]);
    } else if (atom == XCB_ATOM_WM_TRANSIENT_FOR) {
        w->transient_for = XCB_WINDOW_NONE;
        if (reply && reply->format == 32 && reply->type == XCB_ATOM_WINDOW &&
            xcb_get_property_value_length(reply) >= 4)
            w->transient_for = *(const xcb_window_t *)xcb_get_property_value(reply);
        if (w->pic && !w->pic->xpopup) window_set_parent(w->pic, x_transient_parent(s, w));
    } else if (atom == atoms[BC_ATOM_NET_WM_WINDOW_TYPE]) {
        x_note(s, BC_X_LOGGED_TYPE, "a window's _NET_WM_WINDOW_TYPE");
    }
}

/* Every property the window manager reads, read now (a MapRequest). */
static void x_read_properties(struct bc_server *s, struct bc_xwin *w)
{
    xcb_connection_t *c = s->x.conn;
    const xcb_atom_t props[] = { s->x.atoms[BC_ATOM_NET_WM_NAME], XCB_ATOM_WM_NAME, XCB_ATOM_WM_CLASS,
                                 s->x.atoms[BC_ATOM_WM_PROTOCOLS], XCB_ATOM_WM_HINTS,
                                 s->x.atoms[BC_ATOM_MOTIF_WM_HINTS], XCB_ATOM_WM_TRANSIENT_FOR,
                                 s->x.atoms[BC_ATOM_WM_NORMAL_HINTS], s->x.atoms[BC_ATOM_NET_WM_STATE] };
    xcb_get_property_cookie_t cookies[sizeof(props) / sizeof(props[0])];
    xcb_res_query_client_ids_cookie_t pid_cookie = { 0 };
    size_t i;
    for (i = 0; i < sizeof(props) / sizeof(props[0]); ++i)
        cookies[i] = xcb_get_property(c, 0, w->window, props[i], XCB_ATOM_ANY, 0, 2048);
    if (s->x.xres) {
        xcb_res_client_id_spec_t spec;
        spec.client = w->window;
        spec.mask = XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID;
        pid_cookie = xcb_res_query_client_ids(c, 1, &spec);
    }
    w->title_net = 0;
    for (i = 0; i < sizeof(props) / sizeof(props[0]); ++i) {
        xcb_get_property_reply_t *reply = xcb_get_property_reply(c, cookies[i], NULL);
        /* A property the window does not have is a reply of type None. */
        x_property(s, w, props[i], reply && reply->type != XCB_ATOM_NONE ? reply : NULL);
        free(reply);
    }
    if (s->x.xres) {
        xcb_res_query_client_ids_reply_t *reply = xcb_res_query_client_ids_reply(c, pid_cookie, NULL);
        if (reply) {
            xcb_res_client_id_value_iterator_t it = xcb_res_query_client_ids_ids_iterator(reply);
            for (; it.rem; xcb_res_client_id_value_next(&it))
                if ((it.data->spec.mask & XCB_RES_CLIENT_ID_MASK_LOCAL_CLIENT_PID) &&
                    xcb_res_client_id_value_value_length(it.data) == 1)
                    w->pid = *xcb_res_client_id_value_value(it.data);
            free(reply);
        }
    }
}

static void x_set_wm_state(struct bc_server *s, struct bc_xwin *w, uint32_t state)
{
    uint32_t value[2];
    value[0] = state;
    value[1] = XCB_WINDOW_NONE;
    xcb_change_property(s->x.conn, XCB_PROP_MODE_REPLACE, w->window, s->x.atoms[BC_ATOM_WM_STATE],
                        s->x.atoms[BC_ATOM_WM_STATE], 32, 2, value);
}

/* An ICCCM client message (WM_PROTOCOLS) to the window. */
static void x_send_protocol(struct bc_server *s, struct bc_xwin *w, xcb_atom_t protocol)
{
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = w->window;
    ev.type = s->x.atoms[BC_ATOM_WM_PROTOCOLS];
    ev.data.data32[0] = protocol;
    ev.data.data32[1] = XCB_CURRENT_TIME;
    xcb_send_event(s->x.conn, 0, w->window, XCB_EVENT_MASK_NO_EVENT, (const char *)&ev);
}

/* X's focus to window `w` (NULL: to none), and _NET_ACTIVE_WINDOW. */
static void x_activate(struct bc_server *s, struct bc_xwin *w)
{
    xcb_window_t window = w ? w->window : XCB_WINDOW_NONE;
    if (s->x.conn == NULL || s->x.focus == window) return;
    s->x.focus = window;
    xcb_change_property(s->x.conn, XCB_PROP_MODE_REPLACE, s->x.screen->root,
                        s->x.atoms[BC_ATOM_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1, &window);
    if (w == NULL) {
        /* PointerRoot, not None: None discards the keyboard, which a menu
           that grabbed it (an X popup) still needs (wlroots does the same). */
        xcb_set_input_focus(s->x.conn, XCB_INPUT_FOCUS_POINTER_ROOT, XCB_INPUT_FOCUS_POINTER_ROOT,
                            XCB_CURRENT_TIME);
    } else {
        /* ICCCM 4.1.7: input from WM_HINTS is the window's own to take
           with SetInputFocus; WM_TAKE_FOCUS, when it asks for it, lets it
           move the focus itself (a globally active window takes it only
           so). */
        if (w->input)
            xcb_set_input_focus(s->x.conn, XCB_INPUT_FOCUS_POINTER_ROOT, w->window, XCB_CURRENT_TIME);
        if (w->takes_focus) x_send_protocol(s, w, s->x.atoms[BC_ATOM_WM_TAKE_FOCUS]);
    }
    xcb_flush(s->x.conn);
    if (w && w->pic)
        logf_("window %u: X focus (0x%x%s%s)", w->pic->id, (unsigned)w->window,
              w->input ? ", SetInputFocus" : "", w->takes_focus ? ", WM_TAKE_FOCUS" : "");
}

static void x_focus_follows(struct bc_server *s, struct bc_surface *surface)
{
    struct bc_xdg *x = surface ? surface_window(surface) : NULL;
    if (s->x.conn == NULL) return;
    if (x && x->xwin) x_activate(s, x->xwin);
    else x_activate(s, NULL);
}

/* X's stacking follows Blit's pointer. Xwayland turns a pointer on a
   surface into a root position by its window's place, then X finds the
   window there by its own stacking; where two toplevels overlap in X
   space (a dialog over its window) the one Blit's pointer is over must be
   the topmost. So the toplevel under the pointer is raised above the
   topmost other shown toplevel -- never above override-redirect windows,
   which X programs expect to stay on top (wlroots' restack does the same)
   -- before the pointer's event goes to Xwayland, and when a new toplevel
   is shown (X puts it on top, under a pointer that may not move before a
   press). Stacking is otherwise the desktop's: it draws; X's order only
   decides X's hit test. */
static void x_raise(struct bc_server *s, struct bc_xwin *w)
{
    struct bc_xwin *o, *top = NULL;
    uint32_t values[2];
    wl_list_for_each(o, &s->x.windows, link)
        if (o->pic && !o->pic->xpopup && (top == NULL || o->stack > top->stack)) top = o;
    if (top == NULL || top == w) return;
    values[0] = top->window;
    values[1] = XCB_STACK_MODE_ABOVE;
    xcb_configure_window(s->x.conn, w->window, XCB_CONFIG_WINDOW_SIBLING | XCB_CONFIG_WINDOW_STACK_MODE, values);
    xcb_flush(s->x.conn);
    w->stack = ++s->x.stack_seq;
}

static void x_pointer_over(struct bc_server *s, struct bc_surface *surface)
{
    struct bc_xdg *x = surface ? surface_window(surface) : NULL;
    if (s->x.conn == NULL || x == NULL || x->xwin == NULL) return;
    x_raise(s, x->xwin);
}

/* X's cursor. X programs name their cursors -- libXcursor names every
   themed one it loads (XFixesSetCursorName: "left_ptr", "xterm",
   "hand2", or the CSS names GTK asks for) -- and XFixes says the name of
   the one X shows whenever it changes (CursorNotify). That name is the
   window's `WINDOW cursor` when Blit's own table draws a pointer for it
   (blit_pointer_name_shape: Blit's OSC 22 names take X's as aliases, so
   there is one table, Blit's); a cursor with no name, or one Blit does not
   know, is `default`, the display's own pointer. Xwayland draws the X
   cursor as an image (wl_pointer.set_cursor with a surface), which says
   only that a cursor is shown, not which; with no surface (X hid it) it is
   `none`. Sent for the window under the pointer, as for a Wayland one. */
static const char *x_cursor_name(struct bc_server *s, xcb_atom_t atom)
{
    xcb_get_atom_name_reply_t *reply;
    unsigned i;
    char name[24];
    int len;
    if (atom == XCB_ATOM_NONE) return "default";
    for (i = 0; i < sizeof(s->x.cursor_names) / sizeof(s->x.cursor_names[0]); ++i)
        if (s->x.cursor_names[i].atom == atom) return s->x.cursor_names[i].name;
    reply = xcb_get_atom_name_reply(s->x.conn, xcb_get_atom_name(s->x.conn, atom), NULL);
    len = reply ? xcb_get_atom_name_name_length(reply) : 0;
    snprintf(name, sizeof(name), "default");
    if (len > 0 && len < (int)sizeof(name)) {
        memcpy(name, xcb_get_atom_name_name(reply), (size_t)len);
        name[len] = '\0';
        if (blit_pointer_name_shape(name) == -1) snprintf(name, sizeof(name), "default");
    }
    free(reply);
    i = s->x.cursor_next++ % (sizeof(s->x.cursor_names) / sizeof(s->x.cursor_names[0]));
    s->x.cursor_names[i].atom = atom;
    memcpy(s->x.cursor_names[i].name, name, sizeof(name));
    return s->x.cursor_names[i].name;
}

static void x_cursor_notify(struct bc_server *s, xcb_xfixes_cursor_notify_event_t *ev)
{
    snprintf(s->x.cursor, sizeof(s->x.cursor), "%s", x_cursor_name(s, ev->name));
    if (s->x.cursor_shown && s->x.client) cursor_set(s, s->x.client, s->x.cursor);
}

/* The name for a wl_pointer.set_cursor: Xwayland's says X's cursor is
   shown (its name) or hidden; any other client's image is `default`. */
static const char *x_cursor_of(struct bc_server *s, struct wl_client *client, int shown)
{
    if (client == NULL || client != s->x.client) return shown ? "default" : "none";
    s->x.cursor_shown = shown;
    if (!shown) return "none";
    return s->x.cursor[0] ? s->x.cursor : "default";
}

/* The pointer entered an Xwayland surface: X's cursor is the window's
   until X says another (Xwayland may not set one again when the cursor
   is the same). */
static void x_pointer_entered(struct bc_server *s, struct bc_surface *surface)
{
    if (s->x.client && s->x.cursor_shown && s->x.cursor[0] &&
        wl_resource_get_client(surface->resource) == s->x.client)
        cursor_set(s, s->x.client, s->x.cursor);
}

/* A toplevel's place in X space kept inside the root. X confines its
   pointer to the root (the largest output: the compositor's outputs all
   sit at 0,0), so a press on the part of a window past the root's edge
   would reach X at the edge (found by the X gate: a press at 390,170 of a
   window at 1700,900 came as 219,170). So the window manager places a
   toplevel that would reach past the root back inside it, as far as its
   size allows -- at its map, at a ConfigureRequest and at the desktop's
   size; otherwise its position is the program's, never moved. A window
   larger than the root still reaches past it. */
static void x_fit(const struct bc_server *s, int32_t width, int32_t height, int32_t *x, int32_t *y)
{
    if (*x + width > s->x.root_w) *x = s->x.root_w - width;
    if (*y + height > s->x.root_h) *y = s->x.root_h - height;
    if (*x < 0) *x = 0;
    if (*y < 0) *y = 0;
}

/* The window resized to w x h at its own X position (kept inside the
   root), border 0; a synthetic ConfigureNotify when the geometry does not
   change (ICCCM 4.1.5: the client is told either way). */
static void x_resize(struct bc_server *s, struct bc_xwin *w, uint16_t width, uint16_t height)
{
    uint32_t values[5];
    int32_t nx = w->x, ny = w->y;
    int same;
    x_fit(s, width, height, &nx, &ny);
    same = width == w->width && height == w->height && nx == w->x && ny == w->y;
    w->x = (int16_t)nx;
    w->y = (int16_t)ny;
    values[0] = (uint32_t)(int32_t)w->x;
    values[1] = (uint32_t)(int32_t)w->y;
    values[2] = width;
    values[3] = height;
    values[4] = 0;
    xcb_configure_window(s->x.conn, w->window,
                         XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_WIDTH |
                             XCB_CONFIG_WINDOW_HEIGHT | XCB_CONFIG_WINDOW_BORDER_WIDTH,
                         values);
    w->width = width;
    w->height = height;
    if (same) {
        xcb_configure_notify_event_t ev;
        memset(&ev, 0, sizeof(ev));
        ev.response_type = XCB_CONFIGURE_NOTIFY;
        ev.event = w->window;
        ev.window = w->window;
        ev.x = w->x;
        ev.y = w->y;
        ev.width = width;
        ev.height = height;
        xcb_send_event(s->x.conn, 0, w->window, XCB_EVENT_MASK_STRUCTURE_NOTIFY, (const char *)&ev);
    }
}

/* The desktop's states for the window (`WINDOW size`, as xdg's configure
   carries them) as its _NET_WM_STATE: full screen, maximized (vertical
   and horizontal), focused (activated). Written when they change. */
static void x_state_write(struct bc_server *s, struct bc_xwin *w, const struct bc_xdg *x)
{
    xcb_atom_t list[4];
    uint32_t n = 0;
    unsigned bits = (x->size_fullscreen ? BC_XS_FULLSCREEN : 0) | (x->size_maximized ? BC_XS_MAXIMIZED : 0) |
                    (x->size_activated ? BC_XS_FOCUSED : 0);
    if (w->state_set && bits == w->state) return;
    w->state = bits;
    w->state_set = 1;
    if (bits & BC_XS_FULLSCREEN) list[n++] = s->x.atoms[BC_ATOM_NET_WM_STATE_FULLSCREEN];
    if (bits & BC_XS_MAX_VERT) list[n++] = s->x.atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_VERT];
    if (bits & BC_XS_MAX_HORZ) list[n++] = s->x.atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_HORZ];
    if (bits & BC_XS_FOCUSED) list[n++] = s->x.atoms[BC_ATOM_NET_WM_STATE_FOCUSED];
    xcb_change_property(s->x.conn, XCB_PROP_MODE_REPLACE, w->window, s->x.atoms[BC_ATOM_NET_WM_STATE],
                        XCB_ATOM_ATOM, 32, n, list);
}

/* The desktop's `WINDOW size` for an X toplevel: its size (0 0 leaves it
   to the program), its states as _NET_WM_STATE, and X's focus when it is
   activated. */
static void x_configure(struct bc_xdg *x)
{
    struct bc_server *s = x->server;
    struct bc_xwin *w = x->xwin;
    if (s->x.conn == NULL || w == NULL) return;
    if (x->size_w > 0 && x->size_h > 0 && x->size_w <= 32767 && x->size_h <= 32767)
        x_resize(s, w, (uint16_t)x->size_w, (uint16_t)x->size_h);
    x_state_write(s, w, x);
    if (x->size_activated) x_activate(s, w);
    else if (s->x.focus == w->window) x_activate(s, NULL);
    xcb_flush(s->x.conn);
}

/* The desktop's `WINDOW close`: WM_DELETE_WINDOW, else the client killed. */
static void x_close(struct bc_xdg *x)
{
    struct bc_server *s = x->server;
    struct bc_xwin *w = x->xwin;
    if (s->x.conn == NULL || w == NULL) return;
    if (w->deletes) {
        x_send_protocol(s, w, s->x.atoms[BC_ATOM_WM_DELETE_WINDOW]);
        logf_("window %u: WM_DELETE_WINDOW", x->id);
    } else {
        xcb_kill_client(s->x.conn, w->window);
        logf_("window %u: takes no WM_DELETE_WINDOW; its X client is killed", x->id);
    }
    xcb_flush(s->x.conn);
}

static int x_contains(const struct bc_xwin *w, int32_t px, int32_t py)
{
    return px >= w->x && py >= w->y && px < w->x + (int32_t)w->width && py < w->y + (int32_t)w->height;
}

/* The toplevel an override-redirect window (a menu, a dropdown, a
   tooltip) is a popup of: the one its WM_TRANSIENT_FOR names (or the
   toplevel of the menu it names); else the one under its origin in X
   space -- a shown popup there first (a submenu over its menu), then the
   topmost toplevel; else the X toplevel the pointer is in (a submenu or
   tooltip that starts past its window's edge). NULL: none. */
static struct bc_xdg *x_popup_parent(struct bc_server *s, struct bc_xwin *w)
{
    struct bc_xwin *o, *top = NULL;
    struct bc_xdg *p;
    if ((p = x_transient_parent(s, w)) != NULL) return p;
    wl_list_for_each(o, &s->x.windows, link)
        if (o != w && o->pic && o->pic->xpopup && o->pic->parent && x_contains(o, w->x, w->y))
            top = o;  /* the newest */
    if (top) return top->pic->parent;
    wl_list_for_each(o, &s->x.windows, link)
        if (o->pic && !o->pic->xpopup && x_contains(o, w->x, w->y) && (top == NULL || o->stack > top->stack))
            top = o;
    if (top) return top->pic;
    p = surface_window(s->ptr_focus);
    return p && p->xwin ? p : NULL;
}

/* The window and its surface matched: shown as a picture -- a toplevel,
   or for an override-redirect window a popup of the toplevel it belongs
   to (x_popup_parent), at its offset from that toplevel in X space. */
static void x_match(struct bc_server *s, struct bc_xwin *w, struct bc_surface *sf)
{
    struct bc_xdg *x, *parent = NULL;
    struct wl_resource *held = sf->xwl_held;
    w->surface = sf;
    sf->xwl_matched = 1;
    x_set_held(sf, NULL);
    if (w->override_redirect && (parent = x_popup_parent(s, w)) == NULL) {
        if (!w->or_logged)
            logf_("X: override-redirect window 0x%x at %d,%d belongs to no shown X window: not shown",
                  (unsigned)w->window, w->x, w->y);
        w->or_logged = 1;
        if (held) wl_buffer_send_release(held);
        return;
    }
    x = calloc(1, sizeof(*x));
    if (x == NULL) {
        logf_("X: window 0x%x: out of memory, not shown", (unsigned)w->window);
        if (held) wl_buffer_send_release(held);
        return;
    }
    x->server = s;
    x->surface = sf;
    x->xwin = w;
    x->input_fd = -1;
    x->kept.fd = -1;
    x->initial = 1;
    wl_list_init(&x->cached_frames);
    wl_list_init(&x->cached_feedback);
    wl_list_init(&x->cached_buffer_destroy.link);
    wl_list_insert(&s->windows, &x->link);
    sf->xdg = x;
    w->pic = x;
    if (parent) {
        /* Its offset from its toplevel in X space, which is its place in
           the toplevel's surface pixels: Xwayland turns a pointer on
           either surface into X's root position by the window's own. */
        x->xpopup = 1;
        x->parent = parent;
        x->px = w->x - parent->xwin->x;
        x->py = w->y - parent->xwin->y;
        if (s->wire.window) announce_popup(x);
        if (x->id == 0)
            logf_("X: override-redirect window 0x%x is not shown: its window %u is not", (unsigned)w->window,
                  parent->id);
    } else {
        /* X programs leave their frame to the window manager, the desktop,
           unless _MOTIF_WM_HINTS says no decorations (they draw their own). */
        x->decor_mode = w->undecorated ? ZXDG_TOPLEVEL_DECORATION_V1_MODE_CLIENT_SIDE
                                       : ZXDG_TOPLEVEL_DECORATION_V1_MODE_SERVER_SIDE;
        memcpy(x->title, w->title, sizeof(x->title));
        memcpy(x->appid, w->appid, sizeof(x->appid));
        /* Its hints as an xdg toplevel's before its first commit: said
           with `WINDOW new` (announce_hints). */
        x->parent = x_transient_parent(s, w);
        x->min_w = w->min_w; x->min_h = w->min_h;
        x->max_w = w->max_w; x->max_h = w->max_h;
        x->fullscreen = (w->state & BC_XS_FULLSCREEN) != 0;
        x->maximize = (w->state & BC_XS_MAXIMIZED) == BC_XS_MAXIMIZED;
        if (s->wire.window) {
            struct bc_xwin *o;
            announce_toplevel(x);
            logf_("window %u: X window 0x%x %dx%d, its client's pid %u (0 unknown)", x->id,
                  (unsigned)w->window, w->width, w->height, w->pid);
            /* Its transients shown before it name it now. */
            wl_list_for_each(o, &s->x.windows, link)
                if (o->transient_for == w->window && o->pic && !o->pic->xpopup && o->pic->parent == NULL)
                    window_set_parent(o->pic, x);
            /* X stacked it on top; the window Blit's pointer rests on goes
               back above it (x_raise), so a press without a move lands. */
            x_pointer_over(s, s->ptr_focus);
        } else {
            logf_("X window 0x%x \"%s\" is not shown: the peer has no window=1", (unsigned)w->window,
                  w->title);
        }
    }
    if (held) {
        int sent = picture_frame(x, sf, held);
        if (sent != BC_SENT_HELD) wl_buffer_send_release(held);
    }
    /* Sized by the desktop before? Not this picture: a new id waits for
       the desktop's size. */
}

/* The window's picture gone, `WINDOW gone`: and with a toplevel's, its
   popups' (an X menu shows only with its window). The surface stays
   matched; its buffers go back to Xwayland unshown. */
static void x_unshow(struct bc_server *s, struct bc_xwin *w)
{
    struct bc_xdg *x = w->pic, *other, *next;
    if (x == NULL) return;
    x_measure_say(w, x);
    if (s->x.focus == w->window) x_activate(s, NULL);
    wl_list_for_each_safe(other, next, &s->windows, link)
        if (other->xpopup && other->parent == x && other->xwin) x_unshow(s, other->xwin);
    window_gone(x);
    if (x->surface) x->surface->xdg = NULL;
    wl_list_for_each(other, &s->windows, link)
        if (other->parent == x) other->parent = NULL;
    wl_list_remove(&x->link);
    free(x);
    w->pic = NULL;
}

/* The window's picture gone (unmapped, destroyed, its surface gone, or
   Xwayland gone), and the surface's match ended. */
static void x_unmatch(struct bc_server *s, struct bc_xwin *w)
{
    x_unshow(s, w);
    w->surface = NULL;
    w->serial = 0;
}

/* The desktop's `WINDOW close` for an X popup: the person went elsewhere.
   X has no popup_done, and an override-redirect window is its program's,
   not the window manager's, to unmap: an X menu closes itself, on the
   click its grab gets (a press anywhere in its program's windows reaches
   it through Xwayland) or when its window loses X's focus (the desktop's
   `WINDOW size` without `activated`). */
static void x_popup_close(struct bc_xdg *x)
{
    logf_("popup %u: WINDOW close for an X menu: it closes itself, on its grab's click or when its "
          "window is deactivated", x->id);
}

/* An Xwayland surface destroyed: its window's picture goes with it. */
static void x_surface_gone(struct bc_surface *sf)
{
    struct bc_server *s = sf->server;
    struct bc_xwin *w;
    if (sf->role != BC_ROLE_XWAYLAND) return;
    wl_list_for_each(w, &s->x.windows, link)
        if (w->surface == sf) x_unmatch(s, w);
    if (sf->xwl) wl_resource_set_user_data(sf->xwl, NULL);
    sf->xwl = NULL;
    x_set_held(sf, NULL);
    wl_list_remove(&sf->xwl_link);
    wl_list_init(&sf->xwl_link);
}

/* A surface's serial applied at its commit: matched to the window that
   has the same serial, when its WL_SURFACE_SERIAL came first. */
static void x_surface_committed(struct bc_surface *sf)
{
    struct bc_server *s = sf->server;
    struct bc_xwin *w;
    sf->xwl_serial = sf->xwl_pending;
    sf->xwl_pending = 0;
    wl_list_for_each(w, &s->x.windows, link)
        if (w->serial == sf->xwl_serial && w->surface == NULL) {
            x_match(s, w, sf);
            return;
        }
}

/* WL_SURFACE_SERIAL on a window: matched to the surface whose committed
   serial it is, when that commit came first. */
static void x_serial_message(struct bc_server *s, xcb_client_message_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    struct bc_surface *sf;
    uint64_t serial = (uint64_t)ev->data.data32[1] << 32 | ev->data.data32[0];
    if (w == NULL) {
        logf_("X: WL_SURFACE_SERIAL for 0x%x, no window of the root", (unsigned)ev->window);
        return;
    }
    if (w->surface) {
        logf_("X: a second WL_SURFACE_SERIAL for window 0x%x while it is matched", (unsigned)w->window);
        return;
    }
    w->serial = serial;
    wl_list_for_each(sf, &s->x.surfaces, xwl_link)
        if (sf->xwl_serial == serial && !sf->xwl_matched) {
            x_match(s, w, sf);
            return;
        }
}

static void x_window_free(struct bc_server *s, struct bc_xwin *w)
{
    x_unmatch(s, w);
    if (s->x.focus == w->window) s->x.focus = XCB_WINDOW_NONE;
    wl_list_remove(&w->link);
    free(w);
}

/* ---- the window manager's events ---- */

/* A child of the root, known from here on; its focus and property changes
   selected. */
static struct bc_xwin *x_window_new(struct bc_server *s, xcb_window_t window, int16_t x, int16_t y,
                                    uint16_t width, uint16_t height, int override_redirect)
{
    struct bc_xwin *w;
    uint32_t mask = XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE;
    if (window == s->x.wm_window || x_window(s, window)) return NULL;
    w = calloc(1, sizeof(*w));
    if (w == NULL) return NULL;
    w->window = window;
    w->override_redirect = override_redirect;
    w->x = x;
    w->y = y;
    w->width = width;
    w->height = height;
    w->input = 1;
    /* X stacks a new window above its siblings. */
    w->stack = ++s->x.stack_seq;
    wl_list_insert(s->x.windows.prev, &w->link);
    xcb_change_window_attributes(s->x.conn, w->window, XCB_CW_EVENT_MASK, &mask);
    return w;
}

static void x_create_notify(struct bc_server *s, xcb_create_notify_event_t *ev)
{
    if (ev->parent == s->x.screen->root)
        x_window_new(s, ev->window, ev->x, ev->y, ev->width, ev->height, ev->override_redirect);
}

/* The root's children from before the window manager: an X program that
   mapped its window in the moment between Xwayland's start and the window
   manager's had its WL_SURFACE_SERIAL go to no one, so each such window is
   unmapped and mapped again under the window manager -- a new surface and
   serial, matched and shown as any other. */
static void x_adopt(struct bc_server *s)
{
    xcb_connection_t *c = s->x.conn;
    xcb_query_tree_reply_t *tree = xcb_query_tree_reply(c, xcb_query_tree(c, s->x.screen->root), NULL);
    xcb_window_t *children;
    int i, n;
    if (tree == NULL) return;
    children = xcb_query_tree_children(tree);
    n = xcb_query_tree_children_length(tree);
    for (i = 0; i < n; ++i) {
        xcb_get_window_attributes_reply_t *a =
            xcb_get_window_attributes_reply(c, xcb_get_window_attributes(c, children[i]), NULL);
        xcb_get_geometry_reply_t *g = xcb_get_geometry_reply(c, xcb_get_geometry(c, children[i]), NULL);
        struct bc_xwin *w = a && g ? x_window_new(s, children[i], g->x, g->y, g->width, g->height,
                                                  a->override_redirect)
                                   : NULL;
        if (w && a->map_state == XCB_MAP_STATE_VIEWABLE && !w->override_redirect) {
            x_read_properties(s, w);
            w->remapping = 1;
            xcb_unmap_window(c, w->window);
            x_set_wm_state(s, w, BC_WM_STATE_NORMAL);
            xcb_map_window(c, w->window);
            logf_("X: window 0x%x was mapped before the window manager: mapped again", (unsigned)w->window);
        }
        free(a);
        free(g);
    }
    free(tree);
}

static void x_map_request(struct bc_server *s, xcb_map_request_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    if (w == NULL) {
        /* Not a child of the root we were told of: mapped as asked. */
        xcb_map_window(s->x.conn, ev->window);
        return;
    }
    x_read_properties(s, w);
    x_set_wm_state(s, w, BC_WM_STATE_NORMAL);
    {
        int32_t nx = w->x, ny = w->y;
        x_fit(s, w->width, w->height, &nx, &ny);
        if (nx != w->x || ny != w->y) {
            uint32_t values[2];
            values[0] = (uint32_t)nx;
            values[1] = (uint32_t)ny;
            xcb_configure_window(s->x.conn, w->window, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, values);
            logf_("X: window 0x%x placed at %d,%d, inside the root (it asked for %d,%d)", (unsigned)w->window,
                  nx, ny, w->x, w->y);
            w->x = (int16_t)nx;
            w->y = (int16_t)ny;
        }
    }
    xcb_map_window(s->x.conn, w->window);
}

static void x_unmap_notify(struct bc_server *s, xcb_unmap_notify_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    if (w == NULL) return;
    x_unmatch(s, w);
    if (w->remapping) {
        w->remapping = 0;  /* x_adopt's: mapped again already */
    } else if (!w->override_redirect) {
        /* Withdrawn: no WM_STATE Normal, no _NET_WM_STATE (EWMH); mapped
           again, its own property is read afresh. */
        x_set_wm_state(s, w, BC_WM_STATE_WITHDRAWN);
        if (w->state_set) xcb_delete_property(s->x.conn, w->window, s->x.atoms[BC_ATOM_NET_WM_STATE]);
        w->state = 0;
        w->state_set = 0;
    }
}

static void x_configure_request(struct bc_server *s, xcb_configure_request_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    uint32_t values[4];
    uint16_t mask = 0;
    int n = 0;
    if (w == NULL) return;
    if (w->pic && w->pic->size_w > 0 && w->pic->size_h > 0) {
        /* Shown and sized by the desktop: its size stands, and the client
           is told so. */
        x_resize(s, w, (uint16_t)w->pic->size_w, (uint16_t)w->pic->size_h);
        return;
    }
    /* Before the desktop's size: the geometry asked for, kept inside the
       root (x_fit). Stacking is the desktop's: not passed on. */
    {
        int32_t gx = ev->value_mask & XCB_CONFIG_WINDOW_X ? ev->x : w->x;
        int32_t gy = ev->value_mask & XCB_CONFIG_WINDOW_Y ? ev->y : w->y;
        int32_t gw = ev->value_mask & XCB_CONFIG_WINDOW_WIDTH ? ev->width : w->width;
        int32_t gh = ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT ? ev->height : w->height;
        x_fit(s, gw, gh, &gx, &gy);
        if (ev->value_mask & (XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_WIDTH) || gx != w->x) {
            mask |= XCB_CONFIG_WINDOW_X;
            values[n++] = (uint32_t)gx;
        }
        if (ev->value_mask & (XCB_CONFIG_WINDOW_Y | XCB_CONFIG_WINDOW_HEIGHT) || gy != w->y) {
            mask |= XCB_CONFIG_WINDOW_Y;
            values[n++] = (uint32_t)gy;
        }
        if (ev->value_mask & XCB_CONFIG_WINDOW_WIDTH) { mask |= XCB_CONFIG_WINDOW_WIDTH; values[n++] = ev->width; }
        if (ev->value_mask & XCB_CONFIG_WINDOW_HEIGHT) { mask |= XCB_CONFIG_WINDOW_HEIGHT; values[n++] = ev->height; }
    }
    if (mask) xcb_configure_window(s->x.conn, w->window, mask, values);
}

static void x_configure_notify(struct bc_server *s, xcb_configure_notify_event_t *ev)
{
    struct bc_xwin *w;
    if (ev->window == s->x.screen->root) {
        /* The outputs changed (Xwayland's RandR): the root with them. */
        s->x.root_w = ev->width;
        s->x.root_h = ev->height;
        return;
    }
    w = x_window(s, ev->window);
    if (w == NULL || ev->event != s->x.screen->root) return;
    if (w->pic && w->pic->xpopup && w->pic->parent && w->pic->parent->xwin &&
        (ev->x != w->x || ev->y != w->y))
        popup_moved(w->pic, ev->x - w->pic->parent->xwin->x, ev->y - w->pic->parent->xwin->y);
    w->x = ev->x;
    w->y = ev->y;
    w->width = ev->width;
    w->height = ev->height;
    w->override_redirect = ev->override_redirect;
}

/* A program's _NET_WM_STATE request (EWMH: to the root, about its window):
   its full screen and maximize as xdg_toplevel's set_fullscreen and
   set_maximized -- `WINDOW fullscreen` and `WINDOW maximize`, which the
   desktop answers with `WINDOW size`. Maximized is both directions (xdg
   has no half); a toggle is from the state the property says. Before the
   window is shown it is the state it is shown with. */
static void x_state_request(struct bc_server *s, xcb_client_message_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    struct bc_xdg *x;
    unsigned bits, named = 0, i;
    if (w == NULL || w->override_redirect) return;
    bits = w->state;
    for (i = 1; i <= 2; ++i) {
        unsigned bit = x_state_bit(s, ev->data.data32[i]);
        if (bit == 0 || bit == BC_XS_FOCUSED) continue;
        named |= bit;
        if (ev->data.data32[0] == BC_NET_STATE_REMOVE) bits &= ~bit;
        else if (ev->data.data32[0] == BC_NET_STATE_ADD) bits |= bit;
        else if (ev->data.data32[0] == BC_NET_STATE_TOGGLE) bits ^= bit;
        else return;
    }
    x = w->pic;
    if (x == NULL) {
        if (!w->state_set) w->state = bits;
        return;
    }
    /* Only the states it names: the others stand as they are. */
    if ((named & BC_XS_FULLSCREEN) && ((bits & BC_XS_FULLSCREEN) != 0) != (x->fullscreen != 0))
        window_set_fullscreen(x, (bits & BC_XS_FULLSCREEN) != 0);
    if (named & BC_XS_MAXIMIZED) toplevel_maximize(x, (bits & BC_XS_MAXIMIZED) == BC_XS_MAXIMIZED);
}

/* A program's _NET_WM_MOVERESIZE (EWMH; its own title bar or edge, as
   Steam's): `WINDOW move` or `WINDOW resize <edges>`, as xdg_toplevel's
   move and resize, under the same rule (window_grab_ok): a press the
   compositor delivered to the window is down -- the button the message
   names (1, 2, 3), or any when it names none (0). X has no serial; that
   is the whole of the rule here. The keyboard's move and size (9, 10)
   have no line, and a cancel (11) is the desktop's drag ending at the
   release. */
static void x_moveresize_request(struct bc_server *s, xcb_client_message_event_t *ev)
{
    static const char *const edges[8] = { "top-left", "top", "top-right", "right",
                                           "bottom-right", "bottom", "bottom-left", "left" };
    /* X buttons 1, 2, 3: BTN_LEFT, BTN_MIDDLE, BTN_RIGHT, bc_server.buttons_down's bits 0, 2, 1. */
    static const uint32_t bits[4] = { 0, 1u << 0, 1u << 2, 1u << 1 };
    struct bc_xwin *w = x_window(s, ev->window);
    uint32_t direction = ev->data.data32[2], button = ev->data.data32[3];
    int press_ok;
    if (w == NULL || w->pic == NULL || w->pic->xpopup) return;
    if (direction > BC_MOVERESIZE_MOVE) {
        if (direction != BC_MOVERESIZE_CANCEL)
            logf_("window %u: _NET_WM_MOVERESIZE %u (by keyboard) has no line; ignored", w->pic->id, direction);
        return;
    }
    press_ok = button == 0 || (button <= 3 && (s->buttons_down & bits[button]));
    if (!window_grab_ok(w->pic, press_ok)) return;
    if (direction == BC_MOVERESIZE_MOVE) window_move(w->pic);
    else window_resize(w->pic, edges[direction]);
}

static void x_client_message(struct bc_server *s, xcb_client_message_event_t *ev)
{
    xcb_atom_t *atoms = s->x.atoms;
    if (ev->type == atoms[BC_ATOM_WL_SURFACE_SERIAL] && ev->format == 32) {
        x_serial_message(s, ev);
    } else if (ev->type == atoms[BC_ATOM_WL_SURFACE_ID]) {
        if (!(s->x.logged & BC_X_LOGGED_ID))
            logf_("X: Xwayland named a surface by WL_SURFACE_ID, not xwayland_shell_v1: it is "
                  "older than 23.1 or did not bind the shell; its windows are not shown");
        s->x.logged |= BC_X_LOGGED_ID;
    } else if (ev->type == atoms[BC_ATOM_NET_WM_STATE] && ev->format == 32) {
        x_state_request(s, ev);
    } else if (ev->type == atoms[BC_ATOM_NET_WM_MOVERESIZE] && ev->format == 32) {
        x_moveresize_request(s, ev);
    } else if (ev->type == atoms[BC_ATOM_NET_ACTIVE_WINDOW]) {
        x_note(s, BC_X_LOGGED_ACTIVE, "_NET_ACTIVE_WINDOW requests (a program raising its window: the "
                                      "draft has no line for it)");
    } else if (ev->type == atoms[BC_ATOM_WM_CHANGE_STATE] && ev->format == 32) {
        /* ICCCM 4.1.4: IconicState is the program minimizing its window
           (its own title bar's button), as xdg_toplevel.set_minimized. */
        struct bc_xwin *w = x_window(s, ev->window);
        if (w && w->pic && !w->pic->xpopup && ev->data.data32[0] == BC_WM_STATE_ICONIC)
            window_minimize(w->pic);
    }
}

static void x_property_notify(struct bc_server *s, xcb_property_notify_event_t *ev)
{
    struct bc_xwin *w = x_window(s, ev->window);
    xcb_get_property_reply_t *reply;
    if (w == NULL) return;
    if (ev->state == XCB_PROPERTY_DELETE) {
        x_property(s, w, ev->atom, NULL);
        return;
    }
    reply = xcb_get_property_reply(s->x.conn,
                                   xcb_get_property(s->x.conn, 0, w->window, ev->atom, XCB_ATOM_ANY, 0, 2048),
                                   NULL);
    x_property(s, w, ev->atom, reply && reply->type != XCB_ATOM_NONE ? reply : NULL);
    free(reply);
}

static void x_event(struct bc_server *s, xcb_generic_event_t *ev)
{
    switch (ev->response_type & 0x7f) {
    case 0: {
        /* BadWindow is a window gone before our request about it (its
           WM_STATE at its unmap, a focus): X's ordinary race, not said. */
        xcb_generic_error_t *e = (xcb_generic_error_t *)ev;
        if (e->error_code != XCB_WINDOW)
            logf_("X: error %u on request %u.%u (sequence %u, resource 0x%x)", e->error_code,
                  e->major_code, e->minor_code, e->sequence, e->resource_id);
        return;
    }
    case XCB_CREATE_NOTIFY:
        x_create_notify(s, (xcb_create_notify_event_t *)ev);
        return;
    case XCB_DESTROY_NOTIFY: {
        struct bc_xwin *w = x_window(s, ((xcb_destroy_notify_event_t *)ev)->window);
        if (w) x_window_free(s, w);
        return;
    }
    case XCB_MAP_REQUEST:
        x_map_request(s, (xcb_map_request_event_t *)ev);
        return;
    case XCB_MAP_NOTIFY: {
        xcb_map_notify_event_t *m = (xcb_map_notify_event_t *)ev;
        struct bc_xwin *w = x_window(s, m->window);
        if (w == NULL) return;
        w->override_redirect = m->override_redirect;
        /* An override-redirect window has no MapRequest: its properties
           (WM_TRANSIENT_FOR, its parent) are read at its MapNotify, which
           comes before Xwayland's WL_SURFACE_SERIAL for it. */
        if (w->override_redirect) x_read_properties(s, w);
        return;
    }
    case XCB_UNMAP_NOTIFY:
        x_unmap_notify(s, (xcb_unmap_notify_event_t *)ev);
        return;
    case XCB_CONFIGURE_REQUEST:
        x_configure_request(s, (xcb_configure_request_event_t *)ev);
        return;
    case XCB_CONFIGURE_NOTIFY:
        x_configure_notify(s, (xcb_configure_notify_event_t *)ev);
        return;
    case XCB_PROPERTY_NOTIFY:
        x_property_notify(s, (xcb_property_notify_event_t *)ev);
        return;
    case XCB_CLIENT_MESSAGE:
        x_client_message(s, (xcb_client_message_event_t *)ev);
        return;
    case XCB_FOCUS_IN: {
        xcb_focus_in_event_t *f = (xcb_focus_in_event_t *)ev;
        struct bc_xwin *w = x_window(s, f->event);
        /* A program moving X's focus itself (milestone A only notes it). */
        if (w && w->pic && f->event != s->x.focus && f->mode == XCB_NOTIFY_MODE_NORMAL &&
            f->detail != XCB_NOTIFY_DETAIL_POINTER)
            logf_("window %u: its program took X's focus itself", w->pic->id);
        return;
    }
    default:
        if (s->x.xfixes_event && (ev->response_type & 0x7f) == s->x.xfixes_event + XCB_XFIXES_CURSOR_NOTIFY)
            x_cursor_notify(s, (xcb_xfixes_cursor_notify_event_t *)ev);
        return;
    }
}

static void x_wm_stop(struct bc_server *s);

static int on_x_events(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    xcb_generic_event_t *ev;
    int count = 0;
    (void)fd;
    if (s->x.conn == NULL) return 0;
    while ((ev = xcb_poll_for_event(s->x.conn)) != NULL) {
        x_event(s, ev);
        free(ev);
        ++count;
        if (s->x.conn == NULL) return 0;
    }
    if (xcb_connection_has_error(s->x.conn) || (mask & (WL_EVENT_HANGUP | WL_EVENT_ERROR))) {
        logf_("X: the window manager's connection to Xwayland ended");
        x_wm_stop(s);
        return 0;
    }
    if (count) xcb_flush(s->x.conn);
    return count;
}

/* Every window forgotten (their pictures gone) and the connection closed. */
static void x_wm_stop(struct bc_server *s)
{
    while (!wl_list_empty(&s->x.windows)) {
        struct bc_xwin *w = wl_container_of(s->x.windows.next, w, link);
        x_window_free(s, w);
    }
    if (s->x.wm_source) wl_event_source_remove(s->x.wm_source);
    s->x.wm_source = NULL;
    if (s->x.conn) xcb_disconnect(s->x.conn);
    s->x.conn = NULL;
    s->x.screen = NULL;
    s->x.focus = XCB_WINDOW_NONE;
    /* Atoms are the X server's: a new one names them afresh. */
    s->x.xfixes_event = 0;
    s->x.cursor[0] = '\0';
    s->x.cursor_shown = 0;
    memset(s->x.cursor_names, 0, sizeof(s->x.cursor_names));
    if (s->x.wm_fd >= 0) close(s->x.wm_fd);
    s->x.wm_fd = -1;
}

/* The window manager on its connection, once Xwayland is ready. Returns 0
   when it manages the display. */
static int x_wm_start(struct bc_server *s)
{
    xcb_connection_t *c;
    xcb_intern_atom_cookie_t cookies[BC_ATOMS];
    const xcb_query_extension_reply_t *ext;
    xcb_generic_error_t *error;
    uint32_t mask;
    xcb_atom_t supported[9];
    static const char name[] = "blit-compositor";
    int i;
    c = xcb_connect_to_fd(s->x.wm_fd, NULL);
    s->x.wm_fd = -1;  /* the connection's now, closed with it */
    if (xcb_connection_has_error(c)) {
        logf_("X: the window manager could not connect to Xwayland (xcb error %d)",
              xcb_connection_has_error(c));
        xcb_disconnect(c);
        return -1;
    }
    s->x.conn = c;
    s->x.screen = xcb_setup_roots_iterator(xcb_get_setup(c)).data;
    xcb_prefetch_extension_data(c, &xcb_composite_id);
    xcb_prefetch_extension_data(c, &xcb_xfixes_id);
    xcb_prefetch_extension_data(c, &xcb_res_id);
    for (i = 0; i < BC_ATOMS; ++i)
        cookies[i] = xcb_intern_atom(c, 0, (uint16_t)strlen(bc_atom_names[i]), bc_atom_names[i]);
    ext = xcb_get_extension_data(c, &xcb_composite_id);
    if (ext == NULL || !ext->present) {
        logf_("X: Xwayland has no Composite extension; no window manager");
        x_wm_stop(s);
        return -1;
    }
    /* Composite first: every top-level's pixels are its surface's. */
    xcb_composite_redirect_subwindows(c, s->x.screen->root, XCB_COMPOSITE_REDIRECT_MANUAL);
    mask = XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY |
           XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_PROPERTY_CHANGE;
    s->x.root_w = s->x.screen->width_in_pixels;
    s->x.root_h = s->x.screen->height_in_pixels;
    error = xcb_request_check(c, xcb_change_window_attributes_checked(c, s->x.screen->root,
                                                                      XCB_CW_EVENT_MASK, &mask));
    if (error) {
        logf_("X: another window manager holds the root (X error %u)", error->error_code);
        free(error);
        x_wm_stop(s);
        return -1;
    }
    for (i = 0; i < BC_ATOMS; ++i) {
        xcb_intern_atom_reply_t *reply = xcb_intern_atom_reply(c, cookies[i], NULL);
        s->x.atoms[i] = reply ? reply->atom : XCB_ATOM_NONE;
        free(reply);
    }
    ext = xcb_get_extension_data(c, &xcb_xfixes_id);
    if (ext && ext->present) {
        xcb_xfixes_query_version_reply_t *v =
            xcb_xfixes_query_version_reply(c, xcb_xfixes_query_version(c, XCB_XFIXES_MAJOR_VERSION,
                                                                        XCB_XFIXES_MINOR_VERSION), NULL);
        if (v) logf_("X: XFixes %u.%u", v->major_version, v->minor_version);
        /* Cursor names (XFixes 2): CursorNotify when X's cursor changes. */
        s->x.xfixes_event = 0;
        if (v && v->major_version >= 2) {
            s->x.xfixes_event = ext->first_event;
            xcb_xfixes_select_cursor_input(c, s->x.screen->root, XCB_XFIXES_CURSOR_NOTIFY_MASK_DISPLAY_CURSOR);
        }
        free(v);
    }
    s->x.xres = 0;
    ext = xcb_get_extension_data(c, &xcb_res_id);
    if (ext && ext->present) {
        xcb_res_query_version_reply_t *v =
            xcb_res_query_version_reply(c, xcb_res_query_version(c, XCB_RES_MAJOR_VERSION,
                                                                  XCB_RES_MINOR_VERSION), NULL);
        s->x.xres = v && (v->server_major > 1 || (v->server_major == 1 && v->server_minor >= 2));
        free(v);
    }
    /* A window of its own: _NET_SUPPORTING_WM_CHECK, the selections a
       window manager and a compositing manager own (ICCCM 2.8, EWMH). */
    s->x.wm_window = xcb_generate_id(c);
    xcb_create_window(c, XCB_COPY_FROM_PARENT, s->x.wm_window, s->x.screen->root, 0, 0, 10, 10, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, s->x.screen->root_visual, 0, NULL);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, s->x.wm_window, s->x.atoms[BC_ATOM_NET_WM_NAME],
                        s->x.atoms[BC_ATOM_UTF8_STRING], 8, (uint32_t)strlen(name), name);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, s->x.screen->root,
                        s->x.atoms[BC_ATOM_NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1,
                        &s->x.wm_window);
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, s->x.wm_window,
                        s->x.atoms[BC_ATOM_NET_SUPPORTING_WM_CHECK], XCB_ATOM_WINDOW, 32, 1,
                        &s->x.wm_window);
    supported[0] = s->x.atoms[BC_ATOM_NET_WM_STATE];
    supported[1] = s->x.atoms[BC_ATOM_NET_WM_STATE_FULLSCREEN];
    supported[2] = s->x.atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_VERT];
    supported[3] = s->x.atoms[BC_ATOM_NET_WM_STATE_MAXIMIZED_HORZ];
    supported[4] = s->x.atoms[BC_ATOM_NET_ACTIVE_WINDOW];
    supported[5] = s->x.atoms[BC_ATOM_NET_WM_MOVERESIZE];
    supported[6] = s->x.atoms[BC_ATOM_NET_WM_NAME];
    supported[7] = s->x.atoms[BC_ATOM_NET_SUPPORTING_WM_CHECK];
    supported[8] = s->x.atoms[BC_ATOM_NET_WM_STATE_FOCUSED];
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, s->x.screen->root, s->x.atoms[BC_ATOM_NET_SUPPORTED],
                        XCB_ATOM_ATOM, 32, 9, supported);
    xcb_set_selection_owner(c, s->x.wm_window, s->x.atoms[BC_ATOM_WM_S0], XCB_CURRENT_TIME);
    xcb_set_selection_owner(c, s->x.wm_window, s->x.atoms[BC_ATOM_NET_WM_CM_S0], XCB_CURRENT_TIME);
    s->x.focus = XCB_WINDOW_NONE;
    xcb_change_property(c, XCB_PROP_MODE_REPLACE, s->x.screen->root,
                        s->x.atoms[BC_ATOM_NET_ACTIVE_WINDOW], XCB_ATOM_WINDOW, 32, 1, &s->x.focus);
    x_adopt(s);
    xcb_flush(c);
    s->x.wm_source = wl_event_loop_add_fd(s->loop, xcb_get_file_descriptor(c), WL_EVENT_READABLE,
                                          on_x_events, s);
    /* Events xcb read while waiting for the replies above are in its
       queue, not the socket: checked after every dispatch. */
    if (s->x.wm_source) wl_event_source_check(s->x.wm_source);
    return 0;
}

/* ---- Xwayland, the process ---- */

/* Xwayland's -version text: its version, or -1 when it says none. */
static int x_version_of(const char *text, int *major, int *minor)
{
    const char *p = text;
    while ((p = strstr(p, "Version ")) != NULL) {
        p += 8;
        if (*p >= '0' && *p <= '9' && sscanf(p, "%d.%d", major, minor) == 2) return 0;
    }
    return -1;
}

/* Runs `path -version` and reads what it says (Xwayland says it on
   standard error), at most three seconds. */
static int x_version_text(const char *path, char *out, size_t size)
{
    int fd[2], status;
    size_t n = 0;
    pid_t pid;
    uint64_t until = now_us() + 3000000u;
    if (pipe2(fd, O_CLOEXEC) != 0) return -1;
    pid = fork();
    if (pid < 0) {
        close(fd[0]);
        close(fd[1]);
        return -1;
    }
    if (pid == 0) {
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        dup2(fd[1], 1);
        dup2(fd[1], 2);
        execl(path, path, "-version", (char *)NULL);
        _exit(127);
    }
    close(fd[1]);
    while (n + 1 < size) {
        struct pollfd p;
        ssize_t got;
        uint64_t now = now_us();
        if (now >= until) break;
        p.fd = fd[0];
        p.events = POLLIN;
        if (poll(&p, 1, (int)((until - now) / 1000u) + 1) <= 0) break;
        got = read(fd[0], out + n, size - 1 - n);
        if (got <= 0) break;
        n += (size_t)got;
    }
    out[n] = '\0';
    close(fd[0]);
    kill(pid, SIGKILL);
    waitpid(pid, &status, 0);
    return 0;
}

/* The Xwayland to run: BLIT_COMPOSITOR_XWAYLAND's (a path, or a name on
   PATH), else `Xwayland` on PATH. */
static int x_find(const char *want, char *out, size_t size)
{
    const char *path = getenv("PATH"), *p, *end;
    if (strchr(want, '/')) {
        if (access(want, X_OK) != 0) return -1;
        snprintf(out, size, "%s", want);
        return 0;
    }
    for (p = path ? path : "/usr/bin:/bin"; *p; p = *end ? end + 1 : end) {
        end = strchr(p, ':');
        if (end == NULL) end = p + strlen(p);
        if (end > p && (size_t)snprintf(out, size, "%.*s/%s", (int)(end - p), p, want) < size &&
            access(out, X_OK) == 0)
            return 0;
    }
    return -1;
}

/* /tmp/.X11-unix as X expects it: ours or root's, and not one others can
   write into unless it is sticky. */
static int x_socket_dir(void)
{
    struct stat st;
    if (mkdir("/tmp/.X11-unix", 01777) == 0) chmod("/tmp/.X11-unix", 01777);
    else if (errno != EEXIST) return -1;
    if (lstat("/tmp/.X11-unix", &st) != 0 || !S_ISDIR(st.st_mode) ||
        (st.st_uid != 0 && st.st_uid != getuid()) ||
        (!(st.st_mode & S_ISVTX) && (st.st_mode & (S_IWGRP | S_IWOTH))))
        return -1;
    return 0;
}

static int x_listen(const char *path, int abstract)
{
    struct sockaddr_un addr;
    socklen_t len;
    int fd = socket(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0);
    if (fd < 0) return -1;
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + (abstract ? 1 : 0), path, strlen(path));
    len = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + (abstract ? 1 : 0) + strlen(path));
    if (!abstract) unlink(path);
    /* A backlog for the X programs that come before Xwayland is ready. */
    if (bind(fd, (struct sockaddr *)&addr, len) != 0 || listen(fd, 64) != 0) {
        close(fd);
        return -1;
    }
    return fd;
}

/* The first free display: its lock taken (a dead pid's taken over) and
   both its sockets bound. Returns its number, or -1. */
static int x_sockets(struct bc_server *s)
{
    int n;
    if (x_socket_dir() != 0) return -1;
    for (n = 0; n < BC_X_DISPLAYS; ++n) {
        char lock[32], path[32], pid[16];
        int fd = -1;
        snprintf(lock, sizeof(lock), "/tmp/.X%d-lock", n);
        snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", n);
        fd = open(lock, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444);
        if (fd < 0) {
            /* Another's, unless its pid is dead. */
            char held[16] = { 0 };
            long other;
            int r = open(lock, O_RDONLY | O_CLOEXEC);
            if (r < 0) continue;
            if (read(r, held, sizeof(held) - 1) <= 0) held[0] = '\0';
            close(r);
            other = strtol(held, NULL, 10);
            if (other <= 0 || kill((pid_t)other, 0) == 0 || errno != ESRCH || unlink(lock) != 0)
                continue;
            fd = open(lock, O_WRONLY | O_CREAT | O_EXCL | O_CLOEXEC, 0444);
            if (fd < 0) continue;
        }
        snprintf(pid, sizeof(pid), "%10d\n", (int)getpid());
        if (write(fd, pid, 11) != 11) {
            close(fd);
            unlink(lock);
            continue;
        }
        close(fd);
        s->x.listen_fd[0] = x_listen(path, 1);
        s->x.listen_fd[1] = s->x.listen_fd[0] >= 0 ? x_listen(path, 0) : -1;
        if (s->x.listen_fd[1] < 0) {
            if (s->x.listen_fd[0] >= 0) close(s->x.listen_fd[0]);
            s->x.listen_fd[0] = -1;
            unlink(lock);
            continue;
        }
        return n;
    }
    return -1;
}

static void x_sockets_close(struct bc_server *s)
{
    char path[32];
    int k;
    for (k = 0; k < 2; ++k)
        if (s->x.listen_fd[k] >= 0) close(s->x.listen_fd[k]);
    s->x.listen_fd[0] = s->x.listen_fd[1] = -1;
    if (s->x.number < 0) return;
    snprintf(path, sizeof(path), "/tmp/.X11-unix/X%d", s->x.number);
    unlink(path);
    snprintf(path, sizeof(path), "/tmp/.X%d-lock", s->x.number);
    unlink(path);
}

/* Xwayland's Wayland connection is gone: its surfaces went with it. */
static void x_client_gone(struct wl_listener *listener, void *data)
{
    struct bc_server *s = wl_container_of(listener, s, x.client_gone);
    (void)data;
    wl_list_remove(&s->x.client_gone.link);
    wl_list_init(&s->x.client_gone.link);
    s->x.client = NULL;
    x_wm_stop(s);
    /* It ends itself without its compositor; SIGCHLD restarts it. */
    if (s->x.pid > 0) kill(s->x.pid, SIGTERM);
}

static int on_x_ready(int fd, uint32_t mask, void *data)
{
    struct bc_server *s = data;
    char buf[32];
    ssize_t got = (mask & WL_EVENT_READABLE) ? read(fd, buf, sizeof(buf) - 1) : 0;
    if (got > 0 && buf[got - 1] != '\n') return 0;  /* the rest of its line follows */
    wl_event_source_remove(s->x.ready_source);
    s->x.ready_source = NULL;
    close(s->x.ready_fd);
    s->x.ready_fd = -1;
    if (got <= 0) {
        logf_("X: Xwayland closed -displayfd without saying it is ready");
        return 0;  /* its SIGCHLD follows */
    }
    if (x_wm_start(s) != 0) {
        if (s->x.pid > 0) kill(s->x.pid, SIGTERM);
        return 0;
    }
    logf_("X: Xwayland ready on %s after %llu ms: window manager on (pid %d)", s->x.display,
          (unsigned long long)((now_us() - s->x.started_us) / 1000u), (int)s->x.pid);
    return 0;
}

/* Xwayland ended: its windows gone, and started again unless it keeps
   dying at once. */
static int on_x_child(int signal_number, void *data)
{
    struct bc_server *s = data;
    int status;
    pid_t pid;
    uint64_t lived;
    (void)signal_number;
    if (s->x.pid <= 0 || (pid = waitpid(s->x.pid, &status, WNOHANG)) != s->x.pid) return 0;
    s->x.pid = 0;
    if (WIFEXITED(status)) logf_("X: Xwayland exited with status %d", WEXITSTATUS(status));
    else if (WIFSIGNALED(status)) logf_("X: Xwayland was killed by signal %d", WTERMSIG(status));
    x_wm_stop(s);
    if (s->x.ready_source) wl_event_source_remove(s->x.ready_source);
    s->x.ready_source = NULL;
    if (s->x.ready_fd >= 0) close(s->x.ready_fd);
    s->x.ready_fd = -1;
    if (s->x.client) wl_client_destroy(s->x.client);
    if (s->x.stopping) return 0;
    lived = now_us() - s->x.started_us;
    s->x.quick_deaths = lived < BC_X_QUICK_US ? s->x.quick_deaths + 1 : 0;
    if (s->x.quick_deaths >= BC_X_QUICK_LIMIT) {
        logf_("X: Xwayland died within %llu s of its start %d times in a row: X programs are off "
              "until the compositor restarts", BC_X_QUICK_US / 1000000ull, s->x.quick_deaths);
        x_sockets_close(s);
        s->x.on = 0;
        /* A desktop that connects from here on is told X is off. */
        snprintf(s->x.display, sizeof(s->x.display), "-");
        return 0;
    }
    logf_("X: starting Xwayland again on %s", s->x.display);
    wl_event_source_timer_update(s->x.restart, s->x.quick_deaths ? 1000 * s->x.quick_deaths : 100);
    return 0;
}

static int on_x_restart(void *data)
{
    struct bc_server *s = data;
    if (s->x.on && !s->x.stopping && s->x.pid == 0 && x_launch(s) != 0)
        wl_event_source_timer_update(s->x.restart, 2000);
    return 0;
}

/* Xwayland started on the display: its Wayland client made of a
   socketpair, its window manager's connection, its ready pipe. */
static int x_launch(struct bc_server *s)
{
    int wl[2] = { -1, -1 }, wm[2] = { -1, -1 }, ready[2] = { -1, -1 };
    pid_t pid;
    if (socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wl) != 0 ||
        socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, wm) != 0 || pipe2(ready, O_CLOEXEC) != 0) {
        logf_("X: Xwayland not started: %s", strerror(errno));
        goto fail;
    }
    s->x.client = wl_client_create(s->display, wl[0]);
    if (s->x.client == NULL) {
        logf_("X: Xwayland not started: no Wayland client for it");
        goto fail;
    }
    wl[0] = -1;  /* the client's */
    s->x.client_gone.notify = x_client_gone;
    wl_client_add_destroy_listener(s->x.client, &s->x.client_gone);
    s->x.started_us = now_us();
    pid = fork();
    if (pid < 0) {
        logf_("X: Xwayland not started: fork: %s", strerror(errno));
        wl_client_destroy(s->x.client);
        goto fail;
    }
    if (pid == 0) {
        /* The descriptors it is given at 3 to 7 and no other; the signals
           libwayland blocks for its signalfd, and SIGPIPE, as they were. */
        int given[5], k, fd;
        char display[16];
        sigset_t none;
        sigemptyset(&none);
        sigprocmask(SIG_SETMASK, &none, NULL);
        signal(SIGPIPE, SIG_DFL);
        if (s->log) {
            dup2(fileno(s->log), 1);
            dup2(fileno(s->log), 2);
        }
        given[0] = s->x.listen_fd[0];
        given[1] = s->x.listen_fd[1];
        given[2] = wm[1];
        given[3] = wl[1];
        given[4] = ready[1];
        for (k = 0; k < 5; ++k)
            if ((given[k] = fcntl(given[k], F_DUPFD, 64)) < 0) _exit(127);
        for (k = 0; k < 5; ++k)
            if (dup2(given[k], 3 + k) < 0) _exit(127);
        if (close_range(8, ~0u, 0) != 0)
            for (fd = 8; fd < 65536; ++fd) close(fd);
        setenv("WAYLAND_SOCKET", "6", 1);
        unsetenv("WAYLAND_DISPLAY");
        snprintf(display, sizeof(display), ":%d", s->x.number);
        execl(s->x.path, "Xwayland", display, "-rootless", "-listenfd", "3", "-listenfd", "4", "-wm", "5",
              "-displayfd", "7", (char *)NULL);
        _exit(127);
    }
    close(wl[1]);
    close(wm[1]);
    close(ready[1]);
    s->x.pid = pid;
    s->x.wm_fd = wm[0];
    s->x.ready_fd = ready[0];
    s->x.ready_source = wl_event_loop_add_fd(s->loop, ready[0], WL_EVENT_READABLE, on_x_ready, s);
    logf_("X: Xwayland %s started on %s, pid %d", s->x.path, s->x.display, (int)pid);
    return 0;
fail:
    if (wl[0] >= 0) close(wl[0]);
    if (wl[1] >= 0) close(wl[1]);
    if (wm[0] >= 0) close(wm[0]);
    if (wm[1] >= 0) close(wm[1]);
    if (ready[0] >= 0) close(ready[0]);
    if (ready[1] >= 0) close(ready[1]);
    return -1;
}

/* ---- xwayland_shell_v1: Xwayland's surfaces, for its client only ---- */

static void xwl_surface_set_serial(struct wl_client *client, struct wl_resource *resource,
                                   uint32_t lo, uint32_t hi)
{
    struct bc_surface *sf = wl_resource_get_user_data(resource);
    uint64_t serial = (uint64_t)hi << 32 | lo;
    (void)client;
    if (sf == NULL) return;
    if (serial == 0) {
        wl_resource_post_error(resource, XWAYLAND_SURFACE_V1_ERROR_INVALID_SERIAL, "serial 0");
        return;
    }
    if (sf->xwl_serial || sf->xwl_pending) {
        wl_resource_post_error(resource, XWAYLAND_SURFACE_V1_ERROR_ALREADY_ASSOCIATED,
                               "the surface already has a serial");
        return;
    }
    sf->xwl_pending = serial;
}

static void xwl_surface_destroyed(struct wl_resource *resource)
{
    struct bc_surface *sf = wl_resource_get_user_data(resource);
    /* The association stands (xwayland-shell); only the object goes. */
    if (sf) sf->xwl = NULL;
}

static const struct xwayland_surface_v1_interface xwl_surface_impl = {
    .set_serial = xwl_surface_set_serial,
    .destroy = bc_destroy_request,
};

static void shell_get_xwayland_surface(struct wl_client *client, struct wl_resource *resource,
                                       uint32_t id, struct wl_resource *surface_resource)
{
    struct bc_server *s = wl_resource_get_user_data(resource);
    struct bc_surface *sf = wl_resource_get_user_data(surface_resource);
    struct wl_resource *r;
    if (sf->role != BC_ROLE_NONE || sf->xdg || sf->sub) {
        wl_resource_post_error(resource, XWAYLAND_SHELL_V1_ERROR_ROLE, "the surface already has a role");
        return;
    }
    r = wl_resource_create(client, &xwayland_surface_v1_interface, wl_resource_get_version(resource), id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &xwl_surface_impl, sf, xwl_surface_destroyed);
    sf->role = BC_ROLE_XWAYLAND;
    sf->xwl = r;
    wl_list_insert(&s->x.surfaces, &sf->xwl_link);
}

static const struct xwayland_shell_v1_interface xwl_shell_impl = {
    .destroy = bc_destroy_request,
    .get_xwayland_surface = shell_get_xwayland_surface,
};

static void bind_xwayland_shell(struct wl_client *client, void *data, uint32_t version, uint32_t id)
{
    struct bc_server *s = data;
    struct wl_resource *r;
    if (client != s->x.client) {
        wl_client_post_implementation_error(client, "xwayland_shell_v1 is Xwayland's only");
        return;
    }
    r = wl_resource_create(client, &xwayland_shell_v1_interface, (int)version, id);
    if (r == NULL) {
        wl_client_post_no_memory(client);
        return;
    }
    wl_resource_set_implementation(r, &xwl_shell_impl, s, NULL);
}

/* xwayland_shell_v1 is shown only to the Xwayland the compositor started. */
static bool x_global_filter(const struct wl_client *client, const struct wl_global *global, void *data)
{
    struct bc_server *s = data;
    return global != s->x.shell || (s->x.client != NULL && client == s->x.client);
}

/* X programs on, or off with the reason said once. */
static void x_off(struct bc_server *s, const char *why)
{
    logf_("X: X programs are off: %s; the compositor serves Wayland programs only", why);
    snprintf(s->x.display, sizeof(s->x.display), "-");
}

static void x_init(struct bc_server *s, int x11)
{
    const char *want = getenv("BLIT_COMPOSITOR_XWAYLAND");
    char text[4096], why[512];
    int major = 0, minor = 0;
    s->x.number = -1;
    s->x.listen_fd[0] = s->x.listen_fd[1] = -1;
    s->x.wm_fd = s->x.ready_fd = -1;
    s->x.focus = XCB_WINDOW_NONE;
    wl_list_init(&s->x.surfaces);
    wl_list_init(&s->x.windows);
    wl_list_init(&s->x.client_gone.link);
    snprintf(s->x.display, sizeof(s->x.display), "-");
    if (!x11) {
        x_off(s, "turned off (--x11 off: the desktop's X11 Programs setting)");
        return;
    }
    if (x_find(want && *want ? want : "Xwayland", s->x.path, sizeof(s->x.path)) != 0) {
        snprintf(why, sizeof(why), "%s not found%s", want && *want ? want : "Xwayland",
                 want && *want && strchr(want, '/') ? "" : " on PATH");
        x_off(s, why);
        return;
    }
    if (x_version_text(s->x.path, text, sizeof(text)) != 0 ||
        x_version_of(text, &major, &minor) != 0) {
        snprintf(why, sizeof(why), "%s -version said no version (\"%.80s\")", s->x.path, text);
        x_off(s, why);
        return;
    }
    if (major < 23 || (major == 23 && minor < 1)) {
        snprintf(why, sizeof(why), "%s is Xwayland %d.%d; Blit needs 23.1 or later "
                 "(xwayland_shell_v1)", s->x.path, major, minor);
        x_off(s, why);
        return;
    }
    s->x.number = x_sockets(s);
    if (s->x.number < 0) {
        x_off(s, "no free X display from :0 to :32 (/tmp/.X<n>-lock, /tmp/.X11-unix)");
        return;
    }
    snprintf(s->x.display, sizeof(s->x.display), ":%d", s->x.number);
    s->x.shell = wl_global_create(s->display, &xwayland_shell_v1_interface,
                                  BC_VERSION(1, xwayland_shell_v1_interface), s, bind_xwayland_shell);
    s->x.child = wl_event_loop_add_signal(s->loop, SIGCHLD, on_x_child, s);
    s->x.restart = wl_event_loop_add_timer(s->loop, on_x_restart, s);
    if (s->x.shell == NULL || s->x.child == NULL || s->x.restart == NULL) {
        x_sockets_close(s);
        x_off(s, "could not set up for Xwayland");
        return;
    }
    wl_display_set_global_filter(s->display, x_global_filter, s);
    s->x.on = 1;
    logf_("X: Xwayland %d.%d (%s) for X programs on DISPLAY=%s", major, minor, s->x.path, s->x.display);
    if (x_launch(s) != 0) wl_event_source_timer_update(s->x.restart, 2000);
}

/* The compositor's end: Xwayland ended, the display given back. */
static void x_finish(struct bc_server *s)
{
    int k;
    s->x.stopping = 1;
    x_wm_stop(s);
    if (s->x.ready_source) wl_event_source_remove(s->x.ready_source);
    s->x.ready_source = NULL;
    if (s->x.ready_fd >= 0) close(s->x.ready_fd);
    s->x.ready_fd = -1;
    if (s->x.client) wl_client_destroy(s->x.client);
    if (s->x.pid > 0) {
        int status;
        kill(s->x.pid, SIGTERM);
        for (k = 0; k < 100 && waitpid(s->x.pid, &status, WNOHANG) == 0; ++k) {
            struct timespec ts = { 0, 10000000 };
            nanosleep(&ts, NULL);
        }
        if (k == 100) {
            kill(s->x.pid, SIGKILL);
            waitpid(s->x.pid, &status, 0);
        }
        s->x.pid = 0;
    }
    if (s->x.restart) wl_event_source_remove(s->x.restart);
    if (s->x.child) wl_event_source_remove(s->x.child);
    s->x.restart = s->x.child = NULL;
    x_sockets_close(s);
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
        if (!window_is_toplevel(x)) {
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
        window_configure(x);
        return;
    case BC_LINE_OUTPUT:
        x = window_by_id(s, l.id);
        if (!window_is_toplevel(x)) {
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
        if (window_is_toplevel(x)) {
            logf_("window %u: close", l.id);
            window_close(x);
        } else if (window_is_popup(x)) {
            popup_close(x);
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
    bc_wire_line(w, "WINDOW display %s %s", s->socket_name, s->x.display);
    wl_list_for_each(x, &s->windows, link) x->replay = 0;
    /* Toplevels of either kind in creation order (the list is newest first). */
    wl_list_for_each_reverse(x, &s->windows, link) {
        if (!window_is_toplevel(x)) continue;
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
        if (!window_is_popup(x) || p == NULL || p->id == 0) continue;
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
        if (window_is_toplevel(x) && x->id) windows += subs_show(x, NULL, 0);
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
    fprintf(out, "usage: blit-compositor [--display name] [--log path] [--x11 on|off]\n"
                 "  Blit's Wayland compositor for programs (ADR 0228). Needs BLIT_FRAME_SOCKET,\n"
                 "  BLIT_FRAME_TOKEN and XDG_RUNTIME_DIR; listens on $XDG_RUNTIME_DIR/<name>,\n"
                 "  --display's (refused, status 2, when in use or invalid), else blit-<n>.\n"
                 "  With Xwayland 23.1 or later on PATH it runs it for X programs and is its\n"
                 "  window manager; the X display goes to the desktop with the Wayland one.\n"
                 "  --x11 off runs no Xwayland: Wayland programs only (default on).\n");
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
    int i, x11 = 1;

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
        } else if (strcmp(argv[i], "--x11") == 0 && i + 1 < argc &&
                   (strcmp(argv[i + 1], "on") == 0 || strcmp(argv[i + 1], "off") == 0)) {
            x11 = strcmp(argv[++i], "on") == 0;
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
                          bind_shape_manager) ||
        !wl_global_create(server.display, &org_kde_kwin_appmenu_manager_interface,
                          BC_VERSION(1, org_kde_kwin_appmenu_manager_interface), &server,
                          bind_appmenu_manager)) {
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
          "wp_cursor_shape_manager_v1 %d, org_kde_kwin_appmenu_manager %d",
          BC_VERSION(6, wl_compositor_interface), BC_VERSION(1, wl_subcompositor_interface),
          BC_VERSION(4, wl_output_interface), BC_VERSION(8, wl_seat_interface),
          BC_VERSION(6, xdg_wm_base_interface), BC_VERSION(1, zxdg_decoration_manager_v1_interface),
          BC_VERSION(1, wp_presentation_interface),
          BC_VERSION(1, zwp_relative_pointer_manager_v1_interface),
          BC_VERSION(1, zwp_pointer_constraints_v1_interface),
          BC_VERSION(3, wl_data_device_manager_interface),
          BC_VERSION(1, zwp_primary_selection_device_manager_v1_interface),
          BC_VERSION(2, wp_cursor_shape_manager_v1_interface),
          BC_VERSION(1, org_kde_kwin_appmenu_manager_interface));
    server.frame_source = wl_event_loop_add_fd(server.loop, server.wire.fd, WL_EVENT_READABLE,
                                               on_frame_socket, &server);
    server.sigterm_source = wl_event_loop_add_signal(server.loop, SIGTERM, on_signal, &server);
    server.sigint_source = wl_event_loop_add_signal(server.loop, SIGINT, on_signal, &server);
    server.redial = wl_event_loop_add_timer(server.loop, on_redial, &server);
    logf_("listening on %s/%s", runtime, server.socket_name);
    /* X programs, when Xwayland is here (the X section): the display is
       said with the Wayland one. */
    x_init(&server, x11);
    bc_wire_line(&server.wire, "WINDOW display %s %s", server.socket_name, server.x.display);

    wl_display_run(server.display);

    while (!wl_list_empty(&server.input_conns)) {
        struct bc_input_conn *conn = wl_container_of(server.input_conns.next, conn, link);
        wl_event_source_remove(conn->source);
        close(conn->fd);
        wl_list_remove(&conn->link);
        free(conn);
    }
    /* Xwayland first: its windows go and it ends (the X section). */
    x_finish(&server);
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
