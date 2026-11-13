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
    for (shape = 0; shape < BLIT_POINTER_SHAPE_COUNT; ++shape)
        for (palette = 0; palette < 4; ++palette)
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
    /* ADR 0235: the busy pointer goes round (every phase different, each
       spoke lit in turn, geometry kept), and no two arts -- built, spun,
       resampled -- ever share a revision, since the display takes an
       equal revision for the same picture. */
    {
        static BlitPointerArt base, spun[BLIT_POINTER_SPOKES], master, grown;
        unsigned long long seen[BLIT_POINTER_SPOKES + 3];
        int p, q, n = 0;
        config.shape = BLIT_POINTER_SHAPE_BUSY; config.palette = 1; config.size = 24;
        blit_pointer_build(&base, config);
        seen[n++] = base.revision;
        for (p = 0; p < BLIT_POINTER_SPOKES; ++p) {
            blit_pointer_spin(&spun[p], &base, p);
            if (spun[p].width != base.width || spun[p].hotspot_x != base.hotspot_x ||
                memcmp(&spun[p].config, &base.config, sizeof(base.config))) return 7;
            for (q = 0; q < p; ++q)
                if (!memcmp(spun[p].rgba, spun[q].rgba, (size_t)base.width * base.height * 4)) return 8;
            seen[n++] = spun[p].revision;
        }
        config.size = 160;
        blit_pointer_build(&master, config);
        seen[n++] = master.revision;
        blit_pointer_resample(&grown, &master, 96);
        seen[n++] = grown.revision;
        for (p = 0; p < n; ++p)
            for (q = 0; q < p; ++q)
                if (seen[p] == seen[q] || !seen[p]) return 9;
    }
    if (argc == 2) {
        FILE *file = fopen(argv[1],"wb"); if (!file) return 4;
        fprintf(file,"P6\n640 320\n255\n");fwrite(sheet,1,640*320*3,file);fclose(file);
    }
    free(sheet);
    printf("POINTER_ART_PASS shapes=%d palettes=4 sizes=41 antialias=present hotspot=bounded alpha=exact preview=source-derived busy=12-phases revisions=unique\n", BLIT_POINTER_SHAPE_COUNT);
    return 0;
}
