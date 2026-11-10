/* The stand-in X client of tools/tests/xwayland_gate.py (ADR 0228 phase 2,
   milestone A): one X program, on xcb alone, that does what the gate checks
   of an X window in a Hibr window.

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

   It prints one line per event, flushed:

     XSTANDIN mapped
     XSTANDIN configure <w>x<h>          every ConfigureNotify, synthetic too
     XSTANDIN drew <w>x<h>
     XSTANDIN focus-in
     XSTANDIN key <keycode>              a KeyPress
     XSTANDIN button <n> <x>,<y>         a ButtonPress, in the window
     XSTANDIN animated <n>
     XSTANDIN delete                     WM_DELETE_WINDOW: it exits 0

   and exits 1 when its connection fails or ends otherwise.

   Built by tools/tests/xwayland_gate.sh with tcc against libxcb (pkg-config
   xcb, tools/build-flags.sh). */
#define _GNU_SOURCE
#include <poll.h>
#include <stdarg.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#include <xcb/xcb.h>

#define ANIMATE_FRAMES 150
#define ANIMATE_MS 16
#define KEYCODE_SPACE 65

static xcb_connection_t *conn;
static xcb_screen_t *screen;
static xcb_window_t window;
static xcb_gcontext_t gc;
static uint16_t width = 320, height = 200;

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

static uint64_t now_ms(void)
{
    struct timespec ts;
    clock_gettime(CLOCK_MONOTONIC, &ts);
    return (uint64_t)ts.tv_sec * 1000u + (uint64_t)ts.tv_nsec / 1000000u;
}

int main(int argc, char **argv)
{
    const char *title = argc > 1 ? argv[1] : "Blit X stand-in";
    static const char wm_name[] = "X stand-in";
    static const char wm_class[] = "xstandin\0blit.xstandin";
    const xcb_setup_t *setup;
    xcb_format_iterator_t f;
    xcb_atom_t protocols, delete_window, utf8, net_wm_name;
    uint32_t values[2];
    int animate = 0, animated = 0, bpp = 0;
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

    window = xcb_generate_id(conn);
    values[0] = screen->black_pixel;
    values[1] = XCB_EVENT_MASK_EXPOSURE | XCB_EVENT_MASK_STRUCTURE_NOTIFY | XCB_EVENT_MASK_KEY_PRESS |
                XCB_EVENT_MASK_BUTTON_PRESS | XCB_EVENT_MASK_FOCUS_CHANGE;
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
            case XCB_EXPOSE:
                if (((xcb_expose_event_t *)ev)->count == 0) draw();
                break;
            case XCB_MAP_NOTIFY:
                say("mapped");
                break;
            case XCB_CONFIGURE_NOTIFY: {
                xcb_configure_notify_event_t *c = (xcb_configure_notify_event_t *)ev;
                width = c->width;
                height = c->height;
                say("configure %ux%u", c->width, c->height);
                break;
            }
            case XCB_FOCUS_IN:
                if (((xcb_focus_in_event_t *)ev)->detail != XCB_NOTIFY_DETAIL_POINTER) say("focus-in");
                break;
            case XCB_KEY_PRESS: {
                xcb_key_press_event_t *k = (xcb_key_press_event_t *)ev;
                say("key %u", k->detail);
                if (k->detail == KEYCODE_SPACE && animate == 0) {
                    animate = ANIMATE_FRAMES;
                    animated = 0;
                    next_frame = 0;
                }
                break;
            }
            case XCB_BUTTON_PRESS: {
                xcb_button_press_event_t *b = (xcb_button_press_event_t *)ev;
                say("button %u %d,%d", b->detail, b->event_x, b->event_y);
                break;
            }
            case XCB_CLIENT_MESSAGE: {
                xcb_client_message_event_t *m = (xcb_client_message_event_t *)ev;
                if (m->type == protocols && m->format == 32 && m->data.data32[0] == delete_window) {
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
