/* Real production initialization with an SDL boundary that rejects moves.
   Real dummy/software windows/renderers; fake display IDs only at boundary.
   This proves creation-time binding, not physical KMSDRM scanout. */
#define _GNU_SOURCE 1
#include <SDL3/SDL.h>
static SDL_Window *fixture_window(const char *, int, int, SDL_WindowFlags);
static SDL_Window *fixture_window_properties(SDL_PropertiesID);
static bool fixture_move(SDL_Window *, int, int);
static bool fixture_fullscreen(SDL_Window *, bool);
static SDL_DisplayID fixture_window_display(SDL_Window *);
static SDL_Renderer *fixture_renderer(SDL_Window *, const char *);
static void fixture_destroy_window(SDL_Window *);
#define SDL_CreateWindow fixture_window
#define SDL_CreateWindowWithProperties fixture_window_properties
#define SDL_SetWindowPosition fixture_move
#define SDL_SetWindowFullscreen fixture_fullscreen
#define SDL_GetDisplayForWindow fixture_window_display
#define SDL_CreateRenderer fixture_renderer
#define SDL_DestroyWindow fixture_destroy_window
#define blit_kms_open fixture_kms_open
#define main blit_server_entry_for_startup_gate
#include "../../src/blit_server.c"
#undef main
#undef SDL_CreateWindow
#undef SDL_CreateWindowWithProperties
#undef SDL_SetWindowPosition
#undef SDL_SetWindowFullscreen
#undef SDL_GetDisplayForWindow
#undef SDL_CreateRenderer
#undef SDL_DestroyWindow
#undef blit_kms_open

