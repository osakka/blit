#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <unistd.h>
#include <pty.h>

#define BLIT_MAX_DISPLAYS 16
#define BLIT_MAX_SESSIONS 16
#define BLIT_MAX_STREAMS 16
#define BLIT_REQUEST_SIZE 8192
#define BLIT_MAX_CELL_COLS 160
#define BLIT_MAX_CELL_ROWS 100
#define BLIT_STREAM_DIAGNOSTICS 1
#define BLIT_STREAM_SURFACES 2
#define BLIT_STREAM_FRAMES 4
#define BLIT_STREAM_INPUT 8
#define BLIT_STREAM_HARDWARE 16
#define BLIT_STREAM_TERMINALS 32
#define BLIT_MAX_FRAME_SURFACES 16

typedef struct {
    unsigned long long id;
    char name[128];
    SDL_Rect bounds;
} BlitDisplay;

typedef struct {
    int used;
    unsigned long long id;
    unsigned long long revision;
    unsigned long long owner_session_id;
    int width;
    int height;
    char state[16];
    char cells[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    int damage_valid;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
} BlitSurface;

typedef struct {
    unsigned long long surface_id;
    unsigned long long surface_revision;
    int damage_valid;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
} BlitFrameSurface;

typedef struct {
    unsigned long long revision;
    int surface_count;
    BlitFrameSurface surfaces[BLIT_MAX_FRAME_SURFACES];
} BlitFramePlan;

typedef struct {
    int used;
    int fd;
    unsigned int subscriptions;
} BlitStream;

typedef struct {
    SDL_Window *window;
    SDL_Renderer *renderer;
    unsigned long long display_id;
    int initialized;
} BlitOutput;

typedef struct {
    int used;
    unsigned long long id;
    pid_t process_id;
    int pty_fd;
    unsigned long long revision;
    char display_id[128];
    char state[16];
    char surface_id[128];
} BlitSession;

typedef struct {
    int display_count;
    BlitDisplay displays[BLIT_MAX_DISPLAYS];
    int session_count;
    unsigned long long next_session_id;
    BlitSession sessions[BLIT_MAX_SESSIONS];
    BlitSurface surfaces[BLIT_MAX_SESSIONS];
    unsigned long long next_frame_id;
    unsigned long long frame_revision;
    BlitStream streams[BLIT_MAX_STREAMS];
    BlitOutput outputs[BLIT_MAX_DISPLAYS];
    int output_count;
    int sdl_ready;
    char sdl_error[256];
} BlitRuntime;

static volatile sig_atomic_t blit_running = 1;

static void render_outputs(BlitRuntime *runtime, const BlitFramePlan *plan);

static void stop_server(int signal_number)
{
    (void)signal_number;
    blit_running = 0;
}

static int send_all(int fd, const void *data, size_t length)
{
    const char *bytes = (const char *)data;
    size_t sent = 0;

    while (sent < length) {
        ssize_t result = send(fd, bytes + sent, length - sent, 0);

        if (result <= 0)
            return 0;
        sent += (size_t)result;
    }
    return 1;
}

static void send_http(int fd, int status, const char *reason,
                      const char *content_type, const char *body)
{
    char header[512];
    size_t body_length = strlen(body);
    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n\r\n",
        status, reason, content_type, (unsigned long)body_length);

    if (header_length > 0 && (size_t)header_length < sizeof(header)) {
        send_all(fd, header, (size_t)header_length);
        send_all(fd, body, body_length);
    }
}

static void json_escape(const char *input, char *output, size_t capacity)
{
    size_t in = 0;
    size_t out = 0;

    while (input[in] != '\0' && out + 2 < capacity) {
        unsigned char c = (unsigned char)input[in++];

        if (c == '"' || c == '\\') {
            output[out++] = '\\';
            output[out++] = (char)c;
        } else if (c == '\n' && out + 2 < capacity) {
            output[out++] = '\\';
            output[out++] = 'n';
        } else if (c >= 0x20) {
            output[out++] = (char)c;
        }
    }
    output[out] = '\0';
}

static void server_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    snprintf(body, capacity,
             "{\"id\":\"blit\",\"name\":\"blit\","
             "\"api_version\":\"0.1\",\"revision\":1,"
             "\"capabilities\":[\"http-control\",\"websocket-stream\","
             "\"terminal-sessions\",\"topology\","
             "\"native-presentation\",\"diagnostics\"],"
             "\"display_backend\":{\"ready\":%s,\"error\":\"%s\"}}",
             runtime->sdl_ready ? "true" : "false", runtime->sdl_error);
}

static void displays_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    size_t used = 0;
    int i;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"displays\":[");
    for (i = 0; i < runtime->display_count && used < capacity; ++i) {
        char escaped_name[256];
        json_escape(runtime->displays[i].name, escaped_name,
                    sizeof(escaped_name));
        used += (size_t)snprintf(
            body + used, capacity - used,
            "%s{\"id\":\"display-%llu\",\"name\":\"%s\","
            "\"bounds\":{\"x\":%d,\"y\":%d,\"width\":%d,"
            "\"height\":%d},\"modes\":[],"
            "\"presentation_state\":\"available\"}",
            i == 0 ? "" : ",", runtime->displays[i].id, escaped_name,
            runtime->displays[i].bounds.x, runtime->displays[i].bounds.y,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h);
    }
    snprintf(body + used, capacity - used, "]}");
}

static void topology_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    size_t used = 0;
    int i;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"id\":\"topology-0\",\"revision\":1,"
                             "\"outputs\":[");
    for (i = 0; i < runtime->display_count && used < capacity; ++i) {
        used += (size_t)snprintf(
            body + used, capacity - used,
            "%s{\"display_id\":\"display-%llu\",\"bounds\":{"
            "\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
            "\"rotation\":0,\"scale\":1}",
            i == 0 ? "" : ",", runtime->displays[i].id,
            runtime->displays[i].bounds.x, runtime->displays[i].bounds.y,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h);
    }
    snprintf(body + used, capacity - used, "]}");
}

