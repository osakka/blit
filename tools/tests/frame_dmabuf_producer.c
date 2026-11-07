/* ADR 0223: a test producer of dmabuf frames, for frame_dmabuf_live_gate.sh.

   Runs inside a Blit session on a host with a GPU, so it has the session's
   BLIT_FRAME_SOCKET and BLIT_FRAME_TOKEN. It allocates a pool of three
   LINEAR XRGB8888 buffers with GBM on the render node, fills each frame
   with a solid colour through gbm_bo_map, and sends it by version 3 of the
   frame channel (FRM3: one plane, its dmabuf in SCM_RIGHTS). The image is
   placed with a Kitty escape on its own terminal, as ADR 0202 does. A
   buffer is reused only after Blit answered RELEASE for the frame in it.

   It prints one machine-readable line, DMABUF_RESULT, with what it saw:
   the hello's answer, frames sent and accepted, every RELEASE in order,
   any ERROR, and whether the checks held:
     - the hello offers dmabuf=1 and lists XR24 with the LINEAR modifier;
     - every frame but the newest is released, in sequence order, and only
       after a newer frame was sent (Blit holds the frame it shows);
     - a frame with a modifier Blit did not list is refused ENOTSUP;
     - deleting the image (a=d,d=I) releases the frame it still showed.
   Then it holds the last colour for `hold` seconds so the display can be
   captured, deletes the image and exits.

   GBM is loaded with dlopen, so the binary needs only libc and libdl:
     tcc -I build/deps/prefix/include tools/tests/frame_dmabuf_producer.c -ldl
   Usage: frame_dmabuf_producer [probe | run FRAMES HOLD_SECONDS]  */

#define _GNU_SOURCE
#define EGL_EGL_PROTOTYPES 0
#define GL_GLES_PROTOTYPES 0
#include <EGL/egl.h>
#include <EGL/eglext.h>
#include <GLES2/gl2.h>
#include <GLES2/gl2ext.h>
#include <dlfcn.h>
#include <errno.h>
#include <fcntl.h>
#include <gbm.h>
#include <poll.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/socket.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>

#define FOURCC(a, b, c, d) \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)
#define POOL 3
#define WIDTH 256
#define HEIGHT 128
#define IMAGE_ID 4242

static struct gbm_device *(*p_create_device)(int);
static struct gbm_bo *(*p_bo_create)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t);
static void *(*p_bo_map)(struct gbm_bo *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                         uint32_t *, void **);
static void (*p_bo_unmap)(struct gbm_bo *, void *);
static int (*p_bo_get_fd)(struct gbm_bo *);
static uint32_t (*p_bo_get_stride)(struct gbm_bo *);
static uint64_t (*p_bo_get_modifier)(struct gbm_bo *);

static char result[4096];
static size_t result_at;
static char releases[2048];
static size_t releases_at;
static char errors[1024];
static size_t errors_at;

static void note(char *buffer, size_t capacity, size_t *at, const char *text)
{
    if (*at < capacity)
        *at += (size_t)snprintf(buffer + *at, capacity - *at, "%s%s", *at ? "," : "", text);
}

static void finish(const char *verdict)
{
    printf("\nDMABUF_RESULT verdict=%s %s releases=[%s] errors=[%s]\n", verdict, result,
           releases, errors);
    fflush(stdout);
}

static void put32(unsigned char *p, uint32_t v)
{
    p[0] = (unsigned char)v; p[1] = (unsigned char)(v >> 8);
    p[2] = (unsigned char)(v >> 16); p[3] = (unsigned char)(v >> 24);
}

static void put64(unsigned char *p, uint64_t v)
{
    put32(p, (uint32_t)v);
    put32(p + 4, (uint32_t)(v >> 32));
}

static double now_s(void)
{
    struct timespec t;
    clock_gettime(CLOCK_MONOTONIC, &t);
    return (double)t.tv_sec + t.tv_nsec / 1e9;
}

