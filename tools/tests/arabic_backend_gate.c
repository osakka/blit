/* Real composition and incremental-vs-clean frames. No physical scanout. */
#ifdef BLIT_TEST_NATIVE
#include "../../src/blit_kms.c"
#else
#define main blit_server_for_arabic_gate
#include "../../src/blit_server.c"
#undef main
#endif
#include "../../src/blit_raster.h"

#define W 704
#define H 512
#define COLS 18
#define ROWS 7
#define CHECK(x) do { if(!(x)) { fprintf(stderr,"ARABIC_BACKEND_FAIL line=%d\n",__LINE__); exit(1); } } while(0)
static unsigned pixels[W*H], saved[W*H], oracle[W*H];
static unsigned cps[ROWS][COLS], marks[ROWS][COLS][BLIT_MAX_COMBINING];
static unsigned char counts[ROWS][COLS];
static int ox,oy,cw,ch;
static int cursor_visible,cursor_column,cursor_row;
static int wallpaper_active;
static unsigned char wallpaper_rgb[12]={0x31,0x52,0x73,0x31,0x52,0x73,0x31,0x52,0x73,0x31,0x52,0x73};
static BlitGraphicsImage wallpaper;
static BlitFont *font;
#ifdef BLIT_TEST_NATIVE
static KmsOutput output;
static BlitKmsSurfaceView view;
#else
static BlitRuntime *r;
static BlitSurface *surface;
#endif

static void frame(int full,int x,int y,int width,int height)
{
#ifdef BLIT_TEST_NATIVE
    if(full) memset(pixels,0,sizeof(pixels));
    view.damage_valid=!full; view.damage_x=x; view.damage_y=y;
    view.damage_width=width; view.damage_height=height;
    view.cursor_visible=cursor_visible; view.cursor_x=cursor_column; view.cursor_y=cursor_row;
    draw_view(&output,font,NULL,&view);
#else
    int row,col;
    for(row=0;row<ROWS;++row) for(col=0;col<COLS;++col) {
        surface->codepoints[row][col]=cps[row][col];
        surface->combining_count[row][col]=counts[row][col];
        memcpy(surface->combining[row][col],marks[row][col],sizeof(marks[row][col]));
    }
    if(full) r->outputs[0].initialized=0;
    surface->cursor_visible=cursor_visible; surface->cursor_x=cursor_column; surface->cursor_y=cursor_row;
    r->sessions[0].graphics[0]=wallpaper;
    r->sessions[0].graphics[0].used=wallpaper_active;
    surface->damage_valid=1; surface->damage_x=x; surface->damage_y=y;
    surface->damage_width=width; surface->damage_height=height;
    ++surface->revision; ++r->frame_revision;
    r->screenshot_requested_mask=1;
    render_current_surfaces(r,1,0);
    CHECK(r->outputs[0].screenshot_surface);
    for(row=0;row<H;++row) for(col=0;col<W;++col) {
        Uint8 red,green,blue,alpha;
        CHECK(SDL_ReadSurfacePixel(r->outputs[0].screenshot_surface,col,row,&red,&green,&blue,&alpha));
        pixels[row*W+col]=(red<<16)|(green<<8)|blue;
    }
    CHECK(!surface->damage_valid);
#endif
}

static void paint_reference(int cursor)
{
    int row,col,x,y;
    for(row=0;row<ROWS;++row) for(col=0;col<COLS;++col) if(cps[row][col]) {
        BlitCellCluster key;
        BlitRasterGlyph g;
        CHECK(blit_cell_cluster_init(&key,cps[row][col],marks[row][col],counts[row][col],1));
        CHECK(font->raster_cluster_get(font->raster_state,&key,&g));
        for(y=0;y<g.height;++y) for(x=0;x<g.width;++x) {
            int dx=ox+col*cw+g.ink_offset_x+x;
            int dy=oy+row*ch+g.ink_offset_y+y;
            int inside_cursor=dx>=ox+cursor_column*cw&&dx<ox+(cursor_column+1)*cw&&
                dy>=oy+cursor_row*ch&&dy<oy+(cursor_row+1)*ch;
            if(dx>=ox&&dx<ox+COLS*cw&&dy>=oy&&dy<oy+ROWS*ch&&(!cursor||inside_cursor))
                oracle[dy*W+dx]=blit_raster_blend(&g,x,y,cursor?0x123456:0xabcdef,oracle[dy*W+dx]);
        }
    }
}

