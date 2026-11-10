/* ADR 0228: the physical keyboard and mouse, as lines, to blit-compositor's
   input socket for one of its windows.

   A frame client (blit-compositor, or a desktop relaying its windows'
   pictures) asks, on its authenticated channel, for the input of its
   session to go to the input socket of the window one of its images shows.
   The wire text is docs/api.md, "Picture frames by descriptor" and
   "Program windows", and only that; each line is version 5's
   `INPUT <verb> <image id> [arguments]`:

       INPUT sock <image id> <absolute path>\n
           INPUT ok <image id> / INPUT error <image id> <code>
       INPUT on <image id>\n   INPUT off <image id>\n
           whether that picture's window has the keys
       INPUT hold <image id> relative|confine\n
           INPUT ok <image id> / INPUT error <image id> EINVAL
           what the route does while its session holds the pointer
       INPUT member <popup id> <top-level id> <x> <y>\n
           INPUT ok <popup id> / INPUT error <popup id> <code>
           a popup's picture joins its top-level's route, at that offset

   Each route is one image's, so one channel carries the routes of several
   pictures (a desktop multiplexes its windows over one channel per display,
   Hibr #486): one per picture, so up to BLIT_FRAME_INPUT_ROUTES (the
   session's picture ceiling, ADR 0226) per channel, each with its own
   connection and held keys and buttons. At most one route of a session is
   on, across all of its channels: `INPUT on` turns the others off,
   releasing what they held, before the new one gets anything. A route ends
   with its image or its channel; a compositor that closes its socket later
   ends only that route, told `INPUT error <image id> ENOENT`.

   The name comes from a peer whose uid Blit read at the hello, never from a
   terminal stream (ADR 0066). The socket must be that uid's: the file's
   owner, checked before Blit connects, and the listening process's
   credentials (SO_PEERCRED) on the connected socket, so a socket swapped
   between the two checks is refused too. Blit never connects to another
   user's compositor.

   Blit writes lines on the stream socket it connected to, in the picture's
   own surface pixels:

       INPUT key <id> <evdev code> press|release
       INPUT ptr <id> <x> <y>          absolute, pixels, two decimals
       INPUT rel <id> <dx> <dy>        under a relative hold
       INPUT btn <id> <BTN code> press|release
       INPUT wheel <id> <dx> <dy>      notches, positive right and down
       INPUT frame <id> <monotonic us> after each event
       INPUT keymap <id> <revision> <size> <rate> <delay>
                                       no frame: the keymap itself, below

   `INPUT keymap` carries the keymap of the keyboard whose keys reach the
   window (what GET /v1/input/keymap serves by default; one source,
   input_keymap_of): a sealed memfd of `size` bytes, its xkb text and a
   NUL, passed with the line (SCM_RIGHTS), and its repeat (per second and
   ms; 0 0 none). It is written when the route is live and has not been
   sent the current revision -- at once when the revision changes (a
   keyboard configured, plugged or gone, or a key from another keyboard,
   before that key), and when the route goes live after a change. The
   compositor needs nothing else of Blit's to have a keymap: no API, no
   token (ADR 0228).

   Members (ADR 0228, "One input route per top-level"): a popup's picture
   joins its top-level's route with `INPUT member`, at the popup's offset in
   the top-level's surface pixels. The pointer over a member's drawn
   rectangle is mapped into the member's frame and then by that offset into
   the top-level's surface pixels, so the compositor sees one coordinate
   space per top-level and hit-tests its own popups; the topmost draw of the
   route's pictures wins. Keys stay the top-level's. A member ends with its
   picture (deleted, or gone after it was held), or with its route.

   A line the socket cannot take (full, or the compositor gone) is never
   dropped silently: the route is broken, counted in frames.input
   `dropped`, what it held is forgotten (the compositor releases all on the
   socket's end), and at the next reconcile it ends with `INPUT error <id>
   ENOENT`, as a route whose compositor closed its socket.

   A route is live only while it is on, its channel's session is Blit's
   focused session and Blit holds the console (and its image exists), so at
   most one route is live at a time. While live, evdev keys (press and
   release, no repeats), buttons, wheel notches, and either the absolute
   pointer over the image's drawn rectangle or, under pointer lock (?7701),
   the raw relative motion, are sent; the session gets its keys as before.
   A route in hold `confine` keeps the pointer instead, under the lock:
   Blit's pointer stays shown, moves inside the picture's drawn rectangle,
   and is sent as an absolute position (frame_input_confined).
   When it stops being live, every key and button pressed through it is
   released. Included at the end of blit_frames.h. */

typedef struct BlitFrameInput BlitFrameInput;

/* A popup's picture in its top-level's route (INPUT member). More popups
   than this open at once over one window are refused EBUSY. */
#define BLIT_FRAME_INPUT_MEMBERS 16

typedef struct {
    unsigned int image_id;
    int x, y;               /* its offset in the top-level's surface pixels */
    int seen;               /* the picture has existed: its absence is a delete */
} BlitFrameMember;

struct BlitFrameInput {
    int sock;               /* the compositor's input socket, polled in Blit's loop */
    const char *broken;     /* a line the socket did not take: why; ended at reconcile */
    unsigned int image_id;
    int on;                 /* INPUT on / INPUT off */
    int confine;            /* INPUT hold: 0 relative (the default), 1 confine */
    int image_seen;         /* the image has existed: its absence is a delete */
    dev_t sock_dev;         /* the socket file's identity: one route per window socket */
    ino_t sock_ino;
    unsigned char keys[(KEY_MAX + 8) / 8];  /* pressed through this route */
    unsigned int buttons;   /* Blit's buttons 0..2 pressed through it */
    /* The last absolute pointer, for diagnostics: where it was on the
       session's surface, the drawn rectangle it was mapped through, what
       was sent, or why nothing was ("" once something was sent). */
    int last_surface[2];
    int last_draw[4];
    double last_sent[2];
    unsigned int last_over;     /* the picture the pointer was over: the route's or a member's */
    unsigned long long keymap_told; /* the keymap revision this route was sent */
    const char *last_miss;
    BlitFrameMember members[BLIT_FRAME_INPUT_MEMBERS];
    int member_count;
};

