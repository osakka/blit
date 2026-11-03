#ifndef BLIT_WINDOW_GLYPHS_H
#define BLIT_WINDOW_GLYPHS_H

/* ADR 0210: the window-chrome glyph sets, Chalk and Slate, drawn as cell
   geometry. Three sets of 35 in one order -- Chalk at U+100231, Slate light
   at U+100254, Slate dark at U+100277 -- listed in the catalogue's
   cell_geometry section. Chalk is one colour: coverage, drawn in the cell's
   foreground. Slate is the same geometry in its own colours: premultiplied
   RGBA. A pixel no rule sets is transparent in both, so a frame cell shows
   its background (on a held desktop, the wallpaper).

   The geometry table below is the one source of every length. The design
   script tools/fonts/draw-window-glyphs.py reads it from this file, and
   window_glyph_gate holds this drawing to that script pixel for pixel.
   The glyphs are pixel-designed for the owner's 11x22 cell. At other sizes
   lines, stripes, the shadow and the rings keep their pixel widths. What
   scales with the cell is the box (the cell's width), the number of
   stripes (six at 22 px, in proportion to the height) and a button end's
   radius. */

#include <stdint.h>
#include <string.h>

#define BLIT_WINDOW_FIRST 0x100231U
#define BLIT_WINDOW_SET 35U
#define BLIT_WINDOW_LAST (BLIT_WINDOW_FIRST + 3U * BLIT_WINDOW_SET - 1U)

/* The geometry table, in pixels. */
#define BLIT_WINDOW_LINE 1              /* frame lines, stripes, outlines, SGR 51 */
#define BLIT_WINDOW_THICK_RING 3        /* the default button's ring, SGR 51:3 */
#define BLIT_WINDOW_DESIGN_HEIGHT 22    /* the cell height the set is drawn for */
#define BLIT_WINDOW_STRIPES 6           /* stripes at that height; 1 px, 1 px apart */
#define BLIT_WINDOW_STRIPE_INSET 2      /* clear beside a frame or a box */
#define BLIT_WINDOW_TITLE_GAP 5         /* clear before and after the title */
#define BLIT_WINDOW_SHADOW 2            /* the drop shadow's width */
#define BLIT_WINDOW_SHADOW_OFFSET 2     /* and how far it is moved down and right */
#define BLIT_WINDOW_DIALOG_MARGIN 2     /* dialog border, from the content out: */
#define BLIT_WINDOW_DIALOG_BAND 3       /*   face margin, the wide line, */
#define BLIT_WINDOW_DIALOG_GAP 1        /*   a gap of the face, */
#define BLIT_WINDOW_DIALOG_OUTER 1      /*   the outer line */
#define BLIT_WINDOW_BUTTON_INSET 2      /* clear before a button's rounded end */
#define BLIT_WINDOW_BUTTON_RADIUS_NUM 4 /* that end's radius: 4/11 of the cell */
#define BLIT_WINDOW_BUTTON_RADIUS_DEN 11 /*   height, within half of it and the cell */
#define BLIT_WINDOW_ZOOM_NUM 2          /* the zoom box's inner square: 2/3 of the */
#define BLIT_WINDOW_ZOOM_DEN 3          /*   box, from its top-left corner */

static int blit_window_glyph(uint32_t scalar)
{
    return scalar >= BLIT_WINDOW_FIRST && scalar <= BLIT_WINDOW_LAST;
}

/* Slate (light and dark) carries colours; Chalk takes the foreground. */
static int blit_window_colour(uint32_t scalar)
{
    return blit_window_glyph(scalar) && scalar >= BLIT_WINDOW_FIRST + BLIT_WINDOW_SET;
}

