#ifndef BLIT_GRAPHICS_SCENE_H
#define BLIT_GRAPHICS_SCENE_H
#include "blit_graphics.h"
#include <string.h>

/* Immutable presented descriptors; no borrowed pixels or producer ownership. */
typedef struct {
    unsigned long long generation;
    unsigned int image_id, placement_id;
    int x, y, width, height, z, sx, sy, sw, sh;
    int image_width, image_height, format, opaque;
} BlitGraphicsStamp;
typedef struct {
    int valid, count;
    unsigned long long signature;
    BlitGraphicsStamp items[BLIT_GRAPHICS_MAX_DRAWS];
} BlitGraphicsScene;

static void blit_graphics_scene_build(BlitGraphicsScene *scene,
                                     const BlitGraphicsImage *images, int count)
{
    BlitGraphicsDraw draws[BLIT_GRAPHICS_MAX_DRAWS];
    int i;
    const unsigned char *bytes;
    size_t n;
    memset(scene, 0, sizeof(*scene));
    scene->valid = 1;
    scene->count = blit_graphics_collect(images, count, draws, BLIT_GRAPHICS_MAX_DRAWS);
    for (i = 0; i < scene->count; ++i) {
        const BlitGraphicsDraw *d = &draws[i];
        BlitGraphicsStamp *s = &scene->items[i];
        s->generation = d->image->generation;
        s->image_id = d->image_id; s->placement_id = d->placement_id;
        s->x = d->x; s->y = d->y; s->width = d->width; s->height = d->height;
        s->z = d->z; s->sx = d->src_x; s->sy = d->src_y;
        s->sw = d->src_w; s->sh = d->src_h;
        s->image_width = d->image->width; s->image_height = d->image->height;
        s->format = d->image->format; s->opaque = d->image->opaque;
    }
    scene->signature = 1469598103934665603ULL;
    bytes = (const unsigned char *)scene->items;
    for (n = 0; n < (size_t)scene->count * sizeof(scene->items[0]); ++n)
        scene->signature = (scene->signature ^ bytes[n]) * 1099511628211ULL;
}

/* Emit old/new coverage of only changed descriptors. All z layers are then
   recomposited in those regions, including transparency and revealed pixels. */
static int blit_graphics_scene_delta(const BlitGraphicsScene *old,
                                     const BlitGraphicsScene *next,
                                     void (*damage)(void *, const BlitGraphicsStamp *),
                                     void *owner)
{
    int i, j, changed = 0;
    for (i = 0; i < old->count; ++i) {
        for (j = 0; j < next->count; ++j)
            if (old->items[i].image_id == next->items[j].image_id &&
                old->items[i].placement_id == next->items[j].placement_id) break;
        if (j == next->count || memcmp(&old->items[i], &next->items[j], sizeof(old->items[i]))) {
            damage(owner, &old->items[i]); changed = 1;
        }
    }
    for (i = 0; i < next->count; ++i) {
        for (j = 0; j < old->count; ++j)
            if (next->items[i].image_id == old->items[j].image_id &&
                next->items[i].placement_id == old->items[j].placement_id) break;
        if (j == old->count || memcmp(&next->items[i], &old->items[j], sizeof(next->items[i]))) {
            damage(owner, &next->items[i]); changed = 1;
        }
    }
    return changed;
}
#endif
