#define _GNU_SOURCE
#include "blit_vt.h"
#include <errno.h>
#include <fcntl.h>
#include <linux/kd.h>
#include <linux/vt.h>
#include <poll.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <sys/file.h>
#include <sys/ioctl.h>
#include <sys/socket.h>
#include <sys/syscall.h>
#include <sys/wait.h>
#include <time.h>
#include <unistd.h>

static struct {
    int fd;
    int number;
    int keyboard_mode;
    int display_mode;
    struct vt_mode mode;
    struct sigaction old_release;
    struct sigaction old_acquire;
    int signals_installed;
    int active;
    int stop;
    pid_t recovery;
    long long heartbeat_ms;
    int watchdog_timeout_ms;
} vt = { .fd = -1, .stop = -1, .watchdog_timeout_ms = 10000 };
static volatile sig_atomic_t release_pending, acquire_pending;

static long long vt_now_ms(void)
{
    struct timespec now;
    clock_gettime(CLOCK_MONOTONIC, &now);
    return (long long)now.tv_sec * 1000 + now.tv_nsec / 1000000;
}

static void vt_signal(int signal_number)
{
    if (signal_number == SIGUSR1) release_pending = 1;
    if (signal_number == SIGUSR2) acquire_pending = 1;
}

static int restore_vt(void)
{
    int failed = 0;
    if (ioctl(vt.fd, KDSKBMODE, vt.keyboard_mode) < 0) failed = 1;
    if (ioctl(vt.fd, KDSETMODE, vt.display_mode) < 0) failed = 1;
    if (ioctl(vt.fd, VT_SETMODE, &vt.mode) < 0) failed = 1;
    return failed ? -1 : 0;
}

/* Forked BEFORE SDL, DRM monitor and evdev opener threads start. A pidfd
   observes death even if a forked PTY child still holds the stop pipe. Never
   use EOF alone as the crash detector. Do not retain any server/DRM fd. */
static void recover_vt(int death, int command)
{
    struct pollfd fds[2];
    int result;
    int timeout_ms = 10000;
    signal(SIGINT, SIG_IGN);
    signal(SIGTERM, SIG_IGN);
    signal(SIGHUP, SIG_IGN);
    fds[0].fd = death;
    fds[0].events = POLLIN;
    fds[1].fd = command;
    fds[1].events = POLLIN;
    for (;;) {
        char commands[64];
        ssize_t count;
        do { result = poll(fds, 2, timeout_ms); } while (result < 0 && errno == EINTR);
        if (result <= 0) {
            /* A paused/stuck main loop cannot acknowledge VT release. Kill
               its owner and restore the console independently of that loop.
               Hardware/OS hangs and killing this recovery process too are
               outside this process-level recovery boundary. */
            dprintf(2, "blit: console watchdog expired; killing unresponsive server\n");
            syscall(SYS_pidfd_send_signal, death, SIGKILL, NULL, 0);
            break;
        }
        if (fds[0].revents) break;
        count = recv(command, commands, sizeof(commands), 0);
        if (count <= 0 || memchr(commands, 'q', (size_t)count)) break;
        timeout_ms = commands[count - 1] == 'u' ? 120000 : 10000;
    }
    if (restore_vt() < 0) {
        dprintf(2, "blit: console recovery failed: %s\n", strerror(errno));
        result = -1;
    }
    /* Forked descendants can retain the shared open file description until
       exec. Closing just our reference would leave their flock blocking the
       next server even though keyboard recovery already completed. */
    flock(vt.fd, LOCK_UN);
    close(vt.fd);
    _exit(result < 0 ? 1 : 0);
}