/* The order within a set, as the catalogue lists it. */
enum {
    WG_STRIPE, WG_STRIPE_FIRST, WG_STRIPE_LAST, WG_STRIPE_BEFORE_TITLE,
    WG_STRIPE_AFTER_TITLE, WG_TITLE, WG_STRIPE_TO_BOX, WG_STRIPE_FROM_BOX,
    WG_STRIPE_BETWEEN, WG_CLOSE, WG_SHADE, WG_ZOOM, WG_FRAME_LEFT,
    WG_FRAME_RIGHT_TOP, WG_FRAME_RIGHT, WG_FRAME_BOTTOM_LEFT,
    WG_FRAME_BOTTOM_FIRST, WG_FRAME_BOTTOM, WG_FRAME_BOTTOM_RIGHT,
    WG_FRAME_TOP_LEFT, WG_FRAME_TOP, WG_FRAME_TOP_RIGHT, WG_NOTE_CLOSE,
    WG_DIALOG_TOP_LEFT, WG_DIALOG_TOP, WG_DIALOG_TOP_RIGHT, WG_DIALOG_LEFT,
    WG_DIALOG_RIGHT, WG_DIALOG_BOTTOM_LEFT, WG_DIALOG_BOTTOM,
    WG_DIALOG_BOTTOM_RIGHT, WG_BUTTON_LEFT, WG_BUTTON_RIGHT,
    WG_DEFAULT_LEFT, WG_DEFAULT_RIGHT
};

/* Slate's colour roles: K frame and outlines, D dark grey, L highlight,
   B the title bar, W a box's face, S shadow, G a dialog's face, F a
   button's face, N a button's shade; and the controls' lines: O a
   button's ring and a dialog's outer line, R the dialog's wide line, H
   the highlight down its middle. In light, O, R and H are K, D and L;
   dark lifts them so they read on its dark faces. Chalk inks every role
   it uses. */
enum { WR_NONE, WR_K, WR_D, WR_L, WR_B, WR_W, WR_S, WR_G, WR_F, WR_N, WR_O, WR_R, WR_H,
       WR_ROLES };

static const unsigned char blit_window_palette[2][WR_ROLES][3] = {
    { {0, 0, 0}, {0, 0, 0}, {119, 119, 119}, {255, 255, 255}, {214, 214, 214},
      {255, 255, 255}, {0, 0, 0}, {238, 238, 238}, {221, 221, 221}, {153, 153, 153},
      {0, 0, 0}, {119, 119, 119}, {255, 255, 255} },
    { {0, 0, 0}, {8, 8, 10}, {30, 30, 34}, {112, 112, 122}, {60, 60, 67},
      {200, 200, 206}, {0, 0, 0}, {52, 52, 58}, {78, 78, 86}, {40, 40, 45},
      {168, 168, 176}, {122, 122, 130}, {190, 190, 198} },
};

typedef struct {
    unsigned char *roles;
    int w, h;
} WindowCell;

static void wg_fill(WindowCell *c, int x0, int x1, int y0, int y1, unsigned char role)
{
    int x, y;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > c->w) x1 = c->w;
    if (y1 > c->h) y1 = c->h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x) c->roles[y * c->w + x] = role;
}

static void wg_outline(WindowCell *c, int x0, int x1, int y0, int y1, int t)
{
    wg_fill(c, x0, x1, y0, y0 + t, WR_K); wg_fill(c, x0, x1, y1 - t, y1, WR_K);
    wg_fill(c, x0, x0 + t, y0, y1, WR_K); wg_fill(c, x1 - t, x1, y0, y1, WR_K);
}

static int wg_max3(int a, int b, int c)
{
    int m = a > b ? a : b;
    return m > c ? m : c;
}

/* Is (x, y)'s centre inside the rounded rectangle [x0, x1) x [y0, y1),
   corner radius r? In half pixels, so integer; the r term rounds the
   corners' steps as QuickDraw-era pixel art does. */
static int wg_inside_round(int x, int y, int x0, int x1, int y0, int y1, int r)
{
    long px = 2L * x + 1, py = 2L * y + 1, dx, dy;
    if (px < 2L * x0 || px > 2L * x1 || py < 2L * y0 || py > 2L * y1) return 0;
    dx = wg_max3((int)(2L * (x0 + r) - px), 0, (int)(px - 2L * (x1 - r)));
    dy = wg_max3((int)(2L * (y0 + r) - py), 0, (int)(py - 2L * (y1 - r)));
    return dx * dx + dy * dy <= 4L * r * r + 2L * r;
}

/* Which way the edge faces at (x, y): the centre's offset from the nearest
   point of the rectangle's inner (unrounded) part, in half pixels. */
static void wg_round_normal(int x, int y, int x0, int x1, int y0, int y1, int r,
                            int *vx, int *vy)
{
    int px = 2 * x + 1, py = 2 * y + 1;
    *vx = px < 2 * (x0 + r) ? px - 2 * (x0 + r) : px > 2 * (x1 - r) ? px - 2 * (x1 - r) : 0;
    *vy = py < 2 * (y0 + r) ? py - 2 * (y0 + r) : py > 2 * (y1 - r) ? py - 2 * (y1 - r) : 0;
}

