/* The stand-in X client of tools/tests/xwayland_gate.py (ADR 0228 phase 2,
   milestones A and C): one X program, on xcb (and xcb-xfixes, to name its
   cursor), that does what the gate checks of X windows in Hibr windows.

     x_standin_client [title]

   On $DISPLAY it creates one 320x200 top-level, names it (WM_NAME "X
   stand-in", _NET_WM_NAME the title, default "Blit X stand-in", WM_CLASS
   "xstandin" "blit.xstandin"), takes WM_DELETE_WINDOW (WM_PROTOCOLS) and
   maps it. At every Expose it draws the window whole in the pattern of the
   Wayland stand-in (wl_standin_client.c), so the gate compares the frames
   of both the same way: at (x, y) red x & 255, green y & 255, blue 0x80 |
   (x >> 8 & 7) << 3 | (y >> 8 & 7), with PutImage in strips under the core
   request size. A press of the space bar (keycode 65) draws it again 150
   times, 16 ms apart, for the compositor's measure of its first 60 frames.

   Keys (X keycodes: evdev + 8) that each do one thing an X program does:

     D  toggle _MOTIF_WM_HINTS "no decorations" (as Steam sets it)
     M  open a menu: an override-redirect 120x90 window at 30,40 from the
        top-level, which grabs the pointer and the keyboard (as X menus
        do). A press in it is a choice, a press anywhere else (reported
        through the grab) dismisses it, as does Escape or the top-level
        losing X's focus (FocusOut, as Chromium closes its menus when its
        window is deactivated); either way it is destroyed and ungrabbed.
     N  the same menu, 100x60 at 450,320 (inside the dialog, past the
        top-level), with WM_TRANSIENT_FOR naming the top-level
     O  the same menu at 330,40, reaching past the top-level's right edge;
        S, while a menu is open, a submenu at 90,10 from it (80x50, no
        grab), which goes with the menu
     T  open a dialog: a 200x100 top-level at 300,250 from the main one
        (overlapping it and past it), WM_TRANSIENT_FOR the main one and
        WM_NORMAL_HINTS minimum 150x80, maximum 300x200; its
        WM_DELETE_WINDOW destroys it
     P  open a panel: a 200x100 top-level asking for 1800,1000 (past the
        root's edge), maximized from the start (_NET_WM_STATE before its
        map); J then sets its WM_TRANSIENT_FOR to the main one
     I  WM_CHANGE_STATE IconicState (the program minimizing its window)
     L  WM_NORMAL_HINTS on the main window: a base size of 200x150 and no
        minimum (the minimum is the base size, ICCCM)
     F  _NET_WM_STATE toggle full screen (a client message to the root)
     X  _NET_WM_STATE toggle maximized, vertical and horizontal
     G  the next press sends _NET_WM_MOVERESIZE move with its button
     H  the next press sends _NET_WM_MOVERESIZE size bottom-right
     R  _NET_WM_MOVERESIZE move now, with no button down
     C  the window's cursor: a glyph of the core cursor font, named "xterm"
        (XFixesSetCursorName, as libXcursor names the themed ones); U the
        same named "nosuchcursor", a name Blit has no pointer for

   It prints one line per event, flushed:

     XSTANDIN mapped
     XSTANDIN expose                     the last of an Expose series: it draws
     XSTANDIN configure <w>x<h>          every ConfigureNotify, synthetic too
     XSTANDIN drew <w>x<h>
     XSTANDIN focus-in
     XSTANDIN focus-out
     XSTANDIN key <keycode>              a KeyPress
     XSTANDIN undecorated|decorated      the D key
     XSTANDIN button <n> <x>,<y>         a ButtonPress, in the window
     XSTANDIN animated <n>
     XSTANDIN menu <x>,<y> <w>x<h> grab <pointer> <keyboard>
                                         a menu mapped at x, y from the
                                         top-level, its grabs' statuses (0
                                         success)
     XSTANDIN submenu
     XSTANDIN menu-choice <x>,<y>        a press in the menu
     XSTANDIN menu-key <keycode>         a key the menu's grab got
     XSTANDIN menu-moved <x>,<y>         the V key in the menu moved it there
     XSTANDIN menu-dismissed <why>       a press outside, escape, focus-out
     XSTANDIN dialog                     the dialog mapped
     XSTANDIN dialog-button <n> <x>,<y>  a ButtonPress in the dialog
     XSTANDIN panel | panel-button <n> <x>,<y> | panel-transient | panel-delete
     XSTANDIN iconify
     XSTANDIN dialog-delete              its WM_DELETE_WINDOW
     XSTANDIN limits                     the L key
     XSTANDIN state <atoms>              _NET_WM_STATE as the window manager
                                         wrote it: fullscreen, maximized-vert,
                                         maximized-horz, focused, or none
     XSTANDIN moveresize <direction> <button>   sent
     XSTANDIN cursor <name>
     XSTANDIN delete                     WM_DELETE_WINDOW: it exits 0

   and exits 1 when its connection fails or ends otherwise.

   Built by tools/tests/xwayland_gate.sh with tcc against libxcb and
   xcb-xfixes (pkg-config, tools/build-flags.sh xcb). */
