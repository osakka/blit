#include "../../src/blit_raster.h"
#include <stdio.h>
#include <string.h>

#define CHECK(t) do { if (!(t)) { fprintf(stderr,"POWERLINE_FAIL size=%d span=%d cp=%x line=%d: %s\n",size,span,cp,__LINE__,#t); return 1; } } while(0)
int main(int argc, char **argv)
{
    BlitFont font;
    int size=0,span=0,x,y,kind,cases=0;
    unsigned cp=0;
    CHECK(argc==4);
    memset(&font,0,sizeof(font));
    CHECK(!blit_font_load_path(&font,argv[1]));
    for(size=8;size<=64;++size) {
        BlitRaster *r=blit_raster_open(&font,argv[2],argv[3],(size+1)/2,size);
        CHECK(r);
        for(span=1;span<=2;++span) for(kind=0;kind<2;++kind) {
            BlitRasterGlyph right,left,again;
            unsigned char saved[128*64];
            unsigned long count;
            int partial=0,previous=-1;
            cp=0xe0b0+kind;
            CHECK(blit_raster_get(r,cp,span,&right));
            CHECK(right.format==BLIT_RASTER_COVERAGE&&!right.fallback);
            CHECK(right.width==((size+1)/2)*span&&right.height==size);
            memcpy(saved,right.pixels,right.stride*right.height);
            right.pixels=saved;
            CHECK(blit_raster_get(r,cp+2,span,&left));
            CHECK(left.format==BLIT_RASTER_COVERAGE&&!left.fallback);
            for(y=0;y<size;++y) {
                int row_ink=0;
                for(x=0;x<right.width;++x) {
                    int a=saved[y*right.stride+x];
                    CHECK(a==left.pixels[y*left.stride+left.width-1-x]);
                    CHECK(a==saved[(size-1-y)*right.stride+x]);
                    row_ink+=a;
                    partial+=a>0&&a<255;
                }
                CHECK(row_ink>0);
                if(!kind) {
                    CHECK(saved[y*right.stride]==255);
                    CHECK(blit_raster_blend(&right,0,y,0x123456,0xabcdef)==0x123456);
                    if(y<size/2) CHECK(row_ink>=previous);
                    previous=row_ink;
                }
            }
            CHECK(partial>0);
            CHECK(saved[0]>0&&saved[(size-1)*right.stride]>0);
            CHECK(saved[(size/2)*right.stride+right.width-1]>0);
            if(kind) CHECK(!saved[(size/2)*right.stride]);
            count=blit_raster_render_count(r);
            CHECK(blit_raster_get(r,cp,span,&again));
            CHECK(blit_raster_render_count(r)==count);
            CHECK(!memcmp(saved,again.pixels,again.stride*again.height));
            CHECK(blit_raster_cached_bytes(r)<=4U*1024U*1024U);
            cases+=2;
        }
        blit_raster_close(r);
    }
    blit_font_free(&font);
    printf("POWERLINE_GEOMETRY_PASS sizes=57 spans=1,2 scalars=4 cases=%d solid_base=opaque mirror=exact vertical_symmetry=exact slopes=antialiased cache=stable scope=shared_raster\n",cases);
    return 0;
}
