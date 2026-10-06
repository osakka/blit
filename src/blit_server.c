#include <SDL3/SDL.h>
#include <linux/input.h>
#include "blit_kms.h"

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

extern char **environ;

#define BLIT_MAX_DISPLAYS 16
#define BLIT_MAX_PANES 64
#define BLIT_MAX_SESSIONS 16
#define BLIT_MAX_STREAMS 16
#define BLIT_REQUEST_SIZE 8192
#define BLIT_MAX_CELL_COLS 512
#define BLIT_MAX_CELL_ROWS 160
#define BLIT_MAX_INPUT_DEVICES 32
#define BLIT_STREAM_DIAGNOSTICS 1
#define BLIT_STREAM_SURFACES 2
#define BLIT_STREAM_FRAMES 4
#define BLIT_STREAM_INPUT 8
#define BLIT_STREAM_HARDWARE 16
#define BLIT_STREAM_TERMINALS 32
#define BLIT_MAX_FRAME_SURFACES 16
#define BLIT_MAX_IDEMPOTENCY 16
#define BLIT_MAX_EVENT_HISTORY 64
#define BLIT_DEFAULT_FOREGROUND 0xbed2beU
#define BLIT_ATTR_BOLD 1
#define BLIT_ATTR_DIM 2
#define BLIT_ATTR_UNDERLINE 4
#define BLIT_ATTR_INVERSE 8
#define BLIT_ATTR_WIDE 16
#define BLIT_ATTR_CONTINUATION 32
#define BLIT_ATTR_CONCEAL 64
#define BLIT_ATTR_ITALIC 128
#define BLIT_ATTR_BLINK 256
#define BLIT_ATTR_STRIKE 512
#define BLIT_MAX_COMBINING 4

typedef struct {
    unsigned long long id;
    char name[128];
    SDL_Rect bounds;
    double refresh_hz;
} BlitDisplay;

typedef struct {
    int used;
    char id[128];
    char display_id[128];
    int x;
    int y;
    int width;
    int height;
} BlitPane;

typedef struct {
    int valid;
    int x;
    int y;
    uint32_t foreground;
    uint32_t background;
    uint16_t attributes;
    int foreground_set;
    int background_set;
    uint32_t underline_color;
    int underline_color_set;
    int charset_dec;
    int charset_g1_dec;
    int charset_shift;
    int origin_mode;
    int autowrap;
} BlitSavedCursor;

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
    uint32_t combining[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS][BLIT_MAX_COMBINING];
    unsigned char combining_count[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t foreground[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t background[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t underline_color[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint16_t attributes[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    char alternate_cells[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_codepoints[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_combining[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS][BLIT_MAX_COMBINING];
    unsigned char alternate_combining_count[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_foreground[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_background[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_underline_color[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint16_t alternate_attributes[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int cursor_style;
    int scroll_top;
    int scroll_bottom;
    unsigned char tab_stops[BLIT_MAX_CELL_COLS];
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
    int used;
    char id[128];
    char device_id[128];
    char target_session_id[128];
    int enabled;
} BlitInputRoute;

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
    unsigned long long sequence;
    unsigned int subscription;
    char message[2304];
} BlitEventRecord;

typedef struct {
    int used;
    unsigned long long id;
    pid_t process_id;
    int pty_fd;
    unsigned long long revision;
    char display_id[128];
    char pane_id[128];
    char state[16];
    char surface_id[128];
    int exit_code;
    int focused;
    int surface_mode;
    int terminal_escape;
    char terminal_csi[32];
    int terminal_csi_length;
    int terminal_string_type;
    char terminal_string[256];
    int terminal_string_length;
    char terminal_title[128];
    int terminal_charset_dec;
    int terminal_charset_g1_dec;
    int terminal_charset_shift;
    int terminal_charset_g0_pending;
    uint32_t terminal_last_codepoint;
    int terminal_last_width;
    uint32_t terminal_foreground;
    uint32_t terminal_background;
    uint16_t terminal_attributes;
    int terminal_foreground_set;
    int terminal_background_set;
    int terminal_alternate;
    int terminal_saved_x;
    int terminal_saved_y;
    int terminal_cursor_saved;
    int terminal_cursor_saved_x;
    int terminal_cursor_saved_y;
    BlitSavedCursor terminal_saved_cursor[2];
    BlitSavedCursor terminal_alternate_saved_cursor;
    int terminal_dec_cursor_saved;
    int terminal_dec_cursor_saved_x;
    int terminal_dec_cursor_saved_y;
    int terminal_application_cursor;
    int terminal_application_keypad;
    int terminal_origin_mode;
    int terminal_autowrap;
    int terminal_newline_mode;
    int terminal_insert_mode;
    int terminal_focus_reporting;
    int terminal_bracketed_paste;
    int terminal_mouse_mode;
    int terminal_mouse_sgr;
    int terminal_synchronized;
    uint32_t terminal_underline_color;
    int terminal_underline_color_set;
} BlitSession;

enum {
    BLIT_SURFACE_TERMINAL = 0,
    BLIT_SURFACE_EXTERNAL = 1,
    BLIT_SURFACE_MIRROR = 2
};

typedef struct {
    int display_count;
    BlitDisplay displays[BLIT_MAX_DISPLAYS];
    int pane_count;
    BlitPane panes[BLIT_MAX_PANES];
    int pane_columns;
    int pane_rows;
    unsigned long long topology_revision;
    int render_pending;
    uint64_t render_last_ticks;
    int render_interval_ms;
    int session_count;
    unsigned long long next_session_id;
    BlitSession sessions[BLIT_MAX_SESSIONS];
    BlitSurface surfaces[BLIT_MAX_SESSIONS];
    unsigned long long next_frame_id;
    unsigned long long frame_revision;
    unsigned long long next_event_sequence;
    unsigned int mouse_buttons;
    int font_scale;
    int pointer_valid;
    int pointer_output;
    float pointer_x;
    float pointer_y;
    float previous_pointer_x;
    float previous_pointer_y;
    int previous_pointer_output;
    int pointer_dirty;
    int evdev_mouse_fds[BLIT_MAX_INPUT_DEVICES];
    char evdev_mouse_paths[BLIT_MAX_INPUT_DEVICES][128];
    int evdev_mouse_count;
    int evdev_mouse_reported[BLIT_MAX_INPUT_DEVICES];
    int evdev_keyboard_fds[BLIT_MAX_INPUT_DEVICES];
    char evdev_keyboard_paths[BLIT_MAX_INPUT_DEVICES][128];
    int evdev_keyboard_count;
    int evdev_keyboard_reported[BLIT_MAX_INPUT_DEVICES];
    int evdev_shift;
    int evdev_ctrl;
    int evdev_alt;
    float evdev_mouse_x;
    float evdev_mouse_y;
    BlitStream streams[BLIT_MAX_STREAMS];
    BlitInputRoute input_routes[16];
    unsigned long long input_routes_revision;
    BlitOutput outputs[BLIT_MAX_DISPLAYS];
    BlitIdempotency idempotency[BLIT_MAX_IDEMPOTENCY];
    BlitEventRecord event_history[BLIT_MAX_EVENT_HISTORY];
    unsigned int next_event_history;
    char auth_token[256];
    int output_count;
    int sdl_ready;
    int direct_kms;
    BlitKms *kms;
    char sdl_error[256];
    int listen_port;
} BlitRuntime;

static volatile sig_atomic_t blit_running = 1;

static void render_outputs(BlitRuntime *runtime, const BlitFramePlan *plan);
static const char *pane_surface_id(const BlitRuntime *runtime,
                                   const BlitPane *pane);
static void discover_panes(BlitRuntime *runtime);
static void focus_session(BlitRuntime *runtime, BlitSession *target);

static void render_pointer(BlitRuntime *runtime, int output_index)
{
    SDL_Renderer *renderer;
    float x;
    float y;

    if (!runtime->pointer_valid || runtime->pointer_output != output_index)
        return;
    renderer = runtime->outputs[output_index].renderer;
    x = runtime->pointer_x / runtime->font_scale;
    y = runtime->pointer_y / runtime->font_scale;
    /* KMSDRM does not reliably provide a hardware cursor.  Draw a small
       high-contrast software cursor so pointer visibility is independent of
       the compositor, desktop shell, or terminal mouse-reporting mode. */
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
    SDL_RenderLine(renderer, x, y, x, y + 15.0f);
    SDL_RenderLine(renderer, x, y, x + 11.0f, y + 11.0f);
    SDL_RenderLine(renderer, x + 4.0f, y + 10.0f, x + 7.0f, y + 17.0f);
    SDL_SetRenderDrawColor(renderer, 255, 255, 255, 255);
    SDL_RenderLine(renderer, x + 1.0f, y + 1.0f, x + 1.0f, y + 12.0f);
    SDL_RenderLine(renderer, x + 1.0f, y + 1.0f, x + 9.0f, y + 9.0f);
    SDL_RenderLine(renderer, x + 4.0f, y + 10.0f, x + 6.0f, y + 15.0f);
}

static void clear_previous_pointer(BlitRuntime *runtime, int output_index)
{
    SDL_FRect area;

    if (!runtime->pointer_dirty ||
        runtime->previous_pointer_output != output_index)
        return;
    area.x = runtime->previous_pointer_x / runtime->font_scale;
    area.y = runtime->previous_pointer_y / runtime->font_scale;
    area.w = 20.0f;
    area.h = 20.0f;
    SDL_SetRenderDrawColor(runtime->outputs[output_index].renderer,
                           0, 0, 0, 255);
    SDL_RenderFillRect(runtime->outputs[output_index].renderer, &area);
}

static int runtime_has_synchronized_output(const BlitRuntime *runtime)
{
    int index;

    for (index = 0; index < BLIT_MAX_SESSIONS; ++index)
        if (runtime->sessions[index].used &&
            runtime->sessions[index].terminal_synchronized)
            return 1;
    return 0;
}

static void render_current_surfaces(BlitRuntime *runtime)
{
    BlitFramePlan plan;
    int i;

    memset(&plan, 0, sizeof(plan));
    plan.revision = runtime->frame_revision;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSurface *surface = &runtime->surfaces[i];
        if (!surface->used || plan.surface_count >= BLIT_MAX_FRAME_SURFACES)
            continue;
        plan.surfaces[plan.surface_count].surface_id = surface->id;
        plan.surfaces[plan.surface_count].surface_revision = surface->revision;
        plan.surfaces[plan.surface_count].damage_valid = surface->damage_valid;
        plan.surfaces[plan.surface_count].damage_x = surface->damage_x;
        plan.surfaces[plan.surface_count].damage_y = surface->damage_y;
        plan.surfaces[plan.surface_count].damage_width = surface->damage_width;
        plan.surfaces[plan.surface_count].damage_height = surface->damage_height;
        ++plan.surface_count;
    }
    render_outputs(runtime, &plan);
}

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
    char escaped_error[256];
    const char *backend = runtime->direct_kms ? "drm-kms" :
        (runtime->sdl_ready ? "sdl3" : "unavailable");
    json_escape(runtime->sdl_error, escaped_error, sizeof(escaped_error));
    snprintf(body, capacity,
             "{\"id\":\"blit\",\"name\":\"blit\","
             "\"api_version\":\"0.1\",\"revision\":1,"
             "\"capabilities\":[\"http-control\",\"websocket-stream\","
             "\"terminal-sessions\",\"topology\","
             "\"native-presentation\",\"graphics-inventory\","
             "\"audio-inventory\",\"input-devices\",\"input-routes\","
             "\"diagnostics\"],\"display_count\":%d,"
             "\"display_backend\":{\"name\":\"%s\",\"ready\":%s,"
             "\"error\":\"%s\"},\"resources\":{"
             "\"graphics\":\"/v1/graphics\",\"displays\":\"/v1/displays\","
             "\"audio\":\"/v1/audio\",\"input_devices\":\"/v1/input/devices\","
             "\"input_routes\":\"/v1/input/routes\",\"topology\":\"/v1/topology\","
             "\"diagnostics\":\"/v1/diagnostics\"}}",
             runtime->display_count, backend,
             (runtime->direct_kms ? runtime->display_count > 0 :
              runtime->sdl_ready && runtime->output_count > 0) ?
                 "true" : "false", escaped_error);
}

static void diagnostics_json(const BlitRuntime *runtime, char *body,
                             size_t capacity)
{
    char escaped_error[256];
    const char *backend = runtime->direct_kms ? "drm-kms" :
        (runtime->sdl_ready ? "sdl3" : "unavailable");
    int sessions = 0;
    int streams = 0;
    int routes = 0;
    int surfaces = 0;
    int i;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used) ++sessions;
        if (runtime->surfaces[i].used) ++surfaces;
    }
    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used) ++streams;
    for (i = 0; i < 16; ++i)
        if (runtime->input_routes[i].used) ++routes;
    json_escape(runtime->sdl_error, escaped_error, sizeof(escaped_error));
    snprintf(body, capacity,
             "{\"revision\":1,\"health\":{\"ready\":%s,"
             "\"backend\":\"%s\",\"error\":\"%s\"},"
             "\"resources\":{\"displays\":%d,\"outputs\":%d,"
             "\"sessions\":%d,\"surfaces\":%d,\"streams\":%d,"
             "\"input_routes\":%d,\"keyboards\":%d,\"pointers\":%d},"
             "\"render\":{\"pending\":%s,\"interval_ms\":%d,"
             "\"font_scale\":%d,\"direct_kms\":%s},"
             "\"errors\":%s,\"warnings\":%s}",
             (runtime->direct_kms ? runtime->display_count > 0 :
              runtime->sdl_ready && runtime->output_count > 0) ?
                 "true" : "false", backend, escaped_error,
             runtime->display_count, runtime->output_count, sessions, surfaces,
             streams, routes, runtime->evdev_keyboard_count,
             runtime->evdev_mouse_count, runtime->render_pending ? "true" : "false",
             runtime->render_interval_ms, runtime->font_scale,
             runtime->direct_kms ? "true" : "false",
             runtime->sdl_error[0] ? "[\"backend_error\"]" : "[]",
             runtime->evdev_keyboard_count == 0 || runtime->evdev_mouse_count == 0 ?
                 "[\"input_device_missing\"]" : "[]");
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
            "\"height\":%d},\"modes\":[{\"width\":%d,"
            "\"height\":%d,\"refresh_hz\":%.3f}],"
            "\"active_mode\":{\"width\":%d,\"height\":%d,"
            "\"refresh_hz\":%.3f},\"presentation_state\":\"presenting\"}",
            i == 0 ? "" : ",", runtime->displays[i].id, escaped_name,
            runtime->displays[i].bounds.x, runtime->displays[i].bounds.y,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h,
            runtime->displays[i].refresh_hz,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h,
            runtime->displays[i].refresh_hz);
    }
    snprintf(body + used, capacity - used, "]}");
}

static void topology_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    size_t used = 0;
    int i;

    {
        char revision_prefix[256];
        snprintf(revision_prefix, sizeof(revision_prefix),
                             "{\"id\":\"topology-0\",\"revision\":%llu,"
                 "\"layout\":{\"columns\":%d,\"rows\":%d},"
                 "\"outputs\":[", runtime->topology_revision,
                 runtime->pane_columns, runtime->pane_rows);
        snprintf(body, capacity, "%s", revision_prefix);
        used = strlen(body);
    }
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
    used += (size_t)snprintf(body + used, capacity - used, "],\"panes\":[");
    for (i = 0; i < runtime->pane_count && used < capacity; ++i) {
        const BlitPane *pane = &runtime->panes[i];
        const char *surface_id = pane_surface_id(runtime, pane);
        used += (size_t)snprintf(
            body + used, capacity - used,
            "%s{\"id\":\"%s\",\"display_id\":\"%s\","
            "\"bounds\":{\"x\":%d,\"y\":%d,\"width\":%d,\"height\":%d},"
            "\"surface_id\":%s%s%s}",
            i == 0 ? "" : ",", pane->id, pane->display_id, pane->x,
            pane->y, pane->width, pane->height,
            surface_id != NULL ? "\"" : "null",
            surface_id != NULL ? surface_id : "",
            surface_id != NULL ? "\"" : "");
    }
    snprintf(body + used, capacity - used, "]}");
}

static void empty_inventory_json(char *body, size_t capacity,
                                 const char *key)
{
    snprintf(body, capacity, "{\"revision\":1,\"%s\":[]}", key);
}

static int input_bit(const unsigned long *bits, int code)
{
    return (bits[code / (int)(sizeof(unsigned long) * 8)] &
            (1UL << (code % (int)(sizeof(unsigned long) * 8)))) != 0;
}

static void graphics_json(char *body, size_t capacity)
{
    if (access("/dev/dri/card0", F_OK) != 0) {
        empty_inventory_json(body, capacity, "graphics_cards");
        return;
    }
    snprintf(body, capacity,
             "{\"revision\":1,\"graphics_cards\":[{"
             "\"id\":\"card-0\",\"vendor\":\"linux-drm\","
             "\"device\":\"/dev/dri/card0\","
             "\"render_apis\":[\"drm\",\"kms\"],"
             "\"formats\":[\"xrgb8888\",\"argb8888\"],"
             "\"capabilities\":{\"page_flipping\":true,"
             "\"direct_scanout\":true}}]}" );
}

static void audio_json(char *body, size_t capacity)
{
    if (access("/dev/snd", F_OK) != 0) {
        empty_inventory_json(body, capacity, "devices");
        return;
    }
    snprintf(body, capacity,
             "{\"revision\":1,\"devices\":[{"
             "\"id\":\"alsa\",\"name\":\"ALSA sound device\","
             "\"direction\":\"duplex\",\"formats\":[\"s16le\"],"
             "\"channels\":2,\"sample_rates\":[44100,48000]}]}" );
}

static void input_json(char *body, size_t capacity)
{
    size_t used = 0;
    int index;
    int device_count = 0;
    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"devices\":[");
    for (index = 0; index < 32 && used < capacity; ++index) {
        char path[64];
        char name[256];
        unsigned long event_bits[4];
        unsigned long key_bits[32];
        unsigned long rel_bits[4];
        int fd;
        int pointer;
        int keyboard;
        int first_kind = 1;
        int first_capability = 1;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        memset(name, 0, sizeof(name));
        memset(event_bits, 0, sizeof(event_bits));
        memset(key_bits, 0, sizeof(key_bits));
        memset(rel_bits, 0, sizeof(rel_bits));
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits);
        pointer = input_bit(key_bits, BTN_LEFT) || input_bit(key_bits, BTN_RIGHT) ||
            input_bit(rel_bits, REL_X) || input_bit(rel_bits, REL_Y);
        keyboard = input_bit(key_bits, KEY_A) || input_bit(key_bits, KEY_ENTER);
        if (!pointer && !keyboard) { close(fd); continue; }
        used += (size_t)snprintf(body + used, capacity - used,
                                 "%s{\"id\":\"event-%d\",\"name\":\"",
                                 device_count++ > 0 ? "," : "", index);
        {
            char escaped[512];
            json_escape(name[0] ? name : path, escaped, sizeof(escaped));
            used += (size_t)snprintf(body + used, capacity - used, "%s\",\"kinds\":[", escaped);
        }
        if (keyboard) {
            used += (size_t)snprintf(body + used, capacity - used, "\"keyboard\"");
            first_kind = 0;
        }
        if (pointer) {
            used += (size_t)snprintf(body + used, capacity - used, "%s\"pointer\"", first_kind ? "" : ",");
            first_kind = 0;
        }
        used += (size_t)snprintf(body + used, capacity - used, "],\"capabilities\":[");
        if (input_bit(rel_bits, REL_X)) { used += (size_t)snprintf(body + used, capacity - used, "\"rel-x\""); first_capability = 0; }
        if (input_bit(rel_bits, REL_Y)) { used += (size_t)snprintf(body + used, capacity - used, "%s\"rel-y\"", first_capability ? "" : ","); first_capability = 0; }
        if (input_bit(key_bits, BTN_LEFT)) { used += (size_t)snprintf(body + used, capacity - used, "%s\"buttons\"", first_capability ? "" : ","); first_capability = 0; }
        if (keyboard) { used += (size_t)snprintf(body + used, capacity - used, "%s\"keys\"", first_capability ? "" : ","); }
        used += (size_t)snprintf(body + used, capacity - used, "]}");
        close(fd);
    }
    snprintf(body + used, capacity - used, "]}");
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
    if (*cursor++ != ':')
        return 0;
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

/* Parse a bounded JSON array of strings.  The API deliberately keeps process
   launch inputs small and explicit: this is not a general JSON implementation
   and must never allocate based on client-controlled counts. */
static int json_string_array(const char *json, const char *key,
                             char storage[][256], char **values, int limit)
{
    char needle[128];
    const char *cursor;
    int count = 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != ':')
        return -1;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != '[')
        return -1;
    for (;;) {
        size_t out = 0;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') ++cursor;
        if (*cursor == ']')
            return count;
        if (*cursor++ != '"' || count >= limit)
            return -1;
        while (*cursor != '\0' && *cursor != '"') {
            if (out + 1 >= 256)
                return -1;
            if (*cursor == '\\' && cursor[1] != '\0') {
                ++cursor;
                if (*cursor == 'n') storage[count][out++] = '\n';
                else if (*cursor == 'r') storage[count][out++] = '\r';
                else if (*cursor == 't') storage[count][out++] = '\t';
                else storage[count][out++] = *cursor;
                ++cursor;
            } else {
                storage[count][out++] = *cursor++;
            }
        }
        if (*cursor++ != '"')
            return -1;
        storage[count][out] = '\0';
        values[count] = storage[count];
        ++count;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (*cursor != ',' && *cursor != ']')
            return -1;
    }
}

