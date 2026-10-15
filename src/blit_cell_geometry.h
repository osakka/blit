#ifndef BLIT_CELL_GEOMETRY_H
#define BLIT_CELL_GEOMETRY_H

/* One cell-grid calculation for every renderer, pane, input, and API path.
 *
 * A display is a pixel rectangle, but a terminal surface is an integer grid.
 * The pixels that cannot contain a complete cell are deliberately outside
 * that grid.  They are split evenly around it, so a scale such as 1.6 does
 * not leave a one-sided strip or make the first cell appear displaced.
 */
typedef struct {
    int cell_width;
    int cell_height;
    int columns;
    int rows;
    int pixel_width;
    int pixel_height;
    int origin_x;
    int origin_y;
    int remainder_x;
    int remainder_y;
} BlitCellGeometry;

static void blit_cell_geometry_compute(int display_width, int display_height,
                                       int cell_width, int cell_height,
                                       BlitCellGeometry *geometry);

/* A logical pane is a physical region first and a terminal grid second.
 * Computing the grid from the whole display and then splitting its rows is
 * incorrect when a physical region contains a fractional number of cells:
 * the resulting terminal row straddles the physical seam.  Keep this
 * partition rule beside the display rule so rendering, input, PTY sizing,
 * and the topology API cannot grow separate calculations. */
static void blit_cell_geometry_partition_compute(
    int display_width, int display_height, int cell_width, int cell_height,
    int parts_x, int parts_y, int part_x, int part_y,
    int *region_x, int *region_y, int *region_width, int *region_height,
    BlitCellGeometry *geometry)
{
    int left;
    int top;
    int right;
    int bottom;

    if (parts_x < 1) parts_x = 1;
    if (parts_y < 1) parts_y = 1;
    if (part_x < 0) part_x = 0;
    if (part_y < 0) part_y = 0;
    if (part_x >= parts_x) part_x = parts_x - 1;
    if (part_y >= parts_y) part_y = parts_y - 1;
    left = (display_width * part_x) / parts_x;
    top = (display_height * part_y) / parts_y;
    right = (display_width * (part_x + 1)) / parts_x;
    bottom = (display_height * (part_y + 1)) / parts_y;
    if (region_x != NULL) *region_x = left;
    if (region_y != NULL) *region_y = top;
    if (region_width != NULL) *region_width = right - left;
    if (region_height != NULL) *region_height = bottom - top;
    blit_cell_geometry_compute(right - left, bottom - top,
                               cell_width, cell_height, geometry);
}

static void blit_cell_geometry_compute(int display_width, int display_height,
                                       int cell_width, int cell_height,
                                       BlitCellGeometry *geometry)
{
    if (cell_width < 1)
        cell_width = 1;
    if (cell_height < 1)
        cell_height = 1;
    if (display_width < 1)
        display_width = cell_width * 80;
    if (display_height < 1)
        display_height = cell_height * 24;

    geometry->cell_width = cell_width;
    geometry->cell_height = cell_height;
    geometry->columns = display_width / cell_width;
    geometry->rows = display_height / cell_height;
    if (geometry->columns < 1)
        geometry->columns = 1;
    if (geometry->rows < 1)
        geometry->rows = 1;
    geometry->pixel_width = geometry->columns * cell_width;
    geometry->pixel_height = geometry->rows * cell_height;
    geometry->remainder_x = display_width - geometry->pixel_width;
    geometry->remainder_y = display_height - geometry->pixel_height;
    geometry->origin_x = geometry->remainder_x / 2;
    geometry->origin_y = geometry->remainder_y / 2;
}

#endif
