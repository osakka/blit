#include <SDL3/SDL.h>

#include <arpa/inet.h>
#include <errno.h>
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
#define BLIT_REQUEST_SIZE 8192

typedef struct {
    unsigned long long id;
    char name[128];
    SDL_Rect bounds;
} BlitDisplay;

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
    int sdl_ready;
    char sdl_error[256];
} BlitRuntime;

static volatile sig_atomic_t blit_running = 1;

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
             "\"terminal-sessions\",\"topology\",\"diagnostics\"],"
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

static int json_string(const char *json, const char *key, char *value,
                       size_t capacity)
{
    char needle[128];
    const char *cursor;
    size_t out = 0;

    snprintf(needle, sizeof(needle), "\"%s\":\"", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    while (*cursor != '\0' && *cursor != '"' && out + 1 < capacity) {
        if (*cursor == '\\' && cursor[1] != '\0')
            ++cursor;
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

    snprintf(needle, sizeof(needle), "\"%s\":", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    *value = (int)strtol(cursor, &end, 10);
    return end != cursor;
}

static BlitSession *find_session(BlitRuntime *runtime, const char *path)
{
    const char *prefix = "/v1/terminal-sessions/";
    const char *id;
    unsigned long long session_id;
    int i;

    if (strncmp(path, prefix, strlen(prefix)) != 0)
        return NULL;
    id = path + strlen(prefix);
    session_id = strtoull(id, NULL, 10);
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used && runtime->sessions[i].id == session_id)
            return &runtime->sessions[i];
    }
    return NULL;
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
    if (width < 1 || height < 1 || width > 1000 || height > 1000)
        return 0;
    memset(display_id, 0, sizeof(display_id));
    json_string(json, "display_id", display_id, sizeof(display_id));
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

static void send_websocket_text(int fd, const char *text)
{
    unsigned char header[2];
    size_t length = strlen(text);

    if (length > 125)
        return;
    header[0] = 0x81;
    header[1] = (unsigned char)length;
    send_all(fd, header, sizeof(header));
    send_all(fd, text, length);
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
        if (strstr(payload, "\"arguments\"") != NULL ||
            strstr(payload, "\"environment\"") != NULL) {
            send_problem(fd, 501, "Not Implemented", "session_options_pending",
                         "Arguments and environment policy are not implemented yet.");
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
                snprintf(session->display_id, sizeof(session->display_id),
                         "%s", display_id);
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
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    discover_displays(&runtime);

    server_fd = socket(AF_INET, SOCK_STREAM, 0);
    if (server_fd < 0) {
        perror("socket");
        return 1;
    }
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
        fd_set readable;
        struct timeval timeout;
        char request[BLIT_REQUEST_SIZE];
        ssize_t received;

        FD_ZERO(&readable);
        FD_SET(server_fd, &readable);
        timeout.tv_sec = 1;
        timeout.tv_usec = 0;
        if (select(server_fd + 1, &readable, NULL, NULL, &timeout) < 1)
            continue;
        client_fd = accept(server_fd, NULL, NULL);
        if (client_fd < 0) {
            if (errno == EINTR) continue;
            break;
        }
        received = recv(client_fd, request, sizeof(request) - 1, 0);
        if (received > 0) {
            request[received] = '\0';
            handle_request(client_fd, &runtime, request);
        }
        close(client_fd);
    }
    close(server_fd);
    stop_sessions(&runtime);
    if (runtime.sdl_ready)
        SDL_Quit();
    return 0;
}
