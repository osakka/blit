/* Production canonical-grid fixture, not a compositor/physical benchmark. */
#define main blit_server_for_rectangle_gate
#include "../../src/blit_server.c"
#undef main

#define CHECK(x) do { if (!(x)) { fprintf(stderr,"RECTANGLE_COPY_FAIL line=%d expression=%s\n",__LINE__,#x); exit(1); } } while (0)
static BlitSurface *grid, *original;
static BlitSession *session;
static unsigned cases;

static void equal_cell(int x,int y,int sx,int sy)
{
#define EQ(field) CHECK(grid->field[y][x] == original->field[sy][sx])
    EQ(cells); EQ(codepoints); EQ(combining_count); EQ(attributes);
    EQ(foreground); EQ(background); EQ(underline_color); EQ(links);
    CHECK(!memcmp(grid->combining[y][x],original->combining[sy][sx],sizeof(grid->combining[y][x])));
#undef EQ
}

static void seed_dimensions(int width,int height)
{
    int x,y;
    memset(grid,0,sizeof(*grid)); memset(session,0,sizeof(*session));
    grid->width=width; grid->height=height;
    grid->scroll_bottom=height-1; grid->right_margin=width-1;
    grid->cursor_x=17; grid->cursor_y=9;
    session->terminal_attributes=BLIT_ATTR_BOLD; session->terminal_foreground=0x123456;
    session->pty_fd=-1;
    for(y=0;y<height;++y) for(x=0;x<width;++x) {
        unsigned n=y*width+x;
        grid->cells[y][x]=(char)('A'+n%26); grid->codepoints[y][x]=grid->cells[y][x];
        grid->combining_count[y][x]=n%17;
        for(int mark=0;mark<grid->combining_count[y][x];++mark)
            grid->combining[y][x][mark]=0x0300+mark;
        grid->attributes[y][x]=BLIT_ATTR_UNDERLINE|(n%2?BLIT_ATTR_PROTECTED:0);
        grid->foreground[y][x]=n+1; grid->background[y][x]=n+0x100;
        grid->underline_color[y][x]=n+0x1000; grid->links[y][x]=1+n%7;
    }
    memcpy(original,grid,sizeof(*grid));
}

static void seed(void) { seed_dimensions(24,12); }

static void wide(int x,int y)
{
    grid->codepoints[y][x]=0x1f469; grid->cells[y][x]='?';
    grid->attributes[y][x]=BLIT_ATTR_WIDE|BLIT_ATTR_UNDERLINE;
    grid->combining_count[y][x]=2;
    memset(grid->combining[y][x],0,sizeof(grid->combining[y][x]));
    grid->combining[y][x][0]=0x200d; grid->combining[y][x][1]=0x1f4bb;
    terminal_clear_cell(grid,x+1,y);
    grid->attributes[y][x+1]=BLIT_ATTR_CONTINUATION;
    grid->links[y][x+1]=grid->links[y][x];
    grid->damage_valid=0;
    memcpy(original,grid,sizeof(*grid));
}

