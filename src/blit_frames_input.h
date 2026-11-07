/* ADR 0225: the physical keyboard and mouse to a compositor's EIS socket.

   A version 3 frame client (blit-gamescope, or a desktop relaying its
   windows' pictures) asks, on its authenticated channel, for the input of
   its session to go to the EIS socket of the compositor whose output one of
   its images shows. The wire text is docs/api.md, "Picture frames by
   descriptor", and only that:

       INPUT eis <absolute path> <image id>\n
           OK input <image id> / ERROR INPUT <image id> <code>
       INPUT on <image id>\n   INPUT off <image id>\n
           whether that picture's window has the keys

   Each route is one image's, so one channel carries the routes of several
   pictures (a desktop multiplexes its windows over one channel per display,
   Hibr #486): up to BLIT_FRAME_INPUT_ROUTES per channel, each with its own
   libei context, devices and held keys and buttons. At most one route of a
   session is on, across all of its channels: `INPUT on` turns the others
   off, releasing what they held, before the new one gets anything. A route
   ends with its image or its channel; a compositor that refuses or drops it
   later ends only that route, told `ERROR INPUT <image id> ENOENT`.

   The name comes from a peer whose uid Blit read at the hello, never from a
   terminal stream (ADR 0066). The socket must be that uid's: the file's
   owner, checked before Blit connects, and the listening process's
   credentials (SO_PEERCRED) on the connected socket, so a socket swapped
   between the two checks is refused too. Blit never connects to another
   user's compositor.

   libei is loaded at run time, so `blit` has no dependency on it; there are
   no libei headers on the build host, so the few functions used are
   declared here from the libei 1.x public API. Blit is a sender: it binds
   the keyboard, pointer, absolute pointer, button and scroll capabilities of
   each seat it is offered and emulates on the devices the compositor adds.

   A route is live only while it is on, its channel's session is Blit's
   focused session and Blit holds the console (and its image exists), so at
   most one route is live at a time. While live, evdev keys (press and
   release, no repeats), buttons, wheel notches, and either the absolute
   pointer over the image's drawn rectangle or, under pointer lock (?7701),
   the raw relative motion, are sent; the session gets its keys as before.
   When it stops being live, every key and button pressed through it is
   released first, then emulation stops. Included at the end of
   blit_frames.h. */

#include <dlfcn.h>
#include <stdbool.h>

/* libei 1.x, libei.h: enum ei_device_capability (bit flags since 1.0). */
#define BLIT_EI_CAP_POINTER 1u
#define BLIT_EI_CAP_POINTER_ABSOLUTE 2u
#define BLIT_EI_CAP_KEYBOARD 4u
#define BLIT_EI_CAP_SCROLL 16u
#define BLIT_EI_CAP_BUTTON 32u
/* libei 1.x, libei.h: enum ei_event_type, the ones a sender acts on. */
#define BLIT_EI_EVENT_CONNECT 1
#define BLIT_EI_EVENT_DISCONNECT 2
#define BLIT_EI_EVENT_SEAT_ADDED 3
#define BLIT_EI_EVENT_DEVICE_ADDED 5
#define BLIT_EI_EVENT_DEVICE_REMOVED 6
#define BLIT_EI_EVENT_DEVICE_PAUSED 7
#define BLIT_EI_EVENT_DEVICE_RESUMED 8

#define BLIT_EI_DEVICES 8
/* One wheel notch, in libei's discrete units. */
#define BLIT_EI_SCROLL_NOTCH 120

struct ei;
struct ei_event;
struct ei_seat;
struct ei_device;
struct ei_region;

static struct {
    void *library;
    struct ei *(*new_sender)(void *user_data);
    void (*configure_name)(struct ei *ei, const char *name);
    int (*setup_backend_fd)(struct ei *ei, int fd);
    int (*get_fd)(struct ei *ei);
    void (*dispatch)(struct ei *ei);
    struct ei_event *(*get_event)(struct ei *ei);
    struct ei *(*unref)(struct ei *ei);
    int (*event_get_type)(struct ei_event *event);
    struct ei_seat *(*event_get_seat)(struct ei_event *event);
    struct ei_device *(*event_get_device)(struct ei_event *event);
    struct ei_event *(*event_unref)(struct ei_event *event);
    void (*seat_bind_capabilities)(struct ei_seat *seat, ...);
    bool (*device_has_capability)(struct ei_device *device, int capability);
    struct ei_device *(*device_ref)(struct ei_device *device);
    struct ei_device *(*device_unref)(struct ei_device *device);
    struct ei_region *(*device_get_region)(struct ei_device *device, size_t index);
    uint32_t (*region_get_x)(struct ei_region *region);
    uint32_t (*region_get_y)(struct ei_region *region);
    uint32_t (*region_get_width)(struct ei_region *region);
    uint32_t (*region_get_height)(struct ei_region *region);
    void (*start_emulating)(struct ei_device *device, uint32_t sequence);
    void (*stop_emulating)(struct ei_device *device);
    void (*frame)(struct ei_device *device, uint64_t time);
    void (*keyboard_key)(struct ei_device *device, uint32_t keycode, bool is_press);
    void (*pointer_motion)(struct ei_device *device, double x, double y);
    void (*pointer_motion_absolute)(struct ei_device *device, double x, double y);
    void (*button_button)(struct ei_device *device, uint32_t button, bool is_press);
    void (*scroll_discrete)(struct ei_device *device, int32_t x, int32_t y);
} blit_ei;