static SDL_Window *windows[8];
static SDL_DisplayID bindings[8];
static int created, properties_created, moves, destroyed, failure_mode, fallbacks;
int fixture_kms_open(BlitKms *kms, char *error, size_t capacity)
{
    (void)kms; ++fallbacks;
    snprintf(error, capacity, "mock fallback deliberately unavailable; no DRM opened");
    return -1;
}
static SDL_Window *fixture_window(const char *title, int w, int h, SDL_WindowFlags flags)
{
    SDL_Window *window = SDL_CreateWindow(title, w, h, flags);
    if (window && created < 8) {
        windows[created] = window; bindings[created++] = 11;
    }
    return window;
}
static SDL_Window *fixture_window_properties(SDL_PropertiesID props)
{
    SDL_Window *window;
    Sint64 x = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_X_NUMBER, 0);
    Sint64 y = SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_Y_NUMBER, 0);
    SDL_DisplayID binding = (SDL_DisplayID)x & 0xffff;
    if (!SDL_WINDOWPOS_ISCENTERED(x) || x != y || (binding != 11 && binding != 57))
        return SDL_SetError("missing creation-time display binding"), (SDL_Window *)NULL;
    if (failure_mode == 1 && binding == 57)
        return SDL_SetError("injected creation failure"), (SDL_Window *)NULL;
    window = SDL_CreateWindow("startup-gate",
        (int)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_WIDTH_NUMBER, 0),
        (int)SDL_GetNumberProperty(props, SDL_PROP_WINDOW_CREATE_HEIGHT_NUMBER, 0), 0);
    if (window && created < 8) {
        windows[created] = window; bindings[created++] = binding; ++properties_created;
    }
    return window;
}
static bool fixture_move(SDL_Window *window, int x, int y)
{
    (void)window; (void)x; (void)y; ++moves;
    return SDL_SetError("KMSDRM window moves unsupported");
}
static bool fixture_fullscreen(SDL_Window *window, bool enabled)
{
    (void)enabled;
    if (failure_mode == 3 && fixture_window_display(window) == 57)
        return SDL_SetError("injected fullscreen failure");
    return true;
}
static SDL_DisplayID fixture_window_display(SDL_Window *window)
{
    int i;
    for (i = 0; i < created; ++i) if (windows[i] == window)
        return failure_mode == 2 && bindings[i] == 57 ? 11 : bindings[i];
    return 0;
}
static SDL_Renderer *fixture_renderer(SDL_Window *window, const char *name)
{
    if (failure_mode == 4 && fixture_window_display(window) == 57)
        return SDL_SetError("injected renderer failure"), (SDL_Renderer *)NULL;
    return SDL_CreateRenderer(window, name);
}
static void fixture_destroy_window(SDL_Window *window)
{ if (window) ++destroyed; SDL_DestroyWindow(window); }
int main(int argc, char **argv)
{
    const char *renderer = getenv("BLIT_TEST_RENDERER");
    const char *driver;
    int i, mode;
    if (argc != 2) return 2;
    if (!renderer) renderer = "software";
    driver = strcmp(renderer, "software") == 0 ? "dummy" : "offscreen";
    SDL_SetHint(SDL_HINT_VIDEO_DRIVER, driver);
    if (!SDL_Init(SDL_INIT_VIDEO) || strcmp(SDL_GetCurrentVideoDriver(), driver) != 0) return 2;
    for (mode = 0; mode <= 4; ++mode) {
        BlitRuntime *runtime = calloc(1, sizeof(*runtime));
        if (!runtime || blit_font_load_path(&runtime->font, argv[1])) return 2;
        created = properties_created = moves = destroyed = fallbacks = 0;
        failure_mode = mode;
        snprintf(runtime->renderer_request, sizeof(runtime->renderer_request), "%s", renderer);
        runtime->display_count = 2;
        runtime->displays[0].id = 11; runtime->displays[1].id = 57;
        set_display_geometry(&runtime->displays[0], 0, 0, 64, 120);
        set_display_geometry(&runtime->displays[1], 64, 0, 96, 64);
        initialize_outputs(runtime);
        if (mode != 0) {
            const char *codes[] = {"", "sdl_output_window_failed", "sdl_output_binding_failed",
                "sdl_output_fullscreen_failed", "sdl_output_renderer_failed"};
            int found = 0;
            for (i = 0; i < runtime->diagnostic_log_count; ++i)
                if (strstr(runtime->diagnostic_logs[i], codes[mode]) &&
                    strstr(runtime->diagnostic_logs[i], "display 57")) found = 1;
            if (!found || runtime->output_count || fallbacks != 0 || destroyed != created || moves ||
                !strstr(runtime->sdl_error, "requires VT ownership")) {
                fprintf(stderr, "SDL_STARTUP_FAILURE_PATH_FAIL mode=%d logs=%d windows=%d/%d\n",
                    mode, found, destroyed, created);
                return 1;
            }
            destroy_outputs(runtime); blit_font_free(&runtime->font); free(runtime);
            continue;
        }
        if (moves || properties_created != 2 || runtime->output_count != 2) {
            fprintf(stderr, "SDL_STARTUP_FAIL moves=%d creation_bindings=%d outputs=%d\n",
                moves, properties_created, runtime->output_count);
            return 1;
        }
        for (i = 0; i < 2; ++i)
            if (fixture_window_display(runtime->outputs[i].window) != runtime->displays[i].id ||
                runtime->outputs[i].display_id != runtime->displays[i].id ||
                !runtime->outputs[i].renderer ||
                strcmp(SDL_GetRendererName(runtime->outputs[i].renderer), renderer) != 0 ||
                runtime->outputs[i].render_width != runtime->displays[i].bounds.w ||
                runtime->outputs[i].render_height != runtime->displays[i].bounds.h) return 1;
        destroy_outputs(runtime); blit_font_free(&runtime->font); free(runtime);
        if (created != destroyed) return 1;
    }
    SDL_Quit();
    printf("SDL_STARTUP_PASS outputs=2 ids=11,57 creation_binding=exact moves=0 failure_paths=4 cleanup=exact renderer=%s scope=mock_display_boundary\n", renderer);
    return 0;
}
