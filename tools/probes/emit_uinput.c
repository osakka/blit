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
        ioctl(fd, UI_SET_KEYBIT, KEY_LEFTSHIFT);
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

static int make_gamepad(void)
{
    struct uinput_user_dev device;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK);
    if (fd < 0) return -1;
    ioctl(fd, UI_SET_EVBIT, EV_SYN);
    ioctl(fd, UI_SET_EVBIT, EV_KEY);
    ioctl(fd, UI_SET_EVBIT, EV_ABS);
    ioctl(fd, UI_SET_KEYBIT, BTN_GAMEPAD);
    ioctl(fd, UI_SET_KEYBIT, BTN_SOUTH);
    ioctl(fd, UI_SET_KEYBIT, BTN_EAST);
    ioctl(fd, UI_SET_ABSBIT, ABS_X);
    ioctl(fd, UI_SET_ABSBIT, ABS_Y);
    memset(&device, 0, sizeof(device));
    snprintf(device.name, sizeof(device.name), "%s", "Blit test gamepad");
    device.id.bustype = BUS_USB;
    device.id.vendor = 0x1d50;
    device.id.product = 0x0003;
    device.id.version = 1;
    device.absmin[ABS_X] = -32768;
    device.absmax[ABS_X] = 32767;
    device.absmin[ABS_Y] = -32768;
    device.absmax[ABS_Y] = 32767;
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

int main(int argc, char **argv)
{
    int keyboard = make_device("Blit test keyboard", 0);
    int mouse = make_device("Blit test mouse", 1);
    int mouse2 = make_device("Blit test mouse 2", 1);
    int gamepad = make_gamepad();
    int i;
    int hold_after_events = argc > 1 && strcmp(argv[1], "--hold") == 0;
    int flood = argc > 1 && strcmp(argv[1], "--flood") == 0;
    if (keyboard < 0 || mouse < 0 || mouse2 < 0 || gamepad < 0) return 1;
    sleep(8);
    if (flood) {
        /* Keep one physical mouse continuously readable long enough to prove
           that the gateway returns to the main loop instead of draining an
           unbounded evdev descriptor in one turn. The normal paced workload
           below remains unchanged for the ordinary input gate. */
        for (i = 0; i < 5000; ++i) {
            emit_event(mouse, EV_REL, REL_X, (i & 1) ? 1 : -1);
            sync_event(mouse);
            usleep(1000);
        }
    }
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
    emit_event(gamepad, EV_ABS, ABS_X, 12000);
    sync_event(gamepad);
    emit_event(gamepad, EV_KEY, BTN_SOUTH, 1);
    sync_event(gamepad);
    emit_event(gamepad, EV_KEY, BTN_SOUTH, 0);
    sync_event(gamepad);
    /* Exercise the presentation path at a deterministic 60 Hz.  The old
       five-event, two-second cadence verified delivery but could not expose
       a renderer that repainted the whole terminal for every motion. */
    for (i = 0; i < 180; ++i) {
        int delta_x = (i == 1) ? 1200 : (i == 2 ? -1200 :
            ((i & 1) ? 8 : -8));
        emit_event(mouse, EV_REL, REL_X, delta_x);
        emit_event(mouse, EV_REL, REL_Y, (i & 2) ? 4 : -4);
        sync_event(mouse);
        if ((i % 2) == 0) {
            emit_event(mouse2, EV_REL, REL_X, (i & 4) ? 2 : -2);
            emit_event(mouse2, EV_REL, REL_Y, (i & 8) ? 1 : -1);
            sync_event(mouse2);
        }
        if (i == 1) {
            emit_event(mouse, EV_KEY, BTN_LEFT, 1);
            sync_event(mouse);
            /* EV_KEY value 2 is an autorepeat, not another click. */
            emit_event(mouse, EV_KEY, BTN_LEFT, 2);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_LEFT, 0);
            sync_event(mouse);
            /* Two physical devices share one logical pointer. The second
               press and the first release must not create duplicate logical
               transitions while the other device still holds the button. */
            emit_event(mouse, EV_KEY, BTN_LEFT, 1);
            sync_event(mouse);
            /* Cross-device uinput delivery is asynchronous under QEMU/TCG;
               keep both physical buttons held long enough for the reader to
               observe the overlap before releasing the first device. */
            usleep(1000000);
            emit_event(mouse2, EV_KEY, BTN_LEFT, 1);
            sync_event(mouse2);
            usleep(1000000);
            emit_event(mouse, EV_KEY, BTN_LEFT, 0);
            sync_event(mouse);
            usleep(1000000);
            emit_event(mouse, EV_REL, REL_X, 3);
            sync_event(mouse);
            emit_event(mouse2, EV_KEY, BTN_LEFT, 0);
            sync_event(mouse2);
            usleep(500000);
            emit_event(keyboard, EV_KEY, KEY_LEFTSHIFT, 1);
            sync_event(keyboard);
            usleep(500000);
            emit_event(mouse, EV_KEY, BTN_LEFT, 1);
            sync_event(mouse);
            emit_event(mouse, EV_KEY, BTN_LEFT, 0);
            sync_event(mouse);
            usleep(500000);
            emit_event(keyboard, EV_KEY, KEY_LEFTSHIFT, 0);
            sync_event(keyboard);
        }
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
            emit_event(gamepad, EV_ABS, ABS_Y, (i & 1) ? 16000 : -16000);
            sync_event(gamepad);
            emit_event(gamepad, EV_KEY, BTN_EAST, 1);
            sync_event(gamepad);
            emit_event(gamepad, EV_KEY, BTN_EAST, 0);
            sync_event(gamepad);
        }
        usleep(16000);
    }
    if (hold_after_events) sleep(20);
    ioctl(keyboard, UI_DEV_DESTROY);
    ioctl(mouse, UI_DEV_DESTROY);
    ioctl(mouse2, UI_DEV_DESTROY);
    ioctl(gamepad, UI_DEV_DESTROY);
    close(keyboard);
    close(mouse);
    close(mouse2);
    close(gamepad);
    return 0;
}