/* Parse the string-to-string environment object.  Values are applied over
   the inherited environment in the child, so PATH and the normal Hibr/Blit
   launch context remain available unless explicitly replaced. */
static int json_string_object(const char *json, const char *key,
                              char key_storage[][128],
                              char value_storage[][256], int limit)
{
    char needle[128];
    const char *cursor;
    int count = 0;

    snprintf(needle, sizeof(needle), "\"%s\"", key);
    cursor = strstr(json, needle);
    if (cursor == NULL)
        return 0;
    cursor += strlen(needle);
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != ':')
        return -1;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != '{')
        return -1;
    for (;;) {
        size_t key_len = 0;
        size_t value_len = 0;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') ++cursor;
        if (*cursor == '}')
            return count;
        if (*cursor++ != '"' || count >= limit)
            return -1;
        while (*cursor != '\0' && *cursor != '"') {
            if (key_len + 1 >= 128 || *cursor == '\\')
                return -1;
            key_storage[count][key_len++] = *cursor++;
        }
        if (*cursor++ != '"' || key_len == 0)
            return -1;
        key_storage[count][key_len] = '\0';
        if (strchr(key_storage[count], '=') != NULL)
            return -1;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (*cursor++ != ':')
            return -1;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (*cursor++ != '"')
            return -1;
        while (*cursor != '\0' && *cursor != '"') {
            if (value_len + 1 >= 256)
                return -1;
            if (*cursor == '\\' && cursor[1] != '\0') {
                ++cursor;
                if (*cursor == 'n') value_storage[count][value_len++] = '\n';
                else if (*cursor == 'r') value_storage[count][value_len++] = '\r';
                else if (*cursor == 't') value_storage[count][value_len++] = '\t';
                else value_storage[count][value_len++] = *cursor;
                ++cursor;
            } else {
                value_storage[count][value_len++] = *cursor++;
            }
        }
        if (*cursor++ != '"')
            return -1;
        value_storage[count][value_len] = '\0';
        ++count;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (*cursor != ',' && *cursor != '}')
            return -1;
    }
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
             "\"cursor\":{\"x\":%d,\"y\":%d,\"visible\":%s,"
             "\"style\":%d}",
             surface->id, surface->revision, surface->cursor_x,
             surface->cursor_y, surface->cursor_visible ? "true" : "false",
             surface->cursor_style);
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

static void surface_snapshot_json(const BlitSurface *surface, char *body,
                                  size_t capacity)
{
    size_t used;
    int x;
    int y;
    int first = 1;

    used = (size_t)snprintf(body, capacity,
        "{\"id\":\"surface-%llu\",\"revision\":%llu,"
        "\"width\":%d,\"height\":%d,\"cursor\":{\"x\":%d,"
        "\"y\":%d,\"visible\":%s,\"style\":%d},\"cells\":[",
        surface->id, surface->revision, surface->width, surface->height,
        surface->cursor_x, surface->cursor_y,
        surface->cursor_visible ? "true" : "false", surface->cursor_style);
    for (y = 0; y < surface->height; ++y) {
        for (x = 0; x < surface->width; ++x) {
            char text[5];
            char escaped[16];
            uint32_t codepoint = surface->codepoints[y][x];
            uint16_t attributes = surface->attributes[y][x];
            int styled = surface->foreground[y][x] != 0 ||
                surface->background[y][x] != 0 ||
                surface->underline_color[y][x] != 0 || attributes != 0;

            if (codepoint == 0 && !styled)
                continue;
            utf8_encode(codepoint ? codepoint : ' ', text);
            json_escape(text, escaped, sizeof(escaped));
            used += (size_t)snprintf(body + used, capacity - used,
                "%s{\"x\":%d,\"y\":%d,\"text\":\"%s\"",
                first ? "" : ",", x, y, escaped);
            first = 0;
            if (styled) {
                uint32_t foreground = surface->foreground[y][x] ?
                    surface->foreground[y][x] - 1 : BLIT_DEFAULT_FOREGROUND;
                uint32_t background = surface->background[y][x] ?
                    surface->background[y][x] - 1 : 0;
                used += (size_t)snprintf(body + used, capacity - used,
                    ",\"style\":{\"foreground\":\"#%06x\","
                    "\"background\":\"#%06x\",\"bold\":%s,"
                    "\"dim\":%s,\"italic\":%s,\"underline\":%s,"
                    "\"blink\":%s,\"inverse\":%s,\"strike\":%s,"
                    "\"concealed\":%s,\"underline_color\":\"#%06x\"}",
                    (unsigned int)foreground, (unsigned int)background,
                    attributes & BLIT_ATTR_BOLD ? "true" : "false",
                    attributes & BLIT_ATTR_DIM ? "true" : "false",
                    attributes & BLIT_ATTR_ITALIC ? "true" : "false",
                    attributes & BLIT_ATTR_UNDERLINE ? "true" : "false",
                    attributes & BLIT_ATTR_BLINK ? "true" : "false",
                    attributes & BLIT_ATTR_INVERSE ? "true" : "false",
                    attributes & BLIT_ATTR_STRIKE ? "true" : "false",
                    attributes & BLIT_ATTR_CONCEAL ? "true" : "false",
                    (unsigned int)(surface->underline_color[y][x] ?
                        surface->underline_color[y][x] - 1 : 0));
            }
            used += (size_t)snprintf(body + used, capacity - used, "}");
        }
    }
    snprintf(body + used, capacity - used, "]}");
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
        uint32_t underline_color = 0;
        uint16_t attributes = 0;
        int foreground_set = 0;
        int background_set = 0;
        int underline_color_set = 0;
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
            if (json_bool(cursor, "italic", &flag) && flag)
                attributes |= BLIT_ATTR_ITALIC;
            if (json_bool(cursor, "underline", &flag) && flag)
                attributes |= BLIT_ATTR_UNDERLINE;
            if (json_bool(cursor, "blink", &flag) && flag)
                attributes |= BLIT_ATTR_BLINK;
            if (json_bool(cursor, "inverse", &flag) && flag)
                attributes |= BLIT_ATTR_INVERSE;
            if (json_bool(cursor, "strike", &flag) && flag)
                attributes |= BLIT_ATTR_STRIKE;
            if (json_bool(cursor, "concealed", &flag) && flag)
                attributes |= BLIT_ATTR_CONCEAL;
            if (json_string(cursor, "underline_color", color_text,
                            sizeof(color_text))) {
                underline_color_set = parse_color(color_text, &underline_color);
                if (!underline_color_set)
                    style_valid = 0;
            }
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
                    if (underline_color_set)
                        surface->underline_color[y][x + i] = underline_color + 1;
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
        int style = surface->cursor_style;

        if (!json_int(cursor_state, "x", &x) ||
            !json_int(cursor_state, "y", &y) ||
            !json_bool(cursor_state, "visible", &visible) ||
            x < 0 || y < 0 || x >= surface->width || y >= surface->height)
            return -1;
        if (strstr(cursor_state, "\"style\"") != NULL &&
            (!json_int(cursor_state, "style", &style) || style < 0 || style > 6))
            return -1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        surface->cursor_x = x;
        surface->cursor_y = y;
        surface->cursor_visible = visible;
        surface->cursor_style = style;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        if (updated == 0)
            updated = 1;
    }
    return updated;
}

static void terminal_clear_active_screen(BlitSurface *surface)
{
    memset(surface->cells, 0, sizeof(surface->cells));
    memset(surface->codepoints, 0, sizeof(surface->codepoints));
    memset(surface->combining, 0, sizeof(surface->combining));
    memset(surface->combining_count, 0, sizeof(surface->combining_count));
    memset(surface->foreground, 0, sizeof(surface->foreground));
    memset(surface->background, 0, sizeof(surface->background));
    memset(surface->attributes, 0, sizeof(surface->attributes));
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_alignment_test(BlitSurface *surface)
{
    int x;
    int y;
    terminal_clear_active_screen(surface);
    for (y = 0; y < surface->height; ++y) {
        for (x = 0; x < surface->width; ++x) {
            surface->cells[y][x] = 'E';
            surface->codepoints[y][x] = 'E';
            surface->foreground[y][x] = 0;
            surface->background[y][x] = 0;
            surface->underline_color[y][x] = 0;
            surface->attributes[y][x] = 0;
        }
    }
    surface->cursor_x = 0;
    surface->cursor_y = 0;
    surface->cursor_visible = 1;
    surface->cursor_style = 1;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_store_cursor(BlitSavedCursor *saved,
                                  BlitSession *session, BlitSurface *surface);

static void terminal_enter_alternate(BlitSession *session,
                                     BlitSurface *surface, int save_cursor)
{
    if (session->terminal_alternate)
        return;
    if (save_cursor)
        terminal_store_cursor(&session->terminal_alternate_saved_cursor,
                              session, surface);
    memcpy(surface->alternate_cells, surface->cells,
           sizeof(surface->cells));
    memcpy(surface->alternate_codepoints, surface->codepoints,
           sizeof(surface->codepoints));
    memcpy(surface->alternate_combining, surface->combining,
           sizeof(surface->combining));
    memcpy(surface->alternate_combining_count, surface->combining_count,
           sizeof(surface->combining_count));
    memcpy(surface->alternate_foreground, surface->foreground,
           sizeof(surface->foreground));
    memcpy(surface->alternate_background, surface->background,
           sizeof(surface->background));
    memcpy(surface->alternate_underline_color, surface->underline_color,
           sizeof(surface->underline_color));
    memcpy(surface->alternate_attributes, surface->attributes,
           sizeof(surface->attributes));
    session->terminal_saved_x = surface->cursor_x;
    session->terminal_saved_y = surface->cursor_y;
    session->terminal_alternate = 1;
    terminal_clear_active_screen(surface);
    surface->cursor_x = 0;
    surface->cursor_y = 0;
    surface->cursor_visible = 1;
    surface->cursor_style = 1;
}

static void terminal_leave_alternate(BlitSession *session,
                                     BlitSurface *surface)
{
    if (!session->terminal_alternate)
        return;
    memcpy(surface->cells, surface->alternate_cells,
           sizeof(surface->cells));
    memcpy(surface->codepoints, surface->alternate_codepoints,
           sizeof(surface->codepoints));
    memcpy(surface->combining, surface->alternate_combining,
           sizeof(surface->combining));
    memcpy(surface->combining_count, surface->alternate_combining_count,
           sizeof(surface->combining_count));
    memcpy(surface->foreground, surface->alternate_foreground,
           sizeof(surface->foreground));
    memcpy(surface->background, surface->alternate_background,
           sizeof(surface->background));
    memcpy(surface->underline_color, surface->alternate_underline_color,
           sizeof(surface->underline_color));
    memcpy(surface->attributes, surface->alternate_attributes,
           sizeof(surface->attributes));
    session->terminal_alternate = 0;
    if (session->terminal_alternate_saved_cursor.valid) {
        BlitSavedCursor *saved = &session->terminal_alternate_saved_cursor;
        surface->cursor_x = saved->x;
        surface->cursor_y = saved->y;
        session->terminal_foreground = saved->foreground;
        session->terminal_background = saved->background;
        session->terminal_attributes = saved->attributes;
        session->terminal_foreground_set = saved->foreground_set;
        session->terminal_background_set = saved->background_set;
        session->terminal_underline_color = saved->underline_color;
        session->terminal_underline_color_set = saved->underline_color_set;
        session->terminal_charset_dec = saved->charset_dec;
        session->terminal_charset_g1_dec = saved->charset_g1_dec;
        session->terminal_charset_shift = saved->charset_shift;
        session->terminal_origin_mode = saved->origin_mode;
        session->terminal_autowrap = saved->autowrap;
        saved->valid = 0;
    } else {
        surface->cursor_x = session->terminal_saved_x;
        surface->cursor_y = session->terminal_saved_y;
    }
    if (surface->cursor_x >= surface->width)
        surface->cursor_x = surface->width - 1;
    if (surface->cursor_y >= surface->height)
        surface->cursor_y = surface->height - 1;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_clear_cell(BlitSurface *surface, int x, int y)
{
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height)
        return;
    surface->cells[y][x] = 0;
    surface->codepoints[y][x] = 0;
    memset(surface->combining[y][x], 0, sizeof(surface->combining[y][x]));
    surface->combining_count[y][x] = 0;
    surface->foreground[y][x] = 0;
    surface->background[y][x] = 0;
    surface->underline_color[y][x] = 0;
    surface->attributes[y][x] = 0;
    mark_surface_damage(surface, x, y);
}

static void terminal_clear_line(BlitSurface *surface, int y, int start)
{
    int x;

    for (x = start; x < surface->width; ++x)
        terminal_clear_cell(surface, x, y);
}

static void terminal_erase_cell(BlitSession *session, BlitSurface *surface,
                                int x, int y)
{
    terminal_clear_cell(surface, x, y);
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height)
        return;
    if (session->terminal_background_set)
        surface->background[y][x] = session->terminal_background + 1;
}

static void terminal_erase_line(BlitSession *session, BlitSurface *surface,
                                int y, int start)
{
    int x;

    for (x = start; x < surface->width; ++x)
        terminal_erase_cell(session, surface, x, y);
}

static void terminal_copy_cell(BlitSurface *surface, int destination,
                               int source, int y)
{
    surface->cells[y][destination] = surface->cells[y][source];
    surface->codepoints[y][destination] = surface->codepoints[y][source];
    memcpy(surface->combining[y][destination], surface->combining[y][source],
           sizeof(surface->combining[y][destination]));
    surface->combining_count[y][destination] =
        surface->combining_count[y][source];
    surface->foreground[y][destination] = surface->foreground[y][source];
    surface->background[y][destination] = surface->background[y][source];
    surface->underline_color[y][destination] =
        surface->underline_color[y][source];
    surface->attributes[y][destination] = surface->attributes[y][source];
}

static void terminal_scroll_up(BlitSurface *surface)
{
    int y;
    int top = surface->scroll_top;
    int bottom = surface->scroll_bottom;

    if (surface->height < 1 || top < 0 || bottom >= surface->height ||
        top >= bottom)
        return;
    for (y = top + 1; y <= bottom; ++y) {
        memcpy(surface->cells[y - 1], surface->cells[y],
               sizeof(surface->cells[y - 1]));
        memcpy(surface->codepoints[y - 1], surface->codepoints[y],
               sizeof(surface->codepoints[y - 1]));
        memcpy(surface->combining[y - 1], surface->combining[y],
               sizeof(surface->combining[y - 1]));
        memcpy(surface->combining_count[y - 1], surface->combining_count[y],
               sizeof(surface->combining_count[y - 1]));
        memcpy(surface->foreground[y - 1], surface->foreground[y],
               sizeof(surface->foreground[y - 1]));
        memcpy(surface->background[y - 1], surface->background[y],
               sizeof(surface->background[y - 1]));
        memcpy(surface->underline_color[y - 1], surface->underline_color[y],
               sizeof(surface->underline_color[y - 1]));
        memcpy(surface->attributes[y - 1], surface->attributes[y],
               sizeof(surface->attributes[y - 1]));
    }
    memset(surface->cells[bottom], 0, sizeof(surface->cells[bottom]));
    memset(surface->codepoints[bottom], 0,
           sizeof(surface->codepoints[bottom]));
    memset(surface->combining[bottom], 0,
           sizeof(surface->combining[bottom]));
    memset(surface->combining_count[bottom], 0,
           sizeof(surface->combining_count[bottom]));
    memset(surface->foreground[bottom], 0,
           sizeof(surface->foreground[bottom]));
    memset(surface->background[bottom], 0,
           sizeof(surface->background[bottom]));
    memset(surface->underline_color[bottom], 0,
           sizeof(surface->underline_color[bottom]));
    memset(surface->attributes[bottom], 0,
           sizeof(surface->attributes[bottom]));
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = top;
    surface->damage_width = surface->width;
    surface->damage_height = bottom - top + 1;
    surface->cursor_y = bottom;
}

static void terminal_copy_row(BlitSurface *surface, int destination, int source)
{
    memcpy(surface->cells[destination], surface->cells[source],
           sizeof(surface->cells[destination]));
    memcpy(surface->codepoints[destination], surface->codepoints[source],
           sizeof(surface->codepoints[destination]));
    memcpy(surface->combining[destination], surface->combining[source],
           sizeof(surface->combining[destination]));
    memcpy(surface->combining_count[destination],
           surface->combining_count[source],
           sizeof(surface->combining_count[destination]));
    memcpy(surface->foreground[destination], surface->foreground[source],
           sizeof(surface->foreground[destination]));
    memcpy(surface->background[destination], surface->background[source],
           sizeof(surface->background[destination]));
    memcpy(surface->underline_color[destination],
           surface->underline_color[source],
           sizeof(surface->underline_color[destination]));
    memcpy(surface->attributes[destination], surface->attributes[source],
           sizeof(surface->attributes[destination]));
}

static void terminal_blank_row(BlitSurface *surface, int row)
{
    memset(surface->cells[row], 0, sizeof(surface->cells[row]));
    memset(surface->codepoints[row], 0, sizeof(surface->codepoints[row]));
    memset(surface->combining[row], 0, sizeof(surface->combining[row]));
    memset(surface->combining_count[row], 0,
           sizeof(surface->combining_count[row]));
    memset(surface->foreground[row], 0, sizeof(surface->foreground[row]));
    memset(surface->background[row], 0, sizeof(surface->background[row]));
    memset(surface->underline_color[row], 0,
           sizeof(surface->underline_color[row]));
    memset(surface->attributes[row], 0, sizeof(surface->attributes[row]));
}

static void terminal_index(BlitSurface *surface)
{
    if (surface->cursor_y < surface->scroll_bottom)
        ++surface->cursor_y;
    else
        terminal_scroll_up(surface);
}

static void terminal_reverse_index(BlitSurface *surface)
{
    int y;
    int top = surface->scroll_top;
    int bottom = surface->scroll_bottom;
    if (surface->cursor_y > top) {
        --surface->cursor_y;
        return;
    }
    if (top >= bottom) return;
    for (y = bottom; y > top; --y)
        terminal_copy_row(surface, y, y - 1);
    terminal_blank_row(surface, top);
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = top;
    surface->damage_width = surface->width;
    surface->damage_height = bottom - top + 1;
}

static void terminal_reset_tabs(BlitSurface *surface)
{
    int x;
    memset(surface->tab_stops, 0, sizeof(surface->tab_stops));
    for (x = 0; x < BLIT_MAX_CELL_COLS; x += 8)
        surface->tab_stops[x] = 1;
}

static int terminal_next_tab(const BlitSurface *surface, int x)
{
    ++x;
    while (x < surface->width && !surface->tab_stops[x]) ++x;
    return x < surface->width ? x : surface->width - 1;
}

static int terminal_previous_tab(const BlitSurface *surface, int x)
{
    if (x >= surface->width) x = surface->width - 1;
    --x;
    while (x > 0 && !surface->tab_stops[x]) --x;
    return x > 0 || surface->tab_stops[0] ? x : 0;
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

static uint32_t terminal_ansi_palette(int index)
{
    static const uint32_t colors[8] = {
        0x000000U, 0xcd0000U, 0x00cd00U, 0xcdcd00U,
        0x0000eeU, 0xcd00cdU, 0x00cdcdU, 0xe5e5e5U
    };

    if (index < 0 || index > 7)
        return 0;
    return colors[index];
}

static uint32_t terminal_dec_special(uint32_t codepoint)
{
    switch (codepoint) {
    case '`': return 0x25c6; /* diamond */
    case 'a': return 0x2592; /* checkerboard */
    case 'f': return 0x00b0; /* degree */
    case 'g': return 0x00b1; /* plus/minus */
    case 'j': return 0x2518; /* lower right corner */
    case 'k': return 0x2510; /* upper right corner */
    case 'l': return 0x250c; /* upper left corner */
    case 'm': return 0x2514; /* lower left corner */
    case 'n': return 0x253c; /* crossing lines */
    case 'q': return 0x2500; /* horizontal line */
    case 't': return 0x251c; /* left tee */
    case 'u': return 0x2524; /* right tee */
    case 'v': return 0x2534; /* bottom tee */
    case 'w': return 0x252c; /* top tee */
    case 'x': return 0x2502; /* vertical line */
    case 'y': return 0x2264; /* less-than-or-equal */
    case 'z': return 0x2265; /* greater-than-or-equal */
    case '{': return 0x03c0; /* pi */
    case '|': return 0x2260; /* not equal */
    case '}': return 0x00a3; /* pound */
    case '~': return 0x00b7; /* centered dot */
    default: return codepoint;
    }
}

static void terminal_write_reply(BlitSession *session, const char *reply)
{
    if (session != NULL && session->pty_fd >= 0 && reply != NULL)
        write(session->pty_fd, reply, strlen(reply));
}

static BlitSavedCursor *terminal_saved_cursor(BlitSession *session)
{
    return &session->terminal_saved_cursor[session->terminal_alternate ? 1 : 0];
}

static void terminal_store_cursor(BlitSavedCursor *saved,
                                  BlitSession *session, BlitSurface *surface)
{
    saved->valid = 1;
    saved->x = surface->cursor_x;
    saved->y = surface->cursor_y;
    saved->foreground = session->terminal_foreground;
    saved->background = session->terminal_background;
    saved->attributes = session->terminal_attributes;
    saved->foreground_set = session->terminal_foreground_set;
    saved->background_set = session->terminal_background_set;
    saved->underline_color = session->terminal_underline_color;
    saved->underline_color_set = session->terminal_underline_color_set;
    saved->charset_dec = session->terminal_charset_dec;
    saved->charset_g1_dec = session->terminal_charset_g1_dec;
    saved->charset_shift = session->terminal_charset_shift;
    saved->origin_mode = session->terminal_origin_mode;
    saved->autowrap = session->terminal_autowrap;
}

static void terminal_save_cursor(BlitSession *session, BlitSurface *surface)
{
    terminal_store_cursor(terminal_saved_cursor(session), session, surface);
}

static void terminal_restore_cursor(BlitSession *session, BlitSurface *surface)
{
    BlitSavedCursor *saved = terminal_saved_cursor(session);

    if (!saved->valid) {
        surface->cursor_x = 0;
        surface->cursor_y = 0;
        session->terminal_foreground = 0;
        session->terminal_background = 0;
        session->terminal_attributes = 0;
        session->terminal_foreground_set = 0;
        session->terminal_background_set = 0;
        session->terminal_underline_color = 0;
        session->terminal_underline_color_set = 0;
        session->terminal_charset_dec = 0;
        session->terminal_charset_g1_dec = 0;
        session->terminal_charset_shift = 0;
        session->terminal_origin_mode = 0;
        session->terminal_autowrap = 1;
    } else {
        surface->cursor_x = saved->x;
        surface->cursor_y = saved->y;
        session->terminal_foreground = saved->foreground;
        session->terminal_background = saved->background;
        session->terminal_attributes = saved->attributes;
        session->terminal_foreground_set = saved->foreground_set;
        session->terminal_background_set = saved->background_set;
        session->terminal_underline_color = saved->underline_color;
        session->terminal_underline_color_set = saved->underline_color_set;
        session->terminal_charset_dec = saved->charset_dec;
        session->terminal_charset_g1_dec = saved->charset_g1_dec;
        session->terminal_charset_shift = saved->charset_shift;
        session->terminal_origin_mode = saved->origin_mode;
        session->terminal_autowrap = saved->autowrap;
    }
    if (surface->cursor_x < 0) surface->cursor_x = 0;
    if (surface->cursor_y < 0) surface->cursor_y = 0;
    if (surface->cursor_x >= surface->width)
        surface->cursor_x = surface->width - 1;
    if (surface->cursor_y >= surface->height)
        surface->cursor_y = surface->height - 1;
    mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
}

static void terminal_apply_osc(BlitSession *session)
{
    char *separator;
    int command;

    if (session->terminal_string_type != ']')
        return;
    session->terminal_string[session->terminal_string_length] = '\0';
    separator = strchr(session->terminal_string, ';');
    if (separator == NULL)
        return;
    command = atoi(session->terminal_string);
    if (command == 0 || command == 1 || command == 2) {
        snprintf(session->terminal_title, sizeof(session->terminal_title),
                 "%s", separator + 1);
    }
}

static void terminal_apply_string(BlitSession *session, BlitSurface *surface)
{
    const char *payload = session->terminal_string;
    char reply[512];
    int used;

    session->terminal_string[session->terminal_string_length] = '\0';
    if (session->terminal_string_type != 'P')
        return;
    if (strncmp(payload, "$q", 2) == 0) {
        const char *query = payload + 2;
        if (strcmp(query, "r") == 0) {
            snprintf(reply, sizeof(reply), "\033P1$r%d;%dr\033\\",
                     surface->scroll_top + 1, surface->scroll_bottom + 1);
            terminal_write_reply(session, reply);
        } else if (strcmp(query, " q") == 0) {
            snprintf(reply, sizeof(reply), "\033P1$r%d q\033\\",
                     surface->cursor_style == 3 || surface->cursor_style == 4 ?
                     4 : surface->cursor_style == 5 || surface->cursor_style == 6 ?
                     6 : 2);
            terminal_write_reply(session, reply);
        } else if (strcmp(query, "m") == 0) {
            used = snprintf(reply, sizeof(reply), "\033P1$r0");
            if (session->terminal_attributes & BLIT_ATTR_BOLD)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";1");
            if (session->terminal_attributes & BLIT_ATTR_DIM)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";2");
            if (session->terminal_attributes & BLIT_ATTR_ITALIC)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";3");
            if (session->terminal_attributes & BLIT_ATTR_UNDERLINE)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";4");
            if (session->terminal_attributes & BLIT_ATTR_BLINK)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";5");
            if (session->terminal_attributes & BLIT_ATTR_INVERSE)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";7");
            if (session->terminal_attributes & BLIT_ATTR_STRIKE)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";9");
            if (session->terminal_attributes & BLIT_ATTR_CONCEAL)
                used += snprintf(reply + used, sizeof(reply) - (size_t)used, ";8");
            snprintf(reply + used, sizeof(reply) - (size_t)used, "m\033\\");
            terminal_write_reply(session, reply);
        } else {
            terminal_write_reply(session, "\033P0$r\033\\");
        }
    } else if (strncmp(payload, "+q", 2) == 0) {
        snprintf(reply, sizeof(reply), "\033P0+r%s\033\\", payload + 2);
        terminal_write_reply(session, reply);
    }
}