static void empty_inventory_json(char *body, size_t capacity,
                                 const char *key)
{
    snprintf(body, capacity, "{\"revision\":1,\"%s\":[]}", key);
}

static const char *request_body(const char *request)
{
    const char *separator = strstr(request, "\r\n\r\n");
    return separator ? separator + 4 : "";
}

static ssize_t receive_request(int fd, char *request, size_t capacity)
{
    ssize_t received = 0;

    while ((size_t)received + 1 < capacity) {
        ssize_t count = recv(fd, request + received,
                             capacity - (size_t)received - 1, 0);
        const char *headers_end;
        size_t header_length;
        size_t content_length = 0;
        const char *length_header;

        if (count <= 0)
            return count;
        received += count;
        request[received] = '\0';
        headers_end = strstr(request, "\r\n\r\n");
        if (headers_end == NULL)
            continue;
        header_length = (size_t)(headers_end - request) + 4;
        length_header = strstr(request, "Content-Length:");
        if (length_header != NULL)
            content_length = (size_t)strtoul(length_header + 15, NULL, 10);
        if ((size_t)received >= header_length + content_length)
            return received;
    }
    return -1;
}

static int json_string(const char *json, const char *key, char *value,
                       size_t capacity)
{
    char needle[128];
    const char *cursor;
    size_t out = 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor != ':')
        return 0;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor != '"')
        return 0;
    ++cursor;
    while (*cursor != '\0' && *cursor != '"' && out + 1 < capacity) {
        if (*cursor == '\\' && cursor[1] != '\0') {
            ++cursor;
            if (*cursor == 'n') value[out++] = '\n';
            else if (*cursor == 'r') value[out++] = '\r';
            else if (*cursor == 't') value[out++] = '\t';
            else value[out++] = *cursor;
            ++cursor;
            continue;
        }
        value[out++] = *cursor++;
    }
    if (*cursor != '"')
        return 0;
    value[out] = '\0';
    return out > 0;
}

static int json_int(const char *json, const char *key, int *value)
{
    char needle[128];
    const char *cursor;
    char *end;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor != ':')
        return 0;
    ++cursor;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    *value = (int)strtol(cursor, &end, 10);
    return end != cursor;
}

static BlitSurface *find_surface_id(BlitRuntime *runtime,
                                    unsigned long long surface_id)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->surfaces[i].used && runtime->surfaces[i].id == surface_id)
            return &runtime->surfaces[i];
    }
    return NULL;
}

static BlitSurface *find_surface(BlitRuntime *runtime, const char *path)
{
    const char *prefix = "/v1/surfaces/";
    const char *id;
    unsigned long long surface_id;

    if (strncmp(path, prefix, strlen(prefix)) != 0)
        return NULL;
    id = path + strlen(prefix);
    if (sscanf(id, "surface-%llu/cells", &surface_id) != 1)
        return NULL;
    return find_surface_id(runtime, surface_id);
}

static int build_frame_plan(BlitRuntime *runtime, const char *json,
                            unsigned long long revision, BlitFramePlan *plan)
{
    const char *cursor = json;
    int found = 0;

    memset(plan, 0, sizeof(*plan));
    plan->revision = revision;

    while ((cursor = strstr(cursor, "\"surface-")) != NULL) {
        char id[64];
        const char *end = strchr(cursor + 1, '"');
        size_t length;
        unsigned long long surface_id;
        BlitSurface *surface;

        if (end == NULL)
            return 0;
        length = (size_t)(end - (cursor + 1));
        if (length >= sizeof(id))
            return 0;
        memcpy(id, cursor + 1, length);
        id[length] = '\0';
        if (sscanf(id, "surface-%llu", &surface_id) != 1)
            return 0;
        surface = find_surface_id(runtime, surface_id);
        if (surface == NULL || surface->revision != revision ||
            plan->surface_count >= BLIT_MAX_FRAME_SURFACES)
            return 0;
        plan->surfaces[plan->surface_count].surface_id = surface_id;
        plan->surfaces[plan->surface_count].surface_revision =
            surface->revision;
        plan->surfaces[plan->surface_count].damage_valid =
            surface->damage_valid;
        plan->surfaces[plan->surface_count].damage_x = surface->damage_x;
        plan->surfaces[plan->surface_count].damage_y = surface->damage_y;
        plan->surfaces[plan->surface_count].damage_width =
            surface->damage_width;
        plan->surfaces[plan->surface_count].damage_height =
            surface->damage_height;
        ++plan->surface_count;
        found = 1;
        cursor = end + 1;
    }
    return found;
}

static void mutation_receipt_json(const BlitSurface *surface, char *body,
                                  size_t capacity)
{
    snprintf(body, capacity, "{\"revision\":%llu,\"accepted\":true}",
             surface->revision);
}

static void mark_surface_damage(BlitSurface *surface, int x, int y)
{
    int right = x + 1;
    int bottom = y + 1;

    if (!surface->damage_valid) {
        surface->damage_valid = 1;
        surface->damage_x = x;
        surface->damage_y = y;
        surface->damage_width = 1;
        surface->damage_height = 1;
        return;
    }
    if (x < surface->damage_x)
        surface->damage_x = x;
    if (y < surface->damage_y)
        surface->damage_y = y;
    if (right > surface->damage_x + surface->damage_width)
        surface->damage_width = right - surface->damage_x;
    if (bottom > surface->damage_y + surface->damage_height)
        surface->damage_height = bottom - surface->damage_y;
}