#define _GNU_SOURCE
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>
#include <xcb/xfixes.h>

#define ANIMATE_FRAMES 150
#define ANIMATE_MS 16
#define KEYCODE_ESCAPE 9
#define KEYCODE_SPACE 65
#define KEYCODE_D 40
#define KEYCODE_M 58
#define KEYCODE_N 57
#define KEYCODE_T 28
#define KEYCODE_L 46
#define KEYCODE_F 41
#define KEYCODE_X 53
#define KEYCODE_G 42
#define KEYCODE_H 43
#define KEYCODE_R 27
#define KEYCODE_C 54
#define KEYCODE_U 30
#define KEYCODE_O 32
#define KEYCODE_S 39
#define KEYCODE_I 31
#define KEYCODE_P 33
#define KEYCODE_J 44
#define KEYCODE_V 55
/* _NET_WM_MOVERESIZE's directions (EWMH) */
#define MOVERESIZE_SIZE_BOTTOMRIGHT 4
#define MOVERESIZE_MOVE 8
/* _NET_WM_STATE's actions */
#define STATE_TOGGLE 2
/* XC_xterm of the core cursor font */
#define GLYPH_XTERM 152

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window, menu, submenu, dialog, panel;
static xcb_gcontext_t gc, fill;
static uint16_t width = 320, height = 200;
static uint16_t menu_w, menu_h;
static int16_t menu_dx, menu_dy;  /* the menu's place from the window, in X space */
static xcb_atom_t change_state, protocols, delete_window, utf8, net_wm_name, motif_hints, transient_atom, net_wm_state,
    state_fullscreen, state_max_vert, state_max_horz, state_focused, moveresize;

static void say(const char *fmt, ...)
{
    va_list ap;
    fputs("XSTANDIN ", stdout);
    va_start(ap, fmt);
    vprintf(fmt, ap);
    va_end(ap);
    fputc('\n', stdout);
    fflush(stdout);
}

static xcb_atom_t atom(const char *name)
{
    xcb_intern_atom_reply_t *reply =
        xcb_intern_atom_reply(conn, xcb_intern_atom(conn, 0, (uint16_t)strlen(name), name), NULL);
    xcb_atom_t a = reply ? reply->atom : XCB_ATOM_NONE;
    free(reply);
    return a;
}

static uint32_t pattern(uint32_t x, uint32_t y)
{
    uint32_t r = x & 255, g = y & 255, b = 0x80 | ((x >> 8) & 7) << 3 | ((y >> 8) & 7);
    return r << 16 | g << 8 | b;
}

