/* Production scaler: clipping must bound work, not just final pixel stores. */
#include "../../src/blit_kms.c"

int main(void)
{
    KmsOutput output;
    BlitGraphicsImage image;
    BlitGraphicsDraw draw;
    uint32_t *pixels, *reference;
    unsigned char source[97*53*4];
    long long started, full_us, clipped_us;
    int x, y, i, format;
    memset(&output, 0, sizeof(output));
    memset(&image, 0, sizeof(image));
    memset(&draw, 0, sizeof(draw));
    pixels = malloc(1920*1080*4); reference = malloc(1920*1080*4);
    if (!pixels || !reference) return 2;
    for (i = 0; i < sizeof(source); ++i) source[i] = (unsigned char)(i*37+11);
    output.mode.hdisplay = 1920; output.mode.vdisplay = 1080;
    output.back.map = pixels; output.back.pitch = 1920*4;
    image.width = 97; image.height = 53; image.pixels = source;
    draw.image = &image; draw.src_x = 3; draw.src_y = 2;
    draw.src_w = 91; draw.src_h = 47; draw.width = 1919; draw.height = 1079;
    draw.x = -7; draw.y = -9;
    for (format = 24; format <= 32; format += 8) {
        image.format = format;
        for (i = 0; i < 1920*1080; ++i) pixels[i] = 0x123456;
        draw_graphic(&output, &draw, 0, 0, 0, 0, 1920, 1080);
        memcpy(reference, pixels, 1920*1080*4);
        for (i = 0; i < 1920*1080; ++i) pixels[i] = 0x123456;
        draw_graphic(&output, &draw, 0, 0, 103, 71, 127, 103);
        for (y = 0; y < 1080; ++y) for (x = 0; x < 1920; ++x)
            if (pixels[y*1920+x] != (x>=103 && x<127 && y>=71 && y<103 ? reference[y*1920+x] : 0x123456)) {
                fprintf(stderr, "KMS_CLIP_PIXEL_FAIL format=%d x=%d y=%d\n", format, x, y); return 1;
            }
    }
    image.format = 24;
    started = kms_now_us();
    for (i = 0; i < 20; ++i) draw_graphic(&output, &draw, 0, 0, 0, 0, 1920, 1080);
    full_us = kms_now_us()-started;
    started = kms_now_us();
    for (i = 0; i < 20; ++i) draw_graphic(&output, &draw, 0, 0, 103, 71, 127, 103);
    clipped_us = kms_now_us()-started;
    printf("KMS_CLIP_TIMING full_us=%lld clipped_us=%lld ratio=%.6f\n", full_us, clipped_us, (double)clipped_us/full_us);
    free(pixels); free(reference);
    if (full_us < 1 || clipped_us > full_us/10) {
        fprintf(stderr, "KMS_CLIP_WORK_FAIL small damage still scans the full destination image\n"); return 1;
    }
    puts("KMS_GRAPHICS_CLIP_PASS fractional_scale=exact rgb_rgba=exact negative_origin=exact outside_damage=unchanged work=bounded");
    return 0;
}
