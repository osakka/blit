#ifndef BLIT_ICON_CODEPOINTS_H
#define BLIT_ICON_CODEPOINTS_H

/* ADR 0243: where Blit's icons and cell geometry live. They are private use
   in plane 16 from U+101000, in the icon catalogue's layout
   (BLIT_FONT_ICON_CATALOGUE). They were at U+100000-U+100495 until
   0.3.131: the Glyph Protocol reserved U+100000-U+1003FF for programs'
   registered glyphs (2026-11-15), so the whole set moved up by 0x1000, the
   same layout, every glyph at old + 0x1000.

   The transition: from 0.3.131 the old range draws the same glyphs. This is
   the one place it is mapped (blit_icon_scalar), and it is asked at the
   four places a cell's scalar decides what is drawn: the raster's glyph
   lookup (raster_get_key: one cache entry for old and new) and its pen
   planes (blit_raster_get_window_plane), and the renderers' cell tests
   (blit_chrome_colour, the GLES icon pen via blit_icon_codepoint). The GPU
   keys its sprites by the cell's scalar, so an old cell has a sprite of
   its own, made from the same raster glyph. The cell keeps what the
   program wrote (the cells API, copy and scrollback read it back
   unchanged). A later release, named in its notes once Hibr's map has
   moved (Hibr is to gate its own on Blit 0.3.131), drops the old range by
   defining BLIT_ICON_OLD_RANGE_DROPPED -- or deleting the block below --
   and icon_transition_gate's old-range check goes with it. */

#include <stdint.h>

#define BLIT_ICON_FIRST 0x101000U   /* the first codepoint of Blit's set */
#define BLIT_ICON_LAST 0x10FFFDU    /* plane 16's last private-use codepoint */

#ifndef BLIT_ICON_OLD_RANGE_DROPPED
#define BLIT_ICON_OLD_FIRST 0x100000U
#define BLIT_ICON_OLD_LAST 0x100495U
#endif

/* The codepoint a cell's scalar draws as: an old-range one is its new
   codepoint; anything else is itself. */
static inline uint32_t blit_icon_scalar(uint32_t scalar)
{
#ifdef BLIT_ICON_OLD_FIRST
    if (scalar >= BLIT_ICON_OLD_FIRST && scalar <= BLIT_ICON_OLD_LAST)
        return scalar + (BLIT_ICON_FIRST - BLIT_ICON_OLD_FIRST);
#endif
    return scalar;
}

/* Whether a scalar is in Blit's plane-16 range (old or new): only these are
   looked up in Blit's icon font first and carry an icon pen. */
static inline int blit_icon_codepoint(uint32_t scalar)
{
    scalar = blit_icon_scalar(scalar);
    return scalar >= BLIT_ICON_FIRST && scalar <= BLIT_ICON_LAST;
}

#endif