static int apply_cell_update(BlitSurface *surface, const char *json)
{
    const char *cursor = strstr(json, "\"cells\"");
    int updated = 0;

    if (cursor == NULL)
        return 0;
    while ((cursor = strstr(cursor, "\"x\"")) != NULL) {
        char text[BLIT_MAX_CELL_COLS];
        int x, y;
        size_t i;

        if (!json_int(cursor, "x", &x) || !json_int(cursor, "y", &y) ||
            !json_string(cursor, "text", text, sizeof(text)))
            break;
        if (x >= 0 && y >= 0 && x < BLIT_MAX_CELL_COLS &&
            y < BLIT_MAX_CELL_ROWS) {
            for (i = 0; text[i] != '\0' && x + (int)i < BLIT_MAX_CELL_COLS;
                 ++i) {
                if (text[i] == '\n' || text[i] == '\r')
                    break;
                surface->cells[y][x + i] = text[i];
                mark_surface_damage(surface, x + (int)i, y);
            }
            if (surface->width > x) {
                mark_surface_damage(surface, x, y);
                if (surface->damage_width < surface->width - x)
                    surface->damage_width = surface->width - x;
            }
            updated += 1;
        }
        cursor += 3;
    }
    return updated;
}

static void frame_receipt_json(BlitRuntime *runtime, char *body,
                               size_t capacity)
{
    snprintf(body, capacity,
             "{\"frame_id\":\"frame-%llu\",\"revision\":%llu,"
             "\"state\":\"accepted\"}", runtime->next_frame_id - 1,
             runtime->frame_revision);
}

static BlitSession *find_session_id(BlitRuntime *runtime,
                                    unsigned long long session_id)
{
    int i;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used && runtime->sessions[i].id == session_id)
            return &runtime->sessions[i];
    }
    return NULL;
}

static int display_id_is_known(const BlitRuntime *runtime, const char *display_id)
{
    char expected[128];
    int i;

    if (display_id == NULL || display_id[0] == '\0')
        return 1;
    for (i = 0; i < runtime->display_count; ++i) {
        snprintf(expected, sizeof(expected), "display-%llu",
                 runtime->displays[i].id);
        if (strcmp(display_id, expected) == 0)
            return 1;
    }
    return 0;
}

static BlitSession *find_session(BlitRuntime *runtime, const char *path)
{
    const char *prefix = "/v1/terminal-sessions/";
    const char *id;
    unsigned long long session_id;

    if (strncmp(path, prefix, strlen(prefix)) != 0)
        return NULL;
    id = path + strlen(prefix);
    session_id = strtoull(id, NULL, 10);
    return find_session_id(runtime, session_id);
}

static void refresh_session_state(BlitSession *session)
{
    int status;
    pid_t result;

    if (!session->used || strcmp(session->state, "running") != 0)
        return;
    result = waitpid(session->process_id, &status, WNOHANG);
    if (result == session->process_id) {
        snprintf(session->state, sizeof(session->state), "stopped");
        if (session->pty_fd >= 0) {
            close(session->pty_fd);
            session->pty_fd = -1;
        }
    }
}

static void session_json(BlitSession *session, char *body, size_t capacity)
{
    size_t used;

    refresh_session_state(session);
    snprintf(body, capacity,
             "{\"id\":\"%llu\",\"revision\":%llu,"
             "\"state\":\"%s\",\"surface_id\":\"%s\","
             "\"process_id\":%ld",
             session->id, session->revision, session->state,
             session->surface_id, (long)session->process_id);
    used = strlen(body);
    if (session->display_id[0]) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"display_id\":\"%s\"",
                                 session->display_id);
    }
    snprintf(body + used, capacity - used, "}");
}

static void send_problem(int fd, int status, const char *reason,
                         const char *code, const char *detail)
{
    char body[1024];
    snprintf(body, sizeof(body),
             "{\"type\":\"about:blank\",\"title\":\"%s\","
             "\"status\":%d,\"code\":\"%s\",\"detail\":\"%s\"}",
             reason, status, code, detail);
    send_http(fd, status, reason, "application/problem+json", body);
}

static int create_session(BlitRuntime *runtime, const char *json,
                           BlitSession **created)
{
    struct winsize size;
    char command[256];
    char display_id[128];
    int width = 80;
    int height = 24;
    int master_fd;
    pid_t process_id;
    int i;

    if (!json_string(json, "command", command, sizeof(command)))
        return 0;
    json_int(json, "width", &width);
    json_int(json, "height", &height);
    if (width < 1 || height < 1 || width > BLIT_MAX_CELL_COLS ||
        height > BLIT_MAX_CELL_ROWS)
        return 0;
    memset(display_id, 0, sizeof(display_id));
    json_string(json, "display_id", display_id, sizeof(display_id));
    if (!display_id_is_known(runtime, display_id))
        return 0;
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)width;
    size.ws_row = (unsigned short)height;
    process_id = forkpty(&master_fd, NULL, NULL, &size);
    if (process_id < 0)
        return 0;
    if (process_id == 0) {
        execl(command, command, (char *)NULL);
        _exit(127);
    }
    fcntl(master_fd, F_SETFL, O_NONBLOCK);
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (!runtime->sessions[i].used) {
            BlitSession *session = &runtime->sessions[i];
            memset(session, 0, sizeof(*session));
            session->used = 1;
            session->id = runtime->next_session_id++;
            session->process_id = process_id;
            session->pty_fd = master_fd;
            session->revision = 1;
            snprintf(session->state, sizeof(session->state), "running");
            snprintf(session->surface_id, sizeof(session->surface_id),
                     "surface-%llu", session->id);
            snprintf(session->display_id, sizeof(session->display_id), "%s",
                     display_id);
            {
                BlitSurface *surface = &runtime->surfaces[i];
                memset(surface, 0, sizeof(*surface));
                surface->used = 1;
                surface->id = session->id;
                surface->revision = 1;
                surface->owner_session_id = session->id;
                surface->width = width;
                surface->height = height;
                snprintf(surface->state, sizeof(surface->state), "available");
            }
            ++runtime->session_count;
            *created = session;
            return 1;
        }
    }
    kill(process_id, SIGTERM);
    close(master_fd);
    return 0;
}