static int frame_input_key_held(const BlitFrameInput *in, unsigned int code)
{
    return code <= KEY_MAX && (in->keys[code / 8] >> (code % 8)) & 1;
}

static void frame_input_key_mark(BlitFrameInput *in, unsigned int code, int held)
{
    if (code > KEY_MAX)
        return;
    if (held)
        in->keys[code / 8] |= (unsigned char)(1u << (code % 8));
    else
        in->keys[code / 8] &= (unsigned char)~(1u << (code % 8));
}

/* Blit's buttons 0, 1, 2 as the evdev codes `INPUT btn` carries. */
static uint32_t frame_input_button_code(int button)
{
    return button == BLIT_POINTER_BUTTON_LEFT ? BTN_LEFT :
        button == BLIT_POINTER_BUTTON_MIDDLE ? BTN_MIDDLE : BTN_RIGHT;
}

static void frame_input_error(BlitSession *session, const char *code)
{
    if (session != NULL)
        snprintf(session->frame_input_error, sizeof(session->frame_input_error), "%s", code);
}

/* Bytes on a route, in one write, with a descriptor when `fd` is one. A
   socket that cannot take all of them breaks the route (see the top of
   this file). Returns 0 when written. */
static int frame_input_write(BlitSession *session, BlitFrameInput *in, const char *text,
                             size_t length, int fd)
{
    char control[CMSG_SPACE(sizeof(int))];
    struct iovec iov = { (void *)text, length };
    struct msghdr msg;
    ssize_t sent;
    memset(&msg, 0, sizeof(msg));
    msg.msg_iov = &iov;
    msg.msg_iovlen = 1;
    if (fd >= 0) {
        struct cmsghdr *cmsg;
        memset(control, 0, sizeof(control));
        msg.msg_control = control;
        msg.msg_controllen = sizeof(control);
        cmsg = CMSG_FIRSTHDR(&msg);
        cmsg->cmsg_level = SOL_SOCKET;
        cmsg->cmsg_type = SCM_RIGHTS;
        cmsg->cmsg_len = CMSG_LEN(sizeof(int));
        memcpy(CMSG_DATA(cmsg), &fd, sizeof(int));
    }
    sent = sendmsg(in->sock, &msg, MSG_NOSIGNAL | MSG_DONTWAIT);
    if (sent == (ssize_t)length)
        return 0;
    in->broken = sent < 0 && (errno == EAGAIN || errno == EWOULDBLOCK) ? "sock-full"
                                                                       : "sock-disconnected";
    memset(in->keys, 0, sizeof(in->keys));
    in->buttons = 0;
    if (session != NULL) {
        ++session->frame_input_dropped;
        frame_input_error(session, in->broken);
    }
    return -1;
}

/* The keymap of the keyboard whose keys reach the window, and its repeat,
   to a route: `INPUT keymap <id> <revision> <size> <rate> <delay>`
   with a sealed memfd of its text and a NUL. Returns 0 when written. */
static int frame_input_send_keymap(BlitRuntime *runtime, BlitSession *session, BlitFrameInput *in)
{
    static char keymap[256 * 1024];
    char text[128];
    size_t length;
    int rate, delay, fd, n, result;
    length = input_keymap_of(input_keymap_device(runtime), keymap, sizeof(keymap), &rate, &delay);
    if (length == 0)
        return -1;
    fd = memfd_create("blit-keymap", MFD_CLOEXEC | MFD_ALLOW_SEALING);
    if (fd < 0 || write(fd, keymap, length + 1) != (ssize_t)(length + 1) ||
        fcntl(fd, F_ADD_SEALS, F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE | F_SEAL_SEAL) != 0) {
        if (fd >= 0) close(fd);
        return -1;
    }
    n = snprintf(text, sizeof(text), "INPUT keymap %u %llu %zu %d %d\n", in->image_id,
                 runtime->keymap_revision, length + 1, rate, delay);
    result = frame_input_write(session, in, text, (size_t)n, fd);
    close(fd);
    return result;
}

/* One event on a route: its line and the frame after it, in one write.
   Returns 0 when written. */
static int frame_input_line(BlitSession *session, BlitFrameInput *in, const char *fmt, ...)
{
    char text[160];
    va_list ap;
    int n, m;
    if (in->broken != NULL)
        return -1;
    va_start(ap, fmt);
    n = vsnprintf(text, sizeof(text), fmt, ap);
    va_end(ap);
    if (n < 0 || (size_t)n >= sizeof(text) - 48)
        return -1;
    m = snprintf(text + n, sizeof(text) - (size_t)n, "INPUT frame %u %llu\n", in->image_id,
                 (unsigned long long)monotonic_us());
    if (frame_input_write(session, in, text, (size_t)(n + m), -1) != 0)
        return -1;
    if (session != NULL)
        ++session->frame_input_events;
    return 0;
}

/* Whether the image the route names is gone: deleted by Kitty, or no longer
   held after it was. */
static int frame_input_image_gone(BlitSession *session, BlitFrameInput *in)
{
    BlitGraphicsImage *image = graphics_image_find(session, in->image_id);
    if (image != NULL && image->descriptor) {
        in->image_seen = 1;
        return 0;
    }
    return in->image_seen || frame_was_deleted(session, in->image_id);
}

