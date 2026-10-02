#include <SDL3/SDL.h>

int main(void)
{
    int version = SDL_GetVersion();

    SDL_Log("SDL %d.%d.%d", SDL_VERSIONNUM_MAJOR(version),
            SDL_VERSIONNUM_MINOR(version), SDL_VERSIONNUM_MICRO(version));

    if (!SDL_Init(SDL_INIT_VIDEO)) {
        SDL_Log("SDL_Init failed: %s", SDL_GetError());
        return 1;
    }

    SDL_Quit();
    return 0;
}
