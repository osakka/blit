/* blit-compositor's frame channel (ADR 0228, phase 1), the part that needs
   no Wayland, so tools/tests/compositor_wire_unit.c can drive it alone.

   It speaks version 5 of the frame channel (docs/api.md, "Picture frames by
   descriptor" and "Program windows"; "One grammar, version 5": every line
   about a picture is `<WORD> <verb> <id> [arguments]`): the frames, the
   INPUT lines, and the WINDOW lines between the compositor and the desktop
   that serves the channel. Each is one SOCK_SEQPACKET message, one line, no
   descriptor:

       compositor to desktop
           WINDOW display <name> <x-display>   first, after the hello's OK (`:n`, `-` X off)
           WINDOW new <id> <parent|0>
           WINDOW title <id> <text>       WINDOW appid <id> <text>
           WINDOW limits <id> <minW> <minH> <maxW> <maxH>
           WINDOW fullscreen <id> on|off
           WINDOW popup <id> <parent> <x> <y>
           WINDOW decor <id> server|client
           WINDOW maximize <id> on|off    WINDOW minimize <id>
           WINDOW parent <id> <parent|0>
           WINDOW hide <id>
           WINDOW lock <id> relative|confine|off
           WINDOW gone <id>
       desktop to compositor
           WINDOW size <id> <w> <h> [fullscreen] [maximized] [activated]
                       [tiled-left] [tiled-right] [tiled-top] [tiled-bottom]
           WINDOW output <id> <w> <h> <mHz>
           WINDOW close <id>
           WINDOW lock <id> on|off
           WINDOW quit 0                  the person quit the desktop: end now
           WINDOW ok <id>                 WINDOW error <id> <code>

   A peer whose hello answer has no `window=1` (Blit itself says
   `window=0`) does not speak WINDOW: the compositor sends it no WINDOW line
   and no frame, and says so in its log.

   A wl_shm buffer is FRM1 (ADR 0202): its pixels copied into a fresh
   memfd sealed against shrinking, growing and writing, so the client's
   buffer is released as soon as the frame is sent. XRGB8888 becomes format
   24 (RGB); ARGB8888, premultiplied in Wayland, becomes format 32 (RGBA),
   which Blit reads as straight alpha, so it is un-premultiplied here.

   A linux-dmabuf buffer is FRM3 (ADR 0223), zero copy: the client's plane
   descriptors themselves go in SCM_RIGHTS, and the buffer is held until
   Blit's `FRAME release <id> <seq>`. Only the formats and modifiers the
   hello's `formats=` list names are offered to clients (bc_formats_parse). A
   frame Blit refuses `FRAME error <id> ENOTSUP` (it cannot import now) is
   sent again as a copy, FRM1, when its one linear plane can be mapped, and
   every later dmabuf frame on the connection is copied too (BcWire.refused,
   a producer's rule, ADR 0223).

   Input comes straight from Blit (docs/api.md, "Input straight from
   Blit"): the compositor asks `INPUT sock <id> <path>` on the channel for
   each toplevel (bc_wire_input_sock), answered `INPUT ok <id>` or
   `INPUT error <id> <code>`; Blit connects to that stream socket, and
   writes

       INPUT key <id> <evdev code> press|release
       INPUT ptr <id> <x> <y>          the toplevel's own pixels, decimals
                                       (negative over a popup past its left or top)
       INPUT rel <id> <dx> <dy>
       INPUT btn <id> <BTN code> press|release
       INPUT wheel <id> <dx> <dy>      notches, positive right and down
       INPUT frame <id> <monotonic us>
       INPUT keymap <id> <revision> <size> <rate> <delay>
                                       the keymap: a sealed memfd of <size>
                                       bytes passed with the line

   read by bc_parse_input.

   Header-only and static like Blit's other units; C99 plus POSIX/Linux. */
#ifndef BLIT_COMPOSITOR_WIRE_H
#define BLIT_COMPOSITOR_WIRE_H

#define _GNU_SOURCE
#include <errno.h>
#include <fcntl.h>
#include <stdarg.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <poll.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <dirent.h>
#include <unistd.h>

#define BC_WIRE_VERSION 5
/* Titles and app ids: control characters stripped, at most 256 bytes. */
#define BC_TEXT_BYTES 256
/* The largest size the desktop may ask for, either way, in pixels. */
#define BC_SIZE_MAX 32767
/* wl_shm.format's wire values for the two formats every compositor offers. */
#define BC_SHM_ARGB8888 0u
#define BC_SHM_XRGB8888 1u
/* DRM fourccs (drm_fourcc.h; wl_shm uses them too past 0 and 1). */
#define BC_FOURCC(a, b, c, d) ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | \
                               (uint32_t)(d) << 24)
#define BC_FOURCC_XR24 BC_FOURCC('X', 'R', '2', '4')
#define BC_FOURCC_AR24 BC_FOURCC('A', 'R', '2', '4')
#define BC_FOURCC_XB24 BC_FOURCC('X', 'B', '2', '4')
#define BC_FOURCC_AB24 BC_FOURCC('A', 'B', '2', '4')
#define BC_MODIFIER_LINEAR 0ull
/* ADR 0223: an FRM3 frame carries at most four planes; the hello lists at
   most 16 modifiers per format. */