static void terminal_put_codepoint(BlitSession *session, BlitSurface *surface,
                                   uint32_t codepoint, int cell_width);

static void terminal_apply_sgr(BlitSession *session, const char *sequence)
{
    int params[32];
    int count = 0;
    const char *cursor = sequence;
    int i;

    while (*cursor != '\0' && *cursor != 'm' && count < 32) {
        char *end;
        int value;
        if (*cursor == ';') {
            params[count++] = 0;
            ++cursor;
            continue;
        }
        value = (int)strtol(cursor, &end, 10);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        params[count++] = value;
        cursor = end;
        if (*cursor == ';')
            ++cursor;
    }
    if (count == 0)
        params[count++] = 0;
    for (i = 0; i < count; ++i) {
        int value = params[i];
        int bright = 0;
        if (value == 0) {
            session->terminal_foreground = 0;
            session->terminal_background = 0;
            session->terminal_underline_color = 0;
            session->terminal_attributes = 0;
            session->terminal_foreground_set = 0;
            session->terminal_background_set = 0;
            session->terminal_underline_color_set = 0;
        } else if (value == 1) {
            session->terminal_attributes |= BLIT_ATTR_BOLD;
        } else if (value == 2) {
            session->terminal_attributes |= BLIT_ATTR_DIM;
        } else if (value == 3) {
            session->terminal_attributes |= BLIT_ATTR_ITALIC;
        } else if (value == 4) {
            session->terminal_attributes |= BLIT_ATTR_UNDERLINE;
            if (strchr(sequence, ':') != NULL && i + 1 < count) {
                if (params[i + 1] == 0) {
                    session->terminal_attributes &=
                        (uint16_t)~BLIT_ATTR_UNDERLINE;
                    ++i;
                } else if (params[i + 1] == 3) {
                    /* Curly underline is represented by the canonical
                       underline bit until a richer glyph decoration exists. */
                    ++i;
                }
            }
        } else if (value == 5 || value == 6) {
            session->terminal_attributes |= BLIT_ATTR_BLINK;
        } else if (value == 7) {
            session->terminal_attributes |= BLIT_ATTR_INVERSE;
        } else if (value == 22) {
            session->terminal_attributes &=
                (uint16_t)~(BLIT_ATTR_BOLD | BLIT_ATTR_DIM);
        } else if (value == 23) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_ITALIC;
        } else if (value == 24) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_UNDERLINE;
        } else if (value == 25) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_BLINK;
        } else if (value == 27) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_INVERSE;
        } else if (value == 8) {
            session->terminal_attributes |= BLIT_ATTR_CONCEAL;
        } else if (value == 28) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_CONCEAL;
        } else if (value == 9) {
            session->terminal_attributes |= BLIT_ATTR_STRIKE;
        } else if (value == 29) {
            session->terminal_attributes &= (uint16_t)~BLIT_ATTR_STRIKE;
        } else if (value == 39) {
            session->terminal_foreground_set = 0;
        } else if (value == 49) {
            session->terminal_background_set = 0;
        } else if (value == 59) {
            session->terminal_underline_color_set = 0;
        } else if (value >= 30 && value <= 37) {
            session->terminal_foreground = terminal_ansi_palette(value - 30);
            session->terminal_foreground_set = 1;
        } else if (value >= 40 && value <= 47) {
            session->terminal_background = terminal_ansi_palette(value - 40);
            session->terminal_background_set = 1;
        } else if (value >= 90 && value <= 97) {
            bright = value - 90;
            session->terminal_foreground = terminal_ansi_palette(bright);
            session->terminal_foreground =
                ((session->terminal_foreground & 0x7f7f7fU) | 0x303030U);
            session->terminal_foreground_set = 1;
        } else if (value >= 100 && value <= 107) {
            bright = value - 100;
            session->terminal_background = terminal_ansi_palette(bright);
            session->terminal_background =
                ((session->terminal_background & 0x7f7f7fU) | 0x303030U);
            session->terminal_background_set = 1;
        } else if ((value == 38 || value == 48 || value == 58) &&
                   i + 1 < count) {
            int mode = params[++i];
            uint32_t color = 0;
            int valid = 0;
            if (mode == 5 && i + 1 < count) {
                int index = params[++i];
                if (index < 16) {
                    int base = index % 8;
                    color = terminal_ansi_palette(base);
                    if (index >= 8)
                        color = (color & 0x7f7f7fU) | 0x303030U;
                    valid = 1;
                } else if (index < 232) {
                    int cube = index - 16;
                    int r = cube / 36;
                    int g = (cube / 6) % 6;
                    int b = cube % 6;
                    color = (uint32_t)((r ? 55 + r * 40 : 0) << 16) |
                        (uint32_t)((g ? 55 + g * 40 : 0) << 8) |
                        (uint32_t)(b ? 55 + b * 40 : 0);
                    valid = 1;
                } else if (index < 256) {
                    int shade = 8 + (index - 232) * 10;
                    color = (uint32_t)(shade << 16) |
                        (uint32_t)(shade << 8) | (uint32_t)shade;
                    valid = 1;
                }
            } else if (mode == 2 && i + 3 < count) {
                int r = params[++i];
                int g = params[++i];
                int b = params[++i];
                if (r >= 0 && r <= 255 && g >= 0 && g <= 255 &&
                    b >= 0 && b <= 255) {
                    color = (uint32_t)(r << 16) | (uint32_t)(g << 8) |
                        (uint32_t)b;
                    valid = 1;
                }
            }
            if (valid) {
                if (value == 38) {
                    session->terminal_foreground = color;
                    session->terminal_foreground_set = 1;
                } else if (value == 48) {
                    session->terminal_background = color;
                    session->terminal_background_set = 1;
                } else {
                    session->terminal_underline_color = color;
                    session->terminal_underline_color_set = 1;
                }
            }
        }
    }
}

static int terminal_csi_has_mode(const char *sequence, int wanted)
{
    const char *cursor = sequence;
    char *end;
    int value;

    if (*cursor == '?')
        ++cursor;
    while (*cursor != '\0' && *cursor != 'h' && *cursor != 'l') {
        if (*cursor == ';') {
            ++cursor;
            continue;
        }
        value = (int)strtol(cursor, &end, 10);
        if (end == cursor) {
            ++cursor;
            continue;
        }
        if (value == wanted)
            return 1;
        cursor = end;
    }
    return 0;
}

