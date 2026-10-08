/* Assert one exact pixel in a direct-KMS BMP screenshot. */
#include <stdio.h>
#include <stdlib.h>

static unsigned int little16(const unsigned char *p)
{
    return (unsigned int)p[0] | ((unsigned int)p[1] << 8);
}

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
    int red;
    int green;
    int blue;
    long position;

    if (argc != 7) return 2;
    x = atoi(argv[2]);
    y = atoi(argv[3]);
    red = atoi(argv[4]);
    green = atoi(argv[5]);
    blue = atoi(argv[6]);
    file = fopen(argv[1], "rb");
    if (file == NULL || fread(header, 1, sizeof(header), file) != sizeof(header))
        return 1;
    width = (int)little32(header + 18);
    height = (int)little32(header + 22);
    offset = (long)little32(header + 10);
    if (header[0] != 'B' || header[1] != 'M' || little32(header + 14) != 40 ||
        little16(header + 28) != 32 || width < 1 || height < 1 ||
        x < 0 || x >= width || y < 0 || y >= height || offset < 54) {
        fclose(file);
        return 1;
    }
    position = offset + ((long)(height - 1 - y) * width + x) * 4L;
    if (fseek(file, position, SEEK_SET) != 0 ||
        fread(pixel, 1, sizeof(pixel), file) != sizeof(pixel)) {
        fclose(file);
        return 1;
    }
    fclose(file);
    if (pixel[2] != (unsigned char)red || pixel[1] != (unsigned char)green ||
        pixel[0] != (unsigned char)blue)
        return 1;
    printf("BMP_PIXEL_PASS x=%d y=%d rgb=%d,%d,%d\n",
           x, y, red, green, blue);
    return 0;
}