typedef struct {
    struct ei_device *device;
    unsigned int caps;
    int resumed;
    int emulating;
} BlitEiDevice;

typedef struct BlitFrameInput BlitFrameInput;

struct BlitFrameInput {
    struct ei *ei;          /* the libei context, for the route's life */
    int fd;                 /* ei_get_fd, polled in Blit's loop */
    unsigned int image_id;
    int on;                 /* INPUT on / INPUT off */
    int image_seen;         /* the image has existed: its absence is a delete */
    int connected;          /* EI_EVENT_CONNECT seen */
    BlitEiDevice devices[BLIT_EI_DEVICES];
    uint32_t sequence;      /* start_emulating's */
    unsigned char keys[(KEY_MAX + 8) / 8];  /* pressed through this route */
    unsigned int buttons;   /* Blit's buttons 0..2 pressed through it */
    char region[48];        /* the absolute device's first region, WxH+X+Y */
};

/* Every symbol or none: a libei without one of them is not one Blit can
   drive. Tried again on each request, so installing libei1 needs no
   restart. */
static int frame_input_load(void)
{
    void *l;
    if (blit_ei.library != NULL)
        return 1;
    l = dlopen("libei.so.1", RTLD_NOW | RTLD_LOCAL);
    if (l == NULL)
        return 0;
#define BLIT_EI_SYM(field, name) ((*(void **)&blit_ei.field = dlsym(l, name)) != NULL)
    if (!(BLIT_EI_SYM(new_sender, "ei_new_sender") &&
          BLIT_EI_SYM(configure_name, "ei_configure_name") &&
          BLIT_EI_SYM(setup_backend_fd, "ei_setup_backend_fd") &&
          BLIT_EI_SYM(get_fd, "ei_get_fd") &&
          BLIT_EI_SYM(dispatch, "ei_dispatch") &&
          BLIT_EI_SYM(get_event, "ei_get_event") &&
          BLIT_EI_SYM(unref, "ei_unref") &&
          BLIT_EI_SYM(event_get_type, "ei_event_get_type") &&
          BLIT_EI_SYM(event_get_seat, "ei_event_get_seat") &&
          BLIT_EI_SYM(event_get_device, "ei_event_get_device") &&
          BLIT_EI_SYM(event_unref, "ei_event_unref") &&
          BLIT_EI_SYM(seat_bind_capabilities, "ei_seat_bind_capabilities") &&
          BLIT_EI_SYM(device_has_capability, "ei_device_has_capability") &&
          BLIT_EI_SYM(device_ref, "ei_device_ref") &&
          BLIT_EI_SYM(device_unref, "ei_device_unref") &&
          BLIT_EI_SYM(device_get_region, "ei_device_get_region") &&
          BLIT_EI_SYM(region_get_x, "ei_region_get_x") &&
          BLIT_EI_SYM(region_get_y, "ei_region_get_y") &&
          BLIT_EI_SYM(region_get_width, "ei_region_get_width") &&
          BLIT_EI_SYM(region_get_height, "ei_region_get_height") &&
          BLIT_EI_SYM(start_emulating, "ei_device_start_emulating") &&
          BLIT_EI_SYM(stop_emulating, "ei_device_stop_emulating") &&
          BLIT_EI_SYM(frame, "ei_device_frame") &&
          BLIT_EI_SYM(keyboard_key, "ei_device_keyboard_key") &&
          BLIT_EI_SYM(pointer_motion, "ei_device_pointer_motion") &&
          BLIT_EI_SYM(pointer_motion_absolute, "ei_device_pointer_motion_absolute") &&
          BLIT_EI_SYM(button_button, "ei_device_button_button") &&
          BLIT_EI_SYM(scroll_discrete, "ei_device_scroll_discrete"))) {
        dlclose(l);
        memset(&blit_ei, 0, sizeof(blit_ei));
        return 0;
    }
#undef BLIT_EI_SYM
    blit_ei.library = l;
    return 1;
}