#define BC_PLANES_MAX 4
#define BC_FORMATS_MAX 64

typedef struct {
    uint32_t fourcc;
    uint64_t modifier;
} BcFormat;

typedef struct {
    int fd;                 /* the frame socket, -1 when not connected */
    int window;             /* the peer answered window=1 */
    int dmabuf;             /* the peer answered dmabuf=1 with a formats= list */
    int refused;            /* an FRM3 was refused ENOTSUP: dmabuf copied from now on */
    char answer[128];       /* the hello's answer, for the log */
    BcFormat formats[BC_FORMATS_MAX];   /* the hello's formats= list */
    int format_count;
    uint32_t next_id;       /* the next picture id; ids start at 1 */
    uint64_t sequence;      /* the last frame sequence sent, all ids */
    unsigned long frames_sent, frames_failed, lines_sent, lines_failed;
    unsigned long dmabuf_sent, dmabuf_released, dmabuf_refused, dmabuf_copied;
    char last_error[96];
} BcWire;

static void bc_wire_init(BcWire *w)
{
    memset(w, 0, sizeof(*w));
    w->fd = -1;
    w->next_id = 1;
}

/* A picture id for a new window or popup, unique on this connection. */
static uint32_t bc_wire_new_id(BcWire *w)
{
    uint32_t id = w->next_id++;
    if (w->next_id == 0) w->next_id = 1;
    return id;
}

/* Whether `answer` (the hello's reply) carries the word `word`. */
static int bc_answer_has(const char *answer, const char *word)
{
    size_t n = strlen(word);
    const char *p = answer;
    while ((p = strstr(p, word)) != NULL) {
        int starts = p == answer || p[-1] == ' ';
        int ends = p[n] == '\0' || p[n] == ' ' || p[n] == '\n';
        if (starts && ends) return 1;
        p += n;
    }
    return 0;
}

/* The formats= word of a hello's answer, `XR24/0/100000000000001,AR24/0`
   (ADR 0223): each fourcc with each modifier, in lowercase hex. Returns the
   pairs read, at most `max`; a malformed modifier ends its entry. */
static int bc_formats_parse(const char *answer, BcFormat *out, int max)
{
    const char *p = answer;
    int n = 0;
    while ((p = strstr(p, "formats=")) != NULL && p != answer && p[-1] != ' ') p += 8;
    if (p == NULL) return 0;
    p += 8;
    while (*p && *p != ' ' && *p != '\n') {
        const char *end = p + strcspn(p, ", \n");
        if (end - p >= 6 && p[4] == '/') {
            const char *m = p + 5;
            uint32_t fourcc = BC_FOURCC(p[0], p[1], p[2], p[3]);
            while (m < end) {
                uint64_t v = 0;
                int digits = 0;
                while (m < end && *m != '/') {
                    int d = *m >= '0' && *m <= '9' ? *m - '0' : *m >= 'a' && *m <= 'f' ? *m - 'a' + 10 : -1;
                    if (d < 0 || digits == 16) { digits = -1; break; }
                    v = v << 4 | (uint64_t)d;
                    ++digits;
                    ++m;
                }
                if (digits <= 0) break;
                if (n < max) {
                    out[n].fourcc = fourcc;
                    out[n].modifier = v;
                    ++n;
                }
                if (m < end) ++m;
            }
        }
        p = *end == ',' ? end + 1 : end;
    }
    return n;
}

/* Whether the peer imports `fourcc` with `modifier`. */
static int bc_wire_format_ok(const BcWire *w, uint32_t fourcc, uint64_t modifier)
{
    int i;
    for (i = 0; i < w->format_count; ++i)
        if (w->formats[i].fourcc == fourcc && w->formats[i].modifier == modifier) return 1;
    return 0;
}

/* The hello's answer: "OK ..." with window=1 when the peer is a desktop. */
static int bc_wire_parse_hello(BcWire *w, const char *answer)
{
    size_t n;
    w->window = 0;
    w->dmabuf = 0;
    w->refused = 0;
    w->format_count = 0;
    n = strcspn(answer, "\n");
    if (n >= sizeof(w->answer)) n = sizeof(w->answer) - 1;
    memcpy(w->answer, answer, n);
    w->answer[n] = '\0';
    if (strncmp(answer, "OK", 2) != 0 || (answer[2] != '\0' && answer[2] != ' ' && answer[2] != '\n'))
        return -1;
    /* From the whole answer, not the logged copy: a formats= list is long. */
    w->window = bc_answer_has(answer, "window=1");
    w->dmabuf = bc_answer_has(answer, "dmabuf=1");
    if (w->dmabuf) w->format_count = bc_formats_parse(answer, w->formats, BC_FORMATS_MAX);
    if (w->format_count == 0) w->dmabuf = 0;
    return 0;
}

/* Connects to the abstract socket `name` (BLIT_FRAME_SOCKET, its leading
   '@' optional) and says hello as a version 5 client, without waiting: the
   socket is non-blocking from the connect on (a listener whose backlog is
   full is a failed attempt, not a wait). Returns the socket, whose first
   message is the answer (bc_wire_answer), or -1 with last_error set. */
