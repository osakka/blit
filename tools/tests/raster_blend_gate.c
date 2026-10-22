#define _POSIX_C_SOURCE 200809L
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <time.h>
#define CHECK(c) do { if (!(c)) { fprintf(stderr,"RASTER_BLEND_FAIL line=%d %s\n",__LINE__,#c); exit(1); } } while(0)

/* Frozen pre-change arithmetic oracle; test-only, never a renderer. */
static uint32_t reference(const BlitRasterGlyph *g,int x,int y,uint32_t fg,uint32_t bg)
{
    const unsigned char *p; unsigned a,r,b,v;
    if(!g||!g->pixels||x<0||y<0||x>=g->width||y>=g->height)return bg;
    p=g->pixels+y*g->stride+x*(int)g->format;
    if(g->format==BLIT_RASTER_COVERAGE){a=p[0];
        r=(((fg>>16)&255)*a+127)/255;
        v=(((fg>>8)&255)*a+127)/255;b=((fg&255)*a+127)/255;
    }else if(g->format==BLIT_RASTER_RGBA_PREMULTIPLIED){r=p[0];v=p[1];b=p[2];a=p[3];}
    else return bg;
    r+=(((bg>>16)&255)*(255-a)+127)/255;
    v+=(((bg>>8)&255)*(255-a)+127)/255;b+=((bg&255)*(255-a)+127)/255;
    return ((r>255?255:r)<<16)|((v>255?255:v)<<8)|(b>255?255:b);
}
static void equivalence(void)
{
    unsigned char p[32]={0}; BlitRasterGlyph g={0};
    unsigned a,f,b,i,seed=0x12345678; unsigned long long cases=0;
    g.pixels=p;g.width=g.height=1;g.stride=4;
    for(a=0;a<256;++a)for(f=0;f<256;++f)for(b=0;b<256;++b){
        uint32_t fg=0xa5000000U|f*0x010101U,bg=0x5a000000U|b*0x010101U;
        g.format=BLIT_RASTER_COVERAGE;p[0]=a;
        CHECK(blit_raster_blend(&g,0,0,fg,bg)==reference(&g,0,0,fg,bg));++cases;
        g.format=BLIT_RASTER_RGBA_PREMULTIPLIED;
        p[0]=p[1]=p[2]=f;p[3]=a;
        CHECK(blit_raster_blend(&g,0,0,fg,bg)==reference(&g,0,0,fg,bg));++cases;
    }
    g.width=g.height=2;g.stride=12;
    for(i=0;i<100000;++i){
        uint32_t fg,bg;int x=(i%5)-1,y=((i/5)%5)-1;unsigned j;
        seed=seed*1664525U+1013904223U;fg=seed;
        seed=seed*1664525U+1013904223U;bg=seed;
        for(j=0;j<sizeof(p);++j){seed=seed*1664525U+1013904223U;p[j]=seed>>24;}
        g.format=i%3==0?BLIT_RASTER_COVERAGE:i%3==1?BLIT_RASTER_RGBA_PREMULTIPLIED:2;
        CHECK(blit_raster_blend(&g,x,y,fg,bg)==reference(&g,x,y,fg,bg));++cases;
    }
    CHECK(blit_raster_blend(NULL,0,0,0,0xfedcba98)==0xfedcba98);
    g.pixels=NULL;CHECK(blit_raster_blend(&g,0,0,0,0xfedcba98)==0xfedcba98);
    printf("RASTER_BLEND_EQUIVALENCE_PASS cases=%llu formats=coverage,rgba saturation=exact stride_bounds_high_bytes=exact\n",cases);
}
typedef uint32_t (*Blend)(const BlitRasterGlyph*,int,int,uint32_t,uint32_t);
static volatile unsigned long long sink;
static double timed(Blend blend,int mode,unsigned long long *signature)
{
    unsigned char alpha=0;BlitRasterGlyph g={0};struct timespec a,b;
    unsigned i;unsigned long long sum=0;
    g.pixels=&alpha;g.width=g.height=g.stride=1;g.format=BLIT_RASTER_COVERAGE;
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&a);
    for(i=0;i<2000000;++i){
        alpha=mode==0?0:mode==1?255:(i&255);
        sum+=blend(&g,0,0,0x96c3e1U^(i&255),0x123456U^(i&65535));
    }
    clock_gettime(CLOCK_PROCESS_CPUTIME_ID,&b);sink=sum;*signature=sum;
    return (b.tv_sec-a.tv_sec)*1000.0+(b.tv_nsec-a.tv_nsec)/1000000.0;
}
static double median(double *x)
{int i,j;for(i=0;i<5;++i)for(j=i+1;j<5;++j)if(x[j]<x[i]){double t=x[i];x[i]=x[j];x[j]=t;}return x[2];}
int main(int argc,char **argv)
{
    int mode,pass;equivalence();
    if(argc==2&&!strcmp(argv[1],"--benchmark"))for(mode=0;mode<3;++mode){
        double old[5],now[5];unsigned long long a,b;
        for(pass=0;pass<5;++pass){
            if(pass&1){now[pass]=timed(blit_raster_blend,mode,&b);old[pass]=timed(reference,mode,&a);}
            else{old[pass]=timed(reference,mode,&a);now[pass]=timed(blit_raster_blend,mode,&b);}
            CHECK(a==b);
        }
        printf("RASTER_BLEND_CPU mode=%s pixels=2000000 reference_median_ms=%.3f production_median_ms=%.3f scope=local_function_not_frame_fps\n",
            mode==0?"transparent":mode==1?"opaque":"mixed",median(old),median(now));
    }
    return 0;
}