static int load_gbm(void)
{
    void *gbm = dlopen("libgbm.so.1", RTLD_NOW);
    if (gbm == NULL) return 0;
    p_create_device = (struct gbm_device *(*)(int))dlsym(gbm, "gbm_create_device");
    p_bo_create = (struct gbm_bo *(*)(struct gbm_device *, uint32_t, uint32_t, uint32_t, uint32_t))
        dlsym(gbm, "gbm_bo_create");
    p_bo_map = (void *(*)(struct gbm_bo *, uint32_t, uint32_t, uint32_t, uint32_t, uint32_t,
                          uint32_t *, void **))dlsym(gbm, "gbm_bo_map");
    p_bo_unmap = (void (*)(struct gbm_bo *, void *))dlsym(gbm, "gbm_bo_unmap");
    p_bo_get_fd = (int (*)(struct gbm_bo *))dlsym(gbm, "gbm_bo_get_fd");
    p_bo_get_stride = (uint32_t (*)(struct gbm_bo *))dlsym(gbm, "gbm_bo_get_stride");
    p_bo_get_modifier = (uint64_t (*)(struct gbm_bo *))dlsym(gbm, "gbm_bo_get_modifier");
    return p_create_device && p_bo_create && p_bo_map && p_bo_unmap && p_bo_get_fd &&
        p_bo_get_stride && p_bo_get_modifier;
}

static int fill(struct gbm_bo *bo, uint32_t xrgb)
{
    uint32_t stride = 0, x, y;
    void *state = NULL;
    unsigned char *map = (unsigned char *)p_bo_map(bo, 0, 0, WIDTH, HEIGHT,
                                                   GBM_BO_TRANSFER_WRITE, &stride, &state);
    if (map == NULL) return 0;
    for (y = 0; y < HEIGHT; ++y)
        for (x = 0; x < WIDTH; ++x)
            memcpy(map + (size_t)y * stride + (size_t)x * 4, &xrgb, 4);
    p_bo_unmap(bo, state);
    return 1;
}

/* One FRM3: the bo's single plane, and the acquire fence if `fence` >= 0
   (closed here once sent). */
static int send_frame_ex(int sock, struct gbm_bo *bo, unsigned int image_id, uint32_t fourcc,
                         unsigned long long sequence, uint64_t modifier, int fence)
{
    unsigned char header[72];
    struct msghdr message;
    struct iovec vector;
    union { struct cmsghdr h; char space[CMSG_SPACE(sizeof(int) * 2)]; } control;
    struct cmsghdr *cmsg;
    int fds[2], fd = p_bo_get_fd(bo), ok, n = fence >= 0 ? 2 : 1;
    if (fd < 0) return 0;
    fds[0] = fd;
    fds[1] = fence;
    memset(header, 0, sizeof(header));
    memcpy(header, "FRM3", 4);
    put32(header + 4, image_id);
    put64(header + 8, sequence);
    put32(header + 16, WIDTH);
    put32(header + 20, HEIGHT);
    put32(header + 24, fourcc);
    put32(header + 28, 1);
    put64(header + 32, modifier);
    put32(header + 40, 0);
    put32(header + 44, p_bo_get_stride(bo));
    memset(&message, 0, sizeof(message));
    vector.iov_base = header;
    vector.iov_len = sizeof(header);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control.space;
    message.msg_controllen = CMSG_SPACE(sizeof(int) * n);
    cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int) * n);
    memcpy(CMSG_DATA(cmsg), fds, sizeof(int) * n);
    ok = sendmsg(sock, &message, MSG_NOSIGNAL) == (ssize_t)sizeof(header);
    close(fd);
    if (fence >= 0) close(fence);
    return ok;
}