static int frame_input_key_held(const BlitFrameInput *in, unsigned int code)
{
    return code <= KEY_MAX && (in->keys[code / 8] >> (code % 8)) & 1;
}

static void frame_input_key_mark(BlitFrameInput *in, unsigned int code, int held)
{
    if (code > KEY_MAX)
        return;
    if (held)
        in->keys[code / 8] |= (unsigned char)(1u << (code % 8));
    else
        in->keys[code / 8] &= (unsigned char)~(1u << (code % 8));
}

/* Blit's buttons 0, 1, 2 are the evdev codes EIS speaks. */
static uint32_t frame_input_button_code(int button)
{
    return button == BLIT_POINTER_BUTTON_LEFT ? BTN_LEFT :
        button == BLIT_POINTER_BUTTON_MIDDLE ? BTN_MIDDLE : BTN_RIGHT;
}

/* The emulating device with a capability, or NULL. */
static struct ei_device *frame_input_device(BlitFrameInput *in, unsigned int cap)
{
    int i;
    for (i = 0; i < BLIT_EI_DEVICES; ++i)
        if (in->devices[i].device != NULL && in->devices[i].emulating &&
            (in->devices[i].caps & cap))
            return in->devices[i].device;
    return NULL;
}

/* One event sent: counted for the session, and its frame. */
static void frame_input_sent(BlitSession *session, struct ei_device *device)
{
    blit_ei.frame(device, monotonic_us());
    if (session != NULL)
        ++session->frame_input_events;
}

static void frame_input_error(BlitSession *session, const char *code)
{
    if (session != NULL)
        snprintf(session->frame_input_error, sizeof(session->frame_input_error), "%s", code);
}

/* Whether the image the route names is gone: deleted by Kitty, or no longer
   held after it was. */
static int frame_input_image_gone(BlitSession *session, BlitFrameInput *in)
{
    BlitGraphicsImage *image = graphics_image_find(session, in->image_id);
    if (image != NULL && image->descriptor) {
        in->image_seen = 1;
        return 0;
    }
    return in->image_seen || frame_was_deleted(session, in->image_id);
}

/* The one rule: on, the focused session's, the console Blit's, the image
   there, the compositor still there. */
static int frame_input_live(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session;
    if (in == NULL || in->ei == NULL || !in->on || runtime->console_away || !blit_vt_active())
        return 0;
    session = find_session_id(runtime, client->session_id);
    return session != NULL && session->focused && !frame_input_image_gone(session, in);
}

/* Every key and button pressed through the route released, then emulation
   stopped on each device. */
static void frame_input_end(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session = find_session_id(runtime, client->session_id);
    struct ei_device *device;
    unsigned int code;
    int i;
    if (in == NULL)
        return;
    for (i = 0; i < BLIT_EI_DEVICES && !in->devices[i].emulating; ++i) {}
    if (i == BLIT_EI_DEVICES && in->buttons == 0) {
        for (code = 0; code < sizeof(in->keys) && in->keys[code] == 0; ++code) {}
        if (code == sizeof(in->keys))
            return;  /* nothing held, nothing started */
    }
    device = in->ei != NULL ? frame_input_device(in, BLIT_EI_CAP_KEYBOARD) : NULL;
    for (code = 0; code <= KEY_MAX; ++code)
        if (frame_input_key_held(in, code)) {
            frame_input_key_mark(in, code, 0);
            if (device != NULL) {
                blit_ei.keyboard_key(device, code, false);
                frame_input_sent(session, device);
            }
        }
    device = in->ei != NULL ? frame_input_device(in, BLIT_EI_CAP_BUTTON) : NULL;
    for (i = 0; i < 3; ++i)
        if (in->buttons & (1u << i)) {
            in->buttons &= ~(1u << i);
            if (device != NULL) {
                blit_ei.button_button(device, frame_input_button_code(i), false);
                frame_input_sent(session, device);
            }
        }
    for (i = 0; i < BLIT_EI_DEVICES; ++i)
        if (in->devices[i].device != NULL && in->devices[i].emulating) {
            if (in->ei != NULL)
                blit_ei.stop_emulating(in->devices[i].device);
            in->devices[i].emulating = 0;
        }
}

/* The route brought in line with the rule: emulation started on each
   resumed device while live, ended (with its releases) when not. */
static int frame_input_sync(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    int i, live = frame_input_live(runtime, client, in);
    if (in == NULL)
        return 0;
    if (!live) {
        frame_input_end(runtime, client, in);
        return 0;
    }
    for (i = 0; i < BLIT_EI_DEVICES; ++i)
        if (in->devices[i].device != NULL && in->devices[i].resumed &&
            !in->devices[i].emulating) {
            blit_ei.start_emulating(in->devices[i].device, ++in->sequence);
            in->devices[i].emulating = 1;
        }
    return 1;
}