/* One glyph's roles, w x h, row-major; WR_NONE is transparent. */
static void blit_window_roles(uint32_t scalar, unsigned char *roles, int w, int h)
{
    enum {
        L = BLIT_WINDOW_LINE, INSET = BLIT_WINDOW_STRIPE_INSET,
        SW = BLIT_WINDOW_SHADOW, SO = BLIT_WINDOW_SHADOW_OFFSET,
        /* The dialog border's layers, as distances from the content. */
        D1 = BLIT_WINDOW_DIALOG_MARGIN, D2 = D1 + BLIT_WINDOW_DIALOG_BAND,
        D3 = D2 + BLIT_WINDOW_DIALOG_GAP, D4 = D3 + BLIT_WINDOW_DIALOG_OUTER
    };
    unsigned index = (scalar - BLIT_WINDOW_FIRST) % BLIT_WINDOW_SET;
    int slate = blit_window_colour(scalar);
    /* The title bar's stripes: L thick and L apart, six at 22 px and as
       many in proportion at other heights, centred. */
    int n = (BLIT_WINDOW_STRIPES * h + BLIT_WINDOW_DESIGN_HEIGHT / 2) / BLIT_WINDOW_DESIGN_HEIGHT;
    int band, top, side, bx, by, x, y, k, sx0 = 0, sx1 = w, stripes = 0;
    WindowCell cell;
    cell.roles = roles; cell.w = w; cell.h = h;
    memset(roles, WR_NONE, (size_t)w * (size_t)h);
    if (n > (h - 4 * L) / (2 * L)) n = (h - 4 * L) / (2 * L);
    if (n < 1) n = 1;
    band = 2 * L * n;
    top = (h - band) / 2;
    /* A box: square, the cell's width, centred on the stripes. */
    side = band - L < w ? band - L : w;
    bx = (w - side) / 2;
    by = top + (band - L - side) / 2;
    switch (index) {
    case WG_STRIPE: stripes = 1; break;
    case WG_STRIPE_FIRST: case WG_STRIPE_FROM_BOX: stripes = 1; sx0 = INSET; break;
    case WG_STRIPE_LAST: case WG_STRIPE_TO_BOX: stripes = 1; sx1 = w - INSET; break;
    case WG_STRIPE_BEFORE_TITLE: stripes = 1; sx1 = w - BLIT_WINDOW_TITLE_GAP; break;
    case WG_STRIPE_AFTER_TITLE: stripes = 1; sx0 = BLIT_WINDOW_TITLE_GAP; break;
    case WG_STRIPE_BETWEEN: stripes = 1; sx0 = INSET; sx1 = w - INSET; break;
    default: break;
    }
    if (index <= WG_ZOOM) {
        if (slate) wg_fill(&cell, 0, w, 0, h, WR_B);
        for (k = 0; stripes && k < n; ++k) {
            y = top + 2 * L * k;
            wg_fill(&cell, sx0, sx1, y, y + L, slate ? WR_D : WR_K);
            if (slate) wg_fill(&cell, sx0, sx1, y + L, y + 2 * L, WR_L);
        }
        /* The frame line and the rule under the bar; Slate's bevel is a
           highlight under the line and a shade over the rule. */
        wg_fill(&cell, 0, w, 0, L, WR_K);
        wg_fill(&cell, 0, w, h - L, h, WR_K);
        if (slate) {
            wg_fill(&cell, 0, w, L, 2 * L, WR_L);
            wg_fill(&cell, 0, w, h - 2 * L, h - L, WR_D);
        }
    }
    if (index == WG_CLOSE || index == WG_SHADE || index == WG_ZOOM || index == WG_NOTE_CLOSE) {
        if (slate) wg_fill(&cell, bx, bx + side, by, by + side, WR_W);
        wg_outline(&cell, bx, bx + side, by, by + side, L);
        if (index == WG_ZOOM) {
            int inner = (BLIT_WINDOW_ZOOM_NUM * side + BLIT_WINDOW_ZOOM_DEN / 2) / BLIT_WINDOW_ZOOM_DEN;
            wg_outline(&cell, bx, bx + inner, by, by + inner, L);
        } else if (index == WG_SHADE) {
            y = by + (side - 3 * L) / 2;
            wg_fill(&cell, bx, bx + side, y, y + L, WR_K);
        }
        return;
    }
    /* The window frame: a line against the content, and a shadow beside
       the right and under the bottom, SO from the outer corners. */
    switch (index) {
    case WG_FRAME_LEFT: wg_fill(&cell, w - L, w, 0, h, WR_K); return;
    case WG_FRAME_RIGHT_TOP: case WG_FRAME_RIGHT:
        wg_fill(&cell, 0, L, 0, h, WR_K);
        wg_fill(&cell, L, L + SW, index == WG_FRAME_RIGHT_TOP ? SO : 0, h, WR_S);
        return;
    case WG_FRAME_BOTTOM_LEFT: wg_fill(&cell, w - L, w, 0, L, WR_K); return;
    case WG_FRAME_BOTTOM_FIRST: case WG_FRAME_BOTTOM:
        wg_fill(&cell, 0, w, 0, L, WR_K);
        /* The left frame's outer edge is L into the previous cell. */
        wg_fill(&cell, index == WG_FRAME_BOTTOM_FIRST ? SO - L : 0, w, L, L + SW, WR_S);
        return;
    case WG_FRAME_BOTTOM_RIGHT:
        wg_fill(&cell, 0, L + SW, 0, L + SW, WR_S);
        wg_fill(&cell, 0, L, 0, L, WR_K);
        return;
    case WG_FRAME_TOP_LEFT: wg_fill(&cell, w - L, w, h - L, h, WR_K); return;
    case WG_FRAME_TOP: wg_fill(&cell, 0, w, h - L, h, WR_K); return;
    case WG_FRAME_TOP_RIGHT: wg_fill(&cell, 0, L, h - L, h, WR_K); return;
    default: break;
    }
    if (index >= WG_DIALOG_TOP_LEFT && index <= WG_DIALOG_BOTTOM_RIGHT) {
        /* The double border, outward from the content: a margin of the
           face, the wide line (Slate's grey with a highlight down its
           middle), a gap of the face, the outer line; then the shadow, SO
           from the outer line's far corners. A pixel's layer is its
           distance from the content, so the corners are square. */
        int left = index == WG_DIALOG_TOP_LEFT || index == WG_DIALOG_LEFT ||
            index == WG_DIALOG_BOTTOM_LEFT;
        int right = index == WG_DIALOG_TOP_RIGHT || index == WG_DIALOG_RIGHT ||
            index == WG_DIALOG_BOTTOM_RIGHT;
        int upper = index <= WG_DIALOG_TOP_RIGHT;
        int lower = index >= WG_DIALOG_BOTTOM_LEFT;
        int mid = D1 + (D2 - D1) / 2;
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
            int dx = left ? w - 1 - x : right ? x : 0;
            int dy = upper ? h - 1 - y : lower ? y : 0;
            int d = dx > dy ? dx : dy, shadow = 0;
            unsigned char role = WR_NONE;
            if (d < D1 || (d >= D2 && d < D3)) role = slate ? WR_G : WR_NONE;
            else if (d < D2) role = slate ? (d == mid ? WR_H : WR_R) : WR_K;
            else if (d < D4) role = slate ? WR_O : WR_K;
            else {
                if (right && x < D4 + SW)
                    shadow = upper ? y >= h - D4 + SO : !lower || y < D4 + SW;
                else if (lower && y < D4 + SW)
                    shadow = left ? x >= w - D4 + SO : !right || x < D4 + SW;
                if (shadow) role = WR_S;
            }
            if (role != WR_NONE) roles[y * w + x] = role;
        }
        return;
    }
    if (index >= WG_BUTTON_LEFT) {
        /* A rounded end, its straight top and bottom running to the edge
           that the label's SGR 51 lines continue: L thick, or the default
           button's BLIT_WINDOW_THICK_RING, the width SGR 51:3 draws. */
        int mirror = index == WG_BUTTON_RIGHT || index == WG_DEFAULT_RIGHT;
        int t = index >= WG_DEFAULT_LEFT ? BLIT_WINDOW_THICK_RING : L;
        int x0 = BLIT_WINDOW_BUTTON_INSET, r = w - x0, x1, r1, r2;
        if (h / 2 < r) r = h / 2;
        if (BLIT_WINDOW_BUTTON_RADIUS_NUM * h / BLIT_WINDOW_BUTTON_RADIUS_DEN < r)
            r = BLIT_WINDOW_BUTTON_RADIUS_NUM * h / BLIT_WINDOW_BUTTON_RADIUS_DEN;
        if (r < 0) r = 0;
        /* The inner edges' radii, square once a small cell's ring is as
           thick as its end is round. */
        r1 = r - t > 0 ? r - t : 0;
        r2 = r1 - L > 0 ? r1 - L : 0;
        x1 = w + 2 * r;
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
            int mx = mirror ? w - 1 - x : x;
            unsigned char role;
            if (!wg_inside_round(mx, y, x0, x1, 0, h, r)) continue;
            if (!wg_inside_round(mx, y, x0 + t, x1 - t, t, h - t, r1)) role = slate ? WR_O : WR_K;
            else if (!slate) continue;
            else {
                /* The face, and a bevel round the end only (the label's
                   cells between the ends are flat): light where the edge
                   faces up and left, shade where it faces down and right. */
                role = WR_F;
                if (!wg_inside_round(mx, y, x0 + t + L, x1 - t - L, t + L, h - t - L, r2)) {
                    int vx, vy;
                    wg_round_normal(mx, y, x0 + t, x1 - t, t, h - t, r1, &vx, &vy);
                    if (vx) {
                        if (mirror) vx = -vx;
                        role = vx + vy < 0 ? WR_L : WR_N;
                    }
                }
            }
            roles[y * w + x] = role;
        }
    }
}

