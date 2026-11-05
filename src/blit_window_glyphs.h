#ifndef BLIT_WINDOW_GLYPHS_H
#define BLIT_WINDOW_GLYPHS_H

/* ADR 0210: the window-chrome glyph sets, Chalk and Slate, drawn as cell
   geometry. Two sets of 35 in one order -- Chalk at U+100231, Slate at
   U+100254 -- listed in the catalogue's cell_geometry section; Slate dark,
   once at U+100277, is retired (ADR 0211). Chalk is one colour: coverage,
   drawn in the cell's foreground. Slate is the same geometry in the cell's
   own pens (ADR 0211): each pixel is a role, resolved when the cell is
   drawn. A pixel no rule sets is transparent in both, so a frame cell shows
   its background (on a held desktop, the wallpaper).

   ADR 0214 adds the outer-edge frame (nine glyphs from U+1002AE, after the
   controls): ONE frame set whose line sits on the outermost edge of its
   cell, and the title bar's two end cells (Chalk's and Slate's), which
   carry that outer line and run the stripes in. They are below, after the
   dialog and button code; the frame glyphs of the sets above are
   deprecated, drawn unchanged until Hibr moves.

   The geometry table below is the one source of every length. The design
   script tools/fonts/draw-window-glyphs.py reads it from this file, and
   window_glyph_gate holds this drawing to that script pixel for pixel.
   The glyphs are pixel-designed for the owner's 11x22 cell. At other sizes
   lines, stripes and the rings keep their pixel widths. What
   scales with the cell is the box (the cell's width), the number of
   stripes (six at 22 px, in proportion to the height) and a button end's
   radius. */

#include <stdint.h>
#include <string.h>

#define BLIT_WINDOW_FIRST 0x100231U
#define BLIT_WINDOW_SET 35U
/* Chalk, then Slate. The third set, Slate dark (U+100277-100299), is
   retired (ADR 0211): Slate takes its colours from the cell, so one set
   draws light and dark alike. */
#define BLIT_WINDOW_LAST (BLIT_WINDOW_FIRST + 2U * BLIT_WINDOW_SET - 1U)

/* The geometry table, in pixels. */
#define BLIT_WINDOW_LINE 1              /* frame lines, stripes, outlines, SGR 51 */
#define BLIT_WINDOW_THICK_RING 3        /* the default button's ring, SGR 51:3 */
#define BLIT_WINDOW_DESIGN_HEIGHT 22    /* the cell height the set is drawn for */
#define BLIT_WINDOW_STRIPES 6           /* stripes at that height; 1 px, 1 px apart */
#define BLIT_WINDOW_STRIPE_INSET 2      /* clear beside a frame or a box */
#define BLIT_WINDOW_TITLE_GAP 5         /* clear before and after the title */
#define BLIT_WINDOW_SHADOW 2            /* SGR 66's thick shadow (ADR 0211) */
#define BLIT_WINDOW_SHADOW_OFFSET 2     /* where its first row and cell start */
#define BLIT_WINDOW_DIALOG_MARGIN 2     /* dialog border, from the content out: */
#define BLIT_WINDOW_DIALOG_BAND 3       /*   face margin, the wide line, */
#define BLIT_WINDOW_DIALOG_GAP 1        /*   a gap of the face, */
#define BLIT_WINDOW_DIALOG_OUTER 1      /*   the outer line */
#define BLIT_WINDOW_BUTTON_INSET 2      /* clear before a button's rounded end */
#define BLIT_WINDOW_BUTTON_RADIUS_NUM 4 /* that end's radius: 4/11 of the cell */
#define BLIT_WINDOW_BUTTON_RADIUS_DEN 11 /*   height, within half of it and the cell */
#define BLIT_FRAME_FIRST 0x1002AEU      /* the outer-edge frame (ADR 0214): */
#define BLIT_FRAME_COUNT 9U             /*   five frame cells, four title-end cells */
#define BLIT_WINDOW_ZOOM_NUM 2          /* the zoom box's inner square: 2/3 of the */
#define BLIT_WINDOW_ZOOM_DEN 3          /*   box, from its top-left corner */

