#define _GNU_SOURCE 1
#include <SDL3/SDL.h>
static int scene_clears;
static bool scene_counted_clear(SDL_Renderer *renderer);
#define SDL_RenderClear scene_counted_clear
#define main unused_blit_server_main
#include "../../src/blit_server.c"
#undef main
#undef SDL_RenderClear
static bool scene_counted_clear(SDL_Renderer *renderer)
{ ++scene_clears; return SDL_RenderClear(renderer); }
#include "../../src/blit_graphics_scene.h"
#define CHECK(x) do { if (!(x)) { fprintf(stderr,"PLACEMENT_DAMAGE_FAIL line=%d: %s\n",__LINE__,#x); exit(1); } } while(0)
static int hits;
static void count_damage(void *owner, const BlitGraphicsStamp *stamp)
{ (void)owner; CHECK(stamp->image_id == 2); ++hits; }
static SDL_Surface *scene_pixels(BlitOutput *o)
{
    SDL_Surface *p, *rgba;
    CHECK(SDL_SetRenderTarget(o->renderer,o->scene_texture));
    p=SDL_RenderReadPixels(o->renderer,NULL);CHECK(p);
    rgba=SDL_ConvertSurface(p,SDL_PIXELFORMAT_RGBA32);CHECK(rgba);
    SDL_DestroySurface(p);CHECK(SDL_SetRenderTarget(o->renderer,NULL));return rgba;
}
static int protocol_max_damage_cells;
static void protocol_frame(BlitRuntime *r, BlitSurface *v, const char *wire)
{
    SDL_Surface *incremental, *full;
    unsigned long long before=v->graphics_presented.signature;
    int y;
    CHECK(terminal_consume_output(r,&r->sessions[0],v,
        (const unsigned char *)wire,strlen(wire)));
    CHECK(!r->sessions[0].graphics_rejected);
    CHECK(v->damage_valid && (v->damage_width<v->width || v->damage_height<v->height));
    if(v->damage_width*v->damage_height>protocol_max_damage_cells)
        protocol_max_damage_cells=v->damage_width*v->damage_height;
    ++v->revision;
    r->sessions[0].terminal_synchronized=1;
    render_current_surfaces(r,1,0);
    CHECK(v->damage_valid && v->graphics_presented.signature==before);
    r->sessions[0].terminal_synchronized=0;
    render_current_surfaces(r,0,0);
    CHECK(v->damage_valid && v->graphics_presented.signature==before);
    /* Main-loop held attempts are restricted to pointer-dirty outputs. */
    r->pointer_dirty=1;r->pointer_dirty_outputs=1;
    render_current_surfaces(r,1,1);
    CHECK(v->damage_valid && v->graphics_presented.signature==before);
    scene_clears=0;
    render_current_surfaces(r,1,0);
    CHECK(!scene_clears && !v->damage_valid);
    incremental=scene_pixels(&r->outputs[0]);
    ++r->topology_revision;mark_surface_full_damage(v);
    render_current_surfaces(r,1,0);CHECK(scene_clears);
    full=scene_pixels(&r->outputs[0]);
    CHECK(incremental->w==full->w && incremental->h==full->h);
    for(y=0;y<full->h;++y)
        CHECK(!memcmp((unsigned char *)incremental->pixels+y*incremental->pitch,
                      (unsigned char *)full->pixels+y*full->pitch,full->w*4));
    SDL_DestroySurface(incremental);SDL_DestroySurface(full);
}
static void protocol_frames(BlitRuntime *r, BlitSurface *v)
{
    char encoded[1024],wire[2048];
    int step,i;
    for(step=0;step<3;++step){
        /* 128 independently specified Base64 quartets = 384 RGBA bytes. */
        for(i=0;i<512;i+=4)memcpy(encoded+i,step==0?"4ihQ":step==1?"yihQ":"8ChQ",4);
        encoded[512]=0;
        v->cursor_x=4+step*3;v->cursor_y=3;
        snprintf(wire,sizeof(wire),"\033_Ga=T,i=10,f=32,s=8,v=12,C=1,q=2,m=1;%.256s\033\\",encoded);
        CHECK(!terminal_consume_output(r,&r->sessions[0],v,
            (const unsigned char *)wire,strlen(wire)));
        CHECK(!graphics_image_find(&r->sessions[0],10)->complete);
        snprintf(wire,sizeof(wire),"\033_Gm=0,q=2;%s\033\\",encoded+256);
        protocol_frame(r,v,wire);
        v->cursor_x+=3;
        protocol_frame(r,v,"\033_Ga=p,i=10,p=7,C=1,q=2;\033\\");
        protocol_frame(r,v,"\033_Ga=d,d=i,i=10,p=7,q=2;\033\\");
        CHECK(graphics_image_find(&r->sessions[0],10));
        protocol_frame(r,v,"\033_Ga=d,d=I,i=10,q=2;\033\\");
        CHECK(!graphics_image_find(&r->sessions[0],10));
    }
    /* Parameterized sixel, transparent and opaque, with retained wallpaper. */
    v->cursor_x=5;v->cursor_y=3;
    protocol_frame(r,v,"\033P0;1q\"1;1;8;6#1;2;100;0;0!8~\033\\");
    v->cursor_x=10;v->cursor_y=4;
    protocol_frame(r,v,"\033P0;2q\"1;1;8;6#1;2;0;100;0!8~\033\\");
    for(i=0;i<BLIT_GRAPHICS_MAX_IMAGES;++i)
        if(r->sessions[0].graphics[i].used &&
           !strcmp(r->sessions[0].graphics[i].protocol,"sixel")){
            snprintf(wire,sizeof(wire),"\033_Ga=d,d=I,i=%u,q=2;\033\\",r->sessions[0].graphics[i].id);
            protocol_frame(r,v,wire);
        }
    printf("GRAPHICS_PROTOCOL_PASS frames=16 max_producer_damage_cells=%d full_cells=480 chunked_upload=hidden_until_complete placement_delete=bounded sixel=opaque_transparent full_reference=exact\n",protocol_max_damage_cells);
}
static void sdl_scene_gate(void)
{
    BlitRuntime *r=calloc(1,sizeof(*r));
    BlitSurface *v=calloc(1,sizeof(*v));
    BlitGraphicsImage *images;
    unsigned char wallpaper[320*192*4], window[32*24*4], overlap[32*24*4];
    const char *name=getenv("BLIT_TEST_RENDERER");
    int i,x,y,step;
    CHECK(r&&v);if(!name)name="software";
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    CHECK(blit_font_load_path(&r->font,"assets/fonts/Blit-Hibr-Terminus16.psf")==0);
    r->font_scale_tenths=10;r->output_count=r->display_count=1;
    r->outputs[0].window=SDL_CreateWindow("scene-damage",320,192,SDL_WINDOW_HIDDEN);
    CHECK(r->outputs[0].window);
    r->outputs[0].renderer=SDL_CreateRenderer(r->outputs[0].window,name);CHECK(r->outputs[0].renderer);
    CHECK(!strcmp(SDL_GetRendererName(r->outputs[0].renderer),name));
    r->outputs[0].display_id=r->displays[0].id=1;
    r->outputs[0].render_width=320;r->outputs[0].render_height=192;
    set_display_geometry(&r->displays[0],0,0,320,192);
    r->sessions[0].used=1;r->sessions[0].id=1;r->sessions[0].pty_fd=-1;
    strcpy(r->sessions[0].display_id,"display-1");
    v->used=1;v->id=v->owner_session_id=1;v->revision=1;v->width=40;v->height=12;
    r->surfaces[0]=v;
    for(y=0;y<12;++y)for(x=0;x<40;++x)v->codepoints[y][x]=' ';
    for(i=0;i<320*192;++i){wallpaper[i*4]=i%251;wallpaper[i*4+1]=(i/320)%251;wallpaper[i*4+2]=35;wallpaper[i*4+3]=255;}
    for(i=0;i<32*24;++i){window[i*4]=230;window[i*4+1]=40;window[i*4+2]=90;window[i*4+3]=128;
        overlap[i*4]=30;overlap[i*4+1]=80;overlap[i*4+2]=220;overlap[i*4+3]=255;}
    images=r->sessions[0].graphics;
    for(i=0;i<3;++i){images[i].used=images[i].complete=images[i].visible=1;images[i].id=i+1;images[i].format=32;images[i].generation=i+1;}
    images[0].pixels=wallpaper;images[0].width=320;images[0].height=192;images[0].z=-1073741825;images[0].opaque=1;
    images[1].pixels=window;images[1].width=32;images[1].height=24;images[1].x=8;images[1].y=32;
    images[2].pixels=overlap;images[2].width=32;images[2].height=24;images[2].x=56;images[2].y=48;images[2].opaque=1;
    mark_surface_full_damage(v);render_current_surfaces(r,1,0);
    CHECK(v->graphics_presented.valid);
    for(step=0;step<36;++step){
        SDL_Surface *incremental,*full;
        unsigned long long uploads=r->outputs[0].graphics_texture_uploads;
        images[1].x=(step*7)%264;images[1].y=32+(step%3)*13;
        if(step==30)images[1].z=-1;
        if(step==31)images[1].src_x=3;
        if(step==32){++images[1].generation;window[0]=20;}
        if(step==33)images[1].visible=0;
        if(step==34)images[1].visible=1;
        if(step==35){images[1].x=-7;images[1].y=-5;}
        ++v->revision;scene_clears=0;
        /* No producer damage: the shared comparison must cover it. */
        render_current_surfaces(r,1,0);
        CHECK(!scene_clears);CHECK(!v->damage_valid);
        CHECK(r->outputs[0].graphics_texture_uploads==uploads+(step==32));
        CHECK(v->graphics_presented.signature==v->graphics_prepared.signature);
        incremental=scene_pixels(&r->outputs[0]);
        ++r->topology_revision;mark_surface_full_damage(v);
        render_current_surfaces(r,1,0);CHECK(scene_clears);
        full=scene_pixels(&r->outputs[0]);
        CHECK(incremental->w==full->w&&incremental->h==full->h);
        for(y=0;y<full->h;++y)
            CHECK(!memcmp((unsigned char*)incremental->pixels+y*incremental->pitch,
                          (unsigned char*)full->pixels+y*full->pitch,full->w*4));
        SDL_DestroySurface(incremental);SDL_DestroySurface(full);
    }
    /* A held synchronized frame cannot acknowledge future scene pixels. */
    { unsigned long long before=v->graphics_presented.signature;
      r->sessions[0].terminal_synchronized=1;images[1].x+=8;++v->revision;
      render_current_surfaces(r,1,0);CHECK(v->graphics_presented.signature==before);
      r->sessions[0].terminal_synchronized=0;render_current_surfaces(r,1,0);
      CHECK(v->graphics_presented.signature!=before); }
    { unsigned long long before=v->graphics_presented.signature;
      images[1].x+=8;++v->revision;
      render_current_surfaces(r,0,0);
      CHECK(v->graphics_presented.signature==before&&v->damage_valid);
      render_current_surfaces(r,1,0);CHECK(v->graphics_presented.signature!=before); }
    protocol_frames(r,v);
    destroy_outputs(r);blit_font_free(&r->font);free(v);free(r);SDL_Quit();
    printf("GRAPHICS_SDL_PASS renderer=%s steps=36 full_reference=exact clears=0 sync=held wallpaper_alpha_z_crop_pixels_delete=covered\n",name);
}
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
    sdl_scene_gate();
    puts("PLACEMENT_DAMAGE_PASS shared_snapshot=move,pixels,z,crop,opacity,delete,add wallpaper=unchanged protocol=old_new_union");
    return 0;
}
