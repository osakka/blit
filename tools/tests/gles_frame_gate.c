/* ADR 0181 step 2: the GLES compositor's first frame. A frame drawn by GLES
   on Mesa's surfaceless platform must read back byte for byte as the frame
   handed to it: the pattern below catches a wrong row order, a red/blue
   swap, a half-pixel offset (bleed between neighbouring pixels) and a
   padded pitch. */
#include "../../src/blit_gles.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static uint32_t pattern(int x, int y, int w, int h)
{
    /* Every pixel distinct from its neighbours in every channel. */
    uint32_t r = (uint32_t)((x * 7 + y * 3) & 0xff);
    uint32_t g = (uint32_t)((x * 13 + y * 29 + 11) & 0xff);
    uint32_t b = (uint32_t)((y * 17 + x * 5 + (x == w - 1) * 40 + (y == h - 1) * 80) & 0xff);
    return (r << 16) | (g << 8) | b;
}

static int check(BlitGles *gles, int w, int h, int padding)
{
    char error[256];
    int pitch = w * 4 + padding;
    unsigned char *frame = calloc((size_t)pitch * (size_t)h, 1);
    uint32_t *back = calloc((size_t)w * (size_t)h, 4);
    BlitGlesTarget *target = blit_gles_target_open(gles, w, h, error, sizeof(error));
    int x, y, bad = 0;
    if (!frame || !back || !target) {
        fprintf(stderr, "GLES_FRAME_FAIL target %dx%d: %s\n", w, h, target ? "alloc" : error);
        exit(1);
    }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            ((uint32_t *)(frame + (size_t)y * pitch))[x] = pattern(x, y, w, h) | 0xff000000U;
    if (blit_gles_draw_frame(gles, target, frame, pitch, error, sizeof(error)) < 0 ||
        blit_gles_read_frame(gles, target, back, w * 4) < 0) {
        fprintf(stderr, "GLES_FRAME_FAIL draw %dx%d: %s\n", w, h, error);
        exit(1);
    }
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x)
            if ((back[y * w + x] & 0xffffffU) != pattern(x, y, w, h)) {
                if (!bad)
                    fprintf(stderr, "GLES_FRAME_FAIL %dx%d pad=%d first at %d,%d: got %06x want %06x\n",
                            w, h, padding, x, y, back[y * w + x] & 0xffffffU, pattern(x, y, w, h));
                ++bad;
            }
    blit_gles_target_close(gles, target);
    free(frame);
    free(back);
    return bad;
}

int main(void)
{
    char error[256];
    BlitGles *gles = blit_gles_open_surfaceless(error, sizeof(error));
    int bad = 0;
    if (gles == NULL) {
        fprintf(stderr, "GLES_FRAME_FAIL no surfaceless GLES: %s\n", error);
        return 1;
    }
    bad += check(gles, 1024, 768, 0);
    bad += check(gles, 1920, 2160, 0);
    bad += check(gles, 333, 77, 12);
    if (bad) return 1;
    printf("GLES_FRAME_PASS sizes=1024x768,1920x2160,333x77+pad exact renderer=\"%s\"\n",
           blit_gles_identity(gles));
    blit_gles_close(gles);
    return 0;
}