/* The compositor's devices, references dropped (it went, or the route
   ended). Whatever was held is the compositor's to forget. */
static void frame_input_forget_devices(BlitFrameInput *in)
{
    int i;
    for (i = 0; i < BLIT_EI_DEVICES; ++i)
        if (in->devices[i].device != NULL)
            blit_ei.device_unref(in->devices[i].device);
    memset(in->devices, 0, sizeof(in->devices));
    memset(in->keys, 0, sizeof(in->keys));
    in->buttons = 0;
    in->region[0] = '\0';
}

/* The route in a slot gone: its devices and libei context dropped, its slot
   free. Whatever it held was released before, or is a gone compositor's to
   forget. */
static void frame_input_free(BlitFrameClient *client, int slot)
{
    BlitFrameInput *in = client->inputs[slot];
    if (in == NULL)
        return;
    if (in->ei != NULL) {
        frame_input_forget_devices(in);
        blit_ei.unref(in->ei);
    }
    free(in);
    client->inputs[slot] = NULL;
    --frame_input_routes;
}

/* Every route brought in line, and a route whose image has gone ended: it
   is stopped and its keys released by the sync, then it is freed. The
   channel is not told; its image is gone. */
static void frame_input_reconcile(BlitRuntime *runtime)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        BlitSession *session;
        if (!client->used)
            continue;
        session = find_session_id(runtime, client->session_id);
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in == NULL)
                continue;
            frame_input_sync(runtime, client, in);
            if (session != NULL && frame_input_image_gone(session, in)) {
                frame_input_end(runtime, client, in);
                frame_input_free(client, k);
            }
        }
    }
}

/* The compositor refused us in its handshake or closed the connection: the
   client is told `ERROR INPUT <image id> ENOENT` now and that route ends
   (nothing held can be released to a compositor that has gone). The
   channel's other routes are untouched. */
static void frame_input_disconnected(BlitRuntime *runtime, BlitFrameClient *client, int slot,
                                     const char *why)
{
    char line[48];
    snprintf(line, sizeof(line), "ERROR INPUT %u ENOENT\n", client->inputs[slot]->image_id);
    frame_input_free(client, slot);
    frame_input_error(find_session_id(runtime, client->session_id), why);
    frame_reply(client->fd, line);
}

/* The channel closed: its routes end with it, each releasing what it held
   first. */
static void frame_input_close(BlitRuntime *runtime, BlitFrameClient *client)
{
    int k;
    for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k)
        if (client->inputs[k] != NULL) {
            frame_input_end(runtime, client, client->inputs[k]);
            frame_input_free(client, k);
        }
}

static void frame_input_device_event(BlitFrameInput *in, int type, struct ei_device *device)
{
    int i, slot = -1;
    for (i = 0; i < BLIT_EI_DEVICES; ++i)
        if (in->devices[i].device == device)
            slot = i;
    if (type == BLIT_EI_EVENT_DEVICE_ADDED) {
        static const unsigned int caps[] = {
            BLIT_EI_CAP_POINTER, BLIT_EI_CAP_POINTER_ABSOLUTE, BLIT_EI_CAP_KEYBOARD,
            BLIT_EI_CAP_SCROLL, BLIT_EI_CAP_BUTTON
        };
        BlitEiDevice *d;
        if (slot >= 0)
            return;
        for (i = 0; i < BLIT_EI_DEVICES && slot < 0; ++i)
            if (in->devices[i].device == NULL)
                slot = i;
        if (slot < 0)
            return;  /* more devices than any compositor offers: ignored */
        d = &in->devices[slot];
        memset(d, 0, sizeof(*d));
        d->device = blit_ei.device_ref(device);
        for (i = 0; i < (int)(sizeof(caps) / sizeof(caps[0])); ++i)
            if (blit_ei.device_has_capability(device, (int)caps[i]))
                d->caps |= caps[i];
        if ((d->caps & BLIT_EI_CAP_POINTER_ABSOLUTE) && in->region[0] == '\0') {
            struct ei_region *r = blit_ei.device_get_region(device, 0);
            if (r != NULL)
                snprintf(in->region, sizeof(in->region), "%ux%u+%u+%u",
                         blit_ei.region_get_width(r), blit_ei.region_get_height(r),
                         blit_ei.region_get_x(r), blit_ei.region_get_y(r));
        }
        return;
    }
    if (slot < 0)
        return;
    if (type == BLIT_EI_EVENT_DEVICE_RESUMED) {
        in->devices[slot].resumed = 1;
        return;
    }
    /* Paused or removed: the compositor has stopped this device's emulation
       and forgets what it held. */
    if (in->devices[slot].caps & BLIT_EI_CAP_KEYBOARD)
        memset(in->keys, 0, sizeof(in->keys));
    if (in->devices[slot].caps & BLIT_EI_CAP_BUTTON)
        in->buttons = 0;
    in->devices[slot].resumed = 0;
    in->devices[slot].emulating = 0;
    if (type == BLIT_EI_EVENT_DEVICE_REMOVED) {
        blit_ei.device_unref(in->devices[slot].device);
        memset(&in->devices[slot], 0, sizeof(in->devices[slot]));
    }
}