static int send_frame(int sock, struct gbm_bo *bo, unsigned long long sequence, uint64_t modifier)
{
    unsigned char header[72];
    struct msghdr message;
    struct iovec vector;
    union { struct cmsghdr h; char space[CMSG_SPACE(sizeof(int))]; } control;
    struct cmsghdr *cmsg;
    int fd = p_bo_get_fd(bo), ok;
    if (fd < 0) return 0;
    memset(header, 0, sizeof(header));
    memcpy(header, "FRM3", 4);
    put32(header + 4, IMAGE_ID);
    put64(header + 8, sequence);
    put32(header + 16, WIDTH);
    put32(header + 20, HEIGHT);
    put32(header + 24, FOURCC('X', 'R', '2', '4'));
    put32(header + 28, 1);
    put64(header + 32, modifier);
    put32(header + 40, 0);
    put32(header + 44, p_bo_get_stride(bo));
    memset(&message, 0, sizeof(message));
    vector.iov_base = header;
    vector.iov_len = sizeof(header);
    message.msg_iov = &vector;
    message.msg_iovlen = 1;
    message.msg_control = control.space;
    message.msg_controllen = sizeof(control.space);
    cmsg = CMSG_FIRSTHDR(&message);
    cmsg->cmsg_level = SOL_SOCKET;
    cmsg->cmsg_type = SCM_RIGHTS;
    cmsg->cmsg_len = CMSG_LEN(sizeof(int));
    memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    ok = sendmsg(sock, &message, MSG_NOSIGNAL) == (ssize_t)sizeof(header);
    close(fd);
    return ok;
}

/* Reads what Blit sent within `wait` seconds. RELEASE lines free their
   buffer; each is recorded in order. */
static unsigned long long released_seq[4096];
static int released_count;
static unsigned long long buffer_seq[POOL];
static int buffer_busy[POOL];

static void read_replies(int sock, double wait)
{
    double until = now_s() + wait;
    for (;;) {
        struct pollfd p = { sock, POLLIN, 0 };
        double left = until - now_s();
        char line[512];
        ssize_t got;
        if (poll(&p, 1, left > 0 ? (int)(left * 1000) : 0) <= 0)
            return;
        got = recv(sock, line, sizeof(line) - 1, MSG_DONTWAIT);
        if (got <= 0)
            return;
        line[got] = '\0';
        if (strncmp(line, "RELEASE ", 8) == 0) {
            unsigned int id = 0;
            unsigned long long seq = 0;
            char text[48];
            int i;
            sscanf(line + 8, "%u %llu", &id, &seq);
            snprintf(text, sizeof(text), "%llu", seq);
            note(releases, sizeof(releases), &releases_at, text);
            if (released_count < 4096) released_seq[released_count++] = seq;
            for (i = 0; i < POOL; ++i)
                if (buffer_busy[i] && buffer_seq[i] == seq)
                    buffer_busy[i] = 0;
        } else {
            char *end = strchr(line, '\n');
            if (end) *end = '\0';
            note(errors, sizeof(errors), &errors_at, line);
        }
    }
}

/* `alpha HOLD`: an opaque XR24 picture (blue) and, over it at a higher z, an
   AR24 one whose every pixel is premultiplied red at alpha 0x80 (A=0x80,
   R=0x80). Blit draws AR24 as premultiplied, so the screen shows
   R=0x80, B=0xff*(1-0x80/0xff)=0x7f; a straight-alpha reading would show
   R=0x40. */
