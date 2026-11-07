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
