/* ADR 0202: picture frames by descriptor. A program in a session sends the
   pixels of one of its images as a sealed memfd over a local socket instead
   of base64 through its PTY; placement, layering and deletion stay Kitty
   escapes on the PTY under the same image id.

   No name that arrives on any channel is opened (ADR 0066): the socket is
   Blit's own, abstract, and a frame is a descriptor the client already
   holds. A client proves which session it is in with that session's random
   BLIT_FRAME_TOKEN, and its uid (SO_PEERCRED) must be one that session's
   frames may come from. Included into blit_server.c after the image and
   damage helpers it uses.

   Version 2 (ADR 0203) adds two messages for the performance panel: Blit
   sends a version 2 client `HUD1 on` or `HUD1 off` when the panel's Hibr
   section starts or stops being shown for its session, and the client
   sends STA1, the lines to show there.

   Version 3 (ADR 0223) adds FRM3, a frame whose pixels are already in GPU
   memory: dmabuf planes (and optionally an acquire fence) instead of a
   memfd. The GPU compositor imports them as an EGLImage bound to the
   image's texture -- never mapped, read or copied by the CPU -- and keeps
   them until a newer frame of the image has been presented, then answers
   `FRAME release` so the producer may reuse the buffer. Offered in the
   hello's answer only while the GPU compositor can import.

   Version 4 (ADR 0228) is version 3 plus the WINDOW lines between
   blit-compositor and a desktop (docs/api.md, "Program windows").

   Version 5 (docs/api.md, "One grammar, version 5") is the only version:
   version 4 with every line that concerns a picture
   `<WORD> <verb> <id> [arguments]`, the picture id the third word --
   `FRAME release <id> <seq>`, `FRAME error <id> <code>`, `INPUT ok|error
   <id>`, `WINDOW ok|error <id>`. A hello of any other version is refused
   `ERROR 0 EINVAL`, the hello's own refusal. Blit reached directly answers
   ` window=0` and every WINDOW line `WINDOW error <id> ENOTSUP`. */

#define BLIT_FRAME_CLIENTS 32
#define BLIT_FRAME_HELLO_BYTES 16
/* The one version a hello may say. */
#define BLIT_FRAME_VERSION 5
#define BLIT_FRAME_HEADER_BYTES 48
#define BLIT_FRAME_V3_HEADER_BYTES 72
/* Descriptors one message may carry: a memfd, or up to four dmabuf planes
   and a fence. More are a malformed message. */
#define BLIT_FRAME_FDS 8
#define BLIT_FRAME_BATCH 64
/* STA1: magic, line count, then up to eight lines of up to 64 bytes and
   their newlines. Anything longer arrives truncated, and is refused. */
#define BLIT_FRAME_STATUS_BYTES (8 + BLIT_HUD_STATUS_LINES * (BLIT_HUD_STATUS_BYTES + 1))

/* ADR 0225: the INPUT requests and their routes, one per image (blit_frames_input.h). */
struct BlitFrameInput;
static void frame_input_close(BlitRuntime *runtime, BlitFrameClient *client);
static void frame_input_request(BlitRuntime *runtime, BlitFrameClient *client,
                                const unsigned char *message, ssize_t length);
static void frame_input_select(const BlitRuntime *runtime, fd_set *readable, int *max_fd);
static void frame_input_service(BlitRuntime *runtime, const fd_set *readable);
/* Blit's own chords in INPUT keep's grammar (blit_frames_input.h). */
static void frame_input_reserved(BlitRuntime *runtime, char *out, size_t capacity);

/* One line on the client's socket; nothing for an accepted frame. */
static void frame_reply(int fd, const char *line)
{
    if (send(fd, line, strlen(line), MSG_DONTWAIT | MSG_NOSIGNAL) < 0) {
        /* A client that stopped reading loses its errors, not Blit's loop. */
    }
}

/* ---- ADR 0231: the keyboards on the frame channel ----

   A desktop started by the login manager has no API token, so its keyboard
   switcher speaks here. A channel that sends `KEYBOARD list` is told the
   keyboards, `KEYBOARD item <n> <layout> <variant|-> <name...>` each and
   `KEYBOARD current <n>` last (the end of a list), at once and again on
   every change, whoever made it; `KEYBOARD current <n>` switches. A bad
   one is `KEYBOARD error EINVAL`. Opt-in: a channel that only draws is
   told nothing it did not ask for. */
static void frame_keyboards_send(BlitRuntime *runtime, int fd)
{
    char line[512], name[256], reserved[512];
    int i;
    for (i = 0; i < blit_keyboard_count; ++i) {
        blit_keyboard_description(blit_keyboards[i].layout, blit_keyboards[i].variant, name,
                                  sizeof(name));
        snprintf(line, sizeof(line), "KEYBOARD item %d %s %s %s\n", i, blit_keyboards[i].layout,
                 blit_keyboards[i].variant[0] ? blit_keyboards[i].variant : "-", name);
        frame_reply(fd, line);
    }
    /* Blit's own chords, which no shortcut can have: the console switch
       and the stop chord as configured. */
    frame_input_reserved(runtime, reserved, sizeof(reserved));
    snprintf(line, sizeof(line), "KEYBOARD reserved %s\n", reserved);
    frame_reply(fd, line);
    snprintf(line, sizeof(line), "KEYBOARD current %d\n", blit_keyboard_current);
    frame_reply(fd, line);
}

/* Every channel that asked, told the keyboards as they are now. */
static void frame_keyboards_changed(BlitRuntime *runtime)
{
    int i;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        if (client->used && client->authenticated && client->keyboards_wanted)
            frame_keyboards_send(runtime, client->fd);
    }
}

