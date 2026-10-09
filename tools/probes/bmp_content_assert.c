/* Assert that a screenshot contains enough visible pixels in its interior. */
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
    unsigned int offset, width, height, bits, row_bytes;
    unsigned int minimum, visible = 0;
    unsigned int x, y;

    if (argc != 3)
        return 2;
    minimum = (unsigned int)strtoul(argv[2], NULL, 10);
    file = fopen(argv[1], "rb");
    if (file == NULL || fread(header, 1, sizeof(header), file) != sizeof(header))
        return 1;
    width = little32(header + 18);
    height = little32(header + 22);
    offset = little32(header + 10);
    bits = little16(header + 28);
    if (header[0] != 'B' || header[1] != 'M' || little32(header + 14) != 40 ||
        (bits != 24 && bits != 32) || width < 32 || height < 32 ||
        offset < 54) {
        fclose(file);
        return 1;
    }
    row_bytes = ((width * (bits / 8U) + 3U) / 4U) * 4U;
    for (y = 16; y + 16 < height && visible < minimum; y += 2) {
        for (x = 16; x + 16 < width && visible < minimum; x += 2) {
            long position = (long)offset +
                (long)(height - 1U - y) * row_bytes +
                (long)x * (bits / 8U);
            if (fseek(file, position, SEEK_SET) != 0 ||
                fread(pixel, 1, bits / 8U, file) != bits / 8U) {
                fclose(file);
                return 1;
            }
            if (pixel[0] > 8 || pixel[1] > 8 || pixel[2] > 8)
                ++visible;
        }
    }
    fclose(file);
    printf("BMP_CONTENT_RESULT visible_samples=%u minimum=%u\n",
           visible, minimum);
    if (visible < minimum)
        return 1;
    printf("BMP_CONTENT_PASS visible_samples=%u minimum=%u\n",
           visible, minimum);
    return 0;
}
