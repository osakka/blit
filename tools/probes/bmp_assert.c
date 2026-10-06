/* Deterministic 24-bit BMP oracle for the SDL3 Kitty pixel fixture. */
#include <stdio.h>
#include <stdint.h>
#include <string.h>

static uint32_t little32(const unsigned char *bytes)
{
    return (uint32_t)bytes[0] | ((uint32_t)bytes[1] << 8) |
           ((uint32_t)bytes[2] << 16) | ((uint32_t)bytes[3] << 24);
}

static uint16_t little16(const unsigned char *bytes)
{
    return (uint16_t)bytes[0] | ((uint16_t)bytes[1] << 8);
}

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char header[54];
    unsigned char pixel[3];
    uint32_t offset;
    uint32_t width;
    int32_t height;
    uint32_t row;
    long position;
    int require_green = 0;
    int require_sixel = 0;

    if (argc < 2 || argc > 4) return 2;
    if (argc == 3) {
        if (strcmp(argv[2], "--green") != 0) return 2;
        require_green = 1;
    } else if (argc == 4 && strcmp(argv[2], "--sixel") == 0) {
        if (strcmp(argv[3], "--green") != 0) return 2;
        require_sixel = 1;
        require_green = 1;
    }
    file = fopen(argv[1], "rb");
    if (file == NULL || fread(header, 1, sizeof(header), file) != sizeof(header))
        return 1;
    offset = little32(header + 10);
    width = little32(header + 18);
    height = (int32_t)little32(header + 22);
    if (header[0] != 'B' || header[1] != 'M' || offset < 54 || width < 8 ||
        height <= 4 || little16(header + 28) != 24)
        return 1;
    row = (uint32_t)(height - 1 - 4);
    position = (long)offset + (long)row * (long)(((width * 3U + 3U) / 4U) * 4U) +
        4L * 3L;
    if (fseek(file, position, SEEK_SET) != 0 || fread(pixel, 1, 3, file) != 3) {
        fclose(file);
        return 1;
    }
    if (!require_green &&
        (pixel[0] != 0x00 || pixel[1] != 0x00 || pixel[2] != 0xff))
        return 1;
    if (require_green) {
        int y;
        for (y = 4; y <= 36; y += 32) {
            row = (uint32_t)(height - 1 - y);
            position = (long)offset +
                (long)row * (long)(((width * 3U + 3U) / 4U) * 4U) + 4L * 3L;
            if (fseek(file, position, SEEK_SET) != 0 ||
                fread(pixel, 1, 3, file) != 3 || pixel[0] != 0x00 ||
                pixel[1] != 0xff || pixel[2] != 0x00)
                return 1;
        }
    }
    if (require_sixel) {
        row = (uint32_t)(height - 1 - 4);
        position = (long)offset +
            (long)row * (long)(((width * 3U + 3U) / 4U) * 4U) + 5L * 3L;
        if (fseek(file, position, SEEK_SET) != 0 ||
            fread(pixel, 1, 3, file) != 3 || pixel[0] != 0x00 ||
            pixel[1] != 0xff || pixel[2] != 0x00)
            return 1;
    }
    fclose(file);
    return 0;
}
