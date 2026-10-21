#ifndef BLIT_INK_H
#define BLIT_INK_H
#include "blit_font.h"

typedef struct {
    int x0, y0, x1, y1;          /* cells to restore, half-open */
    int source_x0, source_y0, source_x1, source_y1;
} BlitInkRegion;

static int blit_font_has_overhang(const BlitFont *font)
{
    return font && (font->raster_ink_left || font->raster_ink_right ||
                    font->raster_ink_top || font->raster_ink_bottom);
}

static int blit_ink_lower(int value, int padding)
{ return value > padding ? value - padding : 0; }
static int blit_ink_upper(int value, int padding, int limit)
{ return limit - value > padding ? value + padding : limit; }

/* Inputs are already clamped, half-open terminal cell bounds. A separate
   source halo prevents erasing neighbouring ink at the write clip edge.
   An extra horizontal cell covers italic/bold style geometry. */
static BlitInkRegion blit_ink_region(const BlitFont *font,
                                    int x0, int y0, int x1, int y1,
                                    int columns, int rows)
{
    BlitInkRegion r;
    r.x0 = x0; r.y0 = y0; r.x1 = x1; r.y1 = y1;
    if (blit_font_has_overhang(font)) {
        r.x0 = blit_ink_lower(x0, font->raster_ink_left + 1);
        r.x1 = blit_ink_upper(x1, font->raster_ink_right + 1, columns);
        r.y0 = blit_ink_lower(y0, font->raster_ink_top);
        r.y1 = blit_ink_upper(y1, font->raster_ink_bottom, rows);
    }
    r.source_x0 = r.x0; r.source_y0 = r.y0;
    r.source_x1 = r.x1; r.source_y1 = r.y1;
    if (blit_font_has_overhang(font)) {
        r.source_x0 = blit_ink_lower(r.x0, font->raster_ink_right + 1);
        r.source_x1 = blit_ink_upper(r.x1, font->raster_ink_left + 1, columns);
        r.source_y0 = blit_ink_lower(r.y0, font->raster_ink_bottom);
        r.source_y1 = blit_ink_upper(r.y1, font->raster_ink_top, rows);
    }
    return r;
}
#endif
