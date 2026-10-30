/* ADR 0202: picture frames by descriptor. A program in a session sends the
   pixels of one of its images as a sealed memfd over a local socket instead
   of base64 through its PTY; placement, layering and deletion stay Kitty
   escapes on the PTY under the same image id.

   No name that arrives on any channel is opened (ADR 0066): the socket is
   Blit's own, abstract, and a frame is a descriptor the client already
   holds. A client proves which session it is in with that session's random
   BLIT_FRAME_TOKEN, and its uid (SO_PEERCRED) must be one that session's
   frames may come from. Included into blit_server.c after the image and
   damage helpers it uses. */

#define BLIT_FRAME_CLIENTS 32
#define BLIT_FRAME_HELLO_BYTES 16
#define BLIT_FRAME_HEADER_BYTES 48
#define BLIT_FRAME_BATCH 64

/* One line on the client's socket; nothing for an accepted frame. */
static void frame_reply(int fd, const char *line)
{
    if (send(fd, line, strlen(line), MSG_DONTWAIT | MSG_NOSIGNAL) < 0) {
        /* A client that stopped reading loses its errors, not Blit's loop. */
    }
}

static void frame_client_close(BlitRuntime *runtime, int index)
{
    if (runtime->frame_clients[index].used) {
        if (runtime->frame_clients[index].authenticated) {
            BlitSession *session = find_session_id(runtime, runtime->frame_clients[index].session_id);
            if (session != NULL && session->frame_connections > 0)
                --session->frame_connections;
        }
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
    snprintf(line, sizeof(line), "ERROR %u %s\n", image_id, code);
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
    unsigned int image_id;
    unsigned long long sequence;
    unsigned int width, height, format, stride, x, y, w, h;
    int fd;
} BlitFrame;

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

static void frame_apply(BlitRuntime *runtime, BlitSession *session, int client_fd,
                        const BlitFrame *f)
{
    BlitGraphicsImage *image = graphics_image_find(session, f->image_id);
    BlitSurface *surface = find_surface_id(runtime, session->id);
    int whole = f->w == 0 && f->h == 0;
    unsigned int bpp = f->format == 24 ? 3U : 4U;
    unsigned int rows = whole ? f->height : f->h, columns = whole ? f->width : f->w;
    struct stat info;
    int seals;
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
    seals = fcntl(f->fd, F_GET_SEALS);
    if (seals < 0 || (seals & (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) !=
                         (F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE)) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-not-sealed");
        return;
    }
    if (f->stride < columns * bpp || fstat(f->fd, &info) != 0 ||
        (unsigned long long)info.st_size < (unsigned long long)f->stride * (rows - 1) + columns * bpp) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-short");
        return;
    }
    if (image != NULL && !image->descriptor) {
        frame_refuse(runtime, session, client_fd, f->image_id, "EINVAL", "frame-kitty-image");
        return;
    }
    if (image == NULL) {
        if (frame_was_deleted(session, f->image_id)) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOENT", "frame-deleted");
            return;
        }
        if (!whole) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOENT", "frame-edit-unknown");
            return;
        }
        {
            int index;
            for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES && image == NULL; ++index)
                if (!session->graphics[index].used)
                    image = &session->graphics[index];
        }
        if (image == NULL)
            image = graphics_reclaim_unshown(session);
        if (image == NULL) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOSPC", "frame-store-full");
            return;
        }
        memset(image, 0, sizeof(*image));
        image->used = 1;
        image->id = f->image_id;
        image->descriptor = 1;
        snprintf(image->protocol, sizeof(image->protocol), "kitty");
    } else if (f->sequence <= image->descriptor_sequence) {
        /* Older than what it holds: dropped, quietly, as newest wins. */
        ++session->frames_stale;
        return;
    }
    if (whole && (image->pixels == NULL || image->width != (int)f->width ||
                  image->height != (int)f->height || image->format != (int)f->format)) {
        size_t bytes = (size_t)f->width * f->height * bpp;
        unsigned char *pixels = (unsigned char *)malloc(bytes);
        if (pixels == NULL) {
            frame_refuse(runtime, session, client_fd, f->image_id, "ENOMEM", "frame-allocation");
            if (!image->complete) graphics_image_release(image);
            return;
        }
        free(image->pixels);
        image->pixels = pixels;
        image->capacity = image->bytes = bytes;
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
        if (!frame_read_rows(f->fd, at, image_stride, rows, (size_t)columns * bpp, f->stride)) {
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
            if (!kitty_place(image, &placement)) {
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

/* The hello: magic, version, token. On success the client is bound to the
   session the token names, if its uid may send that session frames. */
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
        frame_u32(message + 8) != 1) {
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
            ++session->frame_connections;
            frame_reply(client->fd, "OK\n");
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
        unsigned char message[BLIT_FRAME_HELLO_BYTES + 64];
        union { struct cmsghdr header; char space[CMSG_SPACE(sizeof(int) * 4)]; } control;
        struct iovec vector;
        struct msghdr header;
        struct cmsghdr *cmsg;
        int fds[4], fd_count = 0;
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
                    if (fd_count < 4) fds[fd_count++] = fd;
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
            frame_reply(client->fd, "ERROR 0 EACCES\n");
            frame_client_close(runtime, index);
            break;
        }
        if (got != BLIT_FRAME_HEADER_BYTES || memcmp(message, "FRM1", 4) != 0 ||
            fd_count != 1 || (header.msg_flags & (MSG_TRUNC | MSG_CTRUNC))) {
            for (j = 0; j < fd_count; ++j) close(fds[j]);
            frame_refuse(runtime, session, client->fd,
                         got >= 8 ? frame_u32(message + 4) : 0, "EINVAL", "frame-message");
            continue;
        }
        {
            BlitFrame frame;
            frame.image_id = frame_u32(message + 4);
            frame.sequence = frame_u64(message + 8);
            frame.width = frame_u32(message + 16);
            frame.height = frame_u32(message + 20);
            frame.format = frame_u32(message + 24);
            frame.stride = frame_u32(message + 28);
            frame.x = frame_u32(message + 32);
            frame.y = frame_u32(message + 36);
            frame.w = frame_u32(message + 40);
            frame.h = frame_u32(message + 44);
            frame.fd = fds[0];
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
                close(batch[i].fd);
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
        if (session != NULL && runtime->frame_clients[index].used)
            frame_apply(runtime, session, runtime->frame_clients[index].fd, &batch[i]);
        close(batch[i].fd);
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
}
