/* Test-only libdrm boundary. The unmodified gateway/KMS lifecycle, worker,
   buffer mappings, topology and HTTP loop run against this deterministic GPU. */
#define _GNU_SOURCE
#include <xf86drm.h>
#include <xf86drmMode.h>
#include <libdrm/drm.h>
#include <stdlib.h>
#include <stdio.h>
#include <string.h>
#include <unistd.h>
#include <time.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stdint.h>
#include <sys/timerfd.h>
#include <sys/mman.h>

/* The device is a timerfd (issue #144), readable exactly when a page flip
   has completed -- at once, or at its vblank in periodic mode -- as a real
   DRM fd is. A regular file is always readable, so the server handled
   events on every loop pass and this mock slept 1 ms in drmHandleEvent to
   stop it spinning, capping the loop near 1000 passes a second. Every other
   open goes to the C library. */
static int mock_fds[16], mock_fd_count;
static int mock_device(const char *path)
{
    const char *device = getenv("BLIT_DRM_DEVICE");
    int fd;
    if (device == NULL || path == NULL || strcmp(path, device) != 0)
        return -2;
    fd = timerfd_create(CLOCK_MONOTONIC, TFD_NONBLOCK | TFD_CLOEXEC);
    if (fd >= 0 && mock_fd_count < 16) mock_fds[mock_fd_count++] = fd;
    return fd;
}
static int mock_is_device(int fd)
{
    int i;
    for (i = 0; i < mock_fd_count; ++i) if (mock_fds[i] == fd) return 1;
    return 0;
}
/* A dumb buffer is mapped through the device fd; the mock's is plain
   memory, as a real dumb buffer is, so the map is anonymous. */
void *mmap(void *address, size_t length, int protection, int flags, int fd, off_t offset)
{
    static void *(*real)(void *, size_t, int, int, int, off_t);
    if (real == NULL) real = (void *(*)(void *, size_t, int, int, int, off_t))dlsym(RTLD_NEXT, "mmap");
    if (fd >= 0 && mock_is_device(fd))
        return real(address, length, protection, MAP_SHARED | MAP_ANONYMOUS, -1, 0);
    return real(address, length, protection, flags, fd, offset);
}
void *mmap64(void *address, size_t length, int protection, int flags, int fd, off_t offset)
{
    return mmap(address, length, protection, flags, fd, offset);
}
int open(const char *path, int flags, ...)
{
    static int (*real)(const char *, int, ...);
    mode_t mode = 0;
    int fd = mock_device(path);
    if (fd != -2) return fd;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list list; va_start(list, flags); mode = (mode_t)va_arg(list, int); va_end(list);
    }
    if (real == NULL) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open");
    return real(path, flags, mode);
}
int open64(const char *path, int flags, ...)
{
    static int (*real)(const char *, int, ...);
    mode_t mode = 0;
    int fd = mock_device(path);
    if (fd != -2) return fd;
    if (flags & (O_CREAT | O_TMPFILE)) {
        va_list list; va_start(list, flags); mode = (mode_t)va_arg(list, int); va_end(list);
    }
    if (real == NULL) real = (int (*)(const char *, int, ...))dlsym(RTLD_NEXT, "open64");
    return real(path, flags, mode);
}

