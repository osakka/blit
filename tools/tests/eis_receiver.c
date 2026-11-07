/* A stand-in compositor for frame_input_eis_gate (ADR 0225): an EIS server
   on a socket, through libeis.so.1 loaded at run time (no libeis headers
   are needed to build it; the functions are declared here from the libeis
   1.x public API). It offers each sender one seat with the keyboard,
   pointer, absolute pointer, button and scroll capabilities, makes one
   device of what the sender binds, with one region of the size it is
   given, resumes it, and writes what arrives to a log, one canonical line
   each, flushed at once:

       connect | disconnect | bind <caps> | start | stop
       key <code> press|release        button <code> press|release
       abs <x> <y>    rel <dx> <dy>    scroll <dx> <dy>   (discrete, 120 a notch)
       # frame            (each frame; '#' lines are not canonical)

   Events are recognised by eis_event_type_to_string's names where libeis
   has it, so a wrong number in this file cannot agree with a wrong number in
   Blit; the first line says which.

   eis_receiver probe                         0 if libeis.so.1 loads, else 77
   eis_receiver <socket> <log> <width> <height>   until SIGTERM */
#define _GNU_SOURCE
#include <dlfcn.h>
#include <errno.h>
#include <poll.h>
#include <signal.h>
#include <stdbool.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* libeis 1.x, libeis.h: enum eis_device_capability. */
#define CAP_POINTER 1
#define CAP_POINTER_ABSOLUTE 2
#define CAP_KEYBOARD 4
#define CAP_SCROLL 16
#define CAP_BUTTON 32

struct eis;
struct eis_event;
struct eis_client;
struct eis_seat;
struct eis_device;
struct eis_region;

static struct {
    struct eis *(*new_)(void *user_data);
    int (*setup_backend_socket)(struct eis *eis, const char *path);
    int (*get_fd)(struct eis *eis);
    void (*dispatch)(struct eis *eis);
    struct eis_event *(*get_event)(struct eis *eis);
    int (*event_get_type)(struct eis_event *event);
    struct eis_event *(*event_unref)(struct eis_event *event);
    const char *(*event_type_to_string)(int type);  /* optional */
    struct eis_client *(*event_get_client)(struct eis_event *event);
    bool (*client_is_sender)(struct eis_client *client);
    void (*client_connect)(struct eis_client *client);
    void (*client_disconnect)(struct eis_client *client);
    struct eis_seat *(*client_new_seat)(struct eis_client *client, const char *name);
    void (*seat_configure_capability)(struct eis_seat *seat, int cap);
    void (*seat_add)(struct eis_seat *seat);
    struct eis_seat *(*event_get_seat)(struct eis_event *event);
    bool (*event_seat_has_capability)(struct eis_event *event, int cap);
    struct eis_device *(*seat_new_device)(struct eis_seat *seat);
    void (*device_configure_name)(struct eis_device *device, const char *name);
    void (*device_configure_capability)(struct eis_device *device, int cap);
    struct eis_region *(*device_new_region)(struct eis_device *device);
    void (*region_set_size)(struct eis_region *region, uint32_t w, uint32_t h);
    void (*region_set_offset)(struct eis_region *region, uint32_t x, uint32_t y);
    void (*region_add)(struct eis_region *region);
    struct eis_region *(*region_unref)(struct eis_region *region);
    void (*device_add)(struct eis_device *device);
    void (*device_resume)(struct eis_device *device);
    uint32_t (*keyboard_get_key)(struct eis_event *event);
    bool (*keyboard_get_key_is_press)(struct eis_event *event);
    uint32_t (*button_get_button)(struct eis_event *event);
    bool (*button_get_is_press)(struct eis_event *event);
    double (*pointer_get_dx)(struct eis_event *event);
    double (*pointer_get_dy)(struct eis_event *event);
    double (*pointer_get_absolute_x)(struct eis_event *event);
    double (*pointer_get_absolute_y)(struct eis_event *event);
    int32_t (*scroll_get_discrete_dx)(struct eis_event *event);
    int32_t (*scroll_get_discrete_dy)(struct eis_event *event);
} eis;