/* What one route's compositor said, read without blocking. */
static void frame_input_dispatch(BlitRuntime *runtime, BlitFrameClient *client, int slot)
{
    BlitFrameInput *in = client->inputs[slot];
    struct ei_event *event;
    blit_ei.dispatch(in->ei);
    while (in->ei != NULL && (event = blit_ei.get_event(in->ei)) != NULL) {
        int type = blit_ei.event_get_type(event);
        switch (type) {
        case BLIT_EI_EVENT_CONNECT:
            in->connected = 1;
            break;
        case BLIT_EI_EVENT_DISCONNECT:
            blit_ei.event_unref(event);
            frame_input_disconnected(runtime, client, slot,
                                     in->connected ? "eis-disconnected" : "eis-refused");
            return;  /* the route is gone */
        case BLIT_EI_EVENT_SEAT_ADDED:
            blit_ei.seat_bind_capabilities(blit_ei.event_get_seat(event),
                                           (int)BLIT_EI_CAP_POINTER,
                                           (int)BLIT_EI_CAP_POINTER_ABSOLUTE,
                                           (int)BLIT_EI_CAP_KEYBOARD,
                                           (int)BLIT_EI_CAP_BUTTON,
                                           (int)BLIT_EI_CAP_SCROLL, NULL);
            break;
        case BLIT_EI_EVENT_DEVICE_ADDED:
        case BLIT_EI_EVENT_DEVICE_REMOVED:
        case BLIT_EI_EVENT_DEVICE_PAUSED:
        case BLIT_EI_EVENT_DEVICE_RESUMED:
            frame_input_device_event(in, type, blit_ei.event_get_device(event));
            break;
        default:
            break;
        }
        blit_ei.event_unref(event);
    }
    frame_input_sync(runtime, client, in);
}

static void frame_input_select(const BlitRuntime *runtime, fd_set *readable, int *max_fd)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        if (!runtime->frame_clients[i].used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            const BlitFrameInput *in = runtime->frame_clients[i].inputs[k];
            if (in != NULL && in->ei != NULL && in->fd >= 0) {
                FD_SET(in->fd, readable);
                if (in->fd > *max_fd) *max_fd = in->fd;
            }
        }
    }
}

static void frame_input_service(BlitRuntime *runtime, const fd_set *readable)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in != NULL && in->ei != NULL && in->fd >= 0 && FD_ISSET(in->fd, readable))
                frame_input_dispatch(runtime, client, k);
        }
    }
}

/* ---- What evdev dispatch calls; each a no-op unless a route is live. ----
   Every route is synced, so one that stopped being live releases what it
   held; the live one, at most one, gets the event. */

static void frame_input_key(BlitRuntime *runtime, unsigned int code, int pressed)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            struct ei_device *device;
            if (in == NULL || !frame_input_sync(runtime, client, in))
                continue;
            device = frame_input_device(in, BLIT_EI_CAP_KEYBOARD);
            /* A release only for a key pressed here: one pressed before the
               route went live was never the compositor's. */
            if (device == NULL || (!pressed && !frame_input_key_held(in, code)))
                continue;
            frame_input_key_mark(in, code, pressed);
            blit_ei.keyboard_key(device, code, pressed != 0);
            frame_input_sent(find_session_id(runtime, client->session_id), device);
        }
    }
}

static void frame_input_button(BlitRuntime *runtime, int button, int pressed)
{
    int i, k;
    if (button < 0 || button > 2)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            struct ei_device *device;
            if (in == NULL || !frame_input_sync(runtime, client, in))
                continue;
            device = frame_input_device(in, BLIT_EI_CAP_BUTTON);
            if (device == NULL || (!pressed && !(in->buttons & (1u << button))))
                continue;
            if (pressed)
                in->buttons |= 1u << button;
            else
                in->buttons &= ~(1u << button);
            blit_ei.button_button(device, frame_input_button_code(button), pressed != 0);
            frame_input_sent(find_session_id(runtime, client->session_id), device);
        }
    }
}

