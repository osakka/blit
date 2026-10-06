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

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char rgb[3];
    long size;
    char header[32];

    if (argc != 2) return 2;
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
        !has_foreground(file)) {
        fclose(file);
        return 1;
    }
    fclose(file);
    return 0;
}