/* `KEYBOARD list` or `KEYBOARD current <n>` from a channel. */
static void frame_keyboards_request(BlitRuntime *runtime, BlitFrameClient *client,
                                    const unsigned char *message, size_t length)
{
    char line[64];
    int n;
    char extra;
    if (length >= sizeof(line)) {
        frame_reply(client->fd, "KEYBOARD error EINVAL\n");
        return;
    }
    memcpy(line, message, length);
    line[length] = '\0';
    if (length > 0 && line[length - 1] == '\n')
        line[length - 1] = '\0';
    if (strcmp(line, "KEYBOARD list") == 0) {
        client->keyboards_wanted = 1;
        frame_keyboards_send(runtime, client->fd);
    } else if (sscanf(line, "KEYBOARD current %d%c", &n, &extra) == 1 && n >= 0 &&
               n < blit_keyboard_count && strspn(line + 17, "0123456789") == strlen(line + 17)) {
        if (n != blit_keyboard_current) {
            blit_keyboards_use(n);
            keyboards_changed(runtime);
        } else if (client->keyboards_wanted) {
            frame_keyboards_send(runtime, client->fd);
        }
    } else {
        frame_reply(client->fd, "KEYBOARD error EINVAL\n");
    }
}

static void frame_client_close(BlitRuntime *runtime, int index)
{
    if (runtime->frame_clients[index].used) {
        /* The route ends with the channel: its keys and buttons released
           first (ADR 0225). */
        frame_input_close(runtime, &runtime->frame_clients[index]);
        if (runtime->frame_clients[index].authenticated) {
            BlitSession *session = find_session_id(runtime, runtime->frame_clients[index].session_id);
            if (session != NULL && session->frame_connections > 0)
                --session->frame_connections;
        }
        /* A dmabuf image keeps a duplicate of the socket for its
           `FRAME release` lines; shutting it down ends the connection for
           the peer too. */
        shutdown(runtime->frame_clients[index].fd, SHUT_RDWR);
        close(runtime->frame_clients[index].fd);
    }
    memset(&runtime->frame_clients[index], 0, sizeof(runtime->frame_clients[index]));
}

/* Equal strings, in time that does not depend on where they first differ. */
static int frame_token_equal(const char *a, const char *b)
{
    size_t la = strlen(a), lb = strlen(b), i;
    unsigned char diff = (unsigned char)(la != lb);
    for (i = 0; i < la && i < lb; ++i)
        diff |= (unsigned char)(a[i] ^ b[i]);
    return diff == 0;
}

/* The uids a session's frames may come from: the signed-in user of a
   login session, the account any other session runs as, and root. */
static int frame_peer_allowed(const BlitSession *session, uid_t uid)
{
    if (uid == 0)
        return 1;
    if (session->login_manager)
        return session->login_identity_state == 2 && uid == session->login_identity_uid;
    return uid == session->frame_uid;
}

static void frame_refuse(BlitRuntime *runtime, BlitSession *session, int fd,
                         unsigned int image_id, const char *code, const char *reason)
{
    char line[96];
    if (session != NULL) {
        ++session->frames_refused;
        snprintf(session->frames_last_error, sizeof(session->frames_last_error), "%s", reason);
    }
    ++runtime->frames_refused;
    snprintf(runtime->frames_last_error, sizeof(runtime->frames_last_error), "%s", reason);
    snprintf(line, sizeof(line), "FRAME error %u %s\n", image_id, code);
    frame_reply(fd, line);
}

static int frame_was_deleted(const BlitSession *session, unsigned int id)
{
    int i;
    for (i = 0; i < BLIT_FRAME_DELETED; ++i)
        if (session->frame_deleted[i] == id)
            return 1;
    return 0;
}

/* Called where Kitty frees an image's data: a descriptor image's id then
   answers later frames with ENOENT rather than coming back to life. */
static void frame_note_deleted(BlitSession *session, const BlitGraphicsImage *image)
{
    if (!image->used || !image->descriptor)
        return;
    session->frame_deleted[session->frame_deleted_next] = image->id;
    session->frame_deleted_next = (session->frame_deleted_next + 1) % BLIT_FRAME_DELETED;
}

typedef struct {
    int kind;  /* 1: FRM1, a memfd; 3: FRM3, dmabuf planes */
    unsigned int image_id;
    unsigned long long sequence;
    unsigned int width, height, format, stride, x, y, w, h;
    /* FRM3 only */
    uint32_t fourcc;
    uint64_t modifier;
    unsigned int planes;
    uint32_t offsets[BLIT_DMABUF_PLANES], pitches[BLIT_DMABUF_PLANES];
    /* FRM1: the memfd. FRM3: the planes' descriptors, then the fence if
       there is one. A descriptor taken over by an image is set to -1. */
    int fds[BLIT_FRAME_FDS];
    int fd_count;
} BlitFrame;

static void frame_close_fds(BlitFrame *f)
{
    int i;
    for (i = 0; i < f->fd_count; ++i)
        if (f->fds[i] >= 0) close(f->fds[i]);
    f->fd_count = 0;
}

static uint32_t frame_u32(const unsigned char *p)
{
    return (uint32_t)p[0] | (uint32_t)p[1] << 8 | (uint32_t)p[2] << 16 | (uint32_t)p[3] << 24;
}

static uint64_t frame_u64(const unsigned char *p)
{
    return (uint64_t)frame_u32(p) | (uint64_t)frame_u32(p + 4) << 32;
}

/* Read `rows` rows of `row_bytes` from the descriptor, `stride` apart, into
   `out` rows `out_stride` apart. */
static int frame_read_rows(int fd, unsigned char *out, size_t out_stride,
                           unsigned int rows, size_t row_bytes, size_t stride)
{
    unsigned int row;
    for (row = 0; row < rows; ++row) {
        size_t done = 0;
        while (done < row_bytes) {
            ssize_t got = pread(fd, out + row * out_stride + done, row_bytes - done,
                                (off_t)(row * stride + done));
            if (got <= 0)
                return 0;
            done += (size_t)got;
        }
    }
    return 1;
}

static BlitGraphicsImage *frame_image_for(BlitRuntime *runtime, BlitSession *session,
                                          int client_fd, const BlitFrame *f, int whole,
                                          int *stale);
static void frame_landed(BlitRuntime *runtime, BlitSession *session,
                         BlitGraphicsImage *image, int whole, const BlitFrame *f);

/* ---- ADR 0223: dmabuf frames (version 3). ---- */

#define FRAME_FOURCC(a, b, c, d) \
    ((uint32_t)(a) | (uint32_t)(b) << 8 | (uint32_t)(c) << 16 | (uint32_t)(d) << 24)