/* Whether a member's picture is gone, by the same rule. */
static int frame_input_member_gone(BlitSession *session, BlitFrameMember *m)
{
    BlitGraphicsImage *image = graphics_image_find(session, m->image_id);
    if (image != NULL && image->descriptor) {
        m->seen = 1;
        return 0;
    }
    return m->seen || frame_was_deleted(session, m->image_id);
}

/* The one rule: on, the focused session's, the console Blit's, the image
   there, the compositor still there. */
static int frame_input_live(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session;
    if (in == NULL || in->broken != NULL || !in->on || runtime->console_away ||
        !blit_vt_active())
        return 0;
    session = find_session_id(runtime, client->session_id);
    return session != NULL && session->focused && !frame_input_image_gone(session, in);
}

/* Every key and button pressed through the route released. */
static void frame_input_end(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session;
    unsigned int code;
    int i;
    if (in == NULL)
        return;
    if (in->buttons == 0) {
        for (code = 0; code < sizeof(in->keys) && in->keys[code] == 0; ++code) {}
        if (code == sizeof(in->keys))
            return;  /* nothing held */
    }
    session = find_session_id(runtime, client->session_id);
    for (code = 0; code <= KEY_MAX; ++code)
        if (frame_input_key_held(in, code)) {
            frame_input_key_mark(in, code, 0);
            frame_input_line(session, in, "INPUT key %u %u release\n", in->image_id, code);
        }
    for (i = 0; i < 3; ++i)
        if (in->buttons & (1u << i)) {
            in->buttons &= ~(1u << i);
            frame_input_line(session, in, "INPUT btn %u %u release\n", in->image_id,
                             frame_input_button_code(i));
        }
}

/* The route brought in line with the rule: while live, sent the keymap
   when it has not been sent this revision (ADR 0228); when not, ended
   with its releases. Returns whether it is live. */
static int frame_input_sync(BlitRuntime *runtime, const BlitFrameClient *client, BlitFrameInput *in)
{
    if (in == NULL)
        return 0;
    if (!frame_input_live(runtime, client, in)) {
        frame_input_end(runtime, client, in);
        return 0;
    }
    if (in->keymap_told != runtime->keymap_revision) {
        in->keymap_told = runtime->keymap_revision;
        if (frame_input_send_keymap(runtime, find_session_id(runtime, client->session_id), in) != 0)
            return 0;
    }
    return 1;
}

/* The route in a slot gone: its connection closed, its slot free. Whatever
   it held was released before, or is a gone compositor's to forget. */
static void frame_input_free(BlitFrameClient *client, int slot)
{
    BlitFrameInput *in = client->inputs[slot];
    if (in == NULL)
        return;
    close(in->sock);
    free(in);
    client->inputs[slot] = NULL;
    --frame_input_routes;
}

static void frame_input_disconnected(BlitRuntime *runtime, BlitFrameClient *client, int slot,
                                     const char *why);

/* Every route brought in line, and a route whose image has gone ended: its
   keys released by the sync, then it is freed. The channel is not told;
   its image is gone. A broken route ends as a disconnected one. */
static void frame_input_reconcile(BlitRuntime *runtime)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        BlitSession *session;
        if (!client->used)
            continue;
        session = find_session_id(runtime, client->session_id);
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in == NULL)
                continue;
            if (in->broken != NULL) {
                /* A line it could not take: what it held is forgotten,
                   and the channel told the route has ended. */
                frame_input_disconnected(runtime, client, k, in->broken);
                continue;
            }
            frame_input_sync(runtime, client, in);
            if (session != NULL) {
                /* A member ends with its picture. */
                int m = 0;
                while (m < in->member_count)
                    if (frame_input_member_gone(session, &in->members[m]))
                        in->members[m] = in->members[--in->member_count];
                    else
                        ++m;
            }
            if (session != NULL && frame_input_image_gone(session, in)) {
                frame_input_end(runtime, client, in);
                frame_input_free(client, k);
            }
        }
    }
}

/* The compositor closed its input socket, or the socket would not take a
   line: the client is told `INPUT error <image id> ENOENT` now and that
   route ends (nothing held can be released to a compositor that has gone).
   The channel's other routes are untouched. */
static void frame_input_disconnected(BlitRuntime *runtime, BlitFrameClient *client, int slot,
                                     const char *why)
{
    char line[48];
    snprintf(line, sizeof(line), "INPUT error %u ENOENT\n", client->inputs[slot]->image_id);
    frame_input_free(client, slot);
    frame_input_error(find_session_id(runtime, client->session_id), why);
    frame_reply(client->fd, line);
}

/* The channel closed: its routes end with it, each releasing what it held
   first. */
static void frame_input_close(BlitRuntime *runtime, BlitFrameClient *client)
{
    int k;
    for (k = 0; k < client->input_capacity; ++k)
        if (client->inputs[k] != NULL) {
            frame_input_end(runtime, client, client->inputs[k]);
            frame_input_free(client, k);
        }
    free(client->inputs);
    client->inputs = NULL;
    client->input_capacity = 0;
}

static void frame_input_select(const BlitRuntime *runtime, fd_set *readable, int *max_fd)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        if (!runtime->frame_clients[i].used)
            continue;
        for (k = 0; k < runtime->frame_clients[i].input_capacity; ++k) {
            const BlitFrameInput *in = runtime->frame_clients[i].inputs[k];
            if (in != NULL) {
                FD_SET(in->sock, readable);
                if (in->sock > *max_fd) *max_fd = in->sock;
            }
        }
    }
}

/* The compositor says nothing on its input socket: what is readable is its
   end, or bytes that mean nothing. */
