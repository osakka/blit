#define _GNU_SOURCE 1
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
static int version=SDL_VERSIONNUM(3,4,16);
static const char *driver="kmsdrm";
static int fixture_version(void) { return version; }
static const char *fixture_driver(void) { return driver; }
#define SDL_GetVersion fixture_version
#define SDL_GetCurrentVideoDriver fixture_driver
#include "../../src/blit_sdl_kms_policy.h"
#undef SDL_GetVersion
#undef SDL_GetCurrentVideoDriver
#define CHECK(x) do{if(!(x)){fprintf(stderr,"KMS_POLICY_FAIL line=%d %s\n",__LINE__,#x);return 1;}}while(0)
int main(int argc,char **argv)
{
    CHECK(SDL_GetVersion()==SDL_VERSIONNUM(3,4,16));
    if (argc==2 && !strcmp(argv[1],"conflict")) {
        blit_sdl_kms_policy_prepare();
        CHECK(blit_sdl_kms_policy_error());
        CHECK(!strcmp(blit_sdl_kms_mode_json(driver),"\"atomic\""));
        driver="offscreen";
        CHECK(!blit_sdl_kms_policy_error() && !strcmp(blit_sdl_kms_mode_json(driver),"null"));
        puts("SDL_KMS_POLICY_CONFLICT_PASS explicit_atomic=refused other_driver=unchanged");
        return 0;
    }
    blit_sdl_kms_policy_prepare();
    CHECK(!SDL_GetHintBoolean(SDL_HINT_KMSDRM_ATOMIC,true));
    CHECK(!blit_sdl_kms_policy_error());
    CHECK(!strcmp(blit_sdl_kms_mode_json(driver),"\"legacy\""));
    CHECK(!strcmp(blit_sdl_kms_mode_json("direct-kms"),"null"));
    SDL_ResetHint(SDL_HINT_KMSDRM_ATOMIC);
    version=SDL_VERSIONNUM(3,4,17); driver="kmsdrm";
    blit_sdl_kms_policy_prepare();
    CHECK(SDL_GetHint(SDL_HINT_KMSDRM_ATOMIC)==NULL && !blit_sdl_kms_policy_error());
    puts("SDL_KMS_POLICY_PASS pinned=legacy explicit_atomic=refused other_driver=unchanged native=not_applicable other_version=unchanged scope=real_hints_mock_identity");
    return 0;
}