static void terminal_apply_csi(BlitSession *session, BlitSurface *surface,
                               const char *sequence)
{
    const char *cursor = sequence;
    int private_mode = 0;
    int first = 0;
    int second = 0;
    int has_first = 0;
    int has_second = 0;
    char prefix = 0;
    char command;

    if (*cursor == '?' || *cursor == '>' || *cursor == '<') {
        prefix = *cursor;
        private_mode = prefix == '?';
        ++cursor;
    }
    /* The final byte is the CSI command.  SGR is allowed to carry more
       than two parameters (Hibr uses 0;38;2;r;g;b for truecolor), so do not
       infer the command from only the first two parameters. */
    command = sequence[strlen(sequence) - 1];

    while (*cursor >= '0' && *cursor <= '9') {
        has_first = terminal_number(&cursor, &first);
        break;
    }
    if (*cursor == ';') {
        ++cursor;
        has_second = terminal_number(&cursor, &second);
    }
    if ((prefix == '>' || prefix == '<') && command == 'u') {
        if (prefix == '>') {
            session->terminal_cursor_saved = 1;
            session->terminal_cursor_saved_x = surface->cursor_x;
            session->terminal_cursor_saved_y = surface->cursor_y;
        } else if (session->terminal_cursor_saved) {
            surface->cursor_x = session->terminal_cursor_saved_x;
            surface->cursor_y = session->terminal_cursor_saved_y;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        }
        return;
    }
    if (prefix == '>' && command == 'c' && (!has_first || first == 0)) {
        terminal_write_reply(session, "\033[>1;10;0c");
        return;
    }
    if (prefix == '>' && command == 'q') {
        terminal_write_reply(session, "\033P>|blit-term 0.1\033\\");
        return;
    }
    /* CSI > ... m is the modifyOtherKeys family, not SGR.  It changes
       keyboard encoding and must never alter the cell attributes. */
    if (prefix == '>')
        return;
    if (private_mode && (command == 'h' || command == 'l')) {
        int enabled = command == 'h';
        if (terminal_csi_has_mode(sequence, 47) ||
            terminal_csi_has_mode(sequence, 1047) ||
            terminal_csi_has_mode(sequence, 1049)) {
            if (enabled)
                terminal_enter_alternate(session, surface,
                                         terminal_csi_has_mode(sequence, 1049));
            else
                terminal_leave_alternate(session, surface);
        }
        if (terminal_csi_has_mode(sequence, 1048)) {
            if (enabled) {
                session->terminal_dec_cursor_saved = 1;
                session->terminal_dec_cursor_saved_x = surface->cursor_x;
                session->terminal_dec_cursor_saved_y = surface->cursor_y;
            } else if (session->terminal_dec_cursor_saved) {
                surface->cursor_x = session->terminal_dec_cursor_saved_x;
                surface->cursor_y = session->terminal_dec_cursor_saved_y;
                if (surface->cursor_x >= surface->width)
                    surface->cursor_x = surface->width - 1;
                if (surface->cursor_y >= surface->height)
                    surface->cursor_y = surface->height - 1;
                mark_surface_damage(surface, surface->cursor_x,
                                    surface->cursor_y);
            }
        }
        if (terminal_csi_has_mode(sequence, 1))
            session->terminal_application_cursor = enabled;
        if (terminal_csi_has_mode(sequence, 6)) {
            session->terminal_origin_mode = enabled;
            surface->cursor_x = 0;
            surface->cursor_y = enabled ? surface->scroll_top : 0;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        }
        if (terminal_csi_has_mode(sequence, 66))
            session->terminal_application_keypad = enabled;
        if (terminal_csi_has_mode(sequence, 1004))
            session->terminal_focus_reporting = enabled;
        if (terminal_csi_has_mode(sequence, 2004))
            session->terminal_bracketed_paste = enabled;
        if (terminal_csi_has_mode(sequence, 1000) ||
            terminal_csi_has_mode(sequence, 1002) ||
            terminal_csi_has_mode(sequence, 1003)) {
            if (enabled) {
                if (terminal_csi_has_mode(sequence, 1003))
                    session->terminal_mouse_mode = 1003;
                else if (terminal_csi_has_mode(sequence, 1002))
                    session->terminal_mouse_mode = 1002;
                else
                    session->terminal_mouse_mode = 1000;
            } else {
                session->terminal_mouse_mode = 0;
            }
        }
        if (terminal_csi_has_mode(sequence, 1006))
            session->terminal_mouse_sgr = enabled;
        if (terminal_csi_has_mode(sequence, 2026))
            session->terminal_synchronized = enabled;
        if (terminal_csi_has_mode(sequence, 25)) {
            surface->cursor_visible = enabled;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        }
        if (terminal_csi_has_mode(sequence, 7))
            session->terminal_autowrap = enabled;
        return;
    }
    if (private_mode && command == 'p' && has_first &&
        strstr(sequence, "$p") != NULL) {
        int state = 0;
        switch (first) {
        case 1: state = session->terminal_application_cursor ? 1 : 2; break;
        case 6: state = session->terminal_origin_mode ? 1 : 2; break;
        case 7: state = session->terminal_autowrap ? 1 : 2; break;
        case 25: state = surface->cursor_visible ? 1 : 2; break;
        case 66: state = session->terminal_application_keypad ? 1 : 2; break;
        case 1000: case 1002: case 1003:
            state = session->terminal_mouse_mode == first ? 1 : 2; break;
        case 1004: state = session->terminal_focus_reporting ? 1 : 2; break;
        case 1006: state = session->terminal_mouse_sgr ? 1 : 2; break;
        case 2004: state = session->terminal_bracketed_paste ? 1 : 2; break;
        default: state = 0; break;
        }
        {
            char reply[64];
            snprintf(reply, sizeof(reply), "\033[?%d;%d$y", first, state);
            terminal_write_reply(session, reply);
        }
        return;
    }
    if (private_mode && command == 'u') {
        /* Kitty's cursor query/restore form.  Hibr treats the query as the
           restore operation for the xterm-compatible terminal contract. */
        if (terminal_saved_cursor(session)->valid)
            terminal_restore_cursor(session, surface);
        return;
    }
    if (!private_mode && command == 'p' && has_first &&
        strstr(sequence, "$p") != NULL) {
        int state = 0;
        if (first == 4) state = session->terminal_insert_mode ? 1 : 2;
        else if (first == 20) state = session->terminal_newline_mode ? 1 : 2;
        {
            char reply[64];
            snprintf(reply, sizeof(reply), "\033[%d;%d$y", first, state);
            terminal_write_reply(session, reply);
        }
        return;
    }
    if (!private_mode && (command == 'h' || command == 'l') &&
        terminal_csi_has_mode(sequence, 20)) {
        session->terminal_newline_mode = command == 'h';
    }
    if (!private_mode && (command == 'h' || command == 'l') &&
        terminal_csi_has_mode(sequence, 4)) {
        session->terminal_insert_mode = command == 'h';
    } else if (!private_mode && command == 'p' &&
               strstr(sequence, "!p") != NULL) {
        session->terminal_foreground = 0;
        session->terminal_background = 0;
        session->terminal_attributes = 0;
        session->terminal_foreground_set = 0;
        session->terminal_background_set = 0;
        session->terminal_underline_color = 0;
        session->terminal_underline_color_set = 0;
        session->terminal_application_cursor = 0;
        session->terminal_application_keypad = 0;
        session->terminal_origin_mode = 0;
        session->terminal_autowrap = 1;
        session->terminal_newline_mode = 0;
        session->terminal_insert_mode = 0;
        session->terminal_focus_reporting = 0;
        session->terminal_bracketed_paste = 0;
        session->terminal_mouse_mode = 0;
        session->terminal_mouse_sgr = 0;
        session->terminal_synchronized = 0;
        surface->cursor_style = 1;
        surface->cursor_visible = 1;
        surface->scroll_top = 0;
        surface->scroll_bottom = surface->height - 1;
        terminal_reset_tabs(surface);
        mark_surface_damage(surface, 0, 0);
    } else if (!private_mode && command == 'b') {
        int count = has_first && first > 0 ? first : 1;
        if (count > BLIT_MAX_CELL_COLS) count = BLIT_MAX_CELL_COLS;
        while (count-- > 0 && session->terminal_last_codepoint != 0)
            terminal_put_codepoint(session, surface,
                                   session->terminal_last_codepoint,
                                   session->terminal_last_width > 0 ?
                                   session->terminal_last_width : 1);
    } else if (!private_mode && command == 'X') {
        int count = has_first && first > 0 ? first : 1;
        int x;
        if (count > surface->width - surface->cursor_x)
            count = surface->width - surface->cursor_x;
        for (x = 0; x < count; ++x)
            terminal_erase_cell(session, surface, surface->cursor_x + x,
                                surface->cursor_y);
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (!private_mode && command == 'c') {
        if (!has_first || first == 0)
            terminal_write_reply(session, "\033[?62;1;2;6;7;8;9c");
    } else if (!private_mode && command == 'n' && has_first && first == 5) {
        terminal_write_reply(session, "\033[0n");
    } else if (!private_mode && command == 'n' && has_first && first == 6) {
        char reply[64];
        snprintf(reply, sizeof(reply), "\033[%d;%dR",
                 surface->cursor_y + 1, surface->cursor_x + 1);
        terminal_write_reply(session, reply);
    } else if (!private_mode && command == 't' && has_first && first == 18) {
        char reply[64];
        snprintf(reply, sizeof(reply), "\033[8;%d;%dt",
                 surface->height, surface->width);
        terminal_write_reply(session, reply);
    } else if (!private_mode && command == 's') {
        terminal_save_cursor(session, surface);
    } else if (!private_mode && command == 'u') {
        terminal_restore_cursor(session, surface);
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && (first == 1047 || first == 1049)) {
        if (command == 'h')
            terminal_enter_alternate(session, surface, first == 1049);
        else
            terminal_leave_alternate(session, surface);
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 1048) {
        if (command == 'h') {
            session->terminal_dec_cursor_saved = 1;
            session->terminal_dec_cursor_saved_x = surface->cursor_x;
            session->terminal_dec_cursor_saved_y = surface->cursor_y;
        } else if (session->terminal_dec_cursor_saved) {
            surface->cursor_x = session->terminal_dec_cursor_saved_x;
            surface->cursor_y = session->terminal_dec_cursor_saved_y;
            if (surface->cursor_x >= surface->width)
                surface->cursor_x = surface->width - 1;
            if (surface->cursor_y >= surface->height)
                surface->cursor_y = surface->height - 1;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        }
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 1) {
        session->terminal_application_cursor = command == 'h';
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 6) {
        session->terminal_origin_mode = command == 'h';
        surface->cursor_x = 0;
        surface->cursor_y = session->terminal_origin_mode ?
            surface->scroll_top : 0;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 66) {
        session->terminal_application_keypad = command == 'h';
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 1004) {
        session->terminal_focus_reporting = command == 'h';
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 2004) {
        session->terminal_bracketed_paste = command == 'h';
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && (first == 1000 || first == 1002 || first == 1003)) {
        session->terminal_mouse_mode = command == 'h' ? first : 0;
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 1006) {
        session->terminal_mouse_sgr = command == 'h';
    } else if (private_mode && (command == 'h' || command == 'l') &&
               has_first && first == 25) {
        surface->cursor_visible = command == 'h';
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (!private_mode && command == 'r') {
        int top = has_first && first > 0 ? first - 1 : 0;
        int bottom = has_second && second > 0 ? second - 1 :
            surface->height - 1;
        if (top >= 0 && bottom < surface->height && top < bottom) {
            surface->scroll_top = top;
            surface->scroll_bottom = bottom;
            surface->cursor_x = 0;
            surface->cursor_y = top;
            mark_surface_damage(surface, 0, top);
        }
    } else if (command == 'm') {
        terminal_apply_sgr(session, sequence);
    } else if (!private_mode && command == 'q') {
        int style = has_first ? first : 0;
        if (style >= 0 && style <= 6) {
            /* DECSCUSR: 0 is the terminal's default blinking block.  Blit
               records the VT style even when the backend does not animate
               the blinking variants. */
            surface->cursor_style = style == 0 ? 1 : style;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
        }
    } else if (command == 'H' || command == 'f') {
        surface->cursor_y = has_first && first > 0 ? first - 1 : 0;
        if (session->terminal_origin_mode)
            surface->cursor_y += surface->scroll_top;
        surface->cursor_x = has_second && second > 0 ? second - 1 : 0;
        if (session->terminal_origin_mode) {
            if (surface->cursor_y < surface->scroll_top)
                surface->cursor_y = surface->scroll_top;
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y >= surface->height)
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
        if (session->terminal_origin_mode) {
            if (surface->cursor_y < surface->scroll_top)
                surface->cursor_y = surface->scroll_top;
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y < 0) {
            surface->cursor_y = 0;
        }
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        if (surface->cursor_y >= surface->height)
            surface->cursor_y = surface->height - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'a') {
        int amount = has_first && first > 0 ? first : 1;
        surface->cursor_x += amount;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'e') {
        int amount = has_first && first > 0 ? first : 1;
        surface->cursor_y += amount;
        if (session->terminal_origin_mode) {
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y >= surface->height) {
            surface->cursor_y = surface->height - 1;
        }
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == '@' || command == 'P') {
        int amount = has_first && first > 0 ? first : 1;
        int x;
        if (amount > surface->width)
            amount = surface->width;
        if (command == '@') {
            for (x = surface->width - 1; x >= surface->cursor_x + amount; --x) {
                int source = x - amount;
                surface->cells[surface->cursor_y][x] =
                    surface->cells[surface->cursor_y][source];
                surface->codepoints[surface->cursor_y][x] =
                    surface->codepoints[surface->cursor_y][source];
                memcpy(surface->combining[surface->cursor_y][x],
                       surface->combining[surface->cursor_y][source],
                       sizeof(surface->combining[surface->cursor_y][x]));
                surface->combining_count[surface->cursor_y][x] =
                    surface->combining_count[surface->cursor_y][source];
                surface->foreground[surface->cursor_y][x] =
                    surface->foreground[surface->cursor_y][source];
                surface->background[surface->cursor_y][x] =
                    surface->background[surface->cursor_y][source];
                surface->underline_color[surface->cursor_y][x] =
                    surface->underline_color[surface->cursor_y][source];
                surface->attributes[surface->cursor_y][x] =
                    surface->attributes[surface->cursor_y][source];
            }
            for (x = surface->cursor_x;
                 x < surface->cursor_x + amount && x < surface->width; ++x)
                terminal_clear_cell(surface, x, surface->cursor_y);
        } else {
            for (x = surface->cursor_x; x < surface->width - amount; ++x) {
                int source = x + amount;
                surface->cells[surface->cursor_y][x] =
                    surface->cells[surface->cursor_y][source];
                surface->codepoints[surface->cursor_y][x] =
                    surface->codepoints[surface->cursor_y][source];
                memcpy(surface->combining[surface->cursor_y][x],
                       surface->combining[surface->cursor_y][source],
                       sizeof(surface->combining[surface->cursor_y][x]));
                surface->combining_count[surface->cursor_y][x] =
                    surface->combining_count[surface->cursor_y][source];
                surface->foreground[surface->cursor_y][x] =
                    surface->foreground[surface->cursor_y][source];
                surface->background[surface->cursor_y][x] =
                    surface->background[surface->cursor_y][source];
                surface->underline_color[surface->cursor_y][x] =
                    surface->underline_color[surface->cursor_y][source];
                surface->attributes[surface->cursor_y][x] =
                    surface->attributes[surface->cursor_y][source];
            }
            for (x = surface->width - amount; x < surface->width; ++x)
                terminal_clear_cell(surface, x, surface->cursor_y);
        }
        mark_surface_damage(surface, 0, surface->cursor_y);
        if (surface->damage_width < surface->width)
            surface->damage_width = surface->width;
    } else if (command == 'Z') {
        int count = has_first && first > 0 ? first : 1;
        while (count-- > 0 && surface->cursor_x > 0)
            surface->cursor_x = terminal_previous_tab(surface,
                                                      surface->cursor_x);
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'I') {
        int count = has_first && first > 0 ? first : 1;
        while (count-- > 0)
            surface->cursor_x = terminal_next_tab(surface, surface->cursor_x);
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'L' || command == 'M') {
        int count = has_first && first > 0 ? first : 1;
        int top = surface->cursor_y;
        int bottom = surface->scroll_bottom;
        int y;
        if (count > bottom - top + 1)
            count = bottom - top + 1;
        if (command == 'L') {
            for (y = bottom; y >= top + count; --y)
                terminal_copy_row(surface, y, y - count);
            for (y = top; y < top + count; ++y)
                terminal_blank_row(surface, y);
        } else {
            for (y = top; y <= bottom - count; ++y)
                terminal_copy_row(surface, y, y + count);
            for (y = bottom - count + 1; y <= bottom; ++y)
                terminal_blank_row(surface, y);
        }
        surface->damage_valid = 1;
        surface->damage_x = 0;
        surface->damage_y = top;
        surface->damage_width = surface->width;
        surface->damage_height = bottom - top + 1;
    } else if (command == 'S' || command == 'T') {
        int count = has_first && first > 0 ? first : 1;
        int top = surface->scroll_top;
        int bottom = surface->scroll_bottom;
        int y;
        if (count > bottom - top + 1)
            count = bottom - top + 1;
        if (command == 'S') {
            for (y = top; y <= bottom - count; ++y)
                terminal_copy_row(surface, y, y + count);
            for (y = bottom - count + 1; y <= bottom; ++y)
                terminal_blank_row(surface, y);
        } else {
            for (y = bottom; y >= top + count; --y)
                terminal_copy_row(surface, y, y - count);
            for (y = top; y < top + count; ++y)
                terminal_blank_row(surface, y);
        }
        surface->damage_valid = 1;
        surface->damage_x = 0;
        surface->damage_y = top;
        surface->damage_width = surface->width;
        surface->damage_height = bottom - top + 1;
    } else if (command == 'E' || command == 'F') {
        int count = has_first && first > 0 ? first : 1;
        surface->cursor_y += command == 'E' ? count : -count;
        if (session->terminal_origin_mode) {
            if (surface->cursor_y < surface->scroll_top)
                surface->cursor_y = surface->scroll_top;
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else {
            if (surface->cursor_y < 0) surface->cursor_y = 0;
            if (surface->cursor_y >= surface->height)
                surface->cursor_y = surface->height - 1;
        }
        surface->cursor_x = 0;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'G' || command == '`') {
        surface->cursor_x = has_first && first > 0 ? first - 1 : 0;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'd') {
        surface->cursor_y = has_first && first > 0 ? first - 1 : 0;
        if (session->terminal_origin_mode) {
            surface->cursor_y += surface->scroll_top;
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y >= surface->height)
            surface->cursor_y = surface->height - 1;
        mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'g') {
        int mode = has_first ? first : 0;
        if (mode == 0 && surface->cursor_x >= 0 &&
            surface->cursor_x < surface->width)
            surface->tab_stops[surface->cursor_x] = 0;
        else if (mode == 3)
            memset(surface->tab_stops, 0, sizeof(surface->tab_stops));
    } else if (command == 'J') {
        int mode = has_first ? first : 0;
        int y;
        int start;
        if (mode == 0) {
            terminal_erase_line(session, surface, surface->cursor_y,
                                surface->cursor_x);
            for (y = surface->cursor_y + 1; y < surface->height; ++y)
                terminal_erase_line(session, surface, y, 0);
        } else if (mode == 1) {
            for (y = 0; y < surface->cursor_y; ++y)
                terminal_erase_line(session, surface, y, 0);
            for (start = 0; start <= surface->cursor_x; ++start)
                terminal_erase_cell(session, surface, start, surface->cursor_y);
        } else if (mode == 2 || mode == 3) {
            for (y = 0; y < surface->height; ++y)
                terminal_erase_line(session, surface, y, 0);
        }
    } else if (command == 'K') {
        int mode = has_first ? first : 0;
        int x;
        if (mode == 0)
            terminal_erase_line(session, surface, surface->cursor_y,
                                surface->cursor_x);
        else if (mode == 1) {
            for (x = 0; x <= surface->cursor_x; ++x)
                terminal_erase_cell(session, surface, x, surface->cursor_y);
        } else if (mode == 2)
            terminal_erase_line(session, surface, surface->cursor_y, 0);
    }
}

static void terminal_add_combining(BlitSurface *surface, uint32_t codepoint)
{
    int x = surface->cursor_x - 1;
    int y = surface->cursor_y;
    unsigned char count;
    if (x < 0) return;
    if (surface->attributes[y][x] & BLIT_ATTR_CONTINUATION) --x;
    if (x < 0) return;
    count = surface->combining_count[y][x];
    if (count >= BLIT_MAX_COMBINING) return;
    surface->combining[y][x][count] = codepoint;
    surface->combining_count[y][x] = count + 1;
    mark_surface_damage(surface, x, y);
}

static void terminal_put_codepoint(BlitSession *session, BlitSurface *surface,
                                   uint32_t codepoint, int cell_width)
{
    session->terminal_last_codepoint = codepoint;
    if (cell_width == 2 && surface->cursor_x >= surface->width - 1) {
        if (session->terminal_autowrap) {
            surface->cursor_x = 0;
            if (surface->cursor_y < surface->scroll_bottom)
                ++surface->cursor_y;
            else
                terminal_scroll_up(surface);
        } else {
            cell_width = 1;
        }
    }
    if (surface->cursor_x >= surface->width) {
        if (session->terminal_autowrap) {
            surface->cursor_x = 0;
            if (surface->cursor_y < surface->scroll_bottom)
                ++surface->cursor_y;
            else
                terminal_scroll_up(surface);
        } else {
            surface->cursor_x = surface->width - 1;
        }
    }
    session->terminal_last_width = cell_width;
    if (session->terminal_insert_mode) {
        int x;
        for (x = surface->width - 1; x >= surface->cursor_x + cell_width; --x)
            terminal_copy_cell(surface, x, x - cell_width,
                               surface->cursor_y);
        for (x = 0; x < cell_width && surface->cursor_x + x < surface->width;
             ++x)
            terminal_clear_cell(surface, surface->cursor_x + x,
                                surface->cursor_y);
    }
    terminal_clear_cell(surface, surface->cursor_x, surface->cursor_y);
    surface->codepoints[surface->cursor_y][surface->cursor_x] = codepoint;
    surface->cells[surface->cursor_y][surface->cursor_x] =
        codepoint < 0x80 ? (char)codepoint : '?';
    if (session->terminal_foreground_set)
        surface->foreground[surface->cursor_y][surface->cursor_x] =
            session->terminal_foreground + 1;
    if (session->terminal_background_set)
        surface->background[surface->cursor_y][surface->cursor_x] =
            session->terminal_background + 1;
    if (session->terminal_underline_color_set)
        surface->underline_color[surface->cursor_y][surface->cursor_x] =
            session->terminal_underline_color + 1;
    surface->attributes[surface->cursor_y][surface->cursor_x] =
        session->terminal_attributes | (cell_width == 2 ? BLIT_ATTR_WIDE : 0);
    if (cell_width == 2) {
        terminal_clear_cell(surface, surface->cursor_x + 1,
                            surface->cursor_y);
        surface->attributes[surface->cursor_y][surface->cursor_x + 1] =
            BLIT_ATTR_CONTINUATION;
    }
    mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
    surface->cursor_x += cell_width;
    if (!session->terminal_autowrap && surface->cursor_x >= surface->width)
        surface->cursor_x = surface->width - 1;
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
            } else if (byte == ']' || byte == 'P' || byte == 'X' ||
                       byte == '^' || byte == '_') {
                /* String controls must never leak their payload as glyphs. */
                session->terminal_escape = 3;
                session->terminal_string_type = byte;
                session->terminal_string_length = 0;
            } else if (byte == '(' || byte == ')' || byte == '*' ||
                       byte == '+') {
                /* Designate G0/G1; the active bank is selected by SO/SI. */
                session->terminal_escape = 5;
                session->terminal_charset_g0_pending =
                    byte == '(' || byte == '*';
            } else if (byte == '=') {
                session->terminal_application_keypad = 1;
                session->terminal_escape = 0;
            } else if (byte == '>') {
                session->terminal_application_keypad = 0;
                session->terminal_escape = 0;
            } else if (byte == '#') {
                session->terminal_escape = 6;
            } else if (byte == 'H') {
                if (surface->cursor_x >= 0 &&
                    surface->cursor_x < surface->width)
                    surface->tab_stops[surface->cursor_x] = 1;
                session->terminal_escape = 0;
            } else if (byte == '7') {
                terminal_save_cursor(session, surface);
                session->terminal_escape = 0;
            } else if (byte == '8') {
                terminal_restore_cursor(session, surface);
                session->terminal_escape = 0;
            } else if (byte == 'D') {
                terminal_index(surface);
                session->terminal_escape = 0;
            } else if (byte == 'M') {
                terminal_reverse_index(surface);
                session->terminal_escape = 0;
            } else if (byte == 'E') {
                surface->cursor_x = 0;
                terminal_index(surface);
                session->terminal_escape = 0;
            } else if (byte == 'c') {
                terminal_clear_active_screen(surface);
                surface->cursor_x = 0;
                surface->cursor_y = 0;
                surface->cursor_visible = 1;
                surface->cursor_style = 1;
                surface->scroll_top = 0;
                surface->scroll_bottom = surface->height - 1;
                terminal_reset_tabs(surface);
                session->terminal_charset_dec = 0;
                session->terminal_charset_g1_dec = 0;
                session->terminal_charset_shift = 0;
                session->terminal_origin_mode = 0;
                session->terminal_autowrap = 1;
                session->terminal_newline_mode = 0;
                session->terminal_insert_mode = 0;
                session->terminal_title[0] = '\0';
                session->terminal_application_cursor = 0;
                session->terminal_application_keypad = 0;
                session->terminal_focus_reporting = 0;
                session->terminal_bracketed_paste = 0;
                session->terminal_mouse_mode = 0;
                session->terminal_mouse_sgr = 0;
                session->terminal_synchronized = 0;
                session->terminal_foreground = 0;
                session->terminal_background = 0;
                session->terminal_underline_color = 0;
                session->terminal_underline_color_set = 0;
                session->terminal_attributes = 0;
                session->terminal_foreground_set = 0;
                session->terminal_background_set = 0;
                session->terminal_escape = 0;
            } else {
                session->terminal_escape = 0;
            }
            ++offset;
            continue;
        }
        if (session->terminal_escape == 3) {
            if (byte == 0x9c) {
                if (session->terminal_string_type == ']')
                    terminal_apply_osc(session);
                else
                    terminal_apply_string(session, surface);
                session->terminal_escape = 0;
            } else if (byte == '\a' && session->terminal_string_type == ']') {
                terminal_apply_osc(session);
                session->terminal_escape = 0;
            } else if (byte == 0x1b)
                session->terminal_escape = 4;
            else if (session->terminal_string_length <
                     (int)sizeof(session->terminal_string) - 1)
                session->terminal_string[
                    session->terminal_string_length++] = (char)byte;
            ++offset;
            continue;
        }
        if (session->terminal_escape == 4) {
            if (byte == '\\') {
                if (session->terminal_string_type == ']')
                    terminal_apply_osc(session);
                else
                    terminal_apply_string(session, surface);
                session->terminal_escape = 0;
            } else {
                session->terminal_escape = 3;
            }
            ++offset;
            continue;
        }
        if (session->terminal_escape == 5) {
            if (session->terminal_charset_g0_pending)
                session->terminal_charset_dec = byte == '0';
            else
                session->terminal_charset_g1_dec = byte == '0';
            session->terminal_charset_g0_pending = 0;
            session->terminal_escape = 0;
            ++offset;
            continue;
        }
        if (session->terminal_escape == 6) {
            if (byte == '8')
                terminal_alignment_test(surface);
            session->terminal_escape = 0;
            ++offset;
            continue;
        }
        if (session->terminal_escape == 2) {
            if (byte == 0x18 || byte == 0x1a) {
                /* CAN and SUB abandon an incomplete control sequence. */
                session->terminal_escape = 0;
                session->terminal_csi_length = 0;
                ++offset;
                continue;
            }
            if (byte == '\r' || byte == '\n' || byte == '\b' || byte == '\t') {
                /* ECMA-48 permits C0 controls to execute inside CSI without
                   terminating the sequence. */
                if (byte == '\r') {
                    mark_surface_damage(surface, surface->cursor_x,
                                        surface->cursor_y);
                    surface->cursor_x = 0;
                } else if (byte == '\n') {
                    mark_surface_damage(surface, surface->cursor_x,
                                        surface->cursor_y);
                    if (session->terminal_newline_mode)
                        surface->cursor_x = 0;
                    terminal_index(surface);
                } else if (byte == '\b') {
                    if (surface->cursor_x > 0) --surface->cursor_x;
                    mark_surface_damage(surface, surface->cursor_x,
                                        surface->cursor_y);
                } else {
                    surface->cursor_x = terminal_next_tab(surface,
                                                          surface->cursor_x);
                }
                ++offset;
                continue;
            }
            if ((byte == '?' || byte == '>' || byte == '!') &&
                session->terminal_csi_length == 0) {
                session->terminal_csi[session->terminal_csi_length++] =
                    (char)byte;
                session->terminal_csi[session->terminal_csi_length] = '\0';
            } else if (byte >= 0x40 && byte <= 0x7e) {
                if (session->terminal_csi_length <
                    (int)sizeof(session->terminal_csi) - 1)
                    session->terminal_csi[session->terminal_csi_length++] =
                        (char)byte;
                session->terminal_csi[session->terminal_csi_length] = '\0';
                terminal_apply_csi(session, surface, session->terminal_csi);
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
        } else if (byte == 0x9b) {
            session->terminal_escape = 2;
            session->terminal_csi_length = 0;
            ++offset;
        } else if (byte == 0x9d || byte == 0x90 || byte == 0x98 ||
                   byte == 0x9e || byte == 0x9f) {
            session->terminal_escape = 3;
            session->terminal_string_type = byte == 0x9d ? ']' : byte;
            session->terminal_string_length = 0;
            ++offset;
        } else if (byte == '\r') {
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            surface->cursor_x = 0;
            ++offset;
        } else if (byte == '\n') {
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            if (session->terminal_newline_mode)
                surface->cursor_x = 0;
            terminal_index(surface);
            ++offset;
        } else if (byte == '\b') {
            if (surface->cursor_x > 0)
                --surface->cursor_x;
            mark_surface_damage(surface, surface->cursor_x, surface->cursor_y);
            ++offset;
        } else if (byte == '\t') {
            surface->cursor_x = terminal_next_tab(surface, surface->cursor_x);
            ++offset;
        } else if (byte == 0x0e) {
            session->terminal_charset_shift = 1;
            ++offset;
        } else if (byte == 0x0f) {
            session->terminal_charset_shift = 0;
            ++offset;
        } else if (byte >= 0x20) {
            size_t consumed;
            uint32_t codepoint;
            if ((session->terminal_charset_shift ?
                 session->terminal_charset_g1_dec : session->terminal_charset_dec) &&
                byte < 0x80) {
                consumed = 1;
                codepoint = terminal_dec_special(byte);
            } else if (!utf8_decode(data + offset, length - offset,
                                    &consumed, &codepoint)) {
                consumed = 1;
                codepoint = '?';
            } else if (unicode_cell_width(codepoint) == 0) {
                terminal_add_combining(surface, codepoint);
                offset += consumed;
                continue;
            }
            terminal_put_codepoint(session, surface, codepoint,
                                   unicode_cell_width(codepoint));
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

static BlitInputRoute *find_input_route(BlitRuntime *runtime,
                                        const char *route_id)
{
    int i;

    for (i = 0; i < 16; ++i)
        if (runtime->input_routes[i].used &&
            strcmp(runtime->input_routes[i].id, route_id) == 0)
            return &runtime->input_routes[i];
    return NULL;
}

static BlitSession *input_route_target(BlitRuntime *runtime,
                                       const char *device_id)
{
    int i;

    for (i = 0; i < 16; ++i) {
        BlitInputRoute *route = &runtime->input_routes[i];
        BlitSession *session;
        if (!route->used || !route->enabled ||
            strcmp(route->device_id, device_id) != 0)
            continue;
        if (strncmp(route->target_session_id, "session-", 8) != 0)
            continue;
        session = find_session_id(runtime,
            strtoull(route->target_session_id + 8, NULL, 10));
        if (session != NULL && session->pty_fd >= 0)
            return session;
    }
    return NULL;
}

static void input_routes_json(const BlitRuntime *runtime, char *body,
                              size_t capacity)
{
    size_t used = 0;
    int i;
    int first = 1;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":%llu,\"routes\":[",
                             runtime->input_routes_revision);
    for (i = 0; i < 16 && used < capacity; ++i) {
        const BlitInputRoute *route = &runtime->input_routes[i];
        if (!route->used)
            continue;
        used += (size_t)snprintf(body + used, capacity - used,
            "%s{\"id\":\"%s\",\"device_id\":\"%s\","
            "\"target_session_id\":\"%s\",\"enabled\":%s}",
            first ? "" : ",",
            route->id, route->device_id, route->target_session_id,
            route->enabled ? "true" : "false");
        first = 0;
    }
    snprintf(body + used, capacity - used, "]}");
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

static BlitPane *find_pane_id(BlitRuntime *runtime, const char *pane_id)
{
    int i;

    if (pane_id == NULL || pane_id[0] == '\0')
        return NULL;
    for (i = 0; i < runtime->pane_count; ++i)
        if (runtime->panes[i].used && strcmp(runtime->panes[i].id, pane_id) == 0)
            return &runtime->panes[i];
    return NULL;
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

    if (!session->used ||
        (strcmp(session->state, "running") != 0 &&
         strcmp(session->state, "starting") != 0))
        return;
    result = waitpid(session->process_id, &status, WNOHANG);
    if (result == session->process_id) {
        if (WIFEXITED(status)) {
            session->exit_code = WEXITSTATUS(status);
            snprintf(session->state, sizeof(session->state),
                     session->exit_code == 0 ? "stopped" : "failed");
        } else if (WIFSIGNALED(status)) {
            session->exit_code = 128 + WTERMSIG(status);
            snprintf(session->state, sizeof(session->state), "failed");
        } else {
            session->exit_code = -1;
            snprintf(session->state, sizeof(session->state), "failed");
        }
        if (session->pty_fd >= 0) {
            close(session->pty_fd);
            session->pty_fd = -1;
        }
    } else if (result < 0 && errno != EINTR) {
        session->exit_code = -1;
        snprintf(session->state, sizeof(session->state), "failed");
        if (session->pty_fd >= 0) {
            close(session->pty_fd);
            session->pty_fd = -1;
        }
    }
}

static const char *surface_mode_name(int mode)
{
    if (mode == BLIT_SURFACE_EXTERNAL) return "external";
    if (mode == BLIT_SURFACE_MIRROR) return "mirror";
    return "terminal";
}

static void session_json(BlitRuntime *runtime, BlitSession *session,
                         char *body, size_t capacity)
{
    BlitSurface *surface = find_surface_id(runtime, session->id);
    const char *route_id = NULL;
    int route_index;
    size_t used;
    char escaped_title[256];

    for (route_index = 0; route_index < 16; ++route_index)
        if (runtime->input_routes[route_index].used &&
            strcmp(runtime->input_routes[route_index].target_session_id,
                   "session-") != 0) {
            char target[128];
            snprintf(target, sizeof(target), "session-%llu", session->id);
            if (strcmp(runtime->input_routes[route_index].target_session_id,
                       target) == 0) {
                route_id = runtime->input_routes[route_index].id;
                break;
            }
        }

    refresh_session_state(session);
    json_escape(session->terminal_title, escaped_title,
                sizeof(escaped_title));
    snprintf(body, capacity,
             "{\"id\":\"%llu\",\"revision\":%llu,"
             "\"state\":\"%s\",\"surface_id\":\"%s\","
             "\"process_id\":%ld,\"surface_revision\":%llu,"
             "\"focused\":%s,\"surface_mode\":\"%s\"",
             session->id, session->revision, session->state,
             session->surface_id, (long)session->process_id,
             surface != NULL ? surface->revision : 0,
             session->focused ? "true" : "false",
             surface_mode_name(session->surface_mode));
    used = strlen(body);
    used += (size_t)snprintf(body + used, capacity - used,
                             ",\"title\":\"%s\"", escaped_title);
    if (strcmp(session->state, "running") != 0 &&
        strcmp(session->state, "starting") != 0)
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"exit_code\":%d", session->exit_code);
    if (session->display_id[0]) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"display_id\":\"%s\"",
                                 session->display_id);
    }
    if (session->pane_id[0]) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"pane_id\":\"%s\"", session->pane_id);
    }
    if (route_id != NULL)
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"input_route\":\"%s\"", route_id);
    if (surface != NULL) {
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"cursor\":{\"x\":%d,\"y\":%d,"
                                 "\"visible\":%s,\"style\":%d}", surface->cursor_x,
                                 surface->cursor_y,
                                 surface->cursor_visible ? "true" : "false",
                                 surface->cursor_style);
    }
    snprintf(body + used, capacity - used, "}");
}

