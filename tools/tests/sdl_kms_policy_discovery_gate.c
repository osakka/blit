/* Actual Blit discovery: mock only the video inventory boundary, never DRM. */
#define _GNU_SOURCE 1
#include <SDL3/SDL.h>
static bool fixture_init(SDL_InitFlags);
static const char *fixture_driver(void);
static SDL_DisplayID *fixture_displays(int *);
static void fixture_quit(SDL_InitFlags);
#define SDL_Init fixture_init
#define SDL_GetCurrentVideoDriver fixture_driver
#define SDL_GetDisplays fixture_displays
#define SDL_QuitSubSystem fixture_quit
#define main blit_server_entry_for_kms_policy_gate
#include "../../src/blit_server.c"
#undef main
#undef SDL_Init
#undef SDL_GetCurrentVideoDriver
#undef SDL_GetDisplays
#undef SDL_QuitSubSystem
static int initialized, enumerated, quit, atomic_at_init;
static const char *driver="kmsdrm";
static bool fixture_init(SDL_InitFlags flags)
{ (void)flags; ++initialized; atomic_at_init=SDL_GetHintBoolean(SDL_HINT_KMSDRM_ATOMIC,true); return true; }
static const char *fixture_driver(void) { return driver; }
static SDL_DisplayID *fixture_displays(int *count)
{ ++enumerated; *count=0; return SDL_calloc(1,sizeof(SDL_DisplayID)); }
static void fixture_quit(SDL_InitFlags flags) { (void)flags; ++quit; }
int main(int argc,char **argv)
{
    BlitRuntime *runtime=calloc(1,sizeof(*runtime));
    int conflict=argc==2 && !strcmp(argv[1],"conflict");
    int other=argc==2 && !strcmp(argv[1],"other");
    if (!runtime) return 1;
    if (other) driver="offscreen";
    discover_displays(runtime);
    if (initialized!=1 || atomic_at_init!=(conflict || other)) return 1;
    if (conflict) {
        if (enumerated || quit!=1 || runtime->sdl_ready ||
            !strstr(runtime->sdl_error,"secondary display-plane")) return 1;
    } else if (enumerated!=1 || quit || !runtime->sdl_ready) return 1;
    free(runtime);
    puts("SDL_KMS_POLICY_DISCOVERY_PASS ordering=before_init explicit_conflict=before_enumeration scope=actual_discovery_mock_video_no_DRM");
    return 0;
}
