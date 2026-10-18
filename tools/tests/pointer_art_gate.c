#include "../../src/blit_pointer_art.h"
#include <stdio.h>
#include <stdlib.h>

int main(int argc, char **argv)
{
    BlitPointerArt art;
    BlitPointerConfig config = blit_pointer_default();
    unsigned char *sheet = calloc(640 * 320, 3);
    int shape, palette, size, i, x, y, partial, opaque;
    memset(&art, 0, sizeof(art));
    if (argc == 6) {
        FILE *file;
        config.shape=atoi(argv[2]);config.palette=atoi(argv[3]);config.size=atoi(argv[4]);
        blit_pointer_build(&art,config);
        if (!art.revision) return 5;
        file=fopen(argv[5],"wb");if (!file) return 6;
        fwrite(&art.width,sizeof(int),1,file);fwrite(&art.height,sizeof(int),1,file);
        fwrite(&art.hotspot_x,sizeof(int),1,file);fwrite(&art.hotspot_y,sizeof(int),1,file);
        fwrite(art.rgba,4,art.width*art.height,file);fclose(file);free(sheet);return 0;
    }
    for (shape = 0; shape < 3; ++shape) for (palette = 0; palette < 4; ++palette)
        for (size = 24; size <= 64; ++size) {
            config.shape = shape; config.palette = palette; config.size = size;
            blit_pointer_build(&art, config);
            partial = opaque = 0;
            if (art.width > BLIT_POINTER_MAX || art.hotspot_x >= art.width ||
                art.hotspot_y >= art.height || art.hotspot_x < 0 || art.hotspot_y < 0) return 1;
            for (i = 0; i < art.width * art.height; ++i) {
                int a = art.rgba[i * 4 + 3];
                if (a > 0 && a < 255) ++partial;
                if (a == 255) ++opaque;
                if (!a && blit_pointer_blend(0x123456, art.rgba+i*4) != 0x123456) return 2;
            }
            if (!partial || !opaque) return 3;
        }
    /* A readable source-derived preview, not a runtime screenshot. */
    for (y = 0; y < 320; ++y) for (x = 0; x < 640; ++x) {
        int k = (y * 640 + x) * 3;
        int dark = (x / 160 + y / 80) % 2;
        sheet[k] = dark ? 24 : 225; sheet[k+1] = dark ? 33 : 231; sheet[k+2] = dark ? 49 : 240;
    }
    for (shape = 0; shape < 3; ++shape) for (palette = 0; palette < 4; ++palette) {
        config.shape=shape;config.palette=palette;config.size=56;
        blit_pointer_build(&art, config);
        for (y = 0; y < art.height; ++y) for (x = 0; x < art.width; ++x) {
            int k = ((shape*80+8+y)*640 + palette*160+45+x)*3;
            uint32_t bg = (sheet[k]<<16)|(sheet[k+1]<<8)|sheet[k+2];
            uint32_t colour = blit_pointer_blend(bg, art.rgba+(y*art.width+x)*4);
            sheet[k] = colour>>16;sheet[k+1]=colour>>8;sheet[k+2]=colour;
        }
    }
    if (argc == 2) {
        FILE *file = fopen(argv[1],"wb"); if (!file) return 4;
        fprintf(file,"P6\n640 320\n255\n");fwrite(sheet,1,640*320*3,file);fclose(file);
    }
    free(sheet);
    puts("POINTER_ART_PASS shapes=3 palettes=4 sizes=41 antialias=present hotspot=bounded alpha=exact preview=source-derived");
    return 0;
}