static const char *pane_surface_id(const BlitRuntime *runtime,
                                   const BlitPane *pane)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
        if (runtime->sessions[i].used &&
            strcmp(runtime->sessions[i].pane_id, pane->id) == 0)
            return runtime->sessions[i].surface_id;
    return NULL;
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

static int if_match_revision(int fd, const char *request,
                             unsigned long long current)
{
    char value[96];
    const char *cursor;
    char *end;
    unsigned long long expected;
    if (!request_header(request, "If-Match:", value, sizeof(value)))
        return 1;
    cursor = value;
    if (*cursor == '*') return 1;
    if (*cursor == '"') {
        ++cursor;
        expected = strtoull(cursor, &end, 10);
        if (end == cursor || *end != '"') {
            send_problem(fd, 400, "Bad Request", "invalid_if_match",
                         "If-Match must contain a numeric resource revision.");
            return 0;
        }
    } else {
        expected = strtoull(cursor, &end, 10);
        if (end == cursor || *end != '\0') {
            send_problem(fd, 400, "Bad Request", "invalid_if_match",
                         "If-Match must contain a numeric resource revision.");
            return 0;
        }
    }
    if (expected != current) {
        send_revision_problem(fd, "revision_conflict",
                              "If-Match does not identify the current resource revision.",
                              current);
        return 0;
    }
    return 1;
}

static int create_session(BlitRuntime *runtime, const char *json,
                           BlitSession **created)
{
    struct winsize size;
    char command[256];
    char argument_storage[32][256];
    char *arguments[33];
    char environment_keys[32][128];
    char environment_values[32][256];
    int argument_count;
    int environment_count;
    char display_id[128];
    char pane_id[128];
    BlitPane *pane = NULL;
    int width = 80;
    int height = 24;
    char surface_mode[32];
    int master_fd;
    pid_t process_id;
    unsigned long long session_id;
    int i;

    if (!json_string(json, "command", command, sizeof(command)))
        return 0;
    arguments[0] = command;
    argument_count = json_string_array(json, "arguments", argument_storage,
                                       arguments + 1, 32);
    if (argument_count < 0)
        return 0;
    environment_count = json_string_object(json, "environment",
                                           environment_keys,
                                           environment_values, 32);
    if (environment_count < 0)
        return 0;
    ++argument_count;
    arguments[argument_count] = NULL;
    json_int(json, "width", &width);
    json_int(json, "height", &height);
    memset(surface_mode, 0, sizeof(surface_mode));
    if (json_string(json, "surface_mode", surface_mode, sizeof(surface_mode)) &&
        strcmp(surface_mode, "terminal") != 0 &&
        strcmp(surface_mode, "external") != 0 &&
        strcmp(surface_mode, "mirror") != 0)
        return 0;
    if (width < 1 || height < 1 || width > BLIT_MAX_CELL_COLS ||
        height > BLIT_MAX_CELL_ROWS)
        return 0;
    memset(display_id, 0, sizeof(display_id));
    json_string(json, "display_id", display_id, sizeof(display_id));
    if (!display_id_is_known(runtime, display_id))
        return 0;
    memset(pane_id, 0, sizeof(pane_id));
    if (json_string(json, "pane_id", pane_id, sizeof(pane_id))) {
        pane = find_pane_id(runtime, pane_id);
        if (pane == NULL)
            return 0;
        if (display_id[0] != '\0' && strcmp(display_id, pane->display_id) != 0)
            return 0;
        snprintf(display_id, sizeof(display_id), "%s", pane->display_id);
        width = pane->width;
        height = pane->height;
    }
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)width;
    size.ws_row = (unsigned short)height;
    session_id = runtime->next_session_id;
    process_id = forkpty(&master_fd, NULL, NULL, &size);
    if (process_id < 0) {
        fprintf(stderr, "blit: forkpty failed: %s\n", strerror(errno));
        return 0;
    }
    if (process_id == 0) {
        int environment_index;
        char session_text[32];
        char surface_text[64];
        char port_text[16];
        snprintf(session_text, sizeof(session_text), "%llu", session_id);
        snprintf(surface_text, sizeof(surface_text), "surface-%llu", session_id);
        snprintf(port_text, sizeof(port_text), "%d", runtime->listen_port);
        if (setenv("BLIT_SESSION_ID", session_text, 1) != 0 ||
            setenv("BLIT_SURFACE_ID", surface_text, 1) != 0 ||
            setenv("BLIT_SERVER_PORT", port_text, 1) != 0)
            _exit(126);
        if (pane_id[0] != '\0' && setenv("BLIT_PANE_ID", pane_id, 1) != 0)
            _exit(126);
        for (environment_index = 0; environment_index < environment_count;
             ++environment_index)
            if (setenv(environment_keys[environment_index],
                       environment_values[environment_index], 1) != 0)
                _exit(126);
        execve(command, arguments, environ);
        _exit(127);
    }
    fcntl(master_fd, F_SETFL, O_NONBLOCK);
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (!runtime->sessions[i].used) {
            BlitSession *session = &runtime->sessions[i];
            memset(session, 0, sizeof(*session));
            session->used = 1;
            session->id = session_id;
            ++runtime->next_session_id;
            session->process_id = process_id;
            session->pty_fd = master_fd;
            session->revision = 1;
            session->exit_code = -1;
            session->terminal_autowrap = 1;
            session->surface_mode = strcmp(surface_mode, "external") == 0 ?
                BLIT_SURFACE_EXTERNAL :
                (strcmp(surface_mode, "mirror") == 0 ?
                 BLIT_SURFACE_MIRROR : BLIT_SURFACE_TERMINAL);
            snprintf(session->state, sizeof(session->state), "starting");
            snprintf(session->surface_id, sizeof(session->surface_id),
                     "surface-%llu", session->id);
            snprintf(session->display_id, sizeof(session->display_id), "%s",
                     display_id);
            snprintf(session->pane_id, sizeof(session->pane_id), "%s", pane_id);
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
                surface->cursor_style = 1;
                surface->scroll_top = 0;
                surface->scroll_bottom = height - 1;
                terminal_reset_tabs(surface);
                snprintf(surface->state, sizeof(surface->state), "available");
            }
            ++runtime->session_count;
            if (pane != NULL)
                ++runtime->topology_revision;
            /* Session creation can succeed before the PTY has produced its
               first byte.  The newly attached pane still needs a complete
               first frame and must not wait for a later terminal write. */
            runtime->render_pending = 1;
            *created = session;
            return 1;
        }
    }
    kill(process_id, SIGTERM);
    close(master_fd);
    return 0;
}

static int resize_session_surface(BlitSession *session, BlitSurface *surface,
                                  int width, int height)
{
    struct winsize size;

    if (width < 1 || height < 1 || width > BLIT_MAX_CELL_COLS ||
        height > BLIT_MAX_CELL_ROWS)
        return 0;
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)width;
    size.ws_row = (unsigned short)height;
    if (session->pty_fd >= 0 && ioctl(session->pty_fd, TIOCSWINSZ, &size) < 0)
        return 0;
    memset(surface->cells, 0, sizeof(surface->cells));
    memset(surface->codepoints, 0, sizeof(surface->codepoints));
    memset(surface->foreground, 0, sizeof(surface->foreground));
    memset(surface->background, 0, sizeof(surface->background));
    memset(surface->attributes, 0, sizeof(surface->attributes));
    surface->width = width;
    surface->height = height;
    surface->cursor_x = 0;
    surface->cursor_y = 0;
    surface->cursor_visible = 1;
    surface->cursor_style = 1;
    surface->scroll_top = 0;
    surface->scroll_bottom = height - 1;
    terminal_reset_tabs(surface);
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = width;
    surface->damage_height = height;
    ++surface->revision;
    return 1;
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

static void destroy_session(BlitRuntime *runtime, BlitSession *session)
{
    BlitSurface *surface = find_surface_id(runtime, session->id);
    int had_pane = session->pane_id[0] != '\0';
    char target_id[128];
    int i;
    int route_changed = 0;

    snprintf(target_id, sizeof(target_id), "session-%llu", session->id);

    refresh_session_state(session);
    if (strcmp(session->state, "running") == 0 ||
        strcmp(session->state, "starting") == 0) {
        if (kill(-session->process_id, SIGTERM) < 0)
            kill(session->process_id, SIGTERM);
        while (waitpid(session->process_id, NULL, 0) < 0 && errno == EINTR)
            continue;
    }
    if (session->pty_fd >= 0)
        close(session->pty_fd);
    for (i = 0; i < 16; ++i) {
        if (runtime->input_routes[i].used &&
            strcmp(runtime->input_routes[i].target_session_id,
                   target_id) == 0) {
            runtime->input_routes[i].enabled = 0;
            runtime->input_routes[i].target_session_id[0] = '\0';
            route_changed = 1;
        }
    }
    if (route_changed)
        ++runtime->input_routes_revision;
    if (surface != NULL)
        memset(surface, 0, sizeof(*surface));
    if (session->focused)
        session->focused = 0;
    memset(session, 0, sizeof(*session));
    if (runtime->session_count > 0)
        --runtime->session_count;
    if (had_pane)
        ++runtime->topology_revision;
    for (i = 0; i < runtime->output_count; ++i)
        runtime->outputs[i].initialized = 0;
    runtime->render_pending = 1;
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
    char sequenced[2304];
    size_t length = strlen(message);
    unsigned long long sequence = runtime->next_event_sequence++;
    int i;

    if (length < 2 || message[length - 1] != '}' ||
        length + 32 >= sizeof(sequenced))
        return;
    snprintf(sequenced, sizeof(sequenced), "%.*s,\"sequence\":%llu}",
             (int)(length - 1), message, sequence);
    {
        BlitEventRecord *record = &runtime->event_history[
            runtime->next_event_history % BLIT_MAX_EVENT_HISTORY];
        record->used = 1;
        record->sequence = sequence;
        record->subscription = subscription;
        snprintf(record->message, sizeof(record->message), "%s", sequenced);
        ++runtime->next_event_history;
    }
    for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
        if (runtime->streams[i].used &&
            (runtime->streams[i].subscriptions & subscription) &&
            !send_websocket_text(runtime->streams[i].fd, sequenced))
            remove_stream(runtime, runtime->streams[i].fd);
    }
}

