/* blit-compositor's frame channel (ADR 0228, phase 1), the part that needs
   no Wayland, so tools/tests/compositor_wire_unit.c can drive it alone.

   It speaks version 4 of the frame channel (docs/api.md, "Picture frames by
   descriptor" and "Program windows"): version 3 plus the WINDOW lines
   between the compositor and the desktop that serves the channel. Each is
   one SOCK_SEQPACKET message, one line, no descriptor:

       compositor to desktop
           WINDOW display <name>          first, after the hello's OK
           WINDOW new <id> <parent|0>
           WINDOW title <id> <text>       WINDOW appid <id> <text>
           WINDOW limits <id> <minW> <minH> <maxW> <maxH>
           WINDOW fullscreen <id> on|off
           WINDOW popup <id> <parent> <x> <y>
           WINDOW decor <id> server|client
           WINDOW maximize <id> on|off    WINDOW minimize <id>
           WINDOW parent <id> <parent|0>
           WINDOW hide <id>
           WINDOW gone <id>
       desktop to compositor
           WINDOW size <id> <w> <h> [fullscreen] [maximized] [activated]
           WINDOW output <id> <w> <h> <mHz>
           WINDOW close <id>
           OK window <id>                 ERROR WINDOW <id> <code>

   A peer whose hello answer has no `window=1` (Blit itself says
   `window=0`) does not speak WINDOW: the compositor sends it no WINDOW line
   and no frame, and says so in its log.

   Frames are FRM1 (ADR 0202): a wl_shm buffer's pixels copied into a fresh
   memfd sealed against shrinking, growing and writing, so the client's
   buffer is released as soon as the frame is sent. XRGB8888 becomes format
   24 (RGB); ARGB8888, premultiplied in Wayland, becomes format 32 (RGBA),
   which Blit reads as straight alpha, so it is un-premultiplied here. FRM3
   (dmabuf) is milestone B.

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
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/time.h>
#include <sys/types.h>
#include <sys/un.h>
#include <unistd.h>

#define BC_WIRE_VERSION 4
/* Titles and app ids: control characters stripped, at most 256 bytes. */
#define BC_TEXT_BYTES 256
/* The largest size the desktop may ask for, either way, in pixels. */
#define BC_SIZE_MAX 32767
/* wl_shm.format's wire values for the two formats every compositor offers. */
#define BC_SHM_ARGB8888 0u
#define BC_SHM_XRGB8888 1u

typedef struct {
    int fd;                 /* the frame socket, -1 when not connected */
    int window;             /* the peer answered window=1 */
    int dmabuf;             /* the peer answered dmabuf=1 (milestone B) */
    char answer[128];       /* the hello's answer, for the log */
    uint32_t next_id;       /* the next picture id; ids start at 1 */
    uint64_t sequence;      /* the last frame sequence sent, all ids */
    unsigned long frames_sent, frames_failed, lines_sent, lines_failed;
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

/* The hello's answer: "OK ..." with window=1 when the peer is a desktop. */
static int bc_wire_parse_hello(BcWire *w, const char *answer)
{
    size_t n;
    w->window = 0;
    w->dmabuf = 0;
    n = strcspn(answer, "\n");
    if (n >= sizeof(w->answer)) n = sizeof(w->answer) - 1;
    memcpy(w->answer, answer, n);
    w->answer[n] = '\0';
    if (strncmp(answer, "OK", 2) != 0 || (answer[2] != '\0' && answer[2] != ' ' && answer[2] != '\n'))
        return -1;
    w->window = bc_answer_has(w->answer, "window=1");
    w->dmabuf = bc_answer_has(w->answer, "dmabuf=1");
    return 0;
}

/* Connects to the abstract socket `name` (BLIT_FRAME_SOCKET, its leading
   '@' optional) and says hello as a version 4 client. Returns 0, or -1 with
   last_error set. */
static int bc_wire_connect(BcWire *w, const char *name, const char *token)
{
    struct sockaddr_un addr;
    unsigned char hello[16 + 64];
    uint32_t version = BC_WIRE_VERSION, length;
    char answer[1400];
    ssize_t got;
    struct timeval tv = { 3, 0 };
    socklen_t addrlen;
    int fd;

    if (name[0] == '@') ++name;
    length = (uint32_t)strlen(token);
    if (length < 1 || length > 64 || name[0] == '\0' || strlen(name) + 1 > sizeof(addr.sun_path)) {
        snprintf(w->last_error, sizeof(w->last_error), "bad socket or token");
        return -1;
    }
    fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_CLOEXEC, 0);
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
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    if (send(fd, hello, 16 + length, MSG_NOSIGNAL) != (ssize_t)(16 + length) ||
        (got = recv(fd, answer, sizeof(answer) - 1, 0)) <= 0) {
        snprintf(w->last_error, sizeof(w->last_error), "hello: %s",
                 errno ? strerror(errno) : "closed");
        close(fd);
        return -1;
    }
    answer[got] = '\0';
    if (bc_wire_parse_hello(w, answer) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "hello refused: %.60s", w->answer);
        close(fd);
        return -1;
    }
    tv.tv_sec = 0;
    setsockopt(fd, SOL_SOCKET, SO_RCVTIMEO, &tv, sizeof(tv));
    w->fd = fd;
    return 0;
}

