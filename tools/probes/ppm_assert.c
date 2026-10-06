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

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char rgb[3];
    long size;
    char header[32];

    int require_wide = 0;
    int require_kitty = 0;
    int argument;

    if (argc < 2 || argc > 4) return 2;
    for (argument = 2; argument < argc; ++argument) {
        if (strcmp(argv[argument], "--wide") == 0) require_wide = 1;
        else if (strcmp(argv[argument], "--kitty") == 0) require_kitty = 1;
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
    if (fseek(file, 0, SEEK_END) != 0 || (size = ftell(file)) != 2359312L ||
        !pixel(file, 16L, rgb) || rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0 ||
        !pixel(file, 16L + ((long)100 * 1024L + 100) * 3L, rgb) ||
        rgb[0] != 0 || rgb[1] != 0 || rgb[2] != 0 ||
        !has_foreground(file) ||
        (require_wide && !has_wide_foreground(file)) ||
        (require_kitty && !has_kitty_red(file))) {
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}