static int bc_wire_dial(BcWire *w, const char *name, const char *token)
{
    struct sockaddr_un addr;
    unsigned char hello[16 + 64];
    uint32_t version = BC_WIRE_VERSION, length;
    socklen_t addrlen;
    int fd;

    if (name[0] == '@') ++name;
    length = (uint32_t)strlen(token);
    if (length < 1 || length > 64 || name[0] == '\0' || strlen(name) + 1 > sizeof(addr.sun_path)) {
        snprintf(w->last_error, sizeof(w->last_error), "bad socket or token");
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC | SOCK_NONBLOCK, 0);
    if (fd < 0) {
        snprintf(w->last_error, sizeof(w->last_error), "socket: %s", strerror(errno));
        return -1;
    }
    memset(&addr, 0, sizeof(addr));
    addr.sun_family = AF_UNIX;
    memcpy(addr.sun_path + 1, name, strlen(name));
    addrlen = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(name));
    if (connect(fd, (struct sockaddr *)&addr, addrlen) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "connect: %s", strerror(errno));
        close(fd);
        return -1;
    }
    memcpy(hello, "BLITFRM1", 8);
    memcpy(hello + 8, &version, 4);
    memcpy(hello + 12, &length, 4);
    memcpy(hello + 16, token, length);
    if (send(fd, hello, 16 + length, MSG_NOSIGNAL) != (ssize_t)(16 + length)) {
        snprintf(w->last_error, sizeof(w->last_error), "hello: %s", strerror(errno));
        close(fd);
        return -1;
    }
    return fd;
}

/* The hello's answer, read from a dialed socket that is readable: 0 with
   w->fd the socket when it is `OK ...` (whatever it says of window=1), or
   -1 with last_error set and the socket closed. */
static int bc_wire_answer(BcWire *w, int fd)
{
    char answer[1400];
    ssize_t got = recv(fd, answer, sizeof(answer) - 1, MSG_DONTWAIT);
    if (got <= 0) {
        snprintf(w->last_error, sizeof(w->last_error), "hello: %s",
                 got < 0 ? strerror(errno) : "closed");
        close(fd);
        return -1;
    }
    answer[got] = '\0';
    if (bc_wire_parse_hello(w, answer) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "hello refused: %.60s", w->answer);
        close(fd);
        return -1;
    }
    w->fd = fd;
    return 0;
}

/* Dials and waits up to `timeout_ms` for the answer: the first connection,
   before there is an event loop. Returns 0, or -1 with last_error set. */
static int bc_wire_connect(BcWire *w, const char *name, const char *token, int timeout_ms)
{
    struct pollfd pfd;
    int fd = bc_wire_dial(w, name, token), ready;
    if (fd < 0) return -1;
    pfd.fd = fd;
    pfd.events = POLLIN;
    pfd.revents = 0;
    do ready = poll(&pfd, 1, timeout_ms);
    while (ready < 0 && errno == EINTR);
    if (ready <= 0) {
        snprintf(w->last_error, sizeof(w->last_error), "hello: %s",
                 ready == 0 ? "no answer" : strerror(errno));
        close(fd);
        return -1;
    }
    return bc_wire_answer(w, fd);
}

/* One message with `count` descriptors (0 to BC_PLANES_MAX) in SCM_RIGHTS. */
static int bc_wire_sendmsg_fds(int sock, const void *data, size_t size, const int *fds, int count)
{
    struct msghdr msg;
    struct iovec iov;
    union { struct cmsghdr align; char buf[CMSG_SPACE(sizeof(int) * BC_PLANES_MAX)]; } control;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = (void *)data;
    iov.iov_len = size;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (count > 0) {
        struct cmsghdr *c;
        memset(&control, 0, sizeof(control));
        msg.msg_control = control.buf;
        msg.msg_controllen = CMSG_SPACE(sizeof(int) * (size_t)count);
        c = CMSG_FIRSTHDR(&msg);
        c->cmsg_level = SOL_SOCKET;
        c->cmsg_type = SCM_RIGHTS;
        c->cmsg_len = CMSG_LEN(sizeof(int) * (size_t)count);
        memcpy(CMSG_DATA(c), fds, sizeof(int) * (size_t)count);
    }
    return sendmsg(sock, &msg, MSG_NOSIGNAL | MSG_DONTWAIT) == (ssize_t)size ? 0 : -1;
}

static int bc_wire_sendmsg(int sock, const void *data, size_t size, int fd)
{
    return bc_wire_sendmsg_fds(sock, data, size, &fd, fd >= 0 ? 1 : 0);
}

/* Copies `in` into `out` (BC_TEXT_BYTES + 1 bytes) as a title or app id:
   valid UTF-8 only, C0 and C1 controls and DEL stripped (newline among
   them), at most BC_TEXT_BYTES bytes, never ending inside a character. */
