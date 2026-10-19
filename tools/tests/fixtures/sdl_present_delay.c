/* Test-only public SDL boundary: retain the actual renderer but make present
   take 16ms, reproducing control-loop pressure without physical scanout claims. */
#define _GNU_SOURCE
#include <SDL3/SDL.h>
#include <dlfcn.h>
#include <fcntl.h>
#include <stdlib.h>
#include <time.h>
#include <unistd.h>

bool SDL_RenderPresent(SDL_Renderer *renderer)
{
    static bool (*actual)(SDL_Renderer *);
    const char *arm = getenv("BLIT_TEST_PRESENT_ARM");
    const char *entered = getenv("BLIT_TEST_PRESENT_ENTERED");
    struct timespec delay = {0, 16000000};
    if (!actual) actual = (bool (*)(SDL_Renderer *))dlsym(RTLD_NEXT,"SDL_RenderPresent");
    if (!actual) return false;
    if (arm && entered && access(arm,F_OK) == 0) {
        struct timespec pause = {0,1000000};
        int i, fd = open(entered,O_WRONLY|O_CREAT|O_TRUNC,0600);
        if (fd >= 0) { write(fd,"1",1); close(fd); }
        for (i = 0; i < 3000 && access(arm,F_OK) == 0; ++i)
            nanosleep(&pause,NULL);
    }
    nanosleep(&delay,NULL);
    return actual(renderer);
}
