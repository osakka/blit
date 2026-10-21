#include "../../src/blit_kms.c"
#include "../../src/blit_graphics_scene.h"
#include "fixtures/drm_hotplug_mock.c"
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"GRAPHICS_NATIVE_FAIL line=%d %s\n",__LINE__,#c); exit(1); } } while(0)
static void damaged(void *owner, const BlitGraphicsStamp *s)
{
    BlitKmsSurfaceView *v = owner;
    int x = graphics_floor_div(s->x, 8), y = graphics_floor_div(s->y, 16);
    int r = graphics_ceil_div(s->x + s->width, 8), b = graphics_ceil_div(s->y + s->height, 16);
    blit_damage_include(&v->damage_valid, &v->damage_x, &v->damage_y,
        &v->damage_width, &v->damage_height, x, y, r-x, b-y);
}
static void complete(BlitKms *kms)
{
    drmEventContext c = {0}; c.version = DRM_EVENT_CONTEXT_VERSION;
    c.page_flip_handler = kms_flip_event;
    CHECK(drmHandleEvent(kms->fd, &c) == 0);
}
static void present(BlitKms *kms, BlitKmsSurfaceView *view, unsigned long long visual)
{
    uint32_t submitted = 0, deferred = 0;
    CHECK(blit_kms_present(kms,view,1,0,0,0,1,NULL,0,visual,0,0,0,0,&submitted,&deferred)==0);
    CHECK(submitted == 1 && !deferred);
    complete(kms);
}
int main(int argc, char **argv)
{
    char state[512], trace[512], device[512]; FILE *file;
    BlitKms *kms; KmsSnapshot hardware;
    BlitKmsSurfaceView v = {0};
    BlitGraphicsImage images[3] = {{0}};
    BlitGraphicsScene old, next;
    uint32_t cells[40*12] = {0}, reference[320*192];
    uint16_t attributes[40*12] = {0};
    unsigned char wallpaper[320*192*4], window[32*24*4], overlap[32*24*4];
    unsigned long long visual = 1;
    int i, step, row;
    CHECK(argc == 2);
    snprintf(state,sizeof(state),"%s/state",argv[1]);
    snprintf(trace,sizeof(trace),"%s/trace",argv[1]);
    snprintf(device,sizeof(device),"%s/device",argv[1]);
    setenv("BLIT_DRM_MOCK_STATE",state,1); setenv("BLIT_DRM_MOCK_TRACE",trace,1);
    file=fopen(state,"w"); CHECK(file); fprintf(file,"1 0 0 0\n93 320 192\n"); fclose(file);
    kms=blit_kms_new(NULL,NULL); CHECK(kms);
    kms->fd=open(device,O_RDWR|O_CREAT|O_TRUNC,0600); CHECK(kms->fd>=0);
    CHECK(kms_read_snapshot(kms,&hardware)==0); kms_reconcile_snapshot(kms,&hardware);
    CHECK(kms->output_count == 1);
    for (i=0;i<320*192;++i) {
        wallpaper[i*4]=i%251; wallpaper[i*4+1]=(i/320)%251;
        wallpaper[i*4+2]=35; wallpaper[i*4+3]=255;
    }
    for(i=0;i<32*24;++i) {
        window[i*4]=230; window[i*4+1]=40; window[i*4+2]=90; window[i*4+3]=128;
        overlap[i*4]=30; overlap[i*4+1]=80; overlap[i*4+2]=220; overlap[i*4+3]=255;
    }
    for(i=0;i<3;++i) {
        images[i].used=images[i].complete=images[i].visible=1;
        images[i].id=i+1; images[i].format=32; images[i].generation=i+1;
    }
    images[0].width=320;images[0].height=192;images[0].pixels=wallpaper;
    images[0].z=-1073741825;images[0].opaque=1;
    images[1].width=32;images[1].height=24;images[1].pixels=window;
    images[1].x=8;images[1].y=32;
    images[2].width=32;images[2].height=24;images[2].pixels=overlap;
    images[2].x=56;images[2].y=48;images[2].opaque=1;
    v.surface_id=9;v.surface_revision=1;v.display_id=kms->outputs[0].info.id;
    v.columns=v.stride=40;v.rows=12;v.cell_width=8;v.cell_height=16;
    v.codepoints=cells;v.foreground=cells;v.background=cells;
    v.attributes=attributes;
    v.graphics=images;v.graphics_count=3;v.graphics_damage_complete=1;
    v.damage_valid=1;v.damage_width=40;v.damage_height=12;
    blit_graphics_scene_build(&old,images,3);v.graphics_signature=old.signature;
    present(kms,&v,visual);
    for(step=0;step<36;++step) {
        images[1].x=(step*7)%264; images[1].y=32+(step%3)*13;
        if(step==30) images[1].z=-1;
        if(step==31) images[1].src_x=3;
        if(step==32) { ++images[1].generation; window[0]=20; }
        if(step==33) images[1].visible=0;
        if(step==34) images[1].visible=1;
        if(step==35) { images[1].x=-7; images[1].y=-5; }
        v.damage_valid=0;
        blit_graphics_scene_build(&next,images,3);
        CHECK(blit_graphics_scene_delta(&old,&next,damaged,&v));
        v.graphics_signature=next.signature;++v.surface_revision;
        present(kms,&v,visual);
        CHECK(kms->outputs[0].info.drawn_cells < 40*12);
        for(row=0;row<192;++row)
            memcpy(reference+row*320,(unsigned char*)kms->outputs[0].front.map+row*kms->outputs[0].front.pitch,320*4);
        v.damage_valid=1;v.damage_x=v.damage_y=0;v.damage_width=40;v.damage_height=12;
        ++v.surface_revision;
        present(kms,&v,++visual);
        for(row=0;row<192;++row)
            CHECK(!memcmp(reference+row*320,(unsigned char*)kms->outputs[0].front.map+row*kms->outputs[0].front.pitch,320*4));
        old=next;
    }
    /* Unknown callers retain all-image fallback. */
    v.graphics_damage_complete=0;v.graphics_signature^=1;++v.surface_revision;
    v.damage_valid=1;v.damage_x=1;v.damage_y=1;v.damage_width=v.damage_height=1;
    present(kms,&v,visual);CHECK(kms->outputs[0].info.drawn_cells==40*12);
    /* An in-flight flip must defer, not consume pending graphics damage. */
    {
        uint32_t submitted=0,deferred=0;int consumed=0;
        v.graphics_damage_complete=1;v.damage_consumed=&consumed;
        ++v.surface_revision;v.graphics_signature^=1;
        CHECK(blit_kms_present(kms,&v,1,0,0,0,1,NULL,0,visual,0,0,0,0,&submitted,&deferred)==0);
        CHECK(submitted==1&&consumed);
        consumed=0;submitted=deferred=0;++v.surface_revision;
        CHECK(blit_kms_present(kms,&v,1,0,0,0,1,NULL,0,visual,0,0,0,0,&submitted,&deferred)==0);
        CHECK(!submitted&&deferred==1&&!consumed);
        complete(kms);present(kms,&v,visual);CHECK(consumed);
    }
    blit_kms_close(kms);
    puts("GRAPHICS_NATIVE_PASS steps=36 full_reference=exact wallpaper=unchanged alpha=128 z_crop_pixels_delete=covered damage=bounded");
    return 0;
}