static void bc_text_clean(char *out, const char *in)
{
    const unsigned char *s = (const unsigned char *)(in ? in : "");
    size_t n = 0;
    while (*s) {
        unsigned char c = *s;
        size_t len = c < 0x80 ? 1 : (c & 0xe0) == 0xc0 ? 2 : (c & 0xf0) == 0xe0 ? 3 :
                     (c & 0xf8) == 0xf0 ? 4 : 0;
        size_t k;
        uint32_t cp;
        if (len == 0) { ++s; continue; }
        for (k = 1; k < len; ++k)
            if ((s[k] & 0xc0) != 0x80) break;
        if (k < len) { ++s; continue; }
        cp = len == 1 ? c : len == 2 ? (uint32_t)(c & 0x1f) : len == 3 ? (uint32_t)(c & 0x0f) :
             (uint32_t)(c & 0x07);
        for (k = 1; k < len; ++k) cp = (cp << 6) | (s[k] & 0x3f);
        /* Overlong forms, surrogates and past U+10FFFF are not UTF-8. */
        if ((len == 2 && cp < 0x80) || (len == 3 && cp < 0x800) || (len == 4 && cp < 0x10000) ||
            (cp >= 0xd800 && cp <= 0xdfff) || cp > 0x10ffff) {
            ++s;
            continue;
        }
        if (cp < 0x20 || cp == 0x7f || (cp >= 0x80 && cp <= 0x9f)) { s += len; continue; }
        if (n + len > BC_TEXT_BYTES) break;
        memcpy(out + n, s, len);
        n += len;
        s += len;
    }
    out[n] = '\0';
}

/* One WINDOW line (the caller's format has no newline; one is added). */
static int bc_wire_vline(BcWire *w, int fd, const char *fmt, va_list ap)
{
    char line[64 + BC_TEXT_BYTES];
    int n;
    if (w->fd < 0 || !w->window) return -1;
    n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    if (n < 0 || (size_t)n >= sizeof(line) - 1) return -1;
    line[n++] = '\n';
    if (bc_wire_sendmsg(w->fd, line, (size_t)n, fd) != 0) {
        ++w->lines_failed;
        snprintf(w->last_error, sizeof(w->last_error), "send WINDOW: %s", strerror(errno));
        return -1;
    }
    ++w->lines_sent;
    return 0;
}

static int bc_wire_line(BcWire *w, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = bc_wire_vline(w, -1, fmt, ap);
    va_end(ap);
    return rc;
}

/* A line with a descriptor passed with it (CLIP read's pipe), one packet. */
static int bc_wire_line_fd(BcWire *w, int fd, const char *fmt, ...)
{
    va_list ap;
    int rc;
    va_start(ap, fmt);
    rc = bc_wire_vline(w, fd, fmt, ap);
    va_end(ap);
    return rc;
}

/* `INPUT sock <id> <path>`: a toplevel's input route, straight from Blit to
   the input socket at `path` (docs/api.md, "Input straight from Blit"). */
static int bc_wire_input_sock(BcWire *w, uint32_t id, const char *path)
{
    return bc_wire_line(w, "INPUT sock %u %s", id, path);
}

/* Converts `height` rows of a wl_shm buffer to FRM1 pixels: XRGB8888 (B, G,
   R, X in memory) to RGB, ARGB8888 (premultiplied) to straight RGBA; and,
   for a dmabuf copied, XBGR8888 and ABGR8888 (R, G, B, X/A in memory). */
static void bc_convert_rows(unsigned char *out, uint32_t out_stride, const unsigned char *in,
                            uint32_t in_stride, uint32_t width, uint32_t height, uint32_t format)
{
    uint32_t x, y;
    int ri = format == BC_FOURCC_XB24 || format == BC_FOURCC_AB24 ? 0 : 2, bi = 2 - ri;
    int opaque = format == BC_SHM_XRGB8888 || format == BC_FOURCC_XB24;
    for (y = 0; y < height; ++y) {
        const unsigned char *s = in + (size_t)y * in_stride;
        unsigned char *d = out + (size_t)y * out_stride;
        if (opaque) {
            for (x = 0; x < width; ++x) {
                d[x * 3 + 0] = s[x * 4 + ri];
                d[x * 3 + 1] = s[x * 4 + 1];
                d[x * 3 + 2] = s[x * 4 + bi];
            }
        } else {
            for (x = 0; x < width; ++x) {
                unsigned a = s[x * 4 + 3];
                unsigned r = s[x * 4 + ri], g = s[x * 4 + 1], b = s[x * 4 + bi];
                if (a != 0 && a != 255) {
                    r = (r * 255 + a / 2) / a; if (r > 255) r = 255;
                    g = (g * 255 + a / 2) / a; if (g > 255) g = 255;
                    b = (b * 255 + a / 2) / a; if (b > 255) b = 255;
                } else if (a == 0) {
                    r = g = b = 0;
                }
                d[x * 4 + 0] = (unsigned char)r;
                d[x * 4 + 1] = (unsigned char)g;
                d[x * 4 + 2] = (unsigned char)b;
                d[x * 4 + 3] = (unsigned char)a;
            }
        }
    }
}

/* A frame's pixels as FRM1 carries them: a sealed memfd (fd, -1 for none)
   of `height` rows of `stride` bytes, format 24 (RGB) or 32 (RGBA). The
   compositor keeps each window's newest, to send again on a new channel;
   Blit reads a frame when it arrives (ADR 0202) and the memfd is sealed, so
   one copy may be sent any number of times. */
