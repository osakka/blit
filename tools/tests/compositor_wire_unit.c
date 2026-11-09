/* ADR 0228: blit-compositor's frame-channel core (src/blit_compositor_wire.h)
   without Wayland: the hello's answer, titles cleaned and bounded, the
   desktop's lines, the pixel conversion, and one FRM1 frame and one WINDOW
   line over a socketpair. Prints COMPOSITOR_WIRE_UNIT_OK. */
#include "blit_compositor_wire.h"

static int failures;

#define EXPECT(cond, ...) do { if (!(cond)) { ++failures; \
    fprintf(stderr, "COMPOSITOR_WIRE_UNIT_FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

static void hello(void)
{
    BcWire w;
    bc_wire_init(&w);
    EXPECT(bc_wire_parse_hello(&w, "OK dmabuf=0 window=1\n") == 0 && w.window && !w.dmabuf, "window=1");
    EXPECT(bc_wire_parse_hello(&w, "OK dmabuf=0 window=0\n") == 0 && !w.window, "window=0");
    EXPECT(bc_wire_parse_hello(&w, "OK dmabuf=0\n") == 0 && !w.window, "no window=");
    EXPECT(bc_wire_parse_hello(&w, "OK dmabuf=1 fence=0 formats=XR24/0 window=1") == 0 &&
           w.window && w.dmabuf, "dmabuf=1 window=1 at the end");
    EXPECT(bc_wire_parse_hello(&w, "OK dmabuf=0 window=10\n") == 0 && !w.window, "window=10 is not 1");
    EXPECT(bc_wire_parse_hello(&w, "ERROR 0 EACCES\n") != 0, "refused");
    EXPECT(bc_wire_parse_hello(&w, "OKAY\n") != 0, "OKAY is not OK");
    EXPECT(bc_wire_new_id(&w) == 1 && bc_wire_new_id(&w) == 2, "ids count from 1");
}

static void text(void)
{
    char out[BC_TEXT_BYTES + 1], in[1024];
    size_t i;
    bc_text_clean(out, "plain title");
    EXPECT(strcmp(out, "plain title") == 0, "plain: %s", out);
    bc_text_clean(out, "a\nb\tc\033[31md\x7f" "e\xc2\x85" "f");
    EXPECT(strcmp(out, "abc[31mdef") == 0, "controls stripped: %s", out);
    bc_text_clean(out, "caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9f\x98\x80");
    EXPECT(strcmp(out, "caf\xc3\xa9 \xe2\x9c\x93 \xf0\x9f\x98\x80") == 0, "UTF-8 kept: %s", out);
    bc_text_clean(out, "x\xff\xc3y\xc0\xafz\xed\xa0\x80w");
    EXPECT(strcmp(out, "xyzw") == 0, "invalid, overlong and surrogate bytes dropped: %s", out);
    bc_text_clean(out, NULL);
    EXPECT(out[0] == '\0', "NULL is empty");
    memset(in, 'a', 300);
    in[300] = '\0';
    bc_text_clean(out, in);
    EXPECT(strlen(out) == BC_TEXT_BYTES, "bounded to %d: %zu", BC_TEXT_BYTES, strlen(out));
    /* 255 ASCII bytes then a 2-byte character: it does not fit and is not split. */
    memset(in, 'a', 255);
    memcpy(in + 255, "\xc3\xa9", 3);
    bc_text_clean(out, in);
    EXPECT(strlen(out) == 255, "never ends inside a character: %zu", strlen(out));
    for (i = 0; out[i]; ++i) EXPECT(out[i] == 'a', "byte %zu", i);
}

static void lines(void)
{
    BcLine l;
    EXPECT(bc_parse_line("WINDOW size 1 320 200", &l) == BC_LINE_SIZE && l.id == 1 &&
           l.width == 320 && l.height == 200 && !l.fullscreen && !l.activated, "size");
    EXPECT(bc_parse_line("WINDOW size 4 1920 1080 fullscreen activated\n", &l) == BC_LINE_SIZE &&
           l.fullscreen && l.activated, "size, both flags");
    EXPECT(bc_parse_line("WINDOW size 4 500 300 activated", &l) == BC_LINE_SIZE && !l.fullscreen &&
           l.activated, "size, activated");
    EXPECT(bc_parse_line("WINDOW size 2 0 0", &l) == BC_LINE_SIZE && l.width == 0 && l.height == 0,
           "0 0: the program chooses");
    EXPECT(bc_parse_line("WINDOW size 2 40000 10", &l) == BC_LINE_UNKNOWN, "too wide");
    EXPECT(bc_parse_line("WINDOW size 2 -1 10", &l) == BC_LINE_UNKNOWN, "negative");
    EXPECT(bc_parse_line("WINDOW size 0 10 10", &l) == BC_LINE_UNKNOWN, "id 0");
    EXPECT(bc_parse_line("WINDOW size 2 10 10 maximized", &l) == BC_LINE_SIZE && l.maximized &&
           !l.fullscreen && !l.activated, "maximized");
    EXPECT(bc_parse_line("WINDOW size 2 10 10 fullscreen maximized activated", &l) == BC_LINE_SIZE &&
           l.fullscreen && l.maximized && l.activated, "all three flags");
    EXPECT(bc_parse_line("WINDOW size 2 10 10 activated maximized", &l) == BC_LINE_UNKNOWN,
           "maximized after activated");
    EXPECT(bc_parse_line("WINDOW size 2 10 10 zoomed", &l) == BC_LINE_UNKNOWN, "unknown flag");
    EXPECT(bc_parse_line("WINDOW output 3 2560 1440 59951\n", &l) == BC_LINE_OUTPUT && l.id == 3 &&
           l.width == 2560 && l.height == 1440 && l.refresh_mhz == 59951, "output");
    EXPECT(bc_parse_line("WINDOW output 3 2560 1440 0", &l) == BC_LINE_UNKNOWN, "output, 0 mHz");
    EXPECT(bc_parse_line("WINDOW output 3 0 1440 60000", &l) == BC_LINE_UNKNOWN, "output, 0 wide");
    EXPECT(bc_parse_line("WINDOW output 3 2560 1440", &l) == BC_LINE_UNKNOWN, "output, no refresh");
    EXPECT(bc_parse_line("WINDOW output 3 2560 1440 60000 x", &l) == BC_LINE_UNKNOWN, "output, tail");
    EXPECT(bc_parse_line("WINDOW size 2 10 10 activated fullscreen", &l) == BC_LINE_UNKNOWN,
           "flags out of the draft's order");
    EXPECT(bc_parse_line("WINDOW close 9\n", &l) == BC_LINE_CLOSE && l.id == 9, "close");
    EXPECT(bc_parse_line("WINDOW close 9 x", &l) == BC_LINE_UNKNOWN, "close with a tail");
    EXPECT(bc_parse_line("ERROR WINDOW 3 EINVAL\n", &l) == BC_LINE_ERROR_WINDOW && l.id == 3 &&
           strcmp(l.code, "EINVAL") == 0, "ERROR WINDOW");
    EXPECT(bc_parse_line("OK window 3", &l) == BC_LINE_OK_WINDOW && l.id == 3, "OK window");
    EXPECT(bc_parse_line("ERROR 5 ENOMEM", &l) == BC_LINE_ERROR && l.id == 5, "a refused frame");
    EXPECT(bc_parse_line("ERROR INPUT 5 ENOTSUP", &l) == BC_LINE_OTHER, "an input route's answer");
    EXPECT(bc_parse_line("HUD1 on", &l) == BC_LINE_OTHER, "HUD1");
    EXPECT(bc_parse_line("WINDOW frobnicate 1", &l) == BC_LINE_UNKNOWN, "unknown");
}

static void convert(void)
{
    /* XRGB8888 is B, G, R, X in memory; ARGB8888 the same with alpha,
       premultiplied. */
    const unsigned char xrgb[8] = { 0x30, 0xa0, 0xd0, 0x00, 0x01, 0x02, 0x03, 0xff };
    const unsigned char argb[12] = { 0x40, 0x20, 0x10, 0x80,   0x00, 0x00, 0x00, 0x00,
                                     0x11, 0x22, 0x33, 0xff };
    unsigned char out[12];
    bc_convert_rows(out, 6, xrgb, 8, 2, 1, BC_SHM_XRGB8888);
    EXPECT(out[0] == 0xd0 && out[1] == 0xa0 && out[2] == 0x30 && out[3] == 3 && out[4] == 2 &&
           out[5] == 1, "XRGB to RGB");
    bc_convert_rows(out, 12, argb, 12, 3, 1, BC_SHM_ARGB8888);
    EXPECT(out[0] == 0x20 && out[1] == 0x40 && out[2] == 0x80 && out[3] == 0x80,
           "ARGB half alpha un-premultiplied: %02x %02x %02x %02x", out[0], out[1], out[2], out[3]);
    EXPECT(out[4] == 0 && out[5] == 0 && out[6] == 0 && out[7] == 0, "transparent");
    EXPECT(out[8] == 0x33 && out[9] == 0x22 && out[10] == 0x11 && out[11] == 0xff, "opaque");
}

static void channel(void)
{
    BcWire w;
    int sv[2];
    unsigned char pixels[4 * 3 * 2], header[64], got[18];
    char buf[512];
    struct msghdr msg;
    struct iovec iov;
    union { struct cmsghdr align; char buf[CMSG_SPACE(sizeof(int))]; } control;
    struct cmsghdr *c;
    uint32_t v[10];
    uint64_t seq;
    int fd = -1, seals, i;
    ssize_t n;

    if (socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) != 0) {
        EXPECT(0, "socketpair: %s", strerror(errno));
        return;
    }
    bc_wire_init(&w);
    w.fd = sv[0];
    /* Nothing is sent to a peer without window=1. */
    EXPECT(bc_wire_line(&w, "WINDOW display %s", "blit-0") != 0, "a line without window=1");
    EXPECT(bc_wire_send_shm(&w, 1, pixels, 3, 2, 12, BC_SHM_XRGB8888) != 0, "a frame without window=1");
    w.window = 1;
    EXPECT(bc_wire_line(&w, "WINDOW title %u %s", 1u, "x") == 0, "a line");
    n = recv(sv[1], buf, sizeof(buf) - 1, MSG_DONTWAIT);
    EXPECT(n == 17 && memcmp(buf, "WINDOW title 1 x\n", 17) == 0, "the line: %zd", n);
    for (i = 0; i < (int)sizeof(pixels); ++i) pixels[i] = (unsigned char)i;
    EXPECT(bc_wire_send_shm(&w, 7, pixels, 3, 2, 12, BC_SHM_XRGB8888) == 0, "a frame: %s", w.last_error);
    EXPECT(bc_wire_send_shm(&w, 7, pixels, 3, 2, 8, BC_SHM_XRGB8888) != 0, "a stride too short");
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = header;
    iov.iov_len = sizeof(header);
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.buf;
    msg.msg_controllen = sizeof(control.buf);
    n = recvmsg(sv[1], &msg, MSG_DONTWAIT);
    EXPECT(n == 48 && memcmp(header, "FRM1", 4) == 0, "FRM1 header: %zd", n);
    c = CMSG_FIRSTHDR(&msg);
    if (c && c->cmsg_type == SCM_RIGHTS) memcpy(&fd, CMSG_DATA(c), sizeof(int));
    memcpy(&v[0], header + 4, 4);
    memcpy(&seq, header + 8, 8);
    memcpy(&v[1], header + 16, 4 * 8);
    EXPECT(v[0] == 7 && seq == 1 && v[1] == 3 && v[2] == 2 && v[3] == 24 && v[4] == 9 &&
           v[5] == 0 && v[6] == 0 && v[7] == 0 && v[8] == 0, "header fields");
    EXPECT(fd >= 0, "a descriptor");
    if (fd >= 0) {
        seals = fcntl(fd, F_GET_SEALS);
        EXPECT((seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) ==
               (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE), "sealed: %#x", seals);
        EXPECT(pread(fd, got, sizeof(got), 0) == 18, "18 bytes");
        /* Row 1 starts at byte 12 of the source: 12 13 14 15 -> 14 13 12. */
        EXPECT(got[0] == 2 && got[1] == 1 && got[2] == 0 && got[9] == 14 && got[10] == 13 &&
               got[11] == 12, "pixels");
        close(fd);
    }
    EXPECT(w.frames_sent == 1 && w.frames_failed == 1 && w.lines_sent == 1, "counters");
    close(sv[0]);
    close(sv[1]);
}

int main(void)
{
    hello();
    text();
    lines();
    convert();
    channel();
    if (failures) return 1;
    printf("COMPOSITOR_WIRE_UNIT_OK hello text lines convert channel\n");
    return 0;
}
