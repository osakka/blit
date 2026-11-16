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
    /* ADR 0235: the busy pointer goes round (every phase different, geometry
       kept), and no two arts -- built, spun, resampled -- ever share a
       revision, since the display takes an equal revision for the same
       picture. The spokes light a spoke every third of 36 phases; the
       wheel's colours turn 10 degrees a phase, clockwise, its alpha and rim
       kept. */
    {
        static BlitPointerArt base, spun[BLIT_POINTER_PHASES], master, grown;
        unsigned long long seen[BLIT_POINTER_PHASES * 2 + 6];
        int p, q, n = 0, style;
        for (style = 0; style < 2; ++style) {
            int step = style == BLIT_POINTER_BUSY_SPOKES ? 3 : 1;
            config.shape = BLIT_POINTER_SHAPE_BUSY; config.palette = 1; config.size = 24;
            config.busy_style = style;
            blit_pointer_build(&base, config);
            if (!base.revision) return 10;
            seen[n++] = base.revision;
            for (p = 0; p < BLIT_POINTER_PHASES; p += step) {
                blit_pointer_spin(&spun[p], &base, p);
                if (spun[p].width != base.width || spun[p].hotspot_x != base.hotspot_x ||
                    memcmp(&spun[p].config, &base.config, sizeof(base.config))) return 7;
                for (q = 0; q < p; q += step)
                    if (!memcmp(spun[p].rgba, spun[q].rgba, (size_t)base.width * base.height * 4)) return 8;
                seen[n++] = spun[p].revision;
            }
            if (style == BLIT_POINTER_BUSY_WHEEL) {
                /* At 56 px: colours differ round the wheel; a spin keeps every
                   alpha and the rim; 9 phases on, the colour at 3 o'clock is
                   the colour that was at 12. */
                static BlitPointerArt big, turned;
                int cx, cy, i2, k, top, right, bottom, rim, diff;
                config.size = 56;
                blit_pointer_build(&big, config);
                blit_pointer_spin(&turned, &big, 9);
                cx = big.hotspot_x; cy = big.hotspot_y;
                top = ((cy - 15) * big.width + cx) * 4;
                right = (cy * big.width + cx + 15) * 4;
                bottom = ((cy + 15) * big.width + cx) * 4;
                rim = ((cy - 21) * big.width + cx) * 4;
                diff = 0;
                for (k = 0; k < 3; ++k) diff += abs(big.rgba[top + k] - big.rgba[bottom + k]);
                if (diff < 120) return 11;
                for (i2 = 0; i2 < big.width * big.height; ++i2)
                    if (big.rgba[i2 * 4 + 3] != turned.rgba[i2 * 4 + 3]) return 12;
                (void)rim;
                {
                    /* Outside the spin's blend edge every pixel is kept; the
                       rim is there, opaque and dark. */
                    float sc = 56.0f / 32.0f, edge = (BLIT_POINTER_WHEEL_R - BLIT_POINTER_WHEEL_RIM) * sc;
                    int dark = 0, xx, yy;
                    for (yy = 0; yy < big.height; ++yy) for (xx = 0; xx < big.width; ++xx) {
                        float ddx = xx + 0.5f - (16 * sc + 1), ddy = yy + 0.5f - (16 * sc + 1);
                        int o = (yy * big.width + xx) * 4;
                        if (ddx * ddx + ddy * ddy > (edge + 0.5f) * (edge + 0.5f) &&
                            memcmp(big.rgba + o, turned.rgba + o, 4)) return 13;
                        if (big.rgba[o + 3] == 255 && big.rgba[o] < 70 && big.rgba[o + 1] < 70 &&
                            big.rgba[o + 2] < 70) ++dark;
                    }
                    if (dark < 40) return 16;
                }
                diff = 0;
                for (k = 0; k < 3; ++k) diff += abs(turned.rgba[right + k] - big.rgba[top + k]);
                if (diff > 24) return 14;
            }
        }
        config.busy_style = BLIT_POINTER_BUSY_WHEEL; config.size = 160;
        blit_pointer_build(&master, config);
        seen[n++] = master.revision;
        blit_pointer_resample(&grown, &master, 96);
        seen[n++] = grown.revision;
        for (p = 0; p < n; ++p)
            for (q = 0; q < p; ++q)
                if (seen[p] == seen[q] || !seen[p]) return 9;
        config.busy_style = 9;
        memset(&base, 0, sizeof(base));
        blit_pointer_build(&base, config);
        if (base.revision) return 15;   /* an unknown style builds nothing */
        config.busy_style = BLIT_POINTER_BUSY_WHEEL;
    }
    if (argc == 2) {
        FILE *file = fopen(argv[1],"wb"); if (!file) return 4;
        fprintf(file,"P6\n640 320\n255\n");fwrite(sheet,1,640*320*3,file);fclose(file);
    }
    free(sheet);
    printf("POINTER_ART_PASS shapes=%d palettes=4 sizes=41 antialias=present hotspot=bounded alpha=exact preview=source-derived busy=wheel(36-phases,turns-clockwise,alpha+rim-kept)+spokes(12-of-36) revisions=unique\n", BLIT_POINTER_SHAPE_COUNT);
    return 0;
}