/* The window whole, in strips of rows each under the core request limit
   (BIG-REQUESTS is not needed). The 32-bit ZPixmap of a depth 24 window
   on a little-endian server: each pixel the host's uint32_t. */
static void draw(void)
{
    uint32_t rows = (uint32_t)(60000 / ((uint32_t)width * 4u)), y0, x, y;
    uint32_t *strip;
    if (rows == 0) rows = 1;
    strip = malloc((size_t)width * rows * 4u);
    if (strip == NULL) exit(1);
    for (y0 = 0; y0 < height; y0 += rows) {
        uint32_t n = height - y0 < rows ? height - y0 : rows;
        for (y = 0; y < n; ++y)
            for (x = 0; x < width; ++x) strip[y * width + x] = pattern(x, y0 + y);
        xcb_put_image(conn, XCB_IMAGE_FORMAT_Z_PIXMAP, window, gc, width, (uint16_t)n, 0, (int16_t)y0,
                      0, 24, width * n * 4u, (const uint8_t *)strip);
    }
    free(strip);
    xcb_flush(conn);
    say("drew %ux%u", width, height);
}

/* A window filled with one colour. */
static void draw_fill(xcb_window_t w, uint16_t fw, uint16_t fh)
{
    xcb_rectangle_t r = { 0, 0, fw, fh };
    xcb_poly_fill_rectangle(conn, w, fill, 1, &r);
    xcb_flush(conn);
}

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

/* The main window's root position. */
static void origin(int16_t *x, int16_t *y)
{
    xcb_translate_coordinates_reply_t *t =
        xcb_translate_coordinates_reply(conn, xcb_translate_coordinates(conn, window, screen->root, 0, 0), NULL);
    *x = t ? t->dst_x : 0;
    *y = t ? t->dst_y : 0;
    free(t);
}

/* A menu as X toolkits make one: override-redirect, mapped, then the
   pointer and the keyboard grabbed (owner_events false: every press and
   key reaches the menu, in its coordinates). */
static void menu_open(int16_t dx, int16_t dy, uint16_t mw, uint16_t mh, int transient)
{
    uint32_t values[3];
    int16_t ox, oy;
    xcb_grab_pointer_reply_t *gp;
    xcb_grab_keyboard_reply_t *gk;
    if (menu) return;
    origin(&ox, &oy);
    menu = xcb_generate_id(conn);
    menu_w = mw;
    menu_h = mh;
    menu_dx = dx;
    menu_dy = dy;
    values[0] = screen->white_pixel;
    values[1] = 1;
    values[2] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_KEY_PRESS;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, menu, screen->root, (int16_t)(ox + dx), (int16_t)(oy + dy),
                      mw, mh, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT | XCB_CW_EVENT_MASK, values);
    if (transient)
        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, menu, transient_atom, XCB_ATOM_WINDOW, 32, 1, &window);
    xcb_map_window(conn, menu);
    gp = xcb_grab_pointer_reply(conn, xcb_grab_pointer(conn, 0, menu, XCB_EVENT_MASK_BUTTON_PRESS,
                                                        XCB_GRAB_MODE_ASYNC, XCB_GRAB_MODE_ASYNC, XCB_WINDOW_NONE,
                                                        XCB_CURSOR_NONE, XCB_CURRENT_TIME), NULL);
    gk = xcb_grab_keyboard_reply(conn, xcb_grab_keyboard(conn, 0, menu, XCB_CURRENT_TIME, XCB_GRAB_MODE_ASYNC,
                                                          XCB_GRAB_MODE_ASYNC), NULL);
    say("menu %d,%d %ux%u grab %d %d", dx, dy, mw, mh, gp ? gp->status : -1, gk ? gk->status : -1);
    free(gp);
    free(gk);
}

/* A submenu of the open menu: override-redirect at 90,10 from the menu,
   past its window's edge when the menu is near it, no grab of its own. */