static void reference(void)
{
    int x,y;
    memset(oracle,0,sizeof(oracle));
    for(y=oy;y<oy+ROWS*ch;++y) for(x=ox;x<ox+COLS*cw;++x)
        oracle[y*W+x]=wallpaper_active?0x315273:0x123456;
    paint_reference(0);
    if(cursor_visible) {
        for(y=oy+cursor_row*ch;y<oy+(cursor_row+1)*ch;++y)
            for(x=ox+cursor_column*cw;x<ox+(cursor_column+1)*cw;++x)
                oracle[y*W+x]=
#ifdef BLIT_TEST_NATIVE
                    KMS_WHITE;
#else
                    BLIT_DEFAULT_FOREGROUND;
#endif
        paint_reference(1);
    }
}

static void compare(const unsigned *expected,int tolerance,int size,int step,const char *kind)
{
    int i,shift;
    for(i=0;i<W*H;++i) for(shift=0;shift<=16;shift+=8) {
        int delta=(int)((pixels[i]>>shift)&255)-(int)((expected[i]>>shift)&255);
        int x=i%W,y=i/W;
        int outside=x<ox||x>=ox+COLS*cw||y<oy||y>=oy+ROWS*ch;
        int bound=outside?0:tolerance;
        if(delta < -bound || delta > bound) {
            fprintf(stderr,"ARABIC_PIXEL_FAIL %s size=%d step=%d xy=%d,%d got=%06x expected=%06x\n",kind,size,step,x,y,pixels[i],expected[i]);
            fprintf(stderr,"origin=%d,%d grid=%dx%d\n",ox,oy,cw,ch);
#ifndef BLIT_TEST_NATIVE
            {
                int v;
                BlitRasterGlyph g;
                BlitCellCluster key;
                for(v=7;v<=9;++v) if(cps[3][v]) {
                    blit_cell_cluster_init(&key,cps[3][v],marks[3][v],counts[3][v],1);
                    font->raster_cluster_get(font->raster_state,&key,&g);
                    fprintf(stderr,"cell=%d cp=%04x ink=%dx%d offset=%d,%d slot=%u atlas=%dx%d\n",v,cps[3][v],g.width,g.height,g.ink_offset_x,g.ink_offset_y,blit_cell_cluster_hash(&key)%128,r->outputs[0].glyph_atlas_width,r->outputs[0].glyph_atlas_height);
                }
            }
#endif
            exit(1);
        }
    }
}