typedef struct {
    int fd;
    uint32_t width, height, format, stride;
} BcCopy;

/* Copies a wl_shm buffer's pixels into a fresh memfd sealed against
   shrinking, growing and writing. Needs no channel. Returns 0, or -1 with
   last_error set (counted in frames_failed). */
static int bc_frame_copy(BcWire *w, const void *pixels, uint32_t width, uint32_t height,
                         uint32_t stride, uint32_t format, BcCopy *out)
{
    uint32_t bpp = format == BC_SHM_XRGB8888 || format == BC_FOURCC_XB24 ? 3 : 4;
    size_t bytes;
    unsigned char *map;
    int fd;

    out->fd = -1;
    if (width == 0 || height == 0 || width > BC_SIZE_MAX || height > BC_SIZE_MAX ||
        (format != BC_SHM_XRGB8888 && format != BC_SHM_ARGB8888 && format != BC_FOURCC_XB24 &&
         format != BC_FOURCC_AB24) || stride < width * 4) {
        snprintf(w->last_error, sizeof(w->last_error), "frame %ux%u format %u stride %u",
                 width, height, format, stride);
        ++w->frames_failed;
        return -1;
    }
    out->width = width;
    out->height = height;
    out->format = bpp == 3 ? 24 : 32;
    out->stride = width * bpp;
    bytes = (size_t)out->stride * height;
    fd = memfd_create("blit-compositor-frame", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || ftruncate(fd, (off_t)bytes) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "memfd: %s", strerror(errno));
        if (fd >= 0) close(fd);
        ++w->frames_failed;
        return -1;
    }
    map = (unsigned char *)mmap(NULL, bytes, PROT_READ | PROT_WRITE, MAP_SHARED, fd, 0);
    if (map == MAP_FAILED) {
        snprintf(w->last_error, sizeof(w->last_error), "mmap: %s", strerror(errno));
        close(fd);
        ++w->frames_failed;
        return -1;
    }
    bc_convert_rows(map, out->stride, (const unsigned char *)pixels, stride, width, height, format);
    munmap(map, bytes);
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "seal: %s", strerror(errno));
        close(fd);
        ++w->frames_failed;
        return -1;
    }
    out->fd = fd;
    return 0;
}

/* One whole FRM1 frame of picture `id` from a copy (ADR 0202). Returns 0,
   or -1 with last_error set. */
static int bc_wire_send_frm1(BcWire *w, uint32_t id, const BcCopy *c)
{
    unsigned char h[48];
    uint32_t zero = 0;
    uint64_t seq;
    if (w->fd < 0 || !w->window || c->fd < 0) return -1;
    seq = w->sequence + 1;
    memset(h, 0, sizeof(h));
    memcpy(h, "FRM1", 4);
    memcpy(h + 4, &id, 4);
    memcpy(h + 8, &seq, 8);
    memcpy(h + 16, &c->width, 4);
    memcpy(h + 20, &c->height, 4);
    memcpy(h + 24, &c->format, 4);
    memcpy(h + 28, &c->stride, 4);
    memcpy(h + 32, &zero, 4); /* x, y, w, h all 0: the whole image */
    if (bc_wire_sendmsg(w->fd, h, sizeof(h), c->fd) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "send FRM1: %s", strerror(errno));
        ++w->frames_failed;
        return -1;
    }
    w->sequence = seq;
    ++w->frames_sent;
    return 0;
}

/* A wl_shm buffer's pixels copied and sent as one FRM1 of picture `id`,
   the copy not kept: nothing is held, so the buffer may be released at
   once. Returns 0, or -1 with last_error set. */
static int bc_wire_send_shm(BcWire *w, uint32_t id, const void *pixels, uint32_t width,
                            uint32_t height, uint32_t stride, uint32_t format)
{
    BcCopy c;
    int rc;
    if (w->fd < 0 || !w->window) return -1;
    if (bc_frame_copy(w, pixels, width, height, stride, format, &c) != 0) return -1;
    rc = bc_wire_send_frm1(w, id, &c);
    close(c.fd);
    return rc;
}

/* One dmabuf frame of picture `id` as FRM3 (ADR 0223, "Wire format,
   version 3"): the planes' own descriptors, no copy, no fence. The caller
   holds the buffer until FRAME release <id> <sequence>. Returns the sequence
   sent, or 0 with last_error set. */