static void submenu_open(void)
{
    uint32_t values[2];
    xcb_translate_coordinates_reply_t *t;
    if (!menu || submenu) return;
    t = xcb_translate_coordinates_reply(conn, xcb_translate_coordinates(conn, menu, screen->root, 0, 0), NULL);
    submenu = xcb_generate_id(conn);
    values[0] = screen->white_pixel;
    values[1] = 1;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, submenu, screen->root, (int16_t)((t ? t->dst_x : 0) + 90),
                      (int16_t)((t ? t->dst_y : 0) + 10), 80, 50, 0, XCB_WINDOW_CLASS_INPUT_OUTPUT,
                      screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_OVERRIDE_REDIRECT, values);
    free(t);
    xcb_map_window(conn, submenu);
    xcb_flush(conn);
    say("submenu");
}

static void menu_close(void)
{
    if (!menu) return;
    if (submenu) xcb_destroy_window(conn, submenu);
    submenu = 0;
    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
    xcb_ungrab_keyboard(conn, XCB_CURRENT_TIME);
    xcb_destroy_window(conn, menu);
    xcb_flush(conn);
    menu = 0;
}

/* WM_NORMAL_HINTS (ICCCM 4.1.2.3): flags, then 17 values. */
static void normal_hints(xcb_window_t w, uint32_t flags, int32_t min_w, int32_t min_h, int32_t max_w,
                         int32_t max_h, int32_t base_w, int32_t base_h)
{
    int32_t v[18];
    memset(v, 0, sizeof(v));
    v[0] = (int32_t)flags;
    v[5] = min_w; v[6] = min_h;
    v[7] = max_w; v[8] = max_h;
    v[15] = base_w; v[16] = base_h;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, w, XCB_ATOM_WM_NORMAL_HINTS, XCB_ATOM_WM_SIZE_HINTS, 32, 18, v);
}

static void dialog_open(void)
{
    static const char name[] = "X stand-in dialog";
    static const char wm_class[] = "xstandin\0blit.xstandin";
    uint32_t values[2];
    int16_t ox, oy;
    if (dialog) return;
    origin(&ox, &oy);
    dialog = xcb_generate_id(conn);
    values[0] = screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, dialog, screen->root, (int16_t)(ox + 300), (int16_t)(oy + 250),
                      200, 100, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, dialog, net_wm_name, utf8, 8, (uint32_t)strlen(name), name);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, dialog, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                        sizeof(wm_class), wm_class);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, dialog, protocols, XCB_ATOM_ATOM, 32, 1, &delete_window);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, dialog, transient_atom, XCB_ATOM_WINDOW, 32, 1, &window);
    normal_hints(dialog, 16 | 32, 150, 80, 300, 200, 0, 0);  /* PMinSize | PMaxSize */
    xcb_map_window(conn, dialog);
    xcb_flush(conn);
}

/* A second plain top-level, asking to be at 1800,1000 -- past the root's
   edge (1920x1080 until an output says more) -- and maximized from the
   start (_NET_WM_STATE before its map); its WM_DELETE_WINDOW destroys it. */
static void panel_open(void)
{
    static const char name[] = "X stand-in panel";
    static const char wm_class[] = "xstandin\0blit.xstandin";
    uint32_t values[2];
    xcb_atom_t maximized[2];
    if (panel) return;
    panel = xcb_generate_id(conn);
    values[0] = screen->white_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_BUTTON_PRESS;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, panel, screen->root, 1800, 1000, 200, 100, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual, XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK,
                      values);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, panel, net_wm_name, utf8, 8, (uint32_t)strlen(name), name);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, panel, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                        sizeof(wm_class), wm_class);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, panel, protocols, XCB_ATOM_ATOM, 32, 1, &delete_window);
    maximized[0] = state_max_vert;
    maximized[1] = state_max_horz;
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, panel, net_wm_state, XCB_ATOM_ATOM, 32, 2, maximized);
    xcb_map_window(conn, panel);
    xcb_flush(conn);
}

