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
#define BLIT_MAX_IDEMPOTENCY 16
#define BLIT_DEFAULT_FOREGROUND 0xbed2beU
#define BLIT_ATTR_BOLD 1
#define BLIT_ATTR_DIM 2
#define BLIT_ATTR_UNDERLINE 4
#define BLIT_ATTR_INVERSE 8

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
    uint32_t codepoints[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t foreground[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t background[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    unsigned char attributes[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    int cursor_x;
    int cursor_y;
    int cursor_visible;
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
    char key[129];
    char method[8];
    char path[256];
    char payload[1024];
    int status;
    char reason[32];
    char content_type[64];
    char response[2048];
} BlitIdempotency;

typedef struct {
    int used;
    unsigned long long id;
    pid_t process_id;
    int pty_fd;
    unsigned long long revision;
    char display_id[128];
    char state[16];
    char surface_id[128];
    int terminal_escape;
    char terminal_csi[32];
    int terminal_csi_length;
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
    BlitIdempotency idempotency[BLIT_MAX_IDEMPOTENCY];
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

static int request_header(const char *request, const char *name,
                          char *value, size_t capacity)
{
    const char *cursor = strstr(request, name);
    size_t length = 0;

    if (cursor == NULL)
        return 0;
    cursor += strlen(name);
    while (*cursor == ' ' || *cursor == '\t')
        ++cursor;
    while (cursor[length] != '\0' && cursor[length] != '\r' &&
           cursor[length] != '\n' && length + 1 < capacity)
        ++length;
    memcpy(value, cursor, length);
    value[length] = '\0';
    return length > 0;
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

static int json_bool(const char *json, const char *key, int *value)
{
    char needle[128];
    const char *cursor;

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
    if (strncmp(cursor, "true", 4) == 0) {
        *value = 1;
        return 1;
    }
    if (strncmp(cursor, "false", 5) == 0) {
        *value = 0;
        return 1;
    }
    return 0;
}

static int parse_color(const char *text, uint32_t *color)
{
    unsigned int value;

    if (text[0] != '#' || strlen(text) != 7 ||
        sscanf(text + 1, "%06x", &value) != 1)
        return 0;
    *color = (uint32_t)value;
    return 1;
}

static int utf8_decode(const unsigned char *text, size_t length,
                       size_t *consumed, uint32_t *codepoint)
{
    unsigned char first;
    uint32_t value;

    if (length == 0)
        return 0;
    first = text[0];
    if (first < 0x80) {
        *consumed = 1;
        *codepoint = first;
        return 1;
    }
    if (first >= 0xc2 && first <= 0xdf) {
        if (length < 2 || (text[1] & 0xc0) != 0x80)
            return 0;
        *consumed = 2;
        *codepoint = ((uint32_t)(first & 0x1f) << 6) |
            (text[1] & 0x3f);
        return 1;
    }
    if (first >= 0xe0 && first <= 0xef) {
        if (length < 3 || (text[1] & 0xc0) != 0x80 ||
            (text[2] & 0xc0) != 0x80 ||
            (first == 0xe0 && text[1] < 0xa0) ||
            (first == 0xed && text[1] >= 0xa0))
            return 0;
        value = ((uint32_t)(first & 0x0f) << 12) |
            ((uint32_t)(text[1] & 0x3f) << 6) | (text[2] & 0x3f);
        *consumed = 3;
        *codepoint = value;
        return 1;
    }
    if (first >= 0xf0 && first <= 0xf4) {
        if (length < 4 || (text[1] & 0xc0) != 0x80 ||
            (text[2] & 0xc0) != 0x80 || (text[3] & 0xc0) != 0x80 ||
            (first == 0xf0 && text[1] < 0x90) ||
            (first == 0xf4 && text[1] >= 0x90))
            return 0;
        value = ((uint32_t)(first & 0x07) << 18) |
            ((uint32_t)(text[1] & 0x3f) << 12) |
            ((uint32_t)(text[2] & 0x3f) << 6) | (text[3] & 0x3f);
        *consumed = 4;
        *codepoint = value;
        return 1;
    }
    return 0;
}

static int unicode_cell_width(uint32_t codepoint)
{
    if ((codepoint >= 0x0300 && codepoint <= 0x036f) ||
        (codepoint >= 0x1ab0 && codepoint <= 0x1aff) ||
        (codepoint >= 0x1dc0 && codepoint <= 0x1dff) ||
        (codepoint >= 0x20d0 && codepoint <= 0x20ff) ||
        (codepoint >= 0xfe20 && codepoint <= 0xfe2f))
        return 0;
    if (codepoint == 0x2329 || codepoint == 0x232a ||
        (codepoint >= 0x1100 && codepoint <= 0x115f) ||
        (codepoint >= 0x2e80 && codepoint <= 0xa4c6) ||
        (codepoint >= 0xac00 && codepoint <= 0xd7a3) ||
        (codepoint >= 0xf900 && codepoint <= 0xfaff) ||
        (codepoint >= 0xfe10 && codepoint <= 0xfe19) ||
        (codepoint >= 0xfe30 && codepoint <= 0xfe6f) ||
        (codepoint >= 0xff00 && codepoint <= 0xff60) ||
        (codepoint >= 0xffe0 && codepoint <= 0xffe6) ||
        (codepoint >= 0x1f300 && codepoint <= 0x1faff))
        return 2;
    return 1;
}

static int decode_text_cells(const char *text, uint32_t *codepoints,
                             size_t *count)
{
    size_t offset = 0;
    size_t length = strlen(text);

    *count = 0;
    while (offset < length) {
        size_t consumed;
        uint32_t codepoint;

        if (*count >= BLIT_MAX_CELL_COLS ||
            !utf8_decode((const unsigned char *)text + offset,
                         length - offset, &consumed, &codepoint))
            return 0;
        if (codepoint == '\n' || codepoint == '\r' || codepoint < 0x20 ||
            codepoint == 0x7f || unicode_cell_width(codepoint) != 1)
            return 0;
        codepoints[*count] = codepoint;
        ++*count;
        offset += consumed;
    }
    return *count > 0;
}

static size_t utf8_encode(uint32_t codepoint, char output[5])
{
    if (codepoint < 0x80) {
        output[0] = (char)codepoint;
        output[1] = '\0';
        return 1;
    }
    if (codepoint < 0x800) {
        output[0] = (char)(0xc0 | (codepoint >> 6));
        output[1] = (char)(0x80 | (codepoint & 0x3f));
        output[2] = '\0';
        return 2;
    }
    if (codepoint < 0x10000) {
        output[0] = (char)(0xe0 | (codepoint >> 12));
        output[1] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
        output[2] = (char)(0x80 | (codepoint & 0x3f));
        output[3] = '\0';
        return 3;
    }
    output[0] = (char)(0xf0 | (codepoint >> 18));
    output[1] = (char)(0x80 | ((codepoint >> 12) & 0x3f));
    output[2] = (char)(0x80 | ((codepoint >> 6) & 0x3f));
    output[3] = (char)(0x80 | (codepoint & 0x3f));
    output[4] = '\0';
    return 4;
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
                            unsigned long long revision, BlitFramePlan *plan,
                            unsigned long long *conflict_revision)
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
        if (surface == NULL || plan->surface_count >= BLIT_MAX_FRAME_SURFACES)
            return 0;
        if (surface->revision != revision) {
            *conflict_revision = surface->revision;
            return -1;
        }
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

static void surface_delta_json(const BlitSurface *surface, char *body,
                               size_t capacity)
{
    size_t used;

    snprintf(body, capacity,
             "{\"type\":\"surface_delta\",\"surface_id\":"
             "\"surface-%llu\",\"revision\":%llu,"
             "\"cursor\":{\"x\":%d,\"y\":%d,\"visible\":%s}",
             surface->id, surface->revision, surface->cursor_x,
             surface->cursor_y, surface->cursor_visible ? "true" : "false");
    used = strlen(body);
    if (surface->damage_valid) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"damage\":{\"x\":%d,\"y\":%d,"
                                 "\"width\":%d,\"height\":%d}",
                                 surface->damage_x, surface->damage_y,
                                 surface->damage_width, surface->damage_height);
    }
    snprintf(body + used, capacity - used, "}");
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
    const char *cursor_state = strstr(json, "\"cursor\"");
    int updated = 0;

    if (cursor == NULL)
        return 0;
    while ((cursor = strstr(cursor, "\"x\"")) != NULL) {
        char text[BLIT_MAX_CELL_COLS];
        char color_text[32];
        uint32_t foreground = 0;
        uint32_t background = 0;
        unsigned char attributes = 0;
        int foreground_set = 0;
        int background_set = 0;
        const char *next_cell = strstr(cursor + 3, "\"x\"");
        const char *style_cursor = strstr(cursor, "\"style\"");
        int style_seen = style_cursor != NULL &&
            (next_cell == NULL || style_cursor < next_cell);
        int style_valid = 1;
        int flag;
        int x, y;
        uint32_t decoded[BLIT_MAX_CELL_COLS];
        size_t decoded_count;
        size_t i;

        if (!json_int(cursor, "x", &x) || !json_int(cursor, "y", &y) ||
            !json_string(cursor, "text", text, sizeof(text)))
            break;
        if (style_seen) {
            if (json_string(cursor, "foreground", color_text,
                            sizeof(color_text))) {
                foreground_set = 1;
                if (!parse_color(color_text, &foreground))
                    style_valid = 0;
            }
            if (json_string(cursor, "background", color_text,
                            sizeof(color_text))) {
                background_set = 1;
                if (!parse_color(color_text, &background))
                    style_valid = 0;
            }
            if (json_bool(cursor, "bold", &flag) && flag)
                attributes |= BLIT_ATTR_BOLD;
            if (json_bool(cursor, "dim", &flag) && flag)
                attributes |= BLIT_ATTR_DIM;
            if (json_bool(cursor, "underline", &flag) && flag)
                attributes |= BLIT_ATTR_UNDERLINE;
            if (json_bool(cursor, "inverse", &flag) && flag)
                attributes |= BLIT_ATTR_INVERSE;
        }
        if (!style_valid)
            break;
        if (!decode_text_cells(text, decoded, &decoded_count))
            break;
        if (x >= 0 && y >= 0 && x < BLIT_MAX_CELL_COLS &&
            y < BLIT_MAX_CELL_ROWS) {
            for (i = 0; i < decoded_count && x + (int)i < BLIT_MAX_CELL_COLS;
                 ++i) {
                surface->codepoints[y][x + i] = decoded[i];
                surface->cells[y][x + i] = decoded[i] < 0x80 ?
                    (char)decoded[i] : '?';
                if (style_seen) {
                    if (foreground_set)
                        surface->foreground[y][x + i] = foreground + 1;
                    if (background_set)
                        surface->background[y][x + i] = background + 1;
                    surface->attributes[y][x + i] = attributes;
                }
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
    if (cursor_state != NULL) {
        int x;
        int y;
        int visible;

        if (!json_int(cursor_state, "x", &x) ||
            !json_int(cursor_state, "y", &y) ||
            !json_bool(cursor_state, "visible", &visible) ||
            x < 0 || y < 0 || x >= surface->width || y >= surface->height)
            return -1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        surface->cursor_x = x;
        surface->cursor_y = y;
        surface->cursor_visible = visible;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    }
    return updated;
}

static void terminal_clear_cell(BlitSurface *surface, int x, int y)
{
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height)
        return;
    surface->cells[y][x] = 0;
    surface->codepoints[y][x] = 0;
    surface->foreground[y][x] = 0;
    surface->background[y][x] = 0;
    surface->attributes[y][x] = 0;
    mark_surface_damage(surface, x, y);
}

static void terminal_clear_line(BlitSurface *surface, int y, int start)
{
    int x;

    for (x = start; x < surface->width; ++x)
        terminal_clear_cell(surface, x, y);
}

static void terminal_scroll_up(BlitSurface *surface)
{
    int y;

    if (surface->height < 1)
        return;
    for (y = 1; y < surface->height; ++y) {
        memcpy(surface->cells[y - 1], surface->cells[y],
               sizeof(surface->cells[y - 1]));
        memcpy(surface->codepoints[y - 1], surface->codepoints[y],
               sizeof(surface->codepoints[y - 1]));
        memcpy(surface->foreground[y - 1], surface->foreground[y],
               sizeof(surface->foreground[y - 1]));
        memcpy(surface->background[y - 1], surface->background[y],
               sizeof(surface->background[y - 1]));
        memcpy(surface->attributes[y - 1], surface->attributes[y],
               sizeof(surface->attributes[y - 1]));
    }
    memset(surface->cells[surface->height - 1], 0,
           sizeof(surface->cells[surface->height - 1]));
    memset(surface->codepoints[surface->height - 1], 0,
           sizeof(surface->codepoints[surface->height - 1]));
    memset(surface->foreground[surface->height - 1], 0,
           sizeof(surface->foreground[surface->height - 1]));
    memset(surface->background[surface->height - 1], 0,
           sizeof(surface->background[surface->height - 1]));
    memset(surface->attributes[surface->height - 1], 0,
           sizeof(surface->attributes[surface->height - 1]));
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
    surface->cursor_y = surface->height - 1;
}

static int terminal_number(const char **cursor, int *value)
{
    char *end;

    if (**cursor == ';') {
        *value = 0;
        return 1;
    }
    *value = (int)strtol(*cursor, &end, 10);
    if (end == *cursor)
        return 0;
    *cursor = end;
    return 1;
}

static void terminal_apply_csi(BlitSurface *surface, const char *sequence)
{
    const char *cursor = sequence;
    int first = 0;
    int second = 0;
    int has_first = 0;
    int has_second = 0;
    char command;

    while (*cursor >= '0' && *cursor <= '9') {
        has_first = terminal_number(&cursor, &first);
        break;
    }
    if (*cursor == ';') {
        ++cursor;
        has_second = terminal_number(&cursor, &second);
    }
    command = *cursor;
    if (command == 'H' || command == 'f') {
        surface->cursor_y = has_first && first > 0 ? first - 1 : 0;
        surface->cursor_x = has_second && second > 0 ? second - 1 : 0;
        if (surface->cursor_y >= surface->height)
            surface->cursor_y = surface->height - 1;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'A' || command == 'B' ||
               command == 'C' || command == 'D') {
        int amount = has_first && first > 0 ? first : 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        if (command == 'A') surface->cursor_y -= amount;
        if (command == 'B') surface->cursor_y += amount;
        if (command == 'C') surface->cursor_x += amount;
        if (command == 'D') surface->cursor_x -= amount;
        if (surface->cursor_x < 0) surface->cursor_x = 0;
        if (surface->cursor_y < 0) surface->cursor_y = 0;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        if (surface->cursor_y >= surface->height)
            surface->cursor_y = surface->height - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'J' && (!has_first || first == 2)) {
        int y;
        for (y = 0; y < surface->height; ++y)
            terminal_clear_line(surface, y, 0);
    } else if (command == 'K') {
        terminal_clear_line(surface, surface->cursor_y, surface->cursor_x);
    }
}

static void terminal_put_codepoint(BlitSurface *surface, uint32_t codepoint)
{
    if (surface->cursor_x >= surface->width) {
        surface->cursor_x = 0;
        if (surface->cursor_y + 1 < surface->height)
            ++surface->cursor_y;
        else
            terminal_scroll_up(surface);
    }
    terminal_clear_cell(surface, surface->cursor_x, surface->cursor_y);
    surface->codepoints[surface->cursor_y][surface->cursor_x] = codepoint;
    surface->cells[surface->cursor_y][surface->cursor_x] =
        codepoint < 0x80 ? (char)codepoint : '?';
    mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    ++surface->cursor_x;
}

static int terminal_consume_output(BlitSession *session, BlitSurface *surface,
                                   const unsigned char *data, size_t length)
{
    size_t offset = 0;

    while (offset < length) {
        unsigned char byte = data[offset];
        if (session->terminal_escape == 1) {
            if (byte == '[') {
                session->terminal_escape = 2;
                session->terminal_csi_length = 0;
            } else {
                session->terminal_escape = 0;
            }
            ++offset;
            continue;
        }
        if (session->terminal_escape == 2) {
            if (byte >= 0x40 && byte <= 0x7e) {
                if (session->terminal_csi_length <
                    (int)sizeof(session->terminal_csi) - 1)
                    session->terminal_csi[session->terminal_csi_length++] =
                        (char)byte;
                session->terminal_csi[session->terminal_csi_length] = '\0';
                terminal_apply_csi(surface, session->terminal_csi);
                session->terminal_escape = 0;
            } else if (session->terminal_csi_length <
                       (int)sizeof(session->terminal_csi) - 1) {
                session->terminal_csi[session->terminal_csi_length++] =
                    (char)byte;
            } else {
                session->terminal_escape = 0;
            }
            ++offset;
            continue;
        }
        if (byte == 0x1b) {
            session->terminal_escape = 1;
            ++offset;
        } else if (byte == '\r') {
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            surface->cursor_x = 0;
            ++offset;
        } else if (byte == '\n') {
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            if (surface->cursor_y + 1 < surface->height)
                ++surface->cursor_y;
            else
                terminal_scroll_up(surface);
            ++offset;
        } else if (byte == '\b') {
            if (surface->cursor_x > 0)
                --surface->cursor_x;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            ++offset;
        } else if (byte == '\t') {
            surface->cursor_x = (surface->cursor_x + 8) & ~7;
            if (surface->cursor_x >= surface->width)
                surface->cursor_x = surface->width - 1;
            ++offset;
        } else if (byte >= 0x20) {
            size_t consumed;
            uint32_t codepoint;
            if (!utf8_decode(data + offset, length - offset, &consumed,
                             &codepoint)) {
                consumed = 1;
                codepoint = '?';
            } else if (unicode_cell_width(codepoint) != 1) {
                codepoint = '?';
            }
            terminal_put_codepoint(surface, codepoint);
            offset += consumed;
        } else {
            ++offset;
        }
    }
    return 1;
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

static void session_json(BlitRuntime *runtime, BlitSession *session,
                         char *body, size_t capacity)
{
    BlitSurface *surface = find_surface_id(runtime, session->id);
    size_t used;

    refresh_session_state(session);
    snprintf(body, capacity,
             "{\"id\":\"%llu\",\"revision\":%llu,"
             "\"state\":\"%s\",\"surface_id\":\"%s\","
             "\"process_id\":%ld,\"surface_revision\":%llu",
             session->id, session->revision, session->state,
             session->surface_id, (long)session->process_id,
             surface != NULL ? surface->revision : 0);
    used = strlen(body);
    if (session->display_id[0]) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"display_id\":\"%s\"",
                                 session->display_id);
    }
    if (surface != NULL) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"cursor\":{\"x\":%d,\"y\":%d,"
                                 "\"visible\":%s}", surface->cursor_x,
                                 surface->cursor_y,
                                 surface->cursor_visible ? "true" : "false");
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

static int idempotency_replay(BlitRuntime *runtime, int fd,
                              const char *request, const char *method,
                              const char *path, const char *payload)
{
    char key[129];
    size_t payload_length = strlen(payload);
    int i;

    if (!request_header(request, "Idempotency-Key:", key, sizeof(key)))
        return 0;
    for (i = 0; i < BLIT_MAX_IDEMPOTENCY; ++i) {
        BlitIdempotency *record = &runtime->idempotency[i];
        if (!record->used || strcmp(record->key, key) != 0 ||
            strcmp(record->method, method) != 0 ||
            strcmp(record->path, path) != 0)
            continue;
        if (payload_length >= sizeof(record->payload) ||
            strcmp(record->payload, payload) != 0) {
            send_problem(fd, 409, "Conflict", "idempotency_conflict",
                         "The idempotency key was reused with a different request.");
            return 1;
        }
        send_http(fd, record->status, record->reason, record->content_type,
                  record->response);
        return 1;
    }
    return 0;
}

static void idempotency_store(BlitRuntime *runtime, const char *request,
                              const char *method, const char *path,
                              const char *payload, int status,
                              const char *reason, const char *content_type,
                              const char *response)
{
    char key[129];
    int i;

    if (!request_header(request, "Idempotency-Key:", key, sizeof(key)) ||
        strlen(payload) >= sizeof(runtime->idempotency[0].payload) ||
        strlen(response) >= sizeof(runtime->idempotency[0].response))
        return;
    for (i = 0; i < BLIT_MAX_IDEMPOTENCY; ++i)
        if (!runtime->idempotency[i].used)
            break;
    if (i == BLIT_MAX_IDEMPOTENCY)
        return;
    {
        BlitIdempotency *record = &runtime->idempotency[i];
        record->used = 1;
        snprintf(record->key, sizeof(record->key), "%s", key);
        snprintf(record->method, sizeof(record->method), "%s", method);
        snprintf(record->path, sizeof(record->path), "%s", path);
        snprintf(record->payload, sizeof(record->payload), "%s", payload);
        record->status = status;
        snprintf(record->reason, sizeof(record->reason), "%s", reason);
        snprintf(record->content_type, sizeof(record->content_type), "%s",
                 content_type);
        snprintf(record->response, sizeof(record->response), "%s", response);
    }
}

static void send_revision_problem(int fd, const char *code,
                                  const char *detail,
                                  unsigned long long revision)
{
    char body[1024];

    snprintf(body, sizeof(body),
             "{\"type\":\"about:blank\",\"title\":\"Conflict\","
             "\"status\":409,\"code\":\"%s\",\"detail\":\"%s\","
             "\"revision\":%llu}", code, detail, revision);
    send_http(fd, 409, "Conflict", "application/problem+json", body);
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
                surface->cursor_visible = 1;
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
            BlitSurface *surface = find_surface_id(runtime,
                runtime->sessions[i].id);
            char surface_event[512];

            if (surface != NULL && terminal_consume_output(
                    &runtime->sessions[i], surface, output,
                    (size_t)received)) {
                ++surface->revision;
                surface_delta_json(surface, surface_event,
                                   sizeof(surface_event));
                broadcast_stream(runtime, surface_event, BLIT_STREAM_SURFACES);
            }
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
        session_json(runtime, session, body, sizeof(body));
        send_http(fd, 201, "Created", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/surfaces/", strlen("/v1/surfaces/")) == 0 &&
        strcmp(method, "PUT") == 0) {
        BlitSurface *surface = find_surface(runtime, path);
        int base_revision;

        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;

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
            send_revision_problem(fd, "revision_conflict",
                                  "The surface revision does not match the requested update.",
                                  surface->revision);
            return;
        }
        if (apply_cell_update(surface, payload) <= 0) {
            send_problem(fd, 400, "Bad Request", "invalid_cell_update",
                         "The cells array contains no usable text updates.");
            return;
        }
        ++surface->revision;
        mutation_receipt_json(surface, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        idempotency_store(runtime, request, method, path, payload, 200,
                          "OK", "application/json", body);
        {
            char event[512];
            surface_delta_json(surface, event, sizeof(event));
            broadcast_stream(runtime, event, BLIT_STREAM_SURFACES);
        }
        return;
    }
    if (strcmp(path, "/v1/frames") == 0 && strcmp(method, "POST") == 0) {
        int base_revision;
        int plan_status;
        unsigned long long conflict_revision = 0;
        BlitFramePlan plan;

        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;

        if (!json_int(payload, "base_revision", &base_revision)) {
            send_problem(fd, 400, "Bad Request", "invalid_frame_request",
                         "A base revision and existing surface ids are required.");
            return;
        }
        plan_status = build_frame_plan(runtime, payload,
                                       (unsigned long long)base_revision,
                                       &plan, &conflict_revision);
        if (plan_status < 0) {
            send_revision_problem(fd, "revision_conflict",
                                  "A frame surface revision is stale.",
                                  conflict_revision);
            return;
        }
        if (plan_status == 0) {
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
        idempotency_store(runtime, request, method, path, payload, 202,
                          "Accepted", "application/json", body);
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
        session_json(runtime, session, body, sizeof(body));
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

static int surface_targets_output(BlitRuntime *runtime,
                                  BlitSurface *surface, int output_index)
{
    BlitSession *session = find_session_id(runtime, surface->owner_session_id);
    char display_id[128];

    if (session == NULL)
        return 0;
    if (session->display_id[0] == '\0')
        return output_index == 0;
    snprintf(display_id, sizeof(display_id), "display-%llu",
             runtime->outputs[output_index].display_id);
    return strcmp(session->display_id, display_id) == 0;
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

            if (surface == NULL || !surface->used)
                continue;
            if (surface_targets_output(runtime, surface, output_index))
                ++target_count;
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
            int surface_origin_y = 4;
            int prior_index;
            int first_row = 0;
            int last_row;
            int column;

            if (surface == NULL || !surface->used ||
                surface->revision != plan->surfaces[plan_index].surface_revision)
                continue;
            if (!surface_targets_output(runtime, surface, output_index))
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
            for (prior_index = 0; prior_index < plan_index; ++prior_index) {
                BlitSurface *prior = find_surface_id(
                    runtime, plan->surfaces[prior_index].surface_id);
                if (prior != NULL && prior->used &&
                    surface_targets_output(runtime, prior, output_index))
                    surface_origin_y += prior->height * 8 + 4;
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
                    damage.y = (float)surface_origin_y + first_row * 8.0f;
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
                     column < BLIT_MAX_CELL_COLS; ++column) {
                    uint32_t foreground = surface->foreground[row][column] ?
                        surface->foreground[row][column] - 1 :
                        BLIT_DEFAULT_FOREGROUND;
                    uint32_t background = surface->background[row][column] ?
                        surface->background[row][column] - 1 : 0;
                    unsigned char attributes = surface->attributes[row][column];
                    char glyph[5];
                    SDL_FRect cell_background;

                    if (attributes & BLIT_ATTR_INVERSE) {
                        uint32_t swapped = foreground;
                        foreground = background ? background : 0;
                        background = swapped;
                    }
                    if (attributes & BLIT_ATTR_DIM)
                        foreground = ((foreground & 0xfefefeU) >> 1);
                    if (background != 0) {
                        cell_background.x = 4.0f + column * 8.0f;
                        cell_background.y = (float)surface_origin_y + row * 8.0f;
                        cell_background.w = 8.0f;
                        cell_background.h = 8.0f;
                        SDL_SetRenderDrawColor(renderer,
                            (unsigned char)(background >> 16),
                            (unsigned char)(background >> 8),
                            (unsigned char)background, 255);
                        SDL_RenderFillRect(renderer, &cell_background);
                    }
                    SDL_SetRenderDrawColor(renderer,
                        (unsigned char)(foreground >> 16),
                        (unsigned char)(foreground >> 8),
                        (unsigned char)foreground, 255);
                    utf8_encode(surface->codepoints[row][column] ?
                        surface->codepoints[row][column] : ' ', glyph);
                    SDL_RenderDebugText(renderer, 4.0f + column * 8.0f,
                        (float)surface_origin_y + row * 8.0f, glyph);
                    if (attributes & BLIT_ATTR_BOLD)
                        SDL_RenderDebugText(renderer, 5.0f + column * 8.0f,
                            (float)surface_origin_y + row * 8.0f, glyph);
                    if (attributes & BLIT_ATTR_UNDERLINE)
                        SDL_RenderLine(renderer, 4.0f + column * 8.0f,
                            (float)surface_origin_y + row * 8.0f + 7.0f,
                            11.0f + column * 8.0f,
                            (float)surface_origin_y + row * 8.0f + 7.0f);
                }
            }
            if (surface->cursor_visible &&
                surface->cursor_y >= first_row && surface->cursor_y < last_row) {
                SDL_SetRenderDrawColor(renderer,
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 16),
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 8),
                    (unsigned char)BLIT_DEFAULT_FOREGROUND, 255);
                SDL_RenderLine(renderer, 4.0f + surface->cursor_x * 8.0f,
                    (float)surface_origin_y + surface->cursor_y * 8.0f,
                    4.0f + surface->cursor_x * 8.0f,
                    (float)surface_origin_y + surface->cursor_y * 8.0f + 7.0f);
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
