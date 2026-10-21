/* Offline candidate preview using production rasters, including ink offsets.
   Input is derived from arabic-specimens.json by tools/fonts/preview.rb. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#define W 1200
#define H 1140
static unsigned pixels[W*H];
static int mark(unsigned cp)
{ return (cp >= 0x064b && cp <= 0x065f) || cp == 0x0670; }
static void paint(const BlitRasterGlyph *g, int x, int y, unsigned colour)
{
    int ix, iy;
    x += g->ink_offset_x; y += g->ink_offset_y;
    for (iy=0; iy<g->height; ++iy) for (ix=0; ix<g->width; ++ix) {
        int dx=x+ix, dy=y+iy;
        if (dx>=0 && dx<W && dy>=0 && dy<H) {
            unsigned *p=&pixels[dy*W+dx];
            *p=blit_raster_blend(g,ix,iy,colour,*p);
        }
    }
}
int main(int argc, char **argv)
{
    unsigned samples[6][160], lengths[6];
    BlitFont fallback;
    int p, line, i;
    FILE *input, *out;
    if (argc!=8) return 2;
    input=fopen(argv[6],"r"); if (!input) return 2;
    for(line=0;line<6;++line) {
        if(fscanf(input,"%u",&lengths[line])!=1 || lengths[line]>160) return 2;
        for(i=0;i<(int)lengths[line];++i) if(fscanf(input,"%x",&samples[line][i])!=1) return 2;
    }
    fclose(input);
    memset(&fallback,0,sizeof(fallback));
    if(blit_font_load_path(&fallback,argv[1])) return 2;
    for(i=0;i<W*H;++i) pixels[i]=0x10171f;
    for(p=0;p<2;++p) {
        unsigned fg=p?0xdcecf7:0xffc579;
        BlitRaster *r=blit_raster_open(&fallback,argv[p+2],NULL,24,48);
        BlitRaster *label=blit_raster_open(&fallback,argv[p+2],NULL,12,24);
        const char *title=p?"MODERN / Arabic companion candidate":"RETRO / Arabic companion candidate";
        if(!r||!label||!blit_raster_set_arabic(r,argv[p+4])) return 2;
        for(i=0;title[i];++i) {
            BlitRasterGlyph g;
            if(!blit_raster_get(label,(unsigned char)title[i],1,&g)) return 2;
            paint(&g,40+i*12,p*560+20,fg);
        }
        for(line=0;line<6;++line) {
            int col=0;
            for(i=0;i<(int)lengths[line];) {
                unsigned base=samples[line][i++], n=0;
                uint32_t marks[BLIT_CLUSTER_MAX_SCALARS];
                BlitCellCluster key;
                BlitRasterGlyph g;
                while(i<(int)lengths[line] && mark(samples[line][i])) {
                    if(n>=BLIT_CLUSTER_MAX_SCALARS-1) return 2;
                    marks[n++]=samples[line][i++];
                }
                if(!blit_cell_cluster_init(&key,base,marks,n,1) ||
                    !blit_raster_get_cluster(r,&key,&g)) {
                    fprintf(stderr,"preview fail %04X marks=%u\n",base,n); return 1;
                }
                paint(&g,80+col++*24,p*560+70+line*78,fg);
            }
        }
        blit_raster_close(r); blit_raster_close(label);
    }
    out=fopen(argv[7],"wb"); if(!out) return 2;
    fprintf(out,"P6\n%d %d\n255\n",W,H);
    for(i=0;i<W*H;++i) {
        unsigned char rgb[3]={pixels[i]>>16,pixels[i]>>8,pixels[i]};
        if(fwrite(rgb,1,3,out)!=3) return 2;
    }
    if(fclose(out)) return 2;
    blit_font_free(&fallback);
    return 0;
}
