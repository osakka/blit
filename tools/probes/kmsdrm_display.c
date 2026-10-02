#include <SDL3/SDL.h>
#include <stdlib.h>

int main(void)
{
    int count = 0;
    SDL_DisplayID *displays;
    SDL_Window *window;
    SDL_Event event;
    int running = 1;
    int seconds = 0;

    SDL_SetHint(SDL_HINT_KMSDRM_REQUIRE_DRM_MASTER, "1");

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    displays = SDL_GetDisplays(&count);
    if (displays == NULL || count == 0) {
        SDL_Log("No displays found: %s", SDL_GetError());
        SDL_Quit();
        return 2;
    }

    SDL_Log("Found %d display(s)", count);
    for (int i = 0; i < count; ++i) {
        SDL_Rect bounds;
        const char *name = SDL_GetDisplayName(displays[i]);

        if (SDL_GetDisplayBounds(displays[i], &bounds)) {
            SDL_Log("display[%d] id=%llu name=%s bounds=%d,%d %dx%d", i,
                    (unsigned long long)displays[i], name ? name : "?",
                    bounds.x, bounds.y, bounds.w, bounds.h);
        }
    }
    SDL_free(displays);

    if (getenv("BLIT_KMSDRM_ENUM_ONLY") != NULL) {
        SDL_Quit();
        return 0;
    }

    window = SDL_CreateWindow("blit KMSDRM probe", 640, 480,
                              SDL_WINDOW_FULLSCREEN);
    if (window == NULL) {
        SDL_Log("SDL_CreateWindow failed: %s", SDL_GetError());
        SDL_Quit();
        return 3;
    }

    SDL_Log("KMSDRM window created; press Escape or wait five seconds");
    while (running && seconds < 5000) {
        while (SDL_PollEvent(&event)) {
            if (event.type == SDL_EVENT_QUIT ||
                (event.type == SDL_EVENT_KEY_DOWN &&
                 event.key.key == SDLK_ESCAPE)) {
                running = 0;
            }
        }
        SDL_Delay(50);
        seconds += 50;
    }

    SDL_DestroyWindow(window);
    SDL_Quit();
    return 0;
}