static void *load(void)
{
    void *l = dlopen("libeis.so.1", RTLD_NOW | RTLD_LOCAL);
    if (l == NULL)
        return NULL;
#define SYM(field, name) ((*(void **)&eis.field = dlsym(l, name)) != NULL)
    if (!(SYM(new_, "eis_new") && SYM(setup_backend_socket, "eis_setup_backend_socket") &&
          SYM(get_fd, "eis_get_fd") && SYM(dispatch, "eis_dispatch") &&
          SYM(get_event, "eis_get_event") && SYM(event_get_type, "eis_event_get_type") &&
          SYM(event_unref, "eis_event_unref") && SYM(event_get_client, "eis_event_get_client") &&
          SYM(client_is_sender, "eis_client_is_sender") && SYM(client_connect, "eis_client_connect") &&
          SYM(client_disconnect, "eis_client_disconnect") &&
          SYM(client_new_seat, "eis_client_new_seat") &&
          SYM(seat_configure_capability, "eis_seat_configure_capability") &&
          SYM(seat_add, "eis_seat_add") && SYM(event_get_seat, "eis_event_get_seat") &&
          SYM(event_seat_has_capability, "eis_event_seat_has_capability") &&
          SYM(seat_new_device, "eis_seat_new_device") &&
          SYM(device_configure_name, "eis_device_configure_name") &&
          SYM(device_configure_capability, "eis_device_configure_capability") &&
          SYM(device_new_region, "eis_device_new_region") &&
          SYM(region_set_size, "eis_region_set_size") &&
          SYM(region_set_offset, "eis_region_set_offset") && SYM(region_add, "eis_region_add") &&
          SYM(region_unref, "eis_region_unref") && SYM(device_add, "eis_device_add") &&
          SYM(device_resume, "eis_device_resume") &&
          SYM(keyboard_get_key, "eis_event_keyboard_get_key") &&
          SYM(keyboard_get_key_is_press, "eis_event_keyboard_get_key_is_press") &&
          SYM(button_get_button, "eis_event_button_get_button") &&
          SYM(button_get_is_press, "eis_event_button_get_is_press") &&
          SYM(pointer_get_dx, "eis_event_pointer_get_dx") &&
          SYM(pointer_get_dy, "eis_event_pointer_get_dy") &&
          SYM(pointer_get_absolute_x, "eis_event_pointer_get_absolute_x") &&
          SYM(pointer_get_absolute_y, "eis_event_pointer_get_absolute_y") &&
          SYM(scroll_get_discrete_dx, "eis_event_scroll_get_discrete_dx") &&
          SYM(scroll_get_discrete_dy, "eis_event_scroll_get_discrete_dy"))) {
        fprintf(stderr, "eis_receiver: libeis.so.1 lacks %s\n", dlerror());
        return NULL;
    }
    SYM(event_type_to_string, "eis_event_type_to_string");
#undef SYM
    return l;
}

/* libeis 1.x, libeis.h: enum eis_event_type, used only where libeis cannot
   name its own events. */
static const char *table_name(int type)
{
    switch (type) {
    case 1: return "EIS_EVENT_CLIENT_CONNECT";
    case 2: return "EIS_EVENT_CLIENT_DISCONNECT";
    case 3: return "EIS_EVENT_SEAT_BIND";
    case 4: return "EIS_EVENT_DEVICE_CLOSED";
    case 100: return "EIS_EVENT_FRAME";
    case 200: return "EIS_EVENT_DEVICE_START_EMULATING";
    case 201: return "EIS_EVENT_DEVICE_STOP_EMULATING";
    case 300: return "EIS_EVENT_POINTER_MOTION";
    case 400: return "EIS_EVENT_POINTER_MOTION_ABSOLUTE";
    case 500: return "EIS_EVENT_BUTTON_BUTTON";
    case 600: return "EIS_EVENT_SCROLL_DELTA";
    case 603: return "EIS_EVENT_SCROLL_DISCRETE";
    case 700: return "EIS_EVENT_KEYBOARD_KEY";
    default: return "";
    }
}

static volatile sig_atomic_t running = 1;
static void stop(int signal_number) { (void)signal_number; running = 0; }

