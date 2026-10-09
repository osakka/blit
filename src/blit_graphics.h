#ifndef BLIT_GRAPHICS_H
#define BLIT_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>

#define BLIT_GRAPHICS_MAX_IMAGES 8
/* A physical 1920x2160 output needs a full-screen RGB image for Hibr
   wallpaper (12,441,600 bytes). Keep the limit bounded while covering
   4096-class displays and RGBA frames without silently forcing a glyph
   fallback. */
#define BLIT_GRAPHICS_MAX_WIDTH 4096
#define BLIT_GRAPHICS_MAX_HEIGHT 4096
#define BLIT_GRAPHICS_MAX_BYTES (64U * 1024U * 1024U)
/* Staging bound for a compressed transmission whose decoded size is not
   known until it has been parsed (Kitty f=100). A PNG large enough to fill
   a 4096-class display compresses far below this; a payload that exceeds
   it is refused rather than allowed to grow the allocation. ADR 0056. */
#define BLIT_GRAPHICS_MAX_ENCODED_BYTES (8U * 1024U * 1024U)

/* Renderer-neutral graphics record.  Kitty and sixel decoders both publish
   into this store; SDL3 and direct KMS receive read-only views of the same
   pixels. */
typedef struct {
    int used;
    int complete;
    unsigned int id;
    char protocol[8];
    int width;
    int height;
    int format;
    unsigned char *pixels;
    size_t bytes;
    size_t capacity;
    int visible;
    int opaque;
    int x;
    int y;
    int placement_width;
    int placement_height;
    int z;
    unsigned long long revision;
    /* Unique identity for the pixel contents.  SDL output caches use this
       to discard a texture when a Kitty/Sixel image is replaced or updated. */
    unsigned long long generation;
    unsigned char base64_tail[4];
    int base64_tail_length;
    /* Set while `pixels` holds an undecoded container rather than raw
       samples. A PNG transmission (Kitty f=100) accumulates its compressed
       bytes here and is decoded in place when the final chunk arrives, at
       which point width/height/format come from the image itself and this
       clears. ADR 0056. */
    int encoded;
} BlitGraphicsImage;

#endif