typedef struct { int count, fail_add, fail_remove, delay; unsigned ids[16]; int width[16], height[16]; } MockState;
static MockState state(void)
{
    MockState s = {0}; int i;
    FILE *f = fopen(getenv("BLIT_DRM_MOCK_STATE"), "r");
    if (f == NULL) return s;
    fscanf(f, "%d %d %d %d", &s.count, &s.fail_add, &s.fail_remove, &s.delay);
    if (s.count < -1 || s.count > 16) s.count = 0;
    for (i = 0; i < s.count; ++i) fscanf(f, "%u %d %d", &s.ids[i], &s.width[i], &s.height[i]);
    fclose(f); return s;
}
static void trace(const char *kind, unsigned a, unsigned b)
{
    const char *path = getenv("BLIT_DRM_MOCK_TRACE");
    FILE *f = path ? fopen(path, "a") : NULL;
    if (f) { fprintf(f, "%s %u %u\n", kind, a, b); fclose(f); }
}
drmModeResPtr drmModeGetResources(int fd)
{
    MockState s = state(); int i; (void)fd;
    drmModeResPtr r = calloc(1, sizeof(*r));
    if (s.delay) { trace("probe-start", s.delay, 0); usleep(s.delay * 1000); trace("probe-end", s.delay, 0); }
    if (s.count == -1) { free(r); return NULL; }
    r->count_connectors = s.count; r->connectors = calloc(16, sizeof(uint32_t));
    r->count_crtcs = 16; r->crtcs = calloc(16, sizeof(uint32_t));
    for (i = 0; i < s.count; ++i) r->connectors[i] = s.ids[i];
    for (i = 0; i < 16; ++i) r->crtcs[i] = 100 + i;
    return r;
}
drmModeConnectorPtr drmModeGetConnector(int fd, uint32_t id)
{
    MockState s = state(); int i; (void)fd;
    drmModeConnectorPtr c = calloc(1, sizeof(*c));
    c->connector_id = id; c->connector_type = DRM_MODE_CONNECTOR_DisplayPort;
    c->connector_type_id = id >= 93 ? id - 92 : id;
    for (i = 0; i < s.count; ++i) if (s.ids[i] == id) {
        if (s.fail_add == -1000 - (int)id) { c->connection = DRM_MODE_UNKNOWNCONNECTION; break; }
        c->connection = DRM_MODE_CONNECTED; c->count_modes = 1;
        c->modes = calloc(1, sizeof(*c->modes));
        c->modes[0].hdisplay = s.width[i]; c->modes[0].vdisplay = s.height[i];
        c->modes[0].vrefresh = 60; c->modes[0].type = DRM_MODE_TYPE_PREFERRED;
        c->encoder_id = id; break;
    }
    return c;
}
drmModeEncoderPtr drmModeGetEncoder(int fd, uint32_t id)
{
    drmModeEncoderPtr e = calloc(1, sizeof(*e)); (void)fd;
    e->encoder_id = id; e->possible_crtcs = 1U << ((id - 93) % 16); return e;
}
drmModeCrtcPtr drmModeGetCrtc(int fd, uint32_t id)
{
    drmModeCrtcPtr c = calloc(1, sizeof(*c)); (void)fd; c->crtc_id = id; return c;
}
int drmModeSetCrtc(int fd, uint32_t crtc, uint32_t fb, uint32_t x, uint32_t y,
                    uint32_t *connectors, int count, drmModeModeInfoPtr mode)
{
    MockState s = state(); (void)fd; (void)x; (void)y; (void)mode;
    if (fb && count && connectors && (int)connectors[0] == s.fail_add) return -1;
    if (!fb && (int)crtc == s.fail_remove) return -1;
    trace(fb ? "modeset" : "disable", crtc, fb); return 0;
}
/* One sparse slot per dumb buffer, large enough for 4096x4096 at 32 bpp. */
#define MOCK_SLOT ((off_t)64 << 20)
int drmIoctl(int fd, unsigned long request, void *arg)
{
    static unsigned handle;
    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        struct drm_mode_create_dumb *c = arg;
        c->handle = ++handle; c->pitch = c->width * 4; c->size = (uint64_t)c->pitch * c->height;
        ftruncate(fd, (off_t)(handle + 1) * MOCK_SLOT); trace("allocate", c->handle, (unsigned)c->size);
    } else if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        struct drm_mode_map_dumb *m = arg; m->offset = (uint64_t)m->handle * (uint64_t)MOCK_SLOT;
    } else if (request == DRM_IOCTL_MODE_DESTROY_DUMB) {
        trace("free", ((struct drm_mode_destroy_dumb *)arg)->handle, 0);
    }
    return 0;
}
int drmModeAddFB(int fd, uint32_t w, uint32_t h, uint8_t d, uint8_t b,
                  uint32_t pitch, uint32_t handle, uint32_t *fb)
{
    (void)fd; (void)w; (void)h; (void)d; (void)b; (void)pitch; *fb = handle; return 0;
}
int drmModeRmFB(int fd, uint32_t fb) { (void)fd; (void)fb; return 0; }
typedef struct { void *data; unsigned long long submitted_us; } Pending;
static Pending pending[256]; static int pending_count;
static unsigned long long mock_now_us(void)
{
    struct timespec now; clock_gettime(CLOCK_MONOTONIC, &now);
    return (unsigned long long)now.tv_sec * 1000000ULL + (unsigned long long)now.tv_nsec / 1000ULL;
}
/* BLIT_DRM_MOCK_VBLANK_HZ makes flips behave like a display: each completes
   at the first vblank boundary (a multiple of the period on the monotonic
   clock) after its submission, stamped with that vblank's time and number.
   Unset, a flip completes at the next event poll, as before. */