int blit_vt_open(char *error, unsigned capacity)
{
    int console = -1, death = -1, channel[2] = {-1, -1};
    int saved_errno;
    struct vt_stat state;
    struct vt_mode mode;
    struct sigaction action;
    char path[32];
    if (vt.fd >= 0) {
        snprintf(error, capacity, "Linux VT ownership: already owned");
        errno = EALREADY;
        return -1;
    }
    console = open("/dev/tty0", O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (console < 0 || ioctl(console, VT_GETSTATE, &state) < 0) goto fail;
    snprintf(path, sizeof(path), "/dev/tty%u", state.v_active);
    vt.number = state.v_active;
    close(console);
    console = -1;
    vt.fd = open(path, O_RDWR | O_NOCTTY | O_CLOEXEC);
    if (vt.fd < 0 || flock(vt.fd, LOCK_EX | LOCK_NB) < 0 ||
        ioctl(vt.fd, VT_GETMODE, &vt.mode) < 0 ||
        ioctl(vt.fd, KDGKBMODE, &vt.keyboard_mode) < 0 ||
        ioctl(vt.fd, KDGETMODE, &vt.display_mode) < 0) goto fail;
    /* Do not steal a compositor's VT, or inherit a previously muted console
       and then claim we know how to restore it. */
    if (vt.mode.mode != VT_AUTO || vt.display_mode != KD_TEXT ||
        (vt.keyboard_mode != K_XLATE && vt.keyboard_mode != K_UNICODE)) {
        errno = EBUSY;
        goto fail;
    }
    if (ioctl(vt.fd, VT_GETSTATE, &state) < 0) goto fail;
    if (state.v_active != vt.number) { errno = EAGAIN; goto fail; }
    /* Linux >=5.3: refusing unsupported recovery is safer than muting a VT
       whose keyboard a SIGKILL could leave disabled. */
    death = (int)syscall(SYS_pidfd_open, getpid(), 0);
    if (death < 0 || socketpair(AF_UNIX, SOCK_STREAM | SOCK_CLOEXEC,
                              0, channel) < 0) goto fail;
    vt.recovery = fork();
    if (vt.recovery < 0) goto fail;
    if (vt.recovery == 0) {
        close(channel[1]);
        recover_vt(death, channel[0]);
    }
    close(death); death = -1;
    close(channel[0]); channel[0] = -1;
    vt.stop = channel[1]; channel[1] = -1;
    memset(&action, 0, sizeof(action));
    action.sa_handler = vt_signal;
    sigemptyset(&action.sa_mask);
    if (sigaction(SIGUSR1, &action, &vt.old_release) < 0) goto fail_owned;
    if (sigaction(SIGUSR2, &action, &vt.old_acquire) < 0) {
        sigaction(SIGUSR1, &vt.old_release, NULL);
        goto fail_owned;
    }
    vt.signals_installed = 1;
    memset(&mode, 0, sizeof(mode));
    mode.mode = VT_PROCESS;
    mode.relsig = SIGUSR1;
    mode.acqsig = SIGUSR2;
    if (ioctl(vt.fd, VT_SETMODE, &mode) < 0 ||
        ioctl(vt.fd, KDSKBMODE, K_OFF) < 0 ||
        ioctl(vt.fd, KDSETMODE, KD_GRAPHICS) < 0) goto fail_owned;
    vt.active = 1;
    vt.heartbeat_ms = 0;
    vt.watchdog_timeout_ms = 10000;
    return 0;
fail_owned:
    saved_errno = errno;
    blit_vt_close();
    errno = saved_errno;
fail:
    saved_errno = errno;
    if (console >= 0) close(console);
    if (death >= 0) close(death);
    if (channel[0] >= 0) close(channel[0]);
    if (channel[1] >= 0) close(channel[1]);
    if (vt.fd >= 0) close(vt.fd);
    vt.fd = -1;
    snprintf(error, capacity, "Linux VT ownership: %s", strerror(saved_errno));
    errno = saved_errno;
    return -1;
}

void blit_vt_close(void)
{
    int status;
    if (vt.fd < 0) return;
    /* Restore synchronously, then let the recovery process restore and exit
       before another server can claim the VT. It still holds the flock. */
    if (restore_vt() < 0)
        fprintf(stderr, "blit: console restore failed: %s\n", strerror(errno));
    if (vt.stop >= 0) {
        /* Explicit command: descendants may inherit the descriptor until
           exec, so closing it alone is not a synchronous shutdown request. */
        send(vt.stop, "q", 1, MSG_NOSIGNAL);
        close(vt.stop);
        vt.stop = -1;
    }
    if (vt.recovery > 0) {
        while (waitpid(vt.recovery, &status, 0) < 0 && errno == EINTR) {}
        vt.recovery = 0;
    }
    flock(vt.fd, LOCK_UN);
    close(vt.fd);
    vt.fd = -1;
    vt.active = 0;
    vt.watchdog_timeout_ms = 10000;
    if (vt.signals_installed) {
        sigaction(SIGUSR1, &vt.old_release, NULL);
        sigaction(SIGUSR2, &vt.old_acquire, NULL);
        vt.signals_installed = 0;
    }
    release_pending = acquire_pending = 0;
}

int blit_vt_owned(void) { return vt.fd >= 0; }
int blit_vt_active(void) { return vt.fd < 0 || vt.active; }
int blit_vt_number(void) { return vt.fd >= 0 ? vt.number : 0; }

int blit_vt_pending(void)
{
    int status;
    pid_t reaped;
    if (vt.fd < 0) return 0;
    if (vt_now_ms() - vt.heartbeat_ms >= 1000) {
        if (send(vt.stop, "h", 1, MSG_NOSIGNAL | MSG_DONTWAIT) == 1)
            vt.heartbeat_ms = vt_now_ms();
    }
    reaped = vt.recovery > 0 ? waitpid(vt.recovery, &status, WNOHANG) : 0;
    if (reaped > 0 || (reaped < 0 && errno == ECHILD)) {
        vt.recovery = 0;
        return -1;
    }
    if (release_pending) { release_pending = 0; return 1; }
    if (acquire_pending) { acquire_pending = 0; return 2; }
    return 0;
}

int blit_vt_acknowledge(int acquire)
{
    if (vt.fd < 0) return -1;
    if (acquire) {
        if (ioctl(vt.fd, KDSETMODE, KD_GRAPHICS) < 0 ||
            ioctl(vt.fd, KDSKBMODE, K_OFF) < 0 ||
            ioctl(vt.fd, VT_RELDISP, VT_ACKACQ) < 0) return -1;
        vt.active = 1;
    } else {
        vt.active = 0;
        if (ioctl(vt.fd, KDSKBMODE, vt.keyboard_mode) < 0 ||
            ioctl(vt.fd, KDSETMODE, KD_TEXT) < 0 ||
            ioctl(vt.fd, VT_RELDISP, 1) < 0) return -1;
    }
    return 0;
}

int blit_vt_switch(int number)
{
    if (vt.fd < 0 || !vt.active || number < 1 || number > 63) {
        errno = EINVAL;
        return -1;
    }
    return ioctl(vt.fd, VT_ACTIVATE, number);
}

int blit_vt_update_lease(int enabled)
{
    if (vt.fd < 0) return 0;
    if (send(vt.stop, enabled ? "u" : "h", 1, MSG_NOSIGNAL) != 1) return -1;
    vt.watchdog_timeout_ms = enabled ? 120000 : 10000;
    return 0;
}

void blit_vt_json(char *body, unsigned capacity)
{
    int keyboard_mode = -1;
    if (vt.fd >= 0) ioctl(vt.fd, KDGKBMODE, &keyboard_mode);
    snprintf(body, capacity,
        "{\"owner\":\"%s\",\"vt_number\":%d,\"active\":%s,"
        "\"keyboard_muted\":%s,\"keyboard_mode\":%d,"
        "\"recovery_process\":%s,\"watchdog_timeout_ms\":%d,"
        "\"evdev_exclusive\":false}",
        vt.fd >= 0 ? "native" : "unmanaged", blit_vt_number(),
        vt.fd >= 0 && vt.active ? "true" : "false",
        keyboard_mode == K_OFF ? "true" : "false", keyboard_mode,
        vt.recovery > 0 ? "true" : "false", vt.watchdog_timeout_ms);
}
