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