static int bc_wire_sendmsg(int sock, const void *data, size_t size, int fd)
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
static int bc_wire_line(BcWire *w, const char *fmt, ...)
{
    char line[64 + BC_TEXT_BYTES];
    va_list ap;
    int n;
    if (w->fd < 0 || !w->window) return -1;
    va_start(ap, fmt);
    n = vsnprintf(line, sizeof(line) - 1, fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(line) - 1) return -1;
    line[n++] = '\n';
    if (bc_wire_sendmsg(w->fd, line, (size_t)n, -1) != 0) {
        ++w->lines_failed;
        snprintf(w->last_error, sizeof(w->last_error), "send WINDOW: %s", strerror(errno));
        return -1;
    }
    ++w->lines_sent;
    return 0;
}

/* Converts `height` rows of a wl_shm buffer to FRM1 pixels: XRGB8888 (B, G,
   R, X in memory) to RGB, ARGB8888 (premultiplied) to straight RGBA. */
static void bc_convert_rows(unsigned char *out, uint32_t out_stride, const unsigned char *in,
                            uint32_t in_stride, uint32_t width, uint32_t height, uint32_t format)
{
    uint32_t x, y;
    for (y = 0; y < height; ++y) {
        const unsigned char *s = in + (size_t)y * in_stride;
        unsigned char *d = out + (size_t)y * out_stride;
        if (format == BC_SHM_XRGB8888) {
            for (x = 0; x < width; ++x) {
                d[x * 3 + 0] = s[x * 4 + 2];
                d[x * 3 + 1] = s[x * 4 + 1];
                d[x * 3 + 2] = s[x * 4 + 0];
            }
        } else {
            for (x = 0; x < width; ++x) {
                unsigned a = s[x * 4 + 3];
                unsigned r = s[x * 4 + 2], g = s[x * 4 + 1], b = s[x * 4 + 0];
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

/* Copies a wl_shm buffer's pixels into a fresh sealed memfd and sends them
   as one whole FRM1 frame of picture `id`. Blit reads it when it arrives
   (ADR 0202), so nothing is held: the buffer may be released at once.
   Returns 0, or -1 with last_error set. */
static int bc_wire_send_shm(BcWire *w, uint32_t id, const void *pixels, uint32_t width,
                            uint32_t height, uint32_t stride, uint32_t format)
{
    unsigned char h[48];
    uint32_t bpp = format == BC_SHM_XRGB8888 ? 3 : 4;
    uint32_t fmt = bpp == 3 ? 24 : 32, out_stride, zero = 0;
    uint64_t seq;
    size_t bytes;
    unsigned char *map;
    int fd, rc;

    if (w->fd < 0 || !w->window) return -1;
    if (width == 0 || height == 0 || width > BC_SIZE_MAX || height > BC_SIZE_MAX ||
        (format != BC_SHM_XRGB8888 && format != BC_SHM_ARGB8888) || stride < width * 4) {
        snprintf(w->last_error, sizeof(w->last_error), "frame %ux%u format %u stride %u",
                 width, height, format, stride);
        ++w->frames_failed;
        return -1;
    }
    out_stride = width * bpp;
    bytes = (size_t)out_stride * height;
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
    bc_convert_rows(map, out_stride, (const unsigned char *)pixels, stride, width, height, format);
    munmap(map, bytes);
    if (fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "seal: %s", strerror(errno));
        close(fd);
        ++w->frames_failed;
        return -1;
    }
    seq = w->sequence + 1;
    memset(h, 0, sizeof(h));
    memcpy(h, "FRM1", 4);
    memcpy(h + 4, &id, 4);
    memcpy(h + 8, &seq, 8);
    memcpy(h + 16, &width, 4);
    memcpy(h + 20, &height, 4);
    memcpy(h + 24, &fmt, 4);
    memcpy(h + 28, &out_stride, 4);
    memcpy(h + 32, &zero, 4); /* x, y, w, h all 0: the whole image */
    rc = bc_wire_sendmsg(w->fd, h, sizeof(h), fd);
    close(fd);
    if (rc != 0) {
        snprintf(w->last_error, sizeof(w->last_error), "send FRM1: %s", strerror(errno));
        ++w->frames_failed;
        return -1;
    }
    w->sequence = seq;
    ++w->frames_sent;
    return 0;
}

/* One line from the desktop (or from Blit) on the channel. */
enum {
    BC_LINE_UNKNOWN = 0,
    BC_LINE_SIZE,           /* WINDOW size <id> <w> <h> [fullscreen] [maximized] [activated] */
    BC_LINE_OUTPUT,         /* WINDOW output <id> <w> <h> <mHz> */
    BC_LINE_CLOSE,          /* WINDOW close <id> */
    BC_LINE_OK_WINDOW,      /* OK window <id> */
    BC_LINE_ERROR_WINDOW,   /* ERROR WINDOW <id> <code> */
    BC_LINE_RELEASE,        /* RELEASE <id> <sequence>: FRM3 only, milestone B */
    BC_LINE_ERROR,          /* ERROR <id> <code>: a refused frame */
    BC_LINE_OTHER           /* understood, nothing to do (HUD1, OK input, ...) */
};

typedef struct {
    int kind;
    uint32_t id;
    int32_t width, height;
    int fullscreen, maximized, activated;
    uint32_t refresh_mhz;   /* WINDOW output */
    char code[32];
} BcLine;

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

static int bc_parse_line(const char *line, BcLine *out)
{
    const char *p;
    uint32_t a, b;
    memset(out, 0, sizeof(*out));
    if (strncmp(line, "WINDOW size ", 12) == 0) {
        p = line + 12;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &a) != 0 || *p++ != ' ' ||
            bc_read_u32(&p, BC_SIZE_MAX, &b) != 0 || out->id == 0)
            return out->kind = BC_LINE_UNKNOWN;
        out->width = (int32_t)a;
        out->height = (int32_t)b;
        /* The flags in the draft's order, each at most once. */
        if (strncmp(p, " fullscreen", 11) == 0 && (p[11] == '\0' || p[11] == ' ' || p[11] == '\n')) {
            out->fullscreen = 1;
            p += 11;
        }
        if (strncmp(p, " maximized", 10) == 0 && (p[10] == '\0' || p[10] == ' ' || p[10] == '\n')) {
            out->maximized = 1;
            p += 10;
        }
        if (strncmp(p, " activated", 10) == 0 && (p[10] == '\0' || p[10] == ' ' || p[10] == '\n')) {
            out->activated = 1;
            p += 10;
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
    if (strncmp(line, "OK window ", 10) == 0) {
        p = line + 10;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0) return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_OK_WINDOW;
    }
    if (strncmp(line, "ERROR WINDOW ", 13) == 0) {
        p = line + 13;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            sscanf(p, "%31s", out->code) != 1)
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_ERROR_WINDOW;
    }
    if (strncmp(line, "RELEASE ", 8) == 0) {
        p = line + 8;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0) return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_RELEASE;
    }
    if (strncmp(line, "ERROR ", 6) == 0) {
        p = line + 6;
        if (strncmp(p, "INPUT ", 6) == 0) return out->kind = BC_LINE_OTHER;
        if (bc_read_u32(&p, 0xffffffffUL, &out->id) != 0 || *p++ != ' ' ||
            sscanf(p, "%31s", out->code) != 1)
            return out->kind = BC_LINE_UNKNOWN;
        return out->kind = BC_LINE_ERROR;
    }
    if (strncmp(line, "HUD1 ", 5) == 0 || strncmp(line, "OK input ", 9) == 0)
        return out->kind = BC_LINE_OTHER;
    return out->kind = BC_LINE_UNKNOWN;
}

#endif
