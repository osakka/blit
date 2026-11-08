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
/* ADR 0226: `items` grows with the scene and is kept between builds; a
   scene is copied with blit_graphics_scene_assign and freed with
   blit_graphics_scene_free, never by assignment. */
typedef struct {
    int valid, count, capacity;
    unsigned long long signature;
    BlitGraphicsStamp *items;
} BlitGraphicsScene;

static int blit_graphics_scene_reserve(BlitGraphicsScene *scene, int need)
{
    BlitGraphicsStamp *grown;
    int capacity = scene->capacity > 0 ? scene->capacity : 16;
    if (need <= scene->capacity) return 1;
    while (capacity < need) capacity *= 2;
    grown = (BlitGraphicsStamp *)realloc(scene->items, (size_t)capacity * sizeof(*grown));
    if (grown == NULL) return 0;
    scene->items = grown;
    scene->capacity = capacity;
    return 1;
}

static void blit_graphics_scene_free(BlitGraphicsScene *scene)
{
    free(scene->items);
    memset(scene, 0, sizeof(*scene));
}

/* `to` becomes a copy of `from`. A copy that cannot be made leaves `to`
   invalid, which makes the next frame a full redraw. */
static void blit_graphics_scene_assign(BlitGraphicsScene *to, const BlitGraphicsScene *from)
{
    if (!blit_graphics_scene_reserve(to, from->count)) {
        to->valid = 0;
        to->count = 0;
        return;
    }
    if (from->count > 0)
        memcpy(to->items, from->items, (size_t)from->count * sizeof(from->items[0]));
    to->valid = from->valid;
    to->count = from->count;
    to->signature = from->signature;
}

static void blit_graphics_scene_build(BlitGraphicsScene *scene,
                                     BlitGraphicsImage *const *images, int count)
{
    static BlitGraphicsDrawList list;
    BlitGraphicsDraw *draws;
    int i;
    const unsigned char *bytes;
    size_t n;
    scene->valid = 1;
    scene->signature = 0;
    scene->count = blit_graphics_collect(images, count, &list);
    if (!blit_graphics_scene_reserve(scene, scene->count)) scene->count = 0;
    draws = list.draws;
    for (i = 0; i < scene->count; ++i) {
        const BlitGraphicsDraw *d = &draws[i];
        BlitGraphicsStamp *s = &scene->items[i];
        memset(s, 0, sizeof(*s));
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
    /* A steady scene keeps its order, so the same index is tried first and
       the search is linear only where something moved (ADR 0226). */
#define BLIT_SCENE_SAME(a, b) ((a).image_id == (b).image_id && \
                               (a).placement_id == (b).placement_id)
    for (i = 0; i < old->count; ++i) {
        if (i < next->count && BLIT_SCENE_SAME(old->items[i], next->items[i]))
            j = i;
        else
            for (j = 0; j < next->count; ++j)
                if (BLIT_SCENE_SAME(old->items[i], next->items[j])) break;
        if (j == next->count || memcmp(&old->items[i], &next->items[j], sizeof(old->items[i]))) {
            damage(owner, &old->items[i]); changed = 1;
        }
    }
    for (i = 0; i < next->count; ++i) {
        if (i < old->count && BLIT_SCENE_SAME(next->items[i], old->items[i]))
            j = i;
        else
            for (j = 0; j < old->count; ++j)
                if (BLIT_SCENE_SAME(next->items[i], old->items[j])) break;
        if (j == old->count || memcmp(&next->items[i], &old->items[j], sizeof(next->items[i]))) {
            damage(owner, &next->items[i]); changed = 1;
        }
    }
#undef BLIT_SCENE_SAME
    return changed;
}
#endif
