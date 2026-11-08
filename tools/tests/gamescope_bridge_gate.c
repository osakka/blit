/* ADR 0225: blit-gamescope's cores, without gamescope, PipeWire or a GPU.

     gamescope_bridge_gate unit
         The frame channel client against a fake Blit on a socketpair (hello
         parsing, FRM3 bytes and descriptor, buffers held until RELEASE or
         ERROR and then requeued, stale releases, the memfd fallback after
         ENOTSUP, the INPUT request and its per-image OK / ERROR / pre-INPUT
         answers, another image's answers ignored, a late
         ERROR INPUT <id> ENOENT ending a live route), the placement (a=p and
         a=d, and neither with --no-place) and the terminal
         reader (size report, the ?7701;2$y notice, focus out and in; clicks,
         the wheel, deltas and keys skipped with no event, since the bridge
         never captures; sequences split across reads).

     gamescope_bridge_gate session RESULT_FILE
         Run as a Blit session's command by gamescope_bridge_gate.py: the
         same code against the real server (CPU compositor): CSI 14 t, the
         hello (dmabuf=0), an FRM3 refused ENOTSUP and its buffer requeued,
         fake BGRx buffers sent as sealed-memfd FRM1 and placed with a=p,
         INPUT answered as by a Blit without it. Writes KEY=VALUE lines. */
#include "../../src/blit_gamescope_frames.h"
#include "../../src/blit_gamescope_tty.h"

#include <poll.h>
#include <sys/ioctl.h>
#include <termios.h>
#include <time.h>

static int failures;

#define CHECK(cond, ...) do { if (!(cond)) { ++failures; \
    fprintf(stderr, "GAMESCOPE_BRIDGE_FAIL %s:%d: ", __FILE__, __LINE__); \
    fprintf(stderr, __VA_ARGS__); fputc('\n', stderr); } } while (0)

/* ------------------------------------------------------------ recorders */

static void *requeued[64];
static int requeued_count;

static void record_requeue(void *context, void *cookie)
{
    (void)context;
    if (requeued_count < 64) requeued[requeued_count++] = cookie;
}

static int events[64][3];
static int event_count;

static void record_event(void *context, int event, int a, int b)
{
    (void)context;
    if (event_count < 64) {
        events[event_count][0] = event;
        events[event_count][1] = a;
        events[event_count][2] = b;
        ++event_count;
    }
}

static void feed(GsTty *t, const char *s)
{
    gs_tty_feed(t, (const unsigned char *)s, strlen(s), record_event, NULL);
}

/* The fake Blit's side: one message and the descriptors it carried. */
static ssize_t fake_recv(int fd, unsigned char *buf, size_t cap, int *fds, int *nfds)
{
    struct msghdr msg;
    struct iovec iov;
    union { struct cmsghdr align; char buf[CMSG_SPACE(sizeof(int) * 4)]; } control;
    struct cmsghdr *c;
    ssize_t got;
    memset(&msg, 0, sizeof(msg));
    iov.iov_base = buf;
    iov.iov_len = cap;
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    msg.msg_control = control.buf;
    msg.msg_controllen = sizeof(control.buf);
    *nfds = 0;
    got = recvmsg(fd, &msg, MSG_DONTWAIT);
    for (c = CMSG_FIRSTHDR(&msg); c; c = CMSG_NXTHDR(&msg, c))
        if (c->cmsg_level == SOL_SOCKET && c->cmsg_type == SCM_RIGHTS) {
            int n = (int)((c->cmsg_len - CMSG_LEN(0)) / sizeof(int));
            memcpy(fds, CMSG_DATA(c), sizeof(int) * (size_t)n);
            *nfds = n;
        }
    return got;
}

static void fake_reply(int fd, const char *line) { send(fd, line, strlen(line), 0); }

static uint32_t u32_at(const unsigned char *p) { uint32_t v; memcpy(&v, p, 4); return v; }
static uint64_t u64_at(const unsigned char *p) { uint64_t v; memcpy(&v, p, 8); return v; }

/* ------------------------------------------------------------ unit: frames */