static int blit_window_glyph(uint32_t scalar)
{
    return scalar >= BLIT_WINDOW_FIRST && scalar <= BLIT_WINDOW_LAST;
}

/* Slate draws in three of the cell's pens (ADR 0211); Chalk in one, the
   foreground. */
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

/* The roles a pixel can have (ADR 0211). Chalk inks every role it uses in
   the foreground. Slate resolves each in the cell it is drawn in:
   - BG, the cell's background: the bar. Drawn as nothing, since the
     cell's background is under it.
   - FG, the foreground: frame lines, outlines, box ink, a dialog's
     lines, a button's ring.
   - LINE, the underline colour (SGR 58), white if unset: the bar's
     highlights and a box's face, and the dialog's face (its margin and
     gap) and a button's face, which the application sets as SGR 58 on
     those cells.
   - MIX, half the foreground and half the background: the dark stripe,
     the bar's shade, a button's shade.
   A dialog's or button's own cell background shows where nothing is
   drawn: round a dialog's border the wallpaper, and in a cap's corners
   outside its ring the dialog's face.
   blit_window_pens resolves them; the compositors draw FG, LINE and MIX
   as three coverage planes. */
enum { WR_NONE, WR_FG, WR_LINE, WR_MIX, WR_BG };

/* The three pens a Slate cell draws with, 0xRRGGBB. */
typedef struct { uint32_t fg, line, mix; } BlitWindowPens;

static BlitWindowPens blit_window_pens(uint32_t fg, uint32_t bg, int line_set, uint32_t line)
{
    BlitWindowPens pens;
    int shift;
    pens.fg = fg & 0xffffffU;
    pens.line = line_set ? line & 0xffffffU : 0xffffffU;
    pens.mix = 0;
    /* Each channel halved, rounded down: (fg + bg) / 2. */
    for (shift = 0; shift <= 16; shift += 8)
        pens.mix |= ((((fg >> shift) & 255U) + ((bg >> shift) & 255U)) / 2U) << shift;
    return pens;
}

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
    wg_fill(c, x0, x1, y0, y0 + t, WR_FG); wg_fill(c, x0, x1, y1 - t, y1, WR_FG);
    wg_fill(c, x0, x0 + t, y0, y1, WR_FG); wg_fill(c, x1 - t, x1, y0, y1, WR_FG);
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

/* The title bar's stripes and the box among them, for a w x h cell: n
   stripes (L thick and L apart, six at 22 px and as many in proportion at
   other heights, at least one), centred in a band of 2n lines; and the box,
   square, the cell's width or the band less a line, centred on the stripes.
   The window boxes and the checkbox and switch (ADR 0212) all stand here,
   so they line up at every cell size. */
static void blit_window_band(int w, int h, int *n_out, int *top_out,
                             int *side_out, int *bx_out, int *by_out)
{
    int L = BLIT_WINDOW_LINE;
    int n = (BLIT_WINDOW_STRIPES * h + BLIT_WINDOW_DESIGN_HEIGHT / 2) / BLIT_WINDOW_DESIGN_HEIGHT;
    int band, top, side;
    if (n > (h - 4 * L) / (2 * L)) n = (h - 4 * L) / (2 * L);
    if (n < 1) n = 1;
    band = 2 * L * n;
    top = (h - band) / 2;
    side = band - L < w ? band - L : w;
    *n_out = n; *top_out = top; *side_out = side;
    *bx_out = (w - side) / 2;
    *by_out = top + (band - L - side) / 2;
}

/* The title bar's row: the cell's own background (Slate: the bar), the
   stripes from sx0 to sx1 if there are any, the frame line and the rule
   under the bar; Slate's bevel is a highlight under the line and a shade
   over the rule. n and top are blit_window_band's. */
