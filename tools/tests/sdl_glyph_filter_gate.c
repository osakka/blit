#include <SDL3/SDL.h>
#include <stdio.h>

int main(void)
{
    SDL_Window *window;
    SDL_Renderer *renderer;
    SDL_Texture *texture;
    SDL_ScaleMode mode = SDL_SCALEMODE_INVALID;

    if (!SDL_Init(SDL_INIT_VIDEO)) return 1;
    window = SDL_CreateWindow("blit glyph filter gate", 16, 16,
                              SDL_WINDOW_HIDDEN);
    if (window == NULL) { SDL_Quit(); return 2; }
    renderer = SDL_CreateRenderer(window, "software");
    if (renderer == NULL) {
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 2;
    }
    texture = SDL_CreateTexture(renderer, SDL_PIXELFORMAT_RGBA32,
                                SDL_TEXTUREACCESS_STATIC, 16, 16);
    if (texture == NULL || !SDL_SetTextureScaleMode(texture,
                                                     SDL_SCALEMODE_NEAREST) ||
        !SDL_GetTextureScaleMode(texture, &mode) ||
        mode != SDL_SCALEMODE_NEAREST) {
        if (texture != NULL) SDL_DestroyTexture(texture);
        SDL_DestroyRenderer(renderer);
        SDL_DestroyWindow(window);
        SDL_Quit();
        return 3;
    }
    SDL_DestroyTexture(texture);
    SDL_DestroyRenderer(renderer);
    SDL_DestroyWindow(window);
    SDL_Quit();
    puts("SDL_GLYPH_FILTER_PASS mode=nearest");
    return 0;
}