static uint64_t bc_wire_send_dmabuf(BcWire *w, uint32_t id, uint32_t width, uint32_t height,
                                    uint32_t fourcc, uint64_t modifier, int planes, const int *fds,
                                    const uint32_t *offsets, const uint32_t *pitches)
{
    unsigned char h[72];
    uint32_t count = (uint32_t)planes;
    uint64_t seq;
    int i;
    if (w->fd < 0 || !w->window || !w->dmabuf) return 0;
    if (planes < 1 || planes > BC_PLANES_MAX) {
        snprintf(w->last_error, sizeof(w->last_error), "dmabuf with %d planes", planes);
        ++w->frames_failed;
        return 0;
    }
    seq = w->sequence + 1;
    memset(h, 0, sizeof(h));
    memcpy(h, "FRM3", 4);
    memcpy(h + 4, &id, 4);
    memcpy(h + 8, &seq, 8);
    memcpy(h + 16, &width, 4);
    memcpy(h + 20, &height, 4);
    memcpy(h + 24, &fourcc, 4);
    memcpy(h + 28, &count, 4);
    memcpy(h + 32, &modifier, 8);
    for (i = 0; i < planes; ++i) {
        memcpy(h + 40 + 8 * i, &offsets[i], 4);
        memcpy(h + 44 + 8 * i, &pitches[i], 4);
    }
    if (bc_wire_sendmsg_fds(w->fd, h, sizeof(h), fds, planes) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "send FRM3: %s", strerror(errno));
        ++w->frames_failed;
        return 0;
    }
    w->sequence = seq;
    ++w->frames_sent;
    ++w->dmabuf_sent;
    return seq;
}

/* One line from the desktop (or from Blit) on the channel. */
enum {
    BC_LINE_UNKNOWN = 0,
    BC_LINE_SIZE,           /* WINDOW size <id> <w> <h> [fullscreen] [maximized] [activated] [tiled-*] */
    BC_LINE_OUTPUT,         /* WINDOW output <id> <w> <h> <mHz> */
    BC_LINE_CLOSE,          /* WINDOW close <id> */
    BC_LINE_LOCK,           /* WINDOW lock <id> on|off: the desktop's pointer hold */
    BC_LINE_QUIT,           /* WINDOW quit 0: end every program, exit, no reconnect */
    BC_LINE_OK_WINDOW,      /* WINDOW ok <id> */
    BC_LINE_ERROR_WINDOW,   /* WINDOW error <id> <code> */
    BC_LINE_RELEASE,        /* FRAME release <id> <sequence>: an FRM3 frame's buffer is free */
    BC_LINE_ERROR,          /* FRAME error <id> <code>: a refused frame */
    BC_LINE_OTHER,          /* understood, nothing to do (HUD1, INPUT ok|error) */
    BC_LINE_CLIP_OFFER,     /* CLIP offer <seq> clipboard|primary [<type> ...] */
    BC_LINE_CLIP_READ,      /* CLIP read <seq> <type>, a pipe's write end with it */
    BC_LINE_CLIP_CLEAR      /* CLIP clear <seq> */
};

/* The clipboard's lines (ADR 0229): media types are printable ASCII words. */
#define BC_CLIP_TYPE_BYTES 127
#define BC_CLIP_TYPES 16

typedef struct {
    int kind;
    uint32_t id;
    int32_t width, height;
    int fullscreen, maximized, activated;
    unsigned tiled;         /* BC_TILED_*: the edges the desktop put it against */
    int on;                 /* WINDOW lock */
    uint32_t refresh_mhz;   /* WINDOW output */
    uint64_t sequence;      /* FRAME release */
    char code[32];
    int primary;            /* CLIP offer: the primary selection, not the clipboard */
    char types[BC_TEXT_BYTES + 1];  /* CLIP offer: its types, one space apart; read: the type */
    int ntypes;
} BcLine;

/* One media type word at *p into out (BC_CLIP_TYPE_BYTES + 1); advances. */
static int bc_read_type(const char **p, char *out)
{
    size_t n = 0;
    while ((unsigned char)**p > ' ' && (unsigned char)**p < 0x7f) {
        if (n >= BC_CLIP_TYPE_BYTES) return -1;
        out[n++] = *(*p)++;
    }
    out[n] = '\0';
    return n ? 0 : -1;
}

/* Reads an unsigned decimal word at *p (no sign, no leading space), at most
   `max`; advances *p past it. */
static int bc_read_u32(const char **p, unsigned long max, uint32_t *out)
{
    const char *s = *p;
    unsigned long v = 0;
    if (*s < '0' || *s > '9') return -1;
    while (*s >= '0' && *s <= '9') {
        v = v * 10 + (unsigned long)(*s - '0');
        if (v > max) return -1;
        ++s;
    }
    *out = (uint32_t)v;
    *p = s;
    return 0;
}

/* One line from Blit on an input socket. */
/* WINDOW size's tiled words, in their order: bits of BcLine.tiled. */
enum { BC_TILED_LEFT = 1, BC_TILED_RIGHT = 2, BC_TILED_TOP = 4, BC_TILED_BOTTOM = 8 };

enum {
    BC_INPUT_UNKNOWN = 0,
    BC_INPUT_KEY,           /* code, pressed */
    BC_INPUT_PTR,           /* x, y */
    BC_INPUT_REL,           /* dx, dy */
    BC_INPUT_BTN,           /* code, pressed */
    BC_INPUT_WHEEL,         /* dx, dy (notches) */
    BC_INPUT_FRAME,         /* time_us */
    BC_INPUT_KEYMAP         /* revision size rate delay, and a memfd: no frame follows */
};

typedef struct {
    int kind;
    uint32_t id;
    uint32_t code;
    int pressed;
    double x, y;            /* ptr: pixels; rel: counts; wheel: notches */
    uint64_t time_us;
    uint64_t revision;      /* keymap */
    uint32_t size;          /* keymap: the memfd's bytes, text and NUL */
    int32_t rate, delay;    /* keymap: repeat per second, ms; 0 0 none */
} BcInput;