static void wg_bar(WindowCell *c, int slate, int stripes, int sx0, int sx1, int n, int top)
{
    enum { L = BLIT_WINDOW_LINE };
    int k, y;
    if (slate) wg_fill(c, 0, c->w, 0, c->h, WR_BG);
    for (k = 0; stripes && k < n; ++k) {
        y = top + 2 * L * k;
        wg_fill(c, sx0, sx1, y, y + L, slate ? WR_MIX : WR_FG);
        if (slate) wg_fill(c, sx0, sx1, y + L, y + 2 * L, WR_LINE);
    }
    wg_fill(c, 0, c->w, 0, L, WR_FG);
    wg_fill(c, 0, c->w, c->h - L, c->h, WR_FG);
    if (slate) {
        wg_fill(c, 0, c->w, L, 2 * L, WR_LINE);
        wg_fill(c, 0, c->w, c->h - 2 * L, c->h - L, WR_MIX);
    }
}

/* One glyph's roles, w x h, row-major; WR_NONE is transparent. */
static void blit_window_roles(uint32_t scalar, unsigned char *roles, int w, int h)
{
    enum {
        L = BLIT_WINDOW_LINE, INSET = BLIT_WINDOW_STRIPE_INSET,
        /* The dialog border's layers, as distances from the content. */
        D1 = BLIT_WINDOW_DIALOG_MARGIN, D2 = D1 + BLIT_WINDOW_DIALOG_BAND,
        D3 = D2 + BLIT_WINDOW_DIALOG_GAP, D4 = D3 + BLIT_WINDOW_DIALOG_OUTER
    };
    unsigned index = (scalar - BLIT_WINDOW_FIRST) % BLIT_WINDOW_SET;
    int slate = blit_window_colour(scalar);
    int n, top, side, bx, by, x, y, sx0 = 0, sx1 = w, stripes = 0;
    WindowCell cell;
    cell.roles = roles; cell.w = w; cell.h = h;
    memset(roles, WR_NONE, (size_t)w * (size_t)h);
    blit_window_band(w, h, &n, &top, &side, &bx, &by);
    switch (index) {
    case WG_STRIPE: stripes = 1; break;
    case WG_STRIPE_FIRST: case WG_STRIPE_FROM_BOX: stripes = 1; sx0 = INSET; break;
    case WG_STRIPE_LAST: case WG_STRIPE_TO_BOX: stripes = 1; sx1 = w - INSET; break;
    case WG_STRIPE_BEFORE_TITLE: stripes = 1; sx1 = w - BLIT_WINDOW_TITLE_GAP; break;
    case WG_STRIPE_AFTER_TITLE: stripes = 1; sx0 = BLIT_WINDOW_TITLE_GAP; break;
    case WG_STRIPE_BETWEEN: stripes = 1; sx0 = INSET; sx1 = w - INSET; break;
    default: break;
    }
    if (index <= WG_ZOOM) wg_bar(&cell, slate, stripes, sx0, sx1, n, top);
    if (index == WG_CLOSE || index == WG_SHADE || index == WG_ZOOM || index == WG_NOTE_CLOSE) {
        if (slate) wg_fill(&cell, bx, bx + side, by, by + side, WR_LINE);
        wg_outline(&cell, bx, bx + side, by, by + side, L);
        if (index == WG_ZOOM) {
            int inner = (BLIT_WINDOW_ZOOM_NUM * side + BLIT_WINDOW_ZOOM_DEN / 2) / BLIT_WINDOW_ZOOM_DEN;
            wg_outline(&cell, bx, bx + inner, by, by + inner, L);
        } else if (index == WG_SHADE) {
            y = by + (side - 3 * L) / 2;
            wg_fill(&cell, bx, bx + side, y, y + L, WR_FG);
        }
        return;
    }
    /* The window frame: a line against the content and nothing else. The
       drop shadow is Hibr's Shadow setting, not part of the frame (ADR
       0210, revised 2026-11-04), so the -top and -first pieces now draw the
       same as their plain neighbours; they keep their codepoints. */
    switch (index) {
    case WG_FRAME_LEFT: wg_fill(&cell, w - L, w, 0, h, WR_FG); return;
    case WG_FRAME_RIGHT_TOP: case WG_FRAME_RIGHT: wg_fill(&cell, 0, L, 0, h, WR_FG); return;
    case WG_FRAME_BOTTOM_LEFT: wg_fill(&cell, w - L, w, 0, L, WR_FG); return;
    case WG_FRAME_BOTTOM_FIRST: case WG_FRAME_BOTTOM: wg_fill(&cell, 0, w, 0, L, WR_FG); return;
    case WG_FRAME_BOTTOM_RIGHT: wg_fill(&cell, 0, L, 0, L, WR_FG); return;
    case WG_FRAME_TOP_LEFT: wg_fill(&cell, w - L, w, h - L, h, WR_FG); return;
    case WG_FRAME_TOP: wg_fill(&cell, 0, w, h - L, h, WR_FG); return;
    case WG_FRAME_TOP_RIGHT: wg_fill(&cell, 0, L, h - L, h, WR_FG); return;
    default: break;
    }
    if (index >= WG_DIALOG_TOP_LEFT && index <= WG_DIALOG_BOTTOM_RIGHT) {
        /* The double border, outward from the content: a margin of the
           face, the wide line (Slate's grey with a highlight down its
           middle), a gap of the face, the outer line; nothing beyond it.
           A pixel's layer is its distance from the content, so the
           corners are square. */
        int left = index == WG_DIALOG_TOP_LEFT || index == WG_DIALOG_LEFT ||
            index == WG_DIALOG_BOTTOM_LEFT;
        int right = index == WG_DIALOG_TOP_RIGHT || index == WG_DIALOG_RIGHT ||
            index == WG_DIALOG_BOTTOM_RIGHT;
        int upper = index <= WG_DIALOG_TOP_RIGHT;
        int lower = index >= WG_DIALOG_BOTTOM_LEFT;
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
            int dx = left ? w - 1 - x : right ? x : 0;
            int dy = upper ? h - 1 - y : lower ? y : 0;
            int d = dx > dy ? dx : dy;
            unsigned char role = WR_NONE;
            if (d < D1 || (d >= D2 && d < D3)) role = slate ? WR_LINE : WR_NONE;
            else if (d < D4) role = WR_FG;
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
            if (!wg_inside_round(mx, y, x0 + t, x1 - t, t, h - t, r1)) role = WR_FG;
            else if (!slate) continue;
            else {
                /* The face (LINE, the application's SGR 58), and a shade
                   round the end only where its edge faces down or right;
                   the label's cells between the ends are flat. */
                role = WR_LINE;
                if (!wg_inside_round(mx, y, x0 + t + L, x1 - t - L, t + L, h - t - L, r2)) {
                    int vx, vy;
                    wg_round_normal(mx, y, x0 + t, x1 - t, t, h - t, r1, &vx, &vy);
                    if (vx) {
                        if (mirror) vx = -vx;
                        if (vx + vy >= 0) role = WR_MIX;
                    }
                }
            }
            roles[y * w + x] = role;
        }
    }
}