static void replay_stream(BlitRuntime *runtime, int fd,
                          unsigned long long since)
{
    unsigned long long oldest = 0;
    unsigned long long sequence;
    int gap = 0;
    int i;

    for (i = 0; i < BLIT_MAX_EVENT_HISTORY; ++i) {
        if (runtime->event_history[i].used &&
            (oldest == 0 || runtime->event_history[i].sequence < oldest))
            oldest = runtime->event_history[i].sequence;
    }
    if (oldest != 0 && since + 1 < oldest)
        gap = 1;
    if (gap) {
        char diagnostic[256];
        snprintf(diagnostic, sizeof(diagnostic),
                 "{\"type\":\"diagnostic\",\"severity\":\"warning\","
                 "\"code\":\"replay_gap\",\"detail\":"
                 "\"requested sequence is no longer retained\","
                 "\"resource_id\":\"sequence-%llu\"}", oldest);
        send_websocket_text(fd, diagnostic);
    }
    for (sequence = since + 1; sequence < runtime->next_event_sequence;
         ++sequence) {
        for (i = 0; i < BLIT_MAX_EVENT_HISTORY; ++i) {
            BlitEventRecord *record = &runtime->event_history[i];
            if (record->used && record->sequence == sequence) {
                if (!send_websocket_text(fd, record->message)) {
                    remove_stream(runtime, fd);
                    return;
                }
                break;
            }
        }
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

            if (strcmp(runtime->sessions[i].state, "starting") == 0) {
                snprintf(runtime->sessions[i].state,
                         sizeof(runtime->sessions[i].state), "running");
                ++runtime->sessions[i].revision;
            }
            if (runtime->sessions[i].surface_mode != BLIT_SURFACE_EXTERNAL &&
                surface != NULL &&
                terminal_consume_output(&runtime->sessions[i], surface, output,
                                        (size_t)received)) {
                ++surface->revision;
                surface_delta_json(surface, surface_event,
                                   sizeof(surface_event));
                broadcast_stream(runtime, surface_event, BLIT_STREAM_SURFACES);
                runtime->render_pending = 1;
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
    static char body[4194304];
    const char *payload = request_body(request);

    if (sscanf(request, "%15s %255s", method, path) != 2) {
        send_http(fd, 400, "Bad Request", "application/problem+json",
                  "{\"type\":\"about:blank\",\"title\":\"Bad Request\","
                  "\"status\":400,\"code\":\"malformed_request\","
                  "\"detail\":\"The request line is invalid.\"}");
        return;
    }
    if (runtime->auth_token[0] != '\0') {
        char authorization[320];
        char expected[320];
        if (!request_header(request, "Authorization:", authorization,
                            sizeof(authorization))) {
            send_problem(fd, 401, "Unauthorized", "authentication_required",
                         "A bearer token is required.");
            return;
        }
        snprintf(expected, sizeof(expected), "Bearer %s", runtime->auth_token);
        if (strcmp(authorization, expected) != 0) {
            send_problem(fd, 401, "Unauthorized", "authentication_failed",
                         "The bearer token is not valid.");
            return;
        }
    }

    if (strncmp(path, "/v1/stream", strlen("/v1/stream")) == 0 &&
        (path[strlen("/v1/stream")] == '\0' ||
         path[strlen("/v1/stream")] == '?') &&
        strcmp(method, "GET") == 0) {
        char accept[64];
        char response[256];
        const char *since_value = strstr(path, "since=");
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
        if (since_value != NULL)
            replay_stream(runtime, fd, strtoull(since_value + 6, NULL, 10));
        return;
    }

    if (strcmp(path, "/v1/terminal-sessions") == 0 &&
        strcmp(method, "POST") == 0) {
        BlitSession *session;
        char requested_display_id[128];
        char requested_pane_id[128];
        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;
        if (json_string(payload, "display_id", requested_display_id,
                        sizeof(requested_display_id)) &&
            !display_id_is_known(runtime, requested_display_id)) {
            send_problem(fd, 404, "Not Found", "display_not_found",
                         "The requested display does not exist.");
            return;
        }
        if (json_string(payload, "pane_id", requested_pane_id,
                        sizeof(requested_pane_id)) &&
            find_pane_id(runtime, requested_pane_id) == NULL) {
            send_problem(fd, 404, "Not Found", "pane_not_found",
                         "The requested Blit pane does not exist.");
            return;
        }
        if (!create_session(runtime, payload, &session)) {
            send_problem(fd, 400, "Bad Request", "session_create_failed",
                         "The command, surface dimensions, or PTY could not be created.");
            return;
        }
        session_json(runtime, session, body, sizeof(body));
        send_http(fd, 201, "Created", "application/json", body);
        idempotency_store(runtime, request, method, path, payload, 201,
                          "Created", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/surfaces/", strlen("/v1/surfaces/")) == 0 &&
        (strcmp(method, "PUT") == 0 || strcmp(method, "GET") == 0)) {
        BlitSurface *surface = find_surface(runtime, path);
        int base_revision;

        if (surface == NULL) {
            send_problem(fd, 404, "Not Found", "surface_not_found",
                         "The text surface does not exist.");
            return;
        }
        if (strcmp(method, "GET") == 0) {
            surface_snapshot_json(surface, body, sizeof(body));
            send_http(fd, 200, "OK", "application/json", body);
            return;
        }
        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;
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
        (strcmp(method, "GET") == 0 || strcmp(method, "PATCH") == 0 ||
         strcmp(method, "DELETE") == 0)) {
        BlitSession *session = find_session(runtime, path);
        if (session == NULL) {
            send_problem(fd, 404, "Not Found", "session_not_found",
                         "The terminal session does not exist.");
            return;
        }
        if (strcmp(method, "DELETE") == 0) {
            destroy_session(runtime, session);
            send_http(fd, 204, "No Content", "application/json", "");
            return;
        }
        if (strcmp(method, "PATCH") == 0) {
            char display_id[128];
            char pane_id[128];
            BlitSurface *surface = find_surface_id(runtime, session->id);
            int resize_requested = strstr(payload, "\"surface\"") != NULL;
            int width = 0;
            int height = 0;
            int resized = 0;
            int focused = 0;
            int focused_requested = strstr(payload, "\"focused\"") != NULL;
            int route_requested = strstr(payload, "\"input_route\"") != NULL;
            char input_route_id[128];
            BlitInputRoute *input_route = NULL;
            if (!if_match_revision(fd, request, session->revision))
                return;
            int display_requested = json_string(payload, "display_id",
                                                display_id, sizeof(display_id));
            int pane_requested = json_string(payload, "pane_id", pane_id,
                                             sizeof(pane_id));
            BlitPane *pane = pane_requested ? find_pane_id(runtime, pane_id) : NULL;
            if ((route_requested &&
                 !json_string(payload, "input_route", input_route_id,
                              sizeof(input_route_id))) ||
                (focused_requested && !json_bool(payload, "focused", &focused))) {
                send_problem(fd, 400, "Bad Request", "invalid_input_policy",
                             "input_route must name an existing route and focused must be a JSON boolean.");
                return;
            }
            if (route_requested) {
                input_route = find_input_route(runtime, input_route_id);
                if (input_route == NULL) {
                    send_problem(fd, 404, "Not Found", "input_route_not_found",
                                 "The requested input route does not exist.");
                    return;
                }
            }
            if (pane_requested && pane == NULL) {
                send_problem(fd, 404, "Not Found", "pane_not_found",
                             "The requested Blit pane does not exist.");
                return;
            }
            if (pane_requested) {
                if (display_requested && strcmp(display_id, pane->display_id) != 0) {
                    send_problem(fd, 409, "Conflict", "pane_display_conflict",
                                 "The pane belongs to a different display.");
                    return;
                }
                if (resize_requested &&
                    (width != pane->width || height != pane->height)) {
                    send_problem(fd, 400, "Bad Request", "pane_resize_conflict",
                                 "A pane attachment determines surface dimensions.");
                    return;
                }
                width = pane->width;
                height = pane->height;
                resize_requested = 1;
                display_requested = 1;
                snprintf(display_id, sizeof(display_id), "%s", pane->display_id);
            }
            if (resize_requested && !pane_requested &&
                (!json_int(payload, "width", &width) ||
                 !json_int(payload, "height", &height))) {
                send_problem(fd, 400, "Bad Request", "invalid_surface_resize",
                             "A surface resize requires width and height.");
                return;
            }
            if (display_requested) {
                if (!display_id_is_known(runtime, display_id)) {
                    send_problem(fd, 404, "Not Found", "display_not_found",
                                 "The requested display does not exist.");
                    return;
                }
            }
            if (resize_requested) {
                if (surface == NULL || !resize_session_surface(session, surface,
                                                                 width, height)) {
                    send_problem(fd, 400, "Bad Request", "invalid_surface_resize",
                                 "The requested surface dimensions are not supported.");
                    return;
                }
                resized = 1;
            }
            if (display_requested) {
                snprintf(session->display_id, sizeof(session->display_id),
                         "%s", display_id);
                {
                    int output_index;
                    for (output_index = 0;
                         output_index < runtime->output_count; ++output_index)
                         runtime->outputs[output_index].initialized = 0;
                }
            }
            if (pane_requested)
                snprintf(session->pane_id, sizeof(session->pane_id), "%s", pane_id);
            if (pane_requested)
                ++runtime->topology_revision;
            if (focused_requested) {
                focus_session(runtime, focused ? session : NULL);
            }
            if (route_requested) {
                snprintf(input_route->target_session_id,
                         sizeof(input_route->target_session_id),
                         "session-%llu", session->id);
                ++runtime->input_routes_revision;
            }
            if (resized) {
                int output_index;
                for (output_index = 0; output_index < runtime->output_count;
                     ++output_index)
                    runtime->outputs[output_index].initialized = 0;
            }
            ++session->revision;
            if (resized) {
                char event[512];
                surface_delta_json(surface, event, sizeof(event));
                broadcast_stream(runtime, event, BLIT_STREAM_SURFACES);
            }
        }
        session_json(runtime, session, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/topology") == 0 && strcmp(method, "PUT") == 0) {
        int columns;
        int rows;
        int requested_revision;
        int has_requested_revision = strstr(payload, "\"revision\"") != NULL;
        int i;

        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;
        if (has_requested_revision &&
            (!json_int(payload, "revision", &requested_revision) ||
             requested_revision < 0)) {
            send_problem(fd, 400, "Bad Request", "invalid_topology_revision",
                         "revision must be a non-negative integer.");
            return;
        }
        if (has_requested_revision &&
            (unsigned long long) requested_revision != runtime->topology_revision) {
            send_revision_problem(fd, "revision_conflict",
                                  "The topology revision is no longer current.",
                                  runtime->topology_revision);
            return;
        }
        if (!if_match_revision(fd, request, runtime->topology_revision))
            return;
        if (!strstr(payload, "\"layout\"") ||
            !json_int(payload, "columns", &columns) ||
            !json_int(payload, "rows", &rows) || columns < 1 || columns > 2 ||
            rows < 1 || rows > 2) {
            send_problem(fd, 400, "Bad Request", "invalid_topology_layout",
                         "The layout must specify columns and rows from one through two.");
            return;
        }
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime->sessions[i].used && runtime->sessions[i].pane_id[0]) {
                send_problem(fd, 409, "Conflict", "topology_in_use",
                             "The topology cannot change while a pane is attached.");
                return;
            }
        }
        runtime->pane_columns = columns;
        runtime->pane_rows = rows;
        discover_panes(runtime);
        ++runtime->topology_revision;
        topology_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        idempotency_store(runtime, request, method, path, payload, 200,
                          "OK", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/input/routes") == 0 &&
        strcmp(method, "GET") == 0) {
        input_routes_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/input/routes/", strlen("/v1/input/routes/")) == 0 &&
        strcmp(method, "PUT") == 0) {
        const char *route_id = path + strlen("/v1/input/routes/");
        char device_id[128];
        char target_session_id[128];
        int enabled;
        BlitInputRoute *route = find_input_route(runtime, route_id);
        int i;

        if (*route_id == '\0' || strlen(route_id) >= sizeof(device_id) ||
            !json_string(payload, "device_id", device_id, sizeof(device_id)) ||
            !json_string(payload, "target_session_id", target_session_id,
                         sizeof(target_session_id)) ||
            !json_bool(payload, "enabled", &enabled) ||
            (strcmp(device_id, "evdev-keyboard") != 0 &&
             strcmp(device_id, "evdev-pointer") != 0) ||
            strncmp(target_session_id, "session-", 8) != 0 ||
            find_session_id(runtime,
                strtoull(target_session_id + 8, NULL, 10)) == NULL) {
            send_problem(fd, 400, "Bad Request", "invalid_input_route",
                         "A route requires a supported evdev device, a live session target, and enabled boolean.");
            return;
        }
        if (route == NULL) {
            for (i = 0; i < 16; ++i)
                if (!runtime->input_routes[i].used) {
                    route = &runtime->input_routes[i];
                    memset(route, 0, sizeof(*route));
                    route->used = 1;
                    snprintf(route->id, sizeof(route->id), "%s", route_id);
                    break;
                }
        }
        if (route == NULL) {
            send_problem(fd, 503, "Service Unavailable", "input_route_capacity",
                         "The input-route capacity has been reached.");
            return;
        }
        if (enabled) {
            for (i = 0; i < 16; ++i)
                if (runtime->input_routes[i].used &&
                    &runtime->input_routes[i] != route &&
                    runtime->input_routes[i].enabled &&
                    strcmp(runtime->input_routes[i].device_id, device_id) == 0)
                    runtime->input_routes[i].enabled = 0;
        }
        snprintf(route->device_id, sizeof(route->device_id), "%s", device_id);
        snprintf(route->target_session_id, sizeof(route->target_session_id),
                 "%s", target_session_id);
        route->enabled = enabled;
        ++runtime->input_routes_revision;
        input_routes_json(runtime, body, sizeof(body));
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
    } else if (strcmp(path, "/v1/diagnostics") == 0) {
        diagnostics_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/displays") == 0) {
        displays_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/topology") == 0) {
        topology_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/graphics") == 0) {
        graphics_json(body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/audio") == 0) {
        audio_json(body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/input/devices") == 0) {
        input_json(body, sizeof(body));
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

    if (getenv("BLIT_DISPLAY_BACKEND") != NULL &&
        strcmp(getenv("BLIT_DISPLAY_BACKEND"), "kms") == 0) {
        char error[256];
        error[0] = '\0';
        runtime->direct_kms = 1;
        runtime->kms = blit_kms_new();
        if (runtime->kms == NULL || blit_kms_open(runtime->kms, error,
                                                   sizeof(error)) < 0) {
            snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                     "direct KMS: %s", error[0] ? error : "initialization failed");
            fprintf(stderr, "blit: %s\n", runtime->sdl_error);
            return;
        }
        runtime->display_count = blit_kms_display_count(runtime->kms);
        if (runtime->display_count > BLIT_MAX_DISPLAYS)
            runtime->display_count = BLIT_MAX_DISPLAYS;
        for (i = 0; i < runtime->display_count; ++i) {
            BlitKmsDisplayInfo info;
            if (blit_kms_display_info(runtime->kms, i, &info) < 0)
                continue;
            runtime->displays[i].id = info.id;
            snprintf(runtime->displays[i].name,
                     sizeof(runtime->displays[i].name), "%s", info.name);
            runtime->displays[i].bounds.x = info.x;
            runtime->displays[i].bounds.y = info.y;
            runtime->displays[i].bounds.w = info.width;
            runtime->displays[i].bounds.h = info.height;
            runtime->displays[i].refresh_hz = info.refresh_hz;
        }
        runtime->sdl_error[0] = '\0';
        return;
    }

    {
        const char *evdev_devices = getenv("SDL_EVDEV_DEVICES");
        if (evdev_devices != NULL && evdev_devices[0] != '\0')
            SDL_SetHint("SDL_EVDEV_DEVICES", evdev_devices);
    }
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
        runtime->displays[i].refresh_hz = 60.0;
    }
    SDL_free(ids);
}

static void discover_panes(BlitRuntime *runtime)
{
    const char *layout = getenv("BLIT_PANE_LAYOUT");
    int columns = runtime->pane_columns;
    int rows = runtime->pane_rows;
    int display_index;

    if (columns == 0 && layout != NULL)
        sscanf(layout, "%dx%d", &columns, &rows);
    if (columns < 1 || columns > 2 || rows < 1 || rows > 2) {
        columns = 1;
        rows = 1;
    }
    runtime->pane_columns = columns;
    runtime->pane_rows = rows;
    runtime->pane_count = 0;
    for (display_index = 0; display_index < runtime->display_count;
         ++display_index) {
        BlitDisplay *display = &runtime->displays[display_index];
        int cell_width = (runtime->direct_kms ?
            blit_kms_cell_width(runtime->kms) : 8) * runtime->font_scale;
        int cell_height = (runtime->direct_kms ?
            blit_kms_cell_height(runtime->kms) : 8) * runtime->font_scale;
        int grid_width = display->bounds.w > 0 ? display->bounds.w / cell_width : 80;
        int grid_height = display->bounds.h > 0 ? display->bounds.h / cell_height : 24;
        int pane_width = grid_width / columns;
        int pane_height = grid_height / rows;
        int row;
        int column;

        if (pane_width < 1) pane_width = 1;
        if (pane_height < 1) pane_height = 1;
        if (pane_width > BLIT_MAX_CELL_COLS) pane_width = BLIT_MAX_CELL_COLS;
        if (pane_height > BLIT_MAX_CELL_ROWS) pane_height = BLIT_MAX_CELL_ROWS;
        for (row = 0; row < rows; ++row) {
            for (column = 0; column < columns; ++column) {
                BlitPane *pane;
                if (runtime->pane_count >= BLIT_MAX_PANES)
                    return;
                pane = &runtime->panes[runtime->pane_count++];
                memset(pane, 0, sizeof(*pane));
                pane->used = 1;
                snprintf(pane->display_id, sizeof(pane->display_id),
                         "display-%llu", display->id);
                snprintf(pane->id, sizeof(pane->id), "pane-display-%llu-%d",
                         display->id, row * columns + column + 1);
                pane->x = column * pane_width;
                pane->y = row * pane_height;
                pane->width = pane_width;
                pane->height = pane_height;
            }
        }
    }
}

static void initialize_outputs(BlitRuntime *runtime)
{
    const char *renderer_name = getenv("BLIT_RENDERER");
    int i;

    if (runtime->direct_kms) {
        runtime->output_count = runtime->display_count;
        for (i = 0; i < runtime->output_count; ++i)
            runtime->outputs[i].display_id = runtime->displays[i].id;
        return;
    }

    /* KMSDRM text output must also work on software VKMS hosts.  SDL's
       automatic renderer selection probes kmsro there and can stall; the
       software renderer is the portable baseline and remains overridable. */
    if (renderer_name == NULL || renderer_name[0] == '\0')
        renderer_name = "software";

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
        renderer = SDL_CreateRenderer(window, renderer_name);
        if (renderer == NULL) {
            snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                     "display %llu renderer: %s", display->id, SDL_GetError());
            SDL_DestroyWindow(window);
            continue;
        }
        SDL_SetRenderScale(renderer, (float)runtime->font_scale,
                           (float)runtime->font_scale);
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
    if (runtime->direct_kms) {
        if (runtime->kms != NULL)
            blit_kms_close(runtime->kms);
        runtime->kms = NULL;
        runtime->output_count = 0;
        return;
    }
    for (i = 0; i < runtime->output_count; ++i) {
        SDL_DestroyRenderer(runtime->outputs[i].renderer);
        SDL_DestroyWindow(runtime->outputs[i].window);
    }
    runtime->output_count = 0;
}

static int evdev_has_rel_xy(int fd)
{
    unsigned long relative[2];
    memset(relative, 0, sizeof(relative));
    if (ioctl(fd, EVIOCGBIT(EV_REL, sizeof(relative)), relative) < 0)
        return 0;
    return (relative[REL_X / (sizeof(unsigned long) * 8)] &
            (1UL << (REL_X % (sizeof(unsigned long) * 8)))) != 0 &&
        (relative[REL_Y / (sizeof(unsigned long) * 8)] &
         (1UL << (REL_Y % (sizeof(unsigned long) * 8)))) != 0;
}

static int evdev_has_keyboard(int fd)
{
    unsigned long keys[32];
    memset(keys, 0, sizeof(keys));
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
        return 0;
    return input_bit(keys, KEY_A) && input_bit(keys, KEY_ENTER);
}

static int input_path_seen(char paths[][128], int count, const char *path)
{
    int i;
    for (i = 0; i < count; ++i)
        if (strcmp(paths[i], path) == 0) return 1;
    return 0;
}

static int open_evdev_mice(const char *configured, int *fds,
                           char paths[][128], int *count)
{
    int index;
    *count = 0;
    if (configured != NULL && configured[0] != '\0') {
        int fd = open(configured, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd >= 0 && evdev_has_rel_xy(fd)) {
            fds[(*count)] = fd;
            snprintf(paths[(*count)++], 128, "%s", configured);
        } else if (fd >= 0) {
            fprintf(stderr, "blit: configured mouse %s has no REL_X/REL_Y; scanning all input devices\n", configured);
            close(fd);
        } else {
            fprintf(stderr, "blit: cannot open configured mouse %s: %s\n",
                    configured, strerror(errno));
        }
    }
    for (index = 0; index < 32 && *count < BLIT_MAX_INPUT_DEVICES; ++index) {
        char path[64];
        int fd;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        if (input_path_seen(paths, *count, path)) continue;
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (evdev_has_rel_xy(fd)) {
            fds[*count] = fd;
            snprintf(paths[(*count)++], 128, "%s", path);
        } else close(fd);
    }
    return *count;
}

static int open_evdev_keyboards(int *fds, char paths[][128], int *count)
{
    int index;
    *count = 0;
    for (index = 0; index < 32 && *count < BLIT_MAX_INPUT_DEVICES; ++index) {
        char path[64];
        int fd;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (evdev_has_keyboard(fd)) {
            fds[*count] = fd;
            snprintf(paths[(*count)++], 128, "%s", path);
        } else close(fd);
    }
    return *count;
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

    if (runtime->direct_kms) {
        BlitKmsSurfaceView views[BLIT_MAX_SESSIONS];
        int view_count = 0;
        int surface_index;
        (void)plan;
        for (surface_index = 0; surface_index < BLIT_MAX_SESSIONS &&
             view_count < BLIT_MAX_SESSIONS; ++surface_index) {
            BlitSurface *surface = &runtime->surfaces[surface_index];
            BlitSession *session;
            BlitPane *pane;
            int output_index_for_surface = 0;
            if (!surface->used) continue;
            session = find_session_id(runtime, surface->owner_session_id);
            if (session == NULL) continue;
            pane = find_pane_id(runtime, session->pane_id);
            if (session->display_id[0] != '\0') {
                for (output_index_for_surface = 0;
                     output_index_for_surface < runtime->output_count;
                     ++output_index_for_surface) {
                    char display_id[128];
                    snprintf(display_id, sizeof(display_id), "display-%llu",
                             runtime->outputs[output_index_for_surface].display_id);
                    if (strcmp(display_id, session->display_id) == 0) break;
                }
                if (output_index_for_surface >= runtime->output_count) continue;
            }
            memset(&views[view_count], 0, sizeof(views[view_count]));
            views[view_count].surface_id = surface->id;
            views[view_count].surface_revision = surface->revision;
            views[view_count].display_id = runtime->outputs[output_index_for_surface].display_id;
            views[view_count].origin_x = pane != NULL ?
                pane->x * blit_kms_cell_width(runtime->kms) *
                    runtime->font_scale + 4 : 4;
            views[view_count].origin_y = pane != NULL ?
                pane->y * blit_kms_cell_height(runtime->kms) *
                    runtime->font_scale + 4 : 4;
            views[view_count].cell_width =
                blit_kms_cell_width(runtime->kms) * runtime->font_scale;
            views[view_count].cell_height =
                blit_kms_cell_height(runtime->kms) * runtime->font_scale;
            views[view_count].columns = surface->width;
            views[view_count].rows = surface->height;
            views[view_count].stride = BLIT_MAX_CELL_COLS;
            views[view_count].codepoints = &surface->codepoints[0][0];
            views[view_count].combining = &surface->combining[0][0][0];
            views[view_count].combining_count = &surface->combining_count[0][0];
            views[view_count].foreground = &surface->foreground[0][0];
            views[view_count].background = &surface->background[0][0];
            views[view_count].underline_color =
                &surface->underline_color[0][0];
            views[view_count].attributes = &surface->attributes[0][0];
            views[view_count].cursor_x = surface->cursor_x;
            views[view_count].cursor_y = surface->cursor_y;
            views[view_count].cursor_visible = surface->cursor_visible;
            views[view_count].cursor_style = surface->cursor_style;
            views[view_count].damage_valid = surface->damage_valid;
            views[view_count].damage_x = surface->damage_x;
            views[view_count].damage_y = surface->damage_y;
            views[view_count].damage_width = surface->damage_width;
            views[view_count].damage_height = surface->damage_height;
            ++view_count;
        }
        if (blit_kms_present(runtime->kms, views, view_count,
                             runtime->pointer_x, runtime->pointer_y,
                             runtime->pointer_valid) < 0)
            fprintf(stderr, "blit: direct KMS present failed\n");
        runtime->pointer_dirty = 0;
        return;
    }

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
            } else {
                clear_previous_pointer(runtime, output_index);
            }
            render_pointer(runtime, output_index);
            SDL_RenderPresent(renderer);
            /* A later session needs a full first frame, even if this output
               was previously presenting the empty desktop. */
            runtime->outputs[output_index].initialized = 0;
            continue;
        }
        full_redraw = !runtime->outputs[output_index].initialized ||
            target_count > 1 || runtime->pointer_dirty;
        if (!runtime->outputs[output_index].initialized) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
        } else if (runtime->pointer_dirty) {
            clear_previous_pointer(runtime, output_index);
        }
        SDL_SetRenderDrawColor(renderer, 190, 210, 190, 255);
        for (plan_index = 0; plan_index < plan->surface_count;
             ++plan_index) {
            BlitSurface *surface = find_surface_id(
                runtime, plan->surfaces[plan_index].surface_id);
            BlitSession *session;
            BlitPane *pane;
            char display_id[128];
            float surface_origin_x = 4.0f;
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
            pane = find_pane_id(runtime, session->pane_id);
            if (pane != NULL) {
                surface_origin_x = (float)(pane->x * 8 + 4);
                surface_origin_y = pane->y * 8 + 4;
            }
            if (session->display_id[0] == '\0') {
                if (output_index != 0)
                    continue;
            } else {
                snprintf(display_id, sizeof(display_id), "display-%llu",
                         runtime->outputs[output_index].display_id);
                if (strcmp(session->display_id, display_id) != 0)
                    continue;
            }
            if (pane == NULL) {
                for (prior_index = 0; prior_index < plan_index; ++prior_index) {
                    BlitSurface *prior = find_surface_id(
                        runtime, plan->surfaces[prior_index].surface_id);
                    if (prior != NULL && prior->used &&
                        surface_targets_output(runtime, prior, output_index))
                        surface_origin_y += prior->height * 8 + 4;
                }
            }
            if (!full_redraw) {
                if (!plan->surfaces[plan_index].damage_valid)
                    continue;
                first_row = plan->surfaces[plan_index].damage_y;
                last_row = first_row +
                    plan->surfaces[plan_index].damage_height;
                {
                    SDL_FRect damage;
                    damage.x = surface_origin_x +
                        plan->surfaces[plan_index].damage_x * 8.0f;
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
                    uint16_t attributes = surface->attributes[row][column];
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
                        cell_background.x = surface_origin_x + column * 8.0f;
                        cell_background.y = (float)surface_origin_y + row * 8.0f;
                        cell_background.w = 8.0f;
                        cell_background.h = 8.0f;
                        SDL_SetRenderDrawColor(renderer,
                            (unsigned char)(background >> 16),
                            (unsigned char)(background >> 8),
                            (unsigned char)background, 255);
                        SDL_RenderFillRect(renderer, &cell_background);
                    }
                    {
                        uint32_t underline_color =
                            surface->underline_color[row][column] ?
                            surface->underline_color[row][column] - 1 :
                            foreground;
                    SDL_SetRenderDrawColor(renderer,
                        (unsigned char)(foreground >> 16),
                        (unsigned char)(foreground >> 8),
                        (unsigned char)foreground, 255);
                    utf8_encode(surface->codepoints[row][column] ?
                        surface->codepoints[row][column] : ' ', glyph);
                    if (!(attributes & BLIT_ATTR_CONCEAL)) {
                        SDL_RenderDebugText(renderer, surface_origin_x + column * 8.0f,
                            (float)surface_origin_y + row * 8.0f, glyph);
                        if (surface->combining_count[row][column] > 0) {
                            int combining_index;
                            for (combining_index = 0;
                                 combining_index < surface->combining_count[row][column] &&
                                 combining_index < BLIT_MAX_COMBINING;
                                 ++combining_index) {
                                utf8_encode(surface->combining[row][column][combining_index],
                                            glyph);
                                SDL_RenderDebugText(renderer,
                                    surface_origin_x + column * 8.0f,
                                    (float)surface_origin_y + row * 8.0f, glyph);
                            }
                        }
                        if (attributes & BLIT_ATTR_BOLD)
                            SDL_RenderDebugText(renderer, surface_origin_x + 1.0f + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f, glyph);
                        if (attributes & BLIT_ATTR_ITALIC)
                            SDL_RenderDebugText(renderer, surface_origin_x + 1.0f + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f, glyph);
                        if (attributes & BLIT_ATTR_UNDERLINE)
                            SDL_SetRenderDrawColor(renderer,
                                (unsigned char)(underline_color >> 16),
                                (unsigned char)(underline_color >> 8),
                                (unsigned char)underline_color, 255);
                        if (attributes & BLIT_ATTR_UNDERLINE)
                            SDL_RenderLine(renderer, surface_origin_x + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f + 7.0f,
                                11.0f + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f + 7.0f);
                        if (attributes & BLIT_ATTR_STRIKE)
                            SDL_RenderLine(renderer, surface_origin_x + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f + 4.0f,
                                11.0f + column * 8.0f,
                                (float)surface_origin_y + row * 8.0f + 4.0f);
                    }
                    }
                }
            }
            if (surface->cursor_visible &&
                surface->cursor_y >= first_row && surface->cursor_y < last_row) {
                float cursor_x = surface_origin_x + surface->cursor_x * 8.0f;
                float cursor_y = (float)surface_origin_y + surface->cursor_y * 8.0f;
                SDL_SetRenderDrawColor(renderer,
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 16),
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 8),
                    (unsigned char)BLIT_DEFAULT_FOREGROUND, 255);
                if (surface->cursor_style == 3 || surface->cursor_style == 4)
                    SDL_RenderLine(renderer, cursor_x, cursor_y + 7.0f,
                                   cursor_x + 7.0f, cursor_y + 7.0f);
                else if (surface->cursor_style == 5 || surface->cursor_style == 6)
                    SDL_RenderLine(renderer, cursor_x, cursor_y,
                                   cursor_x, cursor_y + 7.0f);
                else {
                    SDL_FRect cursor_rect;
                    cursor_rect.x = cursor_x;
                    cursor_rect.y = cursor_y;
                    cursor_rect.w = 8.0f;
                    cursor_rect.h = 8.0f;
                    SDL_RenderFillRect(renderer, &cursor_rect);
                }
            }
        }
        render_pointer(runtime, output_index);
        SDL_RenderPresent(renderer);
        runtime->outputs[output_index].initialized = 1;
    }
    runtime->pointer_dirty = 0;
}

