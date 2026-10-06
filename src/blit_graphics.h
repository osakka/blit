#ifndef BLIT_GRAPHICS_H
#define BLIT_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

#define BLIT_GRAPHICS_MAX_IMAGES 8
#define BLIT_GRAPHICS_MAX_WIDTH 512
#define BLIT_GRAPHICS_MAX_HEIGHT 512
#define BLIT_GRAPHICS_MAX_BYTES (4U * 1024U * 1024U)

/* Renderer-neutral Kitty image record.  Ownership remains with the terminal
   session; SDL3 and direct KMS receive read-only views of the same pixels. */
typedef struct {
    int used;
    int complete;
    unsigned int id;
    int width;
    int height;
    int format;
    unsigned char *pixels;
    size_t bytes;
    size_t capacity;
    int visible;
    int x;
    int y;
    int placement_width;
    int placement_height;
    int z;
    unsigned long long revision;
    unsigned char base64_tail[4];
    int base64_tail_length;
} BlitGraphicsImage;

#endif