/* ADR 0214: the outer-edge frame, nine glyphs from BLIT_FRAME_FIRST. A
   window has one frame. Its line sits on the outermost edge of each frame
   cell -- the left cell's left edge, the right cell's right edge, the
   bottom row's bottom edge -- and nothing else in the cell is drawn: the
   cell's own background is the padding (the window's face, which the
   application sets on the frame cells). Frame cells: left, right,
   bottom-left (left and bottom lines), bottom, bottom-right; one colour,
   coverage in the cell's foreground, the same for Chalk and Slate, whose
   frames were always a 1 px foreground line. The title bar's two end cells
   are the bar row's frame cells, and belong to the title-bar styles:
   Chalk's (coverage) and Slate's (pens, the bar glyphs' roles). Each is
   the bar -- its top line and rule, Slate's bevel rows -- with the outer
   line down its outer edge, and the stripes run into the cell, stopping
   BLIT_WINDOW_STRIPE_INSET px from the cell's outer edge (the line and one
   clear pixel), so they join the stripes of the next cell. */
enum {
    FR_LEFT, FR_RIGHT, FR_BOTTOM_LEFT, FR_BOTTOM, FR_BOTTOM_RIGHT,
    FR_CHALK_END_LEFT, FR_CHALK_END_RIGHT, FR_SLATE_END_LEFT, FR_SLATE_END_RIGHT
};

