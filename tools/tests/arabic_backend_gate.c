/* ADR 0241, 0243: Arabic words through the KMS compositor's own cell path
   (draw_view, CPU): real composition against a raster oracle, incremental
   frames equal to clean ones (wallpaper restored, pane edges clipped), the
   block cursor over a word's overhanging ink, and a run the word face
   refuses drawn cell by cell from the glyph stack. No physical scanout.
   With -DBLIT_TEST_NO_INK_DAMAGE (no ink margins) it must fail: a word's
   ink leaves its row. */
#include "../../src/blit_kms.c"
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
static long overhangs;   /* words whose ink leaves their row */
static int cursor_visible,cursor_column,cursor_row;
static int wallpaper_active;
static unsigned char wallpaper_rgb[12]={0x31,0x52,0x73,0x31,0x52,0x73,0x31,0x52,0x73,0x31,0x52,0x73};
static BlitGraphicsImage wallpaper;
/* ADR 0226: a view takes a table of picture records. */
static BlitGraphicsImage *wallpaper_table[1] = { &wallpaper };
static BlitFont *font;
static KmsOutput output;
static BlitKmsSurfaceView view;

static void frame(int full,int x,int y,int width,int height)
{
    if(full) memset(pixels,0,sizeof(pixels));
    view.damage_valid=!full; view.damage_x=x; view.damage_y=y;
    view.damage_width=width; view.damage_height=height;
    view.cursor_visible=cursor_visible; view.cursor_x=cursor_column; view.cursor_y=cursor_row;
    draw_view(&output,font,NULL,&view);
}

static void paint_glyph(const BlitRasterGlyph *g,int row,int col,int cursor)
{
    int x,y;
    for(y=0;y<g->height;++y) for(x=0;x<g->width;++x) {
        int dx=ox+col*cw+g->ink_offset_x+x;
        int dy=oy+row*ch+g->ink_offset_y+y;
        int inside_cursor=dx>=ox+cursor_column*cw&&dx<ox+(cursor_column+1)*cw&&
            dy>=oy+cursor_row*ch&&dy<oy+(cursor_row+1)*ch;
        if(dx>=ox&&dx<ox+COLS*cw&&dy>=oy&&dy<oy+ROWS*ch&&(!cursor||inside_cursor))
            oracle[dy*W+dx]=blit_raster_blend(g,x,y,cursor?0x123456:0xabcdef,oracle[dy*W+dx]);
    }
}

/* The oracle: each run of Arabic cells is its whole shaped word, placed at
   the run's first cell (a word never leaves its cells sideways, so this is
   the slices' order too); any other cell is its own cluster. */
static void paint_reference(int cursor)
{
    int row,col,k,n;
    for(row=0;row<ROWS;++row) {
        BlitCellCluster cl[COLS];
        const BlitCellCluster *cells[COLS];
        BlitWordRun runs[COLS];
        int worded[COLS];
        for(col=0;col<COLS;++col) {
            worded[col]=0;
            cells[col]=cps[row][col]&&
                blit_cell_cluster_init(&cl[col],cps[row][col],marks[row][col],counts[row][col],1)&&
                blit_cell_word_kind(&cl[col])!=BLIT_WORD_NONE?&cl[col]:NULL;
        }
        n=blit_cell_word_runs(cells,COLS,runs,COLS);
        for(k=0;k<n;++k) {
            BlitCellWord word;
            BlitRasterGlyph whole;
            memset(&word,0,sizeof(word));
            word.kind=runs[k].kind; word.count=runs[k].count;
            memcpy(word.cells,&cl[runs[k].start],(size_t)word.count*sizeof(BlitCellCluster));
            /* Every run here is one the face shapes. */
            CHECK(blit_raster_get_word(font->raster_state,&word,-1,0,&whole,NULL));
            CHECK(whole.ink_offset_x==0&&whole.width==word.count*cw);
            overhangs+=whole.ink_offset_y<0||whole.ink_offset_y+whole.height>ch;
            paint_glyph(&whole,row,runs[k].start,cursor);
            for(col=runs[k].start;col<runs[k].start+runs[k].count;++col) worded[col]=1;
        }
        for(col=0;col<COLS;++col) if(cps[row][col]&&!worded[col]) {
            BlitCellCluster key;
            BlitRasterGlyph g;
            CHECK(blit_cell_cluster_init(&key,cps[row][col],marks[row][col],counts[row][col],1));
            CHECK(font->raster_cluster_get(font->raster_state,&key,0,&g));
            paint_glyph(&g,row,col,cursor);
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
                    KMS_WHITE;
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
            exit(1);
        }
    }
}

