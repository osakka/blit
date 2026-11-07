/* A stand-in libei.so.1 for frame_input_gate (ADR 0225): every function
   Blit loads (src/blit_frames_input.h), with the libei 1.x signatures, and
   no protocol. A context takes the connected socket Blit hands it and
   closes it when unreferenced.

   Silent (no EI_STUB_LOG): it reports no event and has no descriptor to
   poll, so a route through it is accepted and stays without devices. The
   request's answers and the route's on/off/live state are checked this way.

   Recording (EI_STUB_LOG=<file>): it plays a compositor that offers one seat
   and makes one device of what Blit binds, with one region of
   EI_STUB_REGION (WxH), and writes what Blit sends to the file in the same
   canonical lines as tools/tests/eis_receiver.c; when the peer closes the
   socket Blit connected, it reports a disconnect, as libei does when a
   compositor drops its client. So the gate's behaviour
   cases run on a host with no libeis. It proves Blit's side (which events,
   in which order, with which values); that libei carries them to a real EIS
   server is frame_input_eis_gate, against libeis.

   Built by the gate: tcc -shared -fPIC -o <dir>/libei.so.1 libei_stub.c */
#define _GNU_SOURCE
#include <fcntl.h>
#include <sys/epoll.h>
#include <sys/socket.h>
#include <stdarg.h>
#include <stdbool.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <unistd.h>

/* libei 1.x values, as Blit declares them. */
enum { CONNECT = 1, DISCONNECT = 2, SEAT_ADDED = 3, DEVICE_ADDED = 5, DEVICE_RESUMED = 8 };
enum { CAP_POINTER = 1, CAP_POINTER_ABSOLUTE = 2, CAP_KEYBOARD = 4, CAP_SCROLL = 16, CAP_BUTTON = 32 };

struct ei_seat { struct ei *ei; };
struct ei_device { struct ei *ei; };
struct ei_region { uint32_t width, height; };
struct ei_event { int type; struct ei *ei; };
struct ei {
    int fd;
    int wake[2];
    int poll;          /* epoll over the wake pipe and the socket, as libei's */
    int gone;
    int queue[8];
    int queued;
    unsigned int caps;
    struct ei_seat seat;
    struct ei_device device;
    struct ei_region region;
};

static FILE *record(void)
{
    static FILE *out;
    const char *path = getenv("EI_STUB_LOG");
    if (out == NULL && path != NULL && path[0] != '\0') {
        out = fopen(path, "a");
        if (out != NULL) {
            setvbuf(out, NULL, _IOLBF, 0);
            fprintf(out, "# names=stub\n");
        }
    }
    return out;
}

static void say(const char *format, ...)
{
    FILE *out = record();
    va_list args;
    if (out == NULL) return;
    va_start(args, format);
    vfprintf(out, format, args);
    va_end(args);
    fputc('\n', out);
}

static void post(struct ei *ei, int type)
{
    char byte = 1;
    if (ei->queued < 8) ei->queue[ei->queued++] = type;
    if (write(ei->wake[1], &byte, 1) < 0) { /* already readable */ }
}