static void frame_input_service(BlitRuntime *runtime, const fd_set *readable)
{
    int i, k;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            char drain[256];
            ssize_t got;
            if (in == NULL || !FD_ISSET(in->sock, readable))
                continue;
            got = recv(in->sock, drain, sizeof(drain), MSG_DONTWAIT);
            if (got == 0 || (got < 0 && errno != EAGAIN && errno != EWOULDBLOCK && errno != EINTR))
                frame_input_disconnected(runtime, client, k, "sock-disconnected");
        }
    }
}

/* ---- What evdev dispatch calls; each a no-op unless a route is live. ----
   Every route is synced, so one that stopped being live releases what it
   held; the live one, at most one, gets the event. */

static void frame_input_key(BlitRuntime *runtime, unsigned int code, int pressed)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in == NULL || !frame_input_sync(runtime, client, in))
                continue;
            /* A release only for a key pressed here: one pressed before the
               route went live was never the compositor's. */
            if (!pressed && !frame_input_key_held(in, code))
                continue;
            frame_input_key_mark(in, code, pressed);
            frame_input_line(find_session_id(runtime, client->session_id), in,
                             "INPUT key %u %u %s\n", in->image_id, code,
                             pressed ? "press" : "release");
        }
    }
}

static void frame_input_button(BlitRuntime *runtime, int button, int pressed)
{
    int i, k;
    if (button < 0 || button > 2)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in == NULL || !frame_input_sync(runtime, client, in) ||
                (!pressed && !(in->buttons & (1u << button))))
                continue;
            if (pressed)
                in->buttons |= 1u << button;
            else
                in->buttons &= ~(1u << button);
            frame_input_line(find_session_id(runtime, client->session_id), in,
                             "INPUT btn %u %u %s\n", in->image_id,
                             frame_input_button_code(button), pressed ? "press" : "release");
        }
    }
}

/* One notch: `direction` positive is up (or right), as REL_WHEEL counts it
   after the device's natural scrolling; the line counts as Wayland does,
   positive right and down. */
static void frame_input_scroll(BlitRuntime *runtime, int horizontal, int direction)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in == NULL || !frame_input_sync(runtime, client, in) || direction == 0)
                continue;
            frame_input_line(find_session_id(runtime, client->session_id), in,
                             "INPUT wheel %u %d %d\n", in->image_id,
                             horizontal ? (direction > 0 ? 1 : -1) : 0,
                             horizontal ? 0 : (direction > 0 ? -1 : 1));
        }
    }
}

/* The pointer over one route's image's drawn rectangle, as a position in
   the image's own surface pixels (its latest frame): wherever the picture
   is placed and however large it is drawn, through the same placement and
   draw rectangle the compositors use. The route's members are hit-tested
   with it, the topmost draw winning: a point on a member's picture is the
   member's offset plus the point within it, in the top-level's pixels
   (negative where a popup reaches past the top-level's left or top). */
static void frame_input_pointer_route(BlitRuntime *runtime, BlitFrameClient *client, BlitFrameInput *in)
{
    BlitSession *session, *under;
    BlitGraphicsImage *image, *images[1 + BLIT_FRAME_INPUT_MEMBERS];
    static BlitGraphicsDrawList list;
    BlitGraphicsDraw *draws;
    int column, row, px = 0, py = 0, count, k, m, n;
    session = find_session_id(runtime, client->session_id);
    if (session == NULL || !runtime->pointer_valid)
        return;
    under = mouse_target_px(runtime, runtime->pointer_output, runtime->pointer_x,
                            runtime->pointer_y, &column, &row, &px, &py);
    image = graphics_image_find(session, in->image_id);
    if (under != session || image == NULL) {
        in->last_miss = under != session ? "other-session" : "no-image";
        return;
    }
    /* Surface pixels, zero-based; the topmost draw under them wins. */
    --px;
    --py;
    in->last_surface[0] = px;
    in->last_surface[1] = py;
    in->last_miss = "outside-picture";
    images[0] = image;
    n = 1;
    for (m = 0; m < in->member_count; ++m) {
        BlitGraphicsImage *member = graphics_image_find(session, in->members[m].image_id);
        if (member != NULL && member->descriptor)
            images[n++] = member;
    }
    count = blit_graphics_collect(images, n, &list);
    draws = list.draws;
    for (k = count - 1; k >= 0; --k) {
        const BlitGraphicsDraw *d = &draws[k];
        const BlitGraphicsImage *over = d->image;
        double fx, fy, tx, ty;
        if (d->width < 1 || d->height < 1 || over->width < 1 || over->height < 1 ||
            image->width < 1 || image->height < 1 ||
            px < d->x || py < d->y || px >= d->x + d->width || py >= d->y + d->height)
            continue;
        /* Where in the picture drawn, as a fraction of its frame. */
        fx = (d->src_x + (double)(px - d->x) * d->src_w / d->width) / over->width;
        fy = (d->src_y + (double)(py - d->y) * d->src_h / d->height) / over->height;
        /* That point in the top-level's surface pixels, its latest frame. */
        tx = fx * over->width;
        ty = fy * over->height;
        if (over != image)
            for (m = 0; m < in->member_count; ++m)
                if (in->members[m].image_id == d->image_id) {
                    tx += in->members[m].x;
                    ty += in->members[m].y;
                    break;
                }
        in->last_over = d->image_id;
        in->last_draw[0] = d->x; in->last_draw[1] = d->y;
        in->last_draw[2] = d->width; in->last_draw[3] = d->height;
        in->last_sent[0] = tx;
        in->last_sent[1] = ty;
        in->last_miss = "";
        frame_input_line(session, in, "INPUT ptr %u %.2f %.2f\n", in->image_id, tx, ty);
        break;
    }
}