/* FRAME release <id> <sequence>: the producer may reuse that frame's buffer. */
static void frame_send_release(int fd, unsigned int image_id, unsigned long long sequence)
{
    char line[64];
    if (fd < 0)
        return;
    snprintf(line, sizeof(line), "FRAME release %u %llu\n", image_id, sequence);
    frame_reply(fd, line);
}

/* A dmabuf frame Blit is done with: its texture and EGLImage destroyed,
   its descriptors closed, and the producer told. */
static void frame_dmabuf_release(BlitGraphicsDmabuf *d, BlitGraphicsDmabufBuffer *b)
{
    int i;
    if (!b->used)
        return;
    if (b->texture != 0 || b->egl_image != NULL)
        blit_kms_dmabuf_destroy(d->kms, b->texture, b->egl_image);
    for (i = 0; i < b->fd_count; ++i)
        if (b->fds[i] >= 0) close(b->fds[i]);
    frame_send_release(d->reply_fd, d->image_id, b->sequence);
    if (d->session_released != NULL) ++*d->session_released;
    if (d->runtime_released != NULL) ++*d->runtime_released;
    memset(b, 0, sizeof(*b));
}

/* Everything an image holds of dmabufs, released (called where an image is
   freed, and when it goes back to pixels). The image then shows nothing
   until its next frame. */
static void frame_dmabuf_free(BlitGraphicsImage *image)
{
    BlitGraphicsDmabuf *d = image->dmabuf;
    if (d == NULL)
        return;
    frame_dmabuf_release(d, &d->retiring);
    frame_dmabuf_release(d, &d->shown);
    if (d->reply_fd >= 0)
        close(d->reply_fd);
    free(d);
    image->dmabuf = NULL;
}

/* After each present: a frame the GPU drew lets the one before it go. */
static void frame_dmabuf_presented(BlitRuntime *runtime)
{
    int i, k;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        if (!session->used)
            continue;
        for (k = 0; k < session->graphics_count; ++k) {
            BlitGraphicsDmabuf *d = session->graphics[k]->dmabuf;
            if (session->graphics[k]->used && d != NULL && d->retiring.used && d->shown.drawn)
                frame_dmabuf_release(d, &d->retiring);
        }
    }
}

/* Every dmabuf released: Blit's GLES context is going away, or no output
   is composed by the GPU any longer (the CPU compositor never reads a
   dmabuf). Images keep their ids and placements and show nothing until a
   frame comes by memfd; frames by dmabuf are refused ENOTSUP meanwhile. */
static void frame_dmabuf_drop_all(BlitRuntime *runtime)
{
    int i, k;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        BlitSurface *surface;
        int dropped = 0;
        if (!session->used)
            continue;
        for (k = 0; k < session->graphics_count; ++k)
            if (session->graphics[k]->used && session->graphics[k]->dmabuf != NULL) {
                frame_dmabuf_free(session->graphics[k]);
                dropped = 1;
            }
        surface = dropped ? find_surface_id(runtime, session->id) : NULL;
        if (surface != NULL) {
            mark_surface_damage(surface, 0, 0);
            runtime->render_pending = 1;
        }
    }
}

/* Once a turn: if the GPU can no longer import, what it imported goes. */
static void frame_dmabuf_reconcile(BlitRuntime *runtime)
{
    int i, k, live = 0;
    if (blit_kms_dmabuf_formats(runtime->kms, NULL, NULL) != NULL)
        return;
    for (i = 0; i < BLIT_MAX_SESSIONS && !live; ++i)
        for (k = 0; runtime->sessions[i].used && k < runtime->sessions[i].graphics_count; ++k)
            if (runtime->sessions[i].graphics[k]->used &&
                runtime->sessions[i].graphics[k]->dmabuf != NULL)
                live = 1;
    if (live)
        frame_dmabuf_drop_all(runtime);
}

/* A refused dmabuf frame, counted under its reason. */
static void frame_dmabuf_refuse(BlitRuntime *runtime, BlitSession *session, int fd,
                                const BlitFrame *f, const char *code, const char *reason,
                                unsigned long *counter)
{
    ++*counter;
    ++runtime->dmabuf_refused;
    frame_refuse(runtime, session, fd, f->image_id, code, reason);
}

/* FRM3. The checks that need no GPU come first, so a malformed frame is
   EINVAL wherever it is sent; then whether this compositor can import the
   format at all (ENOTSUP); then the planes' sizes against the header (the
   descriptors are seeked, never mapped); then the import itself. */