/* An EWMH request: a client message about the main window, to the root. */
static void ewmh(xcb_atom_t type, uint32_t d0, uint32_t d1, uint32_t d2, uint32_t d3, uint32_t d4)
{
    xcb_client_message_event_t ev;
    memset(&ev, 0, sizeof(ev));
    ev.response_type = XCB_CLIENT_MESSAGE;
    ev.format = 32;
    ev.window = window;
    ev.type = type;
    ev.data.data32[0] = d0;
    ev.data.data32[1] = d1;
    ev.data.data32[2] = d2;
    ev.data.data32[3] = d3;
    ev.data.data32[4] = d4;
    xcb_send_event(conn, 0, screen->root,
                   XCB_EVENT_MASK_SUBSTRUCTURE_REDIRECT | XCB_EVENT_MASK_SUBSTRUCTURE_NOTIFY, (const char *)&ev);
    xcb_flush(conn);
}

static void moveresize_send(int16_t root_x, int16_t root_y, uint32_t direction, uint32_t button)
{
    ewmh(moveresize, (uint32_t)root_x, (uint32_t)root_y, direction, button, 1);
    say("moveresize %u %u", direction, button);
}

/* _NET_WM_STATE as the window manager wrote it. */
static void state_say(void)
{
    xcb_get_property_reply_t *r = xcb_get_property_reply(
        conn, xcb_get_property(conn, 0, window, net_wm_state, XCB_ATOM_ATOM, 0, 32), NULL);
    char text[128] = "";
    int i, n = r && r->format == 32 ? xcb_get_property_value_length(r) / 4 : 0;
    const xcb_atom_t *v = r ? xcb_get_property_value(r) : NULL;
    for (i = 0; i < n; ++i) {
        const char *name = v[i] == state_fullscreen ? "fullscreen" : v[i] == state_max_vert ? "maximized-vert"
                         : v[i] == state_max_horz ? "maximized-horz" : v[i] == state_focused ? "focused" : "other";
        strncat(text, " ", sizeof(text) - strlen(text) - 1);
        strncat(text, name, sizeof(text) - strlen(text) - 1);
    }
    say("state%s", n ? text : " none");
    free(r);
}

static void cursor_named(const char *name)
{
    xcb_font_t font = xcb_generate_id(conn);
    xcb_cursor_t cursor = xcb_generate_id(conn);
    xcb_xfixes_query_version_reply_t *v =
        xcb_xfixes_query_version_reply(conn, xcb_xfixes_query_version(conn, 4, 0), NULL);
    free(v);
    xcb_open_font(conn, font, 6, "cursor");
    xcb_create_glyph_cursor(conn, cursor, font, font, GLYPH_XTERM, GLYPH_XTERM + 1, 0, 0, 0, 0xffff, 0xffff,
                            0xffff);
    xcb_close_font(conn, font);
    xcb_xfixes_set_cursor_name(conn, cursor, (uint16_t)strlen(name), name);
    xcb_change_window_attributes(conn, window, XCB_CW_CURSOR, &cursor);
    xcb_free_cursor(conn, cursor);
    xcb_flush(conn);
    say("cursor %s", name);
}

