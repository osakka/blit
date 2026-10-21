/* Candidate-only shared-raster acceptance. Not a display or bidi test. */
#include "../../src/blit_raster.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int sample(const BlitRasterGlyph *g, int x, int y)
{
    x -= g->ink_offset_x; y -= g->ink_offset_y;
    return x >= 0 && y >= 0 && x < g->width && y < g->height ?
        g->pixels[y*g->stride+x] : 0;
}

int main(int argc, char **argv)
{
    BlitFont fallback;
    int height, failures = 0, cases = 0, overhangs = 0, changes = 0;
    FILE *specimens;
    if (argc != 5) return 2;
    specimens = fopen(argv[4],"r"); if (!specimens) return 2;
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 2;
    for (height = 8; height <= 64; ++height) {
        unsigned cp;
        BlitRaster *r = blit_raster_open(&fallback, argv[2], NULL, (height+1)/2, height);
        if (!r || blit_raster_set_arabic(r, "/nonexistent/blit-arabic-candidate.ttf")) return 2;
        if (!r || !blit_raster_set_arabic(r, argv[3])) return 2;
        for (cp = 0x0621; cp <= 0xfefc; ++cp) {
            BlitRasterGlyph g;
            unsigned i, ink = 0;
            /* Core Arabic alphabet, not every extended Arabic letter. */
            if (!((cp >= 0x0621 && cp <= 0x063a) || (cp >= 0x0640 && cp <= 0x064a) ||
                  (cp >= 0x0660 && cp <= 0x0669) ||
                  (cp >= 0xfe80 && cp <= 0xfefc))) continue;
            ++cases;
            if (!blit_raster_get(r, cp, 1, &g) || g.fallback) {
                if (failures++ < 30) fprintf(stderr, "ARABIC_FAIL height=%d cp=%04X\n", height, cp);
                continue;
            }
            for (i = 0; i < (unsigned)(g.stride * g.height); ++i) ink += g.pixels[i] != 0;
            if (!ink) { fprintf(stderr, "ARABIC_BLANK %04X\n", cp); ++failures; }
            overhangs += g.ink_offset_x != 0 || g.ink_offset_y != 0 ||
                         g.width > (height+1)/2 || g.height > height;
        }
        {
            uint32_t bases[] = {0xfea3, 0xfe92, 0xfeae, 0xfe8d, 0xfefb};
            uint32_t marks[] = {0x064b, 0x064c, 0x064d, 0x064e, 0x064f, 0x0650, 0x0651, 0x0652, 0x0670};
            unsigned b, m;
            for (b = 0; b < sizeof(bases)/sizeof(*bases); ++b)
                for (m = 0; m < sizeof(marks)/sizeof(*marks); ++m) {
                    BlitCellCluster key;
                    BlitRasterGlyph g, plain;
                    unsigned char saved[256*256];
                    int different = 0, x, y;
                    if (!blit_raster_get(r, bases[b], 1, &plain) ||
                        plain.stride*plain.height > (int)sizeof(saved)) return 2;
                    memcpy(saved, plain.pixels, plain.stride*plain.height);
                    plain.pixels = saved;
                    ++cases;
                    if (!blit_cell_cluster_init(&key, bases[b], marks+m, 1, 1) ||
                        !blit_raster_get_cluster(r, &key, &g) || g.fallback) {
                        if (failures++ < 30) fprintf(stderr, "ARABIC_MARK_FAIL height=%d cp=%04X mark=%04X\n", height, bases[b], marks[m]);
                        continue;
                    }
                    for (y=-height; y<height*2; ++y)
                        for (x=-height; x<height*2; ++x)
                            different += sample(&g,x,y) != sample(&plain,x,y);
                    if (!different) { fprintf(stderr,"ARABIC_MARK_IGNORED %04X %04X\n",bases[b],marks[m]); ++failures; }
                    else ++changes;
                }
        }
        rewind(specimens);
        for (;;) {
            unsigned n, i;
            uint32_t scalars[BLIT_CLUSTER_MAX_SCALARS];
            BlitCellCluster key;
            BlitRasterGlyph g, again;
            unsigned long renders;
            if (fscanf(specimens,"%u",&n)!=1) break;
            if (!n || n>BLIT_CLUSTER_MAX_SCALARS) return 2;
            for(i=0;i<n;++i) if(fscanf(specimens,"%x",&scalars[i])!=1) return 2;
            ++cases;
            if(!blit_cell_cluster_init(&key,scalars[0],scalars+1,n-1,1) ||
                !blit_raster_get_cluster(r,&key,&g) || g.fallback) {
                fprintf(stderr,"ARABIC_SPECIMEN_FAIL height=%d cp=%04X marks=%u\n",height,scalars[0],n-1);
                ++failures; continue;
            }
            renders=blit_raster_render_count(r);
            if(!blit_raster_get_cluster(r,&key,&again) || again.pixels!=g.pixels ||
                blit_raster_render_count(r)!=renders) return 2;
        }
        if (blit_raster_cached_bytes(r)>4U*1024U*1024U) return 2;
        {
            BlitRasterGlyph unchanged, saved;
            unsigned long renders = blit_raster_render_count(r);
            size_t bytes = blit_raster_cached_bytes(r);
            memset(&unchanged, 0x5a, sizeof(unchanged)); saved = unchanged;
            /* Uncovered Arabic block scalar: no silent PSF/question mark,
               no cache insertion, and caller's prior result unchanged. */
            if (blit_raster_get(r, 0x08ff, 1, &unchanged) ||
                memcmp(&unchanged, &saved, sizeof(saved)) ||
                blit_raster_render_count(r)!=renders || blit_raster_cached_bytes(r)!=bytes)
                return 2;
        }
        if (blit_raster_set_arabic(r, argv[3])) return 2;
        blit_raster_close(r);
    }
    blit_font_free(&fallback);
    fclose(specimens);
    if (!overhangs || !changes) return 2;
    printf("ARABIC_RASTER cases=%d failed=%d ink_overhangs=%d mark_differences=%d\n", cases, failures, overhangs, changes);
    return failures ? 1 : 0;
}