/* The absolute pointer, to the live route only. */
static void frame_input_pointer(BlitRuntime *runtime)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k)
            if (client->inputs[k] != NULL && frame_input_sync(runtime, client, client->inputs[k]))
                frame_input_pointer_route(runtime, client, client->inputs[k]);
    }
}

/* ADR 0225, hold modes: whether the pointer lock is confined, and to which
   rectangle. It is while the holder's live route is in hold `confine`, its
   picture is drawn and the pointer is on the output that shows it. `box`,
   when asked for, is the picture's topmost draw (the one the mapping above
   tries first) in the pointer's coordinates, cut to the session's surface
   and its display, so a pointer kept inside it always maps. Otherwise the
   lock is relative, as ADR 0198 has it: hidden, still, raw motion sent. The
   pointer's visibility and its motion both ask this, and nothing else. */
static int frame_input_confined(BlitRuntime *runtime, BlitRect *box)
{
    static BlitGraphicsDrawList list;
    BlitSession *holder;
    int i, k;
    if (!frame_input_routes || !runtime->pointer_valid ||
        (holder = pointer_lock_session(runtime)) == NULL)
        return 0;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used || client->session_id != holder->id)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            BlitGraphicsImage *image;
            const BlitGraphicsDraw *d;
            const BlitRect *bounds;
            BlitRect surface;
            int count, display_index, x0, y0, x1, y1;
            if (in == NULL || !frame_input_live(runtime, client, in))
                continue;
            /* The live route is the only one: its hold decides. */
            image = graphics_image_find(holder, in->image_id);
            display_index = display_index_for_output(runtime, runtime->pointer_output);
            if (!in->confine || image == NULL || display_index < 0 ||
                !session_surface_px(runtime, holder, runtime->pointer_output, &surface))
                return 0;
            count = blit_graphics_collect(&image, 1, &list);
            if (count < 1)
                return 0;
            d = &list.draws[count - 1];
            bounds = &runtime->displays[display_index].bounds;
            /* The draw, in the surface's pixels, cut to the surface; then on
               the display, cut to it. */
            x0 = d->x > 0 ? d->x : 0;
            y0 = d->y > 0 ? d->y : 0;
            x1 = d->x + d->width < surface.w ? d->x + d->width : surface.w;
            y1 = d->y + d->height < surface.h ? d->y + d->height : surface.h;
            x0 += bounds->x + surface.x;
            x1 += bounds->x + surface.x;
            y0 += bounds->y + surface.y;
            y1 += bounds->y + surface.y;
            if (x0 < bounds->x) x0 = bounds->x;
            if (y0 < bounds->y) y0 = bounds->y;
            if (x1 > bounds->x + bounds->w) x1 = bounds->x + bounds->w;
            if (y1 > bounds->y + bounds->h) y1 = bounds->y + bounds->h;
            if (x1 <= x0 || y1 <= y0)
                return 0;
            if (box != NULL) {
                box->x = x0;
                box->y = y0;
                box->w = x1 - x0;
                box->h = y1 - y0;
            }
            return 1;
        }
    }
    return 0;
}

/* Under pointer lock: the raw sum the locked program is sent, as relative
   motion, to the live route only (one of the lock holder's). */
static void frame_input_relative(BlitRuntime *runtime, int dx, int dy)
{
    BlitSession *holder = pointer_lock_session(runtime);
    int i, k;
    if (holder == NULL || (dx == 0 && dy == 0))
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used || client->session_id != holder->id)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            if (in != NULL && frame_input_sync(runtime, client, in))
                frame_input_line(holder, in, "INPUT rel %u %d %d\n", in->image_id, dx, dy);
        }
    }
}

/* What GET /v1/input/keymap serves has changed: a new revision, told at
   once to every live route (and to the others when they go live). */
static void frame_input_keymap_changed(BlitRuntime *runtime)
{
    int i, k;
    ++runtime->keymap_revision;
    if (!frame_input_routes)
        return;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used)
            continue;
        for (k = 0; k < client->input_capacity; ++k)
            if (client->inputs[k] != NULL)
                frame_input_sync(runtime, client, client->inputs[k]);
    }
}

/* ---- The requests, on an authenticated channel. ---- */

/* `INPUT error <image id> <code>`; the id is 0 when the line named none or
   could not be read. */
static void frame_input_refuse(BlitRuntime *runtime, BlitFrameClient *client,
                               unsigned int image_id, const char *code)
{
    char line[64];
    frame_input_error(find_session_id(runtime, client->session_id), code);
    snprintf(line, sizeof(line), "INPUT error %u %s\n", image_id, code);
    frame_reply(client->fd, line);
}

/* ADR 0226: whether any channel of the session holds a route for the
   image; such a picture is never freed for room. */
static int frame_input_holds(const BlitSession *session, unsigned int image_id)
{
    int i, k;
    for (i = 0; i < graphics_memory.frame_client_count; ++i) {
        const BlitFrameClient *client = &graphics_memory.frame_clients[i];
        if (!client->used || client->session_id != session->id)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            const BlitFrameInput *in = client->inputs[k];
            int m;
            if (in == NULL)
                continue;
            if (in->image_id == image_id)
                return 1;
            for (m = 0; m < in->member_count; ++m)
                if (in->members[m].image_id == image_id)
                    return 1;
        }
    }
    return 0;
}

/* `INPUT ok <image id>`. */
static void frame_input_accept(BlitFrameClient *client, unsigned int image_id)
{
    char line[48];
    snprintf(line, sizeof(line), "INPUT ok %u\n", image_id);
    frame_reply(client->fd, line);
}

/* This channel's route for an image: its slot, or -1. */
static int frame_input_slot(const BlitFrameClient *client, unsigned int image_id)
{
    int k;
    for (k = 0; k < client->input_capacity; ++k)
        if (client->inputs[k] != NULL && client->inputs[k]->image_id == image_id)
            return k;
    return -1;
}

