#ifndef BLIT_GRAPHICS_H
#define BLIT_GRAPHICS_H

#include <stddef.h>
#include <stdint.h>
#include <stdlib.h>
#include <string.h>

/* ADR 0226: a session's pictures live in a table that grows on demand, and
   memory, not a count, is what bounds them (the budgets below). The count
   is only a ceiling against a runaway program, far above any desktop: past
   it a new picture is refused (Kitty ENOSPC, the API 409). */
#define BLIT_GRAPHICS_MAX_IMAGES 4096
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

/* ADR 0226: the ceiling on a session's placements, every picture's default
   placement and numbered ones (the p key) together. Each picture's numbered
   placements live in a table of their own that grows on demand. */
#define BLIT_GRAPHICS_MAX_PLACEMENTS 16384

/* ADR 0226: a picture budget -- the bytes of decoded pixels Blit keeps (CPU
   copies, sixel and Kitty scratch, and the GPU textures made from them).
   A session's budget adds to the server's (`server`), whose own is NULL. */
typedef struct BlitGraphicsBudget {
    unsigned long long used;
    struct BlitGraphicsBudget *server;
} BlitGraphicsBudget;

/* ADR 0226: the one accounting function. `held` is what one holder (a
   picture's pixels, a scratch buffer, a texture) is charged now; it becomes
   `bytes`, and the budget and the server's move by the difference. Every
   allocation and free of picture memory comes through here, so what is
   reported and what is enforced cannot disagree. A NULL budget only records
   the holder's charge. */
static inline void blit_graphics_account(BlitGraphicsBudget *budget,
                                         size_t *held, size_t bytes)
{
    BlitGraphicsBudget *b;
    for (b = budget; b != NULL; b = b->server)
        b->used = b->used - (unsigned long long)*held + (unsigned long long)bytes;
    *held = bytes;
}

/* ADR 0223: one dmabuf frame of a descriptor image, imported by the GPU
   compositor. Its plane descriptors (and so the producer's buffer) are held
   until a newer frame of the image has been presented; then Blit answers
   RELEASE and closes them. The compositor only reads `texture` and sets
   `drawn` when a frame it composed sampled it. */
#define BLIT_DMABUF_PLANES 4
typedef struct {
    int used;
    unsigned long long sequence;
    int fds[BLIT_DMABUF_PLANES];
    int fd_count;
    unsigned int texture;   /* the GL texture bound to the EGLImage */
    void *egl_image;
    int drawn;
} BlitGraphicsDmabufBuffer;

struct BlitKms;
typedef struct {
    struct BlitKms *kms;    /* whose GLES context holds the textures */
    int reply_fd;           /* a dup of the sender's socket, for RELEASE */
    unsigned long long reply_serial;
    unsigned int image_id;
    /* What a release adds to: the session's count and Blit's. */
    unsigned long *session_released;
    unsigned long long *runtime_released;
    /* The frame shown now, and the one before it until `shown` is drawn. */
    BlitGraphicsDmabufBuffer shown, retiring;
} BlitGraphicsDmabuf;

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

/* ADR 0200: one in-place edit of an image's pixels -- the rectangle that
   changed from generation `from` to generation `to`. A renderer whose copy
   is at `from` need only refresh that rectangle. */
typedef struct {
    unsigned long long from;
    unsigned long long to;
    int x, y, width, height;
} BlitGraphicsEdit;

#define BLIT_GRAPHICS_EDIT_HISTORY 8

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
       sixel and API frames have. ADR 0226: a table of `placement_capacity`
       slots that grows on demand (doubling) and is freed with the image. */
    BlitGraphicsPlacement *placements;
    int placement_capacity;
    unsigned long long revision;
    /* Unique identity for the pixel contents.  The GPU compositor uses this
       to discard a texture when a Kitty/Sixel image is replaced or updated. */
    unsigned long long generation;
    /* ADR 0202: pixels arrive by descriptor, never through the PTY, and a
       frame not newer than the last is dropped. */
    int descriptor;
    unsigned long long descriptor_sequence;
    /* The last edits, oldest first; cleared by anything that changes the
       image some other way. Empty means renderers refresh it whole. */
    BlitGraphicsEdit edits[BLIT_GRAPHICS_EDIT_HISTORY];
    int edit_count;
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
    /* ADR 0213: deleted by the program inside a synchronized update, which
       is still holding the frame that shows it. The image keeps its pixels
       and placements, so a whole redraw while the update is open draws the
       held picture rather than a hole, and nothing can find it by id; it is
       freed when the update ends. */
    int retired;
    char retired_reason[16];
    /* ADR 0223: set while the picture is a dmabuf frame the GPU imported.
       `pixels` is then NULL and the CPU compositor does not draw it. */
    BlitGraphicsDmabuf *dmabuf;
    /* ADR 0226: the budget this picture is charged to (its session's), what
       its pixels and its GPU texture are charged now (the texture by the
       compositor that made it), and when it was last shown -- the frame
       that last drew one of its placements, or its creation if none has.
       Unshown pictures are freed least recently shown first. */
    BlitGraphicsBudget *budget;
    size_t charged;
    size_t texture_charged;
    unsigned long long shown_at;
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
    /* Discovery order, which settles a tie in the sort. */
    int order;
} BlitGraphicsDraw;

/* ADR 0226: what a renderer draws in one frame, a list that grows with the
   pictures (it was sized for 16 pictures of 9 placements). Kept by its
   owner between frames, so a steady scene allocates nothing. */