static void unit_hello(void)
{
    GsFrames f;
    gs_frames_init(&f, 1, NULL, NULL);
    CHECK(gs_frames_parse_hello(&f, "OK dmabuf=1 fence=1 formats=XR24/0/100000000000001,AR24/0\n") == 0 &&
          f.dmabuf == 1 && strcmp(f.formats, "XR24/0/100000000000001,AR24/0") == 0,
          "dmabuf=1 with XR24/0 not taken: dmabuf=%d formats=%s", f.dmabuf, f.formats);
    gs_frames_parse_hello(&f, "OK dmabuf=1 fence=0 formats=XR24/100000000000001,AR24/0\n");
    CHECK(f.dmabuf == 0, "XR24 without LINEAR was taken as importable");
    gs_frames_parse_hello(&f, "OK dmabuf=0\n");
    CHECK(f.dmabuf == 0, "dmabuf=0 taken as dmabuf");
    gs_frames_parse_hello(&f, "OK\n");
    CHECK(f.dmabuf == 0, "a version 1 answer taken as dmabuf");
    CHECK(gs_frames_parse_hello(&f, "ERROR 0 EACCES\n") != 0, "a refused hello accepted");
}

static void unit_channel(void)
{
    GsFrames f;
    int sv[2], fds[4], nfds, i, memfd;
    unsigned char msg[512];
    ssize_t got;
    int cookies[8];
    char line[128];

    CHECK(socketpair(AF_UNIX, SOCK_SEQPACKET, 0, sv) == 0, "socketpair");
    gs_frames_init(&f, 1, record_requeue, NULL);
    f.fd = sv[0];
    gs_frames_parse_hello(&f, "OK dmabuf=1 fence=0 formats=XR24/0\n");
    requeued_count = 0;
    memfd = memfd_create("gate-dmabuf", 0);
    CHECK(ftruncate(memfd, 2560 * 480) == 0, "ftruncate");

    /* Three frames sent: each held, each one descriptor, the header exact. */
    for (i = 0; i < 3; ++i)
        CHECK(gs_frames_send_dmabuf(&f, memfd, 640, 480, 0, 2560, &cookies[i]) == 0,
              "FRM3 %d not sent: %s", i, f.last_error);
    for (i = 0; i < 3; ++i) {
        got = fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
        CHECK(got == 72 && memcmp(msg, "FRM3", 4) == 0, "FRM3 %d: %zd bytes", i, got);
        CHECK(u32_at(msg + 4) == 1 && u64_at(msg + 8) == (uint64_t)(i + 1) &&
              u32_at(msg + 16) == 640 && u32_at(msg + 20) == 480 &&
              u32_at(msg + 24) == GS_FOURCC_XR24 && u32_at(msg + 28) == 1 &&
              u64_at(msg + 32) == 0 && u32_at(msg + 40) == 0 && u32_at(msg + 44) == 2560,
              "FRM3 %d header fields wrong", i);
        {
            int j, zero = 1;
            for (j = 48; j < 72; ++j) zero &= msg[j] == 0;
            CHECK(zero, "FRM3 %d: plane fields past the count are not 0", i);
        }
        CHECK(nfds == 1, "FRM3 %d carried %d descriptors", i, nfds);
        if (nfds) close(fds[0]);
    }
    CHECK(requeued_count == 0, "a buffer came back before any reply");

    /* RELEASE gives exactly that buffer back, in the order Blit says. */
    fake_reply(sv[1], "RELEASE 1 2\n");
    gs_frames_read(&f);
    CHECK(requeued_count == 1 && requeued[0] == &cookies[1], "RELEASE 1 2 did not requeue frame 2");
    fake_reply(sv[1], "RELEASE 1 1\n");
    gs_frames_read(&f);
    CHECK(requeued_count == 2 && requeued[1] == &cookies[0], "RELEASE 1 1 did not requeue frame 1");
    /* A release for something not held changes nothing. */
    fake_reply(sv[1], "RELEASE 1 1\n");
    gs_frames_read(&f);
    CHECK(requeued_count == 2 && f.stale_releases == 1, "a repeated RELEASE requeued twice");
    /* HUD1 is understood and holds nothing back. */
    fake_reply(sv[1], "HUD1 on\n");
    gs_frames_read(&f);
    CHECK(f.hud == 1 && requeued_count == 2, "HUD1 on mishandled");

    /* An ERROR for the image gives back the newest held buffer, and ENOTSUP
       turns FRM3 off: the bridge sends memfds from then on. */
    CHECK(gs_frames_send_dmabuf(&f, memfd, 640, 480, 0, 2560, &cookies[3]) == 0, "FRM3 4 not sent");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    if (nfds) close(fds[0]);
    fake_reply(sv[1], "ERROR 1 ENOTSUP\n");
    gs_frames_read(&f);
    CHECK(requeued_count == 3 && requeued[2] == &cookies[3], "ERROR 1 ENOTSUP did not requeue the refused frame");
    CHECK(!gs_frames_use_dmabuf(&f), "FRM3 still used after ENOTSUP");
    /* Frame 3 (seq 3) is still held: it was accepted. */
    CHECK(f.held[0].used + f.held[1].used + f.held[2].used + f.held[3].used == 1,
          "the accepted frame 3 is no longer held");

    /* The memfd path: a sealed memfd, RGB, the sequence shared with FRM3. */
    {
        unsigned char bgrx[4 * 4 * 2];
        for (i = 0; i < 8; ++i) { bgrx[i * 4] = 0x30; bgrx[i * 4 + 1] = 0xa0; bgrx[i * 4 + 2] = 0xd0; bgrx[i * 4 + 3] = 0; }
        CHECK(gs_frames_send_pixels(&f, bgrx, 4, 2, 16) == 0, "FRM1 not sent");
        got = fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
        CHECK(got == 48 && memcmp(msg, "FRM1", 4) == 0 && nfds == 1, "FRM1: %zd bytes, %d fds", got, nfds);
        CHECK(u64_at(msg + 8) == 5 && u32_at(msg + 16) == 4 && u32_at(msg + 20) == 2 &&
              u32_at(msg + 24) == 24 && u32_at(msg + 28) == 12, "FRM1 header wrong (seq %llu)",
              (unsigned long long)u64_at(msg + 8));
        if (nfds) {
            unsigned char px[3];
            int seals = fcntl(fds[0], F_GET_SEALS);
            CHECK(seals >= 0 && (seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) ==
                  (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE), "FRM1 memfd not sealed (%d)", seals);
            CHECK(pread(fds[0], px, 3, 12 + 3) == 3 && px[0] == 0xd0 && px[1] == 0xa0 && px[2] == 0x30,
                  "BGRx not converted to RGB");
            close(fds[0]);
        }
        /* An ERROR after a memfd frame is that frame's: the held one stays. */
        fake_reply(sv[1], "ERROR 1 EINVAL\n");
        gs_frames_read(&f);
        CHECK(requeued_count == 3, "an ERROR for a memfd frame requeued a held dmabuf");
    }

    /* INPUT (docs/api.md): the exact line, then each answer, each naming the
       image; OK input is followed by INPUT on. */
    CHECK(gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei") == 0, "INPUT not sent");
    got = fake_recv(sv[1], msg, sizeof(msg) - 1, fds, &nfds);
    msg[got > 0 ? got : 0] = '\0';
    CHECK(strcmp((char *)msg, "INPUT eis /run/user/1000/gamescope-0-ei 1\n") == 0 && nfds == 0,
          "INPUT message is %s", msg);
    CHECK(f.input_state == GS_INPUT_SENT, "INPUT not pending");
    CHECK(gs_frames_send_input_focus(&f, 1) != 0, "INPUT on sent before OK input");
    /* Another picture's answers on a shared channel are not this one's;
       the old unnumbered forms are not answers at all. */
    fake_reply(sv[1], "OK input 2\n");
    fake_reply(sv[1], "ERROR INPUT 2 EBUSY\n");
    fake_reply(sv[1], "OK input\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_SENT && !f.input_on, "another image's or an unnumbered answer was taken");
    fake_reply(sv[1], "OK input 1\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_OK && f.input_on, "OK input 1 not taken");
    got = fake_recv(sv[1], msg, sizeof(msg) - 1, fds, &nfds);
    msg[got > 0 ? got : 0] = '\0';
    CHECK(strcmp((char *)msg, "INPUT on 1\n") == 0 && nfds == 0, "after OK input came %s", msg);
    CHECK(gs_frames_send_input_focus(&f, 0) == 0 && !f.input_on, "INPUT off not sent");
    got = fake_recv(sv[1], msg, sizeof(msg) - 1, fds, &nfds);
    msg[got > 0 ? got : 0] = '\0';
    CHECK(strcmp((char *)msg, "INPUT off 1\n") == 0, "INPUT off message is %s", msg);
    gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    fake_reply(sv[1], "ERROR INPUT 1 ENOTSUP\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_UNSUPPORTED && !f.input_on, "ERROR INPUT ENOTSUP not taken as unsupported");
    gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    {
        int before = requeued_count;
        fake_reply(sv[1], "ERROR INPUT 1 EACCES\n");
        gs_frames_read(&f);
        CHECK(f.input_state == GS_INPUT_REFUSED && strcmp(f.input_error, "EACCES") == 0 &&
              requeued_count == before, "ERROR INPUT EACCES");
    }
    CHECK(gs_frames_send_input(&f, "relative-ei") != 0, "a relative EIS path was sent");

    /* A live route that Blit ends later (docs/api.md: the compositor refused
       its handshake late or went away) is told apart from a refusal. */
    gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    CHECK(!f.input_was_ok, "a new INPUT request kept input_was_ok");
    fake_reply(sv[1], "OK input 1\n");
    gs_frames_read(&f);
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds); /* INPUT on 1 */
    CHECK(f.input_state == GS_INPUT_OK && f.input_was_ok && f.input_on, "OK input before the late ENOENT");
    /* Another picture's route ending leaves this one up. */
    fake_reply(sv[1], "ERROR INPUT 3 ENOENT\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_OK && f.input_on, "another image's late ENOENT ended this route");
    fake_reply(sv[1], "ERROR INPUT 1 ENOENT\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_REFUSED && f.input_was_ok && !f.input_on &&
          strcmp(f.input_error, "ENOENT") == 0,
          "late ERROR INPUT ENOENT: state %d was_ok %d on %d error %s", f.input_state,
          f.input_was_ok, f.input_on, f.input_error);
    /* ENOENT before any OK input is a refusal, as before. */
    gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    fake_reply(sv[1], "ERROR INPUT 1 ENOENT\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_REFUSED && !f.input_was_ok && strcmp(f.input_error, "ENOENT") == 0,
          "early ERROR INPUT ENOENT taken as a live route ending");
    /* A line Blit could not read is answered with image 0: still ours. */
    gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei");
    fake_recv(sv[1], msg, sizeof(msg), fds, &nfds);
    fake_reply(sv[1], "ERROR INPUT 0 EINVAL\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_REFUSED && strcmp(f.input_error, "EINVAL") == 0,
          "ERROR INPUT 0 EINVAL not taken");

    /* Under --no-place (phase 2) the desktop app owns focus and sends
       INPUT on/off itself through Hibr's listener: OK input is followed by
       nothing from the bridge. Phase 1 (above) still sends INPUT on. */
    f.focus_by_desktop = 1;
    CHECK(gs_frames_send_input(&f, "/run/user/1000/gamescope-0-ei") == 0, "no-place: INPUT not sent");
    got = fake_recv(sv[1], msg, sizeof(msg) - 1, fds, &nfds);
    msg[got > 0 ? got : 0] = '\0';
    CHECK(strcmp((char *)msg, "INPUT eis /run/user/1000/gamescope-0-ei 1\n") == 0 && nfds == 0,
          "no-place: INPUT message is %s", msg);
    fake_reply(sv[1], "OK input 1\n");
    gs_frames_read(&f);
    CHECK(f.input_state == GS_INPUT_OK && f.input_was_ok && !f.input_on,
          "no-place: OK input 1: state %d was_ok %d on %d", f.input_state, f.input_was_ok, f.input_on);
    got = fake_recv(sv[1], msg, sizeof(msg) - 1, fds, &nfds);
    msg[got > 0 ? got : 0] = '\0';
    CHECK(got < 0 && errno == EAGAIN, "no-place: after OK input the bridge sent %s", msg);
    f.focus_by_desktop = 0;

    /* Blit closing the channel gives every held buffer back. */
    close(sv[1]);
    CHECK(gs_frames_read(&f) == -1 && f.fd == -1, "a closed channel was not noticed");
    CHECK(requeued_count == 4 && requeued[3] == &cookies[2], "held frame 3 not requeued on close");
    close(memfd);
}

/* ------------------------------------------------------------ unit: tty */

static void unit_tty(void)
{
    GsTty t;
    gs_tty_init(&t);
    event_count = 0;
    feed(&t, "\033[4;720;1280t");
    CHECK(event_count == 1 && events[0][0] == GS_TTY_SIZE && events[0][1] == 1280 && events[0][2] == 720,
          "size report not read");
    /* Kitty keys (flags 1/2/4/8), motion, the wheel and a release: nothing. */
    event_count = 0;
    feed(&t, "\033[97;1u\033[97:65:97;2:3u\033[1;5:1A\033[57441;2u\033[13;1:3u"
             "\033[<35;100;50M\033[<64;10;10M\033[<0;10;10m\033[I");
    CHECK(event_count == 1 && events[0][0] == GS_TTY_FOCUS_IN, "keys, motion or wheel produced events (%d)",
          event_count);
    /* The bridge never captures: clicks (left, middle, right, press and
       release), wheel notches and pointer-lock deltas give no event, and
       nothing is ever written back (the reader has no output; the bridge
       writes ?7701h nowhere). */
    event_count = 0;
    feed(&t, "\033[<0;640;360M\033[<0;640;360m\033[<1;5;5M\033[<2;5;5M\033[<2;5;5m"
             "\033[<64;10;10M\033[<65;10;10M\033[<68;10;10M\033[<35;-4;7M\033[<35;12;-3M\033[<0;0;0M");
    CHECK(event_count == 0, "clicks, the wheel or deltas produced %d events", event_count);
    /* A desktop's lock ending is only a notice. */
    feed(&t, "\033[?7701;2$y");
    CHECK(event_count == 1 && events[0][0] == GS_TTY_LOCK_ENDED, "?7701;2$y not read");
    /* Focus out, split across reads, then in. */
    event_count = 0;
    feed(&t, "\033");
    feed(&t, "[");
    feed(&t, "O");
    feed(&t, "\033[I");
    CHECK(event_count == 2 && events[0][0] == GS_TTY_FOCUS_OUT && events[1][0] == GS_TTY_FOCUS_IN,
          "split CSI O or CSI I misread");
    /* An APC answer with ESC inside, then a split size report, read whole. */
    event_count = 0;
    feed(&t, "\033_Gi=1;OK\033\\\033[4;48");
    feed(&t, "0;640t");
    CHECK(event_count == 1 && events[0][0] == GS_TTY_SIZE && events[0][1] == 640 && events[0][2] == 480,
          "a split size report after an APC was misread");
    /* No release key: Ctrl+Alt+M is a key like any other. */
    event_count = 0;
    feed(&t, "\033[109;7u\033[109;7:3u");
    CHECK(event_count == 0, "Ctrl+Alt+M did something");
}

/* ------------------------------------------------------------ session */

static int session_size_w, session_size_h;

static void session_event(void *context, int event, int a, int b)
{
    (void)context;
    if (event == GS_TTY_SIZE) { session_size_w = a; session_size_h = b; }
}

static int run_session(const char *result_path)
{
    FILE *out = fopen(result_path, "w");
    GsFrames f;
    GsPlacement placement = { 0, 0 };
    GsTty t;
    struct termios raw, saved;
    int cookie, memfd, i, cols = 10, rows = 4;
    struct winsize ws;
    unsigned char *bgrx;
    char seq[128];
    time_t start;

    if (!out) return 1;
    tcgetattr(0, &saved);
    raw = saved;
    cfmakeraw(&raw);
    tcsetattr(0, TCSANOW, &raw);
    printf("\033[?25l");
    fflush(stdout);
    gs_tty_init(&t);
    /* The pane's size in pixels, as the bridge reads it. */
    write(1, "\033[14t", 5);
    start = time(NULL);
    while (!session_size_w && time(NULL) - start < 3) {
        struct pollfd p = { 0, POLLIN, 0 };
        unsigned char buf[128];
        if (poll(&p, 1, 200) > 0) {
            ssize_t n = read(0, buf, sizeof(buf));
            if (n > 0) gs_tty_feed(&t, buf, (size_t)n, session_event, NULL);
        }
    }
    if (ioctl(1, TIOCGWINSZ, &ws) == 0)
        fprintf(out, "WINSIZE=%dx%d\n", ws.ws_col, ws.ws_row);
    fprintf(out, "SIZE=%dx%d\n", session_size_w, session_size_h);

    gs_frames_init(&f, 1, record_requeue, NULL);
    if (gs_frames_connect(&f, getenv("BLIT_FRAME_SOCKET") ? getenv("BLIT_FRAME_SOCKET") : "",
                          getenv("BLIT_FRAME_TOKEN") ? getenv("BLIT_FRAME_TOKEN") : "") != 0) {
        fprintf(out, "CONNECT=%s\n", f.last_error);
        fclose(out);
        return 1;
    }
    fprintf(out, "CONNECT=ok\nDMABUF=%d\n", f.dmabuf);
    fflush(out);

    /* An FRM3 (a memfd stands in for the dmabuf): this compositor cannot
       import, so it is refused and the buffer comes back. Sent even though
       the hello said dmabuf=0, to prove the requeue on ERROR. */
    f.dmabuf = 1;
    memfd = memfd_create("gate-fake-dmabuf", 0);
    ftruncate(memfd, 64 * 4 * 48);
    requeued_count = 0;
    gs_frames_send_dmabuf(&f, memfd, 64, 48, 0, 256, &cookie);
    close(memfd);
    for (i = 0; i < 40 && requeued_count == 0; ++i) {
        usleep(50000);
        gs_frames_read(&f);
    }
    fprintf(out, "FRM3_ERROR=%s\nFRM3_REQUEUED=%d\nFRM3_OFF=%d\n", f.last_error,
            requeued_count == 1 && requeued[0] == &cookie, !gs_frames_use_dmabuf(&f));
    fflush(out);

    /* Fake gamescope buffers: BGRx, a stride wider than the row, sent by
       memfd and placed to fill 10x4 cells (scaled). */
    bgrx = calloc(1, 320 * 48);
    for (i = 0; i < 64 * 48; ++i) {
        unsigned char *p = bgrx + (i / 64) * 320 + (i % 64) * 4;
        p[0] = 0x30; p[1] = 0xa0; p[2] = 0xd0; p[3] = 0x00;
    }
    for (i = 0; i < 3; ++i) {
        gs_frames_send_pixels(&f, bgrx, 64, 48, 320);
        usleep(100000);
        gs_frames_read(&f);
        /* Placed once, after the first frame, as the bridge does. */
        if (i == 0 && gs_placement_place(&placement, seq, sizeof(seq), 1, cols, rows) > 0) {
            fputs(seq, stdout);
            fflush(stdout);
        }
    }
    fprintf(out, "FRM1_SENT=%lu\nERRORS=%lu\nLAST_ERROR=%s\n", f.sent_memfd, f.errors, f.last_error);
    fflush(out);

    /* INPUT, answered by this Blit. */
    gs_frames_send_input(&f, "/run/user/0/gamescope-0-ei");
    for (i = 0; i < 40 && f.input_state == GS_INPUT_SENT; ++i) {
        usleep(50000);
        gs_frames_read(&f);
    }
    fprintf(out, "INPUT_STATE=%d\nINPUT_ERROR=%s\nDONE=1\n", f.input_state, f.input_error);
    fclose(out);
    /* Keep the picture up until the gate is done. */
    for (;;) {
        usleep(200000);
        if (gs_frames_read(&f) != 0) break;
    }
    return 0;
}

/* a=p after the first frame and a=d at exit; with --no-place neither, ever,
   whatever the bridge asks. */
static void unit_placement(void)
{
    GsPlacement normal, none;
    char out[128];
    int n;
    memset(&normal, 0, sizeof(normal));
    memset(&none, 0, sizeof(none));
    none.no_place = 1;
    CHECK(gs_placement_delete(&normal, out, sizeof(out), 1) == 0, "a=d before any a=p");
    n = gs_placement_place(&normal, out, sizeof(out), 1, 10, 4);
    CHECK(n > 0 && strcmp(out, "\033[H\033_Ga=p,i=1,q=2,C=1,c=10,r=4\033\\") == 0 && normal.placed,
          "the placement is %s", out);
    n = gs_placement_delete(&normal, out, sizeof(out), 1);
    CHECK(n > 0 && strcmp(out, "\033_Ga=d,d=I,i=1,q=2\033\\") == 0 && !normal.placed, "the delete is %s", out);
    CHECK(gs_placement_delete(&normal, out, sizeof(out), 1) == 0, "a=d twice");
    CHECK(gs_placement_place(&none, out, sizeof(out), 1, 10, 4) == 0 && !none.placed, "--no-place wrote a=p");
    CHECK(gs_placement_place(&none, out, sizeof(out), 1, 20, 8) == 0 && !none.placed, "--no-place wrote a=p on a resize");
    none.placed = 1; /* even if it were somehow marked placed */
    CHECK(gs_placement_delete(&none, out, sizeof(out), 1) == 0, "--no-place wrote a=d");
}

int main(int argc, char **argv)
{
    if (argc >= 2 && strcmp(argv[1], "unit") == 0) {
        unit_hello();
        unit_channel();
        unit_placement();
        unit_tty();
        if (failures) {
            fprintf(stderr, "GAMESCOPE_BRIDGE_UNIT_FAIL %d\n", failures);
            return 1;
        }
        printf("GAMESCOPE_BRIDGE_UNIT_OK hello+frm3+release+error+fallback+memfd+input-per-image+late-enoent+"
               "no-place-no-input-on+placement+no-place+tty-no-capture\n");
        return 0;
    }
    if (argc >= 3 && strcmp(argv[1], "session") == 0)
        return run_session(argv[2]);
    fprintf(stderr, "usage: %s unit | session RESULT_FILE\n", argv[0]);
    return 2;
}