/* `INPUT sock <image-id> <path>`: the checks that need nothing come first,
   then the socket's owner, the connection and its peer; the connection is
   the route's. */
/* One route per compositor window socket in a session (ADR 0229): a route
   to the same socket file on any channel of the same session -- one left
   behind when the desktop asks again under a new picture id, as after a
   hide -- ends when a new one is opened: what it held released to the
   compositor first, its channel told `INPUT error <id> ENOENT`,
   `last_error` `superseded`. Other sessions' routes to that socket stay:
   a desktop gives each display's session its own route to a window, and
   only the session the pointer or the keyboard is in delivers, so a window
   over the seam between two displays takes input on both halves. (0.3.76
   ended them across sessions too; the last display asked then won.) The
   file's device and inode, not its name, say which socket it is. */
static int frame_input_supersede(BlitRuntime *runtime, uid_t uid, unsigned long long session_id,
                                 dev_t dev, ino_t ino)
{
    int i, k, ended = 0;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *other = &runtime->frame_clients[i];
        if (!other->used || !other->authenticated || other->uid != uid ||
            other->session_id != session_id)
            continue;
        for (k = 0; k < other->input_capacity; ++k) {
            BlitFrameInput *old = other->inputs[k];
            if (old == NULL || old->sock_dev != dev || old->sock_ino != ino)
                continue;
            frame_input_end(runtime, other, old);
            frame_input_disconnected(runtime, other, k, "superseded");
            ++ended;
        }
    }
    return ended;
}

static void frame_input_open(BlitRuntime *runtime, BlitFrameClient *client,
                             const char *path, unsigned int image_id)
{
    BlitSession *session = find_session_id(runtime, client->session_id);
    BlitGraphicsImage *image = session != NULL ? graphics_image_find(session, image_id) : NULL;
    struct sockaddr_un address;
    struct ucred peer;
    socklen_t peer_length = sizeof(peer);
    struct stat info;
    BlitFrameInput *in;
    int fd, slot, superseded;

    /* An image whose frame has not come yet is accepted, as a placement
       that arrives first is (ADR 0202); a deleted one, or a Kitty image
       that is not a frame client's, is not one this route can stand for. */
    if (session == NULL || image_id == 0 || (image != NULL && !image->descriptor) ||
        (image == NULL && frame_was_deleted(session, image_id))) {
        frame_input_refuse(runtime, client, image_id, "EINVAL");
        return;
    }
    if (frame_input_slot(client, image_id) >= 0) {
        frame_input_refuse(runtime, client, image_id, "EBUSY");
        return;
    }
    for (slot = 0; slot < client->input_capacity && client->inputs[slot] != NULL; ++slot) {}
    if (slot == client->input_capacity) {
        /* ADR 0226: the table grows, to as many routes as a session can
           have pictures. */
        int capacity = client->input_capacity > 0 ? client->input_capacity * 2 : 4;
        struct BlitFrameInput **grown;
        if (client->input_capacity >= BLIT_FRAME_INPUT_ROUTES) {
            frame_input_refuse(runtime, client, image_id, "EBUSY");
            return;
        }
        if (capacity > BLIT_FRAME_INPUT_ROUTES) capacity = BLIT_FRAME_INPUT_ROUTES;
        grown = (struct BlitFrameInput **)realloc(client->inputs,
                                                  (size_t)capacity * sizeof(*grown));
        if (grown == NULL) {
            frame_input_refuse(runtime, client, image_id, "ENOMEM");
            return;
        }
        memset(grown + client->input_capacity, 0,
               (size_t)(capacity - client->input_capacity) * sizeof(*grown));
        client->inputs = grown;
        client->input_capacity = capacity;
    }
    /* The file itself, not what a link names; a socket, the client's. */
    if (lstat(path, &info) != 0 || !S_ISSOCK(info.st_mode)) {
        frame_input_refuse(runtime, client, image_id, "ENOENT");
        return;
    }
    if (info.st_uid != client->uid) {
        frame_input_refuse(runtime, client, image_id, "EACCES");
        return;
    }
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    memcpy(address.sun_path, path, strlen(path));
    fd = socket(AF_UNIX, SOCK_STREAM | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);
    if (fd < 0 || connect(fd, (struct sockaddr *)&address, sizeof(address)) != 0) {
        if (fd >= 0) close(fd);
        frame_input_refuse(runtime, client, image_id, "ENOENT");
        return;
    }
    /* Who is listening, not only who owns the name: closes the window
       between the lstat and the connect. */
    if (getsockopt(fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) != 0 ||
        peer.uid != client->uid) {
        close(fd);
        frame_input_refuse(runtime, client, image_id, "EACCES");
        return;
    }
    in = (BlitFrameInput *)calloc(1, sizeof(*in));
    if (in == NULL) {
        close(fd);
        frame_input_refuse(runtime, client, image_id, "ENOMEM");
        return;
    }
    superseded = frame_input_supersede(runtime, client->uid, client->session_id, info.st_dev, info.st_ino);
    in->sock_dev = info.st_dev;
    in->sock_ino = info.st_ino;
    in->sock = fd;
    in->image_id = image_id;
    in->image_seen = image != NULL;
    client->inputs[slot] = in;
    ++frame_input_routes;
    /* A route it superseded is the last thing that went wrong: kept. */
    if (!superseded) frame_input_error(session, "");
    frame_input_accept(client, image_id);
}

/* `INPUT on <image-id>`: every other route of the session, on any of its
   channels, turned off first and what it held released, so two windows
   never both get the keys, even if an `off` was lost. */