/* ADR 0210: what SGR 51 draws in a framed cell, as up to four horizontal
   bands {top, bottom, colour}. kind is 1 (51, thin), 3 (51:3, the default
   button's ring), 2 or 4 (51:2 and 51:4, the Slate light and dark title
   bar: frame line and highlight at the top, shade and rule at the bottom,
   the rows every bar glyph has, in the same palette). line is the thin
   and thick lines' colour, 0xRRGGBB. Both compositors draw from this, so
   a framed title meets the bar glyphs beside it row for row. */
typedef struct { int top, bottom; uint32_t colour; } BlitWindowBand;

static uint32_t wg_rgb(int dark, int role)
{
    return (uint32_t)blit_window_palette[dark][role][0] << 16 |
        (uint32_t)blit_window_palette[dark][role][1] << 8 |
        (uint32_t)blit_window_palette[dark][role][2];
}

static int blit_window_framed_bands(int kind, int h, uint32_t line, BlitWindowBand band[4])
{
    int L = BLIT_WINDOW_LINE, t;
    if (kind == 2 || kind == 4) {
        int dark = kind == 4;
        if (h < 4 * L) return 0;
        band[0].top = 0; band[0].bottom = L; band[0].colour = wg_rgb(dark, WR_K);
        band[1].top = L; band[1].bottom = 2 * L; band[1].colour = wg_rgb(dark, WR_L);
        band[2].top = h - 2 * L; band[2].bottom = h - L; band[2].colour = wg_rgb(dark, WR_D);
        band[3].top = h - L; band[3].bottom = h; band[3].colour = wg_rgb(dark, WR_K);
        return 4;
    }
    t = kind == 3 ? BLIT_WINDOW_THICK_RING : L;
    if (t > h / 2) t = h / 2;
    band[0].top = 0; band[0].bottom = t; band[0].colour = line;
    band[1].top = h - t; band[1].bottom = h; band[1].colour = line;
    return 2;
}

/* Draws the glyph into a raster entry's pixels: coverage for Chalk,
   premultiplied RGBA for Slate, in the raster's byte order.
   Returns 0 when the cell is too large for the scratch map. */
static int blit_window_draw(uint32_t scalar, unsigned char *pixels, int stride,
                            int w, int h, int rgba)
{
    unsigned char roles[128 * 128];
    int x, y, dark = scalar >= BLIT_WINDOW_FIRST + 2U * BLIT_WINDOW_SET;
    if (w <= 0 || h <= 0 || w > 128 || h > 128) return 0;
    blit_window_roles(scalar, roles, w, h);
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        unsigned char role = roles[y * w + x];
        unsigned char *out = pixels + y * stride + x * (rgba ? 4 : 1);
        if (role == WR_NONE) continue;
        if (!rgba) { out[0] = 255; continue; }
        out[0] = blit_window_palette[dark][role][0];
        out[1] = blit_window_palette[dark][role][1];
        out[2] = blit_window_palette[dark][role][2];
        out[3] = 255;
    }
    return 1;
}

#endif
