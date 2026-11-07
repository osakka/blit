/* blit-gamescope's frame channel client (ADR 0225, phase 1), the part that
   needs no PipeWire, so tools/tests/gamescope_bridge_gate.c can drive it with
   fake buffers against a socketpair or a real Blit.

   It speaks version 3 of the frame channel (ADR 0202, ADR 0223): the hello,
   FRM3 frames whose plane is a buffer's dmabuf (held until Blit's
   `RELEASE <id> <seq>` or an `ERROR`, then handed back through `requeue`),
   FRM1 frames copied into a fresh sealed memfd when Blit says `dmabuf=0` or
   refuses FRM3 with ENOTSUP, and the INPUT request that names gamescope's EIS
   socket for Blit to send the pane's input to (docs/api.md, "Picture frames
   by descriptor"; ADR 0225), each one SOCK_SEQPACKET message, no descriptor:

       INPUT eis <absolute socket path> <image id>\n
           answered `OK input\n` or `ERROR INPUT <code>\n`; an
           `ERROR INPUT ENOENT\n` after `OK input` means the compositor
           went and Blit ended the route
       INPUT on\n   INPUT off\n
           whether the client's window has the keyboard; a route starts off.
           In phase 1 the bridge sends `INPUT on` right after `OK input`.

   A Blit from before INPUT (0.3.60) treats the request as a malformed frame
   and answers `ERROR <u32 of bytes 4..7> EINVAL\n` -- "T ei" read
   little-endian, 1768235092 -- which counts here as ENOTSUP: the bridge then
   runs frames only.

   Header-only and static like Blit's other units; C99 plus POSIX/Linux. */
#ifndef BLIT_GAMESCOPE_FRAMES_H
#define BLIT_GAMESCOPE_FRAMES_H

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define GS_HOLD_MAX 16
#define GS_FOURCC_XR24 0x34325258u /* 'X' 'R' '2' '4' */
/* The id an older Blit reads from an INPUT message (bytes 4..7, "T ei"). */
#define GS_INPUT_LEGACY_ID 1768235092u

enum gs_input_state {
    GS_INPUT_NONE = 0,     /* not asked */
    GS_INPUT_SENT,         /* asked, no answer yet */
    GS_INPUT_OK,           /* Blit sends the pane's input to the socket */
    GS_INPUT_UNSUPPORTED,  /* this Blit has no INPUT: frames only */
    GS_INPUT_REFUSED       /* Blit refused it (input_error says why) */
};

typedef void (*gs_requeue_fn)(void *context, void *cookie);

typedef struct {
    int used;
    uint64_t sequence;
    void *cookie;
} GsHeld;

typedef struct {
    int fd;                 /* the frame socket, -1 when not connected */
    int dmabuf;             /* Blit offered dmabuf=1 with XR24/0 */
    int dmabuf_refused;     /* an FRM3 was refused ENOTSUP: memfd from now on */
    int hud;                /* HUD1 on/off, informational */
    char formats[256];      /* the hello's formats= token */
    uint32_t image_id;
    uint64_t sequence;      /* last sequence sent, shared by FRM1 and FRM3 */
    GsHeld held[GS_HOLD_MAX];
    gs_requeue_fn requeue;
    void *context;
    int input_state;
    int input_on;           /* INPUT on sent */
    int input_was_ok;       /* this request was answered OK input: a later
                               ERROR INPUT ends a live route, not a refusal */
    char input_error[32];
    /* counters, for the log and the gates */
    uint64_t last_memfd_sequence; /* the newest FRM1 sent */
    unsigned long sent_dmabuf, sent_memfd, released, errors, requeued, stale_releases;
    char last_error[64];
} GsFrames;

static void gs_frames_init(GsFrames *f, uint32_t image_id, gs_requeue_fn requeue, void *context)
{
    memset(f, 0, sizeof(*f));
    f->fd = -1;
    f->image_id = image_id ? image_id : 1;
    f->requeue = requeue;
    f->context = context;
}

/* The hello's answer: "OK\n", "OK dmabuf=0\n" or
   "OK dmabuf=1 fence=<0|1> formats=<list>\n". dmabuf counts only when the
   list offers XR24 with modifier 0 (LINEAR), which is what gamescope sends. */
