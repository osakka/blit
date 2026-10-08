#include <SDL3/SDL.h>
#include <linux/input.h>
#include "blit_kms.h"
#include "blit_font.h"
#include "blit_graphics.h"
#include "blit_unicode_width.h"

#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <netinet/in.h>
#include <signal.h>
#include <stdint.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/ioctl.h>
#include <sys/inotify.h>
#include <sys/select.h>
#include <sys/socket.h>
#include <sys/stat.h>
#include <sys/types.h>
#include <sys/wait.h>
#include <sys/un.h>
#include <time.h>
#include <unistd.h>
#include <pty.h>

extern char **environ;

#define BLIT_MAX_DISPLAYS 16
#define BLIT_MAX_PANES 64
#define BLIT_MAX_SESSIONS 16
#define BLIT_MAX_STREAMS 16
#define BLIT_MAX_HTTP_CLIENTS 32
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
#define BLIT_MAX_RUNTIME_LOGS 64
#define BLIT_MAX_TRACE_EVENTS 128
#define BLIT_PIXEL_FRAME_QUIET_US 100000ULL
/* A PTY is a byte stream, while a terminal desktop frame is commonly emitted
   as many short writes.  Hold presentation briefly after the latest read so
   the renderer never exposes the clear/redraw interval between those writes. */
#define BLIT_TERMINAL_FRAME_QUIET_US 8000ULL
#define BLIT_MAX_SCROLLBACK_LINES 1000
#define BLIT_MAX_OVERLAYS 16
#define BLIT_UPDATE_IDLE_TIMEOUT_US 30000000ULL
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
#define BLIT_MAX_COMBINING 16

#define BLIT_SDL_GRAPHICS_CACHE 32

typedef struct {
    unsigned long long id;
    char name[128];
    SDL_Rect bounds;
    double refresh_hz;
    int mode_count;
    int active_mode;
    BlitKmsMode modes[BLIT_KMS_MAX_MODES];
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
    int width;
    char *cells;
    uint32_t *codepoints;
    uint32_t *combining;
    unsigned char *combining_count;
    uint32_t *foreground;
    uint32_t *background;
    uint32_t *underline_color;
    uint16_t *attributes;
    uint16_t *links;
} BlitScrollbackLine;

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
    uint16_t links[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    char alternate_cells[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_codepoints[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_combining[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS][BLIT_MAX_COMBINING];
    unsigned char alternate_combining_count[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_foreground[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_background[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint32_t alternate_underline_color[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint16_t alternate_attributes[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    uint16_t alternate_links[BLIT_MAX_CELL_ROWS][BLIT_MAX_CELL_COLS];
    int cursor_x;
    int cursor_y;
    int cursor_visible;
    int cursor_style;
    int scroll_top;
    int scroll_bottom;
    unsigned char tab_stops[BLIT_MAX_CELL_COLS];
    BlitScrollbackLine scrollback[BLIT_MAX_SCROLLBACK_LINES];
    int scrollback_count;
    int scrollback_start;
    int scrollback_view;
    int scrollback_view_active;
    int scrollback_saved_cursor_visible;
    int damage_valid;
    int damage_x;
    int damage_y;
    int damage_width;
    int damage_height;
    /* External cell producers may submit one logical frame in bounded HTTP
       chunks. Keep those chunks out of the renderer until the commit chunk
       arrives, otherwise Hibr's wallpaper/menu redraw is visibly torn. */
    int frame_open;
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
    char owner_session_id[64];
    unsigned int subscriptions;
    unsigned char receive_buffer[4096];
    unsigned char *large_receive_buffer;
    size_t large_receive_capacity;
    size_t receive_length;
    unsigned char fragment_buffer[4096];
    size_t fragment_length;
    unsigned char fragment_opcode;
    unsigned long long pixel_frame_session_id;
    unsigned int pixel_frame_image_id;
    size_t pixel_frame_expected;
    size_t pixel_frame_received;
    int pixel_frame_active;
    int pixel_frame_ack;
    int pixel_frame_chunked;
} BlitStream;

typedef struct {
    int used;
    int fd;
    size_t length;
    uint64_t accepted_us;
    char request[BLIT_REQUEST_SIZE];
    int update_fd;
    size_t update_expected;
    size_t update_received;
    uint64_t update_last_progress_us;
    char update_path[256];
    char update_sha256[65];
    int screenshot_output_index;
} BlitHttpClient;

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
    SDL_Texture *scene_texture;
    SDL_Texture *glyph_atlas;
    SDL_Texture *graphics_textures[BLIT_SDL_GRAPHICS_CACHE];
    const BlitGraphicsImage *graphics_sources[BLIT_SDL_GRAPHICS_CACHE];
    unsigned long long graphics_generations[BLIT_SDL_GRAPHICS_CACHE];
    int graphics_widths[BLIT_SDL_GRAPHICS_CACHE];
    int graphics_heights[BLIT_SDL_GRAPHICS_CACHE];
    int graphics_formats[BLIT_SDL_GRAPHICS_CACHE];
    unsigned long long graphics_cache_hits;
    unsigned long long graphics_texture_uploads;
    double last_graphics_upload_ms;
    double max_graphics_upload_ms;
    unsigned long long last_graphics_signature;
    unsigned long long last_topology_revision;
    int last_target_count;
    int glyph_atlas_columns;
    SDL_Vertex *glyph_vertices;
    int *glyph_indices;
    int glyph_vertex_count;
    int glyph_index_count;
    int glyph_capacity;
    SDL_Vertex *background_vertices;
    int *background_indices;
    int background_vertex_count;
    int background_index_count;
    int background_capacity;
    unsigned long long background_batch_submissions;
    unsigned long long background_quads;
    SDL_Surface *screenshot_surface;
    unsigned long long display_id;
    int initialized;
    int vsync_enabled;
    int render_width;
    int render_height;
    int viewport_width;
    int viewport_height;
    float scale_x;
    float scale_y;
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
    unsigned long long revision;
    int enabled;
    char preset[32];
    int intensity;
} BlitEffectState;

typedef struct {
    int used;
    unsigned long long id;
    unsigned long long revision;
    char display_id[128];
    int x;
    int y;
    int width;
    int height;
    uint32_t color;
    int opacity;
} BlitOverlay;

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
    int child_exited;
    int surface_retired;
    int focused;
    int surface_mode;
    /* External Hibr surfaces need the same attach-time reassert that a real
       terminal performs after establishing its PTY geometry.  Schedule it
       after startup output has made the desktop session live; do not inject a
       user key such as Escape, which has desktop meaning. */
    int attach_reassert_pending;
    uint64_t attach_reassert_due_us;
    int terminal_escape;
    /* Hibr emits a single SGR containing bold plus truecolor foreground and
       background (three decimal components each).  A 32-byte buffer cuts
       that sequence in the middle and leaks its tail as terminal text. */
    char terminal_csi[128];
    int terminal_csi_length;
    /* PTY reads may split a UTF-8 scalar between system calls. Keep a valid
       leading prefix until the next read instead of converting it to '?'. */
    unsigned char terminal_utf8_pending[4];
    int terminal_utf8_pending_length;
    int terminal_string_type;
    char terminal_string[4096];
    int terminal_string_length;
    char terminal_title[128];
    char terminal_clipboard[4096];
    char terminal_notification[1024];
    char terminal_default_foreground[32];
    char terminal_default_background[32];
    char terminal_link_uris[32][256];
    int terminal_link_count;
    uint16_t terminal_link;
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
    int terminal_wrap_pending;
    int terminal_origin_mode;
    int terminal_autowrap;
    int terminal_newline_mode;
    int terminal_insert_mode;
    int terminal_alternate_mode;
    int terminal_focus_reporting;
    int terminal_bracketed_paste;
    int terminal_mouse_mode;
    int terminal_mouse_sgr;
    int terminal_synchronized;
    int terminal_frame_batch;
    /* Pixel geometry advertised to child programs and used by Kitty/Sixel.
       This is the renderer's actual cell size, including font scaling. */
    int terminal_cell_width;
    int terminal_cell_height;
    char terminal_term[64];
    uint32_t terminal_underline_color;
    int terminal_underline_color_set;
    BlitGraphicsImage graphics[BLIT_GRAPHICS_MAX_IMAGES];
    unsigned long graphics_rejected;
    unsigned long graphics_evicted;
    char graphics_last_error[64];
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
    uint64_t terminal_frame_quiet_until_us;
    uint64_t render_last_us;
    int render_interval_ms;
    int render_effective_interval_ms;
    uint64_t render_next_us[BLIT_MAX_DISPLAYS];
    unsigned long render_missed_deadlines[BLIT_MAX_DISPLAYS];
    int test_refresh_override;
    int session_count;
    unsigned long long next_session_id;
    BlitSession sessions[BLIT_MAX_SESSIONS];
    /* Terminal grids are allocated only for live sessions.  Keeping the
       maximum-size grid inline for every slot made an idle gateway commit a
       large amount of memory before it had a client. */
    BlitSurface *surfaces[BLIT_MAX_SESSIONS];
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
    int evdev_gamepad_fds[BLIT_MAX_INPUT_DEVICES];
    char evdev_gamepad_paths[BLIT_MAX_INPUT_DEVICES][128];
    int evdev_gamepad_count;
    int evdev_gamepad_reported[BLIT_MAX_INPUT_DEVICES];
    int evdev_shift;
    int evdev_ctrl;
    int evdev_alt;
    int evdev_watch_fd;
    int evdev_rescan_requested;
    float evdev_mouse_x;
    float evdev_mouse_y;
    BlitStream streams[BLIT_MAX_STREAMS];
    BlitHttpClient http_clients[BLIT_MAX_HTTP_CLIENTS];
    BlitInputRoute input_routes[16];
    unsigned long long input_routes_revision;
    BlitOutput outputs[BLIT_MAX_DISPLAYS];
    BlitIdempotency idempotency[BLIT_MAX_IDEMPOTENCY];
    BlitEventRecord event_history[BLIT_MAX_EVENT_HISTORY];
    unsigned int next_event_history;
    BlitEffectState effect;
    BlitOverlay overlays[BLIT_MAX_OVERLAYS];
    unsigned long long next_overlay_id;
    char auth_token[256];
    char command_allowlist[1024];
    int output_count;
    int sdl_ready;
    int direct_kms;
    int sdl_vsync_failures;
    char sdl_renderer_name[64];
    BlitKms *kms;
    BlitFont font;
    char sdl_screenshot_dir[256];
    unsigned long sdl_screenshot_after;
    unsigned long sdl_frame;
    uint64_t started_us;
    unsigned long long diagnostic_frames;
    double diagnostic_last_frame_gap_ms;
    double diagnostic_max_frame_gap_ms;
    uint64_t diagnostic_frame_rate_started_us;
    uint64_t diagnostic_frame_rate_last_us;
    unsigned long long diagnostic_frame_rate_frames;
    unsigned long long diagnostic_frame_rate_last_frames;
    double diagnostic_frame_rate_hz;
    unsigned long long diagnostic_frame_rate_window_ms;
    double diagnostic_frame_rate_max_render_ms;
    unsigned long long diagnostic_frame_rate_missed_deadlines;
    unsigned long long diagnostic_frame_rate_window_sequence;
    unsigned long long diagnostic_frame_rate_window_missed_start;
    double diagnostic_frame_rate_window_max_render_ms;
    unsigned long long pixel_frames_accepted;
    unsigned long long pixel_frame_errors;
    unsigned long long pixel_frames_pending[BLIT_MAX_DISPLAYS];
    uint64_t pixel_frames_last_accepted_us;
    unsigned long long diagnostic_loop_iterations;
    unsigned long long diagnostic_terminal_reads;
    unsigned long long diagnostic_present_calls;
    uint64_t diagnostic_last_loop_us;
    double diagnostic_last_loop_gap_ms;
    double diagnostic_max_loop_gap_ms;
    double diagnostic_last_select_ms;
    double diagnostic_max_select_ms;
    double diagnostic_last_kms_present_ms;
    double diagnostic_max_kms_present_ms;
    double diagnostic_last_sdl_present_ms;
    double diagnostic_max_sdl_present_ms;
    double diagnostic_last_sdl_events_ms;
    double diagnostic_max_sdl_events_ms;
    double diagnostic_last_evdev_scan_ms;
    double diagnostic_max_evdev_scan_ms;
    double diagnostic_last_render_ms;
    double diagnostic_max_render_ms;
    double diagnostic_last_terminal_ms;
    double diagnostic_max_terminal_ms;
    unsigned long long diagnostic_terminal_bytes;
    unsigned long long diagnostic_pointer_cache_hits;
    unsigned long long diagnostic_pointer_cache_misses;
    unsigned long long diagnostic_input_events;
    double diagnostic_last_input_dispatch_ms;
    double diagnostic_max_input_dispatch_ms;
    unsigned long long diagnostic_evdev_mouse_events;
    unsigned long long diagnostic_evdev_mouse_syncs;
    unsigned long long diagnostic_evdev_mouse_motion_events;
    unsigned long long diagnostic_evdev_mouse_wheel_events;
    unsigned long long diagnostic_evdev_mouse_button_events;
    uint64_t diagnostic_last_evdev_mouse_event_us;
    char diagnostic_last_evdev_mouse_path[128];
    unsigned long long diagnostic_stream_dropped_events;
    unsigned long long diagnostic_stream_backpressure_disconnects;
    unsigned long long diagnostic_stream_queue_peak_bytes;
    unsigned long long diagnostic_log_sequence;
    int diagnostic_log_count;
    int diagnostic_log_next;
    char diagnostic_logs[BLIT_MAX_RUNTIME_LOGS][512];
    unsigned long long diagnostic_trace_sequence;
    int diagnostic_trace_count;
    int diagnostic_trace_next;
    char diagnostic_trace[BLIT_MAX_TRACE_EVENTS][768];
    int diagnostic_critical_trace_count;
    int diagnostic_critical_trace_next;
    char diagnostic_critical_trace[BLIT_MAX_TRACE_EVENTS][768];
    int sdl_screenshot_written;
    uint32_t screenshot_requested_mask;
    char sdl_error[256];
    int listen_port;
    char update_token[256];
    char update_state[32];
    char update_detail[256];
} BlitRuntime;

static unsigned long long blit_graphics_generation;

static unsigned long long next_graphics_generation(void)
{
    ++blit_graphics_generation;
    if (blit_graphics_generation == 0)
        ++blit_graphics_generation;
    return blit_graphics_generation;
}

static int runtime_terminal_cell_width(const BlitRuntime *runtime)
{
    int value = runtime->direct_kms ? blit_kms_cell_width(runtime->kms) :
        blit_font_width(&runtime->font);
    if (value < 1) value = 8;
    return value * (runtime->font_scale > 0 ? runtime->font_scale : 1);
}

static int runtime_terminal_cell_height(const BlitRuntime *runtime)
{
    int value = runtime->direct_kms ? blit_kms_cell_height(runtime->kms) :
        blit_font_height(&runtime->font);
    if (value < 1) value = 16;
    return value * (runtime->font_scale > 0 ? runtime->font_scale : 1);
}

static int display_index_for_output(const BlitRuntime *runtime,
                                    int output_index);
static int output_index_for_display_index(const BlitRuntime *runtime,
                                          int display_index);

static volatile sig_atomic_t blit_running = 1;
static volatile sig_atomic_t blit_restart_requested = 0;
#ifndef BLIT_BUILD_VERSION
#define BLIT_BUILD_VERSION "development"
#endif
static char blit_version[64] = BLIT_BUILD_VERSION;

static void render_outputs(BlitRuntime *runtime, const BlitFramePlan *plan,
                           uint32_t output_mask);
static SDL_Texture *create_sdl_glyph_atlas(SDL_Renderer *renderer,
                                           const BlitFont *font,
                                           int *columns_out);
static const char *pane_surface_id(const BlitRuntime *runtime,
                                   const BlitPane *pane);
static void discover_panes(BlitRuntime *runtime);
static void focus_session(BlitRuntime *runtime, BlitSession *target);
static void disable_input_routes_for_target(BlitRuntime *runtime,
                                            const char *target_id);
static int activate_direct_kms_fallback(BlitRuntime *runtime);
static void destroy_sdl_graphics_cache(BlitOutput *output);
static uint64_t monotonic_us(void);
static void diagnostics_json(const BlitRuntime *runtime, char *body,
                             size_t capacity);
static void remove_http_client(BlitRuntime *runtime, int index);
static void terminal_clear_cell(BlitSurface *surface, int x, int y);
static void terminal_clear_wide_overlap(BlitSurface *surface, int x, int y);
static void mark_cursor_damage(BlitSurface *surface, int x, int y);
static void mark_surface_full_damage(BlitSurface *surface);
static void terminal_clear_active_screen(BlitSurface *surface);
static BlitSurface *find_surface_id(BlitRuntime *runtime,
                                    unsigned long long surface_id);
static BlitSession *find_session_id(BlitRuntime *runtime,
                                    unsigned long long session_id);
static BlitGraphicsImage *graphics_image_find(BlitSession *session,
                                              unsigned int id);
static void graphics_image_release(BlitGraphicsImage *image);
static int surface_targets_output(BlitRuntime *runtime,
                                  BlitSurface *surface, int output_index);
static void pixel_frame_reset(BlitRuntime *runtime, BlitStream *stream,
                              int release_image);
static void update_status_json(const BlitRuntime *runtime, char *body,
                               size_t capacity);
static int update_authorized(const BlitRuntime *runtime, const char *request);
static void cancel_update_upload(BlitRuntime *runtime, int fd,
                                 const char *request);
static void reset_update_state(BlitRuntime *runtime, int fd,
                               const char *request);
static int terminate_session_process(pid_t process_id);
static void remove_session_streams(BlitRuntime *runtime,
                                   const char *session_id);
static void send_mouse_report(BlitRuntime *runtime, SDL_WindowID window_id,
                              float pixel_x, float pixel_y, int button,
                              int release);
static void update_pointer_position(BlitRuntime *runtime,
                                    const char *device_id);
static void broadcast_evdev_pointer(BlitRuntime *runtime, const char *kind,
                                    int button, int pressed, int delta,
                                    const char *device_id);
static void dispatch_pointer_button(BlitRuntime *runtime, int button,
                                    int pressed, const char *device_id);

static void effect_json(const BlitRuntime *runtime, char *body,
                        size_t capacity)
{
    snprintf(body, capacity,
             "{\"revision\":%llu,\"enabled\":%s,"
             "\"preset\":\"%s\",\"intensity\":%d,"
             "\"backend\":\"%s\"}",
             runtime->effect.revision,
             runtime->effect.enabled ? "true" : "false",
             runtime->effect.preset, runtime->effect.intensity,
             runtime->direct_kms ? "drm-kms" :
                 (runtime->sdl_ready ? "sdl3" : "unavailable"));
}

static void overlay_json(const BlitOverlay *overlay, char *body,
                         size_t capacity)
{
    snprintf(body, capacity,
             "{\"id\":\"overlay-%llu\",\"revision\":%llu,"
             "\"display_id\":\"%s\",\"bounds\":{\"x\":%d,"
             "\"y\":%d,\"width\":%d,\"height\":%d},"
             "\"color\":\"#%06x\",\"opacity\":%d}",
             overlay->id, overlay->revision, overlay->display_id,
             overlay->x, overlay->y, overlay->width, overlay->height,
             (unsigned int)(overlay->color & 0xffffffU), overlay->opacity);
}

static void overlays_json(const BlitRuntime *runtime, char *body,
                          size_t capacity)
{
    size_t used = 0;
    int i;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"backend\":\"%s\","
                             "\"overlays\":[",
                             runtime->direct_kms ? "drm-kms" :
                                 (runtime->sdl_ready ? "sdl3" : "unavailable"));
    for (i = 0; i < BLIT_MAX_OVERLAYS; ++i) {
        char item[768];
        if (!runtime->overlays[i].used) continue;
        overlay_json(&runtime->overlays[i], item, sizeof(item));
        used += (size_t)snprintf(body + used, capacity - used, "%s%s",
                                 used > 0 && body[used - 1] != '[' ? "," : "",
                                 item);
    }
    snprintf(body + used, capacity - used, "]}");
}

static void render_pointer(BlitRuntime *runtime, int output_index)
{
    SDL_Renderer *renderer;
    int display_index;
    float x;
    float y;

    if (!runtime->pointer_valid || runtime->pointer_output != output_index)
        return;
    display_index = display_index_for_output(runtime, output_index);
    if (display_index < 0)
        return;
    renderer = runtime->outputs[output_index].renderer;
    x = (runtime->pointer_x - runtime->displays[display_index].bounds.x) /
        runtime->font_scale;
    y = (runtime->pointer_y - runtime->displays[display_index].bounds.y) /
        runtime->font_scale;
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

static int refresh_period_ms(double refresh_hz)
{
    int period;

    if (refresh_hz < 1.0)
        refresh_hz = 60.0;
    /* Floor the period so rounding can never turn a requested refresh into
       an intentionally slower scheduler.  The physical output still limits
       visible presentation to its selected mode and vblank. */
    period = (int)(1000.0 / refresh_hz);
    if (period < 1)
        period = 1;
    return period;
}

static int display_index_for_output(const BlitRuntime *runtime,
                                    int output_index)
{
    int i;
    if (output_index < 0 || output_index >= runtime->output_count)
        return -1;
    for (i = 0; i < runtime->display_count; ++i)
        if (runtime->displays[i].id == runtime->outputs[output_index].display_id)
            return i;
    return output_index < runtime->display_count ? output_index : -1;
}

static int output_index_for_display_index(const BlitRuntime *runtime,
                                          int display_index)
{
    int output_index;

    if (display_index < 0 || display_index >= runtime->display_count)
        return -1;
    for (output_index = 0; output_index < runtime->output_count;
         ++output_index)
        if (runtime->outputs[output_index].display_id ==
            runtime->displays[display_index].id)
            return output_index;
    return -1;
}

static double output_refresh_hz(const BlitRuntime *runtime, int output_index)
{
    int display_index = display_index_for_output(runtime, output_index);
    return display_index >= 0 ? runtime->displays[display_index].refresh_hz : 60.0;
}

static void apply_test_refresh_override(BlitRuntime *runtime)
{
    const char *configured = getenv("BLIT_TEST_REFRESH_HZ");
    const char *cursor;
    int count = 0;
    double values[BLIT_MAX_DISPLAYS];

    if (getenv("BLIT_TEST_MODE") == NULL ||
        strcmp(getenv("BLIT_TEST_MODE"), "1") != 0 ||
        configured == NULL || configured[0] == '\0')
        return;
    cursor = configured;
    while (*cursor != '\0' && count < runtime->display_count) {
        char *end;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        values[count] = strtod(cursor, &end);
        if (end == cursor || values[count] < 1.0)
            return;
        cursor = end;
        while (*cursor == ' ' || *cursor == '\t') ++cursor;
        ++count;
        if (*cursor == ',') ++cursor;
        else if (*cursor != '\0') return;
    }
    if (count != runtime->display_count || *cursor != '\0')
        return;
    for (count = 0; count < runtime->display_count; ++count)
        runtime->displays[count].refresh_hz = values[count];
    runtime->test_refresh_override = 1;
    fprintf(stderr, "blit: test refresh override active: %s\n", configured);
}

static int output_render_period_ms(const BlitRuntime *runtime, int output_index)
{
    int refresh_period;

    refresh_period = refresh_period_ms(output_refresh_hz(runtime, output_index));
    /* The configured interval may make us render more often, but must never
       impose a slower cadence than the selected display mode. */
    return runtime->render_interval_ms < refresh_period ?
        runtime->render_interval_ms : refresh_period;
}

static uint64_t monotonic_us(void)
{
    struct timespec now;

    if (clock_gettime(CLOCK_MONOTONIC, &now) != 0)
        return 0;
    return (uint64_t)now.tv_sec * 1000000ULL +
        (uint64_t)now.tv_nsec / 1000ULL;
}

static uint64_t output_render_period_us(const BlitRuntime *runtime,
                                        int output_index)
{
    double refresh_hz = output_refresh_hz(runtime, output_index);
    double period_us;
    uint64_t configured_period_us;

    if (refresh_hz < 1.0)
        refresh_hz = 60.0;
    period_us = 1000000.0 / refresh_hz;
    configured_period_us = (uint64_t)runtime->render_interval_ms * 1000ULL;
    /* A configured interval may request more work, never less.  Flooring the
       hardware period avoids an intentional under-run caused by fractional
       millisecond refresh periods; the page-flip/vblank remains authoritative
       for visible presentation. */
    if (configured_period_us < (uint64_t)period_us)
        period_us = (double)configured_period_us;
    if (period_us < 1.0)
        period_us = 1.0;
    return (uint64_t)period_us;
}

static void reset_render_schedule(BlitRuntime *runtime, uint64_t now)
{
    int i;
    int minimum = 0;

    for (i = 0; i < BLIT_MAX_DISPLAYS; ++i)
        runtime->render_next_us[i] = 0;
    for (i = 0; i < runtime->output_count; ++i) {
        int period = output_render_period_ms(runtime, i);
        runtime->render_next_us[i] = now;
        if (minimum == 0 || period < minimum)
            minimum = period;
    }
    runtime->render_effective_interval_ms = minimum > 0 ? minimum :
        runtime->render_interval_ms;
}

static uint32_t all_output_mask(const BlitRuntime *runtime)
{
    if (runtime->output_count >= 32)
        return 0xffffffffU;
    return runtime->output_count > 0 ?
        (1U << runtime->output_count) - 1U : 0;
}

static uint32_t due_output_mask(const BlitRuntime *runtime, uint64_t now)
{
    uint32_t mask = 0;
    int i;

    for (i = 0; i < runtime->output_count; ++i) {
        if (runtime->render_next_us[i] == 0 ||
            now >= runtime->render_next_us[i])
            mask |= 1U << i;
    }
    /* Pointer damage belongs only to the output that lost the pointer and the
       output that received it. Redrawing every display for one motion makes
       multi-display pointer input needlessly expensive and visibly choppy. */
    if (runtime->pointer_dirty) {
        if (runtime->previous_pointer_output >= 0 &&
            runtime->previous_pointer_output < runtime->output_count)
            mask |= 1U << runtime->previous_pointer_output;
        if (runtime->pointer_output >= 0 &&
            runtime->pointer_output < runtime->output_count)
            mask |= 1U << runtime->pointer_output;
    }
    for (i = 0; i < runtime->output_count; ++i)
        if (runtime->pixel_frames_pending[i] > 0)
            mask |= 1U << i;
    return mask;
}

static void advance_render_schedule(BlitRuntime *runtime, uint64_t now,
                                    uint32_t output_mask)
{
    int i;
    for (i = 0; i < runtime->output_count; ++i) {
        uint64_t period;
        uint64_t late;
        uint64_t missed;
        if (!(output_mask & (1U << i)))
            continue;
        period = output_render_period_us(runtime, i);
        if (runtime->render_next_us[i] == 0 ||
            now < runtime->render_next_us[i]) {
            runtime->render_next_us[i] = now + period;
            continue;
        }
        late = now - runtime->render_next_us[i];
        missed = late / period;
        runtime->render_missed_deadlines[i] += (unsigned long)missed;
        /* Preserve the output's phase.  Advancing from `now` would make every
           late frame permanently shift the cadence and amplify jitter. */
        runtime->render_next_us[i] += (missed + 1) * period;
    }
}

static int render_wait_ms(const BlitRuntime *runtime, uint64_t now)
{
    int wait_ms = 10;
    int i;

    if (!runtime->render_pending)
        return wait_ms;
    if (runtime->terminal_frame_quiet_until_us > now) {
        uint64_t delta = runtime->terminal_frame_quiet_until_us - now;
        wait_ms = (int)((delta + 999ULL) / 1000ULL);
        if (wait_ms < 1) wait_ms = 1;
    }
    if (runtime->pointer_dirty) {
        if (runtime->terminal_frame_quiet_until_us > now)
            return wait_ms;
        /* Direct KMS may still own the output's back buffer until its page
           flip event arrives.  A zero-timeout select while that flip is
           pending spins the gateway at 100% CPU and prevents the bounded
           flip-timeout path from being serviced.  SDL can present the cached
           scene immediately; KMS yields briefly and still services input
           and DRM events without adding visible pointer latency. */
        return runtime->direct_kms ? 1 : 0;
    }
    for (i = 0; i < runtime->output_count; ++i) {
        uint64_t delta;
        if (runtime->render_next_us[i] <= now)
            return 0;
        delta = runtime->render_next_us[i] - now;
        if ((int)((delta + 999ULL) / 1000ULL) < wait_ms)
            wait_ms = (int)((delta + 999ULL) / 1000ULL);
    }
    return wait_ms;
}

static int render_work_remains(const BlitRuntime *runtime)
{
    int i;
    if (runtime->pointer_dirty)
        return 1;
    for (i = 0; i < runtime->output_count; ++i)
        if (runtime->pixel_frames_pending[i] > 0)
            return 1;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
        if (runtime->surfaces[i] != NULL &&
            runtime->surfaces[i]->used && !runtime->surfaces[i]->frame_open &&
            runtime->surfaces[i]->damage_valid)
            return 1;
    return 0;
}

static void expire_pixel_frame_pending(BlitRuntime *runtime, uint64_t now)
{
    int i;
    if (runtime->pixel_frames_last_accepted_us == 0 ||
        now < runtime->pixel_frames_last_accepted_us ||
        now - runtime->pixel_frames_last_accepted_us <
            BLIT_PIXEL_FRAME_QUIET_US)
        return;
    for (i = 0; i < runtime->output_count; ++i)
        runtime->pixel_frames_pending[i] = 0;
}

static void render_current_surfaces(BlitRuntime *runtime,
                                    uint32_t output_mask)
{
    BlitFramePlan plan;
    int i;

    memset(&plan, 0, sizeof(plan));
    plan.revision = runtime->frame_revision;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSurface *surface = runtime->surfaces[i];
        if (surface == NULL || !surface->used ||
            plan.surface_count >= BLIT_MAX_FRAME_SURFACES)
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
    render_outputs(runtime, &plan, output_mask);
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
        ssize_t result = send(fd, bytes + sent, length - sent,
                             MSG_DONTWAIT | MSG_NOSIGNAL);

        if (result < 0 && (errno == EINTR || errno == EAGAIN ||
                           errno == EWOULDBLOCK)) {
            if (errno == EINTR)
                continue;
            /* A slow receiver must not hold the single gateway loop.  The
               caller treats this as a failed connection and removes the
               stream or closes the HTTP response socket. */
            return 0;
        }
        if (result <= 0)
            return 0;
        sent += (size_t)result;
    }
    return 1;
}

static int send_bytes_blocking(int fd, const void *data, size_t length)
{
    int flags = fcntl(fd, F_GETFL, 0);
    const unsigned char *bytes = (const unsigned char *)data;
    size_t sent = 0;

    if (flags >= 0)
        fcntl(fd, F_SETFL, flags & ~O_NONBLOCK);
    while (sent < length) {
        ssize_t result = send(fd, bytes + sent, length - sent,
                              MSG_NOSIGNAL);
        if (result < 0 && errno == EINTR)
            continue;
        if (result <= 0) {
            if (flags >= 0)
                fcntl(fd, F_SETFL, flags);
            return 0;
        }
        sent += (size_t)result;
    }
    if (flags >= 0)
        fcntl(fd, F_SETFL, flags);
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
        "X-Blit-Version: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n\r\n",
        status, reason, content_type, blit_version,
        (unsigned long)body_length);

    if (header_length > 0 && (size_t)header_length < sizeof(header)) {
        /* Finite HTTP responses can be much larger than the socket send
           buffer (a full Hibr cell snapshot is several megabytes).  The
           gateway socket is nonblocking for request multiplexing, so the
           ordinary send_all path may stop at EAGAIN and leave a truncated
           Content-Length response.  Complete the bounded response before
           closing the connection, as the screenshot path already does. */
        send_bytes_blocking(fd, header, (size_t)header_length);
        send_bytes_blocking(fd, body, body_length);
    }
}

static void send_http_bytes(int fd, int status, const char *reason,
                            const char *content_type,
                            const unsigned char *body, size_t length)
{
    char header[512];
    int header_length = snprintf(
        header, sizeof(header),
        "HTTP/1.1 %d %s\r\n"
        "Content-Type: %s\r\n"
        "X-Blit-Version: %s\r\n"
        "Content-Length: %lu\r\n"
        "Connection: close\r\n"
        "Cache-Control: no-store\r\n\r\n",
        status, reason, content_type, blit_version, (unsigned long)length);

    if (header_length > 0 && (size_t)header_length < sizeof(header)) {
        send_all(fd, header, (size_t)header_length);
        send_bytes_blocking(fd, body, length);
    }
}

static void send_problem(int fd, int status, const char *reason,
                         const char *code, const char *detail);

static int screenshot_output_index(const BlitRuntime *runtime,
                                   const char *path)
{
    const char *prefix = "/v1/diagnostics/screenshots/";
    const char *requested;
    unsigned long long id;
    int i;

    if (strncmp(path, prefix, strlen(prefix)) != 0)
        return -1;
    requested = path + strlen(prefix);
    if (*requested == '\0' || strchr(requested, '/') != NULL)
        return -1;
    id = strtoull(strncmp(requested, "display-", 8) == 0 ?
                  requested + 8 : requested, NULL, 10);
    for (i = 0; i < runtime->output_count; ++i)
        if (runtime->outputs[i].display_id == id)
            return i;
    return -1;
}

static void send_sdl_screenshot(int fd, BlitRuntime *runtime,
                                const char *path)
{
    char temporary[] = "/tmp/blit-api-screenshot-XXXXXX";
    unsigned char *bytes = NULL;
    SDL_Surface *pixels;
    struct stat file_status;
    int output_index;
    int temporary_fd;
    int input_fd;
    BlitOutput *output;
    ssize_t received;
    size_t offset = 0;

    if (runtime->direct_kms || !runtime->sdl_ready) {
        send_problem(fd, 501, "Not Implemented", "screenshot_backend",
                     "API screenshots currently require the SDL3 backend.");
        return;
    }
    output_index = screenshot_output_index(runtime, path);
    if (output_index < 0 || runtime->outputs[output_index].renderer == NULL) {
        send_problem(fd, 404, "Not Found", "display_not_found",
                     "The requested display does not exist or has no renderer.");
        return;
    }
    output = &runtime->outputs[output_index];
    pixels = output->screenshot_surface;
    if (pixels != NULL)
        output->screenshot_surface = NULL;
    else
        pixels = SDL_RenderReadPixels(output->renderer, NULL);
    if (pixels == NULL) {
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "The SDL renderer could not read the current framebuffer.");
        return;
    }
    temporary_fd = mkstemp(temporary);
    if (temporary_fd < 0 || !SDL_SaveBMP(pixels, temporary)) {
        if (temporary_fd >= 0) close(temporary_fd);
        SDL_DestroySurface(pixels);
        unlink(temporary);
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "The framebuffer image could not be encoded.");
        return;
    }
    close(temporary_fd);
    SDL_DestroySurface(pixels);
    input_fd = open(temporary, O_RDONLY | O_CLOEXEC);
    if (input_fd < 0 || fstat(input_fd, &file_status) < 0 ||
        file_status.st_size <= 0 ||
        (uintmax_t)file_status.st_size > 64U * 1024U * 1024U) {
        if (input_fd >= 0) close(input_fd);
        unlink(temporary);
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "The encoded framebuffer image could not be read.");
        return;
    }
    bytes = (unsigned char *)malloc((size_t)file_status.st_size);
    if (bytes == NULL) {
        close(input_fd);
        unlink(temporary);
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "There is not enough memory for the framebuffer image.");
        return;
    }
    while (offset < (size_t)file_status.st_size) {
        received = read(input_fd, bytes + offset,
                        (size_t)file_status.st_size - offset);
        if (received <= 0) break;
        offset += (size_t)received;
    }
    close(input_fd);
    unlink(temporary);
    if (offset != (size_t)file_status.st_size) {
        free(bytes);
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "The encoded framebuffer image was truncated.");
        return;
    }
    send_http_bytes(fd, 200, "OK", "image/bmp", bytes, offset);
    free(bytes);
}

static int send_kms_screenshot(int fd, BlitRuntime *runtime, int output_index)
{
    unsigned char *bytes = NULL;
    size_t length = 0;
    int result = blit_kms_screenshot(runtime->kms, output_index,
                                     &bytes, &length);
    if (result == 0) return 0;
    if (result < 0) {
        send_problem(fd, 503, "Service Unavailable", "screenshot_failed",
                     "The KMS framebuffer could not be encoded.");
        return -1;
    }
    send_http_bytes(fd, 200, "OK", "image/bmp", bytes, length);
    free(bytes);
    return 1;
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

static int json_appendf(char *body, size_t capacity, size_t *used,
                        const char *format, ...)
{
    int written;
    va_list arguments;

    if (*used >= capacity)
        return 0;
    va_start(arguments, format);
    written = vsnprintf(body + *used, capacity - *used, format, arguments);
    va_end(arguments);
    if (written < 0 || (size_t)written >= capacity - *used)
        return 0;
    *used += (size_t)written;
    return 1;
}

static void diagnostic_log(BlitRuntime *runtime, const char *severity,
                           const char *code, const char *detail)
{
    char escaped[320];
    int slot;
    if (runtime == NULL) return;
    json_escape(detail != NULL ? detail : "", escaped, sizeof(escaped));
    slot = runtime->diagnostic_log_next;
    snprintf(runtime->diagnostic_logs[slot],
             sizeof(runtime->diagnostic_logs[slot]),
             "{\"sequence\":%llu,\"severity\":\"%s\","
             "\"code\":\"%s\",\"detail\":\"%s\"}",
             ++runtime->diagnostic_log_sequence,
             severity != NULL ? severity : "info",
             code != NULL ? code : "runtime",
             escaped);
    runtime->diagnostic_log_next =
        (runtime->diagnostic_log_next + 1) % BLIT_MAX_RUNTIME_LOGS;
    if (runtime->diagnostic_log_count < BLIT_MAX_RUNTIME_LOGS)
        ++runtime->diagnostic_log_count;
}

static void diagnostic_trace(BlitRuntime *runtime, const char *kind,
                             const char *detail)
{
    char escaped[512];
    char (*events)[768];
    int *event_count;
    int *event_next;
    int slot;

    if (runtime == NULL)
        return;
    json_escape(detail != NULL ? detail : "", escaped, sizeof(escaped));
    if (kind != NULL && strcmp(kind, "surface_update") != 0) {
        events = runtime->diagnostic_critical_trace;
        event_count = &runtime->diagnostic_critical_trace_count;
        event_next = &runtime->diagnostic_critical_trace_next;
    } else {
        events = runtime->diagnostic_trace;
        event_count = &runtime->diagnostic_trace_count;
        event_next = &runtime->diagnostic_trace_next;
    }
    slot = *event_next;
    snprintf(events[slot], sizeof(events[slot]),
             "{\"sequence\":%llu,\"time_us\":%llu,\"kind\":\"%s\","
             "\"detail\":\"%s\"}",
             ++runtime->diagnostic_trace_sequence,
             (unsigned long long)monotonic_us(),
             kind != NULL ? kind : "runtime", escaped);
    *event_next = (*event_next + 1) % BLIT_MAX_TRACE_EVENTS;
    if (*event_count < BLIT_MAX_TRACE_EVENTS)
        ++*event_count;
}

static void diagnostic_trace_json(BlitRuntime *runtime, char *body,
                                  size_t capacity, const char *kind_filter)
{
    size_t used = 0;
    int critical = kind_filter != NULL && kind_filter[0] != '\0' &&
        strcmp(kind_filter, "surface_update") != 0;
    char (*events)[768] = critical ? runtime->diagnostic_critical_trace :
        runtime->diagnostic_trace;
    int count = critical ? runtime->diagnostic_critical_trace_count :
        runtime->diagnostic_trace_count;
    int next = critical ? runtime->diagnostic_critical_trace_next :
        runtime->diagnostic_trace_next;
    int index = (next - count +
                 BLIT_MAX_TRACE_EVENTS) % BLIT_MAX_TRACE_EVENTS;
    int first = 1;

    json_appendf(body, capacity, &used,
                 "{\"revision\":%llu,\"events\":[",
                 runtime->diagnostic_trace_sequence);
    while (count-- > 0 && used + 4 < capacity) {
        int include = kind_filter == NULL || kind_filter[0] == '\0' ||
            strstr(events[index], kind_filter) != NULL;
        if (include) {
            json_appendf(body, capacity, &used, "%s%s",
                         first ? "" : ",", events[index]);
            first = 0;
        }
        index = (index + 1) % BLIT_MAX_TRACE_EVENTS;
    }
    json_appendf(body, capacity, &used, "]}");
}

static unsigned long long diagnostic_missed_deadlines(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->render_missed_deadlines[i];
    return total;
}

static unsigned long long diagnostic_graphics_cache_hits(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->outputs[i].graphics_cache_hits;
    return total;
}

static unsigned long long diagnostic_graphics_texture_uploads(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->outputs[i].graphics_texture_uploads;
    return total;
}

static unsigned long long diagnostic_pixel_frames_pending(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->pixel_frames_pending[i];
    return total;
}

static double diagnostic_last_graphics_upload_ms(const BlitRuntime *runtime)
{
    double value = 0.0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        if (runtime->outputs[i].last_graphics_upload_ms > value)
            value = runtime->outputs[i].last_graphics_upload_ms;
    return value;
}

static double diagnostic_max_graphics_upload_ms(const BlitRuntime *runtime)
{
    double value = 0.0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        if (runtime->outputs[i].max_graphics_upload_ms > value)
            value = runtime->outputs[i].max_graphics_upload_ms;
    return value;
}

static unsigned long long diagnostic_background_batches(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->outputs[i].background_batch_submissions;
    return total;
}

static unsigned long long diagnostic_background_quads(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < runtime->output_count; ++i)
        total += runtime->outputs[i].background_quads;
    return total;
}

static unsigned long long diagnostic_stream_queue_bytes(
    const BlitRuntime *runtime)
{
    unsigned long long total = 0;
    int i;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used)
            total += runtime->streams[i].receive_length;
    return total;
}

static void record_input_dispatch(BlitRuntime *runtime, uint64_t started_us)
{
    double elapsed;
    uint64_t now;
    if (runtime == NULL)
        return;
    now = monotonic_us();
    elapsed = now >= started_us ? (double)(now - started_us) / 1000.0 : 0.0;
    ++runtime->diagnostic_input_events;
    runtime->diagnostic_last_input_dispatch_ms = elapsed;
    if (elapsed > runtime->diagnostic_max_input_dispatch_ms)
        runtime->diagnostic_max_input_dispatch_ms = elapsed;
}

static void runtime_diagnostics_json(BlitRuntime *runtime, char *body,
                                     size_t capacity)
{
    char snapshot[8192];
    size_t used;
    int count;
    int first;
    int index;
    uint64_t now = monotonic_us();
    double frame_rate_hz = runtime->diagnostic_frame_rate_hz;
    unsigned long long frame_rate_age_ms = 0;

    if (runtime->diagnostic_frame_rate_last_us > 0 &&
        now >= runtime->diagnostic_frame_rate_last_us)
        frame_rate_age_ms = (now - runtime->diagnostic_frame_rate_last_us) /
            1000ULL;
    if (runtime->diagnostic_frame_rate_last_us == 0 ||
        frame_rate_age_ms > 2000ULL)
        frame_rate_hz = 0.0;

    diagnostics_json(runtime, snapshot, sizeof(snapshot));
    used = (size_t)snprintf(body, capacity,
        "{\"snapshot\":%s,\"performance\":{\"uptime_ms\":%llu,"
        "\"frames\":%llu,\"last_frame_gap_ms\":%.3f,"
        "\"max_frame_gap_ms\":%.3f,\"last_render_ms\":%.3f,"
        "\"max_render_ms\":%.3f,\"missed_deadlines\":%llu,"
        "\"frame_rate_max_render_ms\":%.3f,"
        "\"frame_rate_missed_deadlines\":%llu,"
        "\"frame_rate_window_sequence\":%llu,"
        "\"last_terminal_ms\":%.3f,\"max_terminal_ms\":%.3f,"
        "\"terminal_bytes\":%llu,\"pointer_cache_hits\":%llu,"
        "\"pointer_cache_misses\":%llu,\"frame_rate_hz\":%.3f,"
        "\"input_events\":%llu,\"last_input_dispatch_ms\":%.3f,"
        "\"max_input_dispatch_ms\":%.3f,"
        "\"evdev_mouse_events\":%llu,"
        "\"evdev_mouse_syncs\":%llu,"
        "\"evdev_mouse_motion_events\":%llu,"
        "\"evdev_mouse_wheel_events\":%llu,"
        "\"evdev_mouse_button_events\":%llu,"
        "\"last_evdev_mouse_event_age_ms\":%llu,"
        "\"last_evdev_mouse_path\":\"%s\","
        "\"stream_queue_bytes\":%llu,\"stream_queue_peak_bytes\":%llu,"
        "\"stream_dropped_events\":%llu,"
        "\"stream_backpressure_disconnects\":%llu,"
        "\"graphics_cache_hits\":%llu,\"graphics_texture_uploads\":%llu,"
        "\"last_graphics_upload_ms\":%.3f,\"max_graphics_upload_ms\":%.3f,"
        "\"background_batches\":%llu,\"background_quads\":%llu,"
        "\"pixel_frames_accepted\":%llu,\"pixel_frame_errors\":%llu,"
        "\"pixel_frames_pending\":%llu,"
        "\"frame_rate_frames\":%llu,\"frame_rate_window_ms\":%llu,"
        "\"frame_rate_age_ms\":%llu,\"loop_iterations\":%llu,"
        "\"terminal_reads\":%llu,\"present_calls\":%llu,"
        "\"last_loop_gap_ms\":%.3f,\"max_loop_gap_ms\":%.3f,"
        "\"last_kms_present_ms\":%.3f,\"max_kms_present_ms\":%.3f,"
        "\"last_sdl_present_ms\":%.3f,\"max_sdl_present_ms\":%.3f,"
        "\"last_select_ms\":%.3f,\"max_select_ms\":%.3f,"
        "\"last_sdl_events_ms\":%.3f,\"max_sdl_events_ms\":%.3f,"
        "\"last_evdev_scan_ms\":%.3f,\"max_evdev_scan_ms\":%.3f},"
        "\"logs\":[",
        snapshot,
        runtime->started_us > 0 && now >= runtime->started_us ?
            (unsigned long long)((now - runtime->started_us) / 1000ULL) : 0,
        runtime->diagnostic_frames, runtime->diagnostic_last_frame_gap_ms,
        runtime->diagnostic_max_frame_gap_ms,
        runtime->diagnostic_last_render_ms,
        runtime->diagnostic_max_render_ms,
        diagnostic_missed_deadlines(runtime),
        runtime->diagnostic_frame_rate_max_render_ms,
        runtime->diagnostic_frame_rate_missed_deadlines,
        runtime->diagnostic_frame_rate_window_sequence,
        runtime->diagnostic_last_terminal_ms,
        runtime->diagnostic_max_terminal_ms,
        runtime->diagnostic_terminal_bytes,
        runtime->diagnostic_pointer_cache_hits,
        runtime->diagnostic_pointer_cache_misses,
        frame_rate_hz,
        runtime->diagnostic_input_events,
        runtime->diagnostic_last_input_dispatch_ms,
        runtime->diagnostic_max_input_dispatch_ms,
        runtime->diagnostic_evdev_mouse_events,
        runtime->diagnostic_evdev_mouse_syncs,
        runtime->diagnostic_evdev_mouse_motion_events,
        runtime->diagnostic_evdev_mouse_wheel_events,
        runtime->diagnostic_evdev_mouse_button_events,
        runtime->diagnostic_last_evdev_mouse_event_us > 0 &&
                now >= runtime->diagnostic_last_evdev_mouse_event_us ?
            (unsigned long long)((now -
                runtime->diagnostic_last_evdev_mouse_event_us) / 1000ULL) : 0,
        runtime->diagnostic_last_evdev_mouse_path,
        diagnostic_stream_queue_bytes(runtime),
        runtime->diagnostic_stream_queue_peak_bytes,
        runtime->diagnostic_stream_dropped_events,
        runtime->diagnostic_stream_backpressure_disconnects,
        diagnostic_graphics_cache_hits(runtime),
        diagnostic_graphics_texture_uploads(runtime),
        diagnostic_last_graphics_upload_ms(runtime),
        diagnostic_max_graphics_upload_ms(runtime),
        diagnostic_background_batches(runtime),
        diagnostic_background_quads(runtime),
        runtime->pixel_frames_accepted,
        runtime->pixel_frame_errors,
        diagnostic_pixel_frames_pending(runtime),
        runtime->diagnostic_frame_rate_last_frames,
        runtime->diagnostic_frame_rate_window_ms, frame_rate_age_ms,
        runtime->diagnostic_loop_iterations,
        runtime->diagnostic_terminal_reads,
        runtime->diagnostic_present_calls,
        runtime->diagnostic_last_loop_gap_ms,
        runtime->diagnostic_max_loop_gap_ms,
        runtime->diagnostic_last_kms_present_ms,
        runtime->diagnostic_max_kms_present_ms,
        runtime->diagnostic_last_sdl_present_ms,
        runtime->diagnostic_max_sdl_present_ms,
        runtime->diagnostic_last_select_ms,
        runtime->diagnostic_max_select_ms,
        runtime->diagnostic_last_sdl_events_ms,
        runtime->diagnostic_max_sdl_events_ms,
        runtime->diagnostic_last_evdev_scan_ms,
        runtime->diagnostic_max_evdev_scan_ms);
    count = runtime->diagnostic_log_count;
    first = 1;
    index = (runtime->diagnostic_log_next - count + BLIT_MAX_RUNTIME_LOGS) %
        BLIT_MAX_RUNTIME_LOGS;
    while (count-- > 0 && used + 4 < capacity) {
        used += (size_t)snprintf(body + used, capacity - used, "%s%s",
                                 first ? "" : ",",
                                 runtime->diagnostic_logs[index]);
        first = 0;
        index = (index + 1) % BLIT_MAX_RUNTIME_LOGS;
    }
    snprintf(body + used, capacity - used, "]}");
}

static void server_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    char escaped_error[256];
    const char *backend = runtime->direct_kms ? "drm-kms" :
        (runtime->sdl_ready ? "sdl3" : "unavailable");
    json_escape(runtime->sdl_error, escaped_error, sizeof(escaped_error));
    snprintf(body, capacity,
             "{\"id\":\"blit\",\"name\":\"blit\","
             "\"server_version\":\"%s\",\"api_version\":\"0.1\",\"revision\":1,"
             "\"capabilities\":[\"http-control\",\"websocket-stream\","
             "\"terminal-sessions\",\"topology\","
             "\"native-presentation\",\"graphics-inventory\","
             "\"audio-inventory\",\"input-devices\",\"input-routes\","
             "\"network-terminal-input\",\"diagnostics\","
             "\"runtime-diagnostics\",\"lifecycle-control\","
             "\"effects\",\"overlays\"],\"display_count\":%d,"
             "\"display_backend\":{\"name\":\"%s\",\"ready\":%s,"
             "\"error\":\"%s\"},\"resources\":{"
             "\"graphics\":\"/v1/graphics\",\"displays\":\"/v1/displays\","
             "\"audio\":\"/v1/audio\",\"input_devices\":\"/v1/input/devices\","
             "\"input_routes\":\"/v1/input/routes\",\"topology\":\"/v1/topology\","
             "\"diagnostics\":\"/v1/diagnostics\","
             "\"runtime_diagnostics\":\"/v1/diagnostics/runtime\","
             "\"effects\":\"/v1/effects\","
             "\"overlays\":\"/v1/overlays\"}}",
             blit_version, runtime->display_count, backend,
             (runtime->direct_kms ? runtime->display_count > 0 :
              runtime->sdl_ready && runtime->output_count > 0 &&
              runtime->output_count == runtime->display_count) ?
                 "true" : "false", escaped_error);
}

static void version_json(char *body, size_t capacity)
{
    snprintf(body, capacity,
             "{\"server_version\":\"%s\",\"api_version\":\"0.1\"}",
             blit_version);
}

static const char *display_presentation_state(const BlitRuntime *runtime,
                                              int display_index)
{
    if (runtime->direct_kms) {
        BlitKmsDisplayInfo info;
        if (blit_kms_display_info(runtime->kms, display_index, &info) == 0) {
            if (info.presentation_state == BLIT_KMS_PRESENTATION_FAILED)
                return "failed";
            if (info.presentation_state == BLIT_KMS_PRESENTATION_RECOVERING)
                return "recovering";
        }
        return "presenting";
    }
    {
        int output_index;
        for (output_index = 0; output_index < runtime->output_count;
             ++output_index)
            if (runtime->outputs[output_index].display_id ==
                runtime->displays[display_index].id)
                return "presenting";
    }
    return "failed";
}

static int runtime_has_failed_display(const BlitRuntime *runtime)
{
    int i;
    for (i = 0; i < runtime->display_count; ++i)
        if (strcmp(display_presentation_state(runtime, i), "failed") == 0)
            return 1;
    return 0;
}

static void render_scheduler_json(const BlitRuntime *runtime, char *body,
                                  size_t capacity)
{
    size_t used = 0;
    int i;

    used += (size_t)snprintf(body + used, capacity - used, "[");
    for (i = 0; i < runtime->output_count && used < capacity; ++i) {
        int display_index = display_index_for_output(runtime, i);
        BlitKmsDisplayInfo kms_info;
        int have_kms_info = runtime->direct_kms && display_index >= 0 &&
            blit_kms_display_info(runtime->kms, display_index, &kms_info) == 0;
        used += (size_t)snprintf(
            body + used, capacity - used,
            "%s{\"display_id\":\"display-%llu\",\"refresh_hz\":%.3f,"
            "\"refresh_source\":\"%s\",\"effective_interval_ms\":%d,"
            "\"target_interval_us\":%llu,\"target_interval_ms\":%.3f,"
            "\"missed_deadlines\":%lu,\"last_present_ms\":%.3f,"
            "\"max_present_ms\":%.3f,\"present_calls\":%llu,"
            "\"render_width\":%d,\"render_height\":%d,"
            "\"viewport_width\":%d,\"viewport_height\":%d,"
            "\"scale_x\":%.3f,\"scale_y\":%.3f}",
            i == 0 ? "" : ",", runtime->outputs[i].display_id,
            output_refresh_hz(runtime, i),
            runtime->test_refresh_override ? "test-override" : "display-mode",
            output_render_period_ms(runtime, i),
            (unsigned long long)output_render_period_us(runtime, i),
            (double)output_render_period_us(runtime, i) / 1000.0,
            runtime->render_missed_deadlines[i],
            have_kms_info ? kms_info.last_present_ms :
                runtime->diagnostic_last_kms_present_ms,
            have_kms_info ? kms_info.max_present_ms :
                runtime->diagnostic_max_kms_present_ms,
            have_kms_info ? kms_info.present_calls :
                runtime->diagnostic_present_calls,
            runtime->direct_kms ?
                runtime->displays[display_index].bounds.w :
                runtime->outputs[i].render_width,
            runtime->direct_kms ?
                runtime->displays[display_index].bounds.h :
                runtime->outputs[i].render_height,
            runtime->direct_kms ? runtime->displays[display_index].bounds.w :
                runtime->outputs[i].viewport_width,
            runtime->direct_kms ? runtime->displays[display_index].bounds.h :
                runtime->outputs[i].viewport_height,
            runtime->direct_kms ? 1.0f : runtime->outputs[i].scale_x,
            runtime->direct_kms ? 1.0f : runtime->outputs[i].scale_y);
    }
    snprintf(body + used, capacity - used, "]");
}

static void diagnostics_json(const BlitRuntime *runtime, char *body,
                             size_t capacity)
{
    char escaped_error[256];
    char scheduler[4096];
    const char *backend = runtime->direct_kms ? "drm-kms" :
        (runtime->sdl_ready ? "sdl3" : "unavailable");
    int sessions = 0;
    int streams = 0;
    int routes = 0;
    int surfaces = 0;
    const char *presentation_sync;
    char escaped_renderer[128];
    int i;
    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        if (runtime->sessions[i].used) ++sessions;
        if (runtime->surfaces[i] != NULL && runtime->surfaces[i]->used)
            ++surfaces;
    }
    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used) ++streams;
    for (i = 0; i < 16; ++i)
        if (runtime->input_routes[i].used) ++routes;
    render_scheduler_json(runtime, scheduler, sizeof(scheduler));
    json_escape(runtime->sdl_error, escaped_error, sizeof(escaped_error));
    json_escape(runtime->sdl_renderer_name, escaped_renderer,
                sizeof(escaped_renderer));
    presentation_sync = runtime->direct_kms ?
        (getenv("BLIT_KMS_ASYNC_PAGEFLIP") != NULL ?
             "drm-page-flip-async" : "drm-page-flip") :
        (runtime->output_count == 0 ||
         runtime->output_count != runtime->display_count ||
         runtime->sdl_vsync_failures > 0 ? "scheduler-only" : "sdl-vsync");
    snprintf(body, capacity,
             "{\"revision\":1,\"health\":{\"ready\":%s,"
             "\"backend\":\"%s\",\"error\":\"%s\"},"
             "\"resources\":{\"displays\":%d,\"outputs\":%d,"
             "\"sessions\":%d,\"surfaces\":%d,\"streams\":%d,"
             "\"input_routes\":%d,\"keyboards\":%d,\"pointers\":%d,"
             "\"gamepads\":%d},"
             "\"render\":{\"pending\":%s,\"interval_ms\":%d,"
             "\"effective_interval_ms\":%d,\"mode\":\"refresh-aware\","
             "\"outputs\":%s,"
             "\"font_scale\":%d,\"direct_kms\":%s,"
             "\"renderer\":\"%s\","
             "\"presentation_sync\":\"%s\","
             "\"pointer\":{\"valid\":%s,\"x\":%.1f,\"y\":%.1f,"
             "\"output\":%d,\"previous_x\":%.1f,\"previous_y\":%.1f,"
             "\"previous_output\":%d,\"dirty\":%s}},"
             "\"errors\":%s,\"warnings\":%s}",
             (runtime->direct_kms ? runtime->display_count > 0 :
              runtime->sdl_ready && runtime->output_count > 0 &&
              runtime->output_count == runtime->display_count) ?
                 "true" : "false", backend, escaped_error,
             runtime->display_count, runtime->output_count, sessions, surfaces,
             streams, routes, runtime->evdev_keyboard_count,
             runtime->evdev_mouse_count, runtime->evdev_gamepad_count,
             runtime->render_pending ? "true" : "false",
             runtime->render_interval_ms, runtime->render_effective_interval_ms,
             scheduler,
             runtime->font_scale,
             runtime->direct_kms ? "true" : "false", escaped_renderer,
             presentation_sync,
             runtime->pointer_valid ? "true" : "false",
             runtime->pointer_x, runtime->pointer_y, runtime->pointer_output,
             runtime->previous_pointer_x, runtime->previous_pointer_y,
             runtime->previous_pointer_output,
             runtime->pointer_dirty ? "true" : "false",
             runtime->sdl_error[0] ? "[\"backend_error\"]" : "[]",
             runtime_has_failed_display(runtime) ?
                 "[\"display_presentation_failed\"]" :
                 (runtime->evdev_keyboard_count == 0 ||
                  runtime->evdev_mouse_count == 0 ?
                  "[\"input_device_missing\"]" : "[]"));
}

static void displays_json(const BlitRuntime *runtime, char *body, size_t capacity)
{
    size_t used = 0;
    int i;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"displays\":[");
    for (i = 0; i < runtime->display_count && used < capacity; ++i) {
        char escaped_name[256];
        int mode_index;
        int active_mode = runtime->displays[i].active_mode;
        json_escape(runtime->displays[i].name, escaped_name,
                    sizeof(escaped_name));
        used += (size_t)snprintf(
            body + used, capacity - used,
            "%s{\"id\":\"display-%llu\",\"name\":\"%s\","
            "\"bounds\":{\"x\":%d,\"y\":%d,\"width\":%d,"
            "\"height\":%d},\"modes\":[",
            i == 0 ? "" : ",", runtime->displays[i].id, escaped_name,
            runtime->displays[i].bounds.x, runtime->displays[i].bounds.y,
            runtime->displays[i].bounds.w, runtime->displays[i].bounds.h,
            runtime->displays[i].bounds.h);
        for (mode_index = 0; mode_index < runtime->displays[i].mode_count &&
             used < capacity; ++mode_index) {
            BlitKmsMode *mode = &runtime->displays[i].modes[mode_index];
            used += (size_t)snprintf(
                body + used, capacity - used,
                "%s{\"width\":%d,\"height\":%d,\"refresh_hz\":%.3f}",
                mode_index == 0 ? "" : ",", mode->width, mode->height,
                mode->refresh_hz);
        }
        if (active_mode < 0 || active_mode >= runtime->displays[i].mode_count)
            active_mode = 0;
        if (runtime->displays[i].mode_count > 0) {
            BlitKmsMode *mode = &runtime->displays[i].modes[active_mode];
            used += (size_t)snprintf(
                body + used, capacity - used,
                "],\"active_mode\":{\"width\":%d,\"height\":%d,"
                "\"refresh_hz\":%.3f},\"presentation_state\":\"%s\"}",
                mode->width, mode->height, mode->refresh_hz,
                display_presentation_state(runtime, i));
        } else {
            used += (size_t)snprintf(
                body + used, capacity - used,
                "],\"active_mode\":null,\"presentation_state\":\"%s\"}",
                display_presentation_state(runtime, i));
        }
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

static int read_text_file(const char *path, char *value, size_t capacity)
{
    FILE *file;
    size_t length;

    if (capacity == 0)
        return 0;
    file = fopen(path, "r");
    if (file == NULL)
        return 0;
    if (fgets(value, (int)capacity, file) == NULL) {
        fclose(file);
        value[0] = '\0';
        return 0;
    }
    fclose(file);
    length = strlen(value);
    while (length > 0 &&
           (value[length - 1] == '\n' || value[length - 1] == '\r'))
        value[--length] = '\0';
    return length > 0;
}

static int input_bit(const unsigned long *bits, int code)
{
    return (bits[code / (int)(sizeof(unsigned long) * 8)] &
            (1UL << (code % (int)(sizeof(unsigned long) * 8)))) != 0;
}

static void graphics_json(char *body, size_t capacity)
{
    size_t used = 0;
    int card;
    int found = 0;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"graphics_cards\":[");
    for (card = 0; card < 32 && used < capacity; ++card) {
        char path[64];
        char sysfs_path[128];
        char vendor[64] = "unknown";
        char device[64] = "unknown";
        char driver[128] = "unknown";
        char driver_link[256];
        char render_node[64] = "";
        ssize_t driver_length;
        int render_available = 0;
        int length;

        snprintf(path, sizeof(path), "/dev/dri/card%d", card);
        if (access(path, R_OK | W_OK) != 0)
            continue;
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/drm/card%d/device/vendor", card);
        read_text_file(sysfs_path, vendor, sizeof(vendor));
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/drm/card%d/device/device", card);
        read_text_file(sysfs_path, device, sizeof(device));
        snprintf(sysfs_path, sizeof(sysfs_path),
                 "/sys/class/drm/card%d/device/driver", card);
        driver_length = readlink(sysfs_path, driver_link,
                                 sizeof(driver_link) - 1);
        if (driver_length > 0) {
            const char *last_slash;
            driver_link[driver_length] = '\0';
            last_slash = strrchr(driver_link, '/');
            snprintf(driver, sizeof(driver), "%s",
                     last_slash != NULL ? last_slash + 1 : driver_link);
        }
        {
            int render_index;
            for (render_index = 128; render_index < 160; ++render_index) {
                snprintf(render_node, sizeof(render_node),
                         "/dev/dri/renderD%d", render_index);
                if (access(render_node, R_OK | W_OK) == 0) {
                    render_available = 1;
                    break;
                }
            }
            if (!render_available)
                render_node[0] = '\0';
        }
        length = snprintf(body + used, capacity - used,
                          "%s{\"id\":\"card-%d\","
                          "\"vendor\":\"%s\","
                          "\"device\":\"%s\","
                          "\"render_apis\":[\"drm\",\"kms\"],"
                          "\"formats\":[\"xrgb8888\",\"argb8888\"],"
                          "\"capabilities\":{\"driver\":\"%s\","
                          "\"device_node\":\"%s\","
                          "\"page_flipping\":true,"
                          "\"direct_scanout\":true,"
                          "\"hardware_acceleration\":%s%s%s%s}}",
                          found ? "," : "", card, vendor, device, driver,
                          path, render_available ? "true" : "false",
                          render_available ? ",\"render_node\":\"" : "",
                          render_available ? render_node : "",
                          render_available ? "\"" : "");
        if (length < 0 || (size_t)length >= capacity - used)
            break;
        used += (size_t)length;
        found = 1;
    }
    snprintf(body + used, capacity - used, "]}");
}

static void audio_json(char *body, size_t capacity)
{
    size_t used = 0;
    int card;
    int found = 0;

    used += (size_t)snprintf(body + used, capacity - used,
                             "{\"revision\":1,\"devices\":[");
    for (card = 0; card < 32 && used < capacity; ++card) {
        char control[64];
        char pcm[64];
        char name_path[96];
        char name[128] = "ALSA card";
        char escaped_name[256];
        int playback = 0;
        int capture = 0;
        int device;
        int length;

        snprintf(control, sizeof(control), "/dev/snd/controlC%d", card);
        for (device = 0; device < 32; ++device) {
            snprintf(pcm, sizeof(pcm), "/dev/snd/pcmC%dD%dp", card, device);
            if (access(pcm, R_OK | W_OK) == 0) playback = 1;
            snprintf(pcm, sizeof(pcm), "/dev/snd/pcmC%dD%dc", card, device);
            if (access(pcm, R_OK | W_OK) == 0) capture = 1;
        }
        if (access(control, R_OK | W_OK) != 0 || (!playback && !capture))
            continue;
        snprintf(name_path, sizeof(name_path), "/proc/asound/card%d/id", card);
        read_text_file(name_path, name, sizeof(name));
        json_escape(name, escaped_name, sizeof(escaped_name));
        length = snprintf(body + used, capacity - used,
                          "%s{\"id\":\"alsa-card-%d\","
                          "\"name\":\"%s\",\"direction\":\"%s\","
                          "\"formats\":[%s]}", found ? "," : "", card,
                          escaped_name,
                          playback && capture ? "duplex" :
                          (playback ? "playback" : "capture"),
                          playback || capture ? "\"s16le\"" : "");
        if (length < 0 || (size_t)length >= capacity - used)
            break;
        used += (size_t)length;
        found = 1;
    }
    snprintf(body + used, capacity - used, "]}");
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
        unsigned long abs_bits[4];
        int fd;
        int pointer;
        int keyboard;
        int gamepad;
        int first_kind = 1;
        int first_route = 1;
        int first_capability = 1;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        memset(name, 0, sizeof(name));
        memset(event_bits, 0, sizeof(event_bits));
        memset(key_bits, 0, sizeof(key_bits));
        memset(rel_bits, 0, sizeof(rel_bits));
        memset(abs_bits, 0, sizeof(abs_bits));
        ioctl(fd, EVIOCGNAME(sizeof(name)), name);
        ioctl(fd, EVIOCGBIT(0, sizeof(event_bits)), event_bits);
        ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(key_bits)), key_bits);
        ioctl(fd, EVIOCGBIT(EV_REL, sizeof(rel_bits)), rel_bits);
        ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(abs_bits)), abs_bits);
        pointer = input_bit(key_bits, BTN_LEFT) || input_bit(key_bits, BTN_RIGHT) ||
            input_bit(rel_bits, REL_X) || input_bit(rel_bits, REL_Y);
        keyboard = input_bit(key_bits, KEY_A) || input_bit(key_bits, KEY_ENTER);
        gamepad = (input_bit(abs_bits, ABS_X) || input_bit(abs_bits, ABS_Y) ||
                   input_bit(abs_bits, ABS_RX) || input_bit(abs_bits, ABS_RY)) &&
            (input_bit(key_bits, BTN_GAMEPAD) ||
             input_bit(key_bits, BTN_SOUTH) || input_bit(key_bits, BTN_EAST));
        if (!pointer && !keyboard && !gamepad) { close(fd); continue; }
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
        if (gamepad) {
            used += (size_t)snprintf(body + used, capacity - used, "%s\"gamepad\"", first_kind ? "" : ",");
            first_kind = 0;
        }
        used += (size_t)snprintf(body + used, capacity - used, "],\"route_ids\":[");
        if (keyboard) {
            used += (size_t)snprintf(body + used, capacity - used,
                                     "\"evdev-keyboard\"");
            first_route = 0;
        }
        if (pointer) {
            used += (size_t)snprintf(body + used, capacity - used,
                                     "%s\"evdev-pointer\"",
                                     first_route ? "" : ",");
            first_route = 0;
        }
        if (gamepad) {
            used += (size_t)snprintf(body + used, capacity - used,
                                     "%s\"evdev-gamepad\"",
                                     first_route ? "" : ",");
        }
        used += (size_t)snprintf(body + used, capacity - used, "],\"capabilities\":[");
        if (input_bit(rel_bits, REL_X)) { used += (size_t)snprintf(body + used, capacity - used, "\"rel-x\""); first_capability = 0; }
        if (input_bit(rel_bits, REL_Y)) { used += (size_t)snprintf(body + used, capacity - used, "%s\"rel-y\"", first_capability ? "" : ","); first_capability = 0; }
        if (input_bit(key_bits, BTN_LEFT)) { used += (size_t)snprintf(body + used, capacity - used, "%s\"buttons\"", first_capability ? "" : ","); first_capability = 0; }
        if (keyboard) { used += (size_t)snprintf(body + used, capacity - used, "%s\"keys\"", first_capability ? "" : ","); }
        if (input_bit(abs_bits, ABS_X) || input_bit(abs_bits, ABS_Y) ||
            input_bit(abs_bits, ABS_RX) || input_bit(abs_bits, ABS_RY)) {
            used += (size_t)snprintf(body + used, capacity - used, "%s\"axes\"", first_capability ? "" : ",");
            first_capability = 0;
        }
        if (gamepad) { used += (size_t)snprintf(body + used, capacity - used, "%s\"gamepad-buttons\"", first_capability ? "" : ","); }
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

/* Find a JSON object key without matching text inside a quoted string.  Blit
   accepts only small bounded request objects, so this deliberately remains a
   lightweight key scanner rather than growing a second general JSON parser. */
static const char *json_key_cursor(const char *json, const char *key)
{
    const char *cursor = json;
    size_t key_length = strlen(key);

    while (*cursor != '\0') {
        const char *quoted;
        const char *character;
        size_t index = 0;
        int match = 1;

        if (*cursor != '"') {
            ++cursor;
            continue;
        }
        quoted = cursor++;
        character = cursor;
        while (*character != '\0' && *character != '"') {
            if (*character == '\\' && character[1] != '\0') {
                match = 0;
                character += 2;
                ++index;
                continue;
            }
            if (index >= key_length || *character != key[index])
                match = 0;
            ++index;
            ++character;
        }
        if (*character == '"') {
            const char *after = character + 1;
            while (*after == ' ' || *after == '\t' || *after == '\r' ||
                   *after == '\n')
                ++after;
            if (match && index == key_length && *after == ':')
                return quoted;
            cursor = character + 1;
        } else {
            return NULL;
        }
    }
    return NULL;
}

static int json_string(const char *json, const char *key, char *value,
                       size_t capacity)
{
    const char *cursor;
    size_t out = 0;

    cursor = json_key_cursor(json, key);
    if (cursor == NULL)
        return 0;
    cursor += strlen(key) + 2;
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
    const char *cursor;
    char *end;

    cursor = json_key_cursor(json, key);
    if (cursor == NULL)
        return 0;
    cursor += strlen(key) + 2;
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
    const char *cursor;

    cursor = json_key_cursor(json, key);
    if (cursor == NULL)
        return 0;
    cursor += strlen(key) + 2;
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

/* Parse the bounded topology placement array.  Placement is deliberately a
   small, closed object rather than a general JSON decoder: every member is
   required, coordinates are integers, and the caller validates completeness
   and display identity before mutating runtime state. */
static int json_output_placements(const char *json, char ids[][128],
                                  int *xs, int *ys, int limit)
{
    const char *cursor = strstr(json, "\"placements\"");
    int count = 0;

    if (cursor == NULL) return 0;
    cursor += strlen("\"placements\"");
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != ':') return -1;
    while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
           *cursor == '\n') ++cursor;
    if (*cursor++ != '[') return -1;
    for (;;) {
        const char *object_start;
        const char *object_end;
        size_t object_length;
        char object[512];

        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n' || *cursor == ',') ++cursor;
        if (*cursor == ']') return count;
        if (*cursor != '{' || count >= limit) return -1;
        object_start = cursor;
        object_end = strchr(object_start, '}');
        if (object_end == NULL) return -1;
        object_length = (size_t)(object_end - object_start + 1);
        if (object_length >= sizeof(object)) return -1;
        memcpy(object, object_start, object_length);
        object[object_length] = '\0';
        if (!json_string(object, "display_id", ids[count], 128) ||
            !json_int(object, "x", &xs[count]) ||
            !json_int(object, "y", &ys[count]))
            return -1;
        ++count;
        cursor = object_end + 1;
        while (*cursor == ' ' || *cursor == '\t' || *cursor == '\r' ||
               *cursor == '\n') ++cursor;
        if (*cursor != ',' && *cursor != ']') return -1;
    }
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

static int utf8_prefix_is_incomplete(const unsigned char *text, size_t length)
{
    size_t expected;
    size_t i;

    if (text == NULL || length == 0 || length >= 4)
        return 0;
    if (text[0] >= 0xc2 && text[0] <= 0xdf)
        expected = 2;
    else if (text[0] >= 0xe0 && text[0] <= 0xef)
        expected = 3;
    else if (text[0] >= 0xf0 && text[0] <= 0xf4)
        expected = 4;
    else
        return 0;
    if (length >= expected)
        return 0;
    for (i = 1; i < length; ++i)
        if ((text[i] & 0xc0) != 0x80)
            return 0;
    return 1;
}

static int unicode_cell_width(uint32_t codepoint)
{
    size_t low = 0;
    size_t high = BLIT_UNICODE_WIDTH_COUNT;

    if (codepoint < 0x20 ||
        (codepoint >= 0x7f && codepoint < 0xa0))
        return 0;
    if (codepoint < 0x0300)
        return 1;
    while (low < high) {
        size_t middle = (low + high) / 2;
        const BlitUnicodeWidthRange *range =
            &blit_unicode_width_table[middle];
        if (codepoint < range->first)
            high = middle;
        else if (codepoint > range->last)
            low = middle + 1;
        else
            return range->width;
    }
    return 1;
}

static int decode_text_cells(const char *text, uint32_t *codepoints,
                             unsigned char *widths, uint32_t *combining,
                             unsigned char *combining_counts, size_t *count)
{
    size_t offset = 0;
    size_t length = strlen(text);

    *count = 0;
    while (offset < length) {
        size_t consumed;
        uint32_t codepoint;

        if (!utf8_decode((const unsigned char *)text + offset,
                         length - offset, &consumed, &codepoint))
            return 0;
        if (codepoint == '\n' || codepoint == '\r' || codepoint < 0x20 ||
            codepoint == 0x7f)
            return 0;
        {
            int width = unicode_cell_width(codepoint);
            if (width == 0) {
                if (*count == 0 ||
                    combining_counts[*count - 1] >= BLIT_MAX_COMBINING)
                    return 0;
                combining[(*count - 1) * BLIT_MAX_COMBINING +
                          combining_counts[*count - 1]++] = codepoint;
            } else {
                if (*count >= BLIT_MAX_CELL_COLS || (width != 1 && width != 2))
                    return 0;
                codepoints[*count] = codepoint;
                widths[*count] = (unsigned char)width;
                combining_counts[*count] = 0;
                ++*count;
            }
        }
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
        if (runtime->surfaces[i] != NULL && runtime->surfaces[i]->used &&
            runtime->surfaces[i]->id == surface_id)
            return runtime->surfaces[i];
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

static BlitSurface *find_scrollback_surface(BlitRuntime *runtime,
                                            const char *path)
{
    const char *prefix = "/v1/surfaces/";
    const char *id;
    unsigned long long surface_id;

    if (strncmp(path, prefix, strlen(prefix)) != 0)
        return NULL;
    id = path + strlen(prefix);
    if (sscanf(id, "surface-%llu/scrollback", &surface_id) != 1)
        return NULL;
    if (strstr(id, "/scrollback") == NULL)
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

static size_t cell_text_utf8(char *text, size_t capacity, uint32_t codepoint,
                             const uint32_t *combining,
                             unsigned combining_count)
{
    size_t used = 0;
    unsigned index;
    char glyph[5];
    size_t glyph_length;

    if (capacity == 0) return 0;
    glyph_length = utf8_encode(codepoint ? codepoint : ' ', glyph);
    if (glyph_length + 1 > capacity) return 0;
    memcpy(text + used, glyph, glyph_length);
    used += glyph_length;
    for (index = 0; index < combining_count && index < BLIT_MAX_COMBINING;
         ++index) {
        glyph_length = utf8_encode(combining[index], glyph);
        if (used + glyph_length + 1 > capacity) break;
        memcpy(text + used, glyph, glyph_length);
        used += glyph_length;
    }
    text[used] = '\0';
    return used;
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

static int surface_snapshot_json(const BlitSurface *surface,
                                 const BlitSession *session, char *body,
                                 size_t capacity)
{
    size_t used;
    int x;
    int y;
    int first = 1;

    used = 0;
    if (!json_appendf(body, capacity, &used,
        "{\"id\":\"surface-%llu\",\"revision\":%llu,"
        "\"width\":%d,\"height\":%d,\"cursor\":{\"x\":%d,"
        "\"y\":%d,\"visible\":%s,\"style\":%d},\"cells\":[",
        surface->id, surface->revision, surface->width, surface->height,
        surface->cursor_x, surface->cursor_y,
        surface->cursor_visible ? "true" : "false", surface->cursor_style))
        return 0;
    for (y = 0; y < surface->height; ++y) {
        for (x = 0; x < surface->width; ++x) {
            char text[5 + BLIT_MAX_COMBINING * 4];
            char escaped[256];
            uint32_t codepoint = surface->codepoints[y][x];
            unsigned combining_count = surface->combining_count[y][x];
            uint16_t attributes = surface->attributes[y][x];
            uint16_t link = surface->links[y][x];
            int cell_width = (attributes & BLIT_ATTR_CONTINUATION) ? 0 :
                ((attributes & BLIT_ATTR_WIDE) ? 2 : 1);
            int styled = surface->foreground[y][x] != 0 ||
                surface->background[y][x] != 0 ||
                surface->underline_color[y][x] != 0 || attributes != 0;

            if (codepoint == 0 && combining_count == 0 && !styled && link == 0)
                continue;
            cell_text_utf8(text, sizeof(text), codepoint,
                           surface->combining[y][x], combining_count);
            json_escape(text, escaped, sizeof(escaped));
            if (!json_appendf(body, capacity, &used,
                "%s{\"x\":%d,\"y\":%d,\"width\":%d,\"text\":\"%s\"",
                first ? "" : ",", x, y, cell_width, escaped))
                return 0;
            first = 0;
            if (styled) {
                uint32_t foreground = surface->foreground[y][x] ?
                    surface->foreground[y][x] - 1 : BLIT_DEFAULT_FOREGROUND;
                uint32_t background = surface->background[y][x] ?
                    surface->background[y][x] - 1 : 0;
                if (!json_appendf(body, capacity, &used,
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
                        surface->underline_color[y][x] - 1 : 0)))
                    return 0;
            }
            if (link != 0 && session != NULL && link <= session->terminal_link_count) {
                char escaped_link[512];
                json_escape(session->terminal_link_uris[link - 1],
                            escaped_link, sizeof(escaped_link));
                if (!json_appendf(body, capacity, &used,
                                  ",\"link\":\"%s\"", escaped_link))
                    return 0;
            }
            if (!json_appendf(body, capacity, &used, "}"))
                return 0;
        }
    }
    if (!json_appendf(body, capacity, &used, "]}"))
        return 0;
    return 1;
}

static void surface_scrollback_json(const BlitSurface *surface, char *body,
                                    size_t capacity)
{
    size_t used;
    int line_index;
    int first = 1;

    used = (size_t)snprintf(body, capacity,
                            "{\"id\":\"surface-%llu\","
                            "\"count\":%d,\"view\":%d,\"lines\":[",
                            surface->id, surface->scrollback_count,
                            surface->scrollback_view);
    for (line_index = 0; line_index < surface->scrollback_count;
         ++line_index) {
        int index = (surface->scrollback_start + line_index) %
            BLIT_MAX_SCROLLBACK_LINES;
        const BlitScrollbackLine *line = &surface->scrollback[index];
        int last = line->width;
        int column;
        char text[32768];
        char escaped[65536];
        size_t text_used = 0;

        while (last > 0 && line->codepoints[last - 1] == 0) --last;
        for (column = 0; column < last && text_used + 5 < sizeof(text);
             ++column) {
            char cell_text[5 + BLIT_MAX_COMBINING * 4];
            size_t cell_length = cell_text_utf8(
                cell_text, sizeof(cell_text), line->codepoints[column],
                &line->combining[column * BLIT_MAX_COMBINING],
                line->combining_count[column]);
            if (text_used + cell_length + 1 >= sizeof(text)) break;
            memcpy(text + text_used, cell_text, cell_length);
            text_used += cell_length;
        }
        text[text_used] = '\0';
        json_escape(text, escaped, sizeof(escaped));
        used += (size_t)snprintf(body + used, capacity - used,
                                 "%s{\"index\":%d,\"width\":%d,"
                                 "\"text\":\"%s\"}",
                                 first ? "" : ",", line_index, line->width,
                                 escaped);
        first = 0;
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

/* Cursor movement is not visual damage while the terminal cursor is hidden.
 * Hibr moves its logical cursor frequently while painting wallpaper and UI;
 * scheduling a frame for each invisible move defeats cell damage suppression. */
static void mark_cursor_damage(BlitSurface *surface, int x, int y)
{
    if (surface != NULL && surface->cursor_visible)
        mark_surface_damage(surface, x, y);
}

static void mark_surface_damage_width(BlitSurface *surface, int x, int y,
                                      int width)
{
    int right = x + (width > 0 ? width : 1);
    int bottom = y + 1;

    if (!surface->damage_valid) {
        surface->damage_valid = 1;
        surface->damage_x = x;
        surface->damage_y = y;
        surface->damage_width = right - x;
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

static void mark_surface_full_damage(BlitSurface *surface)
{
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void mark_surface_damage_rect(BlitSurface *surface, int x, int y,
                                     int width, int height)
{
    int right;
    int bottom;

    if (surface == NULL || width <= 0 || height <= 0)
        return;
    right = x + width;
    bottom = y + height;
    if (x < 0) x = 0;
    if (y < 0) y = 0;
    if (right > surface->width) right = surface->width;
    if (bottom > surface->height) bottom = surface->height;
    if (x >= right || y >= bottom)
        return;
    if (!surface->damage_valid) {
        surface->damage_valid = 1;
        surface->damage_x = x;
        surface->damage_y = y;
        surface->damage_width = right - x;
        surface->damage_height = bottom - y;
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

static void mark_graphics_damage(BlitSurface *surface, int x, int y,
                                 int width, int height, int cell_width,
                                 int cell_height)
{
    int pixel_right;
    int pixel_bottom;
    int cell_x;
    int cell_y;
    int cell_right;
    int cell_bottom;

    if (surface == NULL || width <= 0 || height <= 0 || cell_width < 1 ||
        cell_height < 1)
        return;
    pixel_right = x + width;
    pixel_bottom = y + height;
    x = x < 0 ? 0 : x;
    y = y < 0 ? 0 : y;
    if (pixel_right <= 0 || pixel_bottom <= 0)
        return;
    cell_x = x / cell_width;
    cell_y = y / cell_height;
    cell_right = (pixel_right + cell_width - 1) / cell_width;
    cell_bottom = (pixel_bottom + cell_height - 1) / cell_height;
    mark_surface_damage_rect(surface, cell_x, cell_y,
                              cell_right - cell_x, cell_bottom - cell_y);
}

static int apply_cell_update(BlitSurface *surface, const char *json)
{
    const char *cursor = strstr(json, "\"cells\"");
    const char *cursor_state = strstr(json, "\"cursor\"");
    int updated = 0;

    if (cursor == NULL)
        return 0;
    cursor = json_key_cursor(cursor, "x");
    while (cursor != NULL) {
        char cell_json[4096];
        char text[BLIT_MAX_CELL_COLS];
        char color_text[32];
        uint32_t foreground = 0;
        uint32_t background = 0;
        uint32_t underline_color = 0;
        uint16_t attributes = 0;
        int foreground_set = 0;
        int background_set = 0;
        int underline_color_set = 0;
        const char *next_cell;
        const char *style_cursor;
        int style_seen;
        int style_valid = 1;
        int flag;
        int x, y;
        uint32_t decoded[BLIT_MAX_CELL_COLS];
        unsigned char decoded_widths[BLIT_MAX_CELL_COLS];
        uint32_t decoded_combining[BLIT_MAX_CELL_COLS * BLIT_MAX_COMBINING];
        unsigned char decoded_combining_counts[BLIT_MAX_CELL_COLS];
        size_t decoded_count;
        size_t cell_length;
        size_t i;

        next_cell = json_key_cursor(cursor + 3, "x");
        cell_length = (size_t)((next_cell != NULL ? next_cell :
                                cursor + strlen(cursor)) - cursor);
        if (cell_length == 0 || cell_length >= sizeof(cell_json))
            break;
        memcpy(cell_json, cursor, cell_length);
        cell_json[cell_length] = '\0';
        style_cursor = json_key_cursor(cell_json, "style");
        style_seen = style_cursor != NULL;
        if (!json_int(cell_json, "x", &x) ||
            !json_int(cell_json, "y", &y) ||
            !json_string(cell_json, "text", text, sizeof(text)))
            break;
        if (style_seen) {
            if (json_string(cell_json, "foreground", color_text,
                            sizeof(color_text))) {
                foreground_set = 1;
                if (!parse_color(color_text, &foreground))
                    style_valid = 0;
            }
            if (json_string(cell_json, "background", color_text,
                            sizeof(color_text))) {
                background_set = 1;
                if (!parse_color(color_text, &background))
                    style_valid = 0;
            }
            if (json_bool(cell_json, "bold", &flag) && flag)
                attributes |= BLIT_ATTR_BOLD;
            if (json_bool(cell_json, "dim", &flag) && flag)
                attributes |= BLIT_ATTR_DIM;
            if (json_bool(cell_json, "italic", &flag) && flag)
                attributes |= BLIT_ATTR_ITALIC;
            if (json_bool(cell_json, "underline", &flag) && flag)
                attributes |= BLIT_ATTR_UNDERLINE;
            if (json_bool(cell_json, "blink", &flag) && flag)
                attributes |= BLIT_ATTR_BLINK;
            if (json_bool(cell_json, "inverse", &flag) && flag)
                attributes |= BLIT_ATTR_INVERSE;
            if (json_bool(cell_json, "strike", &flag) && flag)
                attributes |= BLIT_ATTR_STRIKE;
            if (json_bool(cell_json, "concealed", &flag) && flag)
                attributes |= BLIT_ATTR_CONCEAL;
            if (json_string(cell_json, "underline_color", color_text,
                            sizeof(color_text))) {
                underline_color_set = parse_color(color_text, &underline_color);
                if (!underline_color_set)
                    style_valid = 0;
            }
        }
        if (!style_valid)
            break;
        memset(decoded_combining_counts, 0, sizeof(decoded_combining_counts));
        memset(decoded_combining, 0, sizeof(decoded_combining));
        if (!decode_text_cells(text, decoded, decoded_widths,
                               decoded_combining, decoded_combining_counts,
                               &decoded_count))
            break;
        if (x >= 0 && y >= 0 && x < BLIT_MAX_CELL_COLS &&
            y < BLIT_MAX_CELL_ROWS) {
            int target_x = x;
            for (i = 0; i < decoded_count && target_x < surface->width;
                 ++i) {
                int width = decoded_widths[i];
                if (target_x + width > surface->width)
                    break;
                terminal_clear_wide_overlap(surface, target_x, y);
                terminal_clear_cell(surface, target_x, y);
                surface->codepoints[y][target_x] = decoded[i];
                surface->cells[y][target_x] = decoded[i] < 0x80 ?
                    (char)decoded[i] : '?';
                memcpy(surface->combining[y][target_x],
                       &decoded_combining[i * BLIT_MAX_COMBINING],
                       sizeof(surface->combining[y][target_x]));
                surface->combining_count[y][target_x] =
                    decoded_combining_counts[i];
                surface->attributes[y][target_x] =
                    attributes | (width == 2 ? BLIT_ATTR_WIDE : 0);
                if (style_seen) {
                    if (foreground_set)
                        surface->foreground[y][target_x] = foreground + 1;
                    if (background_set)
                        surface->background[y][target_x] = background + 1;
                    if (underline_color_set)
                        surface->underline_color[y][target_x] = underline_color + 1;
                }
                if (width == 2) {
                    terminal_clear_wide_overlap(surface, target_x + 1, y);
                    terminal_clear_cell(surface, target_x + 1, y);
                    surface->attributes[y][target_x + 1] =
                        BLIT_ATTR_CONTINUATION;
                    if (style_seen) {
                        if (foreground_set)
                            surface->foreground[y][target_x + 1] = foreground + 1;
                        if (background_set)
                            surface->background[y][target_x + 1] = background + 1;
                        if (underline_color_set)
                            surface->underline_color[y][target_x + 1] =
                                underline_color + 1;
                    }
                }
                mark_surface_damage_width(surface, target_x, y, width);
                target_x += width;
            }
            updated += 1;
        }
        if (next_cell == NULL)
            break;
        cursor = next_cell;
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
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
        surface->cursor_x = x;
        surface->cursor_y = y;
        surface->cursor_visible = visible;
        surface->cursor_style = style;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
    memset(surface->underline_color, 0, sizeof(surface->underline_color));
    memset(surface->attributes, 0, sizeof(surface->attributes));
    memset(surface->links, 0, sizeof(surface->links));
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
static void terminal_scrollback_show_live(BlitSurface *surface);
static void terminal_scrollback_show_offset(BlitSurface *surface, int view);

static int terminal_swap_screens(BlitSurface *surface)
{
    unsigned char *scratch;

    /* The two screens are fixed-size canonical state.  Swapping them only
       happens on DEC private-mode transitions, never on the render path;
       use one heap scratch buffer instead of duplicating a second renderer
       or relying on a large stack frame. */
    scratch = (unsigned char *)malloc(sizeof(surface->combining));
    if (scratch == NULL)
        return 0;
#define BLIT_SWAP_SCREEN_FIELD(field) do { \
        memcpy(scratch, surface->field, sizeof(surface->field)); \
        memcpy(surface->field, surface->alternate_##field, \
               sizeof(surface->field)); \
        memcpy(surface->alternate_##field, scratch, sizeof(surface->field)); \
    } while (0)
    BLIT_SWAP_SCREEN_FIELD(cells);
    BLIT_SWAP_SCREEN_FIELD(codepoints);
    BLIT_SWAP_SCREEN_FIELD(combining);
    BLIT_SWAP_SCREEN_FIELD(combining_count);
    BLIT_SWAP_SCREEN_FIELD(foreground);
    BLIT_SWAP_SCREEN_FIELD(background);
    BLIT_SWAP_SCREEN_FIELD(underline_color);
    BLIT_SWAP_SCREEN_FIELD(attributes);
    BLIT_SWAP_SCREEN_FIELD(links);
#undef BLIT_SWAP_SCREEN_FIELD
    free(scratch);
    return 1;
}

static void terminal_enter_alternate(BlitSession *session,
                                     BlitSurface *surface, int mode)
{
    terminal_scrollback_show_live(surface);
    if (session->terminal_alternate)
        return;
    if (mode == 1049)
        terminal_store_cursor(&session->terminal_alternate_saved_cursor,
                              session, surface);
    if (!terminal_swap_screens(surface)) {
        fprintf(stderr, "blit: alternate-screen swap allocation failed\n");
        return;
    }
    session->terminal_saved_x = surface->cursor_x;
    session->terminal_saved_y = surface->cursor_y;
    session->terminal_alternate = 1;
    session->terminal_alternate_mode = mode;
    session->terminal_wrap_pending = 0;
    if (mode == 1047 || mode == 1049)
        terminal_clear_active_screen(surface);
}

static void terminal_leave_alternate(BlitSession *session,
                                     BlitSurface *surface)
{
    int mode;

    if (!session->terminal_alternate)
        return;
    mode = session->terminal_alternate_mode;
    if (mode == 1047)
        terminal_clear_active_screen(surface);
    if (!terminal_swap_screens(surface)) {
        fprintf(stderr, "blit: alternate-screen swap allocation failed\n");
        return;
    }
    session->terminal_alternate = 0;
    session->terminal_alternate_mode = 0;
    session->terminal_wrap_pending = 0;
    if (mode == 1049 && session->terminal_alternate_saved_cursor.valid) {
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
    }
    if (surface->cursor_x >= surface->width)
        surface->cursor_x = surface->width - 1;
    if (surface->cursor_y >= surface->height)
        surface->cursor_y = surface->height - 1;
    /* DECSC/DECRC save the autowrap mode, not its transient pending flag. */
    session->terminal_wrap_pending = 0;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_clear_cell(BlitSurface *surface, int x, int y)
{
    int changed;
    if (x < 0 || y < 0 || x >= surface->width || y >= surface->height)
        return;
    changed = surface->cells[y][x] != 0 ||
        surface->codepoints[y][x] != 0 ||
        surface->combining_count[y][x] != 0 ||
        surface->foreground[y][x] != 0 ||
        surface->background[y][x] != 0 ||
        surface->underline_color[y][x] != 0 ||
        surface->attributes[y][x] != 0 || surface->links[y][x] != 0;
    surface->cells[y][x] = 0;
    surface->codepoints[y][x] = 0;
    memset(surface->combining[y][x], 0, sizeof(surface->combining[y][x]));
    surface->combining_count[y][x] = 0;
    surface->foreground[y][x] = 0;
    surface->background[y][x] = 0;
    surface->underline_color[y][x] = 0;
    surface->attributes[y][x] = 0;
    surface->links[y][x] = 0;
    if (changed)
        mark_surface_damage(surface, x, y);
}

static int terminal_cell_matches(const BlitSession *session,
                                 const BlitSurface *surface, int x, int y,
                                 uint32_t codepoint, int cell_width)
{
    uint16_t attributes;
    uint32_t foreground;
    uint32_t background;
    uint32_t underline_color;

    if (session == NULL || surface == NULL || x < 0 || y < 0 ||
        x >= surface->width || y >= surface->height)
        return 0;
    if (surface->attributes[y][x] & BLIT_ATTR_CONTINUATION)
        return 0;
    attributes = session->terminal_attributes |
        (cell_width == 2 ? BLIT_ATTR_WIDE : 0);
    foreground = session->terminal_foreground_set ?
        session->terminal_foreground + 1 : 0;
    background = session->terminal_background_set ?
        session->terminal_background + 1 : 0;
    underline_color = session->terminal_underline_color_set ?
        session->terminal_underline_color + 1 : 0;
    if (surface->codepoints[y][x] != codepoint ||
        surface->foreground[y][x] != foreground ||
        surface->background[y][x] != background ||
        surface->underline_color[y][x] != underline_color ||
        surface->attributes[y][x] != attributes ||
        surface->links[y][x] != session->terminal_link ||
        surface->combining_count[y][x] != 0)
        return 0;
    if (cell_width == 2) {
        if (x + 1 >= surface->width ||
            surface->codepoints[y][x + 1] != 0 ||
            surface->combining_count[y][x + 1] != 0 ||
            surface->foreground[y][x + 1] != 0 ||
            surface->background[y][x + 1] != 0 ||
            surface->underline_color[y][x + 1] != 0 ||
            surface->attributes[y][x + 1] != BLIT_ATTR_CONTINUATION ||
            surface->links[y][x + 1] != session->terminal_link)
            return 0;
    } else if (x + 1 < surface->width &&
               (surface->attributes[y][x] & BLIT_ATTR_WIDE ||
                surface->attributes[y][x + 1] & BLIT_ATTR_CONTINUATION)) {
        return 0;
    }
    return 1;
}

static void terminal_clear_wide_overlap(BlitSurface *surface, int x, int y)
{
    uint16_t attributes;

    if (surface == NULL || x < 0 || y < 0 ||
        x >= surface->width || y >= surface->height)
        return;
    attributes = surface->attributes[y][x];
    if ((attributes & BLIT_ATTR_CONTINUATION) && x > 0)
        terminal_clear_cell(surface, x - 1, y);
    if ((attributes & BLIT_ATTR_WIDE) && x + 1 < surface->width &&
        (surface->attributes[y][x + 1] & BLIT_ATTR_CONTINUATION))
        terminal_clear_cell(surface, x + 1, y);
}

static void terminal_normalize_wide_row(BlitSurface *surface, int y)
{
    int x;

    if (surface == NULL || y < 0 || y >= surface->height)
        return;
    for (x = 0; x < surface->width; ++x) {
        uint16_t attributes = surface->attributes[y][x];
        if ((attributes & BLIT_ATTR_CONTINUATION) &&
            (x == 0 || !(surface->attributes[y][x - 1] & BLIT_ATTR_WIDE)))
            terminal_clear_cell(surface, x, y);
        else if ((attributes & BLIT_ATTR_WIDE) &&
                 (x + 1 >= surface->width ||
                  !(surface->attributes[y][x + 1] & BLIT_ATTR_CONTINUATION)))
            terminal_clear_cell(surface, x, y);
    }
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
    terminal_clear_wide_overlap(surface, x, y);
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

static void scrollback_line_release(BlitScrollbackLine *line)
{
    free(line->cells);
    free(line->codepoints);
    free(line->combining);
    free(line->combining_count);
    free(line->foreground);
    free(line->background);
    free(line->underline_color);
    free(line->attributes);
    free(line->links);
    memset(line, 0, sizeof(*line));
}

static int scrollback_line_prepare(BlitScrollbackLine *line, int width)
{
    if (line->width != width || line->cells == NULL)
        scrollback_line_release(line);
    if (line->cells == NULL) {
        line->cells = (char *)calloc((size_t)width, sizeof(*line->cells));
        line->codepoints = (uint32_t *)calloc((size_t)width,
                                               sizeof(*line->codepoints));
        line->combining = (uint32_t *)calloc(
            (size_t)width * BLIT_MAX_COMBINING, sizeof(*line->combining));
        line->combining_count = (unsigned char *)calloc(
            (size_t)width, sizeof(*line->combining_count));
        line->foreground = (uint32_t *)calloc((size_t)width,
                                              sizeof(*line->foreground));
        line->background = (uint32_t *)calloc((size_t)width,
                                              sizeof(*line->background));
        line->underline_color = (uint32_t *)calloc(
            (size_t)width, sizeof(*line->underline_color));
        line->attributes = (uint16_t *)calloc((size_t)width,
                                              sizeof(*line->attributes));
        line->links = (uint16_t *)calloc((size_t)width,
                                          sizeof(*line->links));
        if (line->cells == NULL || line->codepoints == NULL ||
            line->combining == NULL || line->combining_count == NULL ||
            line->foreground == NULL || line->background == NULL ||
            line->underline_color == NULL || line->attributes == NULL ||
            line->links == NULL) {
            scrollback_line_release(line);
            return 0;
        }
        line->width = width;
    }
    return 1;
}

static int scrollback_capture_row(BlitScrollbackLine *line,
                                  const BlitSurface *surface, int row)
{
    if (!scrollback_line_prepare(line, surface->width))
        return 0;
    memcpy(line->cells, surface->cells[row],
           (size_t)surface->width * sizeof(*line->cells));
    memcpy(line->codepoints, surface->codepoints[row],
           (size_t)surface->width * sizeof(*line->codepoints));
    memcpy(line->combining, surface->combining[row],
           (size_t)surface->width * BLIT_MAX_COMBINING *
           sizeof(*line->combining));
    memcpy(line->combining_count, surface->combining_count[row],
           (size_t)surface->width * sizeof(*line->combining_count));
    memcpy(line->foreground, surface->foreground[row],
           (size_t)surface->width * sizeof(*line->foreground));
    memcpy(line->background, surface->background[row],
           (size_t)surface->width * sizeof(*line->background));
    memcpy(line->underline_color, surface->underline_color[row],
           (size_t)surface->width * sizeof(*line->underline_color));
    memcpy(line->attributes, surface->attributes[row],
           (size_t)surface->width * sizeof(*line->attributes));
    memcpy(line->links, surface->links[row],
           (size_t)surface->width * sizeof(*line->links));
    return 1;
}

static void terminal_scrollback_push(BlitSurface *surface, int row)
{
    int index;

    if (surface->scrollback_count < BLIT_MAX_SCROLLBACK_LINES) {
        index = (surface->scrollback_start + surface->scrollback_count) %
            BLIT_MAX_SCROLLBACK_LINES;
    } else {
        index = surface->scrollback_start;
    }
    if (!scrollback_capture_row(&surface->scrollback[index], surface, row))
        return;
    if (surface->scrollback_count < BLIT_MAX_SCROLLBACK_LINES)
        ++surface->scrollback_count;
    else
        surface->scrollback_start = (surface->scrollback_start + 1) %
            BLIT_MAX_SCROLLBACK_LINES;
    surface->scrollback_view = 0;
}

static void terminal_scrollback_release(BlitSurface *surface)
{
    int index;

    for (index = 0; index < BLIT_MAX_SCROLLBACK_LINES; ++index)
        scrollback_line_release(&surface->scrollback[index]);
    surface->scrollback_count = 0;
    surface->scrollback_start = 0;
    surface->scrollback_view = 0;
    surface->scrollback_view_active = 0;
    surface->scrollback_saved_cursor_visible = 0;
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
    surface->links[y][destination] = surface->links[y][source];
}

static void terminal_scroll_up(BlitSession *session, BlitSurface *surface)
{
    int y;
    int top = surface->scroll_top;
    int bottom = surface->scroll_bottom;

    if (surface->height < 1 || top < 0 || bottom >= surface->height ||
        top >= bottom)
        return;
    if (session != NULL && !session->terminal_alternate && top == 0 &&
        bottom == surface->height - 1)
        terminal_scrollback_push(surface, top);
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
        memcpy(surface->links[y - 1], surface->links[y],
               sizeof(surface->links[y - 1]));
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
    memset(surface->links[bottom], 0, sizeof(surface->links[bottom]));
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
    memcpy(surface->links[destination], surface->links[source],
           sizeof(surface->links[destination]));
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
    memset(surface->links[row], 0, sizeof(surface->links[row]));
}

static void terminal_scrollback_backup_live(BlitSurface *surface)
{
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
    memcpy(surface->alternate_links, surface->links,
           sizeof(surface->links));
}

static void terminal_scrollback_show_live(BlitSurface *surface)
{
    if (surface == NULL || !surface->scrollback_view_active)
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
    memcpy(surface->links, surface->alternate_links,
           sizeof(surface->links));
    surface->scrollback_view_active = 0;
    surface->scrollback_view = 0;
    surface->cursor_visible = surface->scrollback_saved_cursor_visible;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_scrollback_show_offset(BlitSurface *surface, int view)
{
    int row;
    int start;

    if (view <= 0) {
        terminal_scrollback_show_live(surface);
        return;
    }
    if (view > surface->scrollback_count)
        view = surface->scrollback_count;
    if (view < 1)
        return;
    if (!surface->scrollback_view_active) {
        terminal_scrollback_backup_live(surface);
        surface->scrollback_view_active = 1;
        surface->scrollback_saved_cursor_visible = surface->cursor_visible;
        surface->cursor_visible = 0;
    }
    start = surface->scrollback_count - view;
    for (row = 0; row < surface->height; ++row) {
        int logical = start + row;
        terminal_blank_row(surface, row);
        if (logical < surface->scrollback_count) {
            int index = (surface->scrollback_start + logical) %
                BLIT_MAX_SCROLLBACK_LINES;
            BlitScrollbackLine *line = &surface->scrollback[index];
            int width = line->width < surface->width ? line->width :
                surface->width;
            memcpy(surface->cells[row], line->cells,
                   (size_t)width * sizeof(*line->cells));
            memcpy(surface->codepoints[row], line->codepoints,
                   (size_t)width * sizeof(*line->codepoints));
            memcpy(surface->combining[row], line->combining,
                   (size_t)width * BLIT_MAX_COMBINING *
                   sizeof(*line->combining));
            memcpy(surface->combining_count[row], line->combining_count,
                   (size_t)width * sizeof(*line->combining_count));
            memcpy(surface->foreground[row], line->foreground,
                   (size_t)width * sizeof(*line->foreground));
            memcpy(surface->background[row], line->background,
                   (size_t)width * sizeof(*line->background));
            memcpy(surface->underline_color[row], line->underline_color,
                   (size_t)width * sizeof(*line->underline_color));
            memcpy(surface->attributes[row], line->attributes,
                   (size_t)width * sizeof(*line->attributes));
            memcpy(surface->links[row], line->links,
                   (size_t)width * sizeof(*line->links));
        } else if (logical - surface->scrollback_count < surface->height) {
            int live_row = logical - surface->scrollback_count;
            memcpy(surface->cells[row], surface->alternate_cells[live_row],
                   sizeof(surface->cells[row]));
            memcpy(surface->codepoints[row],
                   surface->alternate_codepoints[live_row],
                   sizeof(surface->codepoints[row]));
            memcpy(surface->combining[row],
                   surface->alternate_combining[live_row],
                   sizeof(surface->combining[row]));
            memcpy(surface->combining_count[row],
                   surface->alternate_combining_count[live_row],
                   sizeof(surface->combining_count[row]));
            memcpy(surface->foreground[row],
                   surface->alternate_foreground[live_row],
                   sizeof(surface->foreground[row]));
            memcpy(surface->background[row],
                   surface->alternate_background[live_row],
                   sizeof(surface->background[row]));
            memcpy(surface->underline_color[row],
                   surface->alternate_underline_color[live_row],
                   sizeof(surface->underline_color[row]));
            memcpy(surface->attributes[row],
                   surface->alternate_attributes[live_row],
                   sizeof(surface->attributes[row]));
            memcpy(surface->links[row], surface->alternate_links[live_row],
                   sizeof(surface->links[row]));
        }
    }
    surface->scrollback_view = view;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = surface->width;
    surface->damage_height = surface->height;
}

static void terminal_index(BlitSession *session, BlitSurface *surface)
{
    if (surface->cursor_y < surface->scroll_bottom)
        ++surface->cursor_y;
    else
        terminal_scroll_up(session, surface);
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
    session->terminal_wrap_pending = 0;
    mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
}

static int terminal_colour_reply(const char *value, char *reply,
                                 size_t capacity)
{
    int index;

    if (value == NULL || strlen(value) != 7 || value[0] != '#')
        return 0;
    for (index = 1; index < 7; ++index) {
        char digit = value[index];
        if (!((digit >= '0' && digit <= '9') ||
              (digit >= 'a' && digit <= 'f') ||
              (digit >= 'A' && digit <= 'F')))
            return 0;
    }
    snprintf(reply, capacity, "rgb:%c%c%c%c/%c%c%c%c/%c%c%c%c",
             value[1], value[2], value[1], value[2],
             value[3], value[4], value[3], value[4],
             value[5], value[6], value[5], value[6]);
    return 1;
}

static void terminal_apply_osc(BlitSession *session)
{
    char *separator;
    const char *payload;
    int command;

    if (session->terminal_string_type != ']')
        return;
    session->terminal_string[session->terminal_string_length] = '\0';
    separator = strchr(session->terminal_string, ';');
    if (separator == NULL)
        return;
    command = atoi(session->terminal_string);
    payload = separator + 1;
    if (command == 0 || command == 1 || command == 2) {
        snprintf(session->terminal_title, sizeof(session->terminal_title),
                 "%s", payload);
    } else if ((command == 10 || command == 11) &&
               strcmp(payload, "?") == 0) {
        char colour[32];
        const char *configured = command == 10 ?
            session->terminal_default_foreground :
            session->terminal_default_background;
        if (terminal_colour_reply(configured, colour, sizeof(colour))) {
            char reply[96];
            snprintf(reply, sizeof(reply), "\033]%d;%s\033\\",
                     command, colour);
            terminal_write_reply(session, reply);
        }
    } else if (command == 52) {
        /* Keep OSC 52 in Hibr's canonical selection;base64 form.  A query
           (selection;?) is deliberately not answered or treated as a set. */
        char *query = strchr((char *)payload, ';');
        if (query != NULL && strcmp(query + 1, "?") != 0)
            snprintf(session->terminal_clipboard,
                     sizeof(session->terminal_clipboard), "%s", payload);
    } else if (command == 9) {
        /* Hibr consumes the next notification as a raw OSC payload.  A
           numeric OSC 9 progress report is not a user notification. */
        const char *cursor = payload;
        while (*cursor >= '0' && *cursor <= '9') ++cursor;
        if (*cursor != ';')
            snprintf(session->terminal_notification,
                     sizeof(session->terminal_notification), "%s", payload);
    } else if (command == 777 && strncmp(payload, "notify;", 7) == 0) {
        snprintf(session->terminal_notification,
                 sizeof(session->terminal_notification), "%s", payload + 7);
    } else if (command == 8) {
        char *uri = strchr((char *)payload, ';');
        int index;
        if (uri == NULL || uri[1] == '\0') {
            session->terminal_link = 0;
            return;
        }
        ++uri;
        for (index = 0; index < session->terminal_link_count; ++index)
            if (strcmp(session->terminal_link_uris[index], uri) == 0) {
                session->terminal_link = (uint16_t)(index + 1);
                return;
            }
        if (session->terminal_link_count >= 32) {
            session->terminal_link = 0;
            return;
        }
        snprintf(session->terminal_link_uris[session->terminal_link_count],
                 sizeof(session->terminal_link_uris[0]), "%s", uri);
        ++session->terminal_link_count;
        session->terminal_link = (uint16_t)session->terminal_link_count;
    }
}

static void graphics_image_release(BlitGraphicsImage *image)
{
    if (image == NULL) return;
    free(image->pixels);
    memset(image, 0, sizeof(*image));
}

static void graphics_reject(BlitSession *session, const char *reason)
{
    ++session->graphics_rejected;
    snprintf(session->graphics_last_error,
             sizeof(session->graphics_last_error), "%s", reason);
}

static void kitty_reply(BlitSession *session, int image_id, int quiet,
                        const char *message)
{
    char reply[160];
    int is_ok = strcmp(message, "OK") == 0;

    if (quiet == 2 || (quiet == 1 && is_ok))
        return;
    if (getenv("BLIT_KITTY_TRACE") != NULL)
        fprintf(stderr, "blit: Kitty reply image=%d quiet=%d message=%s\n",
                image_id, quiet, message);
    snprintf(reply, sizeof(reply), "\033_Gi=%d;%s\033\\", image_id, message);
    terminal_write_reply(session, reply);
}

static int kitty_cells_to_pixels(const BlitSession *session, int cells,
                                 int fallback)
{
    long long pixels;
    int cell_width = session->terminal_cell_width > 0 ?
        session->terminal_cell_width : 8;
    if (cells <= 0)
        return fallback;
    pixels = (long long)cells * cell_width;
    if (pixels > INT_MAX)
        return -1;
    return (int)pixels;
}

static int kitty_rows_to_pixels(const BlitSession *session, int rows,
                                int fallback)
{
    long long pixels;
    int cell_height = session->terminal_cell_height > 0 ?
        session->terminal_cell_height : 16;
    if (rows <= 0)
        return fallback;
    pixels = (long long)rows * cell_height;
    if (pixels > INT_MAX)
        return -1;
    return (int)pixels;
}

static BlitGraphicsImage *graphics_image_find(BlitSession *session,
                                              unsigned int id)
{
    int index;
    for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES; ++index)
        if (session->graphics[index].used && session->graphics[index].id == id)
            return &session->graphics[index];
    return NULL;
}

static int kitty_parameter(const char *header, char key, char *value,
                           size_t capacity)
{
    const char *cursor = header;
    while (*cursor != '\0') {
        const char *end = strchr(cursor, ',');
        size_t length = end != NULL ? (size_t)(end - cursor) : strlen(cursor);
        if (length >= 3 && cursor[0] == key && cursor[1] == '=') {
            size_t copy = length - 2;
            if (copy >= capacity) return 0;
            memcpy(value, cursor + 2, copy);
            value[copy] = '\0';
            return 1;
        }
        if (end == NULL) break;
        cursor = end + 1;
    }
    return 0;
}

static int kitty_parameter_int(const char *header, char key, int fallback,
                               int *value)
{
    char text[32];
    char *end;
    long parsed;
    if (!kitty_parameter(header, key, text, sizeof(text))) {
        *value = fallback;
        return 1;
    }
    parsed = strtol(text, &end, 10);
    if (*text == '\0' || *end != '\0' || parsed < -2147483647L ||
        parsed > 2147483647L)
        return 0;
    *value = (int)parsed;
    return 1;
}

static int kitty_base64_value(unsigned char byte)
{
    if (byte >= 'A' && byte <= 'Z') return byte - 'A';
    if (byte >= 'a' && byte <= 'z') return byte - 'a' + 26;
    if (byte >= '0' && byte <= '9') return byte - '0' + 52;
    if (byte == '+') return 62;
    if (byte == '/') return 63;
    return -1;
}

static int kitty_decode_chunk(BlitGraphicsImage *image, const char *payload)
{
    unsigned char quartet[4];
    int quartet_length = image->base64_tail_length;
    size_t offset = 0;
    if (quartet_length > 0)
        memcpy(quartet, image->base64_tail, (size_t)quartet_length);
    while (offset < strlen(payload)) {
        int value;
        unsigned char byte = (unsigned char)payload[offset++];
        if (byte == '=' || kitty_base64_value(byte) >= 0) {
            quartet[quartet_length++] = byte;
            if (quartet_length == 4) {
                int a = quartet[0] == '=' ? -1 : kitty_base64_value(quartet[0]);
                int b = quartet[1] == '=' ? -1 : kitty_base64_value(quartet[1]);
                int c = quartet[2] == '=' ? -1 : kitty_base64_value(quartet[2]);
                int d = quartet[3] == '=' ? -1 : kitty_base64_value(quartet[3]);
                if (a < 0 || b < 0 || (c < 0 && quartet[2] != '=') ||
                    (d < 0 && quartet[3] != '=')) return 0;
                if (image->bytes + 1 > image->capacity) return 0;
                image->pixels[image->bytes++] = (unsigned char)((a << 2) | (b >> 4));
                if (quartet[2] != '=') {
                    if (image->bytes + 1 > image->capacity) return 0;
                    image->pixels[image->bytes++] = (unsigned char)((b << 4) | (c >> 2));
                }
                if (quartet[3] != '=') {
                    if (image->bytes + 1 > image->capacity) return 0;
                    image->pixels[image->bytes++] = (unsigned char)((c << 6) | d);
                }
                quartet_length = 0;
            }
        } else if (byte != '\n' && byte != '\r' && byte != ' ' && byte != '\t') {
            return 0;
        }
    }
    if (quartet_length > 0) {
        memcpy(image->base64_tail, quartet, (size_t)quartet_length);
        image->base64_tail_length = quartet_length;
    } else {
        image->base64_tail_length = 0;
    }
    return 1;
}

static void terminal_apply_kitty(BlitSession *session, BlitSurface *surface)
{
    char *payload = session->terminal_string;
    char *separator;
    char action[8];
    int image_id = 0;
    int format = 0;
    int width = 0;
    int height = 0;
    int more = 0;
    int x = surface->cursor_x * (session->terminal_cell_width > 0 ?
        session->terminal_cell_width : 8);
    int y = surface->cursor_y * (session->terminal_cell_height > 0 ?
        session->terminal_cell_height : 16);
    int placement_columns = 0;
    int placement_rows = 0;
    int placement_width = 0;
    int placement_height = 0;
    int z = 0;
    int quiet = 0;
    int display_on_complete = 0;
    int has_action;
    BlitGraphicsImage *image;
    int index;

    payload[session->terminal_string_length] = '\0';
    if (payload[0] == 'G') ++payload;
    separator = strchr(payload, ';');
    if (separator == NULL) {
        graphics_reject(session, "missing-payload");
        return;
    }
    *separator = '\0';
    has_action = kitty_parameter(payload, 'a', action, sizeof(action));
    if (!has_action) action[0] = '\0';
    if (!kitty_parameter_int(payload, 'i', 0, &image_id) || image_id < 0 ||
        !kitty_parameter_int(payload, 'm', 0, &more) ||
        !kitty_parameter_int(payload, 'x', x, &x) ||
        !kitty_parameter_int(payload, 'y', y, &y) ||
        !kitty_parameter_int(payload, 'c', 0, &placement_columns) ||
        !kitty_parameter_int(payload, 'r', 0, &placement_rows) ||
        !kitty_parameter_int(payload, 'z', 0, &z) ||
        !kitty_parameter_int(payload, 'q', 0, &quiet) || quiet < 0 || quiet > 2) {
        graphics_reject(session, "invalid-parameters");
        return;
    }
    placement_width = kitty_cells_to_pixels(session, placement_columns, 0);
    placement_height = kitty_rows_to_pixels(session, placement_rows, 0);
    if (placement_width < 0 || placement_height < 0) {
        graphics_reject(session, "placement-bounds");
        return;
    }
    /* Hibr emits the protocol's canonical a=T form.  The following chunks
       deliberately omit a and carry only q=2, so an absent action means a
       continuation of the already allocated image. */
    if (strcmp(action, "T") == 0) {
        display_on_complete = 1;
        snprintf(action, sizeof(action), "t");
    }
    if (strcmp(action, "d") == 0) {
        if (image_id == 0) {
            for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES; ++index)
                graphics_image_release(&session->graphics[index]);
        } else {
            image = graphics_image_find(session, (unsigned int)image_id);
            if (image != NULL) graphics_image_release(image);
        }
        mark_surface_full_damage(surface);
        return;
    }
    if (strcmp(action, "q") == 0) {
        BlitGraphicsImage query;
        char response[64];
        memset(&query, 0, sizeof(query));
        if (image_id == 0 || !kitty_parameter_int(payload, 'f', 0, &format) ||
            !kitty_parameter_int(payload, 's', 0, &width) ||
            !kitty_parameter_int(payload, 'v', 0, &height) ||
            (format != 24 && format != 32) || width < 1 ||
            width > BLIT_GRAPHICS_MAX_WIDTH || height < 1 ||
            height > BLIT_GRAPHICS_MAX_HEIGHT) {
            graphics_reject(session, "query-bounds");
            kitty_reply(session, image_id, quiet, "EINVAL");
            return;
        }
        query.capacity = (size_t)width * (size_t)height *
            (format == 24 ? 3U : 4U);
        query.pixels = (unsigned char *)malloc(query.capacity);
        if (query.pixels == NULL ||
            !kitty_decode_chunk(&query, separator + 1) ||
            query.base64_tail_length != 0 || query.bytes != query.capacity) {
            free(query.pixels);
            graphics_reject(session, "query-data");
            kitty_reply(session, image_id, quiet, "EINVAL");
            return;
        }
        free(query.pixels);
        snprintf(response, sizeof(response), "OK");
        kitty_reply(session, image_id, quiet, response);
        return;
    }
    if (strcmp(action, "p") == 0) {
        image = graphics_image_find(session, (unsigned int)image_id);
        if (image == NULL || !image->complete) {
            graphics_reject(session, "placement-incomplete");
            return;
        }
        image->visible = 1;
        image->x = x;
        image->y = y;
        image->placement_width = placement_width > 0 ? placement_width : image->width;
        image->placement_height = placement_height > 0 ? placement_height : image->height;
        image->z = z;
        ++image->revision;
        mark_surface_full_damage(surface);
        return;
    }
    if ((action[0] != '\0' && strcmp(action, "t") != 0) ||
        (action[0] == '\0' && image_id == 0) || more < 0 || more > 1) {
        graphics_reject(session, "unsupported-action");
        return;
    }
    image = graphics_image_find(session, (unsigned int)image_id);
    if (action[0] == '\0' && (image == NULL || image->complete)) {
        graphics_reject(session, "continuation-incomplete");
        return;
    }
    if (action[0] != '\0' &&
        (!kitty_parameter_int(payload, 'f', 0, &format) ||
         !kitty_parameter_int(payload, 's', 0, &width) ||
         !kitty_parameter_int(payload, 'v', 0, &height))) {
        graphics_reject(session, "invalid-dimensions");
        return;
    }
    if (image != NULL && strcmp(image->protocol, "kitty") != 0) {
        graphics_reject(session, "image-protocol-mismatch");
        return;
    }
    if (image != NULL) {
        if (format == 0) format = image->format;
        if (width == 0) width = image->width;
        if (height == 0) height = image->height;
    }
    if ((format != 24 && format != 32) || width < 1 ||
        width > BLIT_GRAPHICS_MAX_WIDTH || height < 1 ||
        height > BLIT_GRAPHICS_MAX_HEIGHT) {
        graphics_reject(session, "image-bounds");
        return;
    }
    if (image == NULL) {
        for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES; ++index)
            if (!session->graphics[index].used) {
                image = &session->graphics[index];
                break;
            }
        if (image == NULL) {
            for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES; ++index)
                if (session->graphics[index].used &&
                    !session->graphics[index].complete) {
                    graphics_image_release(&session->graphics[index]);
                    ++session->graphics_evicted;
                    image = &session->graphics[index];
                    break;
                }
        }
        if (image == NULL) {
            graphics_reject(session, "image-store-full");
            return;
        }
        memset(image, 0, sizeof(*image));
        image->used = 1;
        image->id = (unsigned int)image_id;
        snprintf(image->protocol, sizeof(image->protocol), "kitty");
        image->width = width;
        image->height = height;
        image->format = format;
        /* Kitty RGBA images may contain transparency; only API pixel frames
           prove opacity incrementally before using the packed KMS path. */
        image->opaque = 0;
        image->capacity = (size_t)width * (size_t)height *
            (format == 24 ? 3U : 4U);
        if (image->capacity > BLIT_GRAPHICS_MAX_BYTES) {
            graphics_image_release(image);
            return;
        }
        image->pixels = (unsigned char *)malloc(image->capacity);
        if (image->pixels == NULL) {
            graphics_image_release(image);
            graphics_reject(session, "allocation-failed");
            return;
        }
        image->revision = 1;
        image->generation = next_graphics_generation();
    }
    if (display_on_complete) {
        /* Preserve a=T's display intent while later q=2 chunks arrive.  The
           renderer still waits for complete, so this cannot expose partial
           image data. */
        image->visible = 1;
        image->x = x;
        image->y = y;
        image->placement_width = placement_width > 0 ? placement_width : image->width;
        image->placement_height = placement_height > 0 ? placement_height : image->height;
        image->z = z;
    }
    if (image->width != width || image->height != height || image->format != format ||
        !kitty_decode_chunk(image, separator + 1)) {
        graphics_image_release(image);
        graphics_reject(session, "invalid-data");
        return;
    }
    if (!more) {
        if (image->base64_tail_length != 0 || image->bytes != image->capacity) {
            graphics_image_release(image);
            graphics_reject(session, "incomplete-data");
            return;
        }
        image->complete = 1;
        image->generation = next_graphics_generation();
        if (display_on_complete) {
            image->visible = 1;
            image->x = x;
            image->y = y;
            image->placement_width = placement_width > 0 ? placement_width : image->width;
            image->placement_height = placement_height > 0 ? placement_height : image->height;
            image->z = z;
        }
        /* Image composition is part of the surface scene, not terminal cell
           state.  Invalidate the scene when the final Kitty chunk arrives so
           SDL/KMS cannot skip the newly completed image. */
        mark_surface_full_damage(surface);
    }
}

static int sixel_number(const char **cursor, int fallback)
{
    char *end;
    long value;
    if (**cursor < '0' || **cursor > '9')
        return fallback;
    value = strtol(*cursor, &end, 10);
    if (value < 0 || value > 2147483647L)
        return -1;
    *cursor = end;
    return (int)value;
}

static void terminal_apply_sixel(BlitSession *session, BlitSurface *surface)
{
    unsigned char *scratch;
    uint32_t palette[256];
    const char *cursor = session->terminal_string + 1;
    int current_color = 0;
    int x = 0;
    int y = 0;
    int maximum_x = 0;
    int maximum_y = 0;
    int raster_width = 0;
    int raster_height = 0;
    int i;
    BlitGraphicsImage *image = NULL;
    int image_id = 1;

    scratch = (unsigned char *)calloc(512U * 512U, 4U);
    if (scratch == NULL) {
        graphics_reject(session, "sixel-allocation-failed");
        return;
    }
    for (i = 0; i < 256; ++i)
        palette[i] = 0xffffffU;
    palette[0] = 0x000000U;
    while (*cursor != '\0') {
        unsigned char command = (unsigned char)*cursor++;
        if (command == '"') {
            int aspect_y = sixel_number(&cursor, 1);
            int aspect_x;
            int width;
            int height;
            if (*cursor == ';') ++cursor;
            aspect_x = sixel_number(&cursor, 1);
            if (*cursor == ';') ++cursor;
            width = sixel_number(&cursor, 0);
            if (*cursor == ';') ++cursor;
            height = sixel_number(&cursor, 0);
            (void)aspect_y;
            (void)aspect_x;
            if (width < 0 || height < 0 || width > 512 || height > 512) {
                free(scratch);
                graphics_reject(session, "sixel-bounds");
                return;
            }
            raster_width = width;
            raster_height = height;
        } else if (command == '#') {
            int colour = sixel_number(&cursor, -1);
            if (colour < 0 || colour > 255) {
                free(scratch);
                graphics_reject(session, "sixel-colour");
                return;
            }
            current_color = colour;
            if (*cursor == ';') {
                int mode;
                int red;
                int green;
                int blue;
                ++cursor;
                mode = sixel_number(&cursor, -1);
                if (*cursor == ';') ++cursor;
                red = sixel_number(&cursor, -1);
                if (*cursor == ';') ++cursor;
                green = sixel_number(&cursor, -1);
                if (*cursor == ';') ++cursor;
                blue = sixel_number(&cursor, -1);
                if (mode != 2 || red < 0 || red > 100 || green < 0 ||
                    green > 100 || blue < 0 || blue > 100) {
                    free(scratch);
                    graphics_reject(session, "sixel-colour");
                    return;
                }
                palette[current_color] =
                    ((uint32_t)(red * 255 / 100) << 16) |
                    ((uint32_t)(green * 255 / 100) << 8) |
                    (uint32_t)(blue * 255 / 100);
            }
        } else if (command == '!') {
            int repeat = sixel_number(&cursor, -1);
            unsigned char sixel;
            int bit;
            if (repeat < 1 || repeat > 512 || *cursor < '?' || *cursor > '~') {
                free(scratch);
                graphics_reject(session, "sixel-repeat");
                return;
            }
            sixel = (unsigned char)*cursor++ - 63U;
            for (i = 0; i < repeat; ++i) {
                for (bit = 0; bit < 6; ++bit)
                    if (sixel & (1U << bit)) {
                        if (x >= 512 || y + bit >= 512) {
                            free(scratch);
                            graphics_reject(session, "sixel-bounds");
                            return;
                        }
                        {
                            size_t pixel_offset = ((size_t)(y + bit) * 512U +
                                (size_t)x) * 4U;
                            uint32_t colour = palette[current_color];
                            scratch[pixel_offset] = (unsigned char)(colour >> 16);
                            scratch[pixel_offset + 1U] = (unsigned char)(colour >> 8);
                            scratch[pixel_offset + 2U] = (unsigned char)colour;
                            scratch[pixel_offset + 3U] = 255;
                        }
                        if (x + 1 > maximum_x) maximum_x = x + 1;
                        if (y + bit + 1 > maximum_y) maximum_y = y + bit + 1;
                    }
                ++x;
            }
        } else if (command == '$') {
            x = 0;
        } else if (command == '-') {
            x = 0;
            y += 6;
        } else if (command >= '?' && command <= '~') {
            unsigned char sixel = command - 63U;
            int bit;
            if (x >= 512 || y >= 512 || y + 5 >= 512) {
                free(scratch);
                graphics_reject(session, "sixel-bounds");
                return;
            }
            for (bit = 0; bit < 6; ++bit)
                if (sixel & (1U << bit)) {
                    {
                        size_t pixel_offset = ((size_t)(y + bit) * 512U +
                            (size_t)x) * 4U;
                        uint32_t colour = palette[current_color];
                        scratch[pixel_offset] = (unsigned char)(colour >> 16);
                        scratch[pixel_offset + 1U] = (unsigned char)(colour >> 8);
                        scratch[pixel_offset + 2U] = (unsigned char)colour;
                        scratch[pixel_offset + 3U] = 255;
                    }
                    if (x + 1 > maximum_x) maximum_x = x + 1;
                    if (y + bit + 1 > maximum_y) maximum_y = y + bit + 1;
                }
            ++x;
        }
    }
    if (raster_width > 0 && maximum_x > raster_width)
        maximum_x = raster_width;
    if (raster_height > 0 && maximum_y > raster_height)
        maximum_y = raster_height;
    if (maximum_x < 1 || maximum_y < 1) {
        free(scratch);
        graphics_reject(session, "sixel-empty");
        return;
    }
    if (raster_width > maximum_x) maximum_x = raster_width;
    if (raster_height > maximum_y) maximum_y = raster_height;
    for (i = 0; i < BLIT_GRAPHICS_MAX_IMAGES; ++i)
        if (!session->graphics[i].used) {
            image = &session->graphics[i];
            break;
        }
    if (image == NULL) {
        free(scratch);
        graphics_reject(session, "image-store-full");
        return;
    }
    while (graphics_image_find(session, (unsigned int)image_id) != NULL) {
        ++image_id;
        if (image_id <= 0) image_id = 1;
    }
    memset(image, 0, sizeof(*image));
    image->pixels = (unsigned char *)malloc((size_t)maximum_x *
                                             (size_t)maximum_y * 4U);
    if (image->pixels == NULL) {
        free(scratch);
        graphics_reject(session, "sixel-allocation-failed");
        return;
    }
    image->used = 1;
    image->complete = 1;
    image->id = (unsigned int)image_id;
    snprintf(image->protocol, sizeof(image->protocol), "sixel");
    image->width = maximum_x;
    image->height = maximum_y;
    image->format = 32;
    image->opaque = 0;
    image->bytes = image->capacity = (size_t)maximum_x *
        (size_t)maximum_y * 4U;
    for (y = 0; y < maximum_y; ++y)
        memcpy(image->pixels + (size_t)y * (size_t)maximum_x * 4U,
               scratch + (size_t)y * 512U * 4U,
               (size_t)maximum_x * 4U);
    image->visible = 1;
    image->x = surface->cursor_x * (session->terminal_cell_width > 0 ?
        session->terminal_cell_width : 8);
    image->y = surface->cursor_y * (session->terminal_cell_height > 0 ?
        session->terminal_cell_height : 16);
    image->placement_width = maximum_x;
    image->placement_height = maximum_y;
    image->revision = 1;
    image->generation = next_graphics_generation();
    free(scratch);
    mark_surface_full_damage(surface);
}

static void terminal_apply_string(BlitSession *session, BlitSurface *surface)
{
    const char *payload = session->terminal_string;
    char reply[512];
    int used;

    session->terminal_string[session->terminal_string_length] = '\0';
    if (session->terminal_string_type == '_') {
        terminal_apply_kitty(session, surface);
        return;
    }
    if (session->terminal_string_type != 'P')
        return;
    if (payload[0] == 'q') {
        terminal_apply_sixel(session, surface);
        return;
    }
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

    /* Any cursor-addressing/control sequence consumes a deferred wrap. */
    session->terminal_wrap_pending = 0;

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
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
            if (enabled) {
                int alternate_mode = terminal_csi_has_mode(sequence, 1049) ?
                    1049 : terminal_csi_has_mode(sequence, 1047) ?
                    1047 : 47;
                terminal_enter_alternate(session, surface, alternate_mode);
            }
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
                mark_cursor_damage(surface, surface->cursor_x,
                                   surface->cursor_y);
            }
        }
        if (terminal_csi_has_mode(sequence, 1))
            session->terminal_application_cursor = enabled;
        if (terminal_csi_has_mode(sequence, 6)) {
            session->terminal_origin_mode = enabled;
            surface->cursor_x = 0;
            surface->cursor_y = enabled ? surface->scroll_top : 0;
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
            int cursor_was_visible = surface->cursor_visible;
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
            surface->cursor_visible = enabled;
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
            /* The cursor is composited into the cached scene texture.  A
             * visibility transition must invalidate the complete surface:
             * the old cursor can otherwise survive as pixels even though the
             * canonical cell state and cursor state are already hidden. */
            if (cursor_was_visible != enabled)
                mark_surface_full_damage(surface);
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
            /* Match Hibr's VT220-compatible primary device identity. */
            terminal_write_reply(session, "\033[?62;22c");
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
            terminal_enter_alternate(session, surface, first);
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
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
        int cursor_was_visible = surface->cursor_visible;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
        surface->cursor_visible = command == 'h';
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
        if (cursor_was_visible != surface->cursor_visible)
            mark_surface_full_damage(surface);
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
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'A' || command == 'B' ||
               command == 'C' || command == 'D') {
        int amount = has_first && first > 0 ? first : 1;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'a') {
        int amount = has_first && first > 0 ? first : 1;
        surface->cursor_x += amount;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'e') {
        int amount = has_first && first > 0 ? first : 1;
        surface->cursor_y += amount;
        if (session->terminal_origin_mode) {
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y >= surface->height) {
            surface->cursor_y = surface->height - 1;
        }
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == '@' || command == 'P') {
        int amount = has_first && first > 0 ? first : 1;
        int x;
        if (amount > surface->width)
            amount = surface->width;
        if (command == '@') {
            if (amount == 1 &&
                (surface->attributes[surface->cursor_y][surface->cursor_x] &
                 BLIT_ATTR_CONTINUATION)) {
                /* Inserting at the second half of a wide glyph replaces the
                   glyph's two-cell footprint; it must not move the following
                   character left or manufacture an orphan continuation. */
                terminal_clear_cell(surface, surface->cursor_x - 1,
                                    surface->cursor_y);
                terminal_clear_cell(surface, surface->cursor_x,
                                    surface->cursor_y);
            } else {
                for (x = surface->width - 1;
                     x >= surface->cursor_x + amount; --x) {
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
                    surface->links[surface->cursor_y][x] =
                        surface->links[surface->cursor_y][source];
                }
                for (x = surface->cursor_x;
                     x < surface->cursor_x + amount && x < surface->width; ++x)
                    terminal_clear_cell(surface, x, surface->cursor_y);
            }
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
                surface->links[surface->cursor_y][x] =
                    surface->links[surface->cursor_y][source];
            }
            for (x = surface->width - amount; x < surface->width; ++x)
                terminal_clear_cell(surface, x, surface->cursor_y);
        }
        terminal_normalize_wide_row(surface, surface->cursor_y);
        mark_surface_damage(surface, 0, surface->cursor_y);
        if (surface->damage_width < surface->width)
            surface->damage_width = surface->width;
    } else if (command == 'Z') {
        int count = has_first && first > 0 ? first : 1;
        while (count-- > 0 && surface->cursor_x > 0)
            surface->cursor_x = terminal_previous_tab(surface,
                                                      surface->cursor_x);
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'I') {
        int count = has_first && first > 0 ? first : 1;
        while (count-- > 0)
            surface->cursor_x = terminal_next_tab(surface, surface->cursor_x);
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
        /* VT line insertion/deletion leaves the cursor at column zero. */
        surface->cursor_x = 0;
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
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'G' || command == '`') {
        surface->cursor_x = has_first && first > 0 ? first - 1 : 0;
        if (surface->cursor_x >= surface->width)
            surface->cursor_x = surface->width - 1;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
    } else if (command == 'd') {
        surface->cursor_y = has_first && first > 0 ? first - 1 : 0;
        if (session->terminal_origin_mode) {
            surface->cursor_y += surface->scroll_top;
            if (surface->cursor_y > surface->scroll_bottom)
                surface->cursor_y = surface->scroll_bottom;
        } else if (surface->cursor_y >= surface->height)
            surface->cursor_y = surface->height - 1;
        mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
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
            session->terminal_frame_batch = 1;
            for (y = 0; y < surface->height; ++y)
                terminal_erase_line(session, surface, y, 0);
            if (mode == 3)
                terminal_scrollback_release(surface);
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

static void terminal_add_combining(BlitSession *session, BlitSurface *surface,
                                   uint32_t codepoint)
{
    int x = session->terminal_wrap_pending ? surface->cursor_x :
        surface->cursor_x - 1;
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
    int glyph_x;
    int unchanged = 0;

    session->terminal_last_codepoint = codepoint;
    if (session->terminal_wrap_pending && session->terminal_autowrap) {
        surface->cursor_x = 0;
        terminal_index(session, surface);
        session->terminal_wrap_pending = 0;
    }
    if (cell_width == 2 && surface->cursor_x >= surface->width - 1) {
        if (session->terminal_autowrap) {
            surface->cursor_x = 0;
            terminal_index(session, surface);
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
                terminal_scroll_up(session, surface);
        } else {
            surface->cursor_x = surface->width - 1;
        }
    }
    session->terminal_last_width = cell_width;
    if (!session->terminal_insert_mode)
        unchanged = terminal_cell_matches(session, surface,
                                          surface->cursor_x,
                                          surface->cursor_y, codepoint,
                                          cell_width);
    if (unchanged)
        goto advance_cursor;
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
    glyph_x = surface->cursor_x;
    if (surface->attributes[surface->cursor_y][glyph_x] &
        BLIT_ATTR_CONTINUATION)
        mark_surface_damage(surface, glyph_x - 1, surface->cursor_y);
    terminal_clear_wide_overlap(surface, glyph_x, surface->cursor_y);
    terminal_clear_cell(surface, glyph_x, surface->cursor_y);
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
    surface->links[surface->cursor_y][surface->cursor_x] =
        session->terminal_link;
    if (cell_width == 2) {
        terminal_clear_cell(surface, surface->cursor_x + 1,
                            surface->cursor_y);
        surface->attributes[surface->cursor_y][surface->cursor_x + 1] =
            BLIT_ATTR_CONTINUATION;
        surface->links[surface->cursor_y][surface->cursor_x + 1] =
            session->terminal_link;
    }
    mark_surface_damage_width(surface, glyph_x, surface->cursor_y,
                              cell_width);
advance_cursor:
    if (surface->cursor_x + cell_width >= surface->width) {
        surface->cursor_x = surface->width - 1;
        session->terminal_wrap_pending = session->terminal_autowrap;
    } else {
        surface->cursor_x += cell_width;
        session->terminal_wrap_pending = 0;
    }
}

static int terminal_consume_output(BlitSession *session, BlitSurface *surface,
                                   const unsigned char *data, size_t length)
{
    unsigned char combined[sizeof(session->terminal_utf8_pending) + 768];
    size_t offset = 0;

    /* A PTY is a byte stream, not a message transport. Join a pending UTF-8
       prefix with this read before parsing controls or printable cells. */
    if (session->terminal_utf8_pending_length > 0) {
        size_t pending = (size_t)session->terminal_utf8_pending_length;
        if (pending > sizeof(session->terminal_utf8_pending) ||
            length > sizeof(combined) - pending)
            return 0;
        memcpy(combined, session->terminal_utf8_pending, pending);
        memcpy(combined + pending, data, length);
        data = combined;
        length += pending;
        session->terminal_utf8_pending_length = 0;
    }

    /* Any program output belongs to the live screen.  A historical view is
       only a presentation state and must never become the parser's target. */
    terminal_scrollback_show_live(surface);

    while (offset < length) {
        unsigned char byte = data[offset];
        if (session->terminal_escape == 1) {
            session->terminal_wrap_pending = 0;
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
                terminal_index(session, surface);
                session->terminal_escape = 0;
            } else if (byte == 'M') {
                terminal_reverse_index(surface);
                session->terminal_escape = 0;
            } else if (byte == 'E') {
                surface->cursor_x = 0;
                terminal_index(session, surface);
                session->terminal_escape = 0;
            } else if (byte == 'c') {
                if (session->terminal_alternate)
                    terminal_leave_alternate(session, surface);
                terminal_clear_active_screen(surface);
                session->terminal_frame_batch = 1;
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
                session->terminal_wrap_pending = 0;
                session->terminal_focus_reporting = 0;
                session->terminal_bracketed_paste = 0;
                session->terminal_mouse_mode = 0;
                session->terminal_mouse_sgr = 0;
                session->terminal_synchronized = 0;
                session->terminal_alternate_mode = 0;
                terminal_scrollback_release(surface);
                session->terminal_foreground = 0;
                session->terminal_background = 0;
                session->terminal_underline_color = 0;
                session->terminal_underline_color_set = 0;
                session->terminal_attributes = 0;
                session->terminal_foreground_set = 0;
                session->terminal_background_set = 0;
                session->terminal_escape = 0;
            } else if (byte == 'Z') {
                /* VT100/VT500 DECID is the legacy spelling of DA1. */
                terminal_write_reply(session, "\033[?62;22c");
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
                    mark_cursor_damage(surface, surface->cursor_x,
                                       surface->cursor_y);
                    surface->cursor_x = 0;
                    session->terminal_wrap_pending = 0;
                } else if (byte == '\n' || byte == '\v' || byte == '\f') {
                    mark_cursor_damage(surface, surface->cursor_x,
                                       surface->cursor_y);
                    if (session->terminal_newline_mode)
                        surface->cursor_x = 0;
                    terminal_index(session, surface);
                    session->terminal_wrap_pending = 0;
                } else if (byte == '\b') {
                    if (session->terminal_wrap_pending)
                        session->terminal_wrap_pending = 0;
                    else if (surface->cursor_x > 0) --surface->cursor_x;
                    mark_cursor_damage(surface, surface->cursor_x,
                                       surface->cursor_y);
                } else {
                    surface->cursor_x = terminal_next_tab(surface,
                                                          surface->cursor_x);
                    session->terminal_wrap_pending = 0;
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
            session->terminal_wrap_pending = 0;
            session->terminal_escape = 1;
            ++offset;
        } else if (byte == 0x9b) {
            session->terminal_wrap_pending = 0;
            session->terminal_escape = 2;
            session->terminal_csi_length = 0;
            ++offset;
        } else if (byte == 0x9d || byte == 0x90 || byte == 0x98 ||
                   byte == 0x9e || byte == 0x9f) {
            session->terminal_wrap_pending = 0;
            session->terminal_escape = 3;
            session->terminal_string_type = byte == 0x9d ? ']' : byte;
            session->terminal_string_length = 0;
            ++offset;
        } else if (byte == '\r') {
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
            surface->cursor_x = 0;
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == '\n' || byte == '\v' || byte == '\f') {
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
            if (session->terminal_newline_mode)
                surface->cursor_x = 0;
            terminal_index(session, surface);
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == '\b') {
            if (session->terminal_wrap_pending)
                session->terminal_wrap_pending = 0;
            else if (surface->cursor_x > 0)
                --surface->cursor_x;
            mark_cursor_damage(surface, surface->cursor_x, surface->cursor_y);
            ++offset;
        } else if (byte == '\t') {
            surface->cursor_x = terminal_next_tab(surface, surface->cursor_x);
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == 0x0e) {
            session->terminal_charset_shift = 1;
            ++offset;
        } else if (byte == 0x0f) {
            session->terminal_charset_shift = 0;
            ++offset;
        } else if (byte == 0x84) {
            /* C1 IND: index without carriage return. */
            terminal_index(session, surface);
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == 0x85) {
            /* C1 NEL: next line, equivalent to CR followed by IND. */
            mark_cursor_damage(surface, surface->cursor_x,
                               surface->cursor_y);
            surface->cursor_x = 0;
            terminal_index(session, surface);
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == 0x88) {
            /* C1 HTS: set a tab stop at the current column. */
            if (surface->cursor_x >= 0 &&
                surface->cursor_x < surface->width)
                surface->tab_stops[surface->cursor_x] = 1;
            session->terminal_wrap_pending = 0;
            ++offset;
        } else if (byte == 0x8d) {
            /* C1 RI: reverse index within the active scroll region. */
            terminal_reverse_index(surface);
            session->terminal_wrap_pending = 0;
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
                if (utf8_prefix_is_incomplete(data + offset,
                                              length - offset)) {
                    size_t pending = length - offset;
                    if (pending > sizeof(session->terminal_utf8_pending))
                        pending = sizeof(session->terminal_utf8_pending);
                    memcpy(session->terminal_utf8_pending, data + offset,
                           pending);
                    session->terminal_utf8_pending_length = (int)pending;
                    break;
                }
                consumed = 1;
                codepoint = '?';
            } else if (unicode_cell_width(codepoint) == 0) {
                terminal_add_combining(session, surface, codepoint);
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

static void retire_session_surface(BlitRuntime *runtime,
                                   BlitSession *session)
{
    BlitSurface *surface;
    char target_id[128];

    if (runtime == NULL || session == NULL)
        return;
    if (session->surface_retired)
        return;
    surface = find_surface_id(runtime, session->id);
    if (surface != NULL && surface->used) {
        terminal_clear_active_screen(surface);
        surface->frame_open = 0;
        surface->cursor_visible = 0;
        ++surface->revision;
    }
    snprintf(target_id, sizeof(target_id), "session-%llu", session->id);
    disable_input_routes_for_target(runtime, target_id);
    session->focused = 0;
    session->surface_retired = 1;
    runtime->render_pending = 1;
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

static void disable_input_routes_for_target(BlitRuntime *runtime,
                                            const char *target_id)
{
    int i;
    int changed = 0;

    if (runtime == NULL || target_id == NULL || target_id[0] == '\0')
        return;
    for (i = 0; i < 16; ++i) {
        BlitInputRoute *route = &runtime->input_routes[i];
        if (route->used && route->enabled &&
            strcmp(route->target_session_id, target_id) == 0) {
            route->enabled = 0;
            changed = 1;
        }
    }
    if (changed)
        ++runtime->input_routes_revision;
}

static void remove_evdev_mouse(BlitRuntime *runtime, int index)
{
    int i;
    if (index < 0 || index >= runtime->evdev_mouse_count)
        return;
    if (runtime->evdev_mouse_fds[index] >= 0)
        close(runtime->evdev_mouse_fds[index]);
    for (i = index; i + 1 < runtime->evdev_mouse_count; ++i) {
        runtime->evdev_mouse_fds[i] = runtime->evdev_mouse_fds[i + 1];
        snprintf(runtime->evdev_mouse_paths[i],
                 sizeof(runtime->evdev_mouse_paths[i]), "%s",
                 runtime->evdev_mouse_paths[i + 1]);
        runtime->evdev_mouse_reported[i] = runtime->evdev_mouse_reported[i + 1];
    }
    --runtime->evdev_mouse_count;
    runtime->evdev_mouse_fds[runtime->evdev_mouse_count] = -1;
    runtime->evdev_mouse_paths[runtime->evdev_mouse_count][0] = '\0';
    runtime->evdev_mouse_reported[runtime->evdev_mouse_count] = 0;
    /* Keep the configured route across hotplug.  Device availability is
       reported independently; a reconnect should resume the same route
       without requiring a controller to recreate it. */
    runtime->evdev_rescan_requested = 1;
}

static void remove_evdev_keyboard(BlitRuntime *runtime, int index)
{
    int i;
    if (index < 0 || index >= runtime->evdev_keyboard_count)
        return;
    if (runtime->evdev_keyboard_fds[index] >= 0)
        close(runtime->evdev_keyboard_fds[index]);
    for (i = index; i + 1 < runtime->evdev_keyboard_count; ++i) {
        runtime->evdev_keyboard_fds[i] = runtime->evdev_keyboard_fds[i + 1];
        snprintf(runtime->evdev_keyboard_paths[i],
                 sizeof(runtime->evdev_keyboard_paths[i]), "%s",
                 runtime->evdev_keyboard_paths[i + 1]);
        runtime->evdev_keyboard_reported[i] =
            runtime->evdev_keyboard_reported[i + 1];
    }
    --runtime->evdev_keyboard_count;
    runtime->evdev_keyboard_fds[runtime->evdev_keyboard_count] = -1;
    runtime->evdev_keyboard_paths[runtime->evdev_keyboard_count][0] = '\0';
    runtime->evdev_keyboard_reported[runtime->evdev_keyboard_count] = 0;
    /* Keep the configured route across hotplug; it becomes effective again
       when a keyboard is discovered. */
    runtime->evdev_rescan_requested = 1;
}

static void remove_evdev_gamepad(BlitRuntime *runtime, int index)
{
    int i;
    if (index < 0 || index >= runtime->evdev_gamepad_count)
        return;
    if (runtime->evdev_gamepad_fds[index] >= 0)
        close(runtime->evdev_gamepad_fds[index]);
    for (i = index; i + 1 < runtime->evdev_gamepad_count; ++i) {
        runtime->evdev_gamepad_fds[i] = runtime->evdev_gamepad_fds[i + 1];
        snprintf(runtime->evdev_gamepad_paths[i],
                 sizeof(runtime->evdev_gamepad_paths[i]), "%s",
                 runtime->evdev_gamepad_paths[i + 1]);
        runtime->evdev_gamepad_reported[i] =
            runtime->evdev_gamepad_reported[i + 1];
    }
    --runtime->evdev_gamepad_count;
    runtime->evdev_gamepad_fds[runtime->evdev_gamepad_count] = -1;
    runtime->evdev_gamepad_paths[runtime->evdev_gamepad_count][0] = '\0';
    runtime->evdev_gamepad_reported[runtime->evdev_gamepad_count] = 0;
    /* Keep the configured route across hotplug; availability is observable
       through the device inventory and diagnostics. */
    runtime->evdev_rescan_requested = 1;
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

static int display_index_for_id(const BlitRuntime *runtime,
                                const char *display_id)
{
    char expected[128];
    int i;

    for (i = 0; i < runtime->display_count; ++i) {
        snprintf(expected, sizeof(expected), "display-%llu",
                 runtime->displays[i].id);
        if (strcmp(display_id, expected) == 0)
            return i;
    }
    return -1;
}

static int display_index_at(const BlitRuntime *runtime, float x, float y)
{
    int i;

    for (i = 0; i < runtime->display_count; ++i) {
        const SDL_Rect *bounds = &runtime->displays[i].bounds;
        if (x >= bounds->x && x < bounds->x + bounds->w &&
            y >= bounds->y && y < bounds->y + bounds->h)
            return i;
    }
    return -1;
}

static void apply_display_placements(BlitRuntime *runtime,
                                     char ids[][128], int *xs, int *ys,
                                     int count)
{
    int i;
    int output_index;

    for (i = 0; i < count; ++i) {
        int display_index = display_index_for_id(runtime, ids[i]);
        if (display_index < 0) continue;
        runtime->displays[display_index].bounds.x = xs[i];
        runtime->displays[display_index].bounds.y = ys[i];
        if (runtime->direct_kms)
            blit_kms_set_display_position(runtime->kms, display_index,
                                          xs[i], ys[i]);
    }
    if (!runtime->direct_kms) {
        for (output_index = 0; output_index < runtime->output_count;
             ++output_index) {
            for (i = 0; i < count; ++i) {
                int display_index = display_index_for_id(runtime, ids[i]);
                if (display_index >= 0 &&
                    runtime->outputs[output_index].display_id ==
                    runtime->displays[display_index].id)
                    SDL_SetWindowPosition(runtime->outputs[output_index].window,
                                          xs[i], ys[i]);
            }
        }
    }
    for (output_index = 0; output_index < runtime->output_count;
         ++output_index)
        runtime->outputs[output_index].initialized = 0;
    if (runtime->direct_kms)
        runtime->pointer_output = output_index_for_display_index(runtime,
            display_index_at(runtime, runtime->pointer_x,
                             runtime->pointer_y));
    runtime->pointer_dirty = 1;
    runtime->render_pending = 1;
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

static void refresh_session_state(BlitRuntime *runtime, BlitSession *session)
{
    int status;
    pid_t result;
    char target_id[128];

    if (!session->used ||
        (strcmp(session->state, "running") != 0 &&
         strcmp(session->state, "starting") != 0))
        return;
    snprintf(target_id, sizeof(target_id), "session-%llu", session->id);
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
        session->child_exited = 1;
        /* Keep the PTY readable after waitpid reaps the child.  The child may
           have emitted final terminal bytes (including an OSC title or a
           protocol reply) immediately before exit.  Do not retire the
           surface here: service_session_output must drain those bytes before
           it observes EOF and closes the PTY. */
        disable_input_routes_for_target(runtime, target_id);
        remove_session_streams(runtime, target_id);
    } else if (result < 0 && errno != EINTR) {
        session->exit_code = -1;
        snprintf(session->state, sizeof(session->state), "failed");
        session->child_exited = 1;
        if (session->pty_fd >= 0) {
            close(session->pty_fd);
            session->pty_fd = -1;
        }
        retire_session_surface(runtime, session);
        disable_input_routes_for_target(runtime, target_id);
        remove_session_streams(runtime, target_id);
    }
}

static void service_attach_reasserts(BlitRuntime *runtime, uint64_t now)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        BlitSurface *surface;
        struct winsize size;
        int signalled = 0;

        if (!session->used || !session->attach_reassert_pending ||
            now < session->attach_reassert_due_us)
            continue;
        session->attach_reassert_pending = 0;
        if (strcmp(session->state, "running") != 0 || session->pty_fd < 0)
            continue;
        surface = find_surface_id(runtime, session->id);
        if (surface == NULL)
            continue;
        memset(&size, 0, sizeof(size));
        size.ws_col = (unsigned short)surface->width;
        size.ws_row = (unsigned short)surface->height;
        size.ws_xpixel = (unsigned short)(surface->width *
                                          session->terminal_cell_width);
        size.ws_ypixel = (unsigned short)(surface->height *
                                           session->terminal_cell_height);
        if (ioctl(session->pty_fd, TIOCSWINSZ, &size) < 0) {
            fprintf(stderr, "blit: attach reassert resize for session %llu failed: %s\n",
                    session->id, strerror(errno));
            continue;
        }
        /* forkpty makes the child the PTY foreground process group.  The
           explicit signal is intentional: it makes the PTY resize/reassert
           contract deterministic for hosts whose kernel/PTY path does not
           deliver SIGWINCH from the master ioctl. */
        if (kill(-session->process_id, SIGWINCH) == 0)
            signalled = 1;
        else if (kill(session->process_id, SIGWINCH) == 0)
            signalled = 1;
        if (!signalled)
            fprintf(stderr, "blit: attach reassert signal for session %llu failed: %s\n",
                    session->id, strerror(errno));
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
    char escaped_clipboard[8192];
    char escaped_notification[2048];
    char escaped_terminal_term[128];

    refresh_session_state(runtime, session);

    for (route_index = 0; route_index < 16; ++route_index)
        if (runtime->input_routes[route_index].used &&
            runtime->input_routes[route_index].enabled &&
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

    json_escape(session->terminal_title, escaped_title,
                sizeof(escaped_title));
    json_escape(session->terminal_clipboard, escaped_clipboard,
                sizeof(escaped_clipboard));
    json_escape(session->terminal_notification, escaped_notification,
                sizeof(escaped_notification));
    json_escape(session->terminal_term, escaped_terminal_term,
                sizeof(escaped_terminal_term));
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
    if (session->terminal_clipboard[0] != '\0')
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"clipboard\":\"%s\"",
                                 escaped_clipboard);
    if (session->terminal_notification[0] != '\0')
        used += (size_t)snprintf(body + used, capacity - used,
                                 ",\"notification\":\"%s\"",
                                 escaped_notification);
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
    used += (size_t)snprintf(
        body + used, capacity - used,
        ",\"terminal_capabilities\":{\"term\":\"%s\","
        "\"cell_pixels\":{\"width\":%d,\"height\":%d},"
        "\"graphics\":[\"kitty\",\"sixel\"]}",
        escaped_terminal_term, session->terminal_cell_width,
        session->terminal_cell_height);
    used += (size_t)snprintf(body + used, capacity - used,
                             ",\"graphics\":[");
    {
        int graphics_index;
        int first_graphics = 1;
        for (graphics_index = 0; graphics_index < BLIT_GRAPHICS_MAX_IMAGES;
             ++graphics_index) {
            BlitGraphicsImage *image = &session->graphics[graphics_index];
            if (!image->used) continue;
            used += (size_t)snprintf(body + used, capacity - used,
                "%s{\"id\":%u,\"protocol\":\"%s\",\"complete\":%s,\"visible\":%s,"
                "\"width\":%d,\"height\":%d,\"format\":%d,"
                "\"bytes\":%lu,\"x\":%d,\"y\":%d,"
                "\"placement_width\":%d,\"placement_height\":%d,"
                "\"z\":%d,\"revision\":%llu}",
                first_graphics ? "" : ",", image->id, image->protocol,
                image->complete ? "true" : "false",
                image->visible ? "true" : "false", image->width,
                image->height, image->format, (unsigned long)image->bytes,
                image->x, image->y, image->placement_width,
                image->placement_height, image->z, image->revision);
            first_graphics = 0;
        }
    }
    used += (size_t)snprintf(body + used, capacity - used, "]");
    if (session->graphics_rejected > 0 || session->graphics_evicted > 0 ||
        session->graphics_last_error[0] != '\0') {
        char escaped_graphics_error[128];
        json_escape(session->graphics_last_error, escaped_graphics_error,
                    sizeof(escaped_graphics_error));
        used += (size_t)snprintf(
            body + used, capacity - used,
            ",\"graphics_diagnostics\":{\"rejected\":%lu,"
            "\"evicted\":%lu,\"last_error\":\"%s\"}",
            session->graphics_rejected, session->graphics_evicted,
            escaped_graphics_error);
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

static int command_allowed(const BlitRuntime *runtime, const char *command)
{
    const char *cursor = runtime->command_allowlist;

    /* An unset policy preserves local development and test behavior. */
    if (*cursor == '\0')
        return 1;
    while (*cursor != '\0') {
        const char *candidate = cursor;
        size_t length = 0;
        char value[256];

        while (*candidate == ' ' || *candidate == '\t')
            ++candidate;
        while (candidate[length] != '\0' && candidate[length] != ',')
            ++length;
        while (length > 0 &&
               (candidate[length - 1] == ' ' ||
                candidate[length - 1] == '\t'))
            --length;
        if (length > 0 && length < sizeof(value)) {
            memcpy(value, candidate, length);
            value[length] = '\0';
            if (strcmp(value, command) == 0)
                return 1;
        }
        cursor = candidate + length;
        while (*cursor != '\0' && *cursor != ',')
            ++cursor;
        if (*cursor == ',')
            ++cursor;
    }
    return 0;
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
    int width = 0;
    int height = 0;
    int surface_present;
    char surface_mode[32];
    int master_fd;
    pid_t process_id;
    unsigned long long session_id;
    int i;

    if (!json_string(json, "command", command, sizeof(command)))
        return 0;
    surface_present = strstr(json, "\"surface\"") != NULL;
    if (surface_present &&
        (!json_int(json, "width", &width) ||
         !json_int(json, "height", &height)))
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
    memset(surface_mode, 0, sizeof(surface_mode));
    if (json_string(json, "surface_mode", surface_mode, sizeof(surface_mode)) &&
        strcmp(surface_mode, "terminal") != 0 &&
        strcmp(surface_mode, "external") != 0 &&
        strcmp(surface_mode, "mirror") != 0)
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
    } else if (!surface_present) {
        int display_index = display_id[0] != '\0' ?
            display_index_for_id(runtime, display_id) : 0;
        int cell_width;
        int cell_height;
        if (display_index < 0 || display_index >= runtime->display_count)
            return 0;
        cell_width = runtime_terminal_cell_width(runtime);
        cell_height = runtime_terminal_cell_height(runtime);
        width = runtime->displays[display_index].bounds.w / cell_width;
        height = runtime->displays[display_index].bounds.h / cell_height;
    }
    if (width < 1 || height < 1 || width > BLIT_MAX_CELL_COLS ||
        height > BLIT_MAX_CELL_ROWS)
        return 0;
    if (!surface_present && pane == NULL && display_id[0] == '\0')
        return 0;
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)width;
    size.ws_row = (unsigned short)height;
    size.ws_xpixel = (unsigned short)(width * runtime_terminal_cell_width(runtime));
    size.ws_ypixel = (unsigned short)(height * runtime_terminal_cell_height(runtime));
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
        char hibr_log_path[256];
        snprintf(session_text, sizeof(session_text), "%llu", session_id);
        snprintf(surface_text, sizeof(surface_text), "surface-%llu", session_id);
        snprintf(port_text, sizeof(port_text), "%d", runtime->listen_port);
        /* Hibr writes its startup/restart diagnostics to DT_LOG.  An
           API-launched child has no login HOME, so Hibr's relative default
           can fail and stderr then becomes terminal content.  Supply a
           per-session default; an explicit API environment entry still
           overrides it below. */
        snprintf(hibr_log_path, sizeof(hibr_log_path),
                 "/tmp/blit-hibr-%llu.log", session_id);
        if (setenv("BLIT_SESSION_ID", session_text, 1) != 0 ||
            setenv("BLIT_SURFACE_ID", surface_text, 1) != 0 ||
            setenv("BLIT_SERVER_PORT", port_text, 1) != 0 ||
            setenv("TERM", "xterm-kitty", 1) != 0 ||
            setenv("COLORTERM", "truecolor", 1) != 0 ||
            /* Hibr's auto bidi probe treats xterm-kitty as a terminal
               that reorders RTL text. Blit preserves cell order, so Hibr
               must own bidi ordering while its console selects Kitty
               graphics. API environment entries below can override either
               integration default explicitly. */
            setenv("DT_BIDI", "on", 1) != 0 ||
            setenv("HIBR_GFX", "kitty", 1) != 0 ||
            setenv("DT_LOG", hibr_log_path, 0) != 0)
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
            session->terminal_cell_width = runtime_terminal_cell_width(runtime);
            session->terminal_cell_height = runtime_terminal_cell_height(runtime);
            snprintf(session->terminal_term, sizeof(session->terminal_term),
                     "xterm-kitty");
            {
                int environment_index;
                for (environment_index = 0;
                     environment_index < environment_count;
                     ++environment_index) {
                    const char *key = environment_keys[environment_index];
                    const char *value = environment_values[environment_index];
                    if (strcmp(key, "TERM") == 0)
                        snprintf(session->terminal_term,
                                 sizeof(session->terminal_term), "%s", value);
                    else if (strcmp(key, "DT_INK") == 0 ||
                        strcmp(key, "BLIT_TERMINAL_FG") == 0)
                        snprintf(session->terminal_default_foreground,
                                 sizeof(session->terminal_default_foreground),
                                 "%s", value);
                    else if (strcmp(key, "DT_FACE") == 0 ||
                             strcmp(key, "BLIT_TERMINAL_BG") == 0)
                        snprintf(session->terminal_default_background,
                                 sizeof(session->terminal_default_background),
                                 "%s", value);
                }
            }
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
                BlitSurface *surface = (BlitSurface *)calloc(1, sizeof(*surface));
                if (surface == NULL) {
                    terminate_session_process(process_id);
                    close(master_fd);
                    memset(session, 0, sizeof(*session));
                    return 0;
                }
                runtime->surfaces[i] = surface;
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
    int x;
    int y;

    if (width < 1 || height < 1 || width > BLIT_MAX_CELL_COLS ||
        height > BLIT_MAX_CELL_ROWS)
        return 0;
    terminal_scrollback_show_live(surface);
    memset(&size, 0, sizeof(size));
    size.ws_col = (unsigned short)width;
    size.ws_row = (unsigned short)height;
    size.ws_xpixel = (unsigned short)(width * session->terminal_cell_width);
    size.ws_ypixel = (unsigned short)(height * session->terminal_cell_height);
    if (session->pty_fd >= 0 && ioctl(session->pty_fd, TIOCSWINSZ, &size) < 0)
        return 0;

    /* Resize is a grid operation, not a terminal reset.  Preserve the
       overlapping cells on both screens, and blank only cells which are no
       longer visible.  Hibr relies on this while resizing an attached
       desktop: clearing the whole grid here causes a blank frame and leaves
       stale combining/underline data to reappear after a later redraw. */
    for (y = 0; y < BLIT_MAX_CELL_ROWS; ++y) {
        for (x = 0; x < BLIT_MAX_CELL_COLS; ++x) {
            if (x < width && y < height)
                continue;
            surface->cells[y][x] = 0;
            surface->codepoints[y][x] = 0;
            memset(surface->combining[y][x], 0,
                   sizeof(surface->combining[y][x]));
            surface->combining_count[y][x] = 0;
            surface->foreground[y][x] = 0;
            surface->background[y][x] = 0;
            surface->underline_color[y][x] = 0;
            surface->attributes[y][x] = 0;
            surface->alternate_cells[y][x] = 0;
            surface->alternate_codepoints[y][x] = 0;
            memset(surface->alternate_combining[y][x], 0,
                   sizeof(surface->alternate_combining[y][x]));
            surface->alternate_combining_count[y][x] = 0;
            surface->alternate_foreground[y][x] = 0;
            surface->alternate_background[y][x] = 0;
            surface->alternate_underline_color[y][x] = 0;
            surface->alternate_attributes[y][x] = 0;
            surface->alternate_links[y][x] = 0;
        }
    }
    surface->width = width;
    surface->height = height;
    if (surface->cursor_x >= width)
        surface->cursor_x = width - 1;
    if (surface->cursor_y >= height)
        surface->cursor_y = height - 1;
    surface->scroll_top = 0;
    surface->scroll_bottom = height - 1;
    surface->scrollback_view = 0;
    terminal_reset_tabs(surface);
    session->terminal_wrap_pending = 0;
    surface->damage_valid = 1;
    surface->damage_x = 0;
    surface->damage_y = 0;
    surface->damage_width = width;
    surface->damage_height = height;
    ++surface->revision;
    return 1;
}

/* Stop a PTY process group without allowing a misbehaving child to wedge the
   gateway.  Hibr may keep descendants alive after SIGTERM, so a blocking
   waitpid here would make DELETE, SIGINT, and SIGTERM unable to return. */
static int terminate_session_process(pid_t process_id)
{
    int status;
    pid_t result;
    uint64_t deadline;

    if (process_id <= 0)
        return 1;
    if (kill(-process_id, SIGTERM) < 0)
        kill(process_id, SIGTERM);
    deadline = monotonic_us() + 100000;
    for (;;) {
        result = waitpid(process_id, &status, WNOHANG);
        if (result == process_id)
            return 1;
        if (result < 0 && (errno == ECHILD || errno == ESRCH))
            return 1;
        if (monotonic_us() >= deadline)
            break;
        usleep(10000);
    }

    if (kill(-process_id, SIGKILL) < 0)
        kill(process_id, SIGKILL);
    deadline = monotonic_us() + 500000;
    for (;;) {
        result = waitpid(process_id, &status, WNOHANG);
        if (result == process_id)
            return 1;
        if (result < 0 && (errno == ECHILD || errno == ESRCH))
            return 1;
        if (monotonic_us() >= deadline)
            break;
        usleep(10000);
    }
    fprintf(stderr, "blit: warning: process %ld did not reap after SIGKILL\n",
            (long)process_id);
    return 0;
}

static void stop_sessions(BlitRuntime *runtime)
{
    int i;

    for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
        BlitSession *session = &runtime->sessions[i];
        if (!session->used)
            continue;
        refresh_session_state(runtime, session);
        if (strcmp(session->state, "running") == 0 ||
            strcmp(session->state, "starting") == 0) {
            terminate_session_process(session->process_id);
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

    snprintf(target_id, sizeof(target_id), "session-%llu", session->id);

    refresh_session_state(runtime, session);
    if (strcmp(session->state, "running") == 0 ||
        strcmp(session->state, "starting") == 0) {
        terminate_session_process(session->process_id);
    }
    if (session->pty_fd >= 0)
        close(session->pty_fd);
    disable_input_routes_for_target(runtime, target_id);
    remove_session_streams(runtime, target_id);
    if (surface != NULL) {
        terminal_scrollback_release(surface);
        memset(surface, 0, sizeof(*surface));
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime->surfaces[i] == surface) {
                free(surface);
                runtime->surfaces[i] = NULL;
                break;
            }
        }
    }
    for (i = 0; i < BLIT_GRAPHICS_MAX_IMAGES; ++i)
        graphics_image_release(&session->graphics[i]);
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

static unsigned char *stream_receive_data(BlitStream *stream)
{
    return stream->large_receive_buffer != NULL ? stream->large_receive_buffer :
        stream->receive_buffer;
}

static size_t stream_receive_capacity(const BlitStream *stream)
{
    return stream->large_receive_buffer != NULL ? stream->large_receive_capacity :
        sizeof(stream->receive_buffer);
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
            pixel_frame_reset(runtime, &runtime->streams[i], 1);
            free(runtime->streams[i].large_receive_buffer);
            runtime->streams[i].large_receive_buffer = NULL;
            runtime->streams[i].large_receive_capacity = 0;
            close(fd);
            runtime->streams[i].used = 0;
            return;
        }
    }
}

static void remove_session_streams(BlitRuntime *runtime,
                                   const char *session_id)
{
    int i;
    if (session_id == NULL || session_id[0] == '\0')
        return;
    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used &&
            strcmp(runtime->streams[i].owner_session_id, session_id) == 0)
            remove_stream(runtime, runtime->streams[i].fd);
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
            !send_websocket_text(runtime->streams[i].fd, sequenced)) {
            ++runtime->diagnostic_stream_dropped_events;
            ++runtime->diagnostic_stream_backpressure_disconnects;
            remove_stream(runtime, runtime->streams[i].fd);
        }
    }
}

static void broadcast_input_diagnostic(BlitRuntime *runtime,
                                       const char *severity,
                                       const char *code,
                                       const char *device_id,
                                       const char *path,
                                       int active_devices)
{
    char escaped_path[256];
    char event[768];

    json_escape(path, escaped_path, sizeof(escaped_path));
    snprintf(event, sizeof(event),
             "{\"type\":\"diagnostic\",\"severity\":\"%s\","
             "\"code\":\"%s\",\"detail\":\"evdev device state changed\","
             "\"resource_id\":\"%s\",\"path\":\"%s\","
             "\"active_devices\":%d}", severity, code, device_id,
             escaped_path, active_devices);
    broadcast_stream(runtime, event, BLIT_STREAM_DIAGNOSTICS);
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
    /* Hibr emits one logical desktop flush with one write, but a small read
       buffer would fragment that write into many renderable intermediate
       states. Keep the PTY stream canonical while retaining the bounded
       parser contract for ordinary output. */
    unsigned char output[65536];
    char escaped[1536];
    char event[1800];
    ssize_t received;
    int i;
    uint64_t service_started = monotonic_us();

    received = read(fd, output, sizeof(output) - 1);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
        return;
    if (received <= 0) {
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime->sessions[i].used && runtime->sessions[i].pty_fd == fd) {
                close(fd);
                runtime->sessions[i].pty_fd = -1;
                /* A stopped terminal keeps its final parsed frame available
                   for API snapshots and diagnostics.  The caller releases
                   that surface explicitly with DELETE; clearing it here
                   would race the final PTY read and turn a valid last frame
                   into a blank one. */
                return;
            }
        }
        return;
    }
    ++runtime->diagnostic_terminal_reads;
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
                if (runtime->sessions[i].surface_mode == BLIT_SURFACE_EXTERNAL) {
                    runtime->sessions[i].attach_reassert_pending = 1;
                    runtime->sessions[i].attach_reassert_due_us =
                        monotonic_us() + 250000ULL;
                }
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
                if (runtime->sessions[i].terminal_frame_batch)
                    runtime->terminal_frame_quiet_until_us = monotonic_us() +
                        BLIT_TERMINAL_FRAME_QUIET_US;
            }
            runtime->diagnostic_terminal_bytes += (unsigned long long)received;
            runtime->diagnostic_last_terminal_ms =
                (double)(monotonic_us() - service_started) / 1000.0;
            if (runtime->diagnostic_last_terminal_ms >
                runtime->diagnostic_max_terminal_ms)
                runtime->diagnostic_max_terminal_ms =
                    runtime->diagnostic_last_terminal_ms;
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

static void pixel_frame_reset(BlitRuntime *runtime, BlitStream *stream,
                              int release_image)
{
    BlitSession *session;
    BlitGraphicsImage *image;
    if (stream == NULL) return;
    if (release_image && stream->pixel_frame_active) {
        session = find_session_id(runtime, stream->pixel_frame_session_id);
        image = session != NULL ? graphics_image_find(
            session, stream->pixel_frame_image_id) : NULL;
        if (image != NULL && strcmp(image->protocol, "api") == 0 &&
            !image->complete)
            graphics_image_release(image);
    }
    stream->pixel_frame_session_id = 0;
    stream->pixel_frame_image_id = 0;
    stream->pixel_frame_expected = 0;
    stream->pixel_frame_received = 0;
    stream->pixel_frame_active = 0;
    stream->pixel_frame_ack = 0;
    stream->pixel_frame_chunked = 0;
}

static int pixel_frame_begin(BlitRuntime *runtime, BlitStream *stream,
                             const char *message)
{
    char session_id[64];
    unsigned long long session_number;
    int image_id;
    int width;
    int height;
    int format;
    int x = 0;
    int y = 0;
    int z = 0;
    int acknowledge = 0;
    int chunked = 0;
    size_t bytes;
    BlitSession *session;
    BlitSurface *surface;
    BlitGraphicsImage *image;
    int index;
    int old_x = 0;
    int old_y = 0;
    int old_width = 0;
    int old_height = 0;
    int old_visible = 0;

    if (!json_string(message, "session_id", session_id, sizeof(session_id)) ||
        (sscanf(session_id, "session-%llu", &session_number) != 1 &&
         sscanf(session_id, "%llu", &session_number) != 1) ||
        !json_int(message, "image_id", &image_id) || image_id < 1 ||
        !json_int(message, "width", &width) ||
        !json_int(message, "height", &height) ||
        !json_int(message, "format", &format) ||
        (format != 24 && format != 32) || width < 1 ||
        width > BLIT_GRAPHICS_MAX_WIDTH || height < 1 ||
        height > BLIT_GRAPHICS_MAX_HEIGHT)
        return 0;
    bytes = (size_t)width * (size_t)height * (size_t)(format == 24 ? 3 : 4);
    if (bytes > BLIT_GRAPHICS_MAX_BYTES)
        return 0;
    session = find_session_id(runtime, session_number);
    surface = session != NULL ? find_surface_id(runtime, session->id) : NULL;
    if (session == NULL || surface == NULL)
        return 0;
    if (json_int(message, "x", &x) == 0) x = 0;
    if (json_int(message, "y", &y) == 0) y = 0;
    if (json_int(message, "z", &z) == 0) z = 0;
    if (json_bool(message, "ack", &acknowledge) == 0)
        acknowledge = 0;
    if (json_bool(message, "chunked", &chunked) == 0)
        chunked = 0;
    /* A new begin record changes the stream's frame format.  Keep the old
       API image allocation available so same-sized frames can be reused. */
    pixel_frame_reset(runtime, stream, 0);
    image = graphics_image_find(session, (unsigned int)image_id);
    if (image != NULL && strcmp(image->protocol, "api") != 0)
        return 0;
    if (image != NULL) {
        old_x = image->x;
        old_y = image->y;
        old_width = image->placement_width;
        old_height = image->placement_height;
        old_visible = image->visible;
        if (old_visible)
            mark_graphics_damage(surface, old_x, old_y, old_width, old_height,
                                 session->terminal_cell_width > 0 ?
                                     session->terminal_cell_width : 8,
                                 session->terminal_cell_height > 0 ?
                                     session->terminal_cell_height : 16);
    }
    if (image == NULL) {
        for (index = 0; index < BLIT_GRAPHICS_MAX_IMAGES; ++index)
            if (!session->graphics[index].used) {
                image = &session->graphics[index];
                break;
            }
        if (image == NULL)
            return 0;
    }
    if (image->used && image->pixels != NULL &&
        image->width == width && image->height == height &&
        image->format == format && image->capacity == bytes) {
        image->bytes = 0;
        image->complete = 0;
        image->visible = 0;
    } else {
        graphics_image_release(image);
        image->used = 1;
        image->id = (unsigned int)image_id;
        snprintf(image->protocol, sizeof(image->protocol), "api");
        image->width = width;
        image->height = height;
        image->format = format;
        image->capacity = bytes;
        image->pixels = (unsigned char *)malloc(bytes);
        if (image->pixels == NULL) {
            graphics_image_release(image);
            return 0;
        }
    }
    image->x = x;
    image->y = y;
    image->z = z;
    image->opaque = 1;
    image->placement_width = width;
    image->placement_height = height;
    image->revision = 1;
    image->generation = next_graphics_generation();
    stream->pixel_frame_session_id = session_number;
    stream->pixel_frame_image_id = (unsigned int)image_id;
    stream->pixel_frame_expected = bytes;
    stream->pixel_frame_received = 0;
    stream->pixel_frame_active = 1;
    stream->pixel_frame_ack = acknowledge;
    stream->pixel_frame_chunked = chunked;
    send_websocket_text(stream->fd,
        "{\"type\":\"pixel_frame_ready\",\"state\":\"ready\"}");
    return 1;
}

static int service_pixel_frame_binary(BlitRuntime *runtime,
                                      BlitStream *stream, int final,
                                      const unsigned char *payload,
                                      size_t length)
{
    BlitSession *session;
    BlitSurface *surface;
    BlitGraphicsImage *image;
    if (stream == NULL || !stream->pixel_frame_active)
        return 0;
    if (length > stream->pixel_frame_expected -
                 stream->pixel_frame_received) {
        ++runtime->pixel_frame_errors;
        pixel_frame_reset(runtime, stream, 1);
        send_websocket_text(stream->fd,
            "{\"type\":\"pixel_frame_error\",\"code\":\"frame_size\"}");
        return 0;
    }
    session = find_session_id(runtime, stream->pixel_frame_session_id);
    image = session != NULL ? graphics_image_find(
        session, stream->pixel_frame_image_id) : NULL;
    if (session == NULL || image == NULL ||
        strcmp(image->protocol, "api") != 0) {
        pixel_frame_reset(runtime, stream, 0);
        return 0;
    }
    if (stream->pixel_frame_received == 0) {
        image->complete = 0;
        image->visible = 0;
    }
    memcpy(image->pixels + stream->pixel_frame_received, payload, length);
    if (image->format == 32 && image->opaque) {
        size_t alpha_offset = stream->pixel_frame_received;
        size_t alpha_index;
        for (alpha_index = 0; alpha_index < length; ++alpha_index)
            if (((alpha_offset + alpha_index) & 3U) == 3U &&
                payload[alpha_index] != 255U) {
                image->opaque = 0;
                break;
            }
    }
    stream->pixel_frame_received += length;
    if (!final)
        return 1;
    if (stream->pixel_frame_received != stream->pixel_frame_expected) {
        ++runtime->pixel_frame_errors;
        pixel_frame_reset(runtime, stream, 1);
        send_websocket_text(stream->fd,
            "{\"type\":\"pixel_frame_error\",\"code\":\"frame_incomplete\"}");
        return 0;
    }
    image->bytes = stream->pixel_frame_received;
    image->complete = 1;
    image->visible = 1;
    ++image->revision;
    image->generation = next_graphics_generation();
    surface = find_surface_id(runtime, session->id);
    if (surface != NULL)
        mark_graphics_damage(surface, image->x, image->y,
                             image->placement_width, image->placement_height,
                             session->terminal_cell_width > 0 ?
                                 session->terminal_cell_width : 8,
                             session->terminal_cell_height > 0 ?
                                 session->terminal_cell_height : 16);
    if (surface != NULL)
        ++surface->revision;
    runtime->render_pending = 1;
    if (!runtime->direct_kms && surface != NULL) {
        /* SDL's scene cache needs a persistent pending count so an external
           pixel producer cannot be lost between vblanks. Direct KMS already
           consumes each accepted frame through its page-flip path; replaying
           the latest 60 Hz image at a 240 Hz mode would create artificial
           deadline misses in the scheduler. */
        int output_index;
        for (output_index = 0; output_index < runtime->output_count;
             ++output_index)
            if (surface_targets_output(runtime, surface, output_index)) {
                ++runtime->pixel_frames_pending[output_index];
                break;
            }
    }
    ++runtime->pixel_frames_accepted;
    runtime->pixel_frames_last_accepted_us = monotonic_us();
    if (stream->pixel_frame_ack)
        send_websocket_text(stream->fd,
            "{\"type\":\"pixel_frame_present\",\"state\":\"accepted\"}");
    /* Keep the stream armed for the next binary frame.  The begin record is
       the negotiated format; each final binary message is one frame. */
    stream->pixel_frame_received = 0;
    return 1;
}

/* One bounded PTY write boundary for WebSocket and HTTP input adapters. */
static int write_terminal_input(BlitSession *session, const char *data,
                                const char *mode, size_t *requested,
                                size_t *written, const char **effective_mode)
{
    char framed[1024];
    size_t data_length;
    size_t total;
    size_t offset = 0;
    ssize_t count;
    int bracketed;

    if (session == NULL || session->pty_fd < 0 || data == NULL ||
        mode == NULL || requested == NULL || written == NULL ||
        effective_mode == NULL)
        return -1;
    data_length = strlen(data);
    if (data_length == 0 || data_length > 768 ||
        (strcmp(mode, "raw") != 0 && strcmp(mode, "paste") != 0))
        return -1;
    bracketed = strcmp(mode, "paste") == 0 && session->terminal_bracketed_paste;
    if (bracketed) {
        memcpy(framed, "\033[200~", 6);
        memcpy(framed + 6, data, data_length);
        memcpy(framed + 6 + data_length, "\033[201~", 6);
        total = data_length + 12;
        *effective_mode = "paste";
    } else {
        memcpy(framed, data, data_length);
        total = data_length;
        *effective_mode = strcmp(mode, "paste") == 0 ?
            "paste-fallback" : "raw";
    }
    *requested = data_length;
    *written = 0;
    while (offset < total) {
        count = write(session->pty_fd, framed + offset, total - offset);
        if (count > 0) {
            offset += (size_t)count;
            continue;
        }
        if (count < 0 && errno == EINTR)
            continue;
        if (count < 0 && (errno == EAGAIN || errno == EWOULDBLOCK))
            break;
        return -2;
    }
    *written = offset;
    return offset == total ? 0 : -2;
}

static void service_stream_text(BlitRuntime *runtime, int fd,
                                const unsigned char *payload, size_t length)
{
    char message[1025];
    if (length >= sizeof(message))
        return;
    memcpy(message, payload, length);
    message[length] = '\0';
    if (strstr(message, "subscribe") != NULL) {
        int i;
        for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
            if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
                runtime->streams[i].subscriptions = subscription_mask(message);
                json_string(message, "owner_session_id",
                            runtime->streams[i].owner_session_id,
                            sizeof(runtime->streams[i].owner_session_id));
                break;
            }
        }
        send_websocket_text(fd,
            "{\"type\":\"diagnostic\",\"severity\":\"info\","
            "\"code\":\"subscription_received\","
            "\"detail\":\"stream subscription accepted\"}");
    } else if (strstr(message, "\"type\":\"pixel_frame_begin\"") != NULL) {
        int i;
        for (i = 0; i < BLIT_MAX_STREAMS; ++i)
            if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
                if (!pixel_frame_begin(runtime, &runtime->streams[i], message)) {
                    ++runtime->pixel_frame_errors;
                    send_websocket_text(fd,
                        "{\"type\":\"pixel_frame_error\",\"code\":\"invalid_begin\"}");
                }
                break;
            }
    } else if (strstr(message, "\"type\":\"pixel_frame_hide\"") != NULL) {
        char session_id[64];
        int image_id;
        unsigned long long numeric_id;
        BlitSession *session;
        BlitGraphicsImage *image;
        if (json_string(message, "session_id", session_id, sizeof(session_id)) &&
            json_int(message, "image_id", &image_id) && image_id > 0 &&
            sscanf(session_id, "session-%llu", &numeric_id) == 1 &&
            (session = find_session_id(runtime, numeric_id)) != NULL &&
            (image = graphics_image_find(session, (unsigned int)image_id)) != NULL) {
            image->visible = 0;
            {
                BlitSurface *surface = find_surface_id(runtime, session->id);
                if (surface != NULL) mark_surface_full_damage(surface);
            }
        }
    } else if (strstr(message, "\"type\":\"pixel_frame_end\"") != NULL) {
        int i;
        for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
            if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
                if (!runtime->streams[i].pixel_frame_active ||
                    !runtime->streams[i].pixel_frame_chunked ||
                    !service_pixel_frame_binary(runtime, &runtime->streams[i],
                                                1, NULL, 0)) {
                    ++runtime->pixel_frame_errors;
                    send_websocket_text(fd,
                        "{\"type\":\"pixel_frame_error\",\"code\":\"invalid_end\"}");
                }
                break;
            }
        }
    } else if (strstr(message, "\"input\"") != NULL &&
               strstr(message, "session_id") != NULL) {
        char session_id[64];
        char data[768];
        unsigned long long numeric_id;
        BlitSession *session;
        size_t requested;
        size_t written;
        const char *effective_mode;

        if (json_string(message, "session_id", session_id,
                        sizeof(session_id)) &&
            json_string(message, "data", data, sizeof(data)) &&
            sscanf(session_id, "%llu", &numeric_id) == 1) {
            session = find_session_id(runtime, numeric_id);
            if (session != NULL)
                terminal_scrollback_show_live(find_surface_id(runtime,
                                                               session->id));
            if (write_terminal_input(session, data, "raw", &requested,
                                     &written, &effective_mode) == 0) {
                send_websocket_text(fd,
                    "{\"type\":\"diagnostic\",\"severity\":\"info\","
                    "\"code\":\"input_forwarded\","
                    "\"detail\":\"input forwarded to terminal session\"}");
            }
        }
    }
}

static int service_stream_frame(BlitRuntime *runtime, BlitStream *stream,
                                unsigned char opcode, int final,
                                const unsigned char *payload, size_t length)
{
    if (opcode == 0x8) {
        send_websocket_control(stream->fd, 0x8, NULL, 0);
        return 0;
    }
    if (opcode == 0x9) {
        send_websocket_control(stream->fd, 0xA, payload, length);
        return 1;
    }
    if (opcode == 0xA)
        return 1;
    if ((opcode == 0x1 || opcode == 0x2) && !final) {
        if (length > sizeof(stream->fragment_buffer))
            return 1;
        memcpy(stream->fragment_buffer, payload, length);
        stream->fragment_length = length;
        stream->fragment_opcode = opcode;
        return 1;
    }
    if (opcode == 0x0) {
        if (stream->fragment_opcode == 0 ||
            length > sizeof(stream->fragment_buffer) - stream->fragment_length)
            return 1;
        memcpy(stream->fragment_buffer + stream->fragment_length,
               payload, length);
        stream->fragment_length += length;
        if (!final)
            return 1;
        opcode = stream->fragment_opcode;
        payload = stream->fragment_buffer;
        length = stream->fragment_length;
        stream->fragment_opcode = 0;
        stream->fragment_length = 0;
    }
    if (opcode == 0x1 && final)
        service_stream_text(runtime, stream->fd, payload, length);
    else if (opcode == 0x2)
        service_pixel_frame_binary(runtime, stream,
            stream->pixel_frame_chunked ? 0 : final, payload, length);
    return 1;
}

static void service_stream(BlitRuntime *runtime, int fd)
{
    BlitStream *stream = NULL;
    unsigned char incoming[1024];
    ssize_t received;
    size_t offset = 0;
    int i;
    int frames_processed = 0;

    for (i = 0; i < BLIT_MAX_STREAMS; ++i)
        if (runtime->streams[i].used && runtime->streams[i].fd == fd) {
            stream = &runtime->streams[i];
            break;
        }
    if (stream == NULL)
        return;
    for (;;) {
        /* Pixel frames are larger than the small receive scratch buffer.
           MSG_DONTWAIT lets this readiness turn drain all available chunks
           without changing the client's blocking semantics. */
        received = recv(fd, incoming, sizeof(incoming), MSG_DONTWAIT);
        if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK)) {
            /* The previous bounded turn may have left a complete message in
               the user-space buffer after the kernel socket was drained. It
               must still reach the parser; only return when no buffered work
               remains. */
            if (stream->receive_length == 0)
                return;
            received = 0;
        }
        if (received < 0 && errno == EINTR)
            continue;
        if (received <= 0 && stream->receive_length == 0) {
            remove_stream(runtime, fd);
            return;
        }
        if (received > 0) {
            if ((size_t)received > stream_receive_capacity(stream) -
                stream->receive_length) {
                size_t required = stream->receive_length + (size_t)received;
                unsigned char *expanded;
                int had_large = stream->large_receive_buffer != NULL;
                if (required > (size_t)BLIT_GRAPHICS_MAX_BYTES * 2U + 256U) {
                    remove_stream(runtime, fd);
                    return;
                }
                expanded = (unsigned char *)realloc(
                    stream->large_receive_buffer, required);
                if (expanded == NULL) {
                    remove_stream(runtime, fd);
                    return;
                }
                if (!had_large)
                    memcpy(expanded, stream->receive_buffer,
                           stream->receive_length);
                stream->large_receive_buffer = expanded;
                stream->large_receive_capacity = required;
            }
            memcpy(stream_receive_data(stream) + stream->receive_length,
                   incoming, (size_t)received);
            stream->receive_length += (size_t)received;
            if (stream->receive_length >
                runtime->diagnostic_stream_queue_peak_bytes)
                runtime->diagnostic_stream_queue_peak_bytes =
                    stream->receive_length;
        }
    while (stream->receive_length - offset >= 2) {
        unsigned char *frame = stream_receive_data(stream) + offset;
        unsigned char flags = frame[0];
        unsigned char length_code = frame[1] & 0x7f;
        unsigned char mask[4];
        size_t header = 2;
        size_t length;
        size_t total;
        size_t j;
        int masked = (frame[1] & 0x80) != 0;

        if (length_code == 127) {
            remove_stream(runtime, fd);
            return;
        }
        if (length_code == 126) {
            header += 2;
            if (stream->receive_length - offset < header)
                break;
            length = ((size_t)frame[2] << 8) | frame[3];
        } else {
            length = length_code;
        }
        if (masked) header += 4;
        if (length > stream_receive_capacity(stream) - header) {
            size_t required = header + length;
            unsigned char *expanded;
            int had_large = stream->large_receive_buffer != NULL;
            if (required > BLIT_GRAPHICS_MAX_BYTES + 128U) {
                remove_stream(runtime, fd);
                return;
            }
            expanded = (unsigned char *)realloc(
                stream->large_receive_buffer, required);
            if (expanded == NULL) {
                remove_stream(runtime, fd);
                return;
            }
            if (!had_large)
                memcpy(expanded, stream->receive_buffer,
                       stream->receive_length);
            stream->large_receive_buffer = expanded;
            stream->large_receive_capacity = required;
            frame = stream_receive_data(stream) + offset;
        }
        total = header + length;
        if (stream->receive_length - offset < total)
            break;
        if (masked) {
            memcpy(mask, frame + header - 4, sizeof(mask));
            for (j = 0; j < length; ++j)
                frame[header + j] ^= mask[j % 4];
        }
        if (!service_stream_frame(runtime, stream, flags & 0x0f,
                                  (flags & 0x80) != 0,
                                  frame + header, length))
            return;
        offset += total;
        /* Do not let a continuously producing pixel client drain the entire
           socket before the main loop can present the newest complete image.
           A small bounded batch preserves input/PTY fairness and gives the
           refresh-aware renderer a chance to run. Fragmented messages are
           still retained in stream state and resume on the next turn. */
        ++frames_processed;
        if (frames_processed >= 1)
            break;
    }
    if (offset > 0) {
        memmove(stream_receive_data(stream), stream_receive_data(stream) + offset,
                stream->receive_length - offset);
        stream->receive_length -= offset;
        offset = 0;
    }
    if (frames_processed > 0)
        return;
    }
}

static void handle_request(int fd, BlitRuntime *runtime,
                           BlitHttpClient *client, const char *request)
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

    if (strcmp(path, "/v1/control/shutdown") == 0 &&
        strcmp(method, "POST") == 0) {
        blit_running = 0;
        send_http(fd, 202, "Accepted", "application/json",
                  "{\"state\":\"stopping\",\"detail\":\""
                  "shutdown accepted; cleanup is in progress\"}");
        return;
    }
    if (strcmp(path, "/v1/control/restart") == 0 &&
        strcmp(method, "POST") == 0) {
        blit_restart_requested = 1;
        blit_running = 0;
        send_http(fd, 202, "Accepted", "application/json",
                  "{\"state\":\"restarting\",\"detail\":\""
                  "restart accepted; cleanup is in progress\"}");
        return;
    }

    if (strcmp(path, "/v1/control/update/cancel") == 0 &&
        strcmp(method, "POST") == 0) {
        cancel_update_upload(runtime, fd, request);
        return;
    }
    if (strcmp(path, "/v1/control/update/reset") == 0 &&
        strcmp(method, "POST") == 0) {
        reset_update_state(runtime, fd, request);
        return;
    }

    if (strncmp(path, "/v1/diagnostics/screenshots/",
                strlen("/v1/diagnostics/screenshots/")) == 0 &&
        strcmp(method, "GET") == 0) {
        int output_index = screenshot_output_index(runtime, path);
        if (output_index < 0 ||
            (!runtime->direct_kms && runtime->outputs[output_index].renderer == NULL)) {
            send_problem(fd, 404, "Not Found", "display_not_found",
                         "The requested display does not exist or has no renderer.");
            return;
        }
        client->screenshot_output_index = output_index + 1;
        runtime->screenshot_requested_mask |= 1U << output_index;
        runtime->outputs[output_index].initialized = 0;
        runtime->render_next_us[output_index] = 0;
        runtime->render_pending = 1;
        return;
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
                 "X-Blit-Version: %s\r\n"
                 "Sec-WebSocket-Accept: %s\r\n\r\n", blit_version, accept);
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
        char requested_command[256];
        char requested_display_id[128];
        char requested_pane_id[128];
        if (idempotency_replay(runtime, fd, request, method, path, payload))
            return;
        if (!json_string(payload, "command", requested_command,
                         sizeof(requested_command))) {
            send_problem(fd, 400, "Bad Request", "invalid_command",
                         "The command must be a non-empty executable path.");
            return;
        }
        if (!command_allowed(runtime, requested_command)) {
            send_problem(fd, 403, "Forbidden", "command_not_allowed",
                         "The requested executable is not in BLIT_COMMAND_ALLOWLIST.");
            return;
        }
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
        strstr(path, "/scrollback") != NULL &&
        (strcmp(method, "GET") == 0 || strcmp(method, "PATCH") == 0)) {
        BlitSurface *surface = find_scrollback_surface(runtime, path);
        int view;

        if (surface == NULL) {
            send_problem(fd, 404, "Not Found", "surface_not_found",
                         "The text surface does not exist.");
            return;
        }
        if (strcmp(method, "PATCH") == 0) {
            if (!json_int(payload, "view", &view) || view < 0 ||
                view > surface->scrollback_count) {
                send_problem(fd, 400, "Bad Request", "invalid_scrollback_view",
                             "view must be between zero and the retained line count.");
                return;
            }
            terminal_scrollback_show_offset(surface, view);
            ++surface->revision;
        }
        surface_scrollback_json(surface, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/surfaces/", strlen("/v1/surfaces/")) == 0 &&
        (strcmp(method, "PUT") == 0 || strcmp(method, "GET") == 0)) {
        BlitSurface *surface = find_surface(runtime, path);
        int base_revision;
        char presentation[16] = "immediate";
        int presentation_deferred;
        int presentation_commit;

        if (surface == NULL) {
            send_problem(fd, 404, "Not Found", "surface_not_found",
                         "The text surface does not exist.");
            return;
        }
        if (strcmp(method, "GET") == 0) {
            size_t snapshot_capacity = (size_t)surface->width *
                (size_t)surface->height * 512U + 4096U;
            char *snapshot = (char *)malloc(snapshot_capacity);
            int snapshot_ok = snapshot != NULL &&
                surface_snapshot_json(surface,
                                      find_session_id(runtime,
                                                      surface->owner_session_id),
                                      snapshot, snapshot_capacity);
            if (!snapshot_ok) {
                free(snapshot);
                send_problem(fd, 413, "Payload Too Large",
                             "surface_snapshot_too_large",
                             "The surface snapshot exceeds the bounded API response size.");
                return;
            }
            send_http(fd, 200, "OK", "application/json", snapshot);
            free(snapshot);
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
        if (json_string(payload, "presentation", presentation,
                        sizeof(presentation)) &&
            strcmp(presentation, "immediate") != 0 &&
            strcmp(presentation, "defer") != 0 &&
            strcmp(presentation, "commit") != 0) {
            send_problem(fd, 400, "Bad Request", "invalid_presentation_mode",
                         "presentation must be immediate, defer, or commit.");
            return;
        }
        presentation_deferred = strcmp(presentation, "defer") == 0;
        presentation_commit = strcmp(presentation, "commit") == 0;
        if (presentation_commit && !surface->frame_open) {
            send_problem(fd, 409, "Conflict", "frame_not_open",
                         "A commit requires a preceding deferred cell update.");
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
        if (presentation_deferred)
            surface->frame_open = 1;
        else if (presentation_commit)
            surface->frame_open = 0;
        {
            char detail[192];
            snprintf(detail, sizeof(detail),
                     "surface=surface-%llu revision=%llu presentation=%s frame_open=%d",
                     surface->id, surface->revision, presentation,
                     surface->frame_open);
            /* Deferred cell chunks are transport detail.  Keep the bounded
               trace focused on actual presentation boundaries so a busy
               external terminal cannot evict input and render evidence. */
            if (!presentation_deferred)
                diagnostic_trace(runtime, "surface_update", detail);
        }
        if (!presentation_deferred)
            runtime->render_pending = 1;
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
        render_outputs(runtime, &plan, all_output_mask(runtime));
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
        strcmp(path + strlen(path) - 6, "/input") == 0 &&
        strcmp(method, "POST") == 0) {
        const char *suffix = path + strlen(path) - 6;
        char session_path[128];
        char data[768];
        char mode[32] = "raw";
        const char *effective_mode;
        size_t requested;
        size_t written;
        size_t session_path_length = (size_t)(suffix - path);
        BlitSession *session;
        int result;

        if (session_path_length == 0 || session_path_length >=
            sizeof(session_path)) {
            send_problem(fd, 404, "Not Found", "session_not_found",
                         "The terminal session does not exist.");
            return;
        }
        memcpy(session_path, path, session_path_length);
        session_path[session_path_length] = '\0';
        session = find_session(runtime, session_path);
        if (session == NULL) {
            send_problem(fd, 404, "Not Found", "session_not_found",
                         "The terminal session does not exist.");
            return;
        }
        if (!json_string(payload, "data", data, sizeof(data))) {
            send_problem(fd, 400, "Bad Request", "invalid_terminal_input",
                         "data must be a non-empty UTF-8 string of at most 768 bytes.");
            return;
        }
        if (json_string(payload, "mode", mode, sizeof(mode)) == 0)
            snprintf(mode, sizeof(mode), "raw");
        if (strcmp(mode, "raw") != 0 && strcmp(mode, "paste") != 0) {
            send_problem(fd, 400, "Bad Request", "invalid_terminal_input_mode",
                         "mode must be raw or paste.");
            return;
        }
        refresh_session_state(runtime, session);
        terminal_scrollback_show_live(find_surface_id(runtime, session->id));
        result = write_terminal_input(session, data, mode, &requested,
                                      &written, &effective_mode);
        if (result != 0) {
            if (session->pty_fd < 0 || strcmp(session->state, "running") != 0) {
                send_problem(fd, 409, "Conflict", "session_not_running",
                             "Input requires a running terminal session.");
            } else {
                send_problem(fd, 503, "Service Unavailable",
                             "input_backpressure",
                             "The terminal PTY cannot accept the complete input request now.");
            }
            return;
        }
        snprintf(body, sizeof(body),
                 "{\"session_id\":\"session-%llu\","
                 "\"bytes_requested\":%lu,\"bytes_written\":%lu,"
                 "\"mode\":\"%s\"}", session->id,
                 (unsigned long)requested, (unsigned long)written,
                 effective_mode);
        send_http(fd, 202, "Accepted", "application/json", body);
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
            if (idempotency_replay(runtime, fd, request, method, path, payload))
                return;
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
        if (strcmp(method, "PATCH") == 0)
            idempotency_store(runtime, request, method, path, payload, 200,
                              "OK", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/topology") == 0 && strcmp(method, "PUT") == 0) {
        int columns;
        int rows;
        int requested_revision;
        int has_requested_revision = strstr(payload, "\"revision\"") != NULL;
        int placements_present = strstr(payload, "\"placements\"") != NULL;
        int placements_count = 0;
        char placement_ids[BLIT_MAX_DISPLAYS][128];
        int placement_x[BLIT_MAX_DISPLAYS];
        int placement_y[BLIT_MAX_DISPLAYS];
        int layout_changed;
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
        if (placements_present) {
            placements_count = json_output_placements(
                payload, placement_ids, placement_x, placement_y,
                BLIT_MAX_DISPLAYS);
            if (placements_count < 0 || placements_count != runtime->display_count) {
                send_problem(fd, 400, "Bad Request", "invalid_topology_placements",
                             "placements must contain exactly one x/y entry for every display.");
                return;
            }
            for (i = 0; i < placements_count; ++i) {
                int display_index = display_index_for_id(runtime,
                                                         placement_ids[i]);
                int prior;
                if (display_index < 0) {
                    send_problem(fd, 404, "Not Found", "display_not_found",
                                 "A topology placement names an unknown display.");
                    return;
                }
                for (prior = 0; prior < i; ++prior) {
                    if (strcmp(placement_ids[prior], placement_ids[i]) == 0) {
                        send_problem(fd, 400, "Bad Request",
                                     "duplicate_topology_placement",
                                     "Each display may occur only once in placements.");
                        return;
                    }
                }
            }
        }
        layout_changed = columns != runtime->pane_columns ||
            rows != runtime->pane_rows;
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (layout_changed && runtime->sessions[i].used &&
                runtime->sessions[i].pane_id[0]) {
                send_problem(fd, 409, "Conflict", "topology_in_use",
                             "The topology cannot change while a pane is attached.");
                return;
            }
        }
        runtime->pane_columns = columns;
        runtime->pane_rows = rows;
        if (layout_changed)
            discover_panes(runtime);
        if (placements_present)
            apply_display_placements(runtime, placement_ids, placement_x,
                                     placement_y, placements_count);
        else if (layout_changed) {
            for (i = 0; i < runtime->output_count; ++i)
                runtime->outputs[i].initialized = 0;
            runtime->render_pending = 1;
        }
        ++runtime->topology_revision;
        topology_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        idempotency_store(runtime, request, method, path, payload, 200,
                          "OK", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/input/inject") == 0 &&
        strcmp(method, "POST") == 0) {
        uint64_t input_started_us = monotonic_us();
        char kind[64];
        int x;
        int y;
        int button;
        int pressed;
        int delta;

        if (!json_string(payload, "kind", kind, sizeof(kind))) {
            send_problem(fd, 400, "Bad Request", "invalid_input_event",
                         "An input event kind is required.");
            return;
        }
        if (strcmp(kind, "pointer_motion") == 0) {
            if (!json_int(payload, "x", &x) || !json_int(payload, "y", &y)) {
                send_problem(fd, 400, "Bad Request", "invalid_input_event",
                             "Pointer motion requires integer x and y coordinates.");
                return;
            }
            runtime->evdev_mouse_x = (float)x;
            runtime->evdev_mouse_y = (float)y;
            update_pointer_position(runtime, "api-pointer");
        } else if (strcmp(kind, "pointer_button") == 0) {
            if (!json_int(payload, "button", &button) ||
                !json_bool(payload, "pressed", &pressed) ||
                button < 0 || button > 2) {
                send_problem(fd, 400, "Bad Request", "invalid_input_event",
                             "Pointer buttons require button 0..2 and pressed boolean.");
                return;
            }
            dispatch_pointer_button(runtime, button, pressed, "api-pointer");
        } else if (strcmp(kind, "pointer_wheel") == 0) {
            if (!json_int(payload, "delta", &delta) || delta == 0) {
                send_problem(fd, 400, "Bad Request", "invalid_input_event",
                             "Pointer wheel requires a non-zero integer delta.");
                return;
            }
            broadcast_evdev_pointer(runtime, "pointer_wheel",
                                     delta > 0 ? 64 : 65, 1, delta,
                                     "api-pointer");
            send_mouse_report(runtime, 0, runtime->pointer_x,
                              runtime->pointer_y, delta > 0 ? 64 : 65, 0);
        } else {
            send_problem(fd, 422, "Unprocessable Entity", "unsupported_input_event",
                         "Only pointer_motion, pointer_button, and pointer_wheel are supported.");
            return;
        }
        record_input_dispatch(runtime, input_started_us);
        snprintf(body, sizeof(body),
                 "{\"accepted\":true,\"kind\":\"%s\"}", kind);
        send_http(fd, 202, "Accepted", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/input/routes") == 0 &&
        strcmp(method, "GET") == 0) {
        input_routes_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strncmp(path, "/v1/input/routes/", strlen("/v1/input/routes/")) == 0 &&
        strcmp(method, "DELETE") == 0) {
        const char *route_id = path + strlen("/v1/input/routes/");
        BlitInputRoute *route = find_input_route(runtime, route_id);
        if (*route_id == '\0' || route == NULL) {
            send_problem(fd, 404, "Not Found", "input_route_not_found",
                         "The requested input route does not exist.");
            return;
        }
        memset(route, 0, sizeof(*route));
        ++runtime->input_routes_revision;
        send_http(fd, 204, "No Content", "", "");
        return;
    }
    if (strncmp(path, "/v1/input/routes/", strlen("/v1/input/routes/")) == 0 &&
        strcmp(method, "PUT") == 0) {
        const char *route_id = path + strlen("/v1/input/routes/");
        char device_id[128];
        char target_session_id[128];
        int enabled;
        BlitInputRoute *route = find_input_route(runtime, route_id);
        BlitSession *target_session;
        int i;

        if (*route_id == '\0' || strlen(route_id) >= sizeof(device_id) ||
            !json_string(payload, "device_id", device_id, sizeof(device_id)) ||
            !json_string(payload, "target_session_id", target_session_id,
                         sizeof(target_session_id)) ||
            !json_bool(payload, "enabled", &enabled) ||
            (strcmp(device_id, "evdev-keyboard") != 0 &&
             strcmp(device_id, "evdev-pointer") != 0 &&
             strcmp(device_id, "evdev-gamepad") != 0) ||
            strncmp(target_session_id, "session-", 8) != 0 ||
            find_session_id(runtime,
                strtoull(target_session_id + 8, NULL, 10)) == NULL) {
            send_problem(fd, 400, "Bad Request", "invalid_input_route",
                         "A route requires a supported evdev device, a live session target, and enabled boolean.");
            return;
        }
        target_session = find_session_id(runtime,
            strtoull(target_session_id + 8, NULL, 10));
        if (target_session == NULL) {
            send_problem(fd, 400, "Bad Request", "invalid_input_route",
                         "An input route target session does not exist.");
            return;
        }
        refresh_session_state(runtime, target_session);
        if (enabled && strcmp(target_session->state, "running") != 0 &&
            strcmp(target_session->state, "starting") != 0) {
            send_problem(fd, 409, "Conflict", "input_route_target_stopped",
                         "An enabled input route requires a live target session.");
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
    if (strcmp(path, "/v1/effects") == 0 &&
        (strcmp(method, "GET") == 0 || strcmp(method, "PUT") == 0)) {
        int enabled;
        int intensity;
        int revision;
        char preset[32];

        if (strcmp(method, "GET") == 0) {
            effect_json(runtime, body, sizeof(body));
            send_http(fd, 200, "OK", "application/json", body);
            return;
        }
        if (!json_int(payload, "revision", &revision) || revision < 0) {
            send_problem(fd, 400, "Bad Request", "invalid_effect_revision",
                         "revision must be a non-negative integer.");
            return;
        }
        if ((unsigned long long)revision != runtime->effect.revision) {
            send_revision_problem(fd, "revision_conflict",
                                  "The effect revision is no longer current.",
                                  runtime->effect.revision);
            return;
        }
        if (!json_bool(payload, "enabled", &enabled) ||
            !json_string(payload, "preset", preset, sizeof(preset)) ||
            !json_int(payload, "intensity", &intensity) || intensity < 0 ||
            intensity > 100 || (strcmp(preset, "none") != 0 &&
                                strcmp(preset, "scanlines") != 0)) {
            send_problem(fd, 400, "Bad Request", "invalid_effect",
                         "revision, enabled, preset, and intensity are required; preset must be none or scanlines and intensity must be 0..100.");
            return;
        }
        runtime->effect.enabled = enabled;
        snprintf(runtime->effect.preset, sizeof(runtime->effect.preset),
                 "%s", preset);
        runtime->effect.intensity = intensity;
        ++runtime->effect.revision;
        {
            int output_index;
            for (output_index = 0; output_index < runtime->output_count;
                 ++output_index)
                runtime->outputs[output_index].initialized = 0;
        }
        runtime->render_pending = 1;
        effect_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
        return;
    }
    if (strcmp(path, "/v1/overlays") == 0 &&
        (strcmp(method, "GET") == 0 || strcmp(method, "POST") == 0)) {
        if (strcmp(method, "GET") == 0) {
            overlays_json(runtime, body, sizeof(body));
            send_http(fd, 200, "OK", "application/json", body);
            return;
        }
        {
            int x;
            int y;
            int width;
            int height;
            int opacity;
            uint32_t color;
            char display_id[128];
            int i;
            BlitOverlay *overlay = NULL;

            if (!json_string(payload, "display_id", display_id,
                             sizeof(display_id)) ||
                !display_id_is_known(runtime, display_id) ||
                !json_int(payload, "x", &x) || !json_int(payload, "y", &y) ||
                !json_int(payload, "width", &width) || width < 1 || width > 4096 ||
                !json_int(payload, "height", &height) || height < 1 || height > 4096 ||
                !json_string(payload, "color", body, sizeof(body)) ||
                !parse_color(body, &color) ||
                !json_int(payload, "opacity", &opacity) ||
                opacity < 0 || opacity > 100) {
                send_problem(fd, 400, "Bad Request", "invalid_overlay",
                             "display_id, x, y, positive width/height, #RRGGBB color, and opacity 0..100 are required.");
                return;
            }
            for (i = 0; i < BLIT_MAX_OVERLAYS; ++i)
                if (!runtime->overlays[i].used) {
                    overlay = &runtime->overlays[i];
                    break;
                }
            if (overlay == NULL) {
                send_problem(fd, 503, "Service Unavailable",
                             "overlay_capacity",
                             "The overlay capacity has been reached.");
                return;
            }
            memset(overlay, 0, sizeof(*overlay));
            overlay->used = 1;
            overlay->id = runtime->next_overlay_id++;
            overlay->revision = 1;
            snprintf(overlay->display_id, sizeof(overlay->display_id), "%s",
                     display_id);
            overlay->x = x;
            overlay->y = y;
            overlay->width = width;
            overlay->height = height;
            overlay->color = color;
            overlay->opacity = opacity;
            for (i = 0; i < runtime->output_count; ++i)
                runtime->outputs[i].initialized = 0;
            runtime->render_pending = 1;
            overlay_json(overlay, body, sizeof(body));
            send_http(fd, 201, "Created", "application/json", body);
            return;
        }
    }
    if (strncmp(path, "/v1/overlays/", strlen("/v1/overlays/")) == 0 &&
        strcmp(method, "DELETE") == 0) {
        unsigned long long overlay_id = strtoull(
            path + strlen("/v1/overlays/"), NULL, 10);
        int i;
        for (i = 0; i < BLIT_MAX_OVERLAYS; ++i)
            if (runtime->overlays[i].used &&
                runtime->overlays[i].id == overlay_id) {
                memset(&runtime->overlays[i], 0, sizeof(runtime->overlays[i]));
                for (i = 0; i < runtime->output_count; ++i)
                    runtime->outputs[i].initialized = 0;
                runtime->render_pending = 1;
                send_http(fd, 204, "No Content", "application/json", "");
                return;
            }
        send_problem(fd, 404, "Not Found", "overlay_not_found",
                     "The requested overlay does not exist.");
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
    } else if (strcmp(path, "/v1/control/update/status") == 0) {
        update_status_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/version") == 0) {
        version_json(body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/diagnostics") == 0) {
        diagnostics_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strcmp(path, "/v1/diagnostics/runtime") == 0) {
        runtime_diagnostics_json(runtime, body, sizeof(body));
        send_http(fd, 200, "OK", "application/json", body);
    } else if (strncmp(path, "/v1/diagnostics/trace",
                       strlen("/v1/diagnostics/trace")) == 0 &&
               (path[strlen("/v1/diagnostics/trace")] == '\0' ||
                path[strlen("/v1/diagnostics/trace")] == '?')) {
        char kind[64];
        const char *kind_value = strstr(path, "kind=");
        size_t kind_length = 0;
        kind[0] = '\0';
        if (kind_value != NULL) {
            kind_value += strlen("kind=");
            while (kind_value[kind_length] != '\0' &&
                   kind_value[kind_length] != '&' &&
                   kind_length + 1 < sizeof(kind))
                ++kind_length;
            memcpy(kind, kind_value, kind_length);
            kind[kind_length] = '\0';
        }
        diagnostic_trace_json(runtime, body, sizeof(body), kind);
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
        runtime->kms = blit_kms_new(&runtime->font);
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
            runtime->displays[i].mode_count = info.mode_count;
            runtime->displays[i].active_mode = info.active_mode;
            memcpy(runtime->displays[i].modes, info.modes,
                   sizeof(runtime->displays[i].modes));
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
        const SDL_DisplayMode *mode;
        runtime->displays[i].id = (unsigned long long)ids[i];
        snprintf(runtime->displays[i].name, sizeof(runtime->displays[i].name),
                 "%s", name ? name : "unknown");
        if (!SDL_GetDisplayBounds(ids[i], &runtime->displays[i].bounds)) {
            runtime->displays[i].bounds.x = 0;
            runtime->displays[i].bounds.y = 0;
            runtime->displays[i].bounds.w = 0;
            runtime->displays[i].bounds.h = 0;
        }
        mode = SDL_GetCurrentDisplayMode(ids[i]);
        if (mode == NULL)
            mode = SDL_GetDesktopDisplayMode(ids[i]);
        runtime->displays[i].refresh_hz = mode != NULL &&
            mode->refresh_rate > 0.0f ? (double)mode->refresh_rate : 60.0;
        {
            SDL_DisplayMode **available;
            int available_count = 0;
            int mode_index;
            int target_width = mode != NULL ? mode->w :
                runtime->displays[i].bounds.w;
            int target_height = mode != NULL ? mode->h :
                runtime->displays[i].bounds.h;
            available = SDL_GetFullscreenDisplayModes(ids[i],
                                                       &available_count);
            runtime->displays[i].mode_count = available_count <
                BLIT_KMS_MAX_MODES ? available_count : BLIT_KMS_MAX_MODES;
            runtime->displays[i].active_mode = -1;
            for (mode_index = 0; mode_index < runtime->displays[i].mode_count;
                 ++mode_index) {
                SDL_DisplayMode *available_mode = available[mode_index];
                BlitKmsMode *stored = &runtime->displays[i].modes[mode_index];
                stored->width = available_mode->w;
                stored->height = available_mode->h;
                stored->refresh_hz = available_mode->refresh_rate > 0.0f ?
                    (double)available_mode->refresh_rate : 60.0;
                stored->preferred = available_mode->w == target_width &&
                    available_mode->h == target_height;
                if (stored->width == target_width &&
                    stored->height == target_height &&
                    runtime->displays[i].active_mode < 0)
                    runtime->displays[i].active_mode = mode_index;
            }
            if (available != NULL)
                SDL_free(available);
            if (runtime->displays[i].mode_count == 0) {
                runtime->displays[i].mode_count = 1;
                runtime->displays[i].active_mode = 0;
                runtime->displays[i].modes[0].width = target_width;
                runtime->displays[i].modes[0].height = target_height;
                runtime->displays[i].modes[0].refresh_hz =
                    runtime->displays[i].refresh_hz;
                runtime->displays[i].modes[0].preferred = 1;
            } else if (runtime->displays[i].active_mode < 0) {
                runtime->displays[i].active_mode = 0;
            }
        }
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
            blit_kms_cell_width(runtime->kms) : blit_font_width(&runtime->font)) *
            runtime->font_scale;
        int cell_height = (runtime->direct_kms ?
            blit_kms_cell_height(runtime->kms) : blit_font_height(&runtime->font)) *
            runtime->font_scale;
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

    /* Let SDL choose the accelerated renderer on real hardware.  Software is
       still available as an explicit deterministic fallback for VKMS and
       headless fixtures: BLIT_RENDERER=software. */
    if (renderer_name != NULL && renderer_name[0] == '\0')
        renderer_name = NULL;

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
        /* A borderless window is not sufficient on KMSDRM: the backend can
           retain its default mode-sized GBM surface even when the requested
           window dimensions match the display.  Enter exclusive fullscreen
           after positioning so each output owns its complete scanout mode,
           including portrait/tall modes such as 1920x2160. */
        if (!SDL_SetWindowFullscreen(window, true)) {
            fprintf(stderr,
                    "blit: SDL fullscreen display=%llu failed: %s\n",
                    display->id, SDL_GetError());
            SDL_DestroyWindow(window);
            continue;
        }
        renderer = SDL_CreateRenderer(window, renderer_name);
        if (renderer == NULL) {
            snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                     "display %llu renderer: %s", display->id, SDL_GetError());
            SDL_DestroyWindow(window);
            continue;
        }
        /* Hide after the window exists.  Some SDL video backends do not apply
           the global cursor state to a window created later, which leaves a
           native cursor competing with Blit's software cursor. */
        SDL_HideCursor();
        if (runtime->sdl_renderer_name[0] == '\0')
            snprintf(runtime->sdl_renderer_name,
                     sizeof(runtime->sdl_renderer_name), "%s",
                     SDL_GetRendererName(renderer) != NULL ?
                     SDL_GetRendererName(renderer) : "unknown");
        if (!SDL_GetRenderOutputSize(renderer,
                                     &runtime->outputs[runtime->output_count].render_width,
                                     &runtime->outputs[runtime->output_count].render_height)) {
            runtime->outputs[runtime->output_count].render_width = 0;
            runtime->outputs[runtime->output_count].render_height = 0;
        }
        {
            SDL_Rect viewport;
            if (SDL_GetRenderViewport(renderer, &viewport)) {
                runtime->outputs[runtime->output_count].viewport_width =
                    viewport.w;
                runtime->outputs[runtime->output_count].viewport_height =
                    viewport.h;
            }
            if (!SDL_GetRenderScale(renderer,
                                    &runtime->outputs[runtime->output_count].scale_x,
                                    &runtime->outputs[runtime->output_count].scale_y)) {
                runtime->outputs[runtime->output_count].scale_x = 0.0f;
                runtime->outputs[runtime->output_count].scale_y = 0.0f;
            }
        }
        fprintf(stderr, "blit: SDL output display=%llu render=%dx%d\n",
                display->id,
                runtime->outputs[runtime->output_count].render_width,
                runtime->outputs[runtime->output_count].render_height);
        fprintf(stderr, "blit: SDL viewport display=%llu size=%dx%d scale=%.3fx%.3f\n",
                display->id,
                runtime->outputs[runtime->output_count].viewport_width,
                runtime->outputs[runtime->output_count].viewport_height,
                runtime->outputs[runtime->output_count].scale_x,
                runtime->outputs[runtime->output_count].scale_y);
        if (SDL_SetRenderVSync(renderer, 1)) {
            int vsync = 0;
            if (SDL_GetRenderVSync(renderer, &vsync) && vsync > 0)
                runtime->outputs[runtime->output_count].vsync_enabled = 1;
            else
                ++runtime->sdl_vsync_failures;
        } else {
            ++runtime->sdl_vsync_failures;
            fprintf(stderr,
                    "blit: SDL renderer display=%llu cannot enable vsync: %s\n",
                    display->id, SDL_GetError());
        }
        SDL_SetRenderScale(renderer, (float)runtime->font_scale,
                           (float)runtime->font_scale);
        SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
        SDL_RenderClear(renderer);
        SDL_RenderPresent(renderer);
        runtime->outputs[runtime->output_count].window = window;
        runtime->outputs[runtime->output_count].renderer = renderer;
        runtime->outputs[runtime->output_count].glyph_atlas =
            create_sdl_glyph_atlas(renderer, &runtime->font,
                                   &runtime->outputs[runtime->output_count].glyph_atlas_columns);
        runtime->outputs[runtime->output_count].display_id = display->id;
        ++runtime->output_count;
    }
    if (runtime->output_count != runtime->display_count)
        activate_direct_kms_fallback(runtime);
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
        if (runtime->outputs[i].screenshot_surface != NULL)
            SDL_DestroySurface(runtime->outputs[i].screenshot_surface);
        runtime->outputs[i].screenshot_surface = NULL;
        destroy_sdl_graphics_cache(&runtime->outputs[i]);
        if (runtime->outputs[i].scene_texture != NULL)
            SDL_DestroyTexture(runtime->outputs[i].scene_texture);
        runtime->outputs[i].scene_texture = NULL;
        if (runtime->outputs[i].glyph_atlas != NULL)
            SDL_DestroyTexture(runtime->outputs[i].glyph_atlas);
        runtime->outputs[i].glyph_atlas = NULL;
        runtime->outputs[i].glyph_atlas_columns = 0;
        free(runtime->outputs[i].glyph_vertices);
        free(runtime->outputs[i].glyph_indices);
        free(runtime->outputs[i].background_vertices);
        free(runtime->outputs[i].background_indices);
        runtime->outputs[i].glyph_vertices = NULL;
        runtime->outputs[i].glyph_indices = NULL;
        runtime->outputs[i].glyph_vertex_count = 0;
        runtime->outputs[i].glyph_index_count = 0;
        runtime->outputs[i].glyph_capacity = 0;
        runtime->outputs[i].background_vertices = NULL;
        runtime->outputs[i].background_indices = NULL;
        runtime->outputs[i].background_vertex_count = 0;
        runtime->outputs[i].background_index_count = 0;
        runtime->outputs[i].background_capacity = 0;
        SDL_DestroyRenderer(runtime->outputs[i].renderer);
        SDL_DestroyWindow(runtime->outputs[i].window);
    }
    runtime->output_count = 0;
}

static int activate_direct_kms_fallback(BlitRuntime *runtime)
{
    char error[256];
    int i;

    if (runtime->direct_kms || runtime->display_count == 0)
        return 0;
    destroy_outputs(runtime);
    error[0] = '\0';
    runtime->kms = blit_kms_new(&runtime->font);
    if (runtime->kms == NULL || blit_kms_open(runtime->kms, error,
                                               sizeof(error)) < 0) {
        if (runtime->kms != NULL) {
            blit_kms_close(runtime->kms);
            runtime->kms = NULL;
        }
        snprintf(runtime->sdl_error, sizeof(runtime->sdl_error),
                 "SDL outputs unavailable; direct KMS fallback failed: %s",
                 error[0] ? error : "initialization failed");
        diagnostic_log(runtime, "error", "sdl_kms_fallback_failed",
                       runtime->sdl_error);
        return 0;
    }
    runtime->direct_kms = 1;
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
        runtime->displays[i].mode_count = info.mode_count;
        runtime->displays[i].active_mode = info.active_mode;
        memcpy(runtime->displays[i].modes, info.modes,
               sizeof(runtime->displays[i].modes));
        runtime->outputs[i].display_id = info.id;
    }
    apply_test_refresh_override(runtime);
    runtime->output_count = runtime->display_count;
    runtime->sdl_error[0] = '\0';
    fprintf(stderr,
            "blit: SDL presentation unavailable; using direct KMS fallback "
            "(%d display(s))\n", runtime->display_count);
    diagnostic_log(runtime, "warning", "sdl_kms_fallback",
                   "SDL outputs were unavailable; direct KMS is active");
    return runtime->display_count > 0;
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

static int evdev_has_gamepad(int fd)
{
    unsigned long keys[32];
    unsigned long absolute[4];
    int has_axis;
    int has_button;
    memset(keys, 0, sizeof(keys));
    memset(absolute, 0, sizeof(absolute));
    if (ioctl(fd, EVIOCGBIT(EV_ABS, sizeof(absolute)), absolute) < 0)
        return 0;
    if (ioctl(fd, EVIOCGBIT(EV_KEY, sizeof(keys)), keys) < 0)
        return 0;
    has_axis = input_bit(absolute, ABS_X) || input_bit(absolute, ABS_Y) ||
        input_bit(absolute, ABS_RX) || input_bit(absolute, ABS_RY);
    has_button = input_bit(keys, BTN_GAMEPAD) ||
        input_bit(keys, BTN_SOUTH) || input_bit(keys, BTN_EAST) ||
        input_bit(keys, BTN_NORTH) || input_bit(keys, BTN_WEST);
    return has_axis && has_button;
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

static int open_evdev_gamepads(int *fds, char paths[][128], int *count)
{
    int index;
    *count = 0;
    for (index = 0; index < 32 && *count < BLIT_MAX_INPUT_DEVICES; ++index) {
        char path[64];
        int fd;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
        if (fd < 0) continue;
        if (evdev_has_gamepad(fd)) {
            fds[*count] = fd;
            snprintf(paths[(*count)++], 128, "%s", path);
        } else close(fd);
    }
    return *count;
}

static int evdev_path_open(const int *fds, char paths[][128], int count,
                           const char *path)
{
    int i;
    for (i = 0; i < count; ++i)
        if (fds[i] >= 0 && strcmp(paths[i], path) == 0)
            return 1;
    return 0;
}

static void broadcast_input_diagnostic(BlitRuntime *runtime,
                                       const char *severity,
                                       const char *code,
                                       const char *device_id,
                                       const char *path,
                                       int active_devices);

static void open_evdev_watch(BlitRuntime *runtime)
{
    int fd;

    if (runtime->evdev_watch_fd >= 0)
        return;
    fd = inotify_init1(IN_NONBLOCK | IN_CLOEXEC);
    if (fd < 0)
        return;
    if (inotify_add_watch(fd, "/dev/input",
                          IN_CREATE | IN_DELETE | IN_MOVED_TO |
                          IN_MOVED_FROM | IN_ATTRIB) < 0) {
        close(fd);
        return;
    }
    runtime->evdev_watch_fd = fd;
}

static void service_evdev_watch(BlitRuntime *runtime)
{
    char buffer[4096];
    ssize_t received;

    if (runtime->evdev_watch_fd < 0)
        return;
    for (;;) {
        received = read(runtime->evdev_watch_fd, buffer, sizeof(buffer));
        if (received > 0) {
            runtime->evdev_rescan_requested = 1;
            continue;
        }
        if (received < 0 && (errno == EINTR || errno == EAGAIN ||
                             errno == EWOULDBLOCK))
            return;
        close(runtime->evdev_watch_fd);
        runtime->evdev_watch_fd = -1;
        return;
    }
}

static void reconnect_evdev_devices(BlitRuntime *runtime)
{
    int index;

    if (!runtime->evdev_rescan_requested)
        return;
    runtime->evdev_rescan_requested = 0;
    for (index = 0; index < 32; ++index) {
        char path[64];
        int fd;
        snprintf(path, sizeof(path), "/dev/input/event%d", index);
        if (runtime->evdev_mouse_count < BLIT_MAX_INPUT_DEVICES &&
            !evdev_path_open(runtime->evdev_mouse_fds,
                             runtime->evdev_mouse_paths,
                             runtime->evdev_mouse_count, path)) {
            fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0 && evdev_has_rel_xy(fd)) {
                int slot = runtime->evdev_mouse_count++;
                runtime->evdev_mouse_fds[slot] = fd;
                snprintf(runtime->evdev_mouse_paths[slot], 128, "%s", path);
                runtime->evdev_mouse_reported[slot] = 0;
                fprintf(stderr, "blit: evdev mouse reconnected: %s fd=%d\n",
                        path, fd);
                broadcast_input_diagnostic(runtime, "info",
                                            "input_device_reconnected",
                                            "evdev-pointer", path,
                                            runtime->evdev_mouse_count);
                fd = -1;
            }
            if (fd >= 0) close(fd);
        }
        if (runtime->evdev_keyboard_count < BLIT_MAX_INPUT_DEVICES &&
            !evdev_path_open(runtime->evdev_keyboard_fds,
                             runtime->evdev_keyboard_paths,
                             runtime->evdev_keyboard_count, path)) {
            fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0 && evdev_has_keyboard(fd)) {
                int slot = runtime->evdev_keyboard_count++;
                runtime->evdev_keyboard_fds[slot] = fd;
                snprintf(runtime->evdev_keyboard_paths[slot], 128, "%s", path);
                runtime->evdev_keyboard_reported[slot] = 0;
                fprintf(stderr, "blit: evdev keyboard reconnected: %s fd=%d\n",
                        path, fd);
                broadcast_input_diagnostic(runtime, "info",
                                            "input_device_reconnected",
                                            "evdev-keyboard", path,
                                            runtime->evdev_keyboard_count);
                fd = -1;
            }
            if (fd >= 0) close(fd);
        }
        if (runtime->evdev_gamepad_count < BLIT_MAX_INPUT_DEVICES &&
            !evdev_path_open(runtime->evdev_gamepad_fds,
                             runtime->evdev_gamepad_paths,
                             runtime->evdev_gamepad_count, path)) {
            fd = open(path, O_RDONLY | O_NONBLOCK | O_CLOEXEC);
            if (fd >= 0 && evdev_has_gamepad(fd)) {
                int slot = runtime->evdev_gamepad_count++;
                runtime->evdev_gamepad_fds[slot] = fd;
                snprintf(runtime->evdev_gamepad_paths[slot], 128, "%s", path);
                runtime->evdev_gamepad_reported[slot] = 0;
                fprintf(stderr, "blit: evdev gamepad reconnected: %s fd=%d\n",
                        path, fd);
                broadcast_input_diagnostic(runtime, "info",
                                            "input_device_reconnected",
                                            "evdev-gamepad", path,
                                            runtime->evdev_gamepad_count);
                fd = -1;
            }
            if (fd >= 0) close(fd);
        }
    }
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

/* Cell damage can be composed incrementally from the committed scene cache.
 * Graphics are different: hiding/replacing an image can expose pixels that
 * are outside the cell rectangle that caused the render request. Track the
 * complete graphics scene separately so only graphics changes require a
 * full scene rebuild for an external Hibr surface. */
static unsigned long long graphics_scene_signature(const BlitRuntime *runtime,
                                                   int output_index)
{
    unsigned long long signature = 1469598103934665603ULL;
    int session_index;
    int image_index;

    if (runtime == NULL || output_index < 0 ||
        output_index >= runtime->output_count)
        return signature;
    for (session_index = 0; session_index < BLIT_MAX_SESSIONS;
         ++session_index) {
        const BlitSession *session = &runtime->sessions[session_index];
        BlitSurface *surface;
        if (!session->used)
            continue;
        surface = find_surface_id((BlitRuntime *)runtime, session->id);
        if (surface == NULL || !surface_targets_output((BlitRuntime *)runtime,
                                                        surface, output_index))
            continue;
        for (image_index = 0; image_index < BLIT_GRAPHICS_MAX_IMAGES;
             ++image_index) {
            const BlitGraphicsImage *image = &session->graphics[image_index];
            if (!image->used)
                continue;
            /* The signature describes the graphics scene, not session
             * membership. A session with no images must contribute nothing;
             * otherwise an ordinary surface/session transition is reported
             * as graphics_changed and forces a full redraw. */
            signature ^= session->id;
            signature *= 1099511628211ULL;
            signature ^= image->id;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(image->complete != 0);
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(image->visible != 0);
            signature *= 1099511628211ULL;
            signature ^= image->generation;
            signature *= 1099511628211ULL;
            signature ^= image->revision;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(unsigned int)image->x;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(unsigned int)image->y;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(unsigned int)image->placement_width;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(unsigned int)image->placement_height;
            signature *= 1099511628211ULL;
            signature ^= (unsigned long long)(unsigned int)image->z;
            signature *= 1099511628211ULL;
        }
    }
    return signature;
}

static int graphics_scene_count(const BlitRuntime *runtime, int output_index)
{
    int session_index;
    int image_index;
    int count = 0;

    if (runtime == NULL || output_index < 0 ||
        output_index >= runtime->output_count)
        return 0;
    for (session_index = 0; session_index < BLIT_MAX_SESSIONS;
         ++session_index) {
        const BlitSession *session = &runtime->sessions[session_index];
        BlitSurface *surface;
        if (!session->used)
            continue;
        surface = find_surface_id((BlitRuntime *)runtime, session->id);
        if (surface == NULL || !surface_targets_output((BlitRuntime *)runtime,
                                                        surface, output_index))
            continue;
        for (image_index = 0; image_index < BLIT_GRAPHICS_MAX_IMAGES;
             ++image_index)
            if (session->graphics[image_index].used)
                ++count;
    }
    return count;
}

static int active_output_surface_count(const BlitRuntime *runtime,
                                       int output_index)
{
    int surface_index;
    int count = 0;

    if (runtime == NULL || output_index < 0 ||
        output_index >= runtime->output_count)
        return 0;
    for (surface_index = 0; surface_index < BLIT_MAX_SESSIONS;
         ++surface_index) {
        BlitSurface *surface = runtime->surfaces[surface_index];
        if (surface != NULL && surface->used &&
            surface_targets_output((BlitRuntime *)runtime, surface,
                                   output_index))
            ++count;
    }
    return count;
}

static SDL_Texture *create_sdl_glyph_atlas(SDL_Renderer *renderer,
                                           const BlitFont *font,
                                           int *columns_out)
{
    SDL_Texture *texture;
    unsigned char *pixels;
    size_t pixel_bytes;
    size_t glyph;
    int columns;
    int rows;
    int width;
    int height;
    int tile_width;
    int tile_height;
    int row;
    int column;

    if (renderer == NULL || font == NULL || font->glyph_count == 0 ||
        font->width < 1 || font->height < 1 || font->width > 32 ||
        font->height > 64)
        return NULL;
    /* Keep the atlas compact while leaving room for the bundled Hibr font and
       future PSF2 additions. Every cell is a white alpha mask; the renderer
       applies the terminal foreground colour with texture modulation. */
    columns = 32;
    /* Keep a transparent/duplicated edge around every bitmap.  Nearest
       filtering avoids interpolation, but rasterization at a normalized
       tile boundary can still select a neighbouring texel after scaling.
       The border makes the atlas tile self-contained. */
    tile_width = font->width + 2;
    tile_height = font->height + 2;
    rows = (int)((font->glyph_count + (size_t)columns - 1) /
                 (size_t)columns);
    width = columns * tile_width;
    height = rows * tile_height;
    pixel_bytes = (size_t)width * (size_t)height * 4U;
    pixels = (unsigned char *)calloc(1, pixel_bytes);
    if (pixels == NULL)
        return NULL;
    for (glyph = 0; glyph < font->glyph_count; ++glyph) {
        int atlas_x = (int)(glyph % (size_t)columns) * tile_width + 1;
        int atlas_y = (int)(glyph / (size_t)columns) * tile_height + 1;
        for (row = 0; row < font->height; ++row) {
            uint32_t bits = blit_font_glyph_row(font, glyph,
                                                font->codepoints != NULL ?
                                                    font->codepoints[glyph] : 0,
                                                row);
            for (column = 0; column < font->width; ++column) {
                size_t offset;
                if (!(bits & (1U << (font->width - column - 1))))
                    continue;
                offset = ((size_t)(atlas_y + row) * (size_t)width +
                          (size_t)(atlas_x + column)) * 4U;
                pixels[offset] = 255;
                pixels[offset + 1] = 255;
                pixels[offset + 2] = 255;
                pixels[offset + 3] = 255;
            }
        }
        /* Duplicate the interior edge texels into the border.  Transparent
           edge pixels remain transparent; lit edge pixels remain continuous
           if the sampler lands on the guard texel. */
        for (row = 0; row < font->height; ++row) {
            size_t left = ((size_t)(atlas_y + row) * (size_t)width +
                           (size_t)(atlas_x - 1)) * 4U;
            size_t right = ((size_t)(atlas_y + row) * (size_t)width +
                            (size_t)(atlas_x + font->width)) * 4U;
            memcpy(pixels + left, pixels + left + 4U, 4U);
            memcpy(pixels + right, pixels + right - 4U, 4U);
        }
        for (column = -1; column <= font->width; ++column) {
            size_t top = ((size_t)(atlas_y - 1) * (size_t)width +
                          (size_t)(atlas_x + column)) * 4U;
            size_t bottom = ((size_t)(atlas_y + font->height) *
                             (size_t)width + (size_t)(atlas_x + column)) * 4U;
            memcpy(pixels + top, pixels + top + (size_t)width * 4U, 4U);
            memcpy(pixels + bottom,
                   pixels + bottom - (size_t)width * 4U, 4U);
        }
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, width, height);
    if (texture == NULL || !SDL_UpdateTexture(texture, NULL, pixels,
                                               width * 4)) {
        if (texture != NULL)
            SDL_DestroyTexture(texture);
        free(pixels);
        return NULL;
    }
    free(pixels);
    SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    /* SDL3 defaults textures to linear filtering. The atlas contains
       adjacent bitmap glyphs with no padding, so linear sampling can blend
       neighbouring glyph pixels into scaled cell edges. Terminal glyphs are
       discrete bitmap cells: use nearest sampling, and fall back to the
       existing per-pixel renderer if the mode is unavailable. */
    if (!SDL_SetTextureScaleMode(texture, SDL_SCALEMODE_NEAREST)) {
        SDL_DestroyTexture(texture);
        return NULL;
    }
    if (columns_out != NULL)
        *columns_out = columns;
    return texture;
}

static int ensure_sdl_background_batch(BlitOutput *output,
                                       int additional_quads)
{
    SDL_Vertex *vertices;
    int *indices;
    int required;
    int capacity;

    if (output == NULL || additional_quads < 1)
        return 0;
    required = output->background_vertex_count + additional_quads * 4;
    if (required <= output->background_capacity)
        return 1;
    capacity = output->background_capacity > 0 ? output->background_capacity : 256;
    while (capacity < required) {
        if (capacity > 1000000)
            return 0;
        capacity *= 2;
    }
    vertices = (SDL_Vertex *)realloc(output->background_vertices,
                                     (size_t)capacity * sizeof(*vertices));
    if (vertices == NULL)
        return 0;
    indices = (int *)realloc(output->background_indices,
                             (size_t)(capacity / 4) * 6U * sizeof(*indices));
    if (indices == NULL) {
        output->background_vertices = vertices;
        return 0;
    }
    output->background_vertices = vertices;
    output->background_indices = indices;
    output->background_capacity = capacity;
    return 1;
}

static void flush_sdl_background_batch(BlitOutput *output)
{
    if (output == NULL || output->renderer == NULL ||
        output->background_vertex_count == 0)
        return;
    SDL_RenderGeometry(output->renderer, NULL,
                       output->background_vertices,
                       output->background_vertex_count,
                       output->background_indices,
                       output->background_index_count);
    ++output->background_batch_submissions;
    output->background_vertex_count = 0;
    output->background_index_count = 0;
}

static int queue_sdl_background(BlitOutput *output, uint32_t colour,
                                float x, float y, float width, float height)
{
    SDL_Vertex *vertices;
    int *indices;
    int vertex;
    int index;
    float red;
    float green;
    float blue;

    if (output == NULL || !ensure_sdl_background_batch(output, 1))
        return 0;
    red = (float)((colour >> 16) & 0xffU) / 255.0f;
    green = (float)((colour >> 8) & 0xffU) / 255.0f;
    blue = (float)(colour & 0xffU) / 255.0f;
    vertex = output->background_vertex_count;
    vertices = output->background_vertices + vertex;
    vertices[0].position.x = x;
    vertices[0].position.y = y;
    vertices[1].position.x = x + width;
    vertices[1].position.y = y;
    vertices[2].position.x = x + width;
    vertices[2].position.y = y + height;
    vertices[3].position.x = x;
    vertices[3].position.y = y + height;
    for (index = 0; index < 4; ++index) {
        vertices[index].color.r = red;
        vertices[index].color.g = green;
        vertices[index].color.b = blue;
        vertices[index].color.a = 1.0f;
        vertices[index].tex_coord.x = 0.0f;
        vertices[index].tex_coord.y = 0.0f;
    }
    index = output->background_index_count;
    indices = output->background_indices + index;
    indices[0] = vertex;
    indices[1] = vertex + 1;
    indices[2] = vertex + 2;
    indices[3] = vertex;
    indices[4] = vertex + 2;
    indices[5] = vertex + 3;
    output->background_vertex_count += 4;
    output->background_index_count += 6;
    ++output->background_quads;
    return 1;
}

static int ensure_sdl_glyph_batch(BlitOutput *output, int additional_quads)
{
    int required;
    int capacity;
    SDL_Vertex *vertices;
    int *indices;

    if (output == NULL || additional_quads < 1)
        return 0;
    required = output->glyph_vertex_count + additional_quads * 4;
    if (required <= output->glyph_capacity)
        return 1;
    capacity = output->glyph_capacity > 0 ? output->glyph_capacity : 256;
    while (capacity < required) {
        if (capacity > 1000000)
            return 0;
        capacity *= 2;
    }
    vertices = (SDL_Vertex *)realloc(output->glyph_vertices,
                                     (size_t)capacity * sizeof(*vertices));
    if (vertices == NULL)
        return 0;
    indices = (int *)realloc(output->glyph_indices,
                             (size_t)(capacity / 4) * 6U * sizeof(*indices));
    if (indices == NULL) {
        /* Keep the vertex allocation valid; the caller falls back to the
           established one-draw path when the index buffer cannot grow. */
        output->glyph_vertices = vertices;
        return 0;
    }
    output->glyph_vertices = vertices;
    output->glyph_indices = indices;
    output->glyph_capacity = capacity;
    return 1;
}

static void flush_sdl_glyph_batch(BlitOutput *output)
{
    if (output == NULL || output->renderer == NULL ||
        output->glyph_atlas == NULL || output->glyph_vertex_count == 0)
        return;
    SDL_RenderGeometry(output->renderer, output->glyph_atlas,
                       output->glyph_vertices, output->glyph_vertex_count,
                       output->glyph_indices, output->glyph_index_count);
    output->glyph_vertex_count = 0;
    output->glyph_index_count = 0;
}

static int queue_sdl_glyph(BlitOutput *output, const BlitFont *font,
                           uint32_t colour, uint32_t codepoint, float x,
                           float y, float cell_width, float cell_height)
{
    SDL_Vertex *vertices;
    int *indices;
    size_t glyph;
    int columns;
    int rows;
    int atlas_width;
    int atlas_height;
    float source_x;
    float source_y;
    float source_w;
    float source_h;
    float red;
    float green;
    float blue;
    int vertex;
    int index;

    if (output == NULL || font == NULL || font->data == NULL ||
        output->glyph_atlas == NULL || output->glyph_atlas_columns < 1)
        return 0;
    glyph = blit_font_glyph_index(font, codepoint);
    if (glyph >= font->glyph_count || !ensure_sdl_glyph_batch(output, 1))
        return 0;
    columns = output->glyph_atlas_columns;
    rows = ((int)font->glyph_count + columns - 1) / columns;
    atlas_width = columns * (font->width + 2);
    atlas_height = rows * (font->height + 2);
    source_x = (float)((int)(glyph % (size_t)columns) * (font->width + 2) + 1) /
        (float)atlas_width;
    source_y = (float)((int)(glyph / (size_t)columns) * (font->height + 2) + 1) /
        (float)atlas_height;
    source_w = (float)font->width / (float)atlas_width;
    source_h = (float)font->height / (float)atlas_height;
    red = (float)((colour >> 16) & 0xffU) / 255.0f;
    green = (float)((colour >> 8) & 0xffU) / 255.0f;
    blue = (float)(colour & 0xffU) / 255.0f;
    vertex = output->glyph_vertex_count;
    vertices = output->glyph_vertices + vertex;
    vertices[0].position.x = x;
    vertices[0].position.y = y;
    vertices[1].position.x = x + cell_width;
    vertices[1].position.y = y;
    vertices[2].position.x = x + cell_width;
    vertices[2].position.y = y + cell_height;
    vertices[3].position.x = x;
    vertices[3].position.y = y + cell_height;
    vertices[0].tex_coord.x = source_x;
    vertices[0].tex_coord.y = source_y;
    vertices[1].tex_coord.x = source_x + source_w;
    vertices[1].tex_coord.y = source_y;
    vertices[2].tex_coord.x = source_x + source_w;
    vertices[2].tex_coord.y = source_y + source_h;
    vertices[3].tex_coord.x = source_x;
    vertices[3].tex_coord.y = source_y + source_h;
    for (index = 0; index < 4; ++index) {
        vertices[index].color.r = red;
        vertices[index].color.g = green;
        vertices[index].color.b = blue;
        vertices[index].color.a = 1.0f;
    }
    index = output->glyph_index_count;
    indices = output->glyph_indices + index;
    indices[0] = vertex;
    indices[1] = vertex + 1;
    indices[2] = vertex + 2;
    indices[3] = vertex;
    indices[4] = vertex + 2;
    indices[5] = vertex + 3;
    output->glyph_vertex_count += 4;
    output->glyph_index_count += 6;
    return 1;
}

static void render_sdl_glyph(SDL_Renderer *renderer, const BlitFont *font,
                             SDL_Texture *glyph_atlas, int atlas_columns,
                             uint32_t colour, uint32_t codepoint, float x,
                             float y, float cell_width, float cell_height)
{
    int source_width = font != NULL && font->data ? font->width : 5;
    int source_height = font != NULL && font->data ? font->height : 7;
    size_t glyph = blit_font_glyph_index(font, codepoint);
    int source_row;

    if (glyph_atlas != NULL && font != NULL && font->data != NULL &&
        glyph < font->glyph_count && atlas_columns > 0) {
        SDL_FRect source;
        SDL_FRect destination;
        source.x = (float)((int)(glyph % (size_t)atlas_columns) *
                           (font->width + 2) + 1);
        source.y = (float)((int)(glyph / (size_t)atlas_columns) *
                           (font->height + 2) + 1);
        source.w = (float)font->width;
        source.h = (float)font->height;
        destination.x = x;
        destination.y = y;
        destination.w = cell_width;
        destination.h = cell_height;
        SDL_SetTextureColorMod(glyph_atlas,
                               (unsigned char)(colour >> 16),
                               (unsigned char)(colour >> 8),
                               (unsigned char)colour);
        SDL_RenderTexture(renderer, glyph_atlas, &source, &destination);
        return;
    }
    for (source_row = 0; source_row < source_height; ++source_row) {
        uint32_t row_bits = blit_font_glyph_row(font, glyph, codepoint,
                                                source_row);
        int source_column = 0;
        while (source_column < source_width) {
            int run_start;
            int run_end;
            while (source_column < source_width &&
                   !(row_bits & (1U << (source_width - source_column - 1))))
                ++source_column;
            run_start = source_column;
            while (source_column < source_width &&
                   (row_bits & (1U << (source_width - source_column - 1))))
                ++source_column;
            run_end = source_column;
            if (run_end > run_start) {
                SDL_FRect pixel_run;
                pixel_run.x = x + cell_width * run_start / source_width;
                pixel_run.y = y + cell_height * source_row / source_height;
                pixel_run.w = cell_width * (run_end - run_start) /
                    source_width;
                pixel_run.h = cell_height / source_height + 0.5f;
                SDL_RenderFillRect(renderer, &pixel_run);
            }
        }
    }
}

static void render_or_queue_sdl_glyph(BlitOutput *output,
                                      const BlitFont *font, uint32_t colour,
                                      uint32_t codepoint, float x, float y,
                                      float cell_width, float cell_height)
{
    if (queue_sdl_glyph(output, font, colour, codepoint, x, y,
                        cell_width, cell_height))
        return;
    flush_sdl_glyph_batch(output);
    render_sdl_glyph(output->renderer, font, output->glyph_atlas,
                     output->glyph_atlas_columns, colour, codepoint, x, y,
                     cell_width, cell_height);
}

static void destroy_sdl_graphics_cache(BlitOutput *output)
{
    int index;
    if (output == NULL) return;
    for (index = 0; index < BLIT_SDL_GRAPHICS_CACHE; ++index) {
        if (output->graphics_textures[index] != NULL)
            SDL_DestroyTexture(output->graphics_textures[index]);
        output->graphics_textures[index] = NULL;
        output->graphics_sources[index] = NULL;
        output->graphics_generations[index] = 0;
        output->graphics_widths[index] = 0;
        output->graphics_heights[index] = 0;
        output->graphics_formats[index] = 0;
    }
}

static SDL_Texture *sdl_graphics_texture(BlitOutput *output,
                                         const BlitGraphicsImage *image)
{
    int index;
    int free_index = -1;
    int source_bytes;
    size_t pixel_count;
    unsigned char *rgba;
    SDL_Texture *texture;
    int pixel_index;
    int reuse_texture = 0;
    uint64_t upload_started_us;

    if (output == NULL || output->renderer == NULL || image == NULL ||
        image->pixels == NULL || image->width < 1 || image->height < 1)
        return NULL;
    upload_started_us = monotonic_us();
    for (index = 0; index < BLIT_SDL_GRAPHICS_CACHE; ++index) {
        if (output->graphics_sources[index] == image) {
            if (output->graphics_generations[index] == image->generation &&
                output->graphics_textures[index] != NULL) {
                ++output->graphics_cache_hits;
                return output->graphics_textures[index];
            }
            if (output->graphics_textures[index] != NULL &&
                output->graphics_widths[index] == image->width &&
                output->graphics_heights[index] == image->height &&
                output->graphics_formats[index] == image->format) {
                /* API pixel frames deliberately retain image identity. Update
                   the existing GPU texture in place instead of destroying and
                   recreating it for every frame. */
                free_index = index;
                reuse_texture = 1;
                break;
            }
            if (output->graphics_textures[index] != NULL)
                SDL_DestroyTexture(output->graphics_textures[index]);
            output->graphics_textures[index] = NULL;
            free_index = index;
            break;
        }
        if (free_index < 0 && output->graphics_sources[index] == NULL)
            free_index = index;
    }
    if (free_index < 0) free_index = 0;
    if (!reuse_texture && output->graphics_sources[free_index] != NULL &&
        output->graphics_textures[free_index] != NULL)
        SDL_DestroyTexture(output->graphics_textures[free_index]);
    if (!reuse_texture)
        output->graphics_textures[free_index] = NULL;
    output->graphics_sources[free_index] = NULL;
    output->graphics_generations[free_index] = 0;

    source_bytes = image->format == 24 ? 3 : 4;
    pixel_count = (size_t)image->width * (size_t)image->height;
    rgba = (unsigned char *)malloc(pixel_count * 4U);
    if (rgba == NULL) return NULL;
    for (pixel_index = 0; (size_t)pixel_index < pixel_count; ++pixel_index) {
        rgba[pixel_index * 4U] = image->pixels[pixel_index * source_bytes];
        rgba[pixel_index * 4U + 1] =
            image->pixels[pixel_index * source_bytes + 1];
        rgba[pixel_index * 4U + 2] =
            image->pixels[pixel_index * source_bytes + 2];
        rgba[pixel_index * 4U + 3] = source_bytes == 4 ?
            image->pixels[pixel_index * source_bytes + 3] : 255;
    }
    texture = output->graphics_textures[free_index];
    if (texture == NULL) {
        texture = SDL_CreateTexture(output->renderer, SDL_PIXELFORMAT_RGBA32,
                                    SDL_TEXTUREACCESS_STATIC,
                                    image->width, image->height);
        if (texture == NULL) {
            fprintf(stderr, "blit: SDL graphics texture create failed: %s\n",
                    SDL_GetError());
            free(rgba);
            return NULL;
        }
        SDL_SetTextureBlendMode(texture, SDL_BLENDMODE_BLEND);
    }
    if (!SDL_UpdateTexture(texture, NULL, rgba, image->width * 4)) {
        fprintf(stderr, "blit: SDL graphics texture upload failed: %s\n",
                SDL_GetError());
        SDL_DestroyTexture(texture);
        free(rgba);
        output->graphics_textures[free_index] = NULL;
        return NULL;
    }
    free(rgba);
    output->graphics_sources[free_index] = image;
    output->graphics_generations[free_index] = image->generation;
    output->graphics_widths[free_index] = image->width;
    output->graphics_heights[free_index] = image->height;
    output->graphics_formats[free_index] = image->format;
    output->graphics_textures[free_index] = texture;
    output->last_graphics_upload_ms =
        (double)(monotonic_us() - upload_started_us) / 1000.0;
    if (output->last_graphics_upload_ms > output->max_graphics_upload_ms)
        output->max_graphics_upload_ms = output->last_graphics_upload_ms;
    ++output->graphics_texture_uploads;
    return texture;
}

static int render_sdl_graphics(BlitOutput *output,
                               const BlitGraphicsImage *images,
                               int image_count, float origin_x, float origin_y)
{
    SDL_Renderer *renderer = output != NULL ? output->renderer : NULL;
    int drawn[BLIT_GRAPHICS_MAX_IMAGES];
    int api_images_drawn = 0;
    int pass;
    memset(drawn, 0, sizeof(drawn));
    for (pass = 0; pass < image_count && pass < BLIT_GRAPHICS_MAX_IMAGES;
         ++pass) {
        int image_index;
        int best = -1;
        const BlitGraphicsImage *image;
        int width;
        int height;
        SDL_Texture *texture;
        SDL_FRect destination;

        for (image_index = 0; image_index < image_count &&
             image_index < BLIT_GRAPHICS_MAX_IMAGES; ++image_index)
            if (!drawn[image_index] && images[image_index].used &&
                images[image_index].complete && images[image_index].visible &&
                (best < 0 || images[image_index].z < images[best].z))
                best = image_index;
        if (best < 0) break;
        drawn[best] = 1;
        image = &images[best];
        if (!image->used || !image->complete || !image->visible ||
            image->pixels == NULL || image->width < 1 || image->height < 1)
            continue;
        width = image->placement_width > 0 ? image->placement_width : image->width;
        height = image->placement_height > 0 ? image->placement_height : image->height;
        if (width < 1 || height < 1) continue;
        texture = sdl_graphics_texture(output, image);
        if (texture == NULL) continue;
        destination.x = origin_x + image->x;
        destination.y = origin_y + image->y;
        destination.w = (float)width;
        destination.h = (float)height;
        SDL_RenderTexture(renderer, texture, NULL, &destination);
        if (strcmp(image->protocol, "api") == 0)
            ++api_images_drawn;
    }
    return api_images_drawn;
}

static void render_sdl_effects(const BlitRuntime *runtime,
                               SDL_Renderer *renderer, int width, int height)
{
    int y;
    unsigned char alpha;

    if (!runtime->effect.enabled ||
        strcmp(runtime->effect.preset, "scanlines") != 0)
        return;
    alpha = (unsigned char)((runtime->effect.intensity * 255) / 100);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    SDL_SetRenderDrawColor(renderer, 0, 0, 0, alpha);
    for (y = 1; y < height; y += 2)
        SDL_RenderLine(renderer, 0.0f, (float)y,
                       (float)(width - 1), (float)y);
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static int overlay_targets_output(const BlitRuntime *runtime,
                                  const BlitOverlay *overlay, int output_index)
{
    char display_id[128];
    snprintf(display_id, sizeof(display_id), "display-%llu",
             runtime->outputs[output_index].display_id);
    return strcmp(overlay->display_id, display_id) == 0;
}

static void render_sdl_overlays(const BlitRuntime *runtime,
                                SDL_Renderer *renderer, int output_index)
{
    int i;

    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_BLEND);
    for (i = 0; i < BLIT_MAX_OVERLAYS; ++i) {
        SDL_FRect rectangle;
        if (!runtime->overlays[i].used ||
            !overlay_targets_output(runtime, &runtime->overlays[i],
                                     output_index))
            continue;
        rectangle.x = (float)runtime->overlays[i].x;
        rectangle.y = (float)runtime->overlays[i].y;
        rectangle.w = (float)runtime->overlays[i].width;
        rectangle.h = (float)runtime->overlays[i].height;
        SDL_SetRenderDrawColor(renderer,
            (unsigned char)(runtime->overlays[i].color >> 16),
            (unsigned char)(runtime->overlays[i].color >> 8),
            (unsigned char)runtime->overlays[i].color,
            (unsigned char)((runtime->overlays[i].opacity * 255) / 100));
        SDL_RenderFillRect(renderer, &rectangle);
    }
    SDL_SetRenderDrawBlendMode(renderer, SDL_BLENDMODE_NONE);
}

static void capture_sdl_frame(BlitRuntime *runtime, int output_index,
                              SDL_Renderer *renderer)
{
    SDL_Surface *pixels;
    char path[512];
    if (runtime->sdl_screenshot_dir[0] == '\0' ||
        runtime->sdl_screenshot_written ||
        runtime->sdl_frame < runtime->sdl_screenshot_after)
        return;
    pixels = SDL_RenderReadPixels(renderer, NULL);
    if (pixels == NULL) {
        fprintf(stderr, "blit: SDL screenshot read failed: %s\n", SDL_GetError());
        runtime->sdl_screenshot_written = 1;
        return;
    }
    snprintf(path, sizeof(path), "%s/display-%llu.bmp",
             runtime->sdl_screenshot_dir,
             runtime->outputs[output_index].display_id);
    if (!SDL_SaveBMP(pixels, path))
        fprintf(stderr, "blit: SDL screenshot save failed: %s\n", SDL_GetError());
    else
        fprintf(stderr, "blit: SDL screenshot=%s\n", path);
    SDL_DestroySurface(pixels);
    if (output_index + 1 >= runtime->output_count)
        runtime->sdl_screenshot_written = 1;
}

static void capture_requested_screenshot(BlitRuntime *runtime,
                                         int output_index,
                                         SDL_Renderer *renderer)
{
    SDL_Surface *pixels;

    if (!(runtime->screenshot_requested_mask & (1U << output_index)))
        return;
    if (runtime->outputs[output_index].screenshot_surface != NULL) {
        SDL_DestroySurface(runtime->outputs[output_index].screenshot_surface);
        runtime->outputs[output_index].screenshot_surface = NULL;
    }
    pixels = SDL_RenderReadPixels(renderer, NULL);
    if (pixels == NULL) {
        fprintf(stderr, "blit: API screenshot read failed: %s\n",
                SDL_GetError());
        return;
    }
    runtime->outputs[output_index].screenshot_surface = pixels;
}

static int ensure_sdl_scene_texture(BlitOutput *output)
{
    int width;
    int height;

    if (output == NULL || output->renderer == NULL)
        return 0;
    if (output->scene_texture != NULL)
        return 1;
    if (!SDL_GetRenderOutputSize(output->renderer, &width, &height) ||
        width < 1 || height < 1)
        return 0;
    output->scene_texture = SDL_CreateTexture(
        output->renderer, SDL_PIXELFORMAT_RGBA8888,
        SDL_TEXTUREACCESS_TARGET, width, height);
    if (output->scene_texture == NULL) {
        fprintf(stderr, "blit: SDL scene cache unavailable: %s\n",
                SDL_GetError());
        return 0;
    }
    SDL_SetTextureBlendMode(output->scene_texture, SDL_BLENDMODE_NONE);
    return 1;
}

static void finish_sdl_output(BlitRuntime *runtime, int output_index,
                              int scene_target)
{
    SDL_Renderer *renderer = runtime->outputs[output_index].renderer;
    uint64_t present_started_us;
    double present_ms;

    if (scene_target) {
        SDL_SetRenderTarget(renderer, NULL);
        SDL_RenderTexture(renderer, runtime->outputs[output_index].scene_texture,
                          NULL, NULL);
    }
    render_pointer(runtime, output_index);
    capture_sdl_frame(runtime, output_index, renderer);
    capture_requested_screenshot(runtime, output_index, renderer);
    present_started_us = monotonic_us();
    SDL_RenderPresent(renderer);
    present_ms = (double)(monotonic_us() - present_started_us) / 1000.0;
    runtime->diagnostic_last_sdl_present_ms = present_ms;
    if (present_ms > runtime->diagnostic_max_sdl_present_ms)
        runtime->diagnostic_max_sdl_present_ms = present_ms;
    ++runtime->diagnostic_present_calls;
}

static void render_outputs(BlitRuntime *runtime, const BlitFramePlan *plan,
                           uint32_t output_mask)
{
    int output_index;
    int plan_index;
    int row;
    int cell_width = blit_font_width(&runtime->font);
    int cell_height = blit_font_height(&runtime->font);
    Uint64 render_started = 0;
    uint64_t diagnostic_render_started = monotonic_us();

    if (runtime->render_last_us != 0) {
        double gap = (double)(diagnostic_render_started -
                              runtime->render_last_us) / 1000.0;
        runtime->diagnostic_last_frame_gap_ms = gap;
        if (gap > runtime->diagnostic_max_frame_gap_ms)
            runtime->diagnostic_max_frame_gap_ms = gap;
    }
    ++runtime->diagnostic_frames;
    if (runtime->diagnostic_frame_rate_started_us == 0) {
        runtime->diagnostic_frame_rate_started_us = diagnostic_render_started;
        runtime->diagnostic_frame_rate_window_missed_start =
            diagnostic_missed_deadlines(runtime);
        runtime->diagnostic_frame_rate_window_max_render_ms = 0.0;
    }
    ++runtime->diagnostic_frame_rate_frames;

    if (!runtime->direct_kms) {
        ++runtime->sdl_frame;
        if (getenv("BLIT_SDL_TRACE_TIMING") != NULL)
            render_started = SDL_GetPerformanceCounter();
    }

    if (runtime->direct_kms) {
        BlitKmsSurfaceView views[BLIT_MAX_SESSIONS];
        BlitKmsOverlay kms_overlays[BLIT_MAX_OVERLAYS];
        int damage_consumed[BLIT_MAX_SESSIONS];
        int view_count = 0;
        int kms_overlay_count = 0;
        uint64_t kms_present_started;
        double kms_present_ms;
        int surface_index;
        int overlay_index;
        unsigned long long visual_revision = runtime->effect.revision ^
            runtime->next_overlay_id;
        (void)plan;
        memset(damage_consumed, 0, sizeof(damage_consumed));
        memset(kms_overlays, 0, sizeof(kms_overlays));
        for (overlay_index = 0; overlay_index < BLIT_MAX_OVERLAYS;
             ++overlay_index) {
            BlitOverlay *overlay = &runtime->overlays[overlay_index];
            if (!overlay->used || kms_overlay_count >= BLIT_MAX_OVERLAYS)
                continue;
            kms_overlays[kms_overlay_count].display_id = strtoull(
                overlay->display_id +
                    (strncmp(overlay->display_id, "display-", 8) == 0 ? 8 : 0),
                NULL, 10);
            kms_overlays[kms_overlay_count].x = overlay->x;
            kms_overlays[kms_overlay_count].y = overlay->y;
            kms_overlays[kms_overlay_count].width = overlay->width;
            kms_overlays[kms_overlay_count].height = overlay->height;
            kms_overlays[kms_overlay_count].color = overlay->color;
            kms_overlays[kms_overlay_count].opacity = overlay->opacity;
            visual_revision ^= overlay->id * 11400714819323198485ULL;
            visual_revision ^= overlay->revision +
                ((unsigned long long)overlay->opacity << 32);
            ++kms_overlay_count;
        }
        for (surface_index = 0; surface_index < BLIT_MAX_SESSIONS &&
             view_count < BLIT_MAX_SESSIONS; ++surface_index) {
            BlitSurface *surface = runtime->surfaces[surface_index];
            BlitSession *session;
            BlitPane *pane;
            int output_index_for_surface = 0;
            if (surface == NULL || !surface->used) continue;
            session = find_session_id(runtime, surface->owner_session_id);
            if (session == NULL) continue;
            /* Synchronized output retains its last complete frame.  Do not
               submit its partial damage, but continue presenting unrelated
               outputs in the same server. */
            if (session->terminal_synchronized)
                continue;
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
            views[view_count].damage_consumed =
                &damage_consumed[view_count];
            views[view_count].graphics = session->graphics;
            views[view_count].graphics_count = BLIT_GRAPHICS_MAX_IMAGES;
            ++view_count;
        }
        kms_present_started = monotonic_us();
        if (blit_kms_present(runtime->kms, views, view_count,
                             runtime->pointer_x, runtime->pointer_y,
                             runtime->pointer_valid, output_mask,
                             kms_overlays, kms_overlay_count,
                             visual_revision,
                             runtime->effect.enabled &&
                                 strcmp(runtime->effect.preset, "scanlines") == 0,
                             runtime->effect.intensity) < 0)
            fprintf(stderr, "blit: direct KMS present failed\n");
        kms_present_ms = (double)(monotonic_us() - kms_present_started) /
            1000.0;
        runtime->diagnostic_last_kms_present_ms = kms_present_ms;
        if (kms_present_ms > runtime->diagnostic_max_kms_present_ms)
            runtime->diagnostic_max_kms_present_ms = kms_present_ms;
        for (surface_index = 0; surface_index < view_count; ++surface_index)
            if (damage_consumed[surface_index]) {
                BlitSurface *surface = find_surface_id(
                    runtime, views[surface_index].surface_id);
                if (surface != NULL)
                    surface->damage_valid = 0;
            }
        for (surface_index = 0; surface_index < runtime->output_count;
             ++surface_index)
            if ((output_mask & (1U << surface_index)) &&
                runtime->pixel_frames_pending[surface_index] > 0)
                --runtime->pixel_frames_pending[surface_index];
        runtime->pointer_dirty = 0;
        return;
    }

    for (output_index = 0; output_index < runtime->output_count;
         ++output_index) {
        int target_count = 0;
        int synchronized_count = 0;
        int overlay_count = 0;
        int target_changed = 0;
        int external_target_changed = 0;
        int graphics_changed;
        int full_redraw;
        int scene_target = 0;
        int pointer_only;
        int api_images_drawn = 0;
        SDL_Renderer *renderer = runtime->outputs[output_index].renderer;

        if (!(output_mask & (1U << output_index)))
            continue;

        for (plan_index = 0; plan_index < plan->surface_count;
             ++plan_index) {
            BlitSurface *surface = find_surface_id(
                runtime, plan->surfaces[plan_index].surface_id);

            if (surface == NULL || !surface->used)
                continue;
            if (surface_targets_output(runtime, surface, output_index)) {
                BlitSession *session = find_session_id(
                    runtime, surface->owner_session_id);
                if (session != NULL && session->terminal_synchronized) {
                    ++synchronized_count;
                    continue;
                }
                ++target_count;
                if (surface->damage_valid) {
                    target_changed = 1;
                    if (session != NULL &&
                        session->surface_mode == BLIT_SURFACE_EXTERNAL)
                        external_target_changed = 1;
                }
            }
        }
        if (runtime->pixel_frames_pending[output_index] > 0)
            target_changed = 1;
        graphics_changed = runtime->outputs[output_index].initialized == 0 ||
            runtime->outputs[output_index].last_graphics_signature !=
                graphics_scene_signature(runtime, output_index);
        {
            int overlay_index;
            for (overlay_index = 0; overlay_index < BLIT_MAX_OVERLAYS;
                 ++overlay_index)
                if (runtime->overlays[overlay_index].used &&
                    overlay_targets_output(runtime,
                        &runtime->overlays[overlay_index], output_index))
                    ++overlay_count;
        }
        pointer_only = runtime->pointer_dirty &&
            runtime->outputs[output_index].initialized &&
            runtime->outputs[output_index].scene_texture != NULL &&
            !target_changed;
        if (pointer_only) {
            ++runtime->diagnostic_pointer_cache_hits;
            {
                char detail[160];
                snprintf(detail, sizeof(detail),
                         "output=%d path=pointer-cache target_changed=%d",
                         output_index, target_changed);
                diagnostic_trace(runtime, "render_path", detail);
            }
            SDL_SetRenderTarget(renderer, NULL);
            SDL_RenderTexture(renderer,
                              runtime->outputs[output_index].scene_texture,
                              NULL, NULL);
            finish_sdl_output(runtime, output_index, 0);
            continue;
        }
        full_redraw = !runtime->outputs[output_index].initialized ||
            graphics_changed ||
            target_count != runtime->outputs[output_index].last_target_count ||
            runtime->topology_revision !=
                runtime->outputs[output_index].last_topology_revision;
        if (runtime->pointer_dirty)
            ++runtime->diagnostic_pointer_cache_misses;
        {
            char detail[256];
            snprintf(detail, sizeof(detail),
                     "output=%d path=%s target_count=%d active_surfaces=%d target_changed=%d external_changed=%d initialized=%d graphics_changed=%d graphics_images=%d last_graphics_signature=%llu graphics_signature=%llu",
                     output_index, full_redraw ? "full-redraw" :
                         "incremental-damage",
                     target_count, active_output_surface_count(runtime, output_index),
                     target_changed, external_target_changed,
                     runtime->outputs[output_index].initialized,
                     graphics_changed,
                     graphics_scene_count(runtime, output_index),
                     runtime->outputs[output_index].last_graphics_signature,
                     graphics_scene_signature(runtime, output_index));
            diagnostic_trace(runtime, "render_path", detail);
        }
        if (target_count == 0 && overlay_count == 0 &&
            synchronized_count > 0) {
            /* Leave the synchronized output untouched until its owner sends
               the matching CSI ?2026 l.  In particular, do not clear it
               merely because another output caused a render pass. */
            continue;
        }
        scene_target = ensure_sdl_scene_texture(
            &runtime->outputs[output_index]);
        if (scene_target)
            SDL_SetRenderTarget(renderer,
                                runtime->outputs[output_index].scene_texture);
        if (target_count == 0 && overlay_count == 0) {
            /* The output is already in its empty state.  A pending render
               can be caused by another display's PTY or by a stream event;
               do not submit another clear/present for this output. */
            if (!runtime->outputs[output_index].initialized &&
                !runtime->pointer_dirty &&
                !(runtime->screenshot_requested_mask & (1U << output_index)))
                continue;
            if (!runtime->outputs[output_index].initialized) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
            } else if (runtime->pointer_dirty) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
            }
            finish_sdl_output(runtime, output_index, scene_target);
            /* A later session needs a full first frame, even if this output
               was previously presenting the empty desktop. */
            runtime->outputs[output_index].initialized = 0;
            continue;
        }
        if (target_count == 0) {
            if (!runtime->outputs[output_index].initialized)
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            if (!runtime->outputs[output_index].initialized)
                SDL_RenderClear(renderer);
            else if (runtime->pointer_dirty) {
                SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                SDL_RenderClear(renderer);
            }
            render_sdl_effects(runtime, renderer,
                               runtime->displays[display_index_for_output(
                                   runtime, output_index)].bounds.w,
                               runtime->displays[display_index_for_output(
                                   runtime, output_index)].bounds.h);
            render_sdl_overlays(runtime, renderer, output_index);
            finish_sdl_output(runtime, output_index, scene_target);
            runtime->outputs[output_index].initialized = 1;
            runtime->outputs[output_index].last_topology_revision =
                runtime->topology_revision;
            runtime->outputs[output_index].last_target_count = target_count;
            continue;
        }
        /* A surface can be present without having changed since the last
           presentation.  Avoid walking and rasterizing its entire grid, and
           avoid SDL_RenderPresent, until damage or pointer state requires a
           new frame. */
        if (runtime->outputs[output_index].initialized &&
            !runtime->pointer_dirty && !target_changed)
            continue;
        if (!runtime->outputs[output_index].initialized) {
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
        } else if (runtime->pointer_dirty) {
            /* The old cursor is part of the already-present scene. A black
               rectangle would destroy wallpaper and terminal cells. Start
               from a clean scene and redraw the complete output instead. */
            SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
            SDL_RenderClear(renderer);
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
            if (session->terminal_synchronized)
                continue;
            pane = find_pane_id(runtime, session->pane_id);
            if (pane != NULL) {
                surface_origin_x = (float)(pane->x * cell_width + 4);
                surface_origin_y = pane->y * cell_height + 4;
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
                        surface_origin_y += prior->height * cell_height + 4;
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
                        plan->surfaces[plan_index].damage_x * (float)cell_width;
                    damage.y = (float)surface_origin_y + first_row * (float)cell_height;
                    damage.w = plan->surfaces[plan_index].damage_width * (float)cell_width;
                    damage.h = plan->surfaces[plan_index].damage_height * (float)cell_height;
                    SDL_SetRenderDrawColor(renderer, 0, 0, 0, 255);
                    SDL_RenderFillRect(renderer, &damage);
                    SDL_SetRenderDrawColor(renderer, 190, 210, 190, 255);
                }
            }
            last_row = full_redraw ? surface->height : last_row;
            if (last_row > BLIT_MAX_CELL_ROWS)
                last_row = BLIT_MAX_CELL_ROWS;

            /* Wallpaper themes commonly express nearly every terminal cell
               as a coloured background.  Submitting one SDL rectangle per
               cell makes a full-screen Hibr redraw CPU-bound.  Build the
               opaque background quads first, then draw the glyph layer on
               top, preserving the canonical cell ordering while reducing
               thousands of renderer calls to one geometry submission. */
            for (row = first_row; row < last_row; ++row) {
                column = 0;
                while (column < surface->width &&
                       column < BLIT_MAX_CELL_COLS) {
                    uint32_t foreground = surface->foreground[row][column] ?
                        surface->foreground[row][column] - 1 :
                        BLIT_DEFAULT_FOREGROUND;
                    uint32_t background = surface->background[row][column] ?
                        surface->background[row][column] - 1 : 0;
                    uint16_t attributes = surface->attributes[row][column];
                    int run_start;
                    int run_end;
                    if (attributes & BLIT_ATTR_INVERSE) {
                        uint32_t swapped = foreground;
                        foreground = background ? background : 0;
                        background = swapped;
                    }
                    if (background == 0) {
                        ++column;
                        continue;
                    }
                    run_start = column;
                    run_end = column + 1;
                    while (run_end < surface->width &&
                           run_end < BLIT_MAX_CELL_COLS) {
                        uint32_t next_foreground =
                            surface->foreground[row][run_end] ?
                            surface->foreground[row][run_end] - 1 :
                            BLIT_DEFAULT_FOREGROUND;
                        uint32_t next_background =
                            surface->background[row][run_end] ?
                            surface->background[row][run_end] - 1 : 0;
                        uint16_t next_attributes =
                            surface->attributes[row][run_end];
                        if (next_attributes & BLIT_ATTR_INVERSE) {
                            uint32_t swapped = next_foreground;
                            next_foreground = next_background ?
                                next_background : 0;
                            next_background = swapped;
                        }
                        if (next_background != background)
                            break;
                        ++run_end;
                    }
                    if (!queue_sdl_background(
                            &runtime->outputs[output_index], background,
                            surface_origin_x + run_start * (float)cell_width,
                            (float)surface_origin_y + row * (float)cell_height,
                            (float)(run_end - run_start) * cell_width,
                            (float)cell_height)) {
                        flush_sdl_background_batch(
                            &runtime->outputs[output_index]);
                        SDL_SetRenderDrawColor(renderer,
                            (unsigned char)(background >> 16),
                            (unsigned char)(background >> 8),
                            (unsigned char)background, 255);
                        SDL_RenderFillRect(renderer, &(SDL_FRect){
                            surface_origin_x + run_start * (float)cell_width,
                            (float)surface_origin_y + row * (float)cell_height,
                            (float)(run_end - run_start) * cell_width,
                            (float)cell_height});
                    }
                    column = run_end;
                }
            }
            flush_sdl_background_batch(&runtime->outputs[output_index]);
            for (row = first_row; row < last_row; ++row) {
                for (column = 0; column < surface->width &&
                     column < BLIT_MAX_CELL_COLS; ++column) {
                    uint32_t foreground = surface->foreground[row][column] ?
                        surface->foreground[row][column] - 1 :
                        BLIT_DEFAULT_FOREGROUND;
                    uint32_t background = surface->background[row][column] ?
                        surface->background[row][column] - 1 : 0;
                    uint16_t attributes = surface->attributes[row][column];

                    if (attributes & BLIT_ATTR_INVERSE) {
                        uint32_t swapped = foreground;
                        foreground = background ? background : 0;
                        background = swapped;
                    }
                    if (attributes & BLIT_ATTR_DIM)
                        foreground = ((foreground & 0xfefefeU) >> 1);
                    {
                        int glyph_width = (attributes & BLIT_ATTR_WIDE) ?
                            cell_width * 2 : cell_width;
                        uint32_t underline_color =
                            surface->underline_color[row][column] ?
                            surface->underline_color[row][column] - 1 :
                            foreground;
                    SDL_SetRenderDrawColor(renderer,
                        (unsigned char)(foreground >> 16),
                        (unsigned char)(foreground >> 8),
                        (unsigned char)foreground, 255);
                    if (!(attributes & BLIT_ATTR_CONCEAL)) {
                        if (!(attributes & BLIT_ATTR_CONTINUATION) &&
                            surface->codepoints[row][column] != 0 &&
                            surface->codepoints[row][column] != ' ')
                            render_or_queue_sdl_glyph(
                                &runtime->outputs[output_index], &runtime->font,
                                foreground, surface->codepoints[row][column],
                                surface_origin_x + column * (float)cell_width,
                                (float)surface_origin_y + row * (float)cell_height,
                                (float)glyph_width, (float)cell_height);
                        if (surface->combining_count[row][column] > 0) {
                            int combining_index;
                            for (combining_index = 0;
                                 combining_index < surface->combining_count[row][column] &&
                                 combining_index < BLIT_MAX_COMBINING;
                                 ++combining_index) {
                                render_or_queue_sdl_glyph(
                                    &runtime->outputs[output_index],
                                    &runtime->font,
                                    foreground,
                                    surface->combining[row][column][combining_index],
                                    surface_origin_x + column * (float)cell_width,
                                    (float)surface_origin_y + row * (float)cell_height,
                                    (float)glyph_width, (float)cell_height);
                            }
                        }
                        if (attributes & BLIT_ATTR_BOLD &&
                            surface->codepoints[row][column] != 0 &&
                            surface->codepoints[row][column] != ' ')
                            render_or_queue_sdl_glyph(
                                &runtime->outputs[output_index], &runtime->font,
                                foreground, surface->codepoints[row][column],
                                surface_origin_x + 1.0f + column * (float)cell_width,
                                (float)surface_origin_y + row * (float)cell_height,
                                (float)glyph_width, (float)cell_height);
                        if (attributes & BLIT_ATTR_ITALIC &&
                            surface->codepoints[row][column] != 0 &&
                            surface->codepoints[row][column] != ' ')
                            render_or_queue_sdl_glyph(
                                &runtime->outputs[output_index], &runtime->font,
                                foreground, surface->codepoints[row][column],
                                surface_origin_x + 1.0f + column * (float)cell_width,
                                (float)surface_origin_y + row * (float)cell_height,
                                (float)glyph_width, (float)cell_height);
                        if (attributes & (BLIT_ATTR_UNDERLINE | BLIT_ATTR_STRIKE))
                            flush_sdl_glyph_batch(&runtime->outputs[output_index]);
                        if (attributes & BLIT_ATTR_UNDERLINE)
                            SDL_SetRenderDrawColor(renderer,
                                (unsigned char)(underline_color >> 16),
                                (unsigned char)(underline_color >> 8),
                                (unsigned char)underline_color, 255);
                        if (attributes & BLIT_ATTR_UNDERLINE)
                            SDL_RenderLine(renderer, surface_origin_x + column * (float)cell_width,
                                (float)surface_origin_y + row * (float)cell_height + cell_height - 1.0f,
                                surface_origin_x + column * (float)cell_width + glyph_width - 1.0f,
                                (float)surface_origin_y + row * (float)cell_height + cell_height - 1.0f);
                        if (attributes & BLIT_ATTR_STRIKE)
                            SDL_RenderLine(renderer, surface_origin_x + column * (float)cell_width,
                                (float)surface_origin_y + row * (float)cell_height + cell_height / 2.0f,
                                surface_origin_x + column * (float)cell_width + glyph_width - 1.0f,
                                (float)surface_origin_y + row * (float)cell_height + cell_height / 2.0f);
                    }
                    }
                }
            }
            flush_sdl_glyph_batch(&runtime->outputs[output_index]);
            if (surface->cursor_visible &&
                surface->cursor_y >= first_row && surface->cursor_y < last_row) {
                float cursor_x = surface_origin_x + surface->cursor_x * (float)cell_width;
                float cursor_y = (float)surface_origin_y + surface->cursor_y * (float)cell_height;
                SDL_SetRenderDrawColor(renderer,
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 16),
                    (unsigned char)(BLIT_DEFAULT_FOREGROUND >> 8),
                    (unsigned char)BLIT_DEFAULT_FOREGROUND, 255);
                if (surface->cursor_style == 3 || surface->cursor_style == 4)
                    SDL_RenderLine(renderer, cursor_x, cursor_y + cell_height - 1.0f,
                                   cursor_x + cell_width - 1.0f, cursor_y + cell_height - 1.0f);
                else if (surface->cursor_style == 5 || surface->cursor_style == 6)
                    SDL_RenderLine(renderer, cursor_x, cursor_y,
                                   cursor_x, cursor_y + cell_height - 1.0f);
                else {
                    SDL_FRect cursor_rect;
                    cursor_rect.x = cursor_x;
                    cursor_rect.y = cursor_y;
                    cursor_rect.w = (float)cell_width;
                    cursor_rect.h = (float)cell_height;
                    SDL_RenderFillRect(renderer, &cursor_rect);
                }
            }
            {
                SDL_Rect graphics_clip;
                graphics_clip.x = (int)surface_origin_x;
                graphics_clip.y = surface_origin_y;
                graphics_clip.w = surface->width * cell_width;
                graphics_clip.h = surface->height * cell_height;
                SDL_SetRenderClipRect(renderer, &graphics_clip);
                api_images_drawn += render_sdl_graphics(
                    &runtime->outputs[output_index], session->graphics,
                    BLIT_GRAPHICS_MAX_IMAGES, surface_origin_x,
                    (float)surface_origin_y);
                SDL_SetRenderClipRect(renderer, NULL);
            }
        }
        render_sdl_effects(runtime, renderer,
                           runtime->displays[display_index_for_output(
                               runtime, output_index)].bounds.w,
                           runtime->displays[display_index_for_output(
                               runtime, output_index)].bounds.h);
        render_sdl_overlays(runtime, renderer, output_index);
        finish_sdl_output(runtime, output_index, scene_target);
        if (api_images_drawn > 0 &&
            runtime->pixel_frames_pending[output_index] > 0)
            --runtime->pixel_frames_pending[output_index];
        runtime->outputs[output_index].initialized = 1;
        runtime->outputs[output_index].last_topology_revision =
            runtime->topology_revision;
        runtime->outputs[output_index].last_target_count = target_count;
        runtime->outputs[output_index].last_graphics_signature =
            graphics_scene_signature(runtime, output_index);
    }
    if (render_started != 0) {
        Uint64 elapsed = SDL_GetPerformanceCounter() - render_started;
        double milliseconds = (double)elapsed * 1000.0 /
            (double)SDL_GetPerformanceFrequency();
        runtime->diagnostic_last_render_ms = milliseconds;
        if (milliseconds > runtime->diagnostic_max_render_ms)
            runtime->diagnostic_max_render_ms = milliseconds;
        fprintf(stderr, "blit: SDL frame=%lu render_ms=%.3f\n",
                runtime->sdl_frame, milliseconds);
    }
    for (plan_index = 0; plan_index < plan->surface_count; ++plan_index) {
        BlitSurface *surface = find_surface_id(
            runtime, plan->surfaces[plan_index].surface_id);
        BlitSession *session = surface != NULL ? find_session_id(
            runtime, surface->owner_session_id) : NULL;
        int rendered = 0;
        int output_index;
        if (surface != NULL &&
            (session == NULL || !session->terminal_synchronized)) {
            for (output_index = 0; output_index < runtime->output_count;
                 ++output_index)
                if ((output_mask & (1U << output_index)) &&
                    surface_targets_output(runtime, surface, output_index)) {
                    rendered = 1;
                    break;
                }
        }
        if (rendered)
            surface->damage_valid = 0;
        if (rendered && session != NULL)
            session->terminal_frame_batch = 0;
    }
    runtime->pointer_dirty = 0;
    {
        uint64_t elapsed = monotonic_us() - diagnostic_render_started;
        double milliseconds = (double)elapsed / 1000.0;
        runtime->diagnostic_last_render_ms = milliseconds;
        if (milliseconds > runtime->diagnostic_max_render_ms)
            runtime->diagnostic_max_render_ms = milliseconds;
        if (milliseconds > runtime->diagnostic_frame_rate_window_max_render_ms)
            runtime->diagnostic_frame_rate_window_max_render_ms = milliseconds;
    }
    runtime->diagnostic_frame_rate_last_us = monotonic_us();
    if (runtime->diagnostic_frame_rate_last_us >=
            runtime->diagnostic_frame_rate_started_us &&
        runtime->diagnostic_frame_rate_last_us -
            runtime->diagnostic_frame_rate_started_us >= 500000ULL) {
        uint64_t elapsed = runtime->diagnostic_frame_rate_last_us -
            runtime->diagnostic_frame_rate_started_us;
        unsigned long long missed = diagnostic_missed_deadlines(runtime);
        runtime->diagnostic_frame_rate_hz =
            (double)runtime->diagnostic_frame_rate_frames * 1000000.0 /
            (double)elapsed;
        runtime->diagnostic_frame_rate_window_ms = elapsed / 1000ULL;
        runtime->diagnostic_frame_rate_last_frames =
            runtime->diagnostic_frame_rate_frames;
        runtime->diagnostic_frame_rate_max_render_ms =
            runtime->diagnostic_frame_rate_window_max_render_ms;
        runtime->diagnostic_frame_rate_missed_deadlines =
            missed >= runtime->diagnostic_frame_rate_window_missed_start ?
            missed - runtime->diagnostic_frame_rate_window_missed_start : 0;
        ++runtime->diagnostic_frame_rate_window_sequence;
        runtime->diagnostic_frame_rate_started_us =
            runtime->diagnostic_frame_rate_last_us;
        runtime->diagnostic_frame_rate_frames = 0;
        runtime->diagnostic_frame_rate_window_missed_start = missed;
        runtime->diagnostic_frame_rate_window_max_render_ms = 0.0;
    }
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
    {
        int display_index = display_index_for_output(runtime, output_index);
        if (display_index < 0)
            return NULL;
        pixel_x -= runtime->displays[display_index].bounds.x;
        pixel_y -= runtime->displays[display_index].bounds.y;
    }
    pixel_x /= runtime->font_scale;
    pixel_y /= runtime->font_scale;
    cell_width = runtime->direct_kms ? blit_kms_cell_width(runtime->kms) :
        blit_font_width(&runtime->font);
    cell_height = runtime->direct_kms ? blit_kms_cell_height(runtime->kms) :
        blit_font_height(&runtime->font);
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
    /* A display-attached session may intentionally have no Blit pane.  This
       is the API shape used by the primary Hibr desktop: it is rendered at
       the first available stacked surface, but the old hit test only knew
       about panes and silently discarded every button report. */
    {
        int stack_y = 0;
        int session_index;
        for (session_index = 0; session_index < BLIT_MAX_SESSIONS;
             ++session_index) {
            BlitSession *session = &runtime->sessions[session_index];
            BlitSurface *surface;
            int surface_x = 4;
            int surface_y = stack_y + 4;
            if (!session->used || session->pane_id[0] != '\0')
                continue;
            if (session->display_id[0] == '\0') {
                if (output_index != 0)
                    continue;
            } else if (strcmp(session->display_id, display_id) != 0) {
                continue;
            }
            surface = find_surface_id(runtime, session->id);
            if (surface == NULL || !surface->used)
                continue;
            if (pixel_x >= surface_x && pixel_y >= surface_y &&
                pixel_x < surface_x + surface->width * cell_width &&
                pixel_y < surface_y + surface->height * cell_height) {
                *column = (int)((pixel_x - surface_x) /
                                (float)cell_width) + 1;
                *row = (int)((pixel_y - surface_y) /
                             (float)cell_height) + 1;
                return session;
            }
            stack_y += surface->height * cell_height + 4;
        }
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

static int mouse_modifier_bits(const BlitRuntime *runtime)
{
    int modifiers = 0;
    if (runtime->evdev_shift) modifiers |= 4;
    if (runtime->evdev_alt) modifiers |= 8;
    if (runtime->evdev_ctrl) modifiers |= 16;
    return modifiers;
}

static void terminal_scrollback_wheel(BlitRuntime *runtime,
                                      BlitSession *session, int button)
{
    BlitSurface *surface;
    int view;
    char event[512];

    if (runtime == NULL || session == NULL || session->terminal_alternate ||
        (button != 64 && button != 65))
        return;
    surface = find_surface_id(runtime, session->id);
    if (surface == NULL || surface->scrollback_count == 0)
        return;
    view = surface->scrollback_view;
    view += button == 64 ? 3 : -3;
    if (view < 0) view = 0;
    if (view > surface->scrollback_count)
        view = surface->scrollback_count;
    terminal_scrollback_show_offset(surface, view);
    ++surface->revision;
    surface_delta_json(surface, event, sizeof(event));
    broadcast_stream(runtime, event, BLIT_STREAM_SURFACES);
    runtime->render_pending = 1;
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
    if (session->terminal_mouse_mode == 0) {
        if (!release && (button == 64 || button == 65))
            terminal_scrollback_wheel(runtime, session, button);
        else
            terminal_scrollback_show_live(find_surface_id(runtime, session->id));
        return;
    }
    code = button + mouse_modifier_bits(runtime);
    if (session->terminal_mouse_sgr) {
        /* SGR mouse reports use button numbers 0..2 for press/release,
           32+button for motion, and 64+button for wheels.  The legacy
           encoding adds its protocol offset in the byte below; neither
           format uses the current button bitmask as a modifier. */
        snprintf(report, sizeof(report), "\033[<%d;%d;%d%c", code, column,
                 row, release ? 'm' : 'M');
    } else {
        /* Legacy X10/VT reports encode release as button 3, followed by
           the mandatory 32 offset.  Wheel codes already include their
           64 offset and must not be offset a second time. */
        if (release && button < 64)
            code = 3 + mouse_modifier_bits(runtime);
        if (code < 64)
            code += 32;
        snprintf(report, sizeof(report), "\033[M%c%c%c", code,
                 column + 32, row + 32);
    }
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
    button += mouse_modifier_bits(runtime);
    if (session->terminal_mouse_sgr)
        snprintf(report, sizeof(report), "\033[<%d;%d;%dM", button + 32,
                 column, row);
    else
        snprintf(report, sizeof(report), "\033[M%c%c%c", button + 64,
                 column + 32, row + 32);
    write(session->pty_fd, report, strlen(report));
}

static void update_pointer_position(BlitRuntime *runtime,
                                    const char *device_id)
{
    float old_x;
    float old_y;
    int old_output;
    int old_valid;
    int changed;
    if (runtime->output_count <= 0)
        return;
    old_x = runtime->pointer_x;
    old_y = runtime->pointer_y;
    old_output = runtime->pointer_output;
    old_valid = runtime->pointer_valid;
    if (runtime->evdev_mouse_x < 0) runtime->evdev_mouse_x = 0;
    if (runtime->evdev_mouse_y < 0) runtime->evdev_mouse_y = 0;
    {
        int min_x = runtime->displays[0].bounds.x;
        int min_y = runtime->displays[0].bounds.y;
        int max_x = min_x + runtime->displays[0].bounds.w;
        int max_y = min_y + runtime->displays[0].bounds.h;
        int display_index;
        for (display_index = 1; display_index < runtime->display_count;
             ++display_index) {
            const SDL_Rect *bounds = &runtime->displays[display_index].bounds;
            if (bounds->x < min_x) min_x = bounds->x;
            if (bounds->y < min_y) min_y = bounds->y;
            if (bounds->x + bounds->w > max_x) max_x = bounds->x + bounds->w;
            if (bounds->y + bounds->h > max_y) max_y = bounds->y + bounds->h;
        }
        if (runtime->evdev_mouse_x < min_x)
            runtime->evdev_mouse_x = (float)min_x;
        if (runtime->evdev_mouse_y < min_y)
            runtime->evdev_mouse_y = (float)min_y;
        if (runtime->evdev_mouse_x >= max_x)
            runtime->evdev_mouse_x = (float)(max_x - 1);
        if (runtime->evdev_mouse_y >= max_y)
            runtime->evdev_mouse_y = (float)(max_y - 1);
    }
    runtime->previous_pointer_x = runtime->pointer_x;
    runtime->previous_pointer_y = runtime->pointer_y;
    runtime->previous_pointer_output = runtime->pointer_output;
    runtime->pointer_valid = 1;
    runtime->pointer_output = output_index_for_display_index(runtime,
        display_index_at(runtime, runtime->evdev_mouse_x,
                         runtime->evdev_mouse_y));
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
        {
            char detail[160];
            snprintf(detail, sizeof(detail), "x=%.1f y=%.1f output=%d device=%s",
                     runtime->pointer_x, runtime->pointer_y,
                     runtime->pointer_output,
                     device_id != NULL ? device_id : "evdev-pointer");
            diagnostic_trace(runtime, "pointer_motion", detail);
        }
        snprintf(input_event, sizeof(input_event),
                 "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                 "\"device_id\":\"%s\",\"event\":{"
                 "\"kind\":\"pointer_motion\",\"time_us\":%llu,\"x\":%.1f,"
                 "\"y\":%.1f,\"output\":%d,\"column\":%d,\"row\":%d}}",
                 target != NULL ? target->id : 0ULL,
                 device_id != NULL ? device_id : "evdev-pointer",
                 (unsigned long long)monotonic_us(),
                 runtime->pointer_x, runtime->pointer_y,
                 runtime->pointer_output, target_col, target_row);
        broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
    }
    if (runtime->output_count > 0 && !runtime->direct_kms &&
        runtime->pointer_output >= 0 &&
        runtime->pointer_output < runtime->output_count) {
        int display_index = display_index_for_output(runtime,
                                                      runtime->pointer_output);
        const SDL_Rect *bounds;
        if (display_index < 0)
            return;
        bounds = &runtime->displays[display_index].bounds;
        SDL_WarpMouseInWindow(runtime->outputs[runtime->pointer_output].window,
                              runtime->evdev_mouse_x - bounds->x,
                              runtime->evdev_mouse_y - bounds->y);
    }
    send_mouse_motion_report(runtime, 0, runtime->evdev_mouse_x,
                             runtime->evdev_mouse_y);
}

static void update_evdev_pointer(BlitRuntime *runtime)
{
    if (runtime->evdev_mouse_count <= 0)
        return;
    update_pointer_position(runtime, "evdev-pointer");
}

static void broadcast_evdev_pointer(BlitRuntime *runtime, const char *kind,
                                    int button, int pressed, int delta,
                                    const char *device_id)
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
                 "\"device_id\":\"%s\",\"event\":{"
                 "\"kind\":\"pointer_wheel\",\"time_us\":%llu,\"delta\":%d,"
                 "\"x\":%.1f,\"y\":%.1f,\"output\":%d}}",
                 target != NULL ? target->id : 0ULL,
                 device_id != NULL ? device_id : "evdev-pointer",
                 (unsigned long long)monotonic_us(), delta,
                 runtime->pointer_x, runtime->pointer_y,
                 runtime->pointer_output);
    } else {
        snprintf(input_event, sizeof(input_event),
                 "{\"type\":\"input\",\"session_id\":\"session-%llu\","
                 "\"device_id\":\"%s\",\"event\":{"
                 "\"kind\":\"pointer_button\",\"time_us\":%llu,\"button\":%d,"
                 "\"pressed\":%s,\"x\":%.1f,\"y\":%.1f,"
                 "\"output\":%d,\"column\":%d,\"row\":%d}}",
                 target != NULL ? target->id : 0ULL,
                 device_id != NULL ? device_id : "evdev-pointer",
                 (unsigned long long)monotonic_us(), button,
                 pressed ? "true" : "false", runtime->pointer_x,
                 runtime->pointer_y, runtime->pointer_output, column, row);
    }
    broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
}

static void dispatch_pointer_button(BlitRuntime *runtime, int button,
                                    int pressed, const char *device_id)
{
    char detail[160];
    if (button < 0 || button > 2)
        return;
    snprintf(detail, sizeof(detail),
             "button=%d pressed=%d x=%.1f y=%.1f",
             button, pressed, runtime->pointer_x, runtime->pointer_y);
    diagnostic_trace(runtime, "pointer_button", detail);
    if (pressed)
        runtime->mouse_buttons |= 1U << button;
    broadcast_evdev_pointer(runtime, "pointer_button", button, pressed, 0,
                             device_id);
    send_mouse_report(runtime, 0, runtime->pointer_x, runtime->pointer_y,
                      button, !pressed);
    if (!pressed)
        runtime->mouse_buttons &= ~(1U << button);
}

static void broadcast_evdev_gamepad(BlitRuntime *runtime,
                                    const struct input_event *event)
{
    char input_event[384];
    BlitSession *target = input_route_target(runtime, "evdev-gamepad");
    const char *kind = event->type == EV_KEY ? "gamepad_button" :
        (event->type == EV_ABS ? "gamepad_axis" : "gamepad_event");
    snprintf(input_event, sizeof(input_event),
             "{\"type\":\"input\",\"session_id\":\"session-%llu\","
             "\"device_id\":\"evdev-gamepad\",\"event\":{"
             "\"kind\":\"%s\",\"code\":%u,\"value\":%d,"
             "\"time_us\":%llu}}",
             target != NULL ? target->id : 0ULL, kind, event->code,
             event->value, (unsigned long long)monotonic_us());
    broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
}

static void service_evdev_gamepad(BlitRuntime *runtime)
{
    struct input_event event;
    ssize_t received;
    int device_index;

    for (device_index = 0; device_index < runtime->evdev_gamepad_count;
         ++device_index) {
        int device_lost = 0;
        if (runtime->evdev_gamepad_fds[device_index] < 0) continue;
        while ((received = read(runtime->evdev_gamepad_fds[device_index],
                                &event, sizeof(event))) ==
               (ssize_t)sizeof(event)) {
            uint64_t input_started_us = monotonic_us();
            if (!runtime->evdev_gamepad_reported[device_index]) {
                fprintf(stderr, "blit: evdev gamepad events received from %s\n",
                        runtime->evdev_gamepad_paths[device_index]);
                runtime->evdev_gamepad_reported[device_index] = 1;
            }
            if (event.type == EV_KEY || event.type == EV_ABS) {
                broadcast_evdev_gamepad(runtime, &event);
                record_input_dispatch(runtime, input_started_us);
            }
        }
        if (received == 0 ||
            (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
             errno != EINTR))
            device_lost = 1;
        if (device_lost) {
            fprintf(stderr, "blit: evdev gamepad disappeared: %s\n",
                    runtime->evdev_gamepad_paths[device_index]);
            broadcast_input_diagnostic(runtime, "warning", "input_device_lost",
                                        "evdev-gamepad",
                                        runtime->evdev_gamepad_paths[device_index],
                                        runtime->evdev_gamepad_count - 1);
            remove_evdev_gamepad(runtime, device_index);
            --device_index;
        }
    }
}

static void service_evdev_mouse(BlitRuntime *runtime)
{
    struct input_event event;
    ssize_t received;
    int device_index;

    for (device_index = 0; device_index < runtime->evdev_mouse_count;
         ++device_index) {
        int device_lost = 0;
        if (runtime->evdev_mouse_fds[device_index] < 0) continue;
        while ((received = read(runtime->evdev_mouse_fds[device_index],
                                &event, sizeof(event))) ==
               (ssize_t)sizeof(event)) {
            uint64_t input_started_us = monotonic_us();
            ++runtime->diagnostic_evdev_mouse_events;
            runtime->diagnostic_last_evdev_mouse_event_us = input_started_us;
            snprintf(runtime->diagnostic_last_evdev_mouse_path,
                     sizeof(runtime->diagnostic_last_evdev_mouse_path), "%s",
                     runtime->evdev_mouse_paths[device_index]);
            if (!runtime->evdev_mouse_reported[device_index]) {
                fprintf(stderr, "blit: evdev mouse events received from %s\n",
                        runtime->evdev_mouse_paths[device_index]);
                runtime->evdev_mouse_reported[device_index] = 1;
            }
            if (event.type == EV_REL) {
                if (event.code == REL_WHEEL || event.code == REL_HWHEEL) {
                    int button;
                    ++runtime->diagnostic_evdev_mouse_wheel_events;
                    if (event.code == REL_HWHEEL)
                        button = event.value > 0 ? 66 : 67;
                    else
                        button = event.value > 0 ? 64 : 65;
                    broadcast_evdev_pointer(runtime, "pointer_wheel", button,
                                             1, event.value,
                                             "evdev-pointer");
                    send_mouse_report(runtime, 0, runtime->evdev_mouse_x,
                                      runtime->evdev_mouse_y, button, 0);
                    record_input_dispatch(runtime, input_started_us);
                    continue;
                }
                if (event.code == REL_X) {
                    ++runtime->diagnostic_evdev_mouse_motion_events;
                    runtime->evdev_mouse_x += event.value;
                } else if (event.code == REL_Y) {
                    ++runtime->diagnostic_evdev_mouse_motion_events;
                    runtime->evdev_mouse_y += event.value;
                }
            } else if (event.type == EV_SYN && event.code == SYN_REPORT) {
                ++runtime->diagnostic_evdev_mouse_syncs;
                update_evdev_pointer(runtime);
                record_input_dispatch(runtime, input_started_us);
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
                ++runtime->diagnostic_evdev_mouse_button_events;
                int release = event.value == 0;
                dispatch_pointer_button(runtime, button, !release,
                                        "evdev-pointer");
                record_input_dispatch(runtime, input_started_us);
            }
        }
        if (received == 0 ||
            (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
             errno != EINTR))
            device_lost = 1;
        if (device_lost) {
            fprintf(stderr, "blit: evdev mouse disappeared: %s\n",
                    runtime->evdev_mouse_paths[device_index]);
            broadcast_input_diagnostic(runtime, "warning", "input_device_lost",
                                        "evdev-pointer",
                                        runtime->evdev_mouse_paths[device_index],
                                        runtime->evdev_mouse_count - 1);
            remove_evdev_mouse(runtime, device_index);
            --device_index;
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
        terminal_scrollback_show_live(find_surface_id(runtime, session->id));
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
        int device_lost = 0;
        if (runtime->evdev_keyboard_fds[device_index] < 0) continue;
        while ((received = read(runtime->evdev_keyboard_fds[device_index],
                                &event, sizeof(event))) ==
               (ssize_t)sizeof(event)) {
            uint64_t input_started_us = monotonic_us();
            if (event.type != EV_KEY) continue;
            if (event.code == KEY_LEFTSHIFT || event.code == KEY_RIGHTSHIFT)
                runtime->evdev_shift = event.value != 0;
            else if (event.code == KEY_LEFTCTRL || event.code == KEY_RIGHTCTRL)
                runtime->evdev_ctrl = event.value != 0;
            else if (event.code == KEY_LEFTALT || event.code == KEY_RIGHTALT)
                runtime->evdev_alt = event.value != 0;
            if (event.value != 0) {
                {
                    char detail[160];
                    snprintf(detail, sizeof(detail),
                             "code=%u value=%d shift=%d ctrl=%d alt=%d",
                             (unsigned int)event.code, event.value,
                             runtime->evdev_shift, runtime->evdev_ctrl,
                             runtime->evdev_alt);
                    diagnostic_trace(runtime, "keyboard_key", detail);
                }
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
                         "\"kind\":\"key\",\"time_us\":%llu,\"code\":%u,\"pressed\":%s,"
                         "\"repeat\":%s,\"shift\":%s,\"ctrl\":%s,\"alt\":%s}}",
                         target != NULL ? target->id : 0ULL,
                         (unsigned long long)monotonic_us(),
                         (unsigned int)event.code, event.value != 0 ? "true" : "false",
                         event.value == 2 ? "true" : "false",
                         runtime->evdev_shift ? "true" : "false",
                         runtime->evdev_ctrl ? "true" : "false",
                         runtime->evdev_alt ? "true" : "false");
                broadcast_stream(runtime, input_event, BLIT_STREAM_INPUT);
            }
            record_input_dispatch(runtime, input_started_us);
        }
        if (received == 0 ||
            (received < 0 && errno != EAGAIN && errno != EWOULDBLOCK &&
             errno != EINTR))
            device_lost = 1;
        if (device_lost) {
            fprintf(stderr, "blit: evdev keyboard disappeared: %s\n",
                    runtime->evdev_keyboard_paths[device_index]);
            broadcast_input_diagnostic(runtime, "warning", "input_device_lost",
                                        "evdev-keyboard",
                                        runtime->evdev_keyboard_paths[device_index],
                                        runtime->evdev_keyboard_count - 1);
            remove_evdev_keyboard(runtime, device_index);
            --device_index;
        }
    }
}

static void service_sdl_events(BlitRuntime *runtime)
{
    SDL_Event event;

    while (SDL_PollEvent(&event)) {
        /* KMSDRM can feed the same evdev device into SDL while Blit is also
           reading it directly.  The raw path handles motion, buttons, and
           wheel events with the display-global coordinates required for
           multi-output routing.  Discard SDL mouse events whenever that path
           is available, preventing duplicate clicks and cursor trails. */
        if (runtime->evdev_mouse_count > 0 &&
            (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN ||
             event.type == SDL_EVENT_MOUSE_BUTTON_UP ||
             event.type == SDL_EVENT_MOUSE_MOTION ||
             event.type == SDL_EVENT_MOUSE_WHEEL))
            continue;
        if (event.type == SDL_EVENT_MOUSE_BUTTON_DOWN) {
            int button = event.button.button == 1 ? 0 :
                event.button.button == 2 ? 1 :
                event.button.button == 3 ? 2 : -1;
            if (button >= 0) {
                int output_index = mouse_output_index(runtime,
                                                      event.button.windowID);
                float x = event.button.x;
                float y = event.button.y;
                if (output_index >= 0) {
                    int display_index = display_index_for_output(runtime,
                                                                  output_index);
                    if (display_index >= 0) {
                        x += runtime->displays[display_index].bounds.x;
                        y += runtime->displays[display_index].bounds.y;
                    }
                }
                runtime->mouse_buttons |= 1U << button;
                send_mouse_report(runtime, event.button.windowID,
                                  x, y, button, 0);
            }
        } else if (event.type == SDL_EVENT_MOUSE_BUTTON_UP) {
            int button = event.button.button == 1 ? 0 :
                event.button.button == 2 ? 1 :
                event.button.button == 3 ? 2 : -1;
            if (button >= 0) {
                int output_index = mouse_output_index(runtime,
                                                      event.button.windowID);
                float x = event.button.x;
                float y = event.button.y;
                if (output_index >= 0) {
                    int display_index = display_index_for_output(runtime,
                                                                  output_index);
                    if (display_index >= 0) {
                        x += runtime->displays[display_index].bounds.x;
                        y += runtime->displays[display_index].bounds.y;
                    }
                }
                send_mouse_report(runtime, event.button.windowID,
                                  x, y, button, 1);
                runtime->mouse_buttons &= ~(1U << button);
            }
        } else if (event.type == SDL_EVENT_MOUSE_MOTION) {
            int output_index = mouse_output_index(runtime,
                                                  event.motion.windowID);
            float x = event.motion.x;
            float y = event.motion.y;
            if (output_index >= 0) {
                int display_index = display_index_for_output(runtime,
                                                              output_index);
                if (display_index >= 0) {
                    x += runtime->displays[display_index].bounds.x;
                    y += runtime->displays[display_index].bounds.y;
                }
            }
            runtime->previous_pointer_x = runtime->pointer_x;
            runtime->previous_pointer_y = runtime->pointer_y;
            runtime->previous_pointer_output = runtime->pointer_output;
            runtime->pointer_valid = 1;
            runtime->pointer_output = mouse_output_index(runtime,
                                                         event.motion.windowID);
            runtime->pointer_x = x;
            runtime->pointer_y = y;
    runtime->pointer_dirty = 1;
    runtime->render_pending = 1;
    {
        char detail[160];
        snprintf(detail, sizeof(detail), "x=%.1f y=%.1f output=%d",
                 runtime->pointer_x, runtime->pointer_y,
                 runtime->pointer_output);
        diagnostic_trace(runtime, "pointer_motion", detail);
    }
            send_mouse_motion_report(runtime, event.motion.windowID,
                                     x, y);
        } else if (event.type == SDL_EVENT_MOUSE_WHEEL) {
            int button = event.wheel.y > 0 ? 64 : 65;
            int output_index = mouse_output_index(runtime,
                                                  event.wheel.windowID);
            float x = event.wheel.mouse_x;
            float y = event.wheel.mouse_y;
            if (output_index >= 0) {
                int display_index = display_index_for_output(runtime,
                                                              output_index);
                if (display_index >= 0) {
                    x += runtime->displays[display_index].bounds.x;
                    y += runtime->displays[display_index].bounds.y;
                }
            }
            send_mouse_report(runtime, event.wheel.windowID, x, y,
                              button, 0);
        }
    }
}

static void remove_http_client(BlitRuntime *runtime, int index)
{
    if (index < 0 || index >= BLIT_MAX_HTTP_CLIENTS ||
        !runtime->http_clients[index].used)
        return;
    if (runtime->http_clients[index].update_fd >= 0) {
        close(runtime->http_clients[index].update_fd);
        if (runtime->http_clients[index].update_path[0] != '\0')
            unlink(runtime->http_clients[index].update_path);
    }
    close(runtime->http_clients[index].fd);
    memset(&runtime->http_clients[index], 0,
           sizeof(runtime->http_clients[index]));
}

static int add_http_client(BlitRuntime *runtime, int fd)
{
    int i;
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
        if (!runtime->http_clients[i].used) {
            memset(&runtime->http_clients[i], 0,
                   sizeof(runtime->http_clients[i]));
            runtime->http_clients[i].used = 1;
            runtime->http_clients[i].fd = fd;
            runtime->http_clients[i].update_fd = -1;
            runtime->http_clients[i].accepted_us = monotonic_us();
            return i;
        }
    }
    return -1;
}

/* Return 1 when a complete HTTP request is buffered, 0 when more bytes are
   needed, and -1 when the declared body cannot fit in the bounded request. */
static int http_request_complete(const BlitHttpClient *client,
                                 size_t *required)
{
    const char *headers_end;
    const char *length_header;
    size_t header_length;
    size_t content_length = 0;

    headers_end = strstr(client->request, "\r\n\r\n");
    if (headers_end == NULL)
        return 0;
    header_length = (size_t)(headers_end - client->request) + 4;
    length_header = strstr(client->request, "Content-Length:");
    if (length_header != NULL)
        content_length = (size_t)strtoul(length_header + 15, NULL, 10);
    if (content_length > sizeof(client->request) - header_length)
        return -1;
    *required = header_length + content_length;
    return client->length >= *required ? 1 : 0;
}

static int update_authorized(const BlitRuntime *runtime, const char *request)
{
    char token[256];
    return runtime->update_token[0] != '\0' &&
        request_header(request, "X-Blit-Update-Token:", token, sizeof(token)) &&
        strcmp(token, runtime->update_token) == 0;
}

static int read_secret_file(const char *path, char *value, size_t capacity)
{
    int fd;
    ssize_t length;

    if (path == NULL || *path == '\0' || value == NULL || capacity < 2)
        return 0;
    fd = open(path, O_RDONLY | O_CLOEXEC);
    if (fd < 0)
        return 0;
    length = read(fd, value, capacity - 1);
    close(fd);
    if (length <= 0)
        return 0;
    value[length] = '\0';
    while (length > 0 &&
           (value[length - 1] == '\n' || value[length - 1] == '\r' ||
            value[length - 1] == ' ' || value[length - 1] == '\t'))
        value[--length] = '\0';
    return length > 0;
}

static int run_capture_command(const char *command, const char *argument,
                               char *output, size_t capacity)
{
    int pipe_fds[2];
    pid_t child;
    int status;
    ssize_t received;
    size_t used = 0;

    if (pipe(pipe_fds) < 0)
        return 0;
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        return 0;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(pipe_fds[1], STDOUT_FILENO);
        if (null_fd >= 0) dup2(null_fd, STDERR_FILENO);
        close(pipe_fds[0]);
        close(pipe_fds[1]);
        if (null_fd >= 0) close(null_fd);
        execlp(command, command, argument, (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    while (used + 1 < capacity) {
        received = read(pipe_fds[0], output + used, capacity - used - 1);
        if (received <= 0) break;
        used += (size_t)received;
    }
    close(pipe_fds[0]);
    output[used] = '\0';
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 0;
    return 1;
}

static int run_capture_command2(const char *command, const char *argument1,
                                const char *argument2, char *output,
                                size_t capacity)
{
    int pipe_fds[2];
    pid_t child;
    int status;
    ssize_t received;
    size_t used = 0;

    if (pipe(pipe_fds) < 0) return 0;
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]); close(pipe_fds[1]); return 0;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(pipe_fds[1], STDOUT_FILENO);
        if (null_fd >= 0) dup2(null_fd, STDERR_FILENO);
        close(pipe_fds[0]); close(pipe_fds[1]);
        if (null_fd >= 0) close(null_fd);
        execlp(command, command, argument1, argument2, (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    while (used + 1 < capacity) {
        received = read(pipe_fds[0], output + used, capacity - used - 1);
        if (received <= 0) break;
        used += (size_t)received;
    }
    close(pipe_fds[0]);
    output[used] = '\0';
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 0;
    return 1;
}

static int run_capture_command3(const char *command, const char *argument1,
                                const char *argument2, const char *argument3,
                                char *output, size_t capacity)
{
    int pipe_fds[2];
    pid_t child;
    int status;
    ssize_t received;
    size_t used = 0;

    if (pipe(pipe_fds) < 0) return 0;
    child = fork();
    if (child < 0) {
        close(pipe_fds[0]); close(pipe_fds[1]); return 0;
    }
    if (child == 0) {
        int null_fd = open("/dev/null", O_WRONLY);
        dup2(pipe_fds[1], STDOUT_FILENO);
        if (null_fd >= 0) dup2(null_fd, STDERR_FILENO);
        close(pipe_fds[0]); close(pipe_fds[1]);
        if (null_fd >= 0) close(null_fd);
        execlp(command, command, argument1, argument2, argument3,
               (char *)NULL);
        _exit(127);
    }
    close(pipe_fds[1]);
    while (used + 1 < capacity) {
        received = read(pipe_fds[0], output + used, capacity - used - 1);
        if (received <= 0) break;
        used += (size_t)received;
    }
    close(pipe_fds[0]);
    output[used] = '\0';
    if (waitpid(child, &status, 0) != child || !WIFEXITED(status) ||
        WEXITSTATUS(status) != 0)
        return 0;
    return 1;
}

static int update_package_valid(const char *path, const char *expected_sha,
                                char *detail, size_t detail_capacity)
{
    char digest[128];
    char package_name[128];
    char *space;

    if (!run_capture_command("sha256sum", path, digest, sizeof(digest)) ||
        strncmp(digest, expected_sha, 64) != 0) {
        snprintf(detail, detail_capacity, "SHA-256 verification failed");
        return 0;
    }
    if (!run_capture_command3("dpkg-deb", "--field", path, "Package",
                              package_name, sizeof(package_name))) {
        snprintf(detail, detail_capacity, "not a readable Debian package");
        return 0;
    }
    /* dpkg-deb --field is selected by the environment wrapper below; retain
       an explicit package-name check in the update child before install. */
    space = strchr(package_name, '\n');
    if (space != NULL) *space = '\0';
    if (strcmp(package_name, "blit") != 0) {
        snprintf(detail, detail_capacity, "package is not blit");
        return 0;
    }
    return 1;
}

static int run_update_helper(const char *path, const char *sha256,
                             char *output, size_t capacity)
{
    struct sockaddr_un address;
    int fd;
    int length;
    ssize_t received;
    size_t used = 0;
    fd = socket(AF_UNIX, SOCK_STREAM, 0);
    if (fd < 0) return 0;
    memset(&address, 0, sizeof(address));
    address.sun_family = AF_UNIX;
    snprintf(address.sun_path, sizeof(address.sun_path),
             "/run/blit/update.sock");
    if (connect(fd, (struct sockaddr *)&address, sizeof(address)) < 0) {
        close(fd);
        snprintf(output, capacity, "root update helper is unavailable");
        return 0;
    }
    length = snprintf(output, capacity, "UPDATE %s %s\n", path, sha256);
    if (write(fd, output, (size_t)length) != length) {
        close(fd);
        return 0;
    }
    while (used + 1 < capacity) {
        received = read(fd, output + used, capacity - used - 1);
        if (received <= 0) break;
        used += (size_t)received;
    }
    close(fd);
    output[used] = '\0';
    return strncmp(output, "OK", 2) == 0;
}

static void update_status_json(const BlitRuntime *runtime, char *body,
                               size_t capacity)
{
    int i;
    int active = 0;
    size_t received = 0;
    size_t expected = 0;
    char escaped_detail[sizeof(runtime->update_detail) * 2];

    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
        const BlitHttpClient *client = &runtime->http_clients[i];
        if (!client->used || client->update_fd < 0)
            continue;
        active = 1;
        received += client->update_received;
        expected += client->update_expected;
    }
    json_escape(runtime->update_detail, escaped_detail, sizeof(escaped_detail));
    snprintf(body, capacity,
             "{\"state\":\"%s\",\"detail\":\"%s\","
             "\"upload\":{\"active\":%s,\"received_bytes\":%lu,"
             "\"expected_bytes\":%lu}}",
             runtime->update_state[0] != '\0' ? runtime->update_state :
             "disabled", escaped_detail, active ? "true" : "false",
             (unsigned long)received, (unsigned long)expected);
}

static void cancel_update_upload(BlitRuntime *runtime, int fd,
                                 const char *request)
{
    int i;
    int cancelled = 0;

    if (!update_authorized(runtime, request)) {
        send_problem(fd, 403, "Forbidden", "update_disabled",
                     "A valid update token is required.");
        return;
    }
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
        if (runtime->http_clients[i].used &&
            runtime->http_clients[i].update_fd >= 0) {
            remove_http_client(runtime, i);
            ++cancelled;
        }
    }
    if (!cancelled) {
        send_problem(fd, 409, "Conflict", "update_not_active",
                     "There is no package upload in progress.");
        return;
    }
    snprintf(runtime->update_state, sizeof(runtime->update_state),
             "cancelled");
    snprintf(runtime->update_detail, sizeof(runtime->update_detail),
             "package upload cancelled");
    send_http(fd, 202, "Accepted", "application/json",
              "{\"state\":\"cancelled\",\"detail\":\""
              "package upload cancelled\"}");
}

static void reset_update_state(BlitRuntime *runtime, int fd,
                               const char *request)
{
    int i;

    if (!update_authorized(runtime, request)) {
        send_problem(fd, 403, "Forbidden", "update_disabled",
                     "A valid update token is required.");
        return;
    }
    for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
        if (runtime->http_clients[i].used &&
            runtime->http_clients[i].update_fd >= 0) {
            send_problem(fd, 409, "Conflict", "update_active",
                         "An update upload is still in progress.");
            return;
        }
    }
    snprintf(runtime->update_state, sizeof(runtime->update_state), "ready");
    snprintf(runtime->update_detail, sizeof(runtime->update_detail),
             "update state reset");
    send_http(fd, 202, "Accepted", "application/json",
              "{\"state\":\"ready\",\"detail\":\""
              "update state reset\"}");
}

static void finish_update_upload(BlitRuntime *runtime, BlitHttpClient *client)
{
    char output[512];
    close(client->update_fd);
    client->update_fd = -1;
    if (!update_package_valid(client->update_path, client->update_sha256,
                              runtime->update_detail,
                              sizeof(runtime->update_detail))) {
        snprintf(runtime->update_state, sizeof(runtime->update_state),
                 "failed");
        unlink(client->update_path);
        send_problem(client->fd, 422, "Unprocessable Entity", "update_invalid",
                     runtime->update_detail);
        return;
    }
    snprintf(runtime->update_state, sizeof(runtime->update_state), "installing");
    if (!run_update_helper(client->update_path, client->update_sha256,
                           output, sizeof(output))) {
        snprintf(runtime->update_state, sizeof(runtime->update_state), "failed");
        snprintf(runtime->update_detail, sizeof(runtime->update_detail),
                 "privileged update failed: %s", output);
        unlink(client->update_path);
        send_problem(client->fd, 500, "Internal Server Error", "update_install_failed",
                     runtime->update_detail);
        return;
    }
    snprintf(runtime->update_state, sizeof(runtime->update_state), "restarting");
    snprintf(runtime->update_detail, sizeof(runtime->update_detail),
             "package installed; restarting Blit");
    send_http(client->fd, 202, "Accepted", "application/json",
              "{\"state\":\"restarting\",\"detail\":\"package installed; restarting Blit\"}");
    blit_restart_requested = 1;
    blit_running = 0;
}

static void service_update_upload(BlitRuntime *runtime, BlitHttpClient *client)
{
    unsigned char chunk[8192];
    ssize_t received;
    size_t remaining = client->update_expected - client->update_received;
    if (remaining > sizeof(chunk)) remaining = sizeof(chunk);
    /* The HTTP client is selected as readable, but a partial packet can be
       consumed by the previous read before the next scheduler turn.  Never
       let a large package upload block the single gateway/render loop while
       waiting for the next packet. */
    received = recv(client->fd, chunk, remaining, MSG_DONTWAIT);
    if (received < 0 && (errno == EAGAIN || errno == EWOULDBLOCK ||
                         errno == EINTR))
        return;
    if (received <= 0) {
        snprintf(runtime->update_state, sizeof(runtime->update_state), "failed");
        snprintf(runtime->update_detail, sizeof(runtime->update_detail),
                 "update upload interrupted");
        remove_http_client(runtime, (int)(client - runtime->http_clients));
        return;
    }
    if (write(client->update_fd, chunk, (size_t)received) != received) {
        snprintf(runtime->update_state, sizeof(runtime->update_state), "failed");
        snprintf(runtime->update_detail, sizeof(runtime->update_detail),
                 "staged package write failed");
        remove_http_client(runtime, (int)(client - runtime->http_clients));
        return;
    }
    client->update_received += (size_t)received;
    client->update_last_progress_us = monotonic_us();
    if (client->update_received == client->update_expected)
        finish_update_upload(runtime, client);
}

static void service_http_client(BlitRuntime *runtime, int index)
{
    BlitHttpClient *client = &runtime->http_clients[index];
    const char *headers_end;
    ssize_t received;
    size_t required = 0;
    int complete;

    if (!client->used)
        return;
    if (client->update_fd >= 0) {
        service_update_upload(runtime, client);
        return;
    }
    if (client->length + 1 >= sizeof(client->request)) {
        remove_http_client(runtime, index);
        return;
    }
    received = recv(client->fd, client->request + client->length,
                    sizeof(client->request) - client->length - 1, 0);
    if (received == 0) {
        remove_http_client(runtime, index);
        return;
    }
    if (received < 0) {
        if (errno == EINTR || errno == EAGAIN || errno == EWOULDBLOCK)
            return;
        remove_http_client(runtime, index);
        return;
    }
    client->length += (size_t)received;
    client->request[client->length] = '\0';
    headers_end = strstr(client->request, "\r\n\r\n");
    if (headers_end != NULL &&
        strncmp(client->request, "PUT /v1/control/update ", 23) == 0) {
        size_t header_length = (size_t)(headers_end - client->request) + 4;
        size_t content_length = 0;
        char sha256[65];
        /* The update helper is a separate systemd service.  PrivateTmp gives
           each service a different /tmp, so an upload staged there cannot be
           seen by the root helper.  Use the package-owned shared directory
           by default; tests may explicitly select a private staging dir. */
        char temporary[PATH_MAX];
        const char *update_directory = getenv("BLIT_UPDATE_DIRECTORY");
        if (update_directory == NULL || update_directory[0] == '\0')
            update_directory = "/var/lib/blit/updates";
        snprintf(temporary, sizeof(temporary), "%s/blit-update-XXXXXX",
                 update_directory);
        int temporary_fd;
        const char *length_header = strstr(client->request, "Content-Length:");
        if (runtime->update_token[0] == '\0' ||
            !update_authorized(runtime, client->request)) {
            send_problem(client->fd, 403, "Forbidden", "update_disabled",
                         "A valid update token is required.");
            remove_http_client(runtime, index);
            return;
        }
        if (length_header != NULL)
            content_length = (size_t)strtoull(length_header + 15, NULL, 10);
        if (content_length == 0 || content_length > 64U * 1024U * 1024U ||
            !request_header(client->request, "X-Blit-SHA256:", sha256,
                            sizeof(sha256)) || strlen(sha256) != 64) {
            send_problem(client->fd, 413, "Payload Too Large", "update_invalid",
                         "A bounded package length and SHA-256 header are required.");
            remove_http_client(runtime, index);
            return;
        }
        /* Large curl/http clients may wait for this before sending the
           remainder of the package body. */
        send_all(client->fd, "HTTP/1.1 100 Continue\r\n\r\n",
                 strlen("HTTP/1.1 100 Continue\r\n\r\n"));
        temporary_fd = mkstemp(temporary);
        if (temporary_fd < 0) {
            send_problem(client->fd, 500, "Internal Server Error", "update_stage_failed",
                         "The update package could not be staged.");
            remove_http_client(runtime, index);
            return;
        }
        client->update_fd = temporary_fd;
        client->update_expected = content_length;
        client->update_received = client->length > header_length ?
            client->length - header_length : 0;
        client->update_last_progress_us = monotonic_us();
        snprintf(client->update_path, sizeof(client->update_path), "%s", temporary);
        snprintf(client->update_sha256, sizeof(client->update_sha256), "%s", sha256);
        snprintf(runtime->update_state, sizeof(runtime->update_state), "receiving");
        snprintf(runtime->update_detail, sizeof(runtime->update_detail),
                 "receiving Debian package");
        if (client->update_received > content_length ||
            (client->update_received > 0 &&
             write(client->update_fd, client->request + header_length,
                   client->update_received) != (ssize_t)client->update_received)) {
            remove_http_client(runtime, index);
            return;
        }
        if (client->update_received == client->update_expected)
            finish_update_upload(runtime, client);
        return;
    }
    complete = http_request_complete(client, &required);
    if (complete < 0 || (complete == 0 &&
                         client->length + 1 >= sizeof(client->request))) {
        remove_http_client(runtime, index);
        return;
    }
    if (complete == 0 || client->length < required)
        return;
    handle_request(client->fd, runtime, client, client->request);
    if (client->screenshot_output_index > 0) {
        client->length = 0;
        client->request[0] = '\0';
        client->accepted_us = monotonic_us();
        return;
    }
    if (stream_contains(runtime, client->fd)) {
        memset(client, 0, sizeof(*client));
        return;
    }
    remove_http_client(runtime, index);
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
        bind_address = "0.0.0.0";
    memset(&runtime, 0, sizeof(runtime));
    runtime.evdev_watch_fd = -1;
    runtime.started_us = monotonic_us();
    diagnostic_log(&runtime, "info", "server_start", "Blit gateway starting");
    blit_font_load(&runtime.font);
    runtime.font_scale = 1;
    runtime.render_interval_ms = 16;
    {
        const char *font_scale = getenv("BLIT_FONT_SCALE");
        const char *render_interval = getenv("BLIT_RENDER_INTERVAL_MS");
        const char *screenshot_dir = getenv("BLIT_SDL_SCREENSHOT_DIR");
        const char *screenshot_after = getenv("BLIT_SDL_SCREENSHOT_AFTER");
        if (font_scale != NULL)
            runtime.font_scale = atoi(font_scale);
        if (render_interval != NULL)
            runtime.render_interval_ms = atoi(render_interval);
        if (screenshot_dir != NULL)
            snprintf(runtime.sdl_screenshot_dir,
                     sizeof(runtime.sdl_screenshot_dir), "%s", screenshot_dir);
        runtime.sdl_screenshot_after = screenshot_after != NULL ?
            (unsigned long)strtoul(screenshot_after, NULL, 10) : 1;
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
            runtime.evdev_gamepad_fds[input_index] = -1;
        }
    }
    runtime.next_session_id = 1;
    runtime.listen_port = port;
    runtime.next_frame_id = 1;
    runtime.next_event_sequence = 1;
    runtime.topology_revision = 1;
    runtime.input_routes_revision = 1;
    runtime.next_overlay_id = 1;
    runtime.effect.revision = 1;
    snprintf(runtime.effect.preset, sizeof(runtime.effect.preset), "none");
    {
        const char *auth_token = getenv("BLIT_AUTH_TOKEN");
        if (auth_token != NULL)
            snprintf(runtime.auth_token, sizeof(runtime.auth_token), "%s",
                     auth_token);
        {
            const char *update_token = getenv("BLIT_UPDATE_TOKEN");
            if (update_token != NULL)
                snprintf(runtime.update_token, sizeof(runtime.update_token), "%s",
                         update_token);
            if (runtime.update_token[0] == '\0') {
                const char *update_token_file = getenv("BLIT_UPDATE_TOKEN_FILE");
                read_secret_file(update_token_file, runtime.update_token,
                                 sizeof(runtime.update_token));
            }
        }
    }
    snprintf(runtime.update_state, sizeof(runtime.update_state),
             runtime.update_token[0] != '\0' ? "ready" : "disabled");
    snprintf(runtime.update_detail, sizeof(runtime.update_detail),
             runtime.update_token[0] != '\0' ?
             "update API enabled" : "BLIT_UPDATE_TOKEN is not configured");
    if (strcmp(bind_address, "127.0.0.1") != 0 &&
        strcmp(bind_address, "::1") != 0 && runtime.auth_token[0] == '\0')
        fprintf(stderr,
                "blit: warning: network listener has no BLIT_AUTH_TOKEN; "
                "the API is unauthenticated\n");
    {
        const char *command_allowlist = getenv("BLIT_COMMAND_ALLOWLIST");
        if (command_allowlist != NULL)
            snprintf(runtime.command_allowlist,
                     sizeof(runtime.command_allowlist), "%s",
                     command_allowlist);
    }
    signal(SIGINT, stop_server);
    signal(SIGTERM, stop_server);
    signal(SIGPIPE, SIG_IGN);
    discover_displays(&runtime);
    apply_test_refresh_override(&runtime);
    if (runtime.direct_kms && runtime.display_count == 0) {
        fprintf(stderr, "blit: direct KMS backend did not find a usable display\n");
        if (runtime.kms != NULL) blit_kms_close(runtime.kms);
        return 1;
    }
    if (runtime.sdl_ready || runtime.direct_kms)
        initialize_outputs(&runtime);
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
    {
        runtime.evdev_gamepad_count = open_evdev_gamepads(
            runtime.evdev_gamepad_fds, runtime.evdev_gamepad_paths,
            &runtime.evdev_gamepad_count);
        if (runtime.evdev_gamepad_count > 0) {
            int input_index;
            for (input_index = 0; input_index < runtime.evdev_gamepad_count;
                 ++input_index)
                fprintf(stderr, "blit: evdev gamepad[%d]=%s fd=%d\n",
                        input_index, runtime.evdev_gamepad_paths[input_index],
                        runtime.evdev_gamepad_fds[input_index]);
        } else
            fprintf(stderr, "blit: no usable evdev gamepad found\n");
    }
    open_evdev_watch(&runtime);
    runtime.evdev_rescan_requested = 0;
    reset_render_schedule(&runtime, monotonic_us());
    if (runtime.output_count > 0) {
        runtime.pointer_valid = 1;
        runtime.pointer_output = 0;
        runtime.pointer_x = (runtime.direct_kms ?
            runtime.displays[0].bounds.x : 0) +
            (runtime.displays[0].bounds.w > 0 ?
             runtime.displays[0].bounds.w / 2.0f : 0.0f);
        runtime.pointer_y = (runtime.direct_kms ?
            runtime.displays[0].bounds.y : 0) +
            (runtime.displays[0].bounds.h > 0 ?
             runtime.displays[0].bounds.h / 2.0f : 0.0f);
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
    printf("blit gateway listening on %s TCP port %d (%d display(s))\n",
           bind_address, port, runtime.display_count);
    fflush(stdout);

    while (blit_running) {
        int client_fd;
        int max_fd = server_fd;
        int ready;
        int mouse_ready = 0;
        int keyboard_ready = 0;
        int gamepad_ready = 0;
        int kms_ready = 0;
        int kms_fd = runtime.direct_kms ? blit_kms_fd(runtime.kms) : -1;
        int i;
        fd_set readable;
        struct timeval timeout;
        int wait_ms;
        uint64_t now;
        uint32_t output_mask;
        int pending_before_events;
        uint64_t select_started_us;
        uint64_t sdl_events_started_us;

        ++runtime.diagnostic_loop_iterations;
        if (runtime.diagnostic_last_loop_us != 0) {
            double gap = (double)(monotonic_us() -
                                  runtime.diagnostic_last_loop_us) / 1000.0;
            runtime.diagnostic_last_loop_gap_ms = gap;
            if (gap > runtime.diagnostic_max_loop_gap_ms)
                runtime.diagnostic_max_loop_gap_ms = gap;
        }
        runtime.diagnostic_last_loop_us = monotonic_us();

        {
            uint64_t evdev_scan_started_us = monotonic_us();
            reconnect_evdev_devices(&runtime);
            runtime.diagnostic_last_evdev_scan_ms =
                (double)(monotonic_us() - evdev_scan_started_us) / 1000.0;
            if (runtime.diagnostic_last_evdev_scan_ms >
                runtime.diagnostic_max_evdev_scan_ms)
                runtime.diagnostic_max_evdev_scan_ms =
                    runtime.diagnostic_last_evdev_scan_ms;
        }
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i)
            refresh_session_state(&runtime, &runtime.sessions[i]);

        pending_before_events = runtime.render_pending;
        sdl_events_started_us = monotonic_us();
        if (runtime.sdl_ready && !runtime.direct_kms)
            service_sdl_events(&runtime);
        runtime.diagnostic_last_sdl_events_ms =
            (double)(monotonic_us() - sdl_events_started_us) / 1000.0;
        if (runtime.diagnostic_last_sdl_events_ms >
            runtime.diagnostic_max_sdl_events_ms)
            runtime.diagnostic_max_sdl_events_ms =
                runtime.diagnostic_last_sdl_events_ms;
        now = monotonic_us();
        for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
            BlitHttpClient *client = &runtime.http_clients[i];
            if (!client->used)
                continue;
            if (client->update_fd >= 0) {
                if (now > client->update_last_progress_us &&
                    now - client->update_last_progress_us >
                        BLIT_UPDATE_IDLE_TIMEOUT_US) {
                    snprintf(runtime.update_state,
                             sizeof(runtime.update_state), "failed");
                    snprintf(runtime.update_detail,
                             sizeof(runtime.update_detail),
                             "update upload idle timeout");
                    remove_http_client(&runtime, i);
                }
                continue;
            }
            if (now > client->accepted_us &&
                now - client->accepted_us > 5000000ULL)
                remove_http_client(&runtime, i);
        }
        FD_ZERO(&readable);
        FD_SET(server_fd, &readable);
        for (i = 0; i < runtime.evdev_mouse_count; ++i) {
            int input_fd = runtime.evdev_mouse_fds[i];
            if (input_fd < 0) continue;
            FD_SET(input_fd, &readable);
            if (input_fd > max_fd) max_fd = input_fd;
        }
        for (i = 0; i < runtime.evdev_keyboard_count; ++i) {
            int input_fd = runtime.evdev_keyboard_fds[i];
            if (input_fd < 0) continue;
            FD_SET(input_fd, &readable);
            if (input_fd > max_fd) max_fd = input_fd;
        }
        for (i = 0; i < runtime.evdev_gamepad_count; ++i) {
            int input_fd = runtime.evdev_gamepad_fds[i];
            if (input_fd < 0) continue;
            FD_SET(input_fd, &readable);
            if (input_fd > max_fd) max_fd = input_fd;
        }
        if (runtime.evdev_watch_fd >= 0) {
            FD_SET(runtime.evdev_watch_fd, &readable);
            if (runtime.evdev_watch_fd > max_fd)
                max_fd = runtime.evdev_watch_fd;
        }
        for (i = 0; i < BLIT_MAX_STREAMS; ++i) {
            if (runtime.streams[i].used) {
                FD_SET(runtime.streams[i].fd, &readable);
                if (runtime.streams[i].fd > max_fd)
                    max_fd = runtime.streams[i].fd;
            }
        }
        for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
            if (runtime.http_clients[i].used) {
                FD_SET(runtime.http_clients[i].fd, &readable);
                if (runtime.http_clients[i].fd > max_fd)
                    max_fd = runtime.http_clients[i].fd;
            }
        }
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            if (runtime.sessions[i].used && runtime.sessions[i].pty_fd >= 0) {
                FD_SET(runtime.sessions[i].pty_fd, &readable);
                if (runtime.sessions[i].pty_fd > max_fd)
                    max_fd = runtime.sessions[i].pty_fd;
            }
        }
        if (kms_fd >= 0) {
            FD_SET(kms_fd, &readable);
            if (kms_fd > max_fd) max_fd = kms_fd;
        }
        now = monotonic_us();
        service_attach_reasserts(&runtime, now);
        wait_ms = render_wait_ms(&runtime, now);
        for (i = 0; i < BLIT_MAX_SESSIONS; ++i) {
            BlitSession *session = &runtime.sessions[i];
            if (session->used && session->attach_reassert_pending &&
                session->attach_reassert_due_us > now) {
                uint64_t remaining_us = session->attach_reassert_due_us - now;
                int reassert_wait_ms = (int)((remaining_us + 999ULL) / 1000ULL);
                if (reassert_wait_ms < wait_ms)
                    wait_ms = reassert_wait_ms;
            }
        }
        /* A bounded stream service turn may leave complete WebSocket
           messages in Blit's user-space receive buffer after the kernel socket
           itself becomes readable=false. Do not sleep for the idle timeout or
           wait for a new kernel edge in that state; service the buffered work
           on the next loop after presentation. */
        for (i = 0; i < BLIT_MAX_STREAMS; ++i)
            if (runtime.streams[i].used && runtime.streams[i].receive_length > 0) {
                wait_ms = 0;
                break;
            }
        timeout.tv_sec = wait_ms / 1000;
        timeout.tv_usec = (wait_ms % 1000) * 1000;
        select_started_us = monotonic_us();
        ready = select(max_fd + 1, &readable, NULL, NULL, &timeout);
        runtime.diagnostic_last_select_ms =
            (double)(monotonic_us() - select_started_us) / 1000.0;
        if (runtime.diagnostic_last_select_ms >
            runtime.diagnostic_max_select_ms)
            runtime.diagnostic_max_select_ms =
                runtime.diagnostic_last_select_ms;
        if (kms_fd >= 0 && ready > 0 && FD_ISSET(kms_fd, &readable))
            kms_ready = 1;
        if (runtime.direct_kms && runtime.kms != NULL) {
            int serviced = blit_kms_service(runtime.kms, kms_ready);
            if (serviced < 0)
                fprintf(stderr, "blit: KMS event service failed\n");
            else if (serviced > 0)
                runtime.render_pending = 1;
        }
        if (runtime.evdev_watch_fd >= 0 && ready > 0 &&
            FD_ISSET(runtime.evdev_watch_fd, &readable))
            service_evdev_watch(&runtime);
        if (ready > 0 && FD_ISSET(server_fd, &readable)) {
            client_fd = accept(server_fd, NULL, NULL);
            if (client_fd < 0) {
                if (errno == EINTR) continue;
                break;
            }
            fcntl(client_fd, F_SETFD, FD_CLOEXEC);
            if (add_http_client(&runtime, client_fd) < 0) {
                send_problem(client_fd, 503, "Service Unavailable",
                             "http_capacity",
                             "The HTTP connection limit has been reached.");
                close(client_fd);
            }
        }
        if (ready > 0) {
            for (i = 0; i < runtime.evdev_mouse_count; ++i)
                if (runtime.evdev_mouse_fds[i] >= 0 &&
                    FD_ISSET(runtime.evdev_mouse_fds[i], &readable))
                    mouse_ready = 1;
            for (i = 0; i < runtime.evdev_keyboard_count; ++i)
                if (runtime.evdev_keyboard_fds[i] >= 0 &&
                    FD_ISSET(runtime.evdev_keyboard_fds[i], &readable))
                    keyboard_ready = 1;
            for (i = 0; i < runtime.evdev_gamepad_count; ++i)
                if (runtime.evdev_gamepad_fds[i] >= 0 &&
                    FD_ISSET(runtime.evdev_gamepad_fds[i], &readable))
                    gamepad_ready = 1;
            if (keyboard_ready)
                service_evdev_keyboard(&runtime);
            if (mouse_ready)
                service_evdev_mouse(&runtime);
            if (gamepad_ready)
                service_evdev_gamepad(&runtime);
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
            for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
                if (runtime.http_clients[i].used &&
                    FD_ISSET(runtime.http_clients[i].fd, &readable))
                    service_http_client(&runtime, i);
            }
        }
        /* User-space stream data remains actionable even when select() timed
           out because the kernel socket was already drained. Service it
           outside the ready-only block so bounded stream batches cannot spin
           without making progress. */
        for (i = 0; i < BLIT_MAX_STREAMS; ++i)
            if (runtime.streams[i].used && runtime.streams[i].receive_length > 0)
                service_stream(&runtime, runtime.streams[i].fd);
        now = monotonic_us();
        /* A refresh schedule is only meaningful while work is pending.  If
           the display was idle and a new terminal/input event arrived, start
           a fresh phase at that event instead of reporting every idle refresh
           period as a missed deadline. */
        if (!pending_before_events && runtime.render_pending)
            reset_render_schedule(&runtime, now);
        if (runtime.render_pending &&
            now >= runtime.terminal_frame_quiet_until_us &&
            (output_mask = due_output_mask(&runtime, now)) != 0) {
            render_current_surfaces(&runtime, output_mask);
            advance_render_schedule(&runtime, now, output_mask);
            expire_pixel_frame_pending(&runtime, monotonic_us());
            runtime.render_pending = render_work_remains(&runtime);
            runtime.render_last_us = now;
        }
        for (i = 0; i < BLIT_MAX_HTTP_CLIENTS; ++i) {
            BlitHttpClient *client = &runtime.http_clients[i];
            int screenshot_index;
            char screenshot_path[128];
            if (!client->used || client->screenshot_output_index <= 0)
                continue;
            screenshot_index = client->screenshot_output_index - 1;
            if (screenshot_index < 0 || screenshot_index >= runtime.output_count)
                continue;
            snprintf(screenshot_path, sizeof(screenshot_path),
                     "/v1/diagnostics/screenshots/display-%llu",
                     runtime.outputs[screenshot_index].display_id);
            if (runtime.direct_kms) {
                int screenshot_result = send_kms_screenshot(
                    client->fd, &runtime, screenshot_index);
                if (screenshot_result == 0)
                    continue;
            } else {
                if (runtime.outputs[screenshot_index].screenshot_surface == NULL)
                    continue;
                send_sdl_screenshot(client->fd, &runtime, screenshot_path);
            }
            runtime.screenshot_requested_mask &= ~(1U << screenshot_index);
            remove_http_client(&runtime, i);
        }
    }
    close(server_fd);
    if (runtime.evdev_watch_fd >= 0)
        close(runtime.evdev_watch_fd);
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
        for (input_index = 0; input_index < runtime.evdev_gamepad_count;
             ++input_index)
            if (runtime.evdev_gamepad_fds[input_index] >= 0)
                close(runtime.evdev_gamepad_fds[input_index]);
    }
    destroy_outputs(&runtime);
    blit_font_free(&runtime.font);
    if (runtime.sdl_ready)
        SDL_Quit();
    if (blit_restart_requested) {
        char port_text[16];
        snprintf(port_text, sizeof(port_text), "%d", runtime.listen_port);
        execl("/usr/lib/blit/bin/blit-server", "blit-server",
              port_text, (char *)NULL);
        fprintf(stderr, "blit: update restart failed: %s\n", strerror(errno));
        return 1;
    }
    return 0;
}