int main(int argc,char **argv)
{
    int sizes[]={8,9,16,17,22,23,32,48,64};
    int si,step,cases=0,refused=0;
    BlitFont native_font;
    unsigned colours[ROWS*COLS]={0};
    uint32_t attributes[ROWS*COLS]={0};
    memset(&native_font,0,sizeof(native_font)); font=&native_font;
    memset(&output,0,sizeof(output)); output.mode.hdisplay=W; output.mode.vdisplay=H;
    output.back.map=pixels; output.back.pitch=W*4;
    CHECK(argc==4);
    CHECK(!blit_font_load_path(font,"assets/fonts/Blit-Terminus16.psf"));
    for(si=0;si<(int)(sizeof(sizes)/sizeof(*sizes));++si) {
        BlitRaster *raster;
        ch=sizes[si]; cw=(ch+1)/2;
        raster=blit_raster_open(font,argv[1],argv[2],cw,ch);
        CHECK(raster&&blit_raster_set_arabic_words(raster,argv[3])); blit_raster_attach(font,raster);
        /* The word face's ink may take a row above and below, never a
           column sideways. */
        CHECK(font->raster_ink_left==0&&font->raster_ink_right==0&&
              font->raster_ink_top==1&&font->raster_ink_bottom==1);
#ifdef BLIT_TEST_NO_INK_DAMAGE
        font->raster_ink_left=font->raster_ink_right=font->raster_ink_top=font->raster_ink_bottom=0;
#endif
        ox=oy=8;
        memset(&view,0,sizeof(view)); view.columns=COLS; view.rows=ROWS; view.stride=COLS;
        view.cell_width=cw; view.cell_height=ch; view.origin_x=ox; view.origin_y=oy;
        view.codepoints=&cps[0][0]; view.combining=&marks[0][0][0]; view.combining_count=&counts[0][0];
        view.foreground=view.background=colours; view.attributes=attributes;
        view.default_foreground=0xabcdef; view.default_background=0x123456;
        memset(cps,0,sizeof(cps)); memset(counts,0,sizeof(counts)); memset(marks,0,sizeof(marks));
        cursor_visible=wallpaper_active=0;
        memset(&wallpaper,0,sizeof(wallpaper));
        wallpaper.used=wallpaper.visible=wallpaper.complete=wallpaper.opaque=1;
        wallpaper.id=1; wallpaper.width=wallpaper.height=2; wallpaper.format=24;
        wallpaper.pixels=wallpaper_rgb; wallpaper.bytes=wallpaper.capacity=sizeof(wallpaper_rgb);
        wallpaper.placement_width=COLS*cw; wallpaper.placement_height=ROWS*ch;
        wallpaper.z=-1; wallpaper.generation=wallpaper.revision=si+1;
        view.graphics=wallpaper_table; view.graphics_count=0;
        /* Neighbours outside the dirty cell must survive restoration. */
        cps[2][4]=0xfeb2; cps[3][4]=0xfe92; counts[3][4]=1; marks[3][4][0]=0x0650;
        cps[4][4]=0xfe92; cps[1][9]=0xfef7;
        /* All three scalar keys collide modulo128: mask/Arabic/colour
           replacement must flush queued users before reusing atlas texels. */
        cps[5][0]='A'; cps[5][1]=0xfec1; cps[5][2]=0x1f341;
        cps[5][3]=0x08ff; /* a marks-only cell: a run of its own */
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
                0,
                ch,step,"raster-oracle");
            ++cases;
        }
        cps[0][0]=0xfeb2; cps[ROWS-1][COLS-1]=0xfeae;
        counts[0][0]=counts[ROWS-1][COLS-1]=1; marks[0][0][0]=0x064e; marks[ROWS-1][COLS-1][0]=0x0650;
        frame(0,0,0,COLS,ROWS); reference(); compare(oracle,
            0,
            ch,8,"pane-edges"); ++cases;
        cursor_visible=1; cursor_row=3;
        for(step=3;step<=5;++step) {
            cursor_column=step;
            frame(0,2,2,5,3); memcpy(saved,pixels,sizeof(saved));
            frame(1,0,0,COLS,ROWS); compare(saved,0,ch,step,"cursor-incremental");
            reference(); compare(oracle,
                0,
                ch,step,"cursor-neighbour-ink"); ++cases;
        }
        cursor_visible=0; wallpaper_active=1;
        view.graphics_count=1;
        frame(1,0,0,COLS,ROWS);
        cps[3][4]=0xfeb2; marks[3][4][0]=0x064e;
        frame(0,4,3,1,1); memcpy(saved,pixels,sizeof(saved));
        frame(1,0,0,COLS,ROWS); compare(saved,0,ch,9,"wallpaper-incremental");
        reference(); compare(oracle,
            0,
            ch,9,"wallpaper-oracle"); ++cases;
        /* ADR 0243: a run the word face refuses (a Latin acute in it) is
           drawn cell by cell from the glyph stack, exactly as with no word
           face at all, and with ink; a word beside it is not. */
        {
            BlitRaster *plain;
            int x,y,ink=0,same=1,word_differs=0;
            wallpaper_active=0; view.graphics_count=0; cursor_visible=0;
            memset(cps,0,sizeof(cps)); memset(counts,0,sizeof(counts)); memset(marks,0,sizeof(marks));
            cps[6][6]=0xfe91; cps[6][7]=0xfe92; cps[6][8]=0xfe90;
            marks[6][7][0]=0x0301; counts[6][7]=1;
            cps[6][11]=0xfe91; cps[6][12]=0xfe92; cps[6][13]=0xfe90;
            frame(1,0,0,COLS,ROWS); memcpy(saved,pixels,sizeof(saved));
            plain=blit_raster_open(font,argv[1],argv[2],cw,ch);
            CHECK(plain); blit_raster_attach(font,plain);
            frame(1,0,0,COLS,ROWS);
            for(y=oy+6*ch;y<oy+7*ch;++y) {
                for(x=ox+6*cw;x<ox+9*cw;++x) {
                    same&=pixels[y*W+x]==saved[y*W+x];
                    ink|=pixels[y*W+x]!=0x123456;
                }
                for(x=ox+11*cw;x<ox+14*cw;++x) word_differs|=pixels[y*W+x]!=saved[y*W+x];
            }
            if(!same||!ink||!word_differs) {
                fprintf(stderr,"ARABIC_REFUSED_FAIL size=%d same=%d ink=%d word_differs=%d\n",ch,same,ink,word_differs);
                exit(1);
            }
            ++refused; ++cases;
        }
    }
    blit_font_free(font);
    CHECK(overhangs);
    printf("ARABIC_BACKEND_PASS cases=%d sizes=9 refused_runs=%d overhanging_words=%ld clipping=exact incremental=exact native_scanout=not_tested\n",cases,refused,overhangs);
    return 0;
}
