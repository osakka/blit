#ifndef BLIT_EMOJI_SEQUENCES_H
#define BLIT_EMOJI_SEQUENCES_H
#include "blit_cell_cluster.h"
typedef struct {
    uint32_t scalar;
    uint16_t first, count;
    unsigned char terminal;
} BlitEmojiNode;
#include "blit_emoji_sequences_data.h"

static unsigned blit_emoji_child(unsigned node, uint32_t scalar)
{
    unsigned low = blit_emoji_nodes[node].first;
    unsigned high = low + blit_emoji_nodes[node].count;
    while (low < high) {
        unsigned mid = low + (high - low) / 2;
        uint32_t cp = blit_emoji_nodes[mid].scalar;
        if (cp < scalar) low = mid + 1;
        else if (cp > scalar) high = mid;
        else return mid;
    }
    return 0;
}

/* Return a prefix node, zero if the ordered sequence cannot extend. No
   width inference, allocation, normalization or mutation in this helper. */
static unsigned blit_emoji_extend(uint32_t base, const uint32_t *marks,
                                   size_t count, uint32_t next)
{
    unsigned node;
    size_t i;
    if (count >= BLIT_MAX_COMBINING || (count && !marks)) return 0;
    node = blit_emoji_child(0, base);
    for (i = 0; node && i < count; ++i)
        node = blit_emoji_child(node, marks[i]);
    return node ? blit_emoji_child(node, next) : 0;
}
#endif