static void frame_dmabuf_apply(BlitRuntime *runtime, BlitSession *session,
                               BlitFrameClient *client, BlitFrame *f)
{
    BlitGraphicsImage *image;
    BlitGraphicsDmabuf *d;
    BlitGraphicsDmabufBuffer fresh;
    unsigned int i;
    int fence, stale, fresh_image;
    int alpha = f->fourcc == FRAME_FOURCC('A', 'R', '2', '4') ||
        f->fourcc == FRAME_FOURCC('A', 'B', '2', '4');
    unsigned int texture;
    void *egl_image = NULL;
    char error[160];

    if (f->image_id == 0 || f->width < 1 || f->height < 1 || f->planes < 1 ||
        f->planes > BLIT_DMABUF_PLANES ||
        ((unsigned int)f->fd_count != f->planes && (unsigned int)f->fd_count != f->planes + 1)) {
        frame_dmabuf_refuse(runtime, session, client->fd, f, "EINVAL", "dmabuf-header",
                            &session->dmabuf_refused_invalid);
        return;
    }
    for (i = f->planes; i < BLIT_DMABUF_PLANES; ++i)
        if (f->offsets[i] != 0 || f->pitches[i] != 0) {
            frame_dmabuf_refuse(runtime, session, client->fd, f, "EINVAL", "dmabuf-header",
                                &session->dmabuf_refused_invalid);
            return;
        }
    if (f->width > BLIT_GRAPHICS_MAX_WIDTH || f->height > BLIT_GRAPHICS_MAX_HEIGHT) {
        frame_dmabuf_refuse(runtime, session, client->fd, f, "EFBIG", "frame-too-large",
                            &session->dmabuf_refused_invalid);
        return;
    }
    if (blit_kms_dmabuf_formats(runtime->kms, NULL, NULL) == NULL) {
        frame_dmabuf_refuse(runtime, session, client->fd, f, "ENOTSUP", "dmabuf-unavailable",
                            &session->dmabuf_refused_unavailable);
        return;
    }
    if (!blit_kms_dmabuf_accepts(runtime->kms, f->fourcc, f->modifier)) {
        frame_dmabuf_refuse(runtime, session, client->fd, f, "ENOTSUP", "dmabuf-format",
                            &session->dmabuf_refused_format);
        return;
    }
    /* Each plane's buffer must reach as far as the header says: the first
       holds `height` rows of `pitch` bytes, at least four bytes a pixel;
       any other (a modifier's auxiliary plane) starts inside its buffer. */
    for (i = 0; i < f->planes; ++i) {
        off_t size = lseek(f->fds[i], 0, SEEK_END);
        unsigned long long need = i == 0 ?
            (unsigned long long)f->offsets[0] + (unsigned long long)f->pitches[0] * (f->height - 1) +
                (unsigned long long)f->width * 4 :
            (unsigned long long)f->offsets[i] + 1;
        if (size < 0 || f->pitches[i] == 0 || (i == 0 && f->pitches[0] < f->width * 4) ||
            (unsigned long long)size < need) {
            frame_dmabuf_refuse(runtime, session, client->fd, f, "EINVAL", "dmabuf-short",
                                &session->dmabuf_refused_invalid);
            return;
        }
    }
    image = frame_image_for(runtime, session, client->fd, f, 1, &stale);
    if (image == NULL) {
        if (stale) {
            /* Never used: the producer may have it back at once. */
            frame_send_release(client->fd, f->image_id, f->sequence);
            ++session->dmabuf_released;
            ++runtime->dmabuf_released;
        } else {
            ++session->dmabuf_refused_invalid;
            ++runtime->dmabuf_refused;
        }
        return;
    }
    fresh_image = !image->complete;
    fence = (unsigned int)f->fd_count == f->planes + 1 ? f->fds[f->planes] : -1;
    if (fence >= 0)
        f->fds[f->planes] = -1;  /* the import consumes it, whatever happens */
    texture = blit_kms_dmabuf_import(runtime->kms, (int)f->width, (int)f->height, f->fourcc,
                                     f->modifier, (int)f->planes, f->fds, f->offsets,
                                     f->pitches, fence, &egl_image, error, sizeof(error));
    if (texture == 0) {
        frame_dmabuf_refuse(runtime, session, client->fd, f, "ENOTSUP", "dmabuf-import",
                            &session->dmabuf_refused_import);
        if (fresh_image)
            graphics_image_release(image);
        return;
    }
    if (image->dmabuf == NULL) {
        image->dmabuf = (BlitGraphicsDmabuf *)calloc(1, sizeof(*image->dmabuf));
        if (image->dmabuf == NULL) {
            blit_kms_dmabuf_destroy(runtime->kms, texture, egl_image);
            frame_dmabuf_refuse(runtime, session, client->fd, f, "ENOMEM", "frame-allocation",
                                &session->dmabuf_refused_invalid);
            if (fresh_image)
                graphics_image_release(image);
            return;
        }
        image->dmabuf->kms = runtime->kms;
        image->dmabuf->reply_fd = -1;
        image->dmabuf->image_id = image->id;
        image->dmabuf->session_released = &session->dmabuf_released;
        image->dmabuf->runtime_released = &runtime->dmabuf_released;
    }
    d = image->dmabuf;
    /* FRAME release goes to the connection that sent the newest frame. */
    if (d->reply_fd < 0 || d->reply_serial != client->serial) {
        if (d->reply_fd >= 0)
            close(d->reply_fd);
        d->reply_fd = fcntl(client->fd, F_DUPFD_CLOEXEC, 0);
        d->reply_serial = client->serial;
    }
    /* The frame shown so far: kept until this one has been presented if it
       reached the screen, released now if it never did. */
    if (d->shown.used) {
        if (d->shown.drawn) {
            frame_dmabuf_release(d, &d->retiring);
            d->retiring = d->shown;
            memset(&d->shown, 0, sizeof(d->shown));
        } else {
            frame_dmabuf_release(d, &d->shown);
        }
    }
    memset(&fresh, 0, sizeof(fresh));
    fresh.used = 1;
    fresh.sequence = f->sequence;
    fresh.texture = texture;
    fresh.egl_image = egl_image;
    for (i = 0; i < f->planes; ++i) {
        fresh.fds[i] = f->fds[i];
        f->fds[i] = -1;  /* the image holds it now */
    }
    fresh.fd_count = (int)f->planes;
    d->shown = fresh;
    /* No CPU pixels: the picture is the texture, the producer's memory
       (ADR 0226 charges it nothing). */
    free(image->pixels);
    image->pixels = NULL;
    image->bytes = image->capacity = 0;
    graphics_image_charge(session, image);
    image->width = (int)f->width;
    image->height = (int)f->height;
    image->format = alpha ? 32 : 24;
    image->opaque = !alpha;
    image->complete = 1;
    image->descriptor_sequence = f->sequence;
    image->descriptor_accepted_us = monotonic_us();
    image->generation = next_graphics_generation();
    image->edit_count = 0;
    ++session->dmabuf_accepted;
    ++runtime->dmabuf_accepted;
    frame_landed(runtime, session, image, 1, f);
}