static unsigned long long mock_period_us(void)
{
    const char *hz = getenv("BLIT_DRM_MOCK_VBLANK_HZ");
    double value = hz ? atof(hz) : 0.0;
    return value >= 1.0 ? (unsigned long long)(1000000.0 / value) : 0ULL;
}
/* The device becomes readable at the earliest pending completion: its
   vblank in periodic mode, otherwise at once. Nothing pending, it is not. */
static void mock_arm(int fd)
{
    struct itimerspec when;
    unsigned long long period = mock_period_us(), due = 0;
    int i;
    memset(&when, 0, sizeof(when));
    for (i = 0; i < pending_count; ++i) {
        unsigned long long at = period ?
            (pending[i].submitted_us / period + 1) * period : pending[i].submitted_us;
        if (due == 0 || at < due) due = at;
    }
    if (pending_count > 0) {
        if (due == 0) due = 1;
        when.it_value.tv_sec = (time_t)(due / 1000000ULL);
        when.it_value.tv_nsec = (long)(due % 1000000ULL) * 1000L;
    }
    timerfd_settime(fd, TFD_TIMER_ABSTIME, &when, NULL);
}
int drmModePageFlip(int fd, uint32_t crtc, uint32_t fb, uint32_t flags, void *data)
{
    (void)crtc; (void)fb; (void)flags;
    if (pending_count >= 256) return -1;
    pending[pending_count].data = data;
    pending[pending_count++].submitted_us = mock_now_us();
    mock_arm(fd);
    return 0;
}
/* In periodic mode, the vblank number now is the count of whole periods. */
int drmCrtcGetSequence(int fd, uint32_t crtc, uint64_t *sequence, uint64_t *ns)
{
    unsigned long long period = mock_period_us(), now = mock_now_us();
    (void)fd; (void)crtc;
    if (!period) return -1;
    if (sequence) *sequence = now / period;
    if (ns) *ns = (now / period) * period * 1000ULL;
    return 0;
}
int drmHandleEvent(int fd, drmEventContextPtr context)
{
    static unsigned sequence; struct timespec now; int i, kept = 0;
    unsigned long long period = mock_period_us();
    uint64_t expirations;
    if (read(fd, &expirations, sizeof(expirations)) < 0) { /* not due yet, or not a timerfd */ }
    clock_gettime(CLOCK_MONOTONIC, &now);
    if (period) {
        unsigned long long at = mock_now_us();
        for (i = 0; i < pending_count; ++i) {
            unsigned long long vblank = (pending[i].submitted_us / period + 1) * period;
            if (at >= vblank)
                context->page_flip_handler(fd, (unsigned)(vblank / period),
                    (unsigned)(vblank / 1000000ULL), (unsigned)(vblank % 1000000ULL),
                    pending[i].data);
            else
                pending[kept++] = pending[i];
        }
        pending_count = kept;
        mock_arm(fd);
        return 0;
    }
    for (i = 0; i < pending_count; ++i)
        context->page_flip_handler(fd, ++sequence, now.tv_sec, now.tv_nsec / 1000, pending[i].data);
    pending_count = 0;
    mock_arm(fd);
    return 0;
}
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t id, uint32_t type)
{ (void)fd; (void)id; (void)type; return NULL; }
