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
    c->connector_type_id = id - 92;
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
int drmIoctl(int fd, unsigned long request, void *arg)
{
    static unsigned handle;
    if (request == DRM_IOCTL_MODE_CREATE_DUMB) {
        struct drm_mode_create_dumb *c = arg;
        c->handle = ++handle; c->pitch = c->width * 4; c->size = (uint64_t)c->pitch * c->height;
        ftruncate(fd, (off_t)(handle + 1) * 1048576); trace("allocate", c->handle, (unsigned)c->size);
    } else if (request == DRM_IOCTL_MODE_MAP_DUMB) {
        struct drm_mode_map_dumb *m = arg; m->offset = (uint64_t)m->handle * 1048576;
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
typedef struct { void *data; } Pending;
static Pending pending[256]; static int pending_count;
int drmModePageFlip(int fd, uint32_t crtc, uint32_t fb, uint32_t flags, void *data)
{
    (void)fd; (void)crtc; (void)fb; (void)flags;
    if (pending_count >= 256) return -1;
    pending[pending_count++].data = data; return 0;
}
int drmHandleEvent(int fd, drmEventContextPtr context)
{
    static unsigned sequence; struct timespec now; int i;
    usleep(1000); clock_gettime(CLOCK_MONOTONIC, &now);
    for (i = 0; i < pending_count; ++i)
        context->page_flip_handler(fd, ++sequence, now.tv_sec, now.tv_nsec / 1000, pending[i].data);
    pending_count = 0; return 0;
}
drmModeObjectPropertiesPtr drmModeObjectGetProperties(int fd, uint32_t id, uint32_t type)
{ (void)fd; (void)id; (void)type; return NULL; }