static void frame_apply(BlitRuntime *runtime, BlitSession *session, int client_fd,
                        const BlitFrame *f)
{
    BlitGraphicsImage *image;
    int whole = f->w == 0 && f->h == 0;
    unsigned int bpp = f->format == 24 ? 3U : 4U;
    unsigned int rows = whole ? f->height : f->h, columns = whole ? f->width : f->w;
    struct stat info;
    int seals, stale;
    unsigned long long from;

    if (f->image_id == 0 || (f->format != 24 && f->format != 32) || f->width < 1 ||
        f->height < 1) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-header");
        return;
    }
    if (f->width > BLIT_GRAPHICS_MAX_WIDTH || f->height > BLIT_GRAPHICS_MAX_HEIGHT ||
        (size_t)f->width * f->height * bpp > BLIT_GRAPHICS_MAX_BYTES) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EFBIG", "frame-too-large");
        return;
    }
    if (!whole && (f->w < 1 || f->h < 1 || f->x > f->width || f->w > f->width - f->x ||
                   f->y > f->height || f->h > f->height - f->y)) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-bounds");
        return;
    }
    /* The descriptor: a sealed memfd (F_GET_SEALS fails on anything else),
       long enough for its rows, at a stride that holds one. */
    seals = fcntl(f->fds[0], F_GET_SEALS);
    if (seals < 0 || (seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) !=
                         (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-not-sealed");
        return;
    }
    if (f->stride < columns * bpp || fstat(f->fds[0], &info) != 0 ||
        (unsigned long long)info.st_size < (unsigned long long)f->stride * (rows - 1) + columns * bpp) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-short");
        return;
    }
    image = frame_image_for(runtime, session, client_fd, f, whole, &stale);
    if (image == NULL)
        return;
    if (!whole && image->pixels == NULL) {
        /* A dmabuf frame has no CPU pixels to edit (ADR 0223). */
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-edit-dmabuf");
        return;
    }
    /* A memfd frame for an image that showed a dmabuf: the image goes back
       to pixels, and the dmabuf is released. */
    if (whole && image->dmabuf != NULL)
        frame_dmabuf_free(image);
    if (whole && (image->pixels == NULL || image->width != (int)f->width ||
                  image->height != (int)f->height || image->format != (int)f->format)) {
        size_t bytes = (size_t)f->width * f->height * bpp;
        unsigned char *pixels;
        /* ADR 0226: room within the budgets, what the image holds counted
           towards them. */
        if (!graphics_make_room(session, bytes > image->charged ? bytes - image->charged : 0,
                                image)) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOSPC", "frame-budget");
            if (!image->complete) graphics_image_release(image);
            return;
        }
        pixels = (unsigned char *)malloc(bytes);
        if (pixels == NULL) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOMEM", "frame-allocation");
            if (!image->complete) graphics_image_release(image);
            return;
        }
        free(image->pixels);
        image->pixels = pixels;
        image->capacity = image->bytes = bytes;
        graphics_image_charge(session, image);
        image->width = (int)f->width;
        image->height = (int)f->height;
        image->format = (int)f->format;
    }
    if (!whole && image->format != (int)f->format) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-format");
        return;
    }
    {
        size_t image_stride = (size_t)image->width * bpp;
        unsigned char *at = image->pixels + (whole ? 0 : (size_t)f->y * image_stride + (size_t)f->x * bpp);
        if (!frame_read_rows(f->fds[0], at, image_stride, rows, (size_t)columns * bpp, f->stride)) {
            frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-read");
            /* A new image whose first frame could not be read holds no
               picture, so it gives its slot back. */
            if (!image->complete) graphics_image_release(image);
            return;
        }
    }
    image->complete = 1;
    image->opaque = f->format == 24;
    image->descriptor_sequence = f->sequence;
    image->descriptor_accepted_us = monotonic_us();
    from = image->generation;
    image->generation = next_graphics_generation();
    /* An edit is remembered so the GPU uploads only it (ADR 0200); a whole
       frame refreshes the whole texture. */
    if (!whole && from != 0) {
        BlitGraphicsEdit edit;
        edit.from = from;
        edit.to = image->generation;
        edit.x = (int)f->x;
        edit.y = (int)f->y;
        edit.width = (int)f->w;
        edit.height = (int)f->h;
        if (image->edit_count == BLIT_GRAPHICS_EDIT_HISTORY) {
            memmove(image->edits, image->edits + 1,
                    sizeof(image->edits[0]) * (BLIT_GRAPHICS_EDIT_HISTORY - 1));
            --image->edit_count;
        }
        image->edits[image->edit_count++] = edit;
    } else {
        image->edit_count = 0;
    }
    frame_landed(runtime, session, image, whole, f);
}

/* The image for a frame: the descriptor image it names, or a new one for
   a whole frame. NULL when the frame is refused, or dropped as older than
   what the image holds (`*stale` set). */
static BlitGraphicsImage *frame_image_for(BlitRuntime *runtime, BlitSession *session,
                                          int client_fd, const BlitFrame *f, int whole,
                                          int *stale)
{
    BlitGraphicsImage *image = graphics_image_find(session, f->image_id);
    *stale = 0;
    if (image != NULL && !image->descriptor) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-kitty-image");
        return NULL;
    }
    if (image == NULL) {
        if (frame_was_deleted(session, f->image_id)) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOENT", "frame-deleted");
            return NULL;
        }
        if (!whole) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOENT", "frame-edit-unknown");
            return NULL;
        }
        image = graphics_image_new(session);
        if (image == NULL)
            image = graphics_reclaim_unshown(session);
        if (image == NULL) {
            ++session->graphics_refused_ceiling;
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOSPC", "frame-store-full");
            return NULL;
        }
        image->id = f->image_id;
        image->descriptor = 1;
        snprintf(image->protocol, sizeof(image->protocol), "kitty");
    } else if (f->sequence <= image->descriptor_sequence) {
        /* Older than what it holds: dropped, quietly, as newest wins. */
        ++session->frames_stale;
        *stale = 1;
        return NULL;
    }
    return image;
}

/* A frame has landed in `image`: placements that were waiting for it take
   effect, it is counted, and what shows it is damaged. */
static void frame_landed(BlitRuntime *runtime, BlitSession *session,
                         BlitGraphicsImage *image, int whole, const BlitFrame *f)
{
    BlitSurface *surface = find_surface_id(runtime, session->id);
    ++image->revision;
    /* Placements that arrived first (ADR 0202) take effect now. */
    {
        int k;
        for (k = 0; k < BLIT_FRAME_PENDING; ++k) {
            BlitPendingPlacement *p = &session->frame_pending[k];
            KittyPlacement placement;
            if (!p->used || p->image_id != image->id)
                continue;
            placement.id = p->id;
            placement.x = p->x; placement.y = p->y;
            placement.width = p->width; placement.height = p->height;
            placement.z = p->z;
            placement.src_x = p->src_x; placement.src_y = p->src_y;
            placement.src_w = p->src_w; placement.src_h = p->src_h;
            placement.scroll_anchored = p->scroll_anchored;
            if (!kitty_place(session, image, &placement)) {
                graphics_reject(session, "placement-store-full");
                kitty_reply(session, (int)p->image_id, p->quiet, "ENOSPC");
            } else {
                kitty_reply(session, (int)p->image_id, p->quiet, "OK");
            }
            memset(p, 0, sizeof(*p));
            whole = 1;  /* the new placement is damaged whole */
        }
    }
    ++session->frames_accepted;
    ++runtime->frames_accepted;
    if (surface != NULL) {
        int cw = session->terminal_cell_width > 0 ? session->terminal_cell_width : 8;
        int ch = session->terminal_cell_height > 0 ? session->terminal_cell_height : 16;
        if (whole)
            mark_graphics_image_damage(surface, image, cw, ch);
        else
            mark_graphics_rect_damage(surface, image, (int)f->x, (int)f->y,
                                      (int)f->w, (int)f->h, cw, ch);
    }
    runtime->render_pending = 1;
}