static void stop_sessions(BlitRuntime *runtime)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        if (!session->used)
            continue;
        refresh_session_state(session);
        if (strcmp(session->state, "running") == 0) {
            kill(-session->process_id, SIGTERM);
            waitpid(session->process_id, NULL, 0);
            snprintf(session->state, sizeof(session->state), "stopped");
        }
        if (session->pty_fd >= 0)
            close(session->pty_fd);
        session->pty_fd = -1;
    }
}

/* Minimal SHA-1 implementation for the WebSocket opening handshake. */
typedef struct {
    uint32_t h[5];
    uint64_t length;
    unsigned char block[64];
    size_t used;
} Sha1;

static uint32_t left_rotate(uint32_t value, unsigned int bits)
{
    return (value << bits) | (value >> (32 - bits));
}

static void sha1_block(Sha1 *sha, const unsigned char *block)
{
    uint32_t w[80];
    uint32_t a, b, c, d, e, f, k, temp;
    int i;

    for (i = 0; i < 16; ++i) {
        w[i] = ((uint32_t)block[i * 4] << 24) |
               ((uint32_t)block[i * 4 + 1] << 16) |
               ((uint32_t)block[i * 4 + 2] << 8) |
               (uint32_t)block[i * 4 + 3];
    }
    for (i = 16; i < 80; ++i)
        w[i] = left_rotate(w[i - 3] ^ w[i - 8] ^ w[i - 14] ^ w[i - 16], 1);

    a = sha->h[0]; b = sha->h[1]; c = sha->h[2];
    d = sha->h[3]; e = sha->h[4];
    for (i = 0; i < 80; ++i) {
        if (i < 20) {
            f = (b & c) | ((~b) & d);
            k = 0x5a827999;
        } else if (i < 40) {
            f = b ^ c ^ d;
            k = 0x6ed9eba1;
        } else if (i < 60) {
            f = (b & c) | (b & d) | (c & d);
            k = 0x8f1bbcdc;
        } else {
            f = b ^ c ^ d;
            k = 0xca62c1d6;
        }
        temp = left_rotate(a, 5) + f + e + k + w[i];
        e = d; d = c; c = left_rotate(b, 30); b = a; a = temp;
    }
    sha->h[0] += a; sha->h[1] += b; sha->h[2] += c;
    sha->h[3] += d; sha->h[4] += e;
}

static void sha1_init(Sha1 *sha)
{
    sha->h[0] = 0x67452301; sha->h[1] = 0xefcdab89;
    sha->h[2] = 0x98badcfe; sha->h[3] = 0x10325476;
    sha->h[4] = 0xc3d2e1f0; sha->length = 0; sha->used = 0;
}

static void sha1_update(Sha1 *sha, const unsigned char *data, size_t length)
{
    while (length > 0) {
        size_t copy = 64 - sha->used;
        if (copy > length) copy = length;
        memcpy(sha->block + sha->used, data, copy);
        sha->used += copy; data += copy; length -= copy;
        sha->length += (uint64_t)copy * 8;
        if (sha->used == 64) {
            sha1_block(sha, sha->block);
            sha->used = 0;
        }
    }
}

static void sha1_final(Sha1 *sha, unsigned char digest[20])
{
    uint64_t bit_length = sha->length;
    int i;
    unsigned char pad = 0x80;
    unsigned char zero = 0;

    sha1_update(sha, &pad, 1);
    while (sha->used != 56)
        sha1_update(sha, &zero, 1);
    for (i = 7; i >= 0; --i) {
        unsigned char byte = (unsigned char)(bit_length >> (i * 8));
        sha1_update(sha, &byte, 1);
    }
    for (i = 0; i < 5; ++i) {
        digest[i * 4] = (unsigned char)(sha->h[i] >> 24);
        digest[i * 4 + 1] = (unsigned char)(sha->h[i] >> 16);
        digest[i * 4 + 2] = (unsigned char)(sha->h[i] >> 8);
        digest[i * 4 + 3] = (unsigned char)sha->h[i];
    }
}

static void base64_encode(const unsigned char *input, size_t length,
                          char *output, size_t capacity)
{
    static const char alphabet[] =
        "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/";
    size_t i = 0, out = 0;

    while (i < length && out + 4 < capacity) {
        unsigned int a = input[i++];
        unsigned int b = i < length ? input[i++] : 0;
        unsigned int c = i < length ? input[i++] : 0;
        output[out++] = alphabet[(a >> 2) & 63];
        output[out++] = alphabet[((a << 4) | (b >> 4)) & 63];
        output[out++] = i - 1 < length ? alphabet[((b << 2) | (c >> 6)) & 63] : '=';
        output[out++] = i < length ? alphabet[c & 63] : '=';
    }
    output[out] = '\0';
}

static int websocket_accept(const char *request, char *accept, size_t capacity)
{
    const char *prefix = "Sec-WebSocket-Key:";
    const char *key = strstr(request, prefix);
    char combined[256];
    unsigned char digest[20];
    Sha1 sha;
    size_t length = 0;

    if (key == NULL)
        return 0;
    key += strlen(prefix);
    while (*key == ' ' || *key == '\t') ++key;
    while (key[length] != '\0' && key[length] != '\r' &&
           key[length] != '\n' && length + 1 < sizeof(combined))
        ++length;
    if (length == 0 || length + 37 >= sizeof(combined))
        return 0;
    memcpy(combined, key, length);
    memcpy(combined + length, "258EAFA5-E914-47DA-95CA-C5AB0DC85B11", 36);
    length += 36;
    sha1_init(&sha);
    sha1_update(&sha, (const unsigned char *)combined, length);
    sha1_final(&sha, digest);
    base64_encode(digest, sizeof(digest), accept, capacity);
    return 1;
}

static int send_websocket_text(int fd, const char *text)
{
    unsigned char header[4];
    size_t length = strlen(text);

    if (length > 65535)
        return 0;
    header[0] = 0x81;
    if (length <= 125) {
        header[1] = (unsigned char)length;
        send_all(fd, header, 2);
    } else {
        header[1] = 126;
        header[2] = (unsigned char)(length >> 8);
        header[3] = (unsigned char)length;
        send_all(fd, header, 4);
    }
    return send_all(fd, text, length);
}

