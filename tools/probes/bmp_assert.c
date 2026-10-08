/* Deterministic BMP oracle for the direct-KMS screenshot endpoint. */
#include <stdio.h>
#include <string.h>

static unsigned int little32(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8) |
        ((unsigned int)p[2] << 16) | ((unsigned int)p[3] << 24);
}

int main(int argc, char **argv)
{
    FILE *file;
    unsigned char header[54];
    unsigned char pixel[4];
    long offset;
    int width;
    int height;
    int x;
    int y;

    if (argc != 2) return 2;
    file = fopen(argv[1], "rb");
    if (file == NULL || fread(header, 1, sizeof(header), file) != sizeof(header))
        return 1;
    if (header[0] != 'B' || header[1] != 'M' || little32(header + 14) != 40 ||
        header[28] != 32 || header[29] != 0 || header[30] != 0 ||
        header[31] != 0)
        return 1;
    offset = (long)little32(header + 10);
    width = (int)little32(header + 18);
    height = (int)little32(header + 22);
    if (width != 1024 || height != 768 || offset < 54)
        return 1;
    for (y = 16; y < 48; ++y)
        for (x = 16; x < 48; ++x) {
            long position = offset + ((long)(height - 1 - y) * width + x) * 4L;
            if (fseek(file, position, SEEK_SET) != 0 ||
                fread(pixel, 1, sizeof(pixel), file) != sizeof(pixel))
                return 1;
            if (pixel[0] == 0 && pixel[1] == 0 && pixel[2] == 255) {
                fclose(file);
                return 0;
            }
        }
    fclose(file);
    return 1;
}
