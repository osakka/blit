#ifndef BLIT_CELL_CLUSTER_H
#define BLIT_CELL_CLUSTER_H

#include <stddef.h>
#include <stdint.h>
#include <string.h>

/* One cell-storage contract for terminal grids, API cells and render views.
   This preserves the existing sixteen attachments, not a Unicode-width or
   segmentation change. Columns are supplied by the current cell owner. */
#define BLIT_MAX_COMBINING 16
#define BLIT_CLUSTER_MAX_SCALARS (BLIT_MAX_COMBINING + 1)

typedef struct {
    uint32_t scalars[BLIT_CLUSTER_MAX_SCALARS];
    size_t length;
    int columns;
} BlitCellCluster;

static int blit_cluster_scalar_valid(uint32_t scalar)
{
    return scalar <= 0x10ffffU && !(scalar >= 0xd800U && scalar <= 0xdfffU);
}

/* Copy once into a bounded value. Failure leaves the destination untouched;
   no normalization, presentation/width inference or input-buffer aliasing. */
static int blit_cell_cluster_init(BlitCellCluster *cluster, uint32_t base,
                                  const uint32_t *attachments, size_t count,
                                  int columns)
{
    BlitCellCluster next;
    size_t i;
    if (!cluster || count > BLIT_MAX_COMBINING || (count && !attachments) ||
        (columns != 1 && columns != 2) || !blit_cluster_scalar_valid(base))
        return 0;
    for (i = 0; i < count; ++i)
        if (!blit_cluster_scalar_valid(attachments[i])) return 0;
    memset(&next, 0, sizeof(next));
    next.scalars[0] = base;
    for (i = 0; i < count; ++i) next.scalars[i + 1] = attachments[i];
    next.length = count + 1;
    next.columns = columns;
    *cluster = next;
    return 1;
}

static int blit_cell_cluster_equal(const BlitCellCluster *a,
                                   const BlitCellCluster *b)
{
    return a && b && a->length > 0 && a->length <= BLIT_CLUSTER_MAX_SCALARS &&
        (a->columns == 1 || a->columns == 2) &&
        a->length == b->length && a->columns == b->columns &&
        memcmp(a->scalars, b->scalars, a->length * sizeof(uint32_t)) == 0;
}

/* Bucket selection only; callers MUST compare the complete key on a hit. */
static uint32_t blit_cell_cluster_hash(const BlitCellCluster *cluster)
{
    uint32_t hash = 2166136261U;
    size_t i;
    if (!cluster || !cluster->length || cluster->length > BLIT_CLUSTER_MAX_SCALARS ||
        (cluster->columns != 1 && cluster->columns != 2))
        return 0;
    hash = (hash ^ (uint32_t)cluster->columns) * 16777619U;
    for (i = 0; i < cluster->length; ++i)
        hash = (hash ^ cluster->scalars[i]) * 16777619U;
    return (hash ^ (uint32_t)cluster->length) * 16777619U;
}

#endif
