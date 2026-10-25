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
/* The sixel decoder's bounds. They are named because XTSMGRAPHICS reports
   them to the application: an advertised limit that disagrees with the
   enforced one is worse than no answer, since a client sizes its image to
   what it was told.

   Not a limit of the decoder's own: a decoded sixel image is an ordinary
   graphics image, so its bound is the graphics image bound above, and a
   full-size scratch buffer is exactly BLIT_GRAPHICS_MAX_BYTES. Until issue
   #8 this was a literal 512, which is below any realistic picture -- a
   terminal-width image on a 1920-pixel output was refused outright, and the
   whole image was discarded rather than clipped. */
#define BLIT_SIXEL_MAX_WIDTH BLIT_GRAPHICS_MAX_WIDTH
#define BLIT_SIXEL_MAX_HEIGHT BLIT_GRAPHICS_MAX_HEIGHT
#define BLIT_SIXEL_COLOR_REGISTERS 256

/* A numbered Kitty placement (the p key) beyond the image's default one. */
#define BLIT_GRAPHICS_MAX_PLACEMENTS 8
#define BLIT_GRAPHICS_MAX_DRAWS \
    (BLIT_GRAPHICS_MAX_IMAGES * (BLIT_GRAPHICS_MAX_PLACEMENTS + 1))

/* Where one numbered placement puts an image, and which part of it. A zero
   source width or height means "to the image's edge". ADR 0060. */
typedef struct {
    int used;
    unsigned int id;
    int x;
    int y;
    int width;
    int height;
    int z;
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int scroll_anchored;
} BlitGraphicsPlacement;

/* Renderer-neutral graphics record.  Kitty and sixel decoders both publish
   into this store; the compositors receive read-only views of the same
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
    /* Source rectangle of the default placement (Kitty x, y, w, h). Zero
       width or height means to the edge of the image. ADR 0060. */
    int src_x;
    int src_y;
    int src_w;
    int src_h;
    int scroll_anchored;
    /* Placements addressed by a nonzero placement id. The fields above are
       the default placement, the one with id 0, which is also the only one
       sixel and API frames have. */
    BlitGraphicsPlacement placements[BLIT_GRAPHICS_MAX_PLACEMENTS];
    unsigned long long revision;
    /* Unique identity for the pixel contents.  The GPU compositor uses this
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
    /* Set while `pixels` holds a zlib stream rather than the bytes it
       inflates to (Kitty o=z). The compressed bytes accumulate here across
       chunks and are inflated in place when the final chunk arrives. It is
       independent of `encoded`: o=z with f=100 is a deflated PNG, so the
       payload is inflated first and then decoded. ADR 0056. */
    int compressed;
    /* Placement id plus one of a display (a=T) whose cursor movement is owed
       when the last chunk arrives, because the placement's size in cells is
       not known until then. Zero when nothing is owed. ADR 0060. */
    int advance;
} BlitGraphicsImage;

/* One thing a renderer draws: a placement with its defaults resolved and its
   source rectangle clamped to the image. Both renderers draw from a list of
   these rather than walking images, so a placement means the same thing on
   the CPU and GPU compositors. */
typedef struct {
    const BlitGraphicsImage *image;
    /* Kitty's retained scene orders equal-z references by image id and then
       placement id; discovery order is not a rendering contract. */
    unsigned int image_id;
    unsigned int placement_id;
    int x;
    int y;
    int width;
    int height;
    int z;
    int src_x;
    int src_y;
    int src_w;
    int src_h;
} BlitGraphicsDraw;

static inline int blit_graphics_draw_resolve(const BlitGraphicsImage *image,
                                             int x, int y, int width,
                                             int height, int z, int src_x,
                                             int src_y, int src_w, int src_h,
                                             BlitGraphicsDraw *out)
{
    if (src_x < 0) src_x = 0;
    if (src_y < 0) src_y = 0;
    if (src_x >= image->width || src_y >= image->height) return 0;
    if (src_w <= 0 || src_w > image->width - src_x) src_w = image->width - src_x;
    if (src_h <= 0 || src_h > image->height - src_y) src_h = image->height - src_y;
    if (width <= 0) width = src_w;
    if (height <= 0) height = src_h;
    out->image = image;
    out->image_id = image->id;
    out->placement_id = 0;
    out->x = x;
    out->y = y;
    out->width = width;
    out->height = height;
    out->z = z;
    out->src_x = src_x;
    out->src_y = src_y;
    out->src_w = src_w;
    out->src_h = src_h;
    return 1;
}

static inline int blit_graphics_draw_precedes(const BlitGraphicsDraw *left,
                                              const BlitGraphicsDraw *right)
{
    if (left->z != right->z) return left->z < right->z;
    if (left->image_id != right->image_id)
        return left->image_id < right->image_id;
    return left->placement_id < right->placement_id;
}

/* Every visible placement of every drawable image, ordered by z, image id and
   placement id. Returns the number written. */
static inline int blit_graphics_collect(const BlitGraphicsImage *images,
                                        int count, BlitGraphicsDraw *out,
                                        int capacity)
{
    int written = 0;
    int image_index;
    int i;
    for (image_index = 0; image_index < count &&
         image_index < BLIT_GRAPHICS_MAX_IMAGES; ++image_index) {
        const BlitGraphicsImage *image = &images[image_index];
        int slot;
        if (!image->used || !image->complete || image->pixels == NULL ||
            image->width < 1 || image->height < 1)
            continue;
        for (slot = -1; slot < BLIT_GRAPHICS_MAX_PLACEMENTS; ++slot) {
            BlitGraphicsDraw draw;
            int ok;
            if (written >= capacity) return written;
            if (slot < 0) {
                if (!image->visible) continue;
                ok = blit_graphics_draw_resolve(
                    image, image->x, image->y, image->placement_width,
                    image->placement_height, image->z, image->src_x,
                    image->src_y, image->src_w, image->src_h, &draw);
            } else {
                const BlitGraphicsPlacement *p = &image->placements[slot];
                if (!p->used) continue;
                ok = blit_graphics_draw_resolve(
                    image, p->x, p->y, p->width, p->height, p->z, p->src_x,
                    p->src_y, p->src_w, p->src_h, &draw);
                if (ok) draw.placement_id = p->id;
            }
            if (!ok) continue;
            for (i = written; i > 0 &&
                 blit_graphics_draw_precedes(&draw, &out[i - 1]); --i)
                out[i] = out[i - 1];
            out[i] = draw;
            ++written;
        }
    }
    return written;
}

/* Whether anything of this image is currently shown. */
static inline int blit_graphics_shown(const BlitGraphicsImage *image)
{
    int slot;
    if (image->visible) return 1;
    for (slot = 0; slot < BLIT_GRAPHICS_MAX_PLACEMENTS; ++slot)
        if (image->placements[slot].used) return 1;
    return 0;
}

#endif