int main(int argc,char **argv)
{
    int sizes[]={8,9,16,17,22,23,32,48,64};
    int si,step,cases=0;
#ifdef BLIT_TEST_NATIVE
    BlitFont native_font;
    unsigned colours[ROWS*COLS]={0};
    uint16_t attributes[ROWS*COLS]={0};
    memset(&native_font,0,sizeof(native_font)); font=&native_font;
    memset(&output,0,sizeof(output)); output.mode.hdisplay=W; output.mode.vdisplay=H;
    output.back.map=pixels; output.back.pitch=W*4;
#else
    const char *renderer=getenv("BLIT_TEST_RENDERER");
    r=calloc(1,sizeof(*r)); surface=calloc(1,sizeof(*surface)); CHECK(r&&surface);
    CHECK(SDL_Init(SDL_INIT_VIDEO));
    r->output_count=r->display_count=1;
    r->outputs[0].window=SDL_CreateWindow("Arabic ink gate",W,H,SDL_WINDOW_HIDDEN);
    r->outputs[0].renderer=SDL_CreateRenderer(r->outputs[0].window,renderer?renderer:"software");
    CHECK(r->outputs[0].renderer);
    CHECK(!strcmp(SDL_GetRendererName(r->outputs[0].renderer),renderer?renderer:"software"));
    r->outputs[0].display_id=r->displays[0].id=1;
    set_display_geometry(&r->displays[0],0,0,W,H);
    r->outputs[0].render_width=W; r->outputs[0].render_height=H;
    r->surfaces[0]=surface; surface->used=1; surface->id=1; surface->owner_session_id=1;
    surface->width=COLS; surface->height=ROWS;
    r->sessions[0].used=1; r->sessions[0].id=1;
    strcpy(r->sessions[0].terminal_default_foreground,"#abcdef");
    strcpy(r->sessions[0].terminal_default_background,"#123456");
    font=&r->font;
#endif
    CHECK(argc==4);
    CHECK(!blit_font_load_path(font,"assets/fonts/Blit-Hibr-Terminus16.psf"));
    for(si=0;si<(int)(sizeof(sizes)/sizeof(*sizes));++si) {
        BlitRaster *raster;
        ch=sizes[si]; cw=(ch+1)/2;
        raster=blit_raster_open(font,argv[1],argv[2],cw,ch);
        CHECK(raster&&blit_raster_set_arabic(raster,argv[3])); blit_raster_attach(font,raster);
#ifdef BLIT_TEST_NO_INK_DAMAGE
        font->raster_ink_left=font->raster_ink_right=font->raster_ink_top=font->raster_ink_bottom=0;
#endif
#ifdef BLIT_TEST_NATIVE
        ox=oy=8;
        memset(&view,0,sizeof(view)); view.columns=COLS; view.rows=ROWS; view.stride=COLS;
        view.cell_width=cw; view.cell_height=ch; view.origin_x=ox; view.origin_y=oy;
        view.codepoints=&cps[0][0]; view.combining=&marks[0][0][0]; view.combining_count=&counts[0][0];
        view.foreground=view.background=colours; view.attributes=attributes;
        view.default_foreground=0xabcdef; view.default_background=0x123456;
#else
        r->font_size_px=ch;
        r->outputs[0].glyph_geometry_enabled=1;
        r->outputs[0].glyph_atlas=create_sdl_glyph_atlas(r->outputs[0].renderer,font,
            &r->outputs[0].glyph_atlas_columns,&r->outputs[0].glyph_atlas_width,&r->outputs[0].glyph_atlas_height);
        CHECK(r->outputs[0].glyph_atlas);
#ifdef BLIT_TEST_NON_POW2
        {
            BlitOutput *out=&r->outputs[0];
            unsigned char *empty;
            SDL_DestroyTexture(out->glyph_atlas);
            out->glyph_atlas_width=(font->raster_max_width+2)*16;
            out->glyph_atlas_height=(font->raster_max_height+2)*8;
            out->glyph_atlas_columns=16;
            out->glyph_atlas=SDL_CreateTexture(out->renderer,SDL_PIXELFORMAT_RGBA32,
                SDL_TEXTUREACCESS_STATIC,out->glyph_atlas_width,out->glyph_atlas_height);
            CHECK(out->glyph_atlas);
            empty=calloc(out->glyph_atlas_width*out->glyph_atlas_height,4); CHECK(empty);
            CHECK(SDL_UpdateTexture(out->glyph_atlas,NULL,empty,out->glyph_atlas_width*4)); free(empty);
            CHECK(SDL_SetTextureBlendMode(out->glyph_atlas,SDL_BLENDMODE_BLEND_PREMULTIPLIED));
            CHECK(SDL_SetTextureScaleMode(out->glyph_atlas,SDL_SCALEMODE_NEAREST));
        }
#endif
        memset(r->outputs[0].raster_keys,0,sizeof(r->outputs[0].raster_keys));
        surface_origin_px(r,surface,&r->sessions[0],0,cw,ch,&ox,&oy);
#endif
        memset(cps,0,sizeof(cps)); memset(counts,0,sizeof(counts)); memset(marks,0,sizeof(marks));
        cursor_visible=wallpaper_active=0;
        memset(&wallpaper,0,sizeof(wallpaper));
        wallpaper.used=wallpaper.visible=wallpaper.complete=wallpaper.opaque=1;
        wallpaper.id=1; wallpaper.width=wallpaper.height=2; wallpaper.format=24;
        wallpaper.pixels=wallpaper_rgb; wallpaper.bytes=wallpaper.capacity=sizeof(wallpaper_rgb);
        wallpaper.placement_width=COLS*cw; wallpaper.placement_height=ROWS*ch;
        wallpaper.z=-1; wallpaper.generation=wallpaper.revision=si+1;
#ifdef BLIT_TEST_NATIVE
        view.graphics=&wallpaper; view.graphics_count=0;
#endif
        /* Neighbours outside the dirty cell must survive restoration. */
        cps[2][4]=0xfeb2; cps[3][4]=0xfe92; counts[3][4]=1; marks[3][4][0]=0x0650;
        cps[4][4]=0xfe92; cps[1][9]=0xfef7;
        /* All three scalar keys collide modulo128: mask/Arabic/colour
           replacement must flush queued users before reusing atlas texels. */
        cps[5][0]='A'; cps[5][1]=0xfec1; cps[5][2]=0x1f341;
        frame(1,0,0,COLS,ROWS);
        for(step=0;step<8;++step) {
            int oldx=7+(step? (step-1)%3:0),newx=7+step%3;
            cps[3][oldx]=0; counts[3][oldx]=0;
            cps[3][newx]=step%2?0xfeae:0xfeb2;
            counts[3][newx]=1; marks[3][newx][0]=step%2?0x0650:0x0651;
            frame(0,oldx<newx?oldx:newx,3,abs(newx-oldx)+1,1);
            memcpy(saved,pixels,sizeof(saved));
            frame(1,0,0,COLS,ROWS); compare(saved,0,ch,step,"incremental-vs-clean");
            reference(); compare(oracle,
#ifdef BLIT_TEST_NATIVE
                0,
#else
                4,
#endif
                ch,step,"raster-oracle");
            ++cases;
        }
        cps[0][0]=0xfeb2; cps[ROWS-1][COLS-1]=0xfeae;
        counts[0][0]=counts[ROWS-1][COLS-1]=1; marks[0][0][0]=0x064e; marks[ROWS-1][COLS-1][0]=0x0650;
        frame(0,0,0,COLS,ROWS); reference(); compare(oracle,
#ifdef BLIT_TEST_NATIVE
            0,
#else
            4,
#endif
            ch,8,"pane-edges"); ++cases;
        cursor_visible=1; cursor_row=3;
        for(step=3;step<=5;++step) {
            cursor_column=step;
            frame(0,2,2,5,3); memcpy(saved,pixels,sizeof(saved));
            frame(1,0,0,COLS,ROWS); compare(saved,0,ch,step,"cursor-incremental");
            reference(); compare(oracle,
#ifdef BLIT_TEST_NATIVE
                0,
#else
                4,
#endif
                ch,step,"cursor-neighbour-ink"); ++cases;
        }
        cursor_visible=0; wallpaper_active=1;
#ifdef BLIT_TEST_NATIVE
        view.graphics_count=1;
#endif
        frame(1,0,0,COLS,ROWS);
        cps[3][4]=0xfeb2; marks[3][4][0]=0x064e;
        frame(0,4,3,1,1); memcpy(saved,pixels,sizeof(saved));
        frame(1,0,0,COLS,ROWS); compare(saved,0,ch,9,"wallpaper-incremental");
        reference(); compare(oracle,
#ifdef BLIT_TEST_NATIVE
            0,
#else
            4,
#endif
            ch,9,"wallpaper-oracle"); ++cases;
#ifndef BLIT_TEST_NATIVE
        SDL_DestroyTexture(r->outputs[0].glyph_atlas); r->outputs[0].glyph_atlas=NULL;
#endif
    }
    blit_font_free(font);
#ifndef BLIT_TEST_NATIVE
    destroy_outputs(r); free(surface); free(r); SDL_Quit();
#endif
    printf("ARABIC_BACKEND_PASS cases=%d sizes=9 clipping=exact incremental=exact native_scanout=not_tested\n",cases);
    return 0;
}