static int blit_frame_glyph(uint32_t scalar)
{
    return scalar >= BLIT_FRAME_FIRST && scalar < BLIT_FRAME_FIRST + BLIT_FRAME_COUNT;
}

/* Only Slate's two title-end cells draw in pens: 0 and 1; -1 for the rest. */
static int blit_frame_pen_index(uint32_t scalar)
{
    if (!blit_frame_glyph(scalar) || scalar - BLIT_FRAME_FIRST < FR_SLATE_END_LEFT) return -1;
    return (int)(scalar - BLIT_FRAME_FIRST) - FR_SLATE_END_LEFT;
}

static void blit_frame_roles(uint32_t scalar, unsigned char *roles, int w, int h)
{
    enum { L = BLIT_WINDOW_LINE };
    unsigned index = scalar - BLIT_FRAME_FIRST;
    int n, top, side, bx, by, right;
    WindowCell cell;
    cell.roles = roles; cell.w = w; cell.h = h;
    memset(roles, WR_NONE, (size_t)w * (size_t)h);
    switch (index) {
    case FR_LEFT: wg_fill(&cell, 0, L, 0, h, WR_FG); return;
    case FR_RIGHT: wg_fill(&cell, w - L, w, 0, h, WR_FG); return;
    case FR_BOTTOM_LEFT: wg_fill(&cell, 0, L, 0, h, WR_FG); wg_fill(&cell, 0, w, h - L, h, WR_FG); return;
    case FR_BOTTOM: wg_fill(&cell, 0, w, h - L, h, WR_FG); return;
    case FR_BOTTOM_RIGHT: wg_fill(&cell, w - L, w, 0, h, WR_FG); wg_fill(&cell, 0, w, h - L, h, WR_FG); return;
    default: break;
    }
    right = index == FR_CHALK_END_RIGHT || index == FR_SLATE_END_RIGHT;
    blit_window_band(w, h, &n, &top, &side, &bx, &by);
    wg_bar(&cell, index >= FR_SLATE_END_LEFT, 1, right ? 0 : BLIT_WINDOW_STRIPE_INSET,
           right ? w - BLIT_WINDOW_STRIPE_INSET : w, n, top);
    if (right) wg_fill(&cell, w - L, w, 0, h, WR_FG);
    else wg_fill(&cell, 0, L, 0, h, WR_FG);
}

/* ADR 0210: what SGR 51 draws in a framed cell, as up to four horizontal
   bands {top, bottom, colour}. kind is 1 (51, thin), 3 (51:3, the default
   button's ring), 2 (51:2, a Slate title bar's top and bottom rows: frame
   line and highlight, shade and rule, in the cell's pens as every bar
   glyph has them, ADR 0211; 4, once Slate dark's, is the same), or 5
   (51:5, a rule along the bottom only). line is the thin, thick and rule
   lines' colour, 0xRRGGBB: the underline colour, else the foreground.
   Both compositors draw from this, so a framed title meets the bar glyphs
   beside it row for row. */
typedef struct { int top, bottom; uint32_t colour; } BlitWindowBand;

