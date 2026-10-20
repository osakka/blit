#define _GNU_SOURCE
/* Destructive console test. ONLY run in the disposable VM booted with the
   explicit blit_vt_test=1 kernel command-line marker. No physical-host mode. */
#include "../../src/blit_vt.h"
#include <errno.h>
#include <dirent.h>
#include <fcntl.h>
#include <linux/input.h>
#include <linux/uinput.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <termios.h>
#include <time.h>
#include <unistd.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "VT_KERNEL_FAIL line=%d errno=%d\n", __LINE__, errno); exit(1); } } while (0)
static int tty;
static int event_fds[2], event_count;
static void key(int device)
{
    struct input_event events[4];
    memset(events, 0, sizeof(events));
    events[0].type = events[2].type = EV_KEY;
    events[0].code = events[2].code = KEY_A;
    events[0].value = 1;
    events[1].type = events[3].type = EV_SYN;
    CHECK(write(device, events, sizeof(events)) == sizeof(events));
    usleep(30000);
    {
        struct input_event event;
        int i, press = 0;
        for (i = 0; i < event_count; ++i)
            while (read(event_fds[i], &event, sizeof(event)) == sizeof(event))
                if (event.type == EV_KEY && event.code == KEY_A && event.value == 1) press = 1;
        CHECK(press); /* Console mute must NOT disable evdev delivery. */
    }
}
static int keyboard(void)
{
    struct uinput_setup setup;
    int fd = open("/dev/uinput", O_WRONLY | O_NONBLOCK | O_CLOEXEC);
    CHECK(fd >= 0);
    CHECK(ioctl(fd, UI_SET_EVBIT, EV_KEY) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_A) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_ENTER) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_LEFTCTRL) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_LEFTALT) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_F1) == 0);
    CHECK(ioctl(fd, UI_SET_KEYBIT, KEY_F2) == 0);
    memset(&setup, 0, sizeof(setup));
    setup.id.bustype = BUS_USB;
    strcpy(setup.name, "blit-vt117-test-keyboard");
    CHECK(ioctl(fd, UI_DEV_SETUP, &setup) == 0);
    CHECK(ioctl(fd, UI_DEV_CREATE) == 0);
    usleep(100000);
    {
        char name[128], path[256];
        DIR *directory;
        struct dirent *entry;
        int event_fd = -1, clock_id = CLOCK_MONOTONIC;
        CHECK(ioctl(fd, UI_GET_SYSNAME(sizeof(name)), name) >= 0);
        snprintf(path, sizeof(path), "/sys/devices/virtual/input/%s", name);
        directory = opendir(path);
        CHECK(directory != NULL);
        while ((entry = readdir(directory))) {
            if (strncmp(entry->d_name, "event", 5)) continue;
            snprintf(path, sizeof(path), "/dev/input/%s", entry->d_name);
            event_fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (event_fd >= 0) break;
        }
        closedir(directory);
        CHECK(event_fd >= 0 && event_count < 2);
        CHECK(ioctl(event_fd, EVIOCSCLOCKID, &clock_id) == 0);
        event_fds[event_count++] = event_fd;
    }
    return fd;
}
static void expect_console(int present)
{
    struct pollfd poller = { .fd = tty, .events = POLLIN };
    char buffer[128];
    int ready = poll(&poller, 1, present ? 1000 : 100);
    CHECK(present ? ready == 1 : ready == 0);
    if (present) CHECK(read(tty, buffer, sizeof(buffer)) > 0 && buffer[0] == 'a');
}
static void token(int fd, char expected)
{
    struct pollfd poller = { .fd = fd, .events = POLLIN };
    char got;
    CHECK(poll(&poller, 1, 2000) == 1);
    CHECK(read(fd, &got, 1) == 1 && got == expected);
}
static pid_t owner(int channel[2])
{
    pid_t child;
    CHECK(socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC, 0, channel) == 0);
    child = fork();
    CHECK(child >= 0);
    if (child == 0) {
        char error[256], command;
        struct pollfd poller = { .fd = channel[1], .events = POLLIN };
        pid_t holder;
        close(channel[0]);
        if (blit_vt_open(error, sizeof(error)) < 0) {
            fprintf(stderr, "VT_KERNEL_OPEN_ERROR %s\n", error);
            CHECK(0);
        }
        /* Simulate a PTY fork inheriting the recovery control descriptor.
           Normal close needs an explicit message; crash needs the pidfd. */
        holder = fork();
        CHECK(holder >= 0);
        if (holder == 0) { sleep(4); _exit(0); }
        CHECK(write(channel[1], "S", 1) == 1);
        for (;;) {
            int pending = blit_vt_pending();
            CHECK(pending >= 0);
            if (pending) {
                CHECK(blit_vt_acknowledge(pending == 2) == 0);
                CHECK(write(channel[1], pending == 1 ? "R" : "A", 1) == 1);
            }
            if (poll(&poller, 1, 10) > 0 && read(channel[1], &command, 1) == 1) {
                if (command == 'q') {
                    blit_vt_close();
                    kill(holder, SIGKILL);
                    waitpid(holder, NULL, 0);
                    _exit(0);
                }
                if (command == 'h') raise(SIGSTOP);
                if (command == 'u') {
                    CHECK(blit_vt_update_lease(1) == 0);
                    blit_vt_json(error, sizeof(error));
                    CHECK(strstr(error, "\"watchdog_timeout_ms\":120000"));
                    CHECK(write(channel[1], "M", 1) == 1);
                    raise(SIGSTOP);
                    CHECK(blit_vt_update_lease(0) == 0);
                }
            }
        }
    }
    close(channel[1]);
    token(channel[0], 'S');
    return child;
}
static void modes_restored(void)
{
    int keyboard_mode = -1, display_mode = -1, tries;
    struct vt_mode mode;
    for (tries = 0; tries < 100; ++tries) {
        CHECK(ioctl(tty, KDGKBMODE, &keyboard_mode) == 0);
        CHECK(ioctl(tty, KDGETMODE, &display_mode) == 0);
        CHECK(ioctl(tty, VT_GETMODE, &mode) == 0);
        if (keyboard_mode == K_UNICODE && display_mode == KD_TEXT && mode.mode == VT_AUTO) return;
        usleep(10000);
    }
    CHECK(0);
}
int main(void)
{
    char cmdline[4096], error[256];
    int fd = open("/proc/cmdline", O_RDONLY), devices[2], channel[2], mode, status;
    struct termios term, saved;
    pid_t child;
    CHECK(fd >= 0);
    memset(cmdline, 0, sizeof(cmdline));
    CHECK(read(fd, cmdline, sizeof(cmdline) - 1) > 0);
    close(fd);
    CHECK(strstr(cmdline, " blit_vt_test=1 ") != NULL);
    tty = open("/dev/tty1", O_RDWR | O_NONBLOCK | O_NOCTTY);
    CHECK(tty >= 0);
    CHECK(ioctl(tty, VT_ACTIVATE, 1) == 0);
    CHECK(tcgetattr(tty, &saved) == 0);
    term = saved; cfmakeraw(&term);
    CHECK(tcsetattr(tty, TCSANOW, &term) == 0);
    CHECK(ioctl(tty, KDSKBMODE, K_UNICODE) == 0);
    devices[0] = keyboard();
    key(devices[0]); expect_console(1); /* negative control: actual leak */
    child = owner(channel);
    CHECK(ioctl(tty, KDGKBMODE, &mode) == 0 && mode == K_OFF);
    key(devices[0]); expect_console(0);
    devices[1] = keyboard(); /* hotplug a second physical input source */
    key(devices[1]); expect_console(0);
    /* A competing owner must not change an already muted VT. */
    CHECK(blit_vt_open(error, sizeof(error)) < 0);
    CHECK(ioctl(tty, KDGKBMODE, &mode) == 0 && mode == K_OFF);
    CHECK(ioctl(tty, VT_ACTIVATE, 2) == 0);
    token(channel[0], 'R');
    CHECK(ioctl(tty, KDGKBMODE, &mode) == 0 && mode == K_UNICODE);
    CHECK(ioctl(tty, VT_ACTIVATE, 1) == 0);
    token(channel[0], 'A');
    key(devices[1]); expect_console(0);
    CHECK(write(channel[0], "q", 1) == 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(channel[0]); modes_restored();
    key(devices[0]); expect_console(1);
    child = owner(channel);
    CHECK(kill(child, SIGKILL) == 0);
    CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status));
    close(channel[0]); modes_restored();
    key(devices[1]); expect_console(1);
    child = owner(channel);
    CHECK(write(channel[0], "u", 1) == 1);
    token(channel[0], 'M');
    CHECK(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status));
    sleep(11); /* longer than the normal watchdog; install lease must hold */
    CHECK(waitpid(child, &status, WNOHANG) == 0);
    CHECK(kill(child, SIGCONT) == 0);
    CHECK(write(channel[0], "q", 1) == 1);
    CHECK(waitpid(child, &status, 0) == child && WIFEXITED(status) && WEXITSTATUS(status) == 0);
    close(channel[0]); modes_restored();
    child = owner(channel);
    CHECK(write(channel[0], "h", 1) == 1);
    CHECK(waitpid(child, &status, WUNTRACED) == child && WIFSTOPPED(status));
    CHECK(waitpid(child, &status, 0) == child && WIFSIGNALED(status) && WTERMSIG(status) == SIGKILL);
    close(channel[0]); modes_restored();
    key(devices[1]); expect_console(1);
    CHECK(tcsetattr(tty, TCSANOW, &saved) == 0);
    ioctl(devices[0], UI_DEV_DESTROY); ioctl(devices[1], UI_DEV_DESTROY);
    close(devices[0]); close(devices[1]); close(tty);
    puts("VT_KERNEL_PASS baseline_leak=observed muted=2_keyboards evdev_delivery=pass hotplug=pass release_acquire=pass normal_restore=pass sigkill_restore=pass inherited_fd=pass competing_owner=refused update_lease=pass hung_owner_watchdog=pass");
    return 0;
}