static void frame_input_turn_on(BlitRuntime *runtime, BlitFrameClient *client, BlitFrameInput *route)
{
    int i, k;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *other = &runtime->frame_clients[i];
        if (!other->used || !other->authenticated || other->session_id != client->session_id)
            continue;
        for (k = 0; k < other->input_capacity; ++k)
            if (other->inputs[k] != NULL && other->inputs[k] != route && other->inputs[k]->on) {
                other->inputs[k]->on = 0;
                frame_input_sync(runtime, other, other->inputs[k]);
            }
    }
    route->on = 1;
    frame_input_sync(runtime, client, route);
}

/* An image id as the wire writes it: decimal, 1 to 2^32-1, no leading zero;
   0 when it is not one. */
static unsigned int frame_input_parse_id(const char *text)
{
    size_t length = strlen(text);
    unsigned long id;
    if (length < 1 || length > 10 || text[0] < '1' || text[0] > '9' ||
        strspn(text, "0123456789") != length)
        return 0;
    id = strtoul(text, NULL, 10);
    return id > 0xffffffffUL ? 0 : (unsigned int)id;
}

/* A signed pixel offset as the wire writes it: an optional '-', 1 to 5
   digits, |v| <= 32767; 0 with *ok cleared when it is not one. */
static int frame_input_parse_offset(const char *text, int *ok)
{
    const char *digits = text[0] == '-' ? text + 1 : text;
    size_t length = strlen(digits);
    long v;
    *ok = length >= 1 && length <= 5 && strspn(digits, "0123456789") == length;
    if (!*ok)
        return 0;
    v = strtol(text, NULL, 10);
    if (v < -32767 || v > 32767) {
        *ok = 0;
        return 0;
    }
    return (int)v;
}

/* `INPUT member <popup> <top-level> <x> <y>`: the popup's picture joins the
   top-level's route on this channel at that offset (again: moved there).
   EINVAL for a top-level with no route here, the popup the top-level
   itself, or a popup that is a deleted or Kitty image; EBUSY for a popup
   with a route of its own here, or a route with BLIT_FRAME_INPUT_MEMBERS
   already. A member of another route here moves to this one. */
static void frame_input_member(BlitRuntime *runtime, BlitFrameClient *client, unsigned int popup,
                               unsigned int toplevel, int x, int y)
{
    BlitSession *session = find_session_id(runtime, client->session_id);
    BlitGraphicsImage *image = session != NULL ? graphics_image_find(session, popup) : NULL;
    int slot = frame_input_slot(client, toplevel), k, m;
    BlitFrameInput *route;
    if (session == NULL || slot < 0 || popup == toplevel || (image != NULL && !image->descriptor) ||
        (image == NULL && frame_was_deleted(session, popup))) {
        frame_input_refuse(runtime, client, popup, "EINVAL");
        return;
    }
    if (frame_input_slot(client, popup) >= 0) {
        frame_input_refuse(runtime, client, popup, "EBUSY");
        return;
    }
    route = client->inputs[slot];
    for (m = 0; m < route->member_count && route->members[m].image_id != popup; ++m) {}
    if (m == route->member_count && m == BLIT_FRAME_INPUT_MEMBERS) {
        frame_input_refuse(runtime, client, popup, "EBUSY");
        return;
    }
    /* A member of another route here leaves it. */
    for (k = 0; k < client->input_capacity; ++k) {
        BlitFrameInput *other = client->inputs[k];
        int o;
        if (other == NULL || other == route)
            continue;
        for (o = 0; o < other->member_count; ++o)
            if (other->members[o].image_id == popup) {
                other->members[o] = other->members[--other->member_count];
                break;
            }
    }
    if (m == route->member_count) {
        ++route->member_count;
        route->members[m].seen = image != NULL;
    }
    route->members[m].image_id = popup;
    route->members[m].x = x;
    route->members[m].y = y;
    frame_input_accept(client, popup);
}

/* One INPUT message: a line, its newline optional. The image id is read
   from a line of a request's form; a line that has none, or is not one,
   is answered with 0. */