int main(void)
{
    int x,y,dx,dy;
    unsigned random=120;
    grid=calloc(1,sizeof(*grid)); original=calloc(1,sizeof(*original));
    session=calloc(1,sizeof(*session)); CHECK(grid&&original&&session);
    /* Immutable full-grid oracle: result cells are read from original only. */
    for(dy=-2;dy<=2;++dy) for(dx=-3;dx<=3;++dx) {
        int p[]={4,5,9,17,1,4+dy,5+dx,1};
        seed(); terminal_copy_rectangle(session,grid,p,8);
        for(y=0;y<12;++y) for(x=0;x<24;++x) {
            int inside=y>=3+dy&&y<9+dy&&x>=4+dx&&x<17+dx;
            equal_cell(x,y,inside?x-dx:x,inside?y-dy:y);
        }
        CHECK(grid->cursor_x==17&&grid->cursor_y==9);
        CHECK(session->terminal_attributes==BLIT_ATTR_BOLD&&session->terminal_foreground==0x123456);
        ++cases;
    }
    for(int n=0;n<1000;++n) {
        int p[8],top,left,bottom,right,height,width;
        seed();
        random=random*1664525U+1013904223U; top=(int)(random%10);
        random=random*1664525U+1013904223U; left=(int)(random%20);
        random=random*1664525U+1013904223U; bottom=top+(int)(random%(12-top));
        random=random*1664525U+1013904223U; right=left+(int)(random%(24-left));
        random=random*1664525U+1013904223U; dy=(int)(random%12);
        random=random*1664525U+1013904223U; dx=(int)(random%24);
        p[0]=top+1;p[1]=left+1;p[2]=bottom+1;p[3]=right+1;
        p[4]=0;p[5]=dy+1;p[6]=dx+1;p[7]=9;
        terminal_copy_rectangle(session,grid,p,8);
        height=bottom-top+1;if(height>12-dy)height=12-dy;
        width=right-left+1;if(width>24-dx)width=24-dx;
        for(y=0;y<12;++y) for(x=0;x<24;++x) {
            int inside=y>=dy&&y<dy+height&&x>=dx&&x<dx+width;
            equal_cell(x,y,inside?left+x-dx:x,inside?top+y-dy:y);
        }
        ++cases;
    }
    /* Exercise the entire supported page and the maximum row scratch size. */
    for(int reverse=0;reverse<2;++reverse) {
        seed_dimensions(BLIT_MAX_CELL_COLS,BLIT_MAX_CELL_ROWS);
        int p[]={reverse?2:1,reverse?2:1,BLIT_MAX_CELL_ROWS-(reverse?0:1),
                 BLIT_MAX_CELL_COLS-(reverse?0:1),1,reverse?1:2,reverse?1:2,1};
        terminal_copy_rectangle(session,grid,p,8);
        for(y=0;y<BLIT_MAX_CELL_ROWS;++y) for(x=0;x<BLIT_MAX_CELL_COLS;++x) {
            int inside=reverse?(y<BLIT_MAX_CELL_ROWS-1&&x<BLIT_MAX_CELL_COLS-1):(y>0&&x>0);
            equal_cell(x,y,inside?x+(reverse?1:-1):x,inside?y+(reverse?1:-1):y);
        }
        ++cases;
    }
    /* Complete wide/ZWJ clusters survive copying over another wide pair. */
    seed(); wide(2,2); wide(8,5);
    terminal_copy_rectangle(session,grid,(int[]){3,3,3,4,1,6,9,1},8);
    equal_cell(8,5,2,2); equal_cell(9,5,3,2); ++cases;
    /* Cut source owners/tails cannot form a glyph with destination neighbours. */
    seed(); wide(2,2);
    terminal_copy_rectangle(session,grid,(int[]){3,4,3,5,1,6,9,1},8);
    CHECK(grid->codepoints[5][8]==0&&grid->attributes[5][8]==0);
    equal_cell(9,5,4,2); ++cases;
    seed(); wide(2,2);
    terminal_copy_rectangle(session,grid,(int[]){3,3,3,3,1,6,9,1},8);
    CHECK(grid->codepoints[5][8]==0&&grid->attributes[5][8]==0); ++cases;
    /* Destination clipping truncates a wide owner too. */
    seed(); wide(2,2);
    terminal_copy_rectangle(session,grid,(int[]){3,3,3,4,1,6,24,1},8);
    CHECK(grid->codepoints[5][23]==0&&grid->attributes[5][23]==0); ++cases;
    /* Boundary cleanup includes damage on the old owner outside the target. */
    seed(); wide(8,5);
    terminal_copy_rectangle(session,grid,(int[]){1,1,1,1,1,6,10,1},8);
    CHECK(grid->codepoints[5][8]==0&&grid->damage_x<=8);
    equal_cell(9,5,0,0); ++cases;
    seed(); wide(8,5);
    terminal_copy_rectangle(session,grid,(int[]){1,1,1,1,1,6,9,1},8);
    CHECK(grid->codepoints[5][9]==0&&grid->damage_x+grid->damage_width>9); ++cases;
    /* Defaults, high coordinates, origin/margins and inverted rectangles. */
    seed(); terminal_copy_rectangle(session,grid,NULL,0);
    CHECK(!grid->damage_valid); ++cases;
    seed(); terminal_copy_rectangle(session,grid,(int[]){99,99,99,99,9,1,1,9},8);
    equal_cell(0,0,23,11); ++cases;
    seed(); terminal_copy_rectangle(session,grid,(int[]){5,4,2,3,1,1,1,1},8);
    CHECK(!grid->damage_valid); ++cases;
    seed(); session->terminal_origin_mode=1;
    grid->scroll_top=2;grid->scroll_bottom=8;grid->lrmm_enabled=1;grid->left_margin=3;grid->right_margin=15;
    terminal_copy_rectangle(session,grid,(int[]){1,1,2,2,1,7,13,1},8);
    equal_cell(15,8,3,2); equal_cell(16,8,16,8); equal_cell(15,9,15,9); ++cases;
    /* Byte-stream parser and DA1/DECID use the same path, no SDL required. */
    {
        BlitRuntime *runtime=calloc(1,sizeof(*runtime));
        const unsigned char command[]="\033[3;3;3;4;1;6;9;1$v";
        char reply[64]; int fds[2]; ssize_t count;
        seed(); wide(2,2); CHECK(runtime);
        session->graphics[0].used=1; session->graphics[0].x=77;
        for(x=0;x<(int)sizeof(command)-1;++x)
            terminal_consume_output(runtime,session,grid,command+x,1);
        equal_cell(8,5,2,2);equal_cell(9,5,3,2);
        CHECK(session->graphics[0].used&&session->graphics[0].x==77);
        CHECK(pipe(fds)==0);session->pty_fd=fds[1];
        terminal_consume_output(runtime,session,grid,(const unsigned char*)"\033[c\033Z",5);
        count=read(fds[0],reply,sizeof(reply));
        CHECK(count==28&&!memcmp(reply,"\033[?62;4;22;28c\033[?62;4;22;28c",28));
        close(fds[0]);close(fds[1]);session->pty_fd=-1;free(runtime);++cases;
    }
    /* Local primitive CPU cost only: excludes hold, painting and input. */
    {
        struct timespec before,after;
        double ms;
        seed_dimensions(232,71);
        CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&before)==0);
        for(int n=0;n<30;++n) {
            int p[]={2+n,2+n,39+n,145+n,1,3+n,3+n,1};
            terminal_copy_rectangle(session,grid,p,8);
        }
        CHECK(clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&after)==0);
        ms=(after.tv_sec-before.tv_sec)*1000.0+(after.tv_nsec-before.tv_nsec)/1000000.0;
        printf("RECTANGLE_COPY_COST grid=232x71 rectangle=144x38 steps=30 local_cpu_total_ms=%.3f mean_ms=%.3f excludes=parser_hold_render_input\n",ms,ms/30);
    }
    free(session);free(original);free(grid);
    printf("RECTANGLE_COPY_PASS cases=%u metadata=complete immutable_oracle=exact wide_edges=normalized discovery=DA28 scope=parser_not_drag_performance\n",cases);
    return 0;
}