static int gs_frames_parse_hello(GsFrames *f, const char *answer)
{
    const char *p;
    f->dmabuf = 0;
    f->formats[0] = '\0';
    if (strncmp(answer, "OK", 2) != 0)
        return -1;
    p = strstr(answer, "formats=");
    if (p) {
        size_t n = strcspn(p + 8, " \n");
        if (n >= sizeof(f->formats)) n = sizeof(f->formats) - 1;
        memcpy(f->formats, p + 8, n);
        f->formats[n] = '\0';
    }
    if (strstr(answer, "dmabuf=1") && f->formats[0]) {
        /* Find "XR24/" then a modifier list containing "0" as one token. */
        const char *x = strstr(f->formats, "XR24/");
        if (x) {
            const char *m = x + 5;
            while (*m && *m != ',') {
                size_t n = strcspn(m, "/,");
                if (n == 1 && m[0] == '0') { f->dmabuf = 1; break; }
                m += n;
                if (*m == '/') ++m;
            }
        }
    }
    return 0;
}

/* Connects to the abstract socket `name` (BLIT_FRAME_SOCKET without its
   leading '@') and says hello as a version 3 client. Returns 0, or -1 with
   last_error set. */
static int gs_frames_connect(GsFrames *f, const char *name, const char *token)
{
    struct sockaddr_un addr;
    unsigned char hello[16 + 64];
    uint32_t version = 3, length;
    char answer[512];
    ssize_t got;
    struct timeval tv = { 3, 0 };
    socklen_t addrlen;
    int fd;

    if (name[0] == '@') ++name;
    length = (uint32_t)strlen(token);
    if (length < 1 || length > 64 || strlen(name) + 1 > sizeof(addr.sun_path)) {
        snprintf(f->last_error, sizeof(f->last_error), "bad socket or token");
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
    if (fd < 0) {
        snprintf(f->last_error, sizeof(f->last_error), "socket: %s", strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, name, strlen(name));
    addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name));
    if (connect(fd, (struct sockaddr *)&addr, addrlen) != 0) {
        snprintf(f->last_error, sizeof(f->last_error), "connect: %s", strerror(errno));
        close(fd);
        return -1;
    }
    memcpy(hello, "BLITFRM1", 8);
    memcpy(hello + 8, &version, 4);
    memcpy(hello + 12, &length, 4);
    memcpy(hello + 16, token, length);
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (send(fd, hello, 16 + length, MSG_NOSIGNAL) != (ssize_t)(16 + length) ||
        (got = recv(fd, answer, sizeof(answer) - 1, 0)) <= 0) {
        snprintf(f->last_error, sizeof(f->last_error), "hello: %s", strerror(errno));
        close(fd);
        return -1;
    }
    answer[got] = '\0';
    if (gs_frames_parse_hello(f, answer) != 0) {
        answer[strcspn(answer, "\n")] = '\0';
        snprintf(f->last_error, sizeof(f->last_error), "hello refused: %.40s", answer);
        close(fd);
        return -1;
    }
    tv.tv_sec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    f->fd = fd;
    /* A "HUD1 on" may follow the OK in the same read on some kernels: no,
       SEQPACKET keeps it a message of its own, read by gs_frames_read. */
    return 0;
}

static int gs_frames_use_dmabuf(const GsFrames *f)
{
    return f->dmabuf && !f->dmabuf_refused;
}

/* The 72-byte FRM3 header for a one-plane XR24/LINEAR buffer. */
static void gs_frames_frm3_header(unsigned char *h, uint32_t id, uint64_t seq, uint32_t w,
                                  uint32_t hgt, uint32_t offset, uint32_t pitch)
{
    uint32_t fourcc = GS_FOURCC_XR24, planes = 1;
    uint64_t modifier = 0;
    memset(h, 0, 72);
    memcpy(h, "FRM3", 4);
    memcpy(h + 4, &id, 4);
    memcpy(h + 8, &seq, 8);
    memcpy(h + 16, &w, 4);
    memcpy(h + 20, &hgt, 4);
    memcpy(h + 24, &fourcc, 4);
    memcpy(h + 28, &planes, 4);
    memcpy(h + 32, &modifier, 8);
    memcpy(h + 40, &offset, 4);
    memcpy(h + 44, &pitch, 4);
}

static int gs_frames_sendmsg(int sock, const void *data, size_t size, int fd)
{
    struct msghdr msg;
    struct iovec iov;
    union { struct cmsghdr align; char buf[CMSG_SPACE(sizeof(int))]; } control;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = (void *)data;
    iov.iov_len = size;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (fd >= 0) {
        struct cmsghdr *c;
        memset(&control, 0, sizeof(control));
        msg.msg_control = control.buf;
        msg.msg_controllen = sizeof(control.buf);
        c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(c), &fd, sizeof(int));
    }
    return sendmsg(sock, &msg, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)size ? 0 : -1;
}

/* Sends one buffer's dmabuf as FRM3 and holds `cookie` until Blit releases
   or refuses it. Returns 0 when sent and held; -1 when it could not be sent
   (the caller still owns the buffer and requeues it itself). */
