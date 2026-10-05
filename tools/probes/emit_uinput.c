/* Deterministic in-guest evdev producer for the QEMU input gate. */
#include <linux/input.h>
#include <linux/uinput.h>
#include <fcntl.h>
#include <stdio.h>
#include <string.h>
#include <sys/ioctl.h>
#include <unistd.h>

static int make_device(const char *name, int mouse)
{
    struct uinput_user_dev device;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, mouse ? EV_REL : EV_KEY);
    if (mouse) {
        ioctl(fd, UI_SET_RELBIT, REL_X);
        ioctl(fd, UI_SET_RELBIT, REL_Y);
        ioctl(fd, UI_SET_RELBIT, REL_WHEEL);
        ioctl(fd, UI_SET_EVBIT, EV_KEY);
        ioctl(fd, UI_SET_KEYBIT, BTN_LEFT);
        ioctl(fd, UI_SET_KEYBIT, BTN_MIDDLE);
        ioctl(fd, UI_SET_KEYBIT, BTN_RIGHT);
    } else {
        ioctl(fd, UI_SET_KEYBIT, KEY_A);
        ioctl(fd, UI_SET_KEYBIT, KEY_ENTER);
        ioctl(fd, UI_SET_KEYBIT, KEY_LEFTBRACE);
        ioctl(fd, UI_SET_KEYBIT, KEY_UP);
        ioctl(fd, UI_SET_KEYBIT, KEY_KP1);
    }
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "%s", name);
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1d50;
    device.id.product = mouse ? 0x0002 : 0x0001;
    device.id.version = 1;
    if (write(fd, &device, sizeof(device)) != (ssize_t)sizeof(device) ||
        ioctl(fd, UI_DEV_CREATE) < 0) {
        close(fd);
        return -1;
    }
    return fd;
}

static void emit_event(int fd, unsigned short type, unsigned short code,
                       int value)
{
    struct input_event event;
    memset(&event, 0, sizeof(event));
    event.type = type;
    event.code = code;
    event.value = value;
    write(fd, &event, sizeof(event));
}

static void sync_event(int fd)
{
    emit_event(fd, EV_SYN, SYN_REPORT, 0);
}

int main(void)
{
    int keyboard = make_device("Blit test keyboard", 0);
    int mouse = make_device("Blit test mouse", 1);
    int i;
    if (keyboard < 0 || mouse < 0) return 1;
    sleep(8);
    emit_event(keyboard, EV_KEY, KEY_LEFTBRACE, 1);
    sync_event(keyboard);
    emit_event(keyboard, EV_KEY, KEY_LEFTBRACE, 0);
    sync_event(keyboard);
    emit_event(keyboard, EV_KEY, KEY_UP, 1);
    sync_event(keyboard);
    emit_event(keyboard, EV_KEY, KEY_UP, 0);
    sync_event(keyboard);
    emit_event(keyboard, EV_KEY, KEY_KP1, 1);
    sync_event(keyboard);
    emit_event(keyboard, EV_KEY, KEY_KP1, 0);
    sync_event(keyboard);
    /* Exercise the presentation path at a deterministic 60 Hz.  The old
       five-event, two-second cadence verified delivery but could not expose
       a renderer that repainted the whole terminal for every motion. */
    for (i = 0; i < 180; ++i) {
        int delta_x = (i == 1) ? 1200 : (i == 2 ? -1200 :
            ((i & 1) ? 8 : -8));
        emit_event(mouse, EV_REL, REL_X, delta_x);
        emit_event(mouse, EV_REL, REL_Y, (i & 2) ? 4 : -4);
        sync_event(mouse);
        if ((i % 30) == 0) {
            emit_event(keyboard, EV_KEY, KEY_A, 1);
            sync_event(keyboard);
            emit_event(keyboard, EV_KEY, KEY_A, 0);
            sync_event(keyboard);
            emit_event(mouse, EV_KEY, BTN_LEFT, 1);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_RIGHT, 1);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_MIDDLE, 1);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_LEFT, 0);
            sync_event(mouse);
            emit_event(mouse, EV_REL, REL_X, 1);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_RIGHT, 0);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_MIDDLE, 0);
            sync_event(mouse);
            emit_event(mouse, EV_REL, REL_WHEEL, 1);
            sync_event(mouse);
        }
        usleep(16000);
    }
    ioctl(keyboard, UI_DEV_DESTROY);
    ioctl(mouse, UI_DEV_DESTROY);
    close(keyboard);
    close(mouse);
    return 0;
}
