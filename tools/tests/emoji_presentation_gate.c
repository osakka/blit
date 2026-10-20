#include "../../src/blit_raster.h"
#include "../../src/blit_emoji_presentation.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define CHECK(t) do { if (!(t)) { fprintf(stderr,"PRESENTATION_FAIL line=%d size=%d cp=%x span=%d: %s\n",__LINE__,size,cp,span,#t); return 1; } } while(0)
static int size, span;
static uint32_t cp;
static int ink(const BlitRasterGlyph *g)
{
    int x,y,total=0;
    for(y=0;y<g->height;++y) for(x=0;x<g->width;++x) {
        const unsigned char *p=g->pixels+y*g->stride+x*(int)g->format;
        if(g->format==BLIT_RASTER_RGBA_PREMULTIPLIED) {
            if(p[0]>p[3]||p[1]>p[3]||p[2]>p[3]) return 0;
            total+=p[3]!=0;
        } else total+=p[0]!=0;
    }
    return total;
}
int main(int argc,char **argv)
{
    uint32_t points[2048]; unsigned types[2048],n=0,i;
    BlitFont fallback;
    CHECK(argc==6);
    while(n<2048&&scanf("%u %x",&types[n],&points[n])==2) ++n;
    CHECK(n==1599);
    memset(&fallback,0,sizeof(fallback));
    CHECK(!blit_font_load_path(&fallback,argv[1]));
    for(size=atoi(argv[4]);size<=atoi(argv[5]);++size) {
        BlitRaster *raster=blit_raster_open(&fallback,argv[2],argv[3],(size+1)/2,size);
        CHECK(raster);
        for(i=0;i<n;++i) for(span=1;span<=2;++span) {
            BlitRasterGlyph glyph,again;
            BlitCellCluster key;
            uint32_t selector;
            unsigned char saved[64*128];
            cp=points[i];
            if(types[i]) {
                CHECK(blit_emoji_default(cp));
                CHECK(blit_raster_get(raster,cp,span,&glyph));
                CHECK(glyph.format==BLIT_RASTER_RGBA_PREMULTIPLIED&&!glyph.fallback&&ink(&glyph));
            } else {
                CHECK(blit_emoji_variation(cp));
                selector=0xfe0e;
                CHECK(blit_cell_cluster_init(&key,cp,&selector,1,span));
                CHECK(blit_raster_get_cluster(raster,&key,&glyph));
                CHECK(glyph.format==BLIT_RASTER_COVERAGE&&!glyph.fallback&&ink(&glyph));
                CHECK(glyph.width==((size+1)/2)*span&&glyph.height==size);
                memcpy(saved,glyph.pixels,glyph.stride*glyph.height);
                selector=0xfe0f;
                CHECK(blit_cell_cluster_init(&key,cp,&selector,1,span));
                CHECK(blit_raster_get_cluster(raster,&key,&again));
                CHECK(again.format==BLIT_RASTER_RGBA_PREMULTIPLIED&&!again.fallback&&ink(&again));
                selector=0xfe0e;
                CHECK(blit_cell_cluster_init(&key,cp,&selector,1,span));
                CHECK(blit_raster_get_cluster(raster,&key,&glyph));
                CHECK(!memcmp(saved,glyph.pixels,glyph.stride*glyph.height));
                if(!blit_emoji_default(cp)) {
                    CHECK(blit_raster_get(raster,cp,span,&glyph));
                    CHECK(glyph.format==BLIT_RASTER_COVERAGE&&!glyph.fallback&&ink(&glyph));
                }
            }
            CHECK(blit_raster_cached_bytes(raster)<=4U*1024U*1024U);
        }
        blit_raster_close(raster);
    }
    blit_font_free(&fallback);
    printf("EMOJI_PRESENTATION_PASS variations=371 default_colour=1228 spans=1,2 sizes=%s..%s text=coverage colour=premultiplied cache=distinct scope=raster\n",argv[4],argv[5]);
    return 0;
}