static int gs_frames_send_dmabuf(GsFrames *f, int dmabuf_fd, uint32_t width, uint32_t height,
                                 uint32_t offset, uint32_t pitch, void *cookie)
{
    unsigned char h[72];
    int i, slot = -1;
    if (f->fd < 0) return -1;
    for (i = 0; i < GS_HOLD_MAX; ++i)
        if (!f->held[i].used) { slot = i; break; }
    if (slot < 0) {
        snprintf(f->last_error, sizeof(f->last_error), "hold table full");
        return -1;
    }
    gs_frames_frm3_header(h, f->image_id, f->sequence + 1, width, height, offset, pitch);
    if (gs_frames_sendmsg(f->fd, h, sizeof(h), dmabuf_fd) != 0) {
        snprintf(f->last_error, sizeof(f->last_error), "send FRM3: %s", strerror(errno));
        return -1;
    }
    ++f->sequence;
    f->held[slot].used = 1;
    f->held[slot].sequence = f->sequence;
    f->held[slot].cookie = cookie;
    ++f->sent_dmabuf;
    return 0;
}

/* Copies a BGRx picture into a fresh sealed memfd as RGB (FRM1 format 24)
   and sends it. Blit reads it at once (ADR 0202), so nothing is held: the
   caller requeues its buffer as soon as this returns. */
static int gs_frames_send_pixels(GsFrames *f, const unsigned char *bgrx, uint32_t width,
                                 uint32_t height, uint32_t stride)
{
    unsigned char h[48];
    uint32_t out_stride = width * 3, fmt = 24, zero = 0, y, x;
    size_t bytes = (size_t)out_stride * height;
    unsigned char *map;
    int fd, rc;
    if (f->fd < 0 || width == 0 || height == 0) return -1;
    fd = memfd_create("blit-gamescope-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0) {
        if (fd >= 0) close(fd);
        return -1;
    }
    map = (unsigned char *)mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) { close(fd); return -1; }
    for (y = 0; y < height; ++y) {
        const unsigned char *s = bgrx + (size_t)y * stride;
        unsigned char *d = map + (size_t)y * out_stride;
        for (x = 0; x < width; ++x) {
            d[x * 3 + 0] = s[x * 4 + 2];
            d[x * 3 + 1] = s[x * 4 + 1];
            d[x * 3 + 2] = s[x * 4 + 0];
        }
    }
    munmap(map, bytes);
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        close(fd);
        return -1;
    }
    memset(h, 0, sizeof(h));
    memcpy(h, "FRM1", 4);
    {
        uint64_t seq = f->sequence + 1;
        memcpy(h + 4, &f->image_id, 4);
        memcpy(h + 8, &seq, 8);
    }
    memcpy(h + 16, &width, 4);
    memcpy(h + 20, &height, 4);
    memcpy(h + 24, &fmt, 4);
    memcpy(h + 28, &out_stride, 4);
    memcpy(h + 32, &zero, 4); /* x, y, w, h all 0: the whole image */
    rc = gs_frames_sendmsg(f->fd, h, sizeof(h), fd);
    close(fd);
    if (rc == 0) { ++f->sequence; ++f->sent_memfd; f->last_memfd_sequence = f->sequence; }
    return rc;
}

/* The INPUT request (see the top of this file). */
static int gs_frames_format_input(char *out, size_t cap, const char *path, uint32_t image_id)
{
    int n = snprintf(out, cap, "INPUT eis %s %u\n", path, image_id);
    return n > 0 && (size_t)n < cap ? n : -1;
}

static int gs_frames_send_input(GsFrames *f, const char *path)
{
    char line[512];
    int n;
    if (f->fd < 0 || path[0] != '/' || strchr(path, ' ') || strchr(path, '\n')) return -1;
    n = gs_frames_format_input(line, sizeof(line), path, f->image_id);
    if (n < 0 || gs_frames_sendmsg(f->fd, line, (size_t)n, -1) != 0) return -1;
    f->input_state = GS_INPUT_SENT;
    f->input_on = 0;
    f->input_was_ok = 0;
    f->input_error[0] = '\0';
    return 0;
}

/* `INPUT on` / `INPUT off`: whether the window has the keyboard. */
static int gs_frames_send_input_focus(GsFrames *f, int on)
{
    const char *line = on ? "INPUT on\n" : "INPUT off\n";
    if (f->fd < 0 || f->input_state != GS_INPUT_OK) return -1;
    if (gs_frames_sendmsg(f->fd, line, strlen(line), -1) != 0) return -1;
    f->input_on = on;
    return 0;
}