static int stream_contains(const BlitRuntime *runtime, int fd)
{
    int i;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used && runtime->streams[i].fd == fd)
            return 1;
    return 0;
}

static int add_stream(BlitRuntime *runtime, int fd)
{
    int i;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
        if (!runtime->streams[i].used) {
            runtime->streams[i].used = 1;
            runtime->streams[i].fd = fd;
            runtime->streams[i].subscriptions = BLIT_STREAM_DIAGNOSTICS |
                BLIT_STREAM_SURFACES | BLIT_STREAM_FRAMES |
                BLIT_STREAM_INPUT | BLIT_STREAM_HARDWARE |
                BLIT_STREAM_TERMINALS;
            return 1;
        }
    }
    return 0;
}

static void remove_stream(BlitRuntime *runtime, int fd)
{
    int i;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
        if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
            close(fd);
            runtime->streams[i].used = 0;
            return;
        }
    }
}

static void broadcast_stream(BlitRuntime *runtime, const char *message,
                             unsigned int subscription)
{
    int i;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
        if (runtime->streams[i].used &&
            (runtime->streams[i].subscriptions & subscription) &&
            !send_websocket_text(runtime->streams[i].fd, message))
            remove_stream(runtime, runtime->streams[i].fd);
    }
}

static void service_session_output(BlitRuntime *runtime, int fd)
{
    unsigned char output[768];
    char escaped[1536];
    char event[1800];
    ssize_t received;
    int i;

    received = read(fd, output, sizeof(output) - 1);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return;
    if (received <= 0) {
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime->sessions[i].used && runtime->sessions[i].pty_fd == fd) {
                snprintf(runtime->sessions[i].state,
                         sizeof(runtime->sessions[i].state), "stopped");
                close(fd);
                runtime->sessions[i].pty_fd = -1;
                return;
            }
        }
        return;
    }
    output[received] = '\0';
    json_escape((char *)output, escaped, sizeof(escaped));
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used && runtime->sessions[i].pty_fd == fd) {
            snprintf(event, sizeof(event),
                     "{\"type\":\"terminal_output\","
                     "\"session_id\":\"%llu\",\"data\":\"%s\"}",
                     runtime->sessions[i].id, escaped);
            broadcast_stream(runtime, event, BLIT_STREAM_TERMINALS);
            return;
        }
    }
}

static void send_websocket_control(int fd, unsigned char opcode,
                                   const unsigned char *payload, size_t length)
{
    unsigned char header[2];

    if (length > 125)
        return;
    header[0] = (unsigned char)(0x80 | opcode);
    header[1] = (unsigned char)length;
    send_all(fd, header, sizeof(header));
    send_all(fd, payload, length);
}

static unsigned int subscription_mask(const char *payload)
{
    unsigned int mask = 0;

    if (strstr(payload, "diagnostics") != NULL) mask |= BLIT_STREAM_DIAGNOSTICS;
    if (strstr(payload, "surfaces") != NULL) mask |= BLIT_STREAM_SURFACES;
    if (strstr(payload, "frames") != NULL) mask |= BLIT_STREAM_FRAMES;
    if (strstr(payload, "input") != NULL) mask |= BLIT_STREAM_INPUT;
    if (strstr(payload, "hardware") != NULL) mask |= BLIT_STREAM_HARDWARE;
    if (strstr(payload, "terminals") != NULL) mask |= BLIT_STREAM_TERMINALS;
    return mask;
}

static void service_stream(BlitRuntime *runtime, int fd)
{
    unsigned char frame[1025];
    ssize_t received = recv(fd, frame, sizeof(frame) - 1, 0);
    size_t offset;
    size_t length;
    unsigned char opcode;
    int masked;
    unsigned char mask[4];
    size_t i;

    if (received <= 0) {
        remove_stream(runtime, fd);
        return;
    }
    if (received < 2)
        return;
    opcode = (unsigned char)(frame[0] & 0x0f);
    masked = (frame[1] & 0x80) != 0;
    length = frame[1] & 0x7f;
    offset = 2;
    if (length == 126 || length == 127 || length + offset > (size_t)received)
        return;
    if (masked) {
        if (length + offset + 4 > (size_t)received)
            return;
        memcpy(mask, frame + offset, sizeof(mask));
        offset += sizeof(mask);
        for (i = 0; i < length; ++i)
            frame[offset + i] ^= mask[i % 4];
    }
    if (opcode == 0x8) {
        send_websocket_control(fd, 0x8, NULL, 0);
        remove_stream(runtime, fd);
        return;
    }
    if (opcode == 0x9) {
        send_websocket_control(fd, 0xA, frame + offset, length);
        return;
    }
    if (opcode == 0x1) {
        frame[offset + length] = '\0';
        if (strstr((char *)frame + offset, "subscribe") != NULL) {
            int i;
            for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
                if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
                    runtime->streams[i].subscriptions =
                        subscription_mask((char *)frame + offset);
                    break;
                }
            }
        } else if (strstr((char *)frame + offset, "\"input\"") != NULL &&
                   strstr((char *)frame + offset, "session_id") != NULL) {
            char session_id[64];
            char data[768];
            unsigned long long numeric_id;
            BlitSession *session;

            if (json_string((char *)frame + offset, "session_id",
                            session_id, sizeof(session_id)) &&
                json_string((char *)frame + offset, "data",
                            data, sizeof(data)) &&
                sscanf(session_id, "%llu", &numeric_id) == 1) {
                session = find_session_id(runtime, numeric_id);
                if (session != NULL && session->pty_fd >= 0) {
                    write(session->pty_fd, data, strlen(data));
                    send_websocket_text(fd,
                        "{\"type\":\"diagnostic\",\"severity\":\"info\","
                        "\"code\":\"input_forwarded\","
                        "\"detail\":\"input forwarded to terminal session\"}");
                }
            }
        }
        if (strstr((char *)frame + offset, "subscribe") != NULL)
            send_websocket_text(fd,
                "{\"type\":\"diagnostic\",\"severity\":\"info\","
                "\"code\":\"subscription_received\","
                "\"detail\":\"stream subscription accepted\"}");
    }
}