/* One notch: `direction` positive is up (or right), as REL_WHEEL counts it
   after the device's natural scrolling; EIS counts down and right. */
static void frame_input_scroll(BlitRuntime *runtime, int horizontal, int direction)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            struct ei_device *device;
            if (in == NULL || !frame_input_sync(runtime, client, in))
                continue;
            device = frame_input_device(in, BLIT_EI_CAP_SCROLL);
            if (device == NULL || direction == 0)
                continue;
            if (horizontal)
                blit_ei.scroll_discrete(device, direction > 0 ? BLIT_EI_SCROLL_NOTCH : -BLIT_EI_SCROLL_NOTCH, 0);
            else
                blit_ei.scroll_discrete(device, 0, direction > 0 ? -BLIT_EI_SCROLL_NOTCH : BLIT_EI_SCROLL_NOTCH);
            frame_input_sent(find_session_id(runtime, client->session_id), device);
        }
    }
}

/* The pointer over one route's image's drawn rectangle, as a position in
   the compositor's pointer region (its device's first region, which is its
   own coordinate space whatever size of stream it sends), or in the image's
   latest frame when the device has no bounded region (gamescope offers one
   INT32_MAX square, meaning its own output pixels): wherever the picture is
   placed and however large it is drawn, through the same placement and draw
   rectangle the compositors use. */
static void frame_input_pointer_route(BlitRuntime *runtime, BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session, *under;
    BlitGraphicsImage *image;
    BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_PLACEMENTS + 1];
    struct ei_device *device;
    int column, row, px = 0, py = 0, count, k;
    device = frame_input_device(in, BLIT_EI_CAP_POINTER_ABSOLUTE);
    session = find_session_id(runtime, client->session_id);
    if (device == NULL || session == NULL || !runtime->pointer_valid)
        return;
    under = mouse_target_px(runtime, runtime->pointer_output, runtime->pointer_x,
                            runtime->pointer_y, &column, &row, &px, &py);
    image = graphics_image_find(session, in->image_id);
    if (under != session || image == NULL)
        return;
    /* Surface pixels, zero-based; the topmost draw under them wins. */
    --px;
    --py;
    count = blit_graphics_collect(image, 1, draws, BLIT_GRAPHICS_MAX_PLACEMENTS + 1);
    for (k = count - 1; k >= 0; --k) {
        const BlitGraphicsDraw *d = &draws[k];
        struct ei_region *r;
        double fx, fy, rx = 0, ry = 0, rw = image->width, rh = image->height;
        if (d->width < 1 || d->height < 1 || image->width < 1 || image->height < 1 ||
            px < d->x || py < d->y || px >= d->x + d->width || py >= d->y + d->height)
            continue;
        /* Where in the frame, as a fraction of it. */
        fx = (d->src_x + (double)(px - d->x) * d->src_w / d->width) / image->width;
        fy = (d->src_y + (double)(py - d->y) * d->src_h / d->height) / image->height;
        r = blit_ei.device_get_region(device, 0);
        if (r != NULL && blit_ei.region_get_width(r) > 0 && blit_ei.region_get_height(r) > 0 &&
            blit_ei.region_get_width(r) < INT32_MAX && blit_ei.region_get_height(r) < INT32_MAX) {
            rx = blit_ei.region_get_x(r);
            ry = blit_ei.region_get_y(r);
            rw = blit_ei.region_get_width(r);
            rh = blit_ei.region_get_height(r);
        }
        blit_ei.pointer_motion_absolute(device, rx + fx * rw, ry + fy * rh);
        frame_input_sent(session, device);
        break;
    }
}

/* The absolute pointer, to the live route only. */
static void frame_input_pointer(BlitRuntime *runtime)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k)
            if (client->inputs[k] != NULL && frame_input_sync(runtime, client, client->inputs[k]))
                frame_input_pointer_route(runtime, client, client->inputs[k]);
    }
}

/* Under pointer lock: the raw sum the locked program is sent, as relative
   motion, to the live route only (one of the lock holder's). */
static void frame_input_relative(BlitRuntime *runtime, int dx, int dy)
{
    BlitSession *holder = pointer_lock_session(runtime);
    int i, k;
    if (holder == NULL || (dx == 0 && dy == 0))
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used || client->session_id != holder->id)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            struct ei_device *device;
            if (in == NULL || !frame_input_sync(runtime, client, in))
                continue;
            device = frame_input_device(in, BLIT_EI_CAP_POINTER);
            if (device == NULL)
                continue;
            blit_ei.pointer_motion(device, (double)dx, (double)dy);
            frame_input_sent(holder, device);
        }
    }
}

/* ---- The requests, on an authenticated channel. ---- */

/* `ERROR INPUT <image id> <code>`; the id is 0 when the line named none or
   could not be read. */