static void frame_input_request(BlitRuntime *runtime, BlitFrameClient *client,
                                const unsigned char *message, ssize_t length)
{
    char line[160], path[sizeof(((struct sockaddr_un *)0)->sun_path)];
    const char *id_text = NULL, *path_text = NULL;
    size_t path_length = 0;
    unsigned int id;
    int kind = 0;  /* 's' sock, 'n' on, 'f' off, 'h' hold, 'm' member */
    int confine = -1;  /* INPUT hold's mode: 0 relative, 1 confine */
    unsigned int toplevel = 0;  /* INPUT member's */
    int member_x = 0, member_y = 0, member_ok = 0;
    ssize_t i;

    if (length > 0 && message[length - 1] == '\n')
        --length;
    for (i = 0; i < length; ++i)
        if (message[i] < 0x20 || message[i] == 0x7f)
            break;
    if (i != length || length >= (ssize_t)sizeof(line)) {
        frame_input_refuse(runtime, client, 0, "EINVAL");
        return;
    }
    memcpy(line, message, (size_t)length);
    line[length] = '\0';
    if (strncmp(line, "INPUT on ", 9) == 0) {
        kind = 'n';
        id_text = line + 9;
    } else if (strncmp(line, "INPUT off ", 10) == 0) {
        kind = 'f';
        id_text = line + 10;
    } else if (strncmp(line, "INPUT hold ", 11) == 0) {
        /* The id, then the mode: the id is cut out so it reads alone. */
        char *mode = strchr(line + 11, ' ');
        kind = 'h';
        id_text = line + 11;
        if (mode != NULL) {
            *mode++ = '\0';
            confine = strcmp(mode, "confine") == 0 ? 1 : strcmp(mode, "relative") == 0 ? 0 : -1;
        }
    } else if (strncmp(line, "INPUT member ", 13) == 0) {
        /* Four words: each cut out so it reads alone. */
        char *words[4], *p = line + 13;
        int w;
        kind = 'm';
        for (w = 0; w < 4 && p != NULL; ++w) {
            words[w] = p;
            p = strchr(p, ' ');
            if (p != NULL) *p++ = '\0';
        }
        id_text = words[0];
        if (w == 4 && p == NULL) {
            int x_ok, y_ok;
            toplevel = frame_input_parse_id(words[1]);
            member_x = frame_input_parse_offset(words[2], &x_ok);
            member_y = frame_input_parse_offset(words[3], &y_ok);
            member_ok = toplevel != 0 && x_ok && y_ok;
        }
    } else if (strncmp(line, "INPUT sock ", 11) == 0) {
        /* The id, then the path, one word: the id is cut out so it reads
           alone; a path with a space in it is no path. */
        char *rest;
        kind = 's';
        id_text = line + 11;
        rest = strchr(id_text, ' ');
        if (rest != NULL) {
            *rest++ = '\0';
            path_text = rest;
            path_length = strcspn(path_text, " ");
            if (path_text[path_length] != '\0')
                path_length = 0;
        }
    }
    id = id_text != NULL ? frame_input_parse_id(id_text) : 0;
    if (id == 0) {
        frame_input_refuse(runtime, client, 0, "EINVAL");
        return;
    }
    if (kind == 'm') {
        if (!member_ok) {
            frame_input_refuse(runtime, client, id, "EINVAL");
            return;
        }
        frame_input_member(runtime, client, id, toplevel, member_x, member_y);
        return;
    }
    if (kind == 'n' || kind == 'f' || kind == 'h') {
        int slot = frame_input_slot(client, id);
        if (slot < 0 || (kind == 'h' && confine < 0)) {
            frame_input_refuse(runtime, client, id, "EINVAL");
            return;
        }
        if (kind == 'h') {
            /* Takes and gives back nothing: the hold is the desktop's
               (ADR 0198). A lock held now follows the new mode at once. */
            client->inputs[slot]->confine = confine;
            frame_input_accept(client, id);
        } else if (kind == 'n') {
            frame_input_turn_on(runtime, client, client->inputs[slot]);
        } else {
            client->inputs[slot]->on = 0;
            frame_input_sync(runtime, client, client->inputs[slot]);
        }
        return;
    }
    if (path_length == 0 || path_text[0] != '/' || path_length >= sizeof(path)) {
        frame_input_refuse(runtime, client, id, "EINVAL");
        return;
    }
    memcpy(path, path_text, path_length);
    path[path_length] = '\0';
    frame_input_open(runtime, client, path, id);
}

/* frames.input: the session's counts and every route of its channels, with
   its image, its state (off, on but not live, live), its hold (relative or
   confine), its members and its last pointer. Routes that do not fit are
   left out; the object stays whole. */
static size_t frame_input_json(BlitRuntime *runtime, const BlitSession *session,
                               char *out, size_t capacity)
{
    size_t used;
    int i, k, n, first = 1;
    n = snprintf(out, capacity, ",\"input\":{\"events\":%lu,\"dropped\":%lu,\"last_error\":\"%s\","
                 "\"routes\":[", session->frame_input_events, session->frame_input_dropped,
                 session->frame_input_error);
    if (n < 0 || (size_t)n + 3 > capacity)
        return 0;
    used = (size_t)n;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        const BlitFrameClient *client = &runtime->frame_clients[i];
        if (!client->used || client->session_id != session->id)
            continue;
        for (k = 0; k < client->input_capacity; ++k) {
            BlitFrameInput *in = client->inputs[k];
            char entry[400 + BLIT_FRAME_INPUT_MEMBERS * 48], members[BLIT_FRAME_INPUT_MEMBERS * 48 + 2];
            size_t mu = 0;
            int m;
            if (in == NULL)
                continue;
            members[0] = '\0';
            for (m = 0; m < in->member_count; ++m)
                mu += (size_t)snprintf(members + mu, sizeof(members) - mu,
                                       "%s{\"image\":%u,\"x\":%d,\"y\":%d}", m ? "," : "",
                                       in->members[m].image_id, in->members[m].x, in->members[m].y);
            /* `picture`: whether the route's picture is in this session -- not
               come yet, or drawn in another display's session, is `absent`:
               keys still go to a live route, the pointer cannot. */
            n = snprintf(entry, sizeof(entry), "%s{\"image\":%u,\"picture\":\"%s\",\"state\":\"%s\","
                         "\"hold\":\"%s\",\"members\":[%s],"
                         "\"pointer\":{\"surface\":[%d,%d],\"draw\":[%d,%d,%d,%d],"
                         "\"over\":%u,\"sent\":[%.1f,%.1f],\"miss\":\"%s\"}}",
                         first ? "" : ",", in->image_id,
                         session != NULL && graphics_image_find((BlitSession *)session, in->image_id) != NULL
                             ? "present" : "absent",
                         !in->on ? "off" : frame_input_live(runtime, client, in) ? "live" : "on",
                         in->confine ? "confine" : "relative", members,
                         in->last_surface[0], in->last_surface[1],
                         in->last_draw[0], in->last_draw[1], in->last_draw[2], in->last_draw[3],
                         in->last_over, in->last_sent[0], in->last_sent[1],
                         in->last_miss != NULL ? in->last_miss : "none-yet");
            if (n < 0 || (size_t)n >= sizeof(entry) || used + (size_t)n + 3 > capacity)
                continue;
            memcpy(out + used, entry, (size_t)n);
            used += (size_t)n;
            first = 0;
        }
    }
    memcpy(out + used, "]}", 3);
    return used + 2;
}