static void handle_request(int fd, BlitRuntime *runtime,
                           const char *request)
{
    char method[16];
    char path[256];
    char body[16384];
    const char *payload = request_body(request);

    if (sscanf(request, "%15s %255s", method, path) != 2) {
        send_http(fd, 400, "Bad Request", "application/problem+json",
                  "{\"type\":\"about:blank\",\"title\":\"Bad Request\","
                  "\"status\":400,\"code\":\"malformed_request\","
                  "\"detail\":\"The request line is invalid.\"}");
        return;
    }

    if (strcmp(path, "/v1/stream") == 0 && strcmp(method, "GET") == 0) {
        char accept[64];
        char response[256];
        if (!websocket_accept(request, accept, sizeof(accept))) {
            send_http(fd, 400, "Bad Request", "application/problem+json",
                      "{\"type\":\"about:blank\",\"title\":\"Bad Request\","
                      "\"status\":400,\"code\":\"websocket_key_missing\","
                      "\"detail\":\"Sec-WebSocket-Key is required.\"}");
            return;
        }
        if (!add_stream(runtime, fd)) {
            send_problem(fd, 503, "Service Unavailable", "stream_capacity",
                         "The stream connection limit has been reached.");
            return;
        }
        snprintf(response, sizeof(response),
                 "HTTP/1.1 101 Switching Protocols\r\n"
                 "Upgrade: websocket\r\nConnection: Upgrade\r\n"
                 "Sec-WebSocket-Accept: %s\r\n\r\n", accept);
        send_all(fd, response, strlen(response));
        send_websocket_text(fd,
            "{\"type\":\"diagnostic\",\"severity\":\"info\","
            "\"code\":\"stream_connected\",\"detail\":\"blit stream ready\"}");
        return;
    }

    if (strcmp(path, "/v1/terminal-sessions") == 0 &&
        strcmp(method, "POST") == 0) {
        BlitSession *session;
        char requested_display_id[128];
        if (strstr(payload, "\"arguments\"") != NULL ||
            strstr(payload, "\"environment\"") != NULL) {
            send_problem(fd, 501, "Not Implemented", "session_options_pending",
                         "Arguments and environment policy are not implemented yet.");
            return;
        }
        if (json_string(payload, "display_id", requested_display_id,
                        sizeof(requested_display_id)) &&
            !display_id_is_known(runtime, requested_display_id)) {
            send_problem(fd, 404, "Not Found", "display_not_found",
                         "The requested display does not exist.");
            return;
        }
        if (!create_session(runtime, payload, &session)) {
            send_problem(fd, 400, "Bad Request", "session_create_failed",
                         "The command, surface dimensions, or PTY could not be created.");
            return;
        }
        session_json(session, body, sizeof(body));
        send_http(fd, 201, "Created", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/surfaces/", strlen("/v1/surfaces/")) == 0 &&
        strcmp(method, "PUT") == 0) {
        BlitSurface *surface = find_surface(runtime, path);
        int base_revision;

        if (surface == NULL) {
            send_problem(fd, 404, "Not Found", "surface_not_found",
                         "The text surface does not exist.");
            return;
        }
        if (!json_int(payload, "base_revision", &base_revision) ||
            !strstr(payload, "\"cells\"")) {
            send_problem(fd, 400, "Bad Request", "invalid_cell_update",
                         "A base revision and cells array are required.");
            return;
        }
        if ((unsigned long long)base_revision != surface->revision) {
            send_problem(fd, 409, "Conflict", "revision_conflict",
                         "The surface revision does not match the requested update.");
            return;
        }
        if (!apply_cell_update(surface, payload)) {
            send_problem(fd, 400, "Bad Request", "invalid_cell_update",
                         "The cells array contains no usable text updates.");
            return;
        }
        ++surface->revision;
        mutation_receipt_json(surface, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        {
            char event[256];
            snprintf(event, sizeof(event),
                     "{\"type\":\"surface_delta\",\"surface_id\":"
                     "\"surface-%llu\",\"revision\":%llu}",
                     surface->id, surface->revision);
            broadcast_stream(runtime, event, BLIT_STREAM_SURFACES);
        }
        return;
    }
    if (strcmp(path, "/v1/frames") == 0 && strcmp(method, "POST") == 0) {
        int base_revision;
        BlitFramePlan plan;

        if (!json_int(payload, "base_revision", &base_revision) ||
            !build_frame_plan(runtime, payload,
                              (unsigned long long)base_revision, &plan)) {
            send_problem(fd, 400, "Bad Request", "invalid_frame_request",
                         "A base revision and existing surface ids are required.");
            return;
        }
        if ((unsigned long long)base_revision == 0) {
            send_problem(fd, 409, "Conflict", "revision_conflict",
                         "The frame base revision must identify current surface state.");
            return;
        }
        ++runtime->frame_revision;
        ++runtime->next_frame_id;
        frame_receipt_json(runtime, body, sizeof(body));
        render_outputs(runtime, &plan);
        send_http(fd, 202, "Accepted", "application/json", body);
        {
            char event[256];
            snprintf(event, sizeof(event),
                     "{\"type\":\"frame\",\"frame\":%s}", body);
            broadcast_stream(runtime, event, BLIT_STREAM_FRAMES);
        }
        return;
    }
    if (strncmp(path, "/v1/terminal-sessions/",
                strlen("/v1/terminal-sessions/")) == 0 &&
        (strcmp(method, "GET") == 0 || strcmp(method, "PATCH") == 0)) {
        BlitSession *session = find_session(runtime, path);
        if (session == NULL) {
            send_problem(fd, 404, "Not Found", "session_not_found",
                         "The terminal session does not exist.");
            return;
        }
        if (strcmp(method, "PATCH") == 0) {
            char display_id[128];
            if (strstr(payload, "\"input_route\"") != NULL ||
                strstr(payload, "\"focused\"") != NULL) {
                send_problem(fd, 501, "Not Implemented", "input_policy_pending",
                             "Input routing and focus policy are not implemented yet.");
                return;
            }
            if (json_string(payload, "display_id", display_id,
                            sizeof(display_id))) {
                if (!display_id_is_known(runtime, display_id)) {
                    send_problem(fd, 404, "Not Found", "display_not_found",
                                 "The requested display does not exist.");
                    return;
                }
                snprintf(session->display_id, sizeof(session->display_id),
                         "%s", display_id);
                {
                    int output_index;
                    for (output_index = 0;
                         output_index < runtime->output_count; ++output_index)
                        runtime->outputs[output_index].initialized = 0;
                }
            }
            ++session->revision;
        }
        session_json(session, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strcmp(method, "GET") != 0) {
        send_problem(fd, 405, "Method Not Allowed", "method_not_allowed",
                     "This gateway route does not support the requested method.");
        return;
    }
    if (strcmp(path, "/v1/server") == 0) {
        server_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/displays") == 0) {
        displays_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/topology") == 0) {
        topology_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/graphics") == 0) {
        empty_inventory_json(body, sizeof(body), "graphics_cards");
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/audio") == 0) {
        empty_inventory_json(body, sizeof(body), "devices");
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/input/devices") == 0) {
        empty_inventory_json(body, sizeof(body), "devices");
        send_http(fd, 200, "OK", "application/json", body);
    } else {
        send_http(fd, 404, "Not Found", "application/problem+json",
                  "{\"type\":\"about:blank\",\"title\":\"Not Found\","
                  "\"status\":404,\"code\":\"route_not_found\","
                  "\"detail\":\"The requested blit route does not exist.\"}");
    }
}

static void discover_displays(BlitRuntime *runtime)
{
    SDL_DisplayID *ids;
    int count = 0;
    int i;

    runtime->sdl_ready = SDL_Init(SDL_INIT_VIDEO) ? 1 : 0;
    if (!runtime->sdl_ready) {
        snprintf(runtime->sdl_error, sizeof(runtime->sdl_error), "%s",
                 SDL_GetError());
        return;
    }
    ids = SDL_GetDisplays(&count);
    if (ids == NULL) {
        snprintf(runtime->sdl_error, sizeof(runtime->sdl_error), "%s",
                 SDL_GetError());
        return;
    }
    if (count > BLIT_MAX_DISPLAYS)
        count = BLIT_MAX_DISPLAYS;
    runtime->display_count = count;
    runtime->sdl_error[0] = '\0';
    for (i = 0; i < count; ++i) {
        const char *name = SDL_GetDisplayName(ids[i]);
        runtime->displays[i].id = (unsigned long long)ids[i];
        snprintf(runtime->displays[i].name, sizeof(runtime->displays[i].name),
                 "%s", name ? name : "unknown");
        if (!SDL_GetDisplayBounds(ids[i], &runtime->displays[i].bounds)) {
            runtime->displays[i].bounds.x = 0;
            runtime->displays[i].bounds.y = 0;
            runtime->displays[i].bounds.w = 0;
            runtime->displays[i].bounds.h = 0;
        }
    }
    SDL_free(ids);
}

static void initialize_outputs(BlitRuntime *runtime)
{
    int i;

    for (i = 0; i < runtime->display_count; ++i) {
        SDL_Window *window;
        SDL_Renderer *renderer;
        BlitDisplay *display = &runtime->displays[i];

        window = SDL_CreateWindow("blit", display->bounds.w, display->bounds.h,
                                 SDL_WINDOW_BORDERLESS);
        if (window == NULL) {
            snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                     "display %llu window: %s", display->id, SDL_GetError());
            continue;
        }
        SDL_SetWindowPosition(window, display->bounds.x, display->bounds.y);
        renderer = SDL_CreateRenderer(window, NULL);
        if (renderer == NULL) {
            snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                     "display %llu renderer: %s", display->id, SDL_GetError());
            SDL_DestroyWindow(window);
            continue;
        }
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        runtime->outputs[runtime->output_count].window = window;
        runtime->outputs[runtime->output_count].renderer = renderer;
        runtime->outputs[runtime->output_count].display_id = display->id;
        ++runtime->output_count;
    }
}

static void destroy_outputs(BlitRuntime *runtime)
{
    int i;
    for (i = 0; i < runtime->output_count; ++i) {
        SDL_DestroyRenderer(runtime->outputs[i].renderer);
        SDL_DestroyWindow(runtime->outputs[i].window);
    }
    runtime->output_count = 0;
}

static void render_outputs(BlitRuntime *runtime, const BlitFramePlan *plan)
{
    int output_index;
    int plan_index;
    int row;

    for (output_index = 0; output_index < runtime->output_count;
         ++output_index) {
        int target_count = 0;
        int full_redraw;
        SDL_Renderer *renderer = runtime->outputs[output_index].renderer;

        for (plan_index = 0; plan_index < plan->surface_count;
             ++plan_index) {
            BlitSurface *surface = find_surface_id(
                runtime, plan->surfaces[plan_index].surface_id);
            BlitSession *session;
            char display_id[128];

            if (surface == NULL || !surface->used)
                continue;
            session = find_session_id(runtime, surface->owner_session_id);
            if (session == NULL)
                continue;
            if (session->display_id[0] == '\0') {
                if (output_index == 0)
                    ++target_count;
            } else {
                snprintf(display_id, sizeof(display_id), "display-%llu",
                         runtime->outputs[output_index].display_id);
                if (strcmp(session->display_id, display_id) == 0)
                    ++target_count;
            }
        }
        if (target_count == 0) {
            if (!runtime->outputs[output_index].initialized) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
                SDL_RenderPresent(renderer);
                runtime->outputs[output_index].initialized = 1;
            }
            continue;
        }
        full_redraw = !runtime->outputs[output_index].initialized ||
            target_count > 1;
        if (full_redraw) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
        }
        SDL_SetRenderDrawColor(renderer, 190, 210, 190, 255);
        for (plan_index = 0; plan_index < plan->surface_count;
             ++plan_index) {
            BlitSurface *surface = find_surface_id(
                runtime, plan->surfaces[plan_index].surface_id);
            BlitSession *session;
            char display_id[128];
            char line[BLIT_MAX_CELL_COLS + 1];
            int first_row = 0;
            int last_row;
            int column;

            if (surface == NULL || !surface->used ||
                surface->revision != plan->surfaces[plan_index].surface_revision)
                continue;
            session = find_session_id(runtime, surface->owner_session_id);
            if (session == NULL)
                continue;
            if (session->display_id[0] == '\0') {
                if (output_index != 0)
                    continue;
            } else {
                snprintf(display_id, sizeof(display_id), "display-%llu",
                         runtime->outputs[output_index].display_id);
                if (strcmp(session->display_id, display_id) != 0)
                    continue;
            }
            if (!full_redraw) {
                if (!plan->surfaces[plan_index].damage_valid)
                    continue;
                first_row = plan->surfaces[plan_index].damage_y;
                last_row = first_row +
                    plan->surfaces[plan_index].damage_height;
                {
                    SDL_FRect damage;
                    damage.x = 4.0f + plan->surfaces[plan_index].damage_x * 8.0f;
                    damage.y = 4.0f + first_row * 8.0f;
                    damage.w = plan->surfaces[plan_index].damage_width * 8.0f;
                    damage.h = plan->surfaces[plan_index].damage_height * 8.0f;
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_RenderFillRect(renderer, &damage);
                    SDL_SetRenderDrawColor(renderer, 190, 210, 190, 255);
                }
            }
            last_row = full_redraw ? surface->height : last_row;
            if (last_row > BLIT_MAX_CELL_ROWS)
                last_row = BLIT_MAX_CELL_ROWS;
            for (row = first_row; row < last_row; ++row) {
                for (column = 0; column < surface->width &&
                     column < BLIT_MAX_CELL_COLS; ++column)
                    line[column] = surface->cells[row][column] ?
                        surface->cells[row][column] : ' ';
                line[column] = '\0';
                SDL_RenderDebugText(renderer, 4.0f, 4.0f + row * 8.0f,
                                    line);
            }
        }
        SDL_RenderPresent(renderer);
        runtime->outputs[output_index].initialized = 1;
    }
}