/* A decimal number as Blit writes it: an optional '-', digits, and an
   optional fraction. Advances *p. */
static int bc_read_number(const char **p, double *out)
{
    const char *s = *p;
    char *end;
    if (!((*s >= '0' && *s <= '9') || (*s == '-' && s[1] >= '0' && s[1] <= '9'))) return -1;
    errno = 0;
    *out = strtod(s, &end);
    if (errno != 0 || end == s || *out > 1e9 || *out < -1e9) return -1;
    for (; s < end; ++s)
        if (!((*s >= '0' && *s <= '9') || *s == '-' || *s == '.')) return -1;
    *p = end;
    return 0;
}

static int bc_parse_input(const char *line, BcInput *out)
{
    static const struct { const char *word; int kind; } kinds[] = {
        { "key ", BC_INPUT_KEY }, { "ptr ", BC_INPUT_PTR }, { "rel ", BC_INPUT_REL },
        { "btn ", BC_INPUT_BTN }, { "wheel ", BC_INPUT_WHEEL }, { "frame ", BC_INPUT_FRAME },
        { "keymap ", BC_INPUT_KEYMAP },
    };
    const char *p;
    size_t i;
    memset(out, 0, sizeof(*out));
    if (strncmp(line, "INPUT ", 6) != 0) return out->kind = BC_INPUT_UNKNOWN;
    p = line + 6;
    for (i = 0; i < sizeof(kinds) / sizeof(kinds[0]); ++i)
        if (strncmp(p, kinds[i].word, strlen(kinds[i].word)) == 0) break;
    if (i == sizeof(kinds) / sizeof(kinds[0])) return out->kind = BC_INPUT_UNKNOWN;
    p += strlen(kinds[i].word);
    if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || out->id == 0 || *p++ != ' ')
        return out->kind = BC_INPUT_UNKNOWN;
    switch (kinds[i].kind) {
    case BC_INPUT_KEY:
    case BC_INPUT_BTN:
        if (bc_read_u32(&p, 0x2ff, &out->code) != 0 || *p++ != ' ') return out->kind = BC_INPUT_UNKNOWN;
        if (strncmp(p, "press", 5) == 0) { out->pressed = 1; p += 5; }
        else if (strncmp(p, "release", 7) == 0) p += 7;
        else return out->kind = BC_INPUT_UNKNOWN;
        break;
    case BC_INPUT_PTR:
    case BC_INPUT_REL:
    case BC_INPUT_WHEEL:
        if (bc_read_number(&p, &out->x) != 0 || *p++ != ' ' || bc_read_number(&p, &out->y) != 0)
            return out->kind = BC_INPUT_UNKNOWN;
        break;
    default: {
        char *end;
        if (*p < '0' || *p > '9') return out->kind = BC_INPUT_UNKNOWN;
        errno = 0;
        out->time_us = strtoull(p, &end, 10);
        if (errno != 0) return out->kind = BC_INPUT_UNKNOWN;
        p = end;
        if (kinds[i].kind == BC_INPUT_KEYMAP) {
            uint32_t rate, delay;
            out->revision = out->time_us;
            out->time_us = 0;
            if (*p++ != ' ' || bc_read_u32(&p, 0x7fffffffUL, &out->size) != 0 || *p++ != ' ' ||
                bc_read_u32(&p, 1000, &rate) != 0 || *p++ != ' ' ||
                bc_read_u32(&p, 60000, &delay) != 0)
                return out->kind = BC_INPUT_UNKNOWN;
            out->rate = (int32_t)rate;
            out->delay = (int32_t)delay;
        }
        break;
    }
    }
    if (*p != '\0' && *p != '\n') return out->kind = BC_INPUT_UNKNOWN;
    return out->kind = kinds[i].kind;
}