static void gs_frames_requeue_slot(GsFrames *f, int i)
{
    void *cookie = f->held[i].cookie;
    f->held[i].used = 0;
    f->held[i].cookie = NULL;
    ++f->requeued;
    if (f->requeue) f->requeue(f->context, cookie);
}

/* Every held buffer back to the stream: on disconnect and at exit. */
static void gs_frames_requeue_all(GsFrames *f)
{
    int i;
    for (i = 0; i < GS_HOLD_MAX; ++i)
        if (f->held[i].used) gs_frames_requeue_slot(f, i);
}

/* One reply line from Blit. Returns 1 when it was understood. */
static int gs_frames_handle_line(GsFrames *f, const char *line)
{
    unsigned long long id, seq;
    char code[32];
    int i;
    if (sscanf(line, "RELEASE %llu %llu", &id, &seq) == 2) {
        ++f->released;
        for (i = 0; i < GS_HOLD_MAX; ++i)
            if (f->held[i].used && f->held[i].sequence == seq) {
                gs_frames_requeue_slot(f, i);
                return 1;
            }
        ++f->stale_releases;
        return 1;
    }
    if (strcmp(line, "OK input") == 0) {
        f->input_state = GS_INPUT_OK;
        f->input_was_ok = 1;
        gs_frames_send_input_focus(f, 1); /* phase 1: the pane is the window */
        return 1;
    }
    if (sscanf(line, "ERROR INPUT %31s", code) == 1) {
        /* After OK input this is the route ending (ENOENT: the compositor
           refused the handshake late or went away); input_was_ok stays. */
        snprintf(f->input_error, sizeof(f->input_error), "%s", code);
        f->input_on = 0;
        f->input_state = strcmp(code, "ENOTSUP") == 0 ? GS_INPUT_UNSUPPORTED : GS_INPUT_REFUSED;
        return 1;
    }
    if (strncmp(line, "HUD1 ", 5) == 0) {
        f->hud = strncmp(line + 5, "on", 2) == 0;
        return 1;
    }
    if (sscanf(line, "ERROR %llu %31s", &id, code) == 2) {
        ++f->errors;
        snprintf(f->last_error, sizeof(f->last_error), "ERROR %llu %s", id, code);
        if (id == GS_INPUT_LEGACY_ID && f->input_state == GS_INPUT_SENT) {
            /* A Blit that predates INPUT read it as a malformed frame. */
            f->input_state = GS_INPUT_UNSUPPORTED;
            snprintf(f->input_error, sizeof(f->input_error), "ENOTSUP");
            return 1;
        }
        if (id != f->image_id) return 1;
        /* A refused frame is the producer's again. Replies come in the order
           frames are read, and an accepted frame gets no reply, so the
           refusal is for the newest frame still held. */
        {
            int newest = -1;
            for (i = 0; i < GS_HOLD_MAX; ++i)
                if (f->held[i].used && (newest < 0 || f->held[i].sequence > f->held[newest].sequence))
                    newest = i;
            /* After a memfd frame the refusal is that frame's: hold nothing. */
            if (newest >= 0 && f->held[newest].sequence > f->last_memfd_sequence)
                gs_frames_requeue_slot(f, newest);
        }
        if (strcmp(code, "ENOTSUP") == 0) f->dmabuf_refused = 1;
        return 1;
    }
    return 0;
}

/* Reads every pending reply. Returns 0, or -1 when Blit closed the channel
   (every held buffer is then requeued). */
static int gs_frames_read(GsFrames *f)
{
    char buf[512];
    for (;;) {
        ssize_t got = recv(f->fd, buf, sizeof(buf) - 1, MSG_DONTWAIT);
        char *line, *next;
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) return 0;
        if (got < 0 && errno == EINTR) continue;
        if (got <= 0) {
            gs_frames_requeue_all(f);
            close(f->fd);
            f->fd = -1;
            return -1;
        }
        buf[got] = '\0';
        for (line = buf; line && *line; line = next) {
            next = strchr(line, '\n');
            if (next) *next++ = '\0';
            gs_frames_handle_line(f, line);
        }
    }
}

/* The Kitty placement of the picture: once, at the pane's top-left, sized in
   cells to fill it, the cursor not moved (C=1). */
static int gs_frames_place(char *out, size_t cap, uint32_t id, int cols, int rows)
{
    return snprintf(out, cap, "\033[H\033_Ga=p,i=%u,q=2,C=1,c=%d,r=%d\033\\", id, cols, rows);
}

static int gs_frames_delete(char *out, size_t cap, uint32_t id)
{
    return snprintf(out, cap, "\033_Ga=d,d=I,i=%u,q=2\033\\", id);
}

#endif
