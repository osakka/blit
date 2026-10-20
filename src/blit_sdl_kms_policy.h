/* Compatibility boundary for the exact pinned SDL build, ADR0155.
   Use SDL's public hint, never its private driver structs or a vendor patch. */
static int blit_sdl_kms_policy_affected(void)
{ return SDL_GetVersion() == SDL_VERSIONNUM(3,4,16); }
static void blit_sdl_kms_policy_prepare(void)
{
    if (blit_sdl_kms_policy_affected())
        SDL_SetHint(SDL_HINT_KMSDRM_ATOMIC,"0");
}
static const char *blit_sdl_kms_policy_error(void)
{
    const char *driver=SDL_GetCurrentVideoDriver();
    if (blit_sdl_kms_policy_affected() && driver && !strcmp(driver,"kmsdrm") &&
        SDL_GetHintBoolean(SDL_HINT_KMSDRM_ATOMIC,true))
        return "Pinned SDL3.4.16 atomic KMS has uninitialized secondary display-plane state; remove SDL_KMSDRM_ATOMIC=1 to use the documented legacy path.";
    return NULL;
}
static const char *blit_sdl_kms_mode_json(const char *driver)
{
    if (!driver || strcmp(driver,"kmsdrm")) return "null";
    return SDL_GetHintBoolean(SDL_HINT_KMSDRM_ATOMIC,true) ? "\"atomic\"" : "\"legacy\"";
}
