/* Architecture observation only: invokes production functions without DRM,
   Hibr or network access. Reports current work, not desired behaviour/FPS. */
#ifdef BLIT_PROBE_NATIVE
#include "../../src/blit_kms.c"
int main(void)
{
    BlitKmsSurfaceView view = {0};
    BlitGraphicsImage images[2] = {{0}};
    KmsOutput output = {0};
    int i, x, y, w, h;
    size_t bytes = 1920U * 2160U * 4U;
    uint32_t *scene = calloc(1, bytes), *back = calloc(1, bytes);
    if (!scene || !back) return 2;
    for (i = 0; i < 2; ++i) {
        images[i].used = images[i].complete = images[i].visible = 1;
        images[i].id = i + 1;
        images[i].pixels = (unsigned char *)scene;
        images[i].format = 32;
    }
    images[0].width = images[0].placement_width = 1920;
    images[0].height = images[0].placement_height = 2160;
    images[0].z = -1073741825;
    images[1].width = images[1].placement_width = 320;
    images[1].height = images[1].placement_height = 180;
    images[1].x = 400; images[1].y = 400;
    view.columns = 240; view.rows = 135;
    view.cell_width = 8; view.cell_height = 16;
    view.graphics = images; view.graphics_count = 2;
    if (!graphics_bounds_for_view(&view, &x, &y, &w, &h)) return 3;
    printf("{\"probe\":\"native\",\"view_pixels\":[1920,2160],"
           "\"small_image_pixels\":[320,180],\"all_graphics_cell_bounds\":[%d,%d,%d,%d],",
           x, y, w, h);
    output.mode.hdisplay = 1920; output.mode.vdisplay = 2160;
    output.back.map = back; output.back.pitch = 1920 * 4;
    output.scene = (unsigned char *)scene;
    scene[0] = 0x123456; scene[1920U * 2160U - 1] = 0x654321;
    copy_scene_to_back(&output);
    if (back[0] != scene[0] || back[1920U * 2160U - 1] != scene[1920U * 2160U - 1]) return 4;
    printf("\"scene_copy_bytes\":%lu,\"outside_small_image_copied\":true}\n", (unsigned long)bytes);
    free(scene); free(back);
    return 0;
}
#else
#define main blit_architecture_probe_unused_server_main
#include "../../src/blit_server.c"
#undef main
int main(void)
{
    BlitSession *session = calloc(1, sizeof(*session));
    BlitSurface *surface = calloc(1, sizeof(*surface));
    BlitOutput output = {0};
    BlitGraphicsImage *image;
    SDL_Window *window;
    if (!session || !surface) return 2;
    surface->width = 240; surface->height = 135;
    surface->cursor_x = 50; surface->cursor_y = 25;
    session->terminal_cell_width = 8; session->terminal_cell_height = 16;
    session->pty_fd = -1;
    image = &session->graphics[0];
    image->used = image->complete = 1; image->id = 7;
    image->width = 320; image->height = 180; image->format = 32;
    image->generation = 42;
    strcpy(image->protocol, "kitty");
    image->pixels = calloc(320U * 180U, 4);
    if (!image->pixels) return 2;
    strcpy(session->terminal_string, "Ga=p,i=7,p=9,C=1,q=2,c=40;");
    session->terminal_string_length = strlen(session->terminal_string);
    terminal_apply_kitty(session, surface);
    /* Move the SAME existing placement without uploading image pixels. */
    surface->damage_valid = 0;
    surface->cursor_x = 70; surface->cursor_y = 35;
    strcpy(session->terminal_string, "Ga=p,i=7,p=9,C=1,q=2,c=40;");
    session->terminal_string_length = strlen(session->terminal_string);
    terminal_apply_kitty(session, surface);
    if (session->graphics_rejected || image->generation != 42) return 3;
    if (!surface->damage_valid || image->placements[0].x != 560 ||
        image->placements[0].y != 560) return 3;
    printf("{\"probe\":\"shared_protocol_sdl\",\"placement_only_damage_cells\":[%d,%d,%d,%d],"
           "\"existing_placement_moved\":true,\"pixel_generation_unchanged\":true,", surface->damage_x, surface->damage_y,
           surface->damage_width, surface->damage_height);
    if (!SDL_Init(SDL_INIT_VIDEO)) return 4;
    window = SDL_CreateWindow("architecture probe", 64, 64, SDL_WINDOW_HIDDEN);
    if (!window) return 4;
    output.renderer = SDL_CreateRenderer(window, "software");
    if (!output.renderer || !sdl_graphics_texture(&output, image)) return 4;
    image->placements[0].x += 8;
    if (!sdl_graphics_texture(&output, image) || output.graphics_texture_uploads != 1) return 5;
    ++image->generation;
    if (!sdl_graphics_texture(&output, image) || output.graphics_texture_uploads != 2) return 5;
    printf("\"sdl_uploads_initial_plus_move\":1,\"sdl_uploads_after_new_pixels\":2,"
           "\"scope\":\"software resource-policy fixture, not GPU performance\"}\n");
    destroy_sdl_graphics_cache(&output);
    SDL_DestroyRenderer(output.renderer); SDL_DestroyWindow(window); SDL_Quit();
    free(image->pixels); free(session); free(surface);
    return 0;
}
#endif