int main(int argc, char **argv)
{
    const char *title = argc > 1 ? argv[1] : "Blit X stand-in";
    static const char wm_name[] = "X stand-in";
    static const char wm_class[] = "xstandin\0blit.xstandin";
    const xcb_setup_t *setup;
    xcb_format_iterator_t f;
    uint32_t values[2];
    int animate = 0, animated = 0, bpp = 0, undecorated = 0;
    uint32_t press_sends = 0;  /* a _NET_WM_MOVERESIZE direction + 1 for the next press, 0 none */
    uint64_t next_frame = 0;

    conn = xcb_connect(NULL, NULL);
    if (xcb_connection_has_error(conn)) {
        fprintf(stderr, "x_standin_client: cannot open display %s\n", getenv("DISPLAY") ? getenv("DISPLAY") : "");
        return 1;
    }
    setup = xcb_get_setup(conn);
    screen = xcb_setup_roots_iterator(setup).data;
    for (f = xcb_setup_pixmap_formats_iterator(setup); f.rem; xcb_format_next(&f))
        if (f.data->depth == 24) bpp = f.data->bits_per_pixel;
    if (screen->root_depth != 24 || bpp != 32 || setup->image_byte_order != XCB_IMAGE_ORDER_LSB_FIRST) {
        fprintf(stderr, "x_standin_client: needs a depth 24, 32 bpp, LSB-first screen (depth %u bpp %d)\n",
                screen->root_depth, bpp);
        return 1;
    }
    protocols = atom("WM_PROTOCOLS");
    delete_window = atom("WM_DELETE_WINDOW");
    utf8 = atom("UTF8_STRING");
    net_wm_name = atom("_NET_WM_NAME");
    motif_hints = atom("_MOTIF_WM_HINTS");
    transient_atom = XCB_ATOM_WM_TRANSIENT_FOR;
    net_wm_state = atom("_NET_WM_STATE");
    state_fullscreen = atom("_NET_WM_STATE_FULLSCREEN");
    state_max_vert = atom("_NET_WM_STATE_MAXIMIZED_VERT");
    state_max_horz = atom("_NET_WM_STATE_MAXIMIZED_HORZ");
    state_focused = atom("_NET_WM_STATE_FOCUSED");
    moveresize = atom("_NET_WM_MOVERESIZE");
    change_state = atom("WM_CHANGE_STATE");

    window = xcb_generate_id(conn);
    values[0] = screen->black_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE | XCB_EVENT_MASK_PROPERTY_CHANGE;
    xcb_create_window(conn, XCB_COPY_FROM_PARENT, window, screen->root, 0, 0, width, height, 0,
                      XCB_WINDOW_CLASS_INPUT_OUTPUT, screen->root_visual,
                      XCB_CW_BACK_PIXEL | XCB_CW_EVENT_MASK, values);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_NAME, XCB_ATOM_STRING, 8,
                        (uint32_t)strlen(wm_name), wm_name);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, net_wm_name, utf8, 8, (uint32_t)strlen(title),
                        title);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, XCB_ATOM_WM_CLASS, XCB_ATOM_STRING, 8,
                        sizeof(wm_class), wm_class);
    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, protocols, XCB_ATOM_ATOM, 32, 1, &delete_window);
    gc = xcb_generate_id(conn);
    xcb_create_gc(conn, gc, window, 0, NULL);
    fill = xcb_generate_id(conn);
    values[0] = 0x336699;
    xcb_create_gc(conn, fill, window, XCB_GC_FOREGROUND, values);
    xcb_map_window(conn, window);
    xcb_flush(conn);

    for (;;) {
        xcb_generic_event_t *ev;
        struct pollfd p;
        int wait = -1;
        if (animate > 0) {
            uint64_t now = now_ms();
            if (now >= next_frame) {
                draw();
                --animate;
                ++animated;
                next_frame = now + ANIMATE_MS;
                if (animate == 0) say("animated %d", animated);
            }
        }
        while ((ev = xcb_poll_for_event(conn)) != NULL) {
            switch (ev->response_type & 0x7f) {
            case XCB_EXPOSE: {
                xcb_expose_event_t *e = (xcb_expose_event_t *)ev;
                if (e->count != 0) break;
                if (e->window == window) {
                    say("expose");
                    draw();
                } else if (menu && e->window == menu) {
                    draw_fill(menu, menu_w, menu_h);
                } else if (dialog && e->window == dialog) {
                    draw_fill(dialog, 200, 100);
                } else if (panel && e->window == panel) {
                    draw_fill(panel, 200, 100);
                } else if (submenu && e->window == submenu) {
                    draw_fill(submenu, 80, 50);
                }
                break;
            }
            case XCB_MAP_NOTIFY: {
                xcb_map_notify_event_t *m = (xcb_map_notify_event_t *)ev;
                if (m->window == window) say("mapped");
                else if (dialog && m->window == dialog) say("dialog");
                else if (panel && m->window == panel) say("panel");
                break;
            }
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *c = (xcb_configure_notify_event_t *)ev;
                if (c->window != window) break;
                width = c->width;
                height = c->height;
                say("configure %ux%u", c->width, c->height);
                break;
            }
            case XCB_FOCUS_IN:
                if (((xcb_focus_in_event_t *)ev)->detail != XCB_NOTIFY_DETAIL_POINTER) say("focus-in");
                break;
            case XCB_FOCUS_OUT: {
                xcb_focus_out_event_t *o = (xcb_focus_out_event_t *)ev;
                if (o->detail == XCB_NOTIFY_DETAIL_POINTER || o->event != window) break;
                if (o->mode != XCB_NOTIFY_MODE_GRAB && o->mode != XCB_NOTIFY_MODE_UNGRAB) {
                    say("focus-out");
                    if (menu) {
                        menu_close();
                        say("menu-dismissed focus-out");
                    }
                }
                break;
            }
            case XCB_PROPERTY_NOTIFY: {
                xcb_property_notify_event_t *pn = (xcb_property_notify_event_t *)ev;
                if (pn->window == window && pn->atom == net_wm_state) state_say();
                break;
            }
            case XCB_KEY_PRESS: {
                xcb_key_press_event_t *k = (xcb_key_press_event_t *)ev;
                if (menu && k->event == menu) {
                    say("menu-key %u", k->detail);
                    if (k->detail == KEYCODE_S) submenu_open();
                    if (k->detail == KEYCODE_V) {
                        /* The program moves its open menu, 20 right and
                           10 down, as a toolkit repositions one. */
                        int16_t ox, oy;
                        uint32_t at[2];
                        origin(&ox, &oy);
                        menu_dx += 20;
                        menu_dy += 10;
                        at[0] = (uint32_t)(int32_t)(ox + menu_dx);
                        at[1] = (uint32_t)(int32_t)(oy + menu_dy);
                        xcb_configure_window(conn, menu, XCB_CONFIG_WINDOW_X | XCB_CONFIG_WINDOW_Y, at);
                        xcb_flush(conn);
                        say("menu-moved %d,%d", menu_dx, menu_dy);
                    }
                    if (k->detail == KEYCODE_ESCAPE) {
                        menu_close();
                        say("menu-dismissed escape");
                    }
                    break;
                }
                say("key %u", k->detail);
                if (k->detail == KEYCODE_D) {
                    /* flags (MWM_HINTS_DECORATIONS, 2), functions,
                       decorations (none), input mode, status */
                    static const uint32_t none[5] = { 2, 0, 0, 0, 0 };
                    undecorated = !undecorated;
                    if (undecorated)
                        xcb_change_property(conn, XCB_PROP_MODE_REPLACE, window, motif_hints, motif_hints,
                                            32, 5, none);
                    else
                        xcb_delete_property(conn, window, motif_hints);
                    xcb_flush(conn);
                    say(undecorated ? "undecorated" : "decorated");
                } else if (k->detail == KEYCODE_M) {
                    menu_open(30, 40, 120, 90, 0);
                } else if (k->detail == KEYCODE_N) {
                    menu_open(450, 320, 100, 60, 1);
                } else if (k->detail == KEYCODE_O) {
                    menu_open(330, 40, 120, 90, 0);
                } else if (k->detail == KEYCODE_I) {
                    ewmh(change_state, 3, 0, 0, 0, 0);  /* IconicState */
                    say("iconify");
                } else if (k->detail == KEYCODE_P) {
                    panel_open();
                } else if (k->detail == KEYCODE_J && panel) {
                    xcb_change_property(conn, XCB_PROP_MODE_REPLACE, panel, transient_atom, XCB_ATOM_WINDOW, 32, 1,
                                        &window);
                    xcb_flush(conn);
                    say("panel-transient");
                } else if (k->detail == KEYCODE_U) {
                    cursor_named("nosuchcursor");
                } else if (k->detail == KEYCODE_T) {
                    dialog_open();
                } else if (k->detail == KEYCODE_L) {
                    normal_hints(window, 256, 0, 0, 0, 0, 200, 150);  /* PBaseSize */
                    xcb_flush(conn);
                    say("limits");
                } else if (k->detail == KEYCODE_F) {
                    ewmh(net_wm_state, STATE_TOGGLE, state_fullscreen, 0, 1, 0);
                } else if (k->detail == KEYCODE_X) {
                    ewmh(net_wm_state, STATE_TOGGLE, state_max_vert, state_max_horz, 1, 0);
                } else if (k->detail == KEYCODE_G) {
                    press_sends = MOVERESIZE_MOVE + 1;
                } else if (k->detail == KEYCODE_H) {
                    press_sends = MOVERESIZE_SIZE_BOTTOMRIGHT + 1;
                } else if (k->detail == KEYCODE_R) {
                    moveresize_send(0, 0, MOVERESIZE_MOVE, 1);
                } else if (k->detail == KEYCODE_C) {
                    cursor_named("xterm");
                } else if (k->detail == KEYCODE_SPACE && animate == 0) {
                    animate = ANIMATE_FRAMES;
                    animated = 0;
                    next_frame = 0;
                }
                break;
            }
            case XCB_BUTTON_PRESS: {
                xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
                if (menu && b->event == menu) {
                    if (b->event_x >= 0 && b->event_y >= 0 && b->event_x < menu_w && b->event_y < menu_h) {
                        say("menu-choice %d,%d", b->event_x, b->event_y);
                    } else {
                        say("menu-dismissed press %d,%d", b->event_x, b->event_y);
                    }
                    menu_close();
                    break;
                }
                if (dialog && b->event == dialog) {
                    say("dialog-button %u %d,%d", b->detail, b->event_x, b->event_y);
                    break;
                }
                if (panel && b->event == panel) {
                    say("panel-button %u %d,%d", b->detail, b->event_x, b->event_y);
                    break;
                }
                say("button %u %d,%d", b->detail, b->event_x, b->event_y);
                if (press_sends) {
                    /* As a toolkit does before it asks: its own implicit
                       grab let go (EWMH _NET_WM_MOVERESIZE). */
                    xcb_ungrab_pointer(conn, XCB_CURRENT_TIME);
                    moveresize_send(b->root_x, b->root_y, press_sends - 1, b->detail);
                    press_sends = 0;
                }
                break;
            }
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *m = (xcb_client_message_event_t *)ev;
                if (m->type == protocols && m->format == 32 && m->data.data32[0] == delete_window) {
                    if (dialog && m->window == dialog) {
                        say("dialog-delete");
                        xcb_destroy_window(conn, dialog);
                        xcb_flush(conn);
                        dialog = 0;
                        break;
                    }
                    if (panel && m->window == panel) {
                        say("panel-delete");
                        xcb_destroy_window(conn, panel);
                        xcb_flush(conn);
                        panel = 0;
                        break;
                    }
                    say("delete");
                    free(ev);
                    xcb_destroy_window(conn, window);
                    xcb_disconnect(conn);
                    return 0;
                }
                break;
            }
            default:
                break;
            }
            free(ev);
        }
        if (xcb_connection_has_error(conn)) {
            fprintf(stderr, "x_standin_client: the connection to the X server ended\n");
            return 1;
        }
        if (animate > 0) {
            uint64_t now = now_ms();
            wait = next_frame > now ? (int)(next_frame - now) : 0;
        }
        p.fd = xcb_get_file_descriptor(conn);
        p.events = POLLIN;
        poll(&p, 1, wait);
    }
}