struct ei *ei_new_sender(void *user_data)
{
    struct ei *ei = calloc(1, sizeof(*ei));
    (void)user_data;
    if (ei == NULL) return NULL;
    ei->fd = -1;
    ei->wake[0] = ei->wake[1] = -1;
    ei->poll = -1;
    ei->seat.ei = ei;
    ei->device.ei = ei;
    return ei;
}
void ei_configure_name(struct ei *ei, const char *name) { (void)ei; (void)name; }
int ei_setup_backend_fd(struct ei *ei, int fd)
{
    const char *region = getenv("EI_STUB_REGION");
    ei->fd = fd;
    if (record() == NULL) return 0;
    if (pipe2(ei->wake, O_NONBLOCK | O_CLOEXEC) != 0) return -1;
    ei->poll = epoll_create1(EPOLL_CLOEXEC);
    {
        struct epoll_event watch;
        memset(&watch, 0, sizeof(watch));
        watch.events = EPOLLIN;
        if (ei->poll < 0 || epoll_ctl(ei->poll, EPOLL_CTL_ADD, ei->wake[0], &watch) != 0 ||
            epoll_ctl(ei->poll, EPOLL_CTL_ADD, fd, &watch) != 0)
            return -1;
    }
    if (region == NULL || sscanf(region, "%ux%u", &ei->region.width, &ei->region.height) != 2)
        ei->region.width = ei->region.height = 0;
    say("connect");
    post(ei, CONNECT);
    post(ei, SEAT_ADDED);
    return 0;
}
int ei_get_fd(struct ei *ei) { return ei->poll; }
void ei_dispatch(struct ei *ei)
{
    char drain[64];
    if (ei->wake[0] >= 0)
        while (read(ei->wake[0], drain, sizeof(drain)) > 0) {}
    /* The peer closed: the compositor dropped us. */
    if (ei->wake[0] >= 0 && !ei->gone &&
        recv(ei->fd, drain, sizeof(drain), MSG_DONTWAIT | MSG_PEEK) == 0) {
        ei->gone = 1;
        epoll_ctl(ei->poll, EPOLL_CTL_DEL, ei->fd, NULL);
        post(ei, DISCONNECT);
    }
}
struct ei_event *ei_get_event(struct ei *ei)
{
    struct ei_event *event;
    if (ei->queued == 0) return NULL;
    event = calloc(1, sizeof(*event));
    if (event == NULL) return NULL;
    event->type = ei->queue[0];
    event->ei = ei;
    memmove(ei->queue, ei->queue + 1, sizeof(int) * (size_t)--ei->queued);
    return event;
}
struct ei *ei_unref(struct ei *ei)
{
    if (ei == NULL) return NULL;
    if (ei->wake[0] >= 0) {
        say("disconnect");
        close(ei->wake[0]);
        close(ei->wake[1]);
    }
    if (ei->poll >= 0) close(ei->poll);
    if (ei->fd >= 0) close(ei->fd);
    free(ei);
    return NULL;
}
int ei_event_get_type(struct ei_event *event) { return event->type; }
struct ei_seat *ei_event_get_seat(struct ei_event *event) { return &event->ei->seat; }
struct ei_device *ei_event_get_device(struct ei_event *event) { return &event->ei->device; }
struct ei_event *ei_event_unref(struct ei_event *event) { free(event); return NULL; }
void ei_seat_bind_capabilities(struct ei_seat *seat, ...)
{
    static const struct { int cap; const char *name; } order[] = {
        { CAP_KEYBOARD, "keyboard" }, { CAP_POINTER, "pointer" },
        { CAP_POINTER_ABSOLUTE, "pointer_absolute" }, { CAP_BUTTON, "button" },
        { CAP_SCROLL, "scroll" }
    };
    struct ei *ei = seat->ei;
    char bound[128] = "";
    va_list args;
    int cap;
    size_t i;
    va_start(args, seat);
    while ((cap = va_arg(args, int)) > 0)
        ei->caps |= (unsigned int)cap;
    va_end(args);
    for (i = 0; i < sizeof(order) / sizeof(order[0]); ++i)
        if (ei->caps & (unsigned int)order[i].cap) {
            strcat(bound, bound[0] ? "," : "");
            strcat(bound, order[i].name);
        }
    say("bind %s", bound);
    post(ei, DEVICE_ADDED);
    post(ei, DEVICE_RESUMED);
}
bool ei_device_has_capability(struct ei_device *device, int capability)
{
    return (device->ei->caps & (unsigned int)capability) != 0;
}
struct ei_device *ei_device_ref(struct ei_device *device) { return device; }
struct ei_device *ei_device_unref(struct ei_device *device) { (void)device; return NULL; }
struct ei_region *ei_device_get_region(struct ei_device *device, size_t index)
{
    return index == 0 && device->ei->region.width > 0 ? &device->ei->region : NULL;
}
uint32_t ei_region_get_x(struct ei_region *region) { (void)region; return 0; }
uint32_t ei_region_get_y(struct ei_region *region) { (void)region; return 0; }
uint32_t ei_region_get_width(struct ei_region *region) { return region->width; }
uint32_t ei_region_get_height(struct ei_region *region) { return region->height; }
void ei_device_start_emulating(struct ei_device *device, uint32_t sequence) { (void)device; (void)sequence; say("start"); }
void ei_device_stop_emulating(struct ei_device *device) { (void)device; say("stop"); }
void ei_device_frame(struct ei_device *device, uint64_t time) { (void)device; (void)time; say("# frame"); }
void ei_device_keyboard_key(struct ei_device *device, uint32_t keycode, bool is_press)
{
    (void)device;
    say("key %u %s", keycode, is_press ? "press" : "release");
}
void ei_device_pointer_motion(struct ei_device *device, double x, double y) { (void)device; say("rel %.0f %.0f", x, y); }
void ei_device_pointer_motion_absolute(struct ei_device *device, double x, double y)
{
    (void)device;
    say("abs %.2f %.2f", x, y);
}
void ei_device_button_button(struct ei_device *device, uint32_t button, bool is_press)
{
    (void)device;
    say("button %u %s", button, is_press ? "press" : "release");
}
void ei_device_scroll_discrete(struct ei_device *device, int32_t x, int32_t y) { (void)device; say("scroll %d %d", x, y); }