/* STA1 (version 2): "STA1", a little-endian line count of 1 to 8, then
   exactly that many lines, each at most 64 bytes of UTF-8 before its
   newline, filling the message. The count is a check: a message whose
   lines disagree with it is refused, rather than either being trusted.
   Kept, with its time, as the session's latest status. */
static int frame_status(BlitSession *session, const unsigned char *message, ssize_t length)
{
    char lines[BLIT_HUD_STATUS_LINES][BLIT_HUD_STATUS_BYTES + 1];
    uint32_t count;
    ssize_t at = 8;
    int found = 0, i;

    if (length < 8)
        return 0;
    count = frame_u32(message + 4);
    if (count < 1 || count > BLIT_HUD_STATUS_LINES)
        return 0;
    while (at < length) {
        ssize_t start = at;
        int more = 0;
        if (found == (int)count)
            return 0;
        for (; at < length && message[at] != '\n'; ++at) {
            unsigned char c = message[at];
            /* UTF-8 by its shape: a lead byte, then its continuations.
               No control characters. */
            if (more > 0) {
                if ((c & 0xc0) != 0x80) return 0;
                --more;
            } else if (c < 0x20 || c == 0x7f || (c >= 0x80 && c < 0xc2) || c > 0xf4) {
                return 0;
            } else if (c >= 0xc0) {
                more = c >= 0xf0 ? 3 : c >= 0xe0 ? 2 : 1;
            }
        }
        if (at == length || more > 0 || at - start > BLIT_HUD_STATUS_BYTES)
            return 0;
        memcpy(lines[found], message + start, (size_t)(at - start));
        lines[found][at - start] = '\0';
        ++found;
        ++at;
    }
    if (found != (int)count)
        return 0;
    for (i = 0; i < found; ++i)
        memcpy(session->status_lines[i], lines[i], sizeof(lines[i]));
    session->status_count = found;
    session->status_us = monotonic_us();
    return 1;
}

/* Whether the panel shows the Hibr section for this session's output. */
static int frame_hud_applies(BlitRuntime *runtime, const BlitSession *session)
{
    return session != NULL && runtime->hud_enabled &&
        (runtime->hud_sections & BLIT_HUD_SECTION_HIBR) &&
        hud_shows_output(runtime, session_output_index(runtime, session));
}

/* ADR 0203: tell each client when that changes, one line, HUD1
   on or HUD1 off. Called at the hello and once a turn (hud_reconcile), so
   a settings change or a session that moves displays is told too. */
static void frame_hud_notify(BlitRuntime *runtime)
{
    int i;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i) {
        BlitFrameClient *client = &runtime->frame_clients[i];
        int on;
        if (!client->used || !client->authenticated)
            continue;
        on = frame_hud_applies(runtime, find_session_id(runtime, client->session_id));
        if (on == client->hud_on)
            continue;
        client->hud_on = on;
        frame_reply(client->fd, on ? "HUD1 on\n" : "HUD1 off\n");
    }
}

/* ADR 0228: `WINDOW <event> <id> ...` is answered `WINDOW error <id>
   ENOTSUP`: Blit reached directly places no program windows, the desktop
   does. The id is the line's third word, 0 when it has none or it cannot
   be read (`WINDOW display <name>`), as for INPUT. */
static void frame_window_refuse(int fd, const unsigned char *message, ssize_t length)
{
    char line[96], reply[64];
    size_t n = length < (ssize_t)sizeof(line) ? (size_t)length : sizeof(line) - 1;
    unsigned long id = 0;
    const char *p;
    char *end;

    memcpy(line, message, n);
    line[n] = '\0';
    p = strchr(line + 7, ' ');
    if (p != NULL && p[1] >= '0' && p[1] <= '9') {
        errno = 0;
        id = strtoul(p + 1, &end, 10);
        if (errno != 0 || id > 0xffffffffUL || (*end != '\0' && *end != ' ' && *end != '\n'))
            id = 0;
    }
    snprintf(reply, sizeof(reply), "WINDOW error %lu ENOTSUP\n", id);
    frame_reply(fd, reply);
}

/* The hello: magic, version, token. On success the client is bound to the
   session the token names, if its uid may send that session frames. The
   hello's answers -- `OK ...`, `ERROR 0 EINVAL` (not a version 5 hello),
   `ERROR 0 EACCES` -- are not picture lines and keep their shape, so a
   client of any version can read its refusal. */