static int run_alpha(int sock, struct gbm_device *device, int hold)
{
    struct gbm_bo *bg = p_bo_create(device, WIDTH, HEIGHT, FOURCC('X', 'R', '2', '4'),
                                    GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
    struct gbm_bo *fg = p_bo_create(device, WIDTH, HEIGHT, FOURCC('A', 'R', '2', '4'),
                                    GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
    if (bg == NULL || fg == NULL || !fill(bg, 0x000000ff) || !fill(fg, 0x80800000)) {
        note(result, sizeof(result), &result_at, "alpha_buffers=failed");
        finish("FAIL");
        return 1;
    }
    send_frame_ex(sock, bg, 4300, FOURCC('X', 'R', '2', '4'), 1, p_bo_get_modifier(bg), -1);
    send_frame_ex(sock, fg, 4301, FOURCC('A', 'R', '2', '4'), 1, p_bo_get_modifier(fg), -1);
    read_replies(sock, 0.3);
    printf("\033[2;2H\033_Ga=p,i=4300,q=2,z=1,C=1\033\\\033[2;2H\033_Ga=p,i=4301,q=2,z=2,C=1\033\\");
    printf("\033[1;1HDMABUF_ALPHA_HOLDING\n");
    fflush(stdout);
    read_replies(sock, (double)hold);
    printf("\033_Ga=d,d=I,i=4301,q=2\033\\\033_Ga=d,d=I,i=4300,q=2\033\\");
    fflush(stdout);
    read_replies(sock, 1.0);
    {
        char text[64];
        snprintf(text, sizeof(text), "alpha_released=%d", released_count);
        note(result, sizeof(result), &result_at, text);
    }
    finish(errors_at == 0 && released_count == 2 ? "PASS" : "FAIL");
    return 0;
}

/* `fence HOLD`: the frame is drawn by this host's GPU with EGL/GLES into
   the GBM buffer (left half one colour, right half another, two clears),
   and sent with an EGL_ANDROID_native_fence_sync fd as its acquire fence,
   without waiting for the GPU to finish. */
static int render_with_fence(struct gbm_device *device, struct gbm_bo *bo, int *fence)
{
    void *egl = dlopen("libEGL.so.1", RTLD_NOW), *gl = dlopen("libGLESv2.so.2", RTLD_NOW);
    PFNEGLGETPROCADDRESSPROC gpa;
    EGLDisplay (*get_display)(EGLenum, void *, const EGLAttrib *);
    EGLBoolean (*initialize)(EGLDisplay, EGLint *, EGLint *);
    EGLBoolean (*bind_api)(EGLenum);
    EGLContext (*create_context)(EGLDisplay, EGLConfig, EGLContext, const EGLint *);
    EGLBoolean (*make_current)(EGLDisplay, EGLSurface, EGLSurface, EGLContext);
    const char *(*query)(EGLDisplay, EGLint);
    PFNEGLCREATEIMAGEKHRPROC create_image;
    PFNGLEGLIMAGETARGETTEXTURE2DOESPROC image_target;
    PFNEGLCREATESYNCKHRPROC create_sync;
    PFNEGLDESTROYSYNCKHRPROC destroy_sync;
    PFNEGLDUPNATIVEFENCEFDANDROIDPROC dup_fence;
    void (*gen_textures)(GLsizei, GLuint *), (*bind_texture)(GLenum, GLuint);
    void (*gen_framebuffers)(GLsizei, GLuint *), (*bind_framebuffer)(GLenum, GLuint);
    void (*framebuffer_texture)(GLenum, GLenum, GLenum, GLuint, GLint);
    GLenum (*framebuffer_status)(GLenum);
    void (*viewport)(GLint, GLint, GLsizei, GLsizei), (*clear_color)(GLfloat, GLfloat, GLfloat, GLfloat);
    void (*clear)(GLbitfield), (*enable)(GLenum), (*disable)(GLenum), (*flush)(void);
    void (*scissor)(GLint, GLint, GLsizei, GLsizei);
    static const EGLint context_attributes[] = { EGL_CONTEXT_CLIENT_VERSION, 2, EGL_NONE };
    EGLint image_attributes[16], sync_attributes[] = {
        EGL_SYNC_NATIVE_FENCE_FD_ANDROID, EGL_NO_NATIVE_FENCE_FD_ANDROID, EGL_NONE };
    EGLDisplay display;
    EGLContext context;
    EGLImageKHR image;
    EGLSyncKHR sync;
    GLuint texture = 0, framebuffer = 0;
    const char *extensions;
    int plane, n = 0;
    char text[96];

#define WHY(s) do { note(result, sizeof(result), &result_at, "fence_render=" s); return 0; } while (0)
    if (egl == NULL || gl == NULL) WHY("no_libEGL_or_libGLESv2");
    gpa = (PFNEGLGETPROCADDRESSPROC)dlsym(egl, "eglGetProcAddress");
    get_display = (EGLDisplay (*)(EGLenum, void *, const EGLAttrib *))dlsym(egl, "eglGetPlatformDisplay");
    initialize = (EGLBoolean (*)(EGLDisplay, EGLint *, EGLint *))dlsym(egl, "eglInitialize");
    bind_api = (EGLBoolean (*)(EGLenum))dlsym(egl, "eglBindAPI");
    create_context = (EGLContext (*)(EGLDisplay, EGLConfig, EGLContext, const EGLint *))
        dlsym(egl, "eglCreateContext");
    make_current = (EGLBoolean (*)(EGLDisplay, EGLSurface, EGLSurface, EGLContext))
        dlsym(egl, "eglMakeCurrent");
    query = (const char *(*)(EGLDisplay, EGLint))dlsym(egl, "eglQueryString");
    if (!gpa || !get_display || !initialize || !bind_api || !create_context || !make_current || !query)
        WHY("egl_entry_points");
    create_image = (PFNEGLCREATEIMAGEKHRPROC)gpa("eglCreateImageKHR");
    image_target = (PFNGLEGLIMAGETARGETTEXTURE2DOESPROC)gpa("glEGLImageTargetTexture2DOES");
    create_sync = (PFNEGLCREATESYNCKHRPROC)gpa("eglCreateSyncKHR");
    destroy_sync = (PFNEGLDESTROYSYNCKHRPROC)gpa("eglDestroySyncKHR");
    dup_fence = (PFNEGLDUPNATIVEFENCEFDANDROIDPROC)gpa("eglDupNativeFenceFDANDROID");
    gen_textures = (void (*)(GLsizei, GLuint *))dlsym(gl, "glGenTextures");
    bind_texture = (void (*)(GLenum, GLuint))dlsym(gl, "glBindTexture");
    gen_framebuffers = (void (*)(GLsizei, GLuint *))dlsym(gl, "glGenFramebuffers");
    bind_framebuffer = (void (*)(GLenum, GLuint))dlsym(gl, "glBindFramebuffer");
    framebuffer_texture = (void (*)(GLenum, GLenum, GLenum, GLuint, GLint))
        dlsym(gl, "glFramebufferTexture2D");
    framebuffer_status = (GLenum (*)(GLenum))dlsym(gl, "glCheckFramebufferStatus");
    viewport = (void (*)(GLint, GLint, GLsizei, GLsizei))dlsym(gl, "glViewport");
    clear_color = (void (*)(GLfloat, GLfloat, GLfloat, GLfloat))dlsym(gl, "glClearColor");
    clear = (void (*)(GLbitfield))dlsym(gl, "glClear");
    enable = (void (*)(GLenum))dlsym(gl, "glEnable");
    disable = (void (*)(GLenum))dlsym(gl, "glDisable");
    flush = (void (*)(void))dlsym(gl, "glFlush");
    scissor = (void (*)(GLint, GLint, GLsizei, GLsizei))dlsym(gl, "glScissor");
    if (!create_image || !image_target || !create_sync || !destroy_sync || !dup_fence ||
        !gen_textures || !bind_texture || !gen_framebuffers || !bind_framebuffer ||
        !framebuffer_texture || !framebuffer_status || !viewport || !clear_color || !clear ||
        !enable || !disable || !flush || !scissor)
        WHY("gl_entry_points");
    display = get_display(EGL_PLATFORM_GBM_KHR, device, NULL);
    if (display == EGL_NO_DISPLAY || !initialize(display, NULL, NULL)) WHY("egl_initialize");
    extensions = query(display, EGL_EXTENSIONS);
    if (extensions == NULL || strstr(extensions, "EGL_ANDROID_native_fence_sync") == NULL)
        WHY("no_EGL_ANDROID_native_fence_sync");
    if (strstr(extensions, "EGL_KHR_no_config_context") == NULL ||
        strstr(extensions, "EGL_KHR_surfaceless_context") == NULL)
        WHY("no_configless_surfaceless_context");
    bind_api(EGL_OPENGL_ES_API);
    context = create_context(display, (EGLConfig)0, EGL_NO_CONTEXT, context_attributes);
    if (context == EGL_NO_CONTEXT || !make_current(display, EGL_NO_SURFACE, EGL_NO_SURFACE, context))
        WHY("egl_context");
    plane = p_bo_get_fd(bo);
    image_attributes[n++] = EGL_WIDTH; image_attributes[n++] = WIDTH;
    image_attributes[n++] = EGL_HEIGHT; image_attributes[n++] = HEIGHT;
    image_attributes[n++] = EGL_LINUX_DRM_FOURCC_EXT;
    image_attributes[n++] = (EGLint)FOURCC('X', 'R', '2', '4');
    image_attributes[n++] = EGL_DMA_BUF_PLANE0_FD_EXT; image_attributes[n++] = plane;
    image_attributes[n++] = EGL_DMA_BUF_PLANE0_OFFSET_EXT; image_attributes[n++] = 0;
    image_attributes[n++] = EGL_DMA_BUF_PLANE0_PITCH_EXT;
    image_attributes[n++] = (EGLint)p_bo_get_stride(bo);
    image_attributes[n] = EGL_NONE;
    image = create_image(display, EGL_NO_CONTEXT, EGL_LINUX_DMA_BUF_EXT, (EGLClientBuffer)NULL,
                         image_attributes);
    close(plane);
    if (image == EGL_NO_IMAGE_KHR) WHY("import_own_buffer");
    gen_textures(1, &texture);
    bind_texture(GL_TEXTURE_2D, texture);
    image_target(GL_TEXTURE_2D, (GLeglImageOES)image);
    gen_framebuffers(1, &framebuffer);
    bind_framebuffer(GL_FRAMEBUFFER, framebuffer);
    framebuffer_texture(GL_FRAMEBUFFER, GL_COLOR_ATTACHMENT0, GL_TEXTURE_2D, texture, 0);
    if (framebuffer_status(GL_FRAMEBUFFER) != GL_FRAMEBUFFER_COMPLETE) WHY("framebuffer_incomplete");
    viewport(0, 0, WIDTH, HEIGHT);
    clear_color(0x30 / 255.0f, 0x60 / 255.0f, 0xc0 / 255.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    enable(GL_SCISSOR_TEST);
    scissor(WIDTH / 2, 0, WIDTH / 2, HEIGHT);
    clear_color(0xc0 / 255.0f, 0x40 / 255.0f, 0x30 / 255.0f, 1.0f);
    clear(GL_COLOR_BUFFER_BIT);
    disable(GL_SCISSOR_TEST);
    sync = create_sync(display, EGL_SYNC_NATIVE_FENCE_ANDROID, sync_attributes);
    if (sync == EGL_NO_SYNC_KHR) WHY("create_sync");
    flush();
    *fence = dup_fence(display, sync);
    destroy_sync(display, sync);
    if (*fence < 0) WHY("dup_fence");
    snprintf(text, sizeof(text), "fence_render=ok fence_fd=%d", *fence);
    note(result, sizeof(result), &result_at, text);
    return 1;
#undef WHY
}

static int run_fence(int sock, struct gbm_device *device, int hold)
{
    int fence = -1;
    struct gbm_bo *bo = p_bo_create(device, WIDTH, HEIGHT, FOURCC('X', 'R', '2', '4'),
                                    GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
    if (bo == NULL || !render_with_fence(device, bo, &fence)) {
        finish("FAIL");
        return 1;
    }
    send_frame_ex(sock, bo, 4400, FOURCC('X', 'R', '2', '4'), 1, p_bo_get_modifier(bo), fence);
    read_replies(sock, 0.3);
    printf("\033[2;2H\033_Ga=p,i=4400,q=2,C=1\033\\\033[1;1HDMABUF_FENCE_HOLDING\n");
    fflush(stdout);
    read_replies(sock, (double)hold);
    printf("\033_Ga=d,d=I,i=4400,q=2\033\\");
    fflush(stdout);
    read_replies(sock, 1.0);
    finish(errors_at == 0 && released_count == 1 ? "PASS" : "FAIL");
    return 0;
}

int main(int argc, char **argv)
{
    const char *name = getenv("BLIT_FRAME_SOCKET"), *token = getenv("BLIT_FRAME_TOKEN");
    int probe = argc > 1 && strcmp(argv[1], "probe") == 0;
    int frames = argc > 2 ? atoi(argv[2]) : 60;
    int hold = argc > 3 ? atoi(argv[3]) : 10;
    static const uint32_t colours[3] = { 0x00ff0000, 0x0000ff00, 0x000000ff };
    const uint32_t last_colour = 0x0020c060;
    struct gbm_device *device;
    struct gbm_bo *pool[POOL];
    struct sockaddr_un address;
    unsigned char hello[16 + 64];
    char answer[2048], text[160];
    int render, sock, i, sent = 0, in_order = 1, held_newest = 1;
    unsigned long long sequence = 0;
    size_t token_length;
    ssize_t got;

    printf("\033[?25l");
    fflush(stdout);
    render = open("/dev/dri/renderD128", O_RDWR | O_CLOEXEC);
    if (render < 0 || !load_gbm() || (device = p_create_device(render)) == NULL) {
        snprintf(result, sizeof(result), "gbm=unavailable errno=%d", errno);
        finish("FAIL");
        return 1;
    }
    for (i = 0; i < POOL; ++i) {
        pool[i] = p_bo_create(device, WIDTH, HEIGHT, FOURCC('X', 'R', '2', '4'),
                              GBM_BO_USE_LINEAR | GBM_BO_USE_RENDERING);
        if (pool[i] == NULL) {
            snprintf(result, sizeof(result), "gbm_bo_create=failed");
            finish("FAIL");
            return 1;
        }
    }
    snprintf(text, sizeof(text), "gbm=ok modifier=%llx stride=%u",
             (unsigned long long)p_bo_get_modifier(pool[0]), p_bo_get_stride(pool[0]));
    note(result, sizeof(result), &result_at, text);
    if (probe) {
        finish("PROBE");
        return 0;
    }
    if (name == NULL || token == NULL || name[0] != '@' || strlen(token) > 64) {
        note(result, sizeof(result), &result_at, "frame_socket=missing");
        finish("FAIL");
        return 1;
    }
    sock = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path + 1, name + 1, strlen(name + 1));
    if (connect(sock, (struct sockaddr *)&address,
                (socklen_t)(offsetof(struct sockaddr_un, sun_path) + strlen(name))) != 0) {
        note(result, sizeof(result), &result_at, "connect=failed");
        finish("FAIL");
        return 1;
    }
    token_length = strlen(token);
    memcpy(hello, "BLITFRM1", 8);
    put32(hello + 8, 3);
    put32(hello + 12, (uint32_t)token_length);
    memcpy(hello + 16, token, token_length);
    send(sock, hello, 16 + token_length, MSG_NOSIGNAL);
    got = recv(sock, answer, sizeof(answer) - 1, 0);
    answer[got > 0 ? got : 0] = '\0';
    if (got > 0 && answer[got - 1] == '\n') answer[got - 1] = '\0';
    snprintf(text, sizeof(text), "hello=\"%.120s\"", answer);
    note(result, sizeof(result), &result_at, text);
    if (strncmp(answer, "OK dmabuf=1 ", 12) != 0 || strstr(answer, "XR24/0") == NULL) {
        finish("FAIL");
        return 1;
    }
    if (argc > 2 && strcmp(argv[1], "alpha") == 0)
        return run_alpha(sock, device, atoi(argv[2]));
    if (argc > 2 && strcmp(argv[1], "fence") == 0)
        return run_fence(sock, device, atoi(argv[2]));

    /* Frames: a free buffer from the pool, filled, sent. The first is
       placed once it has landed. */
    for (i = 0; i < frames; ++i) {
        int b, waited = 0;
        for (;;) {
            for (b = 0; b < POOL && buffer_busy[b]; ++b) {}
            if (b < POOL) break;
            read_replies(sock, 0.1);
            if (++waited > 50) {
                note(result, sizeof(result), &result_at, "pool=starved");
                finish("FAIL");
                return 1;
            }
        }
        if (!fill(pool[b], i == frames - 1 ? last_colour : colours[i % 3])) {
            note(result, sizeof(result), &result_at, "gbm_bo_map=failed");
            finish("FAIL");
            return 1;
        }
        ++sequence;
        if (!send_frame(sock, pool[b], sequence, p_bo_get_modifier(pool[b]))) {
            note(result, sizeof(result), &result_at, "send=failed");
            finish("FAIL");
            return 1;
        }
        buffer_busy[b] = 1;
        buffer_seq[b] = sequence;
        ++sent;
        if (i == 0) {
            read_replies(sock, 0.2);
            printf("\033[2;2H\033_Ga=p,i=%d,q=2,C=1\033\\", IMAGE_ID);
            fflush(stdout);
        }
        read_replies(sock, 0.05);
    }
    read_replies(sock, 1.0);
    /* Every frame but the newest released, in order; the newest held. */
    for (i = 1; i < released_count; ++i)
        if (released_seq[i] <= released_seq[i - 1]) in_order = 0;
    for (i = 0; i < released_count; ++i)
        if (released_seq[i] == sequence) held_newest = 0;
    snprintf(text, sizeof(text), "sent=%d released=%d in_order=%d newest_held=%d",
             sent, released_count, in_order, held_newest);
    note(result, sizeof(result), &result_at, text);

    /* A modifier Blit did not list. */
    {
        int b;
        int before = (int)errors_at;
        for (b = 0; b < POOL && buffer_busy[b]; ++b) {}
        if (b < POOL) {
            send_frame(sock, pool[b], sequence + 1000, 0x00ffffffffffffffULL);
            read_replies(sock, 0.5);
            snprintf(text, sizeof(text), "bad_modifier=%s",
                     strstr(errors + before, "ENOTSUP") != NULL ? "ENOTSUP" : "not-refused");
            note(result, sizeof(result), &result_at, text);
        }
    }
    printf("\033[1;1HDMABUF_HOLDING\n");
    fflush(stdout);
    read_replies(sock, (double)hold);
    /* Deleting the image releases the frame it showed. */
    {
        int count = released_count, freed = 0;
        printf("\033_Ga=d,d=I,i=%d,q=2\033\\", IMAGE_ID);
        fflush(stdout);
        read_replies(sock, 1.0);
        for (i = count; i < released_count; ++i)
            if (released_seq[i] == sequence) freed = 1;
        snprintf(text, sizeof(text), "delete_released_newest=%d", freed);
        note(result, sizeof(result), &result_at, text);
        finish(released_count == sent && in_order && held_newest && freed &&
               strstr(result, "bad_modifier=ENOTSUP") != NULL ? "PASS" : "FAIL");
    }
    return 0;
}