typedef struct {
    BlitGraphicsDraw *draws;
    int count;
    int capacity;
} BlitGraphicsDrawList;

static inline int blit_graphics_draws_reserve(BlitGraphicsDrawList *list, int need)
{
    BlitGraphicsDraw *grown;
    int capacity = list->capacity > 0 ? list->capacity : 64;
    if (need <= list->capacity) return 1;
    while (capacity < need) capacity *= 2;
    grown = (BlitGraphicsDraw *)realloc(list->draws, (size_t)capacity * sizeof(*grown));
    if (grown == NULL) return 0;
    list->draws = grown;
    list->capacity = capacity;
    return 1;
}

static inline void blit_graphics_draws_free(BlitGraphicsDrawList *list)
{
    free(list->draws);
    memset(list, 0, sizeof(*list));
}

/* ADR 0226: a free numbered placement slot of the image, the table grown
   (doubling) when it has none. NULL only when memory runs out. */
static inline BlitGraphicsPlacement *blit_graphics_placement_slot(BlitGraphicsImage *image)
{
    BlitGraphicsPlacement *grown;
    int slot, capacity;
    for (slot = 0; slot < image->placement_capacity; ++slot)
        if (!image->placements[slot].used) return &image->placements[slot];
    capacity = image->placement_capacity > 0 ? image->placement_capacity * 2 : 4;
    grown = (BlitGraphicsPlacement *)realloc(image->placements,
                                             (size_t)capacity * sizeof(*grown));
    if (grown == NULL) return NULL;
    memset(grown + image->placement_capacity, 0,
           (size_t)(capacity - image->placement_capacity) * sizeof(*grown));
    image->placements = grown;
    slot = image->placement_capacity;
    image->placement_capacity = capacity;
    return &image->placements[slot];
}

/* How many placements the image has: its default one, if shown, and its
   numbered ones. */
static inline int blit_graphics_placement_count(const BlitGraphicsImage *image)
{
    int slot, count = image->visible ? 1 : 0;
    for (slot = 0; slot < image->placement_capacity; ++slot)
        if (image->placements[slot].used) ++count;
    return count;
}

/* ADR 0200: the rectangle that changed since a renderer's copy at
   generation `since`, found by walking the edit history back from the
   image's generation. 0 when the history does not reach `since`, so the
   copy must be refreshed whole. */
static inline int blit_graphics_edited_since(const BlitGraphicsImage *image,
                                             unsigned long long since,
                                             int *x, int *y, int *width, int *height)
{
    unsigned long long at = image->generation;
    int x0 = 0, y0 = 0, x1 = 0, y1 = 0, found = 0, i;
    while (at != since) {
        for (i = image->edit_count - 1; i >= 0; --i)
            if (image->edits[i].to == at) break;
        if (i < 0) return 0;
        {
            const BlitGraphicsEdit *e = &image->edits[i];
            if (!found) {
                x0 = e->x; y0 = e->y; x1 = e->x + e->width; y1 = e->y + e->height;
                found = 1;
            } else {
                if (e->x < x0) x0 = e->x;
                if (e->y < y0) y0 = e->y;
                if (e->x + e->width > x1) x1 = e->x + e->width;
                if (e->y + e->height > y1) y1 = e->y + e->height;
            }
            at = e->from;
        }
    }
    if (!found) return 0;
    *x = x0; *y = y0; *width = x1 - x0; *height = y1 - y0;
    return 1;
}

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

static int blit_graphics_draw_compare(const void *a, const void *b)
{
    const BlitGraphicsDraw *left = (const BlitGraphicsDraw *)a;
    const BlitGraphicsDraw *right = (const BlitGraphicsDraw *)b;
    if (blit_graphics_draw_precedes(left, right)) return -1;
    if (blit_graphics_draw_precedes(right, left)) return 1;
    return left->order < right->order ? -1 : left->order > right->order;
}

/* Every visible placement of every drawable image, ordered by z, image id and
   placement id, then by the order they were found in. `images` holds `count`
   pictures, NULL where a slot is free. Returns the number in the list, which
   is short only when memory ran out. */
static inline int blit_graphics_collect(BlitGraphicsImage *const *images,
                                        int count, BlitGraphicsDrawList *list)
{
    int image_index;
    list->count = 0;
    for (image_index = 0; image_index < count; ++image_index) {
        const BlitGraphicsImage *image = images[image_index];
        int slot;
        if (image == NULL || !image->used || !image->complete ||
            (image->pixels == NULL &&
             (image->dmabuf == NULL || !image->dmabuf->shown.used)) ||
            image->width < 1 || image->height < 1)
            continue;
        for (slot = -1; slot < image->placement_capacity; ++slot) {
            BlitGraphicsDraw draw;
            int ok;
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
            if (!blit_graphics_draws_reserve(list, list->count + 1)) goto sort;
            draw.order = list->count;
            list->draws[list->count++] = draw;
        }
    }
sort:
    if (list->count > 1)
        qsort(list->draws, (size_t)list->count, sizeof(list->draws[0]),
              blit_graphics_draw_compare);
    return list->count;
}

/* Whether anything of this image is currently shown. */
static inline int blit_graphics_shown(const BlitGraphicsImage *image)
{
    int slot;
    if (image->visible) return 1;
    for (slot = 0; slot < image->placement_capacity; ++slot)
        if (image->placements[slot].used) return 1;
    return 0;
}

#endif