static void frame_input_refuse(BlitRuntime *runtime, BlitFrameClient *client,
                               unsigned int image_id, const char *code)
{
    char line[64];
    frame_input_error(find_session_id(runtime, client->session_id), code);
    snprintf(line, sizeof(line), "ERROR INPUT %u %s\n", image_id, code);
    frame_reply(client->fd, line);
}

/* This channel's route for an image: its slot, or -1. */
static int frame_input_slot(const BlitFrameClient *client, unsigned int image_id)
{
    int k;
    for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k)
        if (client->inputs[k] != NULL && client->inputs[k]->image_id == image_id)
            return k;
    return -1;
}

/* `INPUT eis <path> <image-id>`: the checks that need nothing come first,
   then libei, then the socket's owner, the connection and its peer. */
static void frame_input_open(BlitRuntime *runtime, BlitFrameClient *client,
                             const char *path, unsigned int image_id)
{
    BlitSession *session = find_session_id(runtime, client->session_id);
    BlitGraphicsImage *image = session != NULL ? graphics_image_find(session, image_id) : NULL;
    struct sockaddr_un address;
    struct ucred peer;
    socklen_t peer_length = sizeof(peer);
    struct stat info;
    BlitFrameInput *in;
    struct ei *ei;
    int fd, slot;

    /* An image whose frame has not come yet is accepted, as a placement
       that arrives first is (ADR 0202); a deleted one, or a Kitty image
       that is not a frame client's, is not one this route can stand for. */
    if (session == NULL || image_id == 0 || (image != NULL && !image->descriptor) ||
        (image == NULL && frame_was_deleted(session, image_id))) {
        frame_input_refuse(runtime, client, image_id, "EINVAL");
        return;
    }
    if (frame_input_slot(client, image_id) >= 0) {
        frame_input_refuse(runtime, client, image_id, "EBUSY");
        return;
    }
    for (slot = 0; slot < BLIT_FRAME_INPUT_ROUTES && client->inputs[slot] != NULL; ++slot) {}
    if (slot == BLIT_FRAME_INPUT_ROUTES) {
        /* As many routes as a session can have images. */
        frame_input_refuse(runtime, client, image_id, "ENOMEM");
        return;
    }
    if (!frame_input_load()) {
        frame_input_refuse(runtime, client, image_id, "ENOTSUP");
        return;
    }
    /* The file itself, not what a link names; a socket, the client's. */
    if (lstat(path, &info) != 0 || !S_ISSOCK(info.st_mode)) {
        frame_input_refuse(runtime, client, image_id, "ENOENT");
        return;
    }
    if (info.st_uid != client->uid) {
        frame_input_refuse(runtime, client, image_id, "EACCES");
        return;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path));
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        if (fd >= 0) close(fd);
        frame_input_refuse(runtime, client, image_id, "ENOENT");
        return;
    }
    /* Who is listening, not only who owns the name: closes the window
       between the lstat and the connect. */
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) != 0 ||
        peer.uid != client->uid) {
        close(fd);
        frame_input_refuse(runtime, client, image_id, "EACCES");
        return;
    }
    in = (BlitFrameInput *)calloc(1, sizeof(*in));
    ei = blit_ei.new_sender(NULL);
    if (in == NULL || ei == NULL) {
        free(in);
        if (ei != NULL) blit_ei.unref(ei);
        close(fd);
        frame_input_refuse(runtime, client, image_id, "ENOMEM");
        return;
    }
    blit_ei.configure_name(ei, "blit");
    /* The descriptor is libei's from here: the source it makes of it closes
       it, whether or not the setup succeeds. */
    if (blit_ei.setup_backend_fd(ei, fd) != 0) {
        blit_ei.unref(ei);
        free(in);
        frame_input_refuse(runtime, client, image_id, "ENOENT");
        return;
    }
    in->ei = ei;
    in->fd = blit_ei.get_fd(ei);
    in->image_id = image_id;
    in->image_seen = image != NULL;
    client->inputs[slot] = in;
    ++frame_input_routes;
    frame_input_error(session, "");
    {
        char line[48];
        snprintf(line, sizeof(line), "OK input %u\n", image_id);
        frame_reply(client->fd, line);
    }
}

/* `INPUT on <image-id>`: every other route of the session, on any of its
   channels, turned off first and what it held released, so two windows
   never both get the keys, even if an `off` was lost. */
static void frame_input_turn_on(BlitRuntime *runtime, BlitFrameClient *client, BlitFrameInput *route)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *other = &runtime->frame_clients[i];
        if (!other->used || !other->authenticated || other->session_id != client->session_id)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k)
            if (other->inputs[k] != NULL && other->inputs[k] != route && other->inputs[k]->on) {
                other->inputs[k]->on = 0;
                frame_input_sync(runtime, other, other->inputs[k]);
            }
    }
    route->on = 1;
    frame_input_sync(runtime, client, route);
}