static int mouse_output_index(const BlitRuntime *runtime, SDL_WindowID window_id)
{
    int i;

    /* Direct KMS outputs do not have SDL windows.  SDL reports window ID 0
       for the synthetic event path, and asking SDL for the ID of a NULL
       window can make every output look like that same window.  Resolve
       zero-ID KMS input from the server-owned pointer output below instead. */
    if (!runtime->direct_kms && window_id != 0)
        for (i = 0; i < runtime->output_count; ++i)
            if (SDL_GetWindowID(runtime->outputs[i].window) == window_id)
                return i;
    /* Some KMSDRM event paths report a zero window id.  With one physical
       output the target is unambiguous, so retain pointer input there. */
    if (runtime->output_count == 1)
        return 0;
    if (runtime->direct_kms && window_id == 0 &&
        runtime->pointer_output >= 0 &&
        runtime->pointer_output < runtime->output_count)
        return runtime->pointer_output;
    return -1;
}

static BlitSession *mouse_target(BlitRuntime *runtime, int output_index,
                                float pixel_x, float pixel_y,
                                int *column, int *row)
{
    char display_id[128];
    int cell_width;
    int cell_height;
    int i;

    if (output_index < 0 || output_index >= runtime->output_count)
        return NULL;
    if (runtime->direct_kms) {
        pixel_x -= runtime->displays[output_index].bounds.x;
        pixel_y -= runtime->displays[output_index].bounds.y;
    }
    pixel_x /= runtime->font_scale;
    pixel_y /= runtime->font_scale;
    cell_width = runtime->direct_kms ? blit_kms_cell_width(runtime->kms) : 8;
    cell_height = runtime->direct_kms ? blit_kms_cell_height(runtime->kms) : 8;
    snprintf(display_id, sizeof(display_id), "display-%llu",
             runtime->outputs[output_index].display_id);
    for (i = 0; i < runtime->pane_count; ++i) {
        BlitPane *pane = &runtime->panes[i];
        int pane_x;
        int pane_y;
        int j;
        if (!pane->used || strcmp(pane->display_id, display_id) != 0)
            continue;
        pane_x = pane->x * cell_width;
        pane_y = pane->y * cell_height;
        if (pixel_x < pane_x || pixel_y < pane_y ||
            pixel_x >= pane_x + pane->width * cell_width ||
            pixel_y >= pane_y + pane->height * cell_height)
            continue;
        *column = (int)((pixel_x - pane_x) / (float)cell_width) + 1;
        *row = (int)((pixel_y - pane_y) / (float)cell_height) + 1;
        for (j = 0; j < BLIT_MAX_SESSIONS; ++j)
            if (runtime->sessions[j].used &&
                strcmp(runtime->sessions[j].pane_id, pane->id) == 0)
                return &runtime->sessions[j];
        return NULL;
    }
    return NULL;
}

static int active_mouse_button(const BlitRuntime *runtime)
{
    if (runtime->mouse_buttons & 1U) return 0;
    if (runtime->mouse_buttons & 2U) return 1;
    if (runtime->mouse_buttons & 4U) return 2;
    return 3;
}

static void send_focus_report(BlitSession *session, int focused)
{
    const char *report;
    if (session == NULL || session->pty_fd < 0 ||
        !session->terminal_focus_reporting)
        return;
    report = focused ? "\033[I" : "\033[O";
    write(session->pty_fd, report, strlen(report));
}

static void focus_session(BlitRuntime *runtime, BlitSession *target)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used &&
            runtime->sessions[i].focused !=
            (target != NULL && &runtime->sessions[i] == target))
            send_focus_report(&runtime->sessions[i],
                              target != NULL && &runtime->sessions[i] == target);
        if (runtime->sessions[i].used)
            runtime->sessions[i].focused =
                target != NULL && &runtime->sessions[i] == target;
    }
}

static void send_mouse_report(BlitRuntime *runtime, SDL_WindowID window_id,
                              float pixel_x, float pixel_y, int button,
                              int release)
{
    char report[64];
    int output_index = mouse_output_index(runtime, window_id);
    int column = 0;
    int row = 0;
    BlitSession *session = mouse_target(runtime, output_index, pixel_x,
                                        pixel_y, &column, &row);
    BlitSession *routed = input_route_target(runtime, "evdev-pointer");
    int code;

    if (routed != NULL) {
        session = routed;
        if (column == 0) column = 1;
        if (row == 0) row = 1;
    }
    if (session != NULL && !release && button == 0)
        focus_session(runtime, session);
    if (session == NULL || session->pty_fd < 0)
        return;
    if (session->terminal_mouse_mode == 0)
        return;
    code = button;
    if (!release && code < 64 && runtime->mouse_buttons != 0)
        code += 32;
    if (session->terminal_mouse_sgr)
        snprintf(report, sizeof(report), "\033[<%d;%d;%d%c", code, column,
                 row, release ? 'm' : 'M');
    else
        snprintf(report, sizeof(report), "\033[M%c%c%c", code + 32,
                 column + 32, row + 32);
    write(session->pty_fd, report, strlen(report));
}

static void send_mouse_motion_report(BlitRuntime *runtime,
                                     SDL_WindowID window_id,
                                     float pixel_x, float pixel_y)
{
    int output_index = mouse_output_index(runtime, window_id);
    int column = 0;
    int row = 0;
    BlitSession *session = mouse_target(runtime, output_index, pixel_x,
                                        pixel_y, &column, &row);
    BlitSession *routed = input_route_target(runtime, "evdev-pointer");
    int button = active_mouse_button(runtime);
    char report[64];

    if (routed != NULL) {
        session = routed;
        if (column == 0) column = 1;
        if (row == 0) row = 1;
    }
    if (session == NULL || session->pty_fd < 0)
        return;
    if (session->terminal_mouse_mode != 1002 &&
        session->terminal_mouse_mode != 1003)
        return;
    if (session->terminal_mouse_sgr)
        snprintf(report, sizeof(report), "\033[<%d;%d;%dM", button + 32,
                 column, row);
    else
        snprintf(report, sizeof(report), "\033[M%c%c%c", button + 64,
                 column + 32, row + 32);
    write(session->pty_fd, report, strlen(report));
}

static void update_evdev_pointer(BlitRuntime *runtime)
{
    float old_x;
    float old_y;
    int old_output;
    int old_valid;
    int changed;
    if (runtime->evdev_mouse_count <= 0 || runtime->output_count <= 0)
        return;
    old_x = runtime->pointer_x;
    old_y = runtime->pointer_y;
    old_output = runtime->pointer_output;
    old_valid = runtime->pointer_valid;
    if (runtime->evdev_mouse_x < 0) runtime->evdev_mouse_x = 0;
    if (runtime->evdev_mouse_y < 0) runtime->evdev_mouse_y = 0;
    {
        int width = runtime->displays[0].bounds.w;
        int height = runtime->displays[0].bounds.h;
        if (runtime->direct_kms) {
            int display_index;
            width = 0;
            height = 0;
            for (display_index = 0; display_index < runtime->output_count;
                 ++display_index) {
                width += runtime->displays[display_index].bounds.w;
                if (runtime->displays[display_index].bounds.h > height)
                    height = runtime->displays[display_index].bounds.h;
            }
        }
        if (width > 0 && runtime->evdev_mouse_x >= width)
            runtime->evdev_mouse_x = (float)(width - 1);
        if (height > 0 && runtime->evdev_mouse_y >= height)
            runtime->evdev_mouse_y = (float)(height - 1);
    }
    runtime->previous_pointer_x = runtime->pointer_x;
    runtime->previous_pointer_y = runtime->pointer_y;
    runtime->previous_pointer_output = runtime->pointer_output;
    runtime->pointer_valid = 1;
    runtime->pointer_output = 0;
    if (runtime->direct_kms) {
        int output_index;
        int x_offset = 0;
        for (output_index = 0; output_index < runtime->output_count;
             ++output_index) {
            if (runtime->evdev_mouse_x >= x_offset &&
                runtime->evdev_mouse_x < x_offset +
                    runtime->displays[output_index].bounds.w) {
                runtime->pointer_output = output_index;
                break;
            }
            x_offset += runtime->displays[output_index].bounds.w;
        }
    }
    runtime->pointer_x = runtime->evdev_mouse_x;
    runtime->pointer_y = runtime->evdev_mouse_y;
    changed = !old_valid || old_x != runtime->pointer_x ||
        old_y != runtime->pointer_y || old_output != runtime->pointer_output;
    if (!changed)
        return;
    runtime->pointer_dirty = 1;
    runtime->render_pending = 1;
    {
        char input_event[384];
        int target_col = 0;
        int target_row = 0;
        BlitSession *routed = input_route_target(runtime, "evdev-pointer");
        BlitSession *target = mouse_target(runtime, runtime->pointer_output,
            runtime->pointer_x, runtime->pointer_y, &target_col, &target_row);
        if (routed != NULL) target = routed;
        snprintf(input_event, sizeof(input_event),
                 "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                 "\"device_id\":\"evdev-pointer\",\"event\":{"
                 "\"kind\":\"pointer_motion\",\"x\":%.1f,"
                 "\"y\":%.1f,\"output\":%d,\"column\":%d,\"row\":%d}}",
                 target != NULL ? target->id : 0ULL,
                 runtime->pointer_x, runtime->pointer_y,
                 runtime->pointer_output, target_col, target_row);
        broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
    }
    if (runtime->output_count > 0 && !runtime->direct_kms)
        SDL_WarpMouseInWindow(runtime->outputs[0].window,
                              runtime->evdev_mouse_x,
                              runtime->evdev_mouse_y);
    send_mouse_motion_report(runtime, 0, runtime->evdev_mouse_x,
                             runtime->evdev_mouse_y);
}