static int blit_window_framed_bands(int kind, int h, uint32_t line,
                                    const BlitWindowPens *pens, BlitWindowBand band[4])
{
    int L = BLIT_WINDOW_LINE, t;
    if (kind == 2 || kind == 4) {
        if (h < 4 * L) return 0;
        band[0].top = 0; band[0].bottom = L; band[0].colour = pens->fg;
        band[1].top = L; band[1].bottom = 2 * L; band[1].colour = pens->line;
        band[2].top = h - 2 * L; band[2].bottom = h - L; band[2].colour = pens->mix;
        band[3].top = h - L; band[3].bottom = h; band[3].colour = pens->fg;
        return 4;
    }
    if (kind == 5) {
        band[0].top = h - L; band[0].bottom = h; band[0].colour = line;
        return 1;
    }
    t = kind == 3 ? BLIT_WINDOW_THICK_RING : L;
    if (t > h / 2) t = h / 2;
    band[0].top = 0; band[0].bottom = t; band[0].colour = line;
    band[1].top = h - t; band[1].bottom = h; band[1].colour = line;
    return 2;
}

/* ADR 0211: SGR 66's drop shadow in a cell, as up to two rectangles
   [x0, x1) x [y0, y1). kind: 1 right, 2 right from the shadow's offset
   down (a window's first row), 3 under, 4 under from the offset in (its
   first cell), 5 the corner. width is BLIT_WINDOW_LINE, or
   BLIT_WINDOW_SHADOW for a thick shadow. Inset (the default) the strips
   begin BLIT_WINDOW_LINE in from the cell's left or top edge, so on a frame
   glyph's cell they lie just outside its line: a frame glyph and a shadow
   in one cell draw exactly what the frame glyphs drew before 0.3.38.
   Flush they begin at the cell's edge, for the cell beside a frame that
   fills its own cell (a box-drawing border on the window's face). */
typedef struct { int x0, y0, x1, y1; } BlitWindowRect;

static int blit_window_shadow_rects(int kind, int thick, int flush, int w, int h,
                                    BlitWindowRect rect[2])
{
    int L = flush ? 0 : BLIT_WINDOW_LINE, t = thick ? BLIT_WINDOW_SHADOW : BLIT_WINDOW_LINE;
    int SO = BLIT_WINDOW_SHADOW_OFFSET;
    switch (kind) {
    case 1: case 2:
        rect[0].x0 = L; rect[0].x1 = L + t; rect[0].y0 = kind == 2 ? SO : 0; rect[0].y1 = h;
        return 1;
    case 3: case 4:
        /* Inset, the left frame's outer edge is L into the previous cell,
           so the first cell's strip starts SO - L in; flush, the window's
           edge is this cell's own, and it starts SO in. */
        rect[0].x0 = kind == 4 ? SO - L : 0; rect[0].x1 = w; rect[0].y0 = L; rect[0].y1 = L + t;
        return 1;
    case 5:
        /* Where the right strip and the under strip meet: inset an L, not
           the square, so the frame's corner pixel stays the frame's;
           flush the square. */
        rect[0].x0 = L; rect[0].x1 = L + t; rect[0].y0 = 0; rect[0].y1 = L + t;
        rect[1].x0 = 0; rect[1].x1 = L + t; rect[1].y0 = L; rect[1].y1 = L + t;
        return 2;
    default:
        return 0;
    }
}

/* Draws one plane of the glyph as coverage (255 or 0): plane 0, every
   pixel a role inks (Chalk, and the one-colour fallback for Slate), or
   WR_FG, WR_LINE or WR_MIX, that role's pixels alone. BG is never drawn.
   Returns 0 when the cell is too large for the scratch map. */
static int blit_window_draw(uint32_t scalar, int plane, unsigned char *pixels, int stride,
                            int w, int h)
{
    unsigned char roles[128 * 128];
    int x, y;
    if (w <= 0 || h <= 0 || w > 128 || h > 128) return 0;
    blit_window_roles(scalar, roles, w, h);
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        unsigned char role = roles[y * w + x];
        if (role == WR_NONE || role == WR_BG || (plane && role != plane)) continue;
        pixels[y * stride + x] = 255;
    }
    return 1;
}

#endif