static int bc_parse_line(const char *line, BcLine *out)
{
    const char *p;
    uint32_t a, b;
    memset(out, 0, sizeof(*out));
    if (strncmp(line, "CLIP ", 5) == 0) {
        char type[BC_CLIP_TYPE_BYTES + 1];
        int kind;
        p = line + 5;
        if (strncmp(p, "offer ", 6) == 0) { kind = BC_LINE_CLIP_OFFER; p += 6; }
        else if (strncmp(p, "read ", 5) == 0) { kind = BC_LINE_CLIP_READ; p += 5; }
        else if (strncmp(p, "clear ", 6) == 0) { kind = BC_LINE_CLIP_CLEAR; p += 6; }
        else return out->kind = BC_LINE_UNKNOWN;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || out->id == 0)
            return out->kind = BC_LINE_UNKNOWN;
        if (kind == BC_LINE_CLIP_OFFER) {
            size_t used = 0;
            if (strncmp(p, " clipboard", 10) == 0) p += 10;
            else if (strncmp(p, " primary", 8) == 0) { out->primary = 1; p += 8; }
            else return out->kind = BC_LINE_UNKNOWN;
            while (*p == ' ') {
                size_t n;
                ++p;
                if (bc_read_type(&p, type) != 0 || out->ntypes >= BC_CLIP_TYPES)
                    return out->kind = BC_LINE_UNKNOWN;
                n = strlen(type);
                if (used + n + 1 > BC_TEXT_BYTES) return out->kind = BC_LINE_UNKNOWN;
                if (used) out->types[used++] = ' ';
                memcpy(out->types + used, type, n);
                used += n;
                out->types[used] = '\0';
                ++out->ntypes;
            }
        } else if (kind == BC_LINE_CLIP_READ) {
            if (*p++ != ' ' || bc_read_type(&p, type) != 0) return out->kind = BC_LINE_UNKNOWN;
            snprintf(out->types, sizeof(out->types), "%s", type);
            out->ntypes = 1;
        }
        if (*p != '\0' && *p != '\n') return out->kind = BC_LINE_UNKNOWN;
        return out->kind = kind;
    }
    if (strncmp(line, "WINDOW size ", 12) == 0) {
        p = line + 12;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &a) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &b) != 0 || out->id == 0)
            return out->kind = BC_LINE_UNKNOWN;
        out->width = (int32_t)a;
        out->height = (int32_t)b;
        /* The state words, a set: any order, each at most once (Hibr writes
           `activated fullscreen`; an order bought nothing and dropped the
           whole line). An unknown word, or one twice, refuses the line. */
        while (*p == ' ') {
            static const char *const words[] = { "fullscreen", "maximized", "activated", "tiled-left",
                                                 "tiled-right", "tiled-top", "tiled-bottom" };
            size_t k, n = 0;
            int seen;
            ++p;
            for (k = 0; k < sizeof(words) / sizeof(words[0]); ++k) {
                n = strlen(words[k]);
                if (strncmp(p, words[k], n) == 0 && (p[n] == '\0' || p[n] == ' ' || p[n] == '\n')) break;
            }
            if (k == sizeof(words) / sizeof(words[0])) return out->kind = BC_LINE_UNKNOWN;
            seen = k == 0 ? out->fullscreen : k == 1 ? out->maximized : k == 2 ? out->activated
                 : (int)(out->tiled & (1u << (k - 3)));
            if (seen) return out->kind = BC_LINE_UNKNOWN;
            if (k == 0) out->fullscreen = 1;
            else if (k == 1) out->maximized = 1;
            else if (k == 2) out->activated = 1;
            else out->tiled |= 1u << (k - 3);
            p += n;
        }
        if (*p != '\0' && *p != '\n') return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_SIZE;
    }
    if (strncmp(line, "WINDOW output ", 14) == 0) {
        /* A display's mode: at least 1x1, a refresh of 1 mHz to 1000 Hz. */
        p = line + 14;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &a) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &b) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, 1000000UL, &out->refresh_mhz) != 0 || out->id == 0 || a == 0 ||
            b == 0 || out->refresh_mhz == 0 || (*p != '\0' && *p != '\n'))
            return out->kind = BC_LINE_UNKNOWN;
        out->width = (int32_t)a;
        out->height = (int32_t)b;
        return out->kind = BC_LINE_OUTPUT;
    }
    if (strncmp(line, "WINDOW close ", 13) == 0) {
        p = line + 13;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || out->id == 0 ||
            (*p != '\0' && *p != '\n'))
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_CLOSE;
    }
    if (strncmp(line, "WINDOW lock ", 12) == 0) {
        p = line + 12;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || out->id == 0 || *p++ != ' ')
            return out->kind = BC_LINE_UNKNOWN;
        if (strncmp(p, "on", 2) == 0 && (p[2] == '\0' || p[2] == '\n')) out->on = 1;
        else if (strncmp(p, "off", 3) != 0 || (p[3] != '\0' && p[3] != '\n'))
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_LOCK;
    }
    if (strncmp(line, "WINDOW quit ", 12) == 0) {
        /* Id 0, the whole compositor: no other id is defined. */
        p = line + 12;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || out->id != 0 ||
            (*p != '\0' && *p != '\n'))
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_QUIT;
    }
    if (strncmp(line, "WINDOW ok ", 10) == 0) {
        p = line + 10;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0) return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_OK_WINDOW;
    }
    if (strncmp(line, "WINDOW error ", 13) == 0) {
        p = line + 13;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            sscanf(p, "%31s", out->code) != 1)
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_ERROR_WINDOW;
    }
    if (strncmp(line, "FRAME release ", 14) == 0) {
        char *end;
        p = line + 14;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' || *p < '0' || *p > '9')
            return out->kind = BC_LINE_UNKNOWN;
        errno = 0;
        out->sequence = strtoull(p, &end, 10);
        if (errno != 0 || (*end != '\0' && *end != '\n')) return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_RELEASE;
    }
    if (strncmp(line, "FRAME error ", 12) == 0) {
        p = line + 12;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            sscanf(p, "%31s", out->code) != 1)
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_ERROR;
    }
    if (strncmp(line, "HUD1 ", 5) == 0 || strncmp(line, "INPUT ok ", 9) == 0 ||
        strncmp(line, "INPUT error ", 12) == 0)
        return out->kind = BC_LINE_OTHER;
    return out->kind = BC_LINE_UNKNOWN;
}

#endif