static void broadcast_evdev_pointer(BlitRuntime *runtime, const char *kind,
                                    int button, int pressed, int delta)
{
    char input_event[384];
    int column = 0;
    int row = 0;
    BlitSession *routed = input_route_target(runtime, "evdev-pointer");
    BlitSession *target = mouse_target(runtime, runtime->pointer_output,
        runtime->pointer_x, runtime->pointer_y, &column, &row);
    if (routed != NULL)
        target = routed;
    if (strcmp(kind, "pointer_wheel") == 0) {
        snprintf(input_event, sizeof(input_event),
                 "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                 "\"device_id\":\"evdev-pointer\",\"event\":{"
                 "\"kind\":\"pointer_wheel\",\"delta\":%d,"
                 "\"x\":%.1f,\"y\":%.1f,\"output\":%d}}",
                 target != NULL ? target->id : 0ULL, delta,
                 runtime->pointer_x, runtime->pointer_y,
                 runtime->pointer_output);
    } else {
        snprintf(input_event, sizeof(input_event),
                 "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                 "\"device_id\":\"evdev-pointer\",\"event\":{"
                 "\"kind\":\"pointer_button\",\"button\":%d,"
                 "\"pressed\":%s,\"x\":%.1f,\"y\":%.1f,"
                 "\"output\":%d,\"column\":%d,\"row\":%d}}",
                 target != NULL ? target->id : 0ULL, button,
                 pressed ? "true" : "false", runtime->pointer_x,
                 runtime->pointer_y, runtime->pointer_output, column, row);
    }
    broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
}

static void service_evdev_mouse(BlitRuntime *runtime)
{
    struct input_event event;
    ssize_t received;
    int device_index;

    for (device_index = 0; device_index < runtime->evdev_mouse_count;
         ++device_index) {
        if (runtime->evdev_mouse_fds[device_index] < 0) continue;
        while ((received = read(runtime->evdev_mouse_fds[device_index],
                                &event, sizeof(event))) ==
               (ssize_t)sizeof(event)) {
            if (!runtime->evdev_mouse_reported[device_index]) {
                fprintf(stderr, "blit: evdev mouse events received from %s\n",
                        runtime->evdev_mouse_paths[device_index]);
                runtime->evdev_mouse_reported[device_index] = 1;
            }
            if (event.type == EV_REL) {
                if (event.code == REL_WHEEL || event.code == REL_HWHEEL) {
                    int button;
                    if (event.code == REL_HWHEEL)
                        button = event.value > 0 ? 66 : 67;
                    else
                        button = event.value > 0 ? 64 : 65;
                    broadcast_evdev_pointer(runtime, "pointer_wheel", button,
                                             1, event.value);
                    send_mouse_report(runtime, 0, runtime->evdev_mouse_x,
                                      runtime->evdev_mouse_y, button, 0);
                    continue;
                }
                if (event.code == REL_X)
                    runtime->evdev_mouse_x += event.value;
                else if (event.code == REL_Y)
                    runtime->evdev_mouse_y += event.value;
            } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
                update_evdev_pointer(runtime);
            } else if (event.type == EV_KEY &&
                       (event.code == BTN_LEFT ||
                        event.code == BTN_MIDDLE ||
                        event.code == BTN_RIGHT)) {
                int button;
                if (event.code == BTN_LEFT)
                    button = 0;
                else if (event.code == BTN_MIDDLE)
                    button = 1;
                else
                    button = 2;
                int release = event.value == 0;
                if (!release) runtime->mouse_buttons |= 1U << button;
                broadcast_evdev_pointer(runtime, "pointer_button", button,
                                         !release, 0);
                send_mouse_report(runtime, 0, runtime->evdev_mouse_x,
                                  runtime->evdev_mouse_y, button, release);
                if (release) runtime->mouse_buttons &= ~(1U << button);
            }
        }
    }
}

static BlitSession *keyboard_target(BlitRuntime *runtime)
{
    int column = 0;
    int row = 0;
    BlitSession *session = mouse_target(runtime, runtime->pointer_output,
                                        runtime->pointer_x, runtime->pointer_y,
                                        &column, &row);
    int i;
    BlitSession *routed = input_route_target(runtime, "evdev-keyboard");
    if (routed != NULL)
        return routed;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
        if (runtime->sessions[i].used && runtime->sessions[i].focused &&
            runtime->sessions[i].pty_fd >= 0)
            return &runtime->sessions[i];
    if (session != NULL) return session;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
        if (runtime->sessions[i].used && runtime->sessions[i].pty_fd >= 0)
            return &runtime->sessions[i];
    return NULL;
}

static void send_evdev_key(BlitRuntime *runtime, unsigned short code)
{
    BlitSession *session = keyboard_target(runtime);
    char sequence[32];
    int length = 0;
    if (session == NULL || session->pty_fd < 0) return;
    if (runtime->evdev_ctrl && code >= KEY_A && code <= KEY_Z) {
        sequence[0] = (char)(code - KEY_A + 1);
        length = 1;
    } else if (code >= KEY_A && code <= KEY_Z) {
        sequence[0] = (char)(runtime->evdev_shift ? 'A' : 'a' + code - KEY_A);
        if (runtime->evdev_shift) sequence[0] = (char)('A' + code - KEY_A);
        length = 1;
    } else if (code >= KEY_1 && code <= KEY_9) {
        static const char normal[] = "123456789";
        static const char shifted[] = "!@#$%^&*(";
        sequence[0] = (runtime->evdev_shift ? shifted : normal)[code - KEY_1];
        length = 1;
    } else if (code == KEY_0) {
        sequence[0] = runtime->evdev_shift ? ')' : '0'; length = 1;
    } else if (code == KEY_SPACE) {
        sequence[0] = ' '; length = 1;
    } else if (code == KEY_MINUS || code == KEY_EQUAL ||
               code == KEY_LEFTBRACE || code == KEY_RIGHTBRACE ||
               code == KEY_BACKSLASH || code == KEY_SEMICOLON ||
               code == KEY_APOSTROPHE || code == KEY_GRAVE ||
               code == KEY_COMMA || code == KEY_DOT || code == KEY_SLASH) {
        static const char normal[] = {
            '-', '=', '[', ']', '\\', ';', '\'', '`', ',', '.', '/'
        };
        static const char shifted[] = {
            '_', '+', '{', '}', '|', ':', '"', '~', '<', '>', '?'
        };
        int punctuation = code == KEY_MINUS ? 0 : code == KEY_EQUAL ? 1 :
            code == KEY_LEFTBRACE ? 2 : code == KEY_RIGHTBRACE ? 3 :
            code == KEY_BACKSLASH ? 4 : code == KEY_SEMICOLON ? 5 :
            code == KEY_APOSTROPHE ? 6 : code == KEY_GRAVE ? 7 :
            code == KEY_COMMA ? 8 : code == KEY_DOT ? 9 : 10;
        sequence[0] = (runtime->evdev_shift ? shifted : normal)[punctuation];
        length = 1;
    } else if (code == KEY_KP0 || code == KEY_KP1 || code == KEY_KP2 ||
               code == KEY_KP3 || code == KEY_KP4 || code == KEY_KP5 ||
               code == KEY_KP6 || code == KEY_KP7 || code == KEY_KP8 ||
               code == KEY_KP9) {
        int digit = code == KEY_KP0 ? 0 : code == KEY_KP1 ? 1 :
            code == KEY_KP2 ? 2 : code == KEY_KP3 ? 3 : code == KEY_KP4 ? 4 :
            code == KEY_KP5 ? 5 : code == KEY_KP6 ? 6 : code == KEY_KP7 ? 7 :
            code == KEY_KP8 ? 8 : 9;
        if (session->terminal_application_keypad) {
            sequence[0] = '\033'; sequence[1] = 'O';
            sequence[2] = (char)('p' + digit);
            length = 3;
        } else {
            sequence[0] = (char)('0' + digit);
            length = 1;
        }
    } else if (code == KEY_KPENTER || code == KEY_KPPLUS ||
               code == KEY_KPMINUS || code == KEY_KPASTERISK ||
               code == KEY_KPSLASH || code == KEY_KPDOT) {
        if (session->terminal_application_keypad) {
            sequence[0] = '\033'; sequence[1] = 'O';
            sequence[2] = code == KEY_KPENTER ? 'M' :
                code == KEY_KPPLUS ? 'k' : code == KEY_KPMINUS ? 'm' :
                code == KEY_KPASTERISK ? 'j' : code == KEY_KPSLASH ? 'o' : 'n';
            length = 3;
        } else {
            sequence[0] = code == KEY_KPENTER ? '\r' :
                code == KEY_KPPLUS ? '+' : code == KEY_KPMINUS ? '-' :
                code == KEY_KPASTERISK ? '*' : code == KEY_KPSLASH ? '/' : '.';
            length = 1;
        }
    } else if (code == KEY_ENTER) {
        sequence[0] = '\r'; length = 1;
    } else if (code == KEY_BACKSPACE) {
        sequence[0] = 0x7f; length = 1;
    } else if (code == KEY_TAB) {
        sequence[0] = '\t'; length = 1;
    } else if (code == KEY_ESC) {
        sequence[0] = 0x1b; length = 1;
    } else if (code == KEY_HOME || code == KEY_END) {
        sequence[0] = 0x1b;
        sequence[1] = session->terminal_application_cursor ? 'O' : '[';
        if (session->terminal_application_cursor) {
            sequence[2] = code == KEY_HOME ? 'H' : 'F';
            length = 3;
        } else {
            sequence[2] = code == KEY_HOME ? 'H' : 'F';
            length = 3;
        }
    } else if (code == KEY_INSERT || code == KEY_DELETE ||
               code == KEY_PAGEUP || code == KEY_PAGEDOWN) {
        int number = code == KEY_INSERT ? 2 : code == KEY_DELETE ? 3 :
            code == KEY_PAGEUP ? 5 : 6;
        length = (int)snprintf(sequence, sizeof(sequence), "\033[%d~", number);
    } else if (code >= KEY_F1 && code <= KEY_F12) {
        static const char *function_keys[] = {
            "\033OP", "\033OQ", "\033OR", "\033OS", "\033[15~",
            "\033[17~", "\033[18~", "\033[19~", "\033[20~",
            "\033[21~", "\033[23~", "\033[24~"
        };
        const char *function_key = function_keys[code - KEY_F1];
        length = (int)strlen(function_key);
        memcpy(sequence, function_key, (size_t)length);
    } else if (code == KEY_UP || code == KEY_DOWN || code == KEY_LEFT || code == KEY_RIGHT) {
        sequence[0] = 0x1b;
        sequence[1] = session->terminal_application_cursor ? 'O' : '[';
        sequence[2] = code == KEY_UP ? 'A' : code == KEY_DOWN ? 'B' :
            code == KEY_RIGHT ? 'C' : 'D';
        length = 3;
    }
    if (length > 0) {
        if (runtime->evdev_alt && sequence[0] != 0x1b && length <
            (int)sizeof(sequence) - 1) {
            memmove(sequence + 1, sequence, (size_t)length);
            sequence[0] = 0x1b;
            ++length;
        }
        write(session->pty_fd, sequence, (size_t)length);
    }
}

static void service_evdev_keyboard(BlitRuntime *runtime)
{
    struct input_event event;
    ssize_t received;
    int device_index;
    for (device_index = 0; device_index < runtime->evdev_keyboard_count;
         ++device_index) {
        if (runtime->evdev_keyboard_fds[device_index] < 0) continue;
        while ((received = read(runtime->evdev_keyboard_fds[device_index],
                                &event, sizeof(event))) ==
               (ssize_t)sizeof(event)) {
            if (event.type != EV_KEY) continue;
            if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
                runtime->evdev_shift = event.value != 0;
            else if (event.code == KEY_LEFTCTRL || event.code == KEY_RIGHTCTRL)
                runtime->evdev_ctrl = event.value != 0;
            else if (event.code == KEY_LEFTALT || event.code == KEY_RIGHTALT)
                runtime->evdev_alt = event.value != 0;
            if (event.value != 0) {
                if (getenv("BLIT_TRACE_KEYS") != NULL)
                    fprintf(stderr, "blit: key code=%u value=%d shift=%d ctrl=%d alt=%d\n",
                            (unsigned int)event.code, event.value,
                            runtime->evdev_shift, runtime->evdev_ctrl,
                            runtime->evdev_alt);
                if (!runtime->evdev_keyboard_reported[device_index]) {
                    fprintf(stderr, "blit: evdev keyboard events received from %s\n",
                            runtime->evdev_keyboard_paths[device_index]);
                    runtime->evdev_keyboard_reported[device_index] = 1;
                }
                send_evdev_key(runtime, event.code);
            }
            {
                char input_event[256];
                BlitSession *target = keyboard_target(runtime);
                snprintf(input_event, sizeof(input_event),
                         "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                         "\"device_id\":\"evdev-keyboard\",\"event\":{"
                         "\"kind\":\"key\",\"code\":%u,\"pressed\":%s,"
                         "\"repeat\":%s,\"shift\":%s,\"ctrl\":%s,\"alt\":%s}}",
                         target != NULL ? target->id : 0ULL,
                         (unsigned int)event.code, event.value != 0 ? "true" : "false",
                         event.value == 2 ? "true" : "false",
                         runtime->evdev_shift ? "true" : "false",
                         runtime->evdev_ctrl ? "true" : "false",
                         runtime->evdev_alt ? "true" : "false");
                broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
            }
        }
    }
}

static void service_sdl_events(BlitRuntime *runtime)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            int button = event.button.button == 1 ? 0 :
                event.button.button == 2 ? 1 :
                event.button.button == 3 ? 2 : -1;
            if (button >= 0) {
                runtime->mouse_buttons |= 1U << button;
                send_mouse_report(runtime, event.button.windowID,
                                  event.button.x, event.button.y, button, 0);
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            int button = event.button.button == 1 ? 0 :
                event.button.button == 2 ? 1 :
                event.button.button == 3 ? 2 : -1;
            if (button >= 0) {
            send_mouse_report(runtime, event.button.windowID,
                              event.button.x, event.button.y, button, 1);
                runtime->mouse_buttons &= ~(1U << button);
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            runtime->previous_pointer_x = runtime->pointer_x;
            runtime->previous_pointer_y = runtime->pointer_y;
            runtime->previous_pointer_output = runtime->pointer_output;
            runtime->pointer_valid = 1;
            runtime->pointer_output = mouse_output_index(runtime,
                                                         event.motion.windowID);
            runtime->pointer_x = event.motion.x;
            runtime->pointer_y = event.motion.y;
            runtime->pointer_dirty = 1;
            runtime->render_pending = 1;
            send_mouse_motion_report(runtime, event.motion.windowID,
                                     event.motion.x, event.motion.y);
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            int button = event.wheel.y > 0 ? 64 : 65;
            send_mouse_report(runtime, event.wheel.windowID,
                              event.wheel.mouse_x, event.wheel.mouse_y,
                              button, 0);
        }
    }
}

int main(int argc, char **argv)
{
    int port = 8080;
    int server_fd;
    const char *bind_address = getenv("BLIT_BIND_ADDRESS");
    struct sockaddr_in address;
    static BlitRuntime runtime;

    if (argc > 1)
        port = atoi(argv[1]);
    if (port < 1 || port > 65535) {
        fprintf(stderr, "usage: %s [port]\n", argv[0]);
        return 2;
    }
    if (bind_address == NULL || bind_address[0] == '\0')
        bind_address = "127.0.0.1";
    memset(&runtime, 0, sizeof(runtime));
    runtime.font_scale = 1;
    runtime.render_interval_ms = 16;
    {
        const char *font_scale = getenv("BLIT_FONT_SCALE");
        const char *render_interval = getenv("BLIT_RENDER_INTERVAL_MS");
        if (font_scale != NULL)
            runtime.font_scale = atoi(font_scale);
        if (render_interval != NULL)
            runtime.render_interval_ms = atoi(render_interval);
        if (runtime.font_scale < 1 || runtime.font_scale > 3)
            runtime.font_scale = 1;
        if (runtime.render_interval_ms < 1 || runtime.render_interval_ms > 100)
            runtime.render_interval_ms = 16;
    }
    {
        int input_index;
        for (input_index = 0; input_index < BLIT_MAX_INPUT_DEVICES;
             ++input_index) {
            runtime.evdev_mouse_fds[input_index] = -1;
            runtime.evdev_keyboard_fds[input_index] = -1;
        }
    }
    runtime.next_session_id = 1;
    runtime.listen_port = port;
    runtime.next_frame_id = 1;
    runtime.next_event_sequence = 1;
    runtime.topology_revision = 1;
    runtime.input_routes_revision = 1;
    {
        const char *auth_token = getenv("BLIT_AUTH_TOKEN");
        if (auth_token != NULL)
            snprintf(runtime.auth_token, sizeof(runtime.auth_token), "%s",
                     auth_token);
    }
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);
    discover_displays(&runtime);
    if (runtime.direct_kms && runtime.display_count == 0) {
        fprintf(stderr, "blit: direct KMS backend did not find a usable display\n");
        if (runtime.kms != NULL) blit_kms_close(runtime.kms);
        return 1;
    }
    discover_panes(&runtime);
    {
        const char *mouse_device = getenv("BLIT_EVDEV_MOUSE");
        runtime.evdev_mouse_count = open_evdev_mice(
            mouse_device, runtime.evdev_mouse_fds, runtime.evdev_mouse_paths,
            &runtime.evdev_mouse_count);
        if (runtime.evdev_mouse_count > 0) {
            int input_index;
            for (input_index = 0; input_index < runtime.evdev_mouse_count;
                 ++input_index)
                fprintf(stderr, "blit: evdev mouse[%d]=%s fd=%d\n",
                        input_index, runtime.evdev_mouse_paths[input_index],
                        runtime.evdev_mouse_fds[input_index]);
        } else
            fprintf(stderr, "blit: no usable evdev mouse found\n");
    }
    {
        runtime.evdev_keyboard_count = open_evdev_keyboards(
            runtime.evdev_keyboard_fds, runtime.evdev_keyboard_paths,
            &runtime.evdev_keyboard_count);
        if (runtime.evdev_keyboard_count > 0) {
            int input_index;
            for (input_index = 0; input_index < runtime.evdev_keyboard_count;
                 ++input_index)
                fprintf(stderr, "blit: evdev keyboard[%d]=%s fd=%d\n",
                        input_index, runtime.evdev_keyboard_paths[input_index],
                        runtime.evdev_keyboard_fds[input_index]);
        } else
            fprintf(stderr, "blit: no usable evdev keyboard found\n");
    }
    if (runtime.sdl_ready || runtime.direct_kms)
        initialize_outputs(&runtime);
    if (runtime.output_count > 0) {
        runtime.pointer_valid = 1;
        runtime.pointer_output = 0;
        runtime.pointer_x = runtime.displays[0].bounds.w > 0 ?
            runtime.displays[0].bounds.w / 2.0f : 0.0f;
        runtime.pointer_y = runtime.displays[0].bounds.h > 0 ?
            runtime.displays[0].bounds.h / 2.0f : 0.0f;
        runtime.pointer_dirty = 1;
        runtime.render_pending = 1;
    }

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

        for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
            refresh_session_state(&runtime.sessions[i]);

        if (runtime.sdl_ready && !runtime.direct_kms)
            service_sdl_events(&runtime);
        service_evdev_mouse(&runtime);
        service_evdev_keyboard(&runtime);
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
        timeout.tv_sec = 0;
        timeout.tv_usec = 10000;
        ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
        if (ready > 0 && FD_ISSET(server_fd, &readable)) {
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
        if (ready > 0) {
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
        if (runtime.render_pending &&
            !runtime_has_synchronized_output(&runtime) &&
            (SDL_GetTicks() - runtime.render_last_ticks >=
             (uint32_t)runtime.render_interval_ms)) {
            render_current_surfaces(&runtime);
            runtime.render_pending = 0;
            runtime.render_last_ticks = SDL_GetTicks();
        }
    }
    close(server_fd);
    stop_sessions(&runtime);
    {
        int input_index;
        for (input_index = 0; input_index < runtime.evdev_mouse_count;
             ++input_index)
            if (runtime.evdev_mouse_fds[input_index] >= 0)
                close(runtime.evdev_mouse_fds[input_index]);
        for (input_index = 0; input_index < runtime.evdev_keyboard_count;
             ++input_index)
            if (runtime.evdev_keyboard_fds[input_index] >= 0)
                close(runtime.evdev_keyboard_fds[input_index]);
    }
    destroy_outputs(&runtime);
    if (runtime.sdl_ready)
        SDL_Quit();
    return 0;
}
