#define _GNU_SOURCE 1
#define main unused_blit_server_main
#include "../../src/blit_server.c"
#undef main
#include "../../src/blit_graphics_scene.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"PLACEMENT_DAMAGE_FAIL line=%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int hits;
static void count_damage(void *owner, const BlitGraphicsStamp *stamp)
{ (void)owner; CHECK(stamp->image_id == 2); ++hits; }
int main(void)
{
    BlitSession *s = calloc(1, sizeof(*s));
    BlitSurface *v = calloc(1, sizeof(*v));
    BlitGraphicsScene a, b;
    BlitGraphicsImage *image;
    unsigned char pixels[16] = {0};
    int i;
    CHECK(s && v);
    v->width = 80; v->height = 40;
    s->terminal_cell_width = 8; s->terminal_cell_height = 16; s->pty_fd = -1;
    image = &s->graphics[0];
    image->used = image->complete = image->visible = 1; image->id = 1;
    image->width = 640; image->height = 640; image->format = 32;
    image->pixels = pixels; image->generation = 1;
    image->placement_width = 640; image->placement_height = 640;
    image->z = -1073741825;
    image = &s->graphics[1];
    image->used = image->complete = image->visible = 1; image->id = 2;
    image->width = 16; image->height = 32; image->format = 32;
    image->pixels = pixels; image->generation = 2;
    image->x = 16; image->y = 32;
    strcpy(image->protocol, "kitty");
    blit_graphics_scene_build(&a, s->graphics, 8);
    for (i = 0; i < 7; ++i) {
        b = a;
        switch (i) {
        case 0: image->x += 8; break;
        case 1: ++image->generation; break;
        case 2: image->z = -1; break;
        case 3: image->src_x = 1; break;
        case 4: image->opaque = 1; break;
        case 5: image->visible = 0; break;
        case 6: image->visible = 1; break;
        }
        blit_graphics_scene_build(&b, s->graphics, 8);
        hits = 0;
        CHECK(blit_graphics_scene_delta(&a, &b, count_damage, NULL));
        CHECK(hits == (i == 5 || i == 6 ? 1 : 2));
        CHECK(a.signature != b.signature);
        hits = 0; CHECK(!blit_graphics_scene_delta(&b, &b, count_damage, NULL)); CHECK(!hits);
        a = b;
    }
    image->src_x = 0; image->x = 16; image->y = 32;
    v->cursor_x = 6; v->cursor_y = 5;
    strcpy(s->terminal_string, "Ga=p,i=2,C=1,q=2;");
    s->terminal_string_length = strlen(s->terminal_string);
    terminal_apply_kitty(s, v);
    CHECK(!s->graphics_rejected);
    CHECK(v->damage_valid && v->damage_x == 2 && v->damage_y == 2 &&
          v->damage_width == 6 && v->damage_height == 5);
    CHECK(image->generation == 3);
    /* Further moves accumulate until presentation, and unrelated cell damage
       must never be dropped by a later image placement. */
    mark_surface_damage(v, 79, 39);
    v->cursor_x = 10; v->cursor_y = 7;
    strcpy(s->terminal_string, "Ga=p,i=2,C=1,q=2;");
    s->terminal_string_length = strlen(s->terminal_string);
    terminal_apply_kitty(s, v);
    CHECK(v->damage_x == 2 && v->damage_y == 2 && v->damage_width == 78 && v->damage_height == 38);
    free(v); free(s);
    puts("PLACEMENT_DAMAGE_PASS shared_snapshot=move,pixels,z,crop,opacity,delete,add wallpaper=unchanged protocol=old_new_union");
    return 0;
}
