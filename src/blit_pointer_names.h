#ifndef BLIT_POINTER_NAMES_H
#define BLIT_POINTER_NAMES_H

/* The pointer's names (OSC 22): the one table of every name Blit draws a
   pointer for, by the index of its art in blit_pointer_shapes
   (blit_pointer_art.h). Its own header so that blit-compositor, which says
   a program's pointer to the desktop by name (`WINDOW cursor`), asks the
   same table without the art. */
#include <stddef.h>
#include <string.h>

/* OSC 22 results that are not art: the display's own pointer, and none. */
#define BLIT_POINTER_SHAPE_DEFAULT (-2)
#define BLIT_POINTER_SHAPE_HIDDEN (-3)

/* kitty's OSC 22 names and the xterm/X11 names it accepts as aliases, mapped
   to the art drawn for them. A name Blit has no art for is absent: a query
   answers 0 for it and setting it shows the display's own pointer. */
static int blit_pointer_name_shape(const char *name)
{
    static const struct { const char *name; int shape; } names[] = {
        {"default", BLIT_POINTER_SHAPE_DEFAULT},
        {"left_ptr", BLIT_POINTER_SHAPE_DEFAULT},
        {"top_left_arrow", BLIT_POINTER_SHAPE_DEFAULT},
        {"arrow", BLIT_POINTER_SHAPE_DEFAULT},
        {"none", BLIT_POINTER_SHAPE_HIDDEN},
        {"pointer", 1}, {"grab", 1}, {"grabbing", 1}, {"hand", 1},
        {"hand1", 1}, {"hand2", 1}, {"pointing_hand", 1},
        {"openhand", 1}, {"closedhand", 1},
        {"crosshair", 2}, {"cell", 2}, {"cross", 2}, {"tcross", 2},
        {"plus", 2}, {"precision", 2},
        {"text", 3}, {"xterm", 3}, {"ibeam", 3},
        {"wait", 4}, {"progress", 4}, {"watch", 4}, {"clock", 4},
        {"left_ptr_watch", 4},
        {"move", 5}, {"all-scroll", 5}, {"fleur", 5}, {"size_all", 5},
        {"e-resize", 6}, {"w-resize", 6}, {"ew-resize", 6},
        {"col-resize", 6}, {"sb_h_double_arrow", 6},
        {"h_double_arrow", 6}, {"left_side", 6}, {"right_side", 6},
        {"size_hor", 6}, {"split_h", 6},
        {"n-resize", 7}, {"s-resize", 7}, {"ns-resize", 7},
        {"row-resize", 7}, {"sb_v_double_arrow", 7},
        {"v_double_arrow", 7}, {"top_side", 7}, {"bottom_side", 7},
        {"size_ver", 7}, {"split_v", 7},
        {"nw-resize", 8}, {"se-resize", 8}, {"nwse-resize", 8},
        {"top_left_corner", 8}, {"bottom_right_corner", 8},
        {"size_fdiag", 8},
        {"ne-resize", 9}, {"sw-resize", 9}, {"nesw-resize", 9},
        {"top_right_corner", 9}, {"bottom_left_corner", 9},
        {"size_bdiag", 9},
        {"not-allowed", 10}, {"no-drop", 10}, {"forbidden", 10},
        {"crossed_circle", 10}
    };
    size_t i;
    for (i = 0; i < sizeof(names) / sizeof(names[0]); ++i)
        if (!strcmp(name, names[i].name)) return names[i].shape;
    return -1;
}

#endif