int main(int argc, char **argv)
{
    int port = 8080;
    int server_fd;
    const char *bind_address = getenv("BLIT_BIND_ADDRESS");
    struct sockaddr_in address;
    BlitRuntime runtime;

    if (argc > 1)
        port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 2;
    }
    if (bind_address == NULL || bind_address[0] == '\0')
        bind_address = "127.0.0.1";
    memset(&runtime, 0, sizeof(runtime));
    runtime.next_session_id = 1;
    runtime.next_frame_id = 1;
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    discover_displays(&runtime);
    if (runtime.sdl_ready)
        initialize_outputs(&runtime);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
    fcntl(server_fd, F_SETFD, FD_CLOEXEC);
    {
        int reuse = 1;
        setsockopt(server_fd, SOL_SOCKET, SO_REUSEADDR, &reuse, sizeof(reuse));
    }
    memset(&address, 0, sizeof(address));
    address.sin_family = AF_INET;
    if (inet_pton(AF_INET, bind_address, &address.sin_addr) != 1) {
        fprintf(stderr, "invalid BLIT_BIND_ADDRESS: %s\n", bind_address);
        close(server_fd);
        return 2;
    }
    address.sin_port = htons((unsigned short)port);
    if (bind(server_fd, (struct sockaddr *)&address, sizeof(address)) < 0 ||
        listen(server_fd, 16) < 0) {
        perror("bind/listen");
        close(server_fd);
        return 1;
    }
    printf("blit gateway listening on TCP port %d (%d display(s))\n",
           port, runtime.display_count);
    fflush(stdout);

    while (blit_running) {
        int client_fd;
        int max_fd = server_fd;
        int ready;
        int i;
        fd_set readable;
        struct timeval timeout;

        FD_ZERO(&readable);
        FD_SET(server_fd, &readable);
        for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
            if (runtime.streams[i].used) {
                FD_SET(runtime.streams[i].fd, &readable);
                if (runtime.streams[i].fd > max_fd)
                    max_fd = runtime.streams[i].fd;
            }
        }
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime.sessions[i].used && runtime.sessions[i].pty_fd >= 0) {
                FD_SET(runtime.sessions[i].pty_fd, &readable);
                if (runtime.sessions[i].pty_fd > max_fd)
                    max_fd = runtime.sessions[i].pty_fd;
            }
        }
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
        if (ready < 1)
            continue;
        if (FD_ISSET(server_fd, &readable)) {
            char request[BLIT_REQUEST_SIZE];
            ssize_t received;

            client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                break;
            }
            received = receive_request(client_fd, request, sizeof(request));
            if (received > 0) {
                request[received] = '\0';
                handle_request(client_fd, &runtime, request);
            }
            if (!stream_contains(&runtime, client_fd))
                close(client_fd);
        }
        for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
            if (runtime.streams[i].used &&
                FD_ISSET(runtime.streams[i].fd, &readable)) {
                service_stream(&runtime, runtime.streams[i].fd);
            }
        }
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime.sessions[i].used && runtime.sessions[i].pty_fd >= 0 &&
                FD_ISSET(runtime.sessions[i].pty_fd, &readable))
                service_session_output(&runtime, runtime.sessions[i].pty_fd);
        }
    }
    close(server_fd);
    stop_sessions(&runtime);
    destroy_outputs(&runtime);
    if (runtime.sdl_ready)
        SDL_Quit();
    return 0;
}