int main(int argc, char **argv)
{
    struct eis *ctx;
    struct eis_device *device = NULL;
    FILE *log;
    unsigned int width, height;

    if (argc == 2 && strcmp(argv[1], "probe") == 0)
        return load() != NULL ? 0 : 77;
    if (argc != 5) {
        fprintf(stderr, "usage: eis_receiver probe | <socket> <log> <width> <height>\n");
        return 2;
    }
    if (load() == NULL) {
        fprintf(stderr, "eis_receiver: libeis.so.1 is not available\n");
        return 77;
    }
    width = (unsigned int)strtoul(argv[3], NULL, 10);
    height = (unsigned int)strtoul(argv[4], NULL, 10);
    log = fopen(argv[2], "w");
    ctx = eis.new_(NULL);
    if (log == NULL || ctx == NULL || eis.setup_backend_socket(ctx, argv[1]) != 0) {
        fprintf(stderr, "eis_receiver: cannot serve %s: %s\n", argv[1], strerror(errno));
        return 1;
    }
    setvbuf(log, NULL, _IOLBF, 0);
    fprintf(log, "# names=%s\n", eis.event_type_to_string != NULL ? "libeis" : "table");
    signal(SIGTERM, stop);
    signal(SIGINT, stop);
    while (running) {
        struct pollfd p = { eis.get_fd(ctx), POLLIN, 0 };
        struct eis_event *e;
        if (poll(&p, 1, 200) < 0 && errno != EINTR)
            break;
        eis.dispatch(ctx);
        while ((e = eis.get_event(ctx)) != NULL) {
            int type = eis.event_get_type(e);
            const char *name = eis.event_type_to_string != NULL ?
                eis.event_type_to_string(type) : table_name(type);
            if (name == NULL) name = "";
            if (strcmp(name, table_name(type)) != 0 && strcmp(table_name(type), "") != 0)
                fprintf(log, "# libeis names %d %s, this file says %s\n", type, name, table_name(type));
            if (strcmp(name, "EIS_EVENT_CLIENT_CONNECT") == 0) {
                struct eis_client *client = eis.event_get_client(e);
                if (!eis.client_is_sender(client)) {
                    eis.client_disconnect(client);
                    fprintf(log, "refused-receiver\n");
                } else {
                    struct eis_seat *seat;
                    eis.client_connect(client);
                    seat = eis.client_new_seat(client, "stand-in");
                    eis.seat_configure_capability(seat, CAP_POINTER);
                    eis.seat_configure_capability(seat, CAP_POINTER_ABSOLUTE);
                    eis.seat_configure_capability(seat, CAP_KEYBOARD);
                    eis.seat_configure_capability(seat, CAP_BUTTON);
                    eis.seat_configure_capability(seat, CAP_SCROLL);
                    eis.seat_add(seat);
                    device = NULL;
                    fprintf(log, "connect\n");
                }
            } else if (strcmp(name, "EIS_EVENT_CLIENT_DISCONNECT") == 0) {
                device = NULL;
                fprintf(log, "disconnect\n");
            } else if (strcmp(name, "EIS_EVENT_SEAT_BIND") == 0 && device == NULL) {
                static const struct { int cap; const char *name; } caps[] = {
                    { CAP_KEYBOARD, "keyboard" }, { CAP_POINTER, "pointer" },
                    { CAP_POINTER_ABSOLUTE, "pointer_absolute" }, { CAP_BUTTON, "button" },
                    { CAP_SCROLL, "scroll" }
                };
                char bound[128] = "";
                size_t i;
                device = eis.seat_new_device(eis.event_get_seat(e));
                eis.device_configure_name(device, "stand-in");
                for (i = 0; i < sizeof(caps) / sizeof(caps[0]); ++i)
                    if (eis.event_seat_has_capability(e, caps[i].cap)) {
                        eis.device_configure_capability(device, caps[i].cap);
                        strcat(bound, bound[0] ? "," : "");
                        strcat(bound, caps[i].name);
                    }
                if (eis.event_seat_has_capability(e, CAP_POINTER_ABSOLUTE)) {
                    struct eis_region *region = eis.device_new_region(device);
                    eis.region_set_size(region, width, height);
                    eis.region_set_offset(region, 0, 0);
                    eis.region_add(region);
                    eis.region_unref(region);
                }
                eis.device_add(device);
                eis.device_resume(device);
                fprintf(log, "bind %s\n", bound);
            } else if (strcmp(name, "EIS_EVENT_DEVICE_START_EMULATING") == 0) {
                fprintf(log, "start\n");
            } else if (strcmp(name, "EIS_EVENT_DEVICE_STOP_EMULATING") == 0) {
                fprintf(log, "stop\n");
            } else if (strcmp(name, "EIS_EVENT_KEYBOARD_KEY") == 0) {
                fprintf(log, "key %u %s\n", eis.keyboard_get_key(e),
                        eis.keyboard_get_key_is_press(e) ? "press" : "release");
            } else if (strcmp(name, "EIS_EVENT_BUTTON_BUTTON") == 0) {
                fprintf(log, "button %u %s\n", eis.button_get_button(e),
                        eis.button_get_is_press(e) ? "press" : "release");
            } else if (strcmp(name, "EIS_EVENT_POINTER_MOTION") == 0) {
                fprintf(log, "rel %.0f %.0f\n", eis.pointer_get_dx(e), eis.pointer_get_dy(e));
            } else if (strcmp(name, "EIS_EVENT_POINTER_MOTION_ABSOLUTE") == 0) {
                fprintf(log, "abs %.2f %.2f\n", eis.pointer_get_absolute_x(e),
                        eis.pointer_get_absolute_y(e));
            } else if (strcmp(name, "EIS_EVENT_SCROLL_DISCRETE") == 0) {
                fprintf(log, "scroll %d %d\n", eis.scroll_get_discrete_dx(e),
                        eis.scroll_get_discrete_dy(e));
            } else if (strcmp(name, "EIS_EVENT_FRAME") == 0) {
                fprintf(log, "# frame\n");
            } else if (name[0] != '\0') {
                fprintf(log, "# %s\n", name);
            }
            eis.event_unref(e);
        }
    }
    fclose(log);
    return 0;
}