/* An image id as the wire writes it: decimal, 1 to 2^32-1, no leading zero;
   0 when it is not one. */
static unsigned int frame_input_parse_id(const char *text)
{
    size_t length = strlen(text);
    unsigned long id;
    if (length < 1 || length > 10 || text[0] < '1' || text[0] > '9' ||
        strspn(text, "0123456789") != length)
        return 0;
    id = strtoul(text, NULL, 10);
    return id > 0xffffffffUL ? 0 : (unsigned int)id;
}

/* One INPUT message: a line, its newline optional. The image id is read
   from a line of a request's form; a line that has none, or is not one,
   is answered with 0. */
static void frame_input_request(BlitRuntime *runtime, BlitFrameClient *client,
                                const unsigned char *message, ssize_t length)
{
    char line[160], path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *id_text = NULL, *path_text = NULL;
    size_t path_length = 0;
    unsigned int id;
    int kind = 0;  /* 'e' eis, 'n' on, 'f' off */
    ssize_t i;

    if (length > 0 && message[length - 1] == '\n')
        --length;
    for (i = 0; i < length; ++i)
        if (message[i] < 0x20 || message[i] == 0x7f)
            break;
    if (i != length || length >= (ssize_t)sizeof(line)) {
        frame_input_refuse(runtime, client, 0, "EINVAL");
        return;
    }
    memcpy(line, message, (size_t)length);
    line[length] = '\0';
    if (strncmp(line, "INPUT on ", 9) == 0) {
        kind = 'n';
        id_text = line + 9;
    } else if (strncmp(line, "INPUT off ", 10) == 0) {
        kind = 'f';
        id_text = line + 10;
    } else if (strncmp(line, "INPUT eis ", 10) == 0) {
        kind = 'e';
        path_text = line + 10;
        path_length = strcspn(path_text, " ");
        if (path_text[path_length] == ' ')
            id_text = path_text + path_length + 1;
    }
    id = id_text != NULL ? frame_input_parse_id(id_text) : 0;
    /* A version 2 channel's INPUT is not read at all. */
    if (client->version < 3 || id == 0) {
        frame_input_refuse(runtime, client, 0, "EINVAL");
        return;
    }
    if (kind == 'n' || kind == 'f') {
        int slot = frame_input_slot(client, id);
        if (slot < 0) {
            frame_input_refuse(runtime, client, id, "EINVAL");
            return;
        }
        if (kind == 'n') {
            frame_input_turn_on(runtime, client, client->inputs[slot]);
        } else {
            client->inputs[slot]->on = 0;
            frame_input_sync(runtime, client, client->inputs[slot]);
        }
        return;
    }
    if (path_length == 0 || path_text[0] != '/' || path_length >= sizeof(path)) {
        frame_input_refuse(runtime, client, id, "EINVAL");
        return;
    }
    memcpy(path, path_text, path_length);
    path[path_length] = '\0';
    frame_input_open(runtime, client, path, id);
}

/* frames.input: the session's counts and every route of its channels, with
   its image, its state (off, on but not live, live) and the compositor's
   region. Routes that do not fit are left out; the object stays whole. */
static size_t frame_input_json(BlitRuntime *runtime, const BlitSession *session,
                               char *out, size_t capacity)
{
    size_t used;
    int i, k, n, first = 1;
    n = snprintf(out, capacity, ",\"input\":{\"events\":%lu,\"last_error\":\"%s\",\"routes\":[",
                 session->frame_input_events, session->frame_input_error);
    if (n < 0 || (size_t)n + 3 > capacity)
        return 0;
    used = (size_t)n;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        const BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used || client->session_id != session->id)
            continue;
        for (k = 0; k < BLIT_FRAME_INPUT_ROUTES; ++k) {
            BlitFrameInput *in = client->inputs[k];
            char entry[128];
            if (in == NULL)
                continue;
            n = snprintf(entry, sizeof(entry), "%s{\"image\":%u,\"state\":\"%s\",\"region\":\"%s\"}",
                         first ? "" : ",", in->image_id,
                         !in->on ? "off" : frame_input_live(runtime, client, in) ? "live" : "on",
                         in->region);
            if (n < 0 || (size_t)n >= sizeof(entry) || used + (size_t)n + 3 > capacity)
                continue;
            memcpy(out + used, entry, (size_t)n);
            used += (size_t)n;
            first = 0;
        }
    }
    memcpy(out + used, "]}", 3);
    return used + 2;
}
