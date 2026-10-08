/* Deterministic P6 framebuffer oracle for the two-output KMS fixture. */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int pixel(FILE *file, long offset, unsigned char rgb[3])
{
    if (fseek(file, offset, SEEK_SET) != 0) return 0;
    return fread(rgb, 1, 3, file) == 3;
}

static int has_foreground(FILE *file)
{
    int x;
    int y;
    unsigned char rgb[3];
    for (y = 4; y < 20; ++y)
        for (x = 4; x < 12; ++x)
            if (pixel(file, 16L + ((long)y * 1024L + x) * 3L, rgb) &&
                rgb[0] == 0xbe && rgb[1] == 0xd2 && rgb[2] == 0xbe)
                return 1;
    return 0;
}

static int has_wide_foreground(FILE *file)
{
    int x;
    int y;
    int left = 0;
    int right = 0;
    unsigned char rgb[3];
    /* READY occupies columns 0..4. U+2500 starts at column 5 and
       must paint both halves of its two-cell logical width. */
    for (y = 0; y < 16; ++y) {
        for (x = 40; x < 48; ++x)
            if (pixel(file, 16L + ((long)y * 1024L + x) * 3L, rgb) &&
                rgb[0] == 0xbe && rgb[1] == 0xd2 && rgb[2] == 0xbe)
                left = 1;
        for (x = 48; x < 56; ++x)
            if (pixel(file, 16L + ((long)y * 1024L + x) * 3L, rgb) &&
                rgb[0] == 0xbe && rgb[1] == 0xd2 && rgb[2] == 0xbe)
                right = 1;
    }
    return left && right;
}

static int has_kitty_red(FILE *file)
{
    unsigned char rgb[3];
    return pixel(file, 16L + ((long)4 * 1024L + 4) * 3L, rgb) &&
        rgb[0] == 0xff && rgb[1] == 0x00 && rgb[2] == 0x00;
}

static int has_kitty_green_at(FILE *file, int y)
{
    unsigned char rgb[3];
    return pixel(file, 16L + ((long)y * 1024L + 4) * 3L, rgb) &&
        rgb[0] == 0x00 && rgb[1] == 0xff && rgb[2] == 0x00;
}

static int has_sixel_pixels(FILE *file)
{
    unsigned char red[3];
    unsigned char green[3];
    return pixel(file, 16L + ((long)4 * 1024L + 4) * 3L, red) &&
        pixel(file, 16L + ((long)4 * 1024L + 5) * 3L, green) &&
        red[0] == 0xff && red[1] == 0x00 && red[2] == 0x00 &&
        green[0] == 0x00 && green[1] == 0xff && green[2] == 0x00;
}

static int has_api_pixel_canvas(FILE *file)
{
    int x;
    int y;
    unsigned char rgb[3];
    for (y = 16; y < 48; ++y)
        for (x = 16; x < 48; ++x)
            if (pixel(file, 16L + ((long)y * 1024L + x) * 3L, rgb) &&
                rgb[0] == 0xff && rgb[1] == 0x00 && rgb[2] == 0x00)
                return 1;
    return 0;
}

static int has_scanline_suppression(FILE *file)
{
    int y;
    int x;
    unsigned char even[3];
    unsigned char odd[3];

    for (y = 0; y < 30; y += 2)
        for (x = 0; x < 128; ++x)
            if (pixel(file, 16L + ((long)y * 1024L + x) * 3L, even) &&
                (even[0] != 0 || even[1] != 0 || even[2] != 0) &&
                pixel(file, 16L + ((long)(y + 1) * 1024L + x) * 3L, odd) &&
                odd[0] == 0 && odd[1] == 0 && odd[2] == 0)
                return 1;
    return 0;
}

static void report_pixel(FILE *file, int y)
{
    unsigned char rgb[3];
    if (pixel(file, 16L + ((long)y * 1024L + 4) * 3L, rgb))
        fprintf(stdout, "KITTY_PIXEL y=%d rgb=%u,%u,%u\n", y,
                rgb[0], rgb[1], rgb[2]);
}

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char rgb[3];
    long size;
    char header[32];

    int require_wide = 0;
    int require_kitty = 0;
    int require_green = 0;
    int require_sixel = 0;
    int require_visuals = 0;
    int require_api_pixel = 0;
    int argument;

    if (argc < 2 || argc > 4) return 2;
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--wide") == 0) require_wide = 1;
        else if (strcmp(argv[argument], "--kitty") == 0) require_kitty = 1;
        else if (strcmp(argv[argument], "--green") == 0) require_green = 1;
        else if (strcmp(argv[argument], "--sixel") == 0) require_sixel = 1;
        else if (strcmp(argv[argument], "--visuals") == 0) require_visuals = 1;
        else if (strcmp(argv[argument], "--api-pixel") == 0) require_api_pixel = 1;
        else return 2;
    }
    file = fopen(argv[1], "rb");
    if (file == NULL) return 1;
    if (fgets(header, sizeof(header), file) == NULL ||
        strcmp(header, "P6\n") != 0 ||
        fgets(header, sizeof(header), file) == NULL ||
        strcmp(header, "1024 768\n") != 0 ||
        fgets(header, sizeof(header), file) == NULL ||
        strcmp(header, "255\n") != 0) {
        fclose(file);
        return 1;
    }
    if (require_green) {
        report_pixel(file, 4);
        report_pixel(file, 36);
    }
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) != 2359312L ||
        !pixel(file, 16L, rgb) || rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0 ||
        !pixel(file, 16L + ((long)100 * 1024L + 100) * 3L, rgb) ||
        rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0 ||
        (!require_visuals && !has_foreground(file)) ||
        (require_wide && !has_wide_foreground(file)) ||
        (require_kitty && !has_kitty_red(file)) ||
        (require_sixel && !has_sixel_pixels(file)) ||
        (require_api_pixel && !has_api_pixel_canvas(file)) ||
        (require_visuals && !has_scanline_suppression(file)) ||
        (require_green && !has_kitty_green_at(file, 4)) ||
        (require_green && !has_kitty_green_at(file, 36))) {
        fclose(file);
        return 1;
    }
    if (require_visuals) {
        unsigned char rgb[3];
        if (!pixel(file, 16L + ((long)32 * 1024L + 2) * 3L, rgb) ||
            rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 255) {
            fclose(file);
            return 1;
        }
    }
    fclose(file);
    return 0;
}