static void frame_hello(BlitRuntime *runtime, int index, const unsigned char *message,
                        ssize_t length)
{
    BlitFrameClient *client = &runtime->frame_clients[index];
    struct ucred peer;
    socklen_t peer_length = sizeof(peer);
    uint32_t token_length;
    char token[65];
    int i;

    if (length < BLIT_FRAME_HELLO_BYTES || memcmp(message, "BLITFRM1", 8) != 0 ||
        frame_u32(message + 8) != BLIT_FRAME_VERSION) {
        frame_reply(client->fd, "ERROR 0 EINVAL\n");
        frame_client_close(runtime, index);
        return;
    }
    token_length = frame_u32(message + 12);
    if (token_length < 1 || token_length > 64 ||
        (ssize_t)(BLIT_FRAME_HELLO_BYTES + token_length) != length ||
        getsockopt(client->fd, SOL_SOCKET, SO_PEERCRED, &peer, &peer_length) != 0) {
        frame_reply(client->fd, "ERROR 0 EACCES\n");
        frame_client_close(runtime, index);
        return;
    }
    memcpy(token, message + BLIT_FRAME_HELLO_BYTES, token_length);
    token[token_length] = '\0';
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        if (session->used && session->frame_token[0] != '\0' &&
            frame_token_equal(session->frame_token, token) &&
            frame_peer_allowed(session, peer.uid)) {
            client->session_id = session->id;
            client->authenticated = 1;
            client->uid = peer.uid;
            client->serial = ++runtime->frame_serial;
            ++session->frame_connections;
            {
                /* ADR 0223: dmabuf frames are offered only while the GPU
                   compositor can import them, with what it can import.
                   ADR 0228: the WINDOW lines are the desktop's to answer,
                   not Blit's: Blit says window=0 and refuses each one
                   (frame_window_refuse). */
                char answer[1300];
                int fences = 0;
                const char *formats = blit_kms_dmabuf_formats(runtime->kms, &fences, NULL);
                if (formats != NULL)
                    snprintf(answer, sizeof(answer), "OK dmabuf=1 fence=%d formats=%s window=0\n",
                             fences, formats);
                else
                    snprintf(answer, sizeof(answer), "OK dmabuf=0 window=0\n");
                frame_reply(client->fd, answer);
            }
            frame_hud_notify(runtime);
            return;
        }
    }
    ++runtime->frames_refused;
    frame_reply(client->fd, "ERROR 0 EACCES\n");
    frame_client_close(runtime, index);
}

/* Everything waiting on one client: the hello, or frames. Of several frames
   for one image only the newest is applied; the others' descriptors are
   closed unread, so a fast producer costs one copy per image per pass. */
static void frame_client_service(BlitRuntime *runtime, int index)
{
    BlitFrameClient *client = &runtime->frame_clients[index];
    BlitFrame batch[BLIT_FRAME_BATCH];
    int count = 0, i, j;
    BlitSession *session = NULL;

    for (;;) {
        /* A STA1 or a line: the longest line is an `INPUT keep` of the
           desktop's whole shortcut set (ADR 0231). */
        unsigned char message[BLIT_FRAME_STATUS_BYTES > 4096 ? BLIT_FRAME_STATUS_BYTES : 4096];
        union { struct cmsghdr header; char space[CMSG_SPACE(sizeof(int) * BLIT_FRAME_FDS)]; } control;
        struct iovec vector;
        struct msghdr header;
        struct cmsghdr *cmsg;
        int fds[BLIT_FRAME_FDS], fd_count = 0;
        ssize_t got;

        if (count == BLIT_FRAME_BATCH)
            break;
        memset(&header, 0, sizeof(header));
        vector.iov_base = message;
        vector.iov_len = sizeof(message);
        header.msg_iov = &vector;
        header.msg_iovlen = 1;
        header.msg_control = control.space;
        header.msg_controllen = sizeof(control.space);
        got = recvmsg(client->fd, &header, MSG_DONTWAIT | MSG_CMSG_CLOEXEC);
        if (got < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        for (cmsg = got >= 0 ? CMSG_FIRSTHDR(&header) : NULL; cmsg != NULL;
             cmsg = CMSG_NXTHDR(&header, cmsg))
            if (cmsg->cmsg_level == SOL_SOCKET && cmsg->cmsg_type == SCM_RIGHTS) {
                int n = (int)((cmsg->cmsg_len - CMSG_LEN(0)) / sizeof(int)), k;
                for (k = 0; k < n; ++k) {
                    int fd;
                    memcpy(&fd, CMSG_DATA(cmsg) + k * sizeof(int), sizeof(int));
                    if (fd_count < BLIT_FRAME_FDS) fds[fd_count++] = fd;
                    else close(fd);
                }
            }
        if (got <= 0) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            frame_client_close(runtime, index);
            break;
        }
        if (!client->authenticated) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            frame_hello(runtime, index, message, got);
            if (!runtime->frame_clients[index].used)
                return;
            continue;
        }
        session = find_session_id(runtime, client->session_id);
        /* Who may send is checked on every frame, not only at the hello: a
           login session's user changes, and a connection left over from
           one who signed out must not reach the next screen or user. */
        if (session == NULL || !frame_peer_allowed(session, client->uid)) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            if (session != NULL) {
                ++session->frames_refused;
                snprintf(session->frames_last_error, sizeof(session->frames_last_error),
                         "%s", "frame-peer-signed-out");
            }
            frame_reply(client->fd, "FRAME error 0 EACCES\n");
            frame_client_close(runtime, index);
            break;
        }
        if (got >= 4 && memcmp(message, "STA1", 4) == 0) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            if (fd_count != 0 || (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)) ||
                !frame_status(session, message, got))
                frame_reply(client->fd, "FRAME error 0 EINVAL\n");
            continue;
        }
        if (got >= 6 && memcmp(message, "INPUT ", 6) == 0) {
            /* ADR 0225: a line, no descriptor. */
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            if (fd_count != 0 || (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
                frame_input_request(runtime, client, (const unsigned char *)"", 0);
            else
                frame_input_request(runtime, client, message, got);
            continue;
        }
        if (got >= 9 && memcmp(message, "KEYBOARD ", 9) == 0) {
            /* ADR 0231: a line, no descriptor. */
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            if (fd_count != 0 || (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC)))
                frame_reply(client->fd, "KEYBOARD error EINVAL\n");
            else
                frame_keyboards_request(runtime, client, message, (size_t)got);
            continue;
        }
        if (got >= 7 && memcmp(message, "WINDOW ", 7) == 0) {
            /* ADR 0228: a program window's line, which only a desktop
               answers (docs/api.md, "Program windows"). */
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            frame_window_refuse(client->fd, message, got);
            continue;
        }
        if (got >= 4 && memcmp(message, "FRM3", 4) == 0) {
            /* ADR 0223: a dmabuf frame, 72 bytes, one descriptor per
               plane and optionally a fence after them. */
            if (got != BLIT_FRAME_V3_HEADER_BYTES ||
                fd_count < 1 || fd_count > BLIT_DMABUF_PLANES + 1 ||
                (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
                for (j = 0; j < fd_count; ++j) close(fds[j]);
                ++session->dmabuf_refused_invalid;
                ++runtime->dmabuf_refused;
                frame_refuse(runtime, session, client->fd,
                             got >= 8 ? frame_u32(message + 4) : 0, "EINVAL", "frame-message");
                continue;
            }
        } else if (got != BLIT_FRAME_HEADER_BYTES || memcmp(message, "FRM1", 4) != 0 ||
            fd_count != 1 || (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            frame_refuse(runtime, session, client->fd,
                         got >= 8 ? frame_u32(message + 4) : 0, "EINVAL", "frame-message");
            continue;
        }
        {
            BlitFrame frame;
            memset(&frame, 0, sizeof(frame));
            frame.image_id = frame_u32(message + 4);
            frame.sequence = frame_u64(message + 8);
            frame.width = frame_u32(message + 16);
            frame.height = frame_u32(message + 20);
            if (message[3] == '3') {
                frame.kind = 3;
                frame.fourcc = frame_u32(message + 24);
                frame.planes = frame_u32(message + 28);
                frame.modifier = frame_u64(message + 32);
                for (j = 0; j < BLIT_DMABUF_PLANES; ++j) {
                    frame.offsets[j] = frame_u32(message + 40 + 8 * j);
                    frame.pitches[j] = frame_u32(message + 44 + 8 * j);
                }
            } else {
                frame.kind = 1;
                frame.format = frame_u32(message + 24);
                frame.stride = frame_u32(message + 28);
                frame.x = frame_u32(message + 32);
                frame.y = frame_u32(message + 36);
                frame.w = frame_u32(message + 40);
                frame.h = frame_u32(message + 44);
            }
            for (j = 0; j < fd_count; ++j)
                frame.fds[j] = fds[j];
            frame.fd_count = fd_count;
            /* A newer frame for an image already waiting replaces it. A
               whole frame replaces anything; an edit replaces only an
               edit of the same rectangle, so no part of the picture is
               lost. */
            for (i = 0; i < count; ++i)
                if (batch[i].image_id == frame.image_id &&
                    ((frame.w == 0 && frame.h == 0) ||
                     (batch[i].x == frame.x && batch[i].y == frame.y &&
                      batch[i].w == frame.w && batch[i].h == frame.h)))
                    break;
            if (i < count) {
                /* A dmabuf frame never imported goes straight back. */
                if (batch[i].kind == 3) {
                    frame_send_release(client->fd, batch[i].image_id, batch[i].sequence);
                    ++session->dmabuf_released;
                    ++runtime->dmabuf_released;
                }
                frame_close_fds(&batch[i]);
                ++session->frames_superseded;
                batch[i] = frame;
            } else {
                batch[count++] = frame;
            }
        }
    }
    if (session == NULL && count > 0)
        session = find_session_id(runtime, client->session_id);
    for (i = 0; i < count; ++i) {
        if (session != NULL && runtime->frame_clients[index].used) {
            if (batch[i].kind == 3)
                frame_dmabuf_apply(runtime, session, &runtime->frame_clients[index], &batch[i]);
            else
                frame_apply(runtime, session, runtime->frame_clients[index].fd, &batch[i]);
        }
        frame_close_fds(&batch[i]);
    }
}

/* A held placement whose frame never came is refused as Kitty would. */
static void frame_expire_placements(BlitRuntime *runtime, uint64_t now)
{
    int i, k;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        if (!session->used)
            continue;
        for (k = 0; k < BLIT_FRAME_PENDING; ++k) {
            BlitPendingPlacement *p = &session->frame_pending[k];
            if (!p->used || now < p->deadline_us)
                continue;
            kitty_fail(session, (int)p->image_id, p->quiet, "placement-incomplete", "ENOENT");
            memset(p, 0, sizeof(*p));
        }
    }
}

static void frame_listen(BlitRuntime *runtime, int port)
{
    struct sockaddr_un address;
    socklen_t length;
    int fd = socket(AF_UNIX, SOCK_SEQPACKET | SOCK_NONBLOCK | SOCK_CLOEXEC, 0);

    runtime->frame_fd = -1;
    if (fd < 0)
        return;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(runtime->frame_name, sizeof(runtime->frame_name), "blit-frames-%d", port);
    memcpy(address.sun_path + 1, runtime->frame_name, strlen(runtime->frame_name));
    length = (socklen_t)(offsetof(struct sockaddr_un, sun_path) + 1 + strlen(runtime->frame_name));
    if (bind(fd, (struct sockaddr *)&address, length) != 0 || listen(fd, 16) != 0) {
        fprintf(stderr, "blit: frame socket @%s unavailable: %s\n", runtime->frame_name,
                strerror(errno));
        close(fd);
        runtime->frame_name[0] = '\0';
        return;
    }
    runtime->frame_fd = fd;
}

static void frame_select(const BlitRuntime *runtime, fd_set *readable, int *max_fd)
{
    int i;
    if (runtime->frame_fd < 0)
        return;
    FD_SET(runtime->frame_fd, readable);
    if (runtime->frame_fd > *max_fd) *max_fd = runtime->frame_fd;
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i)
        if (runtime->frame_clients[i].used) {
            FD_SET(runtime->frame_clients[i].fd, readable);
            if (runtime->frame_clients[i].fd > *max_fd) *max_fd = runtime->frame_clients[i].fd;
        }
    frame_input_select(runtime, readable, max_fd);
}

static void frame_service(BlitRuntime *runtime, const fd_set *readable)
{
    int i;
    if (runtime->frame_fd < 0)
        return;
    if (FD_ISSET(runtime->frame_fd, readable)) {
        for (;;) {
            int fd = accept4(runtime->frame_fd, NULL, NULL, SOCK_NONBLOCK | SOCK_CLOEXEC);
            if (fd < 0)
                break;
            for (i = 0; i < BLIT_FRAME_CLIENTS && runtime->frame_clients[i].used; ++i) {}
            if (i == BLIT_FRAME_CLIENTS) {
                close(fd);
                continue;
            }
            runtime->frame_clients[i].used = 1;
            runtime->frame_clients[i].fd = fd;
        }
    }
    for (i = 0; i < BLIT_FRAME_CLIENTS; ++i)
        if (runtime->frame_clients[i].used && FD_ISSET(runtime->frame_clients[i].fd, readable))
            frame_client_service(runtime, i);
    frame_input_service(runtime, readable);
}

#include "blit_frames_input.h"
