#ifndef BLIT_CONTROL_GLYPHS_H
#define BLIT_CONTROL_GLYPHS_H

/* ADR 0212: the controls glyph set -- a checkbox, a compact switch and a
   capsule switch -- drawn as cell geometry like the window chrome (ADR
   0210, 0211) and from the same roles: Chalk in the foreground, Slate and
   the capsule in the cell's pens (foreground, SGR 58, half foreground and
   half background). Twenty glyphs from U+10029A, in this order:
     checkbox, Chalk and Slate: off, on, mixed (one cell each);
     compact switch, Chalk and Slate: off-left, off-right, on-left,
       on-right (two cells wide);
     capsule: off-left, off-mid, off-right, on-left, on-mid, on-right
       (three cells wide).
   tools/fonts/draw-control-glyphs.py reads the geometry table below and
   the window table, writes the same rules once more, and is what
   control_glyph_gate holds this drawing to, pixel for pixel.

   Pixel-designed for 11x22. The checkbox and the switch stand on the window
   boxes' rows and width (blit_window_band), so they scale as those do. The
   capsule scales with the cell's height, in 22nds of it; see its table. */

#include "blit_window_glyphs.h"

#define BLIT_CONTROL_FIRST 0x10029AU
#define BLIT_CONTROL_COUNT 20U

/* The geometry table. Checkbox and switch, in pixels: */
#define BLIT_CONTROL_MARK_INSET 2       /* a mark's distance from the box's line... */
#define BLIT_CONTROL_MARK_FROM 7        /* ...in a box this wide or wider; else 1 */
#define BLIT_CONTROL_CHECK_THICK 2      /* the Slate check and dash's rows... */
#define BLIT_CONTROL_CHECK_FROM 9       /* ...in a box this wide or wider; else 1 */
#define BLIT_CONTROL_KNOB_GAP 1         /* clear between a switch's ring and its knob */
#define BLIT_CONTROL_KNOB_MIN 3         /* a smaller knob has no gap, and no ring */
/* The capsule, in 22nds of the cell's height (so 1 px at 22 px), across
   three cells: */
#define BLIT_CAPSULE_UNITS 22           /* the design height */
#define BLIT_CAPSULE_MARGIN 1           /* clear at each end of the track */
#define BLIT_CAPSULE_RADIUS 9           /* the track's ends: its height is 2x this */
#define BLIT_CAPSULE_KNOB 14            /* the knob's diameter */
#define BLIT_CAPSULE_KNOB_INSET 2       /* the knob's distance from the track's end */
#define BLIT_CAPSULE_SAMPLES 8          /* sub-samples a pixel a side, for coverage */
#define BLIT_CAPSULE_FULL 48            /* of 64 samples or more: the pen itself */
#define BLIT_CAPSULE_EDGE 16            /* of 64 or more: half foreground, half background */

/* The order within the set. */
enum {
    CG_CHALK_CHECK_OFF, CG_CHALK_CHECK_ON, CG_CHALK_CHECK_MIXED,
    CG_SLATE_CHECK_OFF, CG_SLATE_CHECK_ON, CG_SLATE_CHECK_MIXED,
    CG_CHALK_SWITCH_OFF_LEFT, CG_CHALK_SWITCH_OFF_RIGHT,
    CG_CHALK_SWITCH_ON_LEFT, CG_CHALK_SWITCH_ON_RIGHT,
    CG_SLATE_SWITCH_OFF_LEFT, CG_SLATE_SWITCH_OFF_RIGHT,
    CG_SLATE_SWITCH_ON_LEFT, CG_SLATE_SWITCH_ON_RIGHT,
    CG_CAPSULE_OFF_LEFT, CG_CAPSULE_OFF_MID, CG_CAPSULE_OFF_RIGHT,
    CG_CAPSULE_ON_LEFT, CG_CAPSULE_ON_MID, CG_CAPSULE_ON_RIGHT
};

static int blit_control_glyph(uint32_t scalar)
{
    return scalar >= BLIT_CONTROL_FIRST && scalar < BLIT_CONTROL_FIRST + BLIT_CONTROL_COUNT;
}

/* Slate's and the capsule's glyphs are drawn in pens, in a dense order
   0..12 (three checkboxes, four switch cells, six capsule cells); Chalk's
   are coverage, drawn in the foreground, and have none: -1. */
static int blit_control_pen_index(uint32_t scalar)
{
    unsigned c;
    if (!blit_control_glyph(scalar)) return -1;
    c = scalar - BLIT_CONTROL_FIRST;
    if (c >= CG_SLATE_CHECK_OFF && c <= CG_SLATE_CHECK_MIXED) return (int)(c - CG_SLATE_CHECK_OFF);
    if (c >= CG_SLATE_SWITCH_OFF_LEFT) return (int)(c - CG_SLATE_SWITCH_OFF_LEFT) + 3;
    return -1;
}

#define BLIT_CONTROL_PENS 13U
/* One dense index for every glyph drawn in pens, window or control, so the
   raster keeps one plane cache: Slate's window glyphs first, then the
   controls, then Slate's two title-end cells (ADR 0214). */
#define BLIT_FRAME_PENS 4U
#define BLIT_CHROME_PENS (BLIT_WINDOW_SET + BLIT_CONTROL_PENS + BLIT_FRAME_PENS)

static int blit_chrome_glyph(uint32_t scalar)
{
    return blit_window_glyph(scalar) || blit_control_glyph(scalar) || blit_frame_glyph(scalar);
}

static int blit_chrome_pen_index(uint32_t scalar)
{
    int pen;
    if (blit_window_colour(scalar)) return (int)(scalar - (BLIT_WINDOW_FIRST + BLIT_WINDOW_SET));
    pen = blit_frame_pen_index(scalar);
    if (pen >= 0) return (int)(BLIT_WINDOW_SET + BLIT_CONTROL_PENS) + pen;
    pen = blit_control_pen_index(scalar);
    return pen < 0 ? -1 : (int)BLIT_WINDOW_SET + pen;
}

static int blit_chrome_colour(uint32_t scalar)
{
    return blit_chrome_pen_index(scalar) >= 0;
}

/* Is the pixel whose centre is (px, py), in half pixels, inside a stadium
   `width` px wide and s px high, its ends round, drawn in from its edge by
   `shrink` px? The ends' centres do not move as it shrinks. */
static int cg_in_stadium(long px, long py, int width, int s, int shrink)
{
    long r = (long)s - 2L * shrink, dx, dy;
    if (r <= 0) return 0;
    dx = wg_max3((int)(s - px), 0, (int)(px - (2L * width - s)));
    dy = py - s;
    return dx * dx + dy * dy <= r * r;
}

/* The same for a disc of radius r half pixels about (cx, cy). */
static int cg_in_disc(long px, long py, long cx, long cy, long r)
{
    return (px - cx) * (px - cx) + (py - cy) * (py - cy) <= r * r;
}

/* A switch's knob: the track's height less a ring and a gap each side, or
   less the ring alone where that would leave under BLIT_CONTROL_KNOB_MIN. */
static int cg_knob(int s)
{
    int kd = s - 2 * (BLIT_WINDOW_LINE + BLIT_CONTROL_KNOB_GAP);
    if (kd < BLIT_CONTROL_KNOB_MIN) kd = s - 2 * BLIT_WINDOW_LINE;
    return kd < 1 ? 1 : kd;
}

/* One pixel (xs along the whole two-cell track, y down the cell) of a
   compact switch whose track is s px high, its top at row `by`. */
static unsigned char cg_switch_role(int slate, int on, int xs, int y, int w, int s, int by)
{
    int L = BLIT_WINDOW_LINE, kd = cg_knob(s), gi = (s - kd) / 2, ty = y - by;
    int kx = on ? 2 * w - gi - kd : gi;
    long px = 2L * xs + 1, py = 2L * ty + 1, cxh = 2L * kx + kd;
    int track, inner, knob, knob_inner;
    if (ty < 0 || ty >= s) return WR_NONE;
    track = cg_in_stadium(px, py, 2 * w, s, 0);
    if (!track) return WR_NONE;
    inner = cg_in_stadium(px, py, 2 * w, s, L);
    knob = cg_in_disc(px, py, cxh, s, kd);
    if (!slate) {
        /* Chalk: off, a ring and a solid knob; on, a solid track with the
           knob left out of it. */
        if (on) return knob ? WR_NONE : WR_FG;
        return (!inner || knob) ? WR_FG : WR_NONE;
    }
    knob_inner = kd >= BLIT_CONTROL_KNOB_MIN ? cg_in_disc(px, py, cxh, s, kd - 2) : knob;
    if (knob) {
        if (!knob_inner) return WR_FG;
        /* The knob's face is the line pen, shaded on its last inside row. */
        return kd >= BLIT_CONTROL_KNOB_MIN && ty - gi == kd - 2 ? WR_MIX : WR_LINE;
    }
    if (!inner) return WR_FG;
    if (on) return WR_MIX;
    /* Off: the cell's background for a face, with a highlight along the
       first row inside the ring, stopping a pixel short of the curve. */
    if (ty == L && cg_in_stadium(px - 2, py, 2 * w, s, L) && cg_in_stadium(px + 2, py, 2 * w, s, L))
        return WR_LINE;
    return WR_BG;
}

/* A checkbox's mark and shade, drawn on the box (side, at bx, by). */
static void cg_checkbox(int slate, int state, WindowCell *cell, int side, int bx, int by)
{
    int L = BLIT_WINDOW_LINE;
    int m = side >= BLIT_CONTROL_MARK_FROM ? BLIT_CONTROL_MARK_INSET : 1;
    int T = side >= BLIT_CONTROL_CHECK_FROM ? BLIT_CONTROL_CHECK_THICK : 1;
    int span = side - 2 * m, i, t;
#define CG_PUT(px_, py_) wg_fill(cell, bx + (px_), bx + (px_) + 1, by + (py_), by + (py_) + 1, WR_FG)
    if (slate) wg_fill(cell, bx, bx + side, by, by + side, WR_LINE);
    wg_outline(cell, bx, bx + side, by, by + side, L);
    if (slate) {
        /* The shade: the bottom and the right inside edge. */
        wg_fill(cell, bx + L, bx + side - L, by + side - 2 * L, by + side - L, WR_MIX);
        wg_fill(cell, bx + side - 2 * L, bx + side - L, by + L, by + side - L, WR_MIX);
    }
    if (state == 1 && !slate) {
        /* An X, one pixel wide, corner to corner of the marks' square. */
        for (i = 0; i < span; ++i) {
            CG_PUT(m + i, m + i);
            CG_PUT(m + i, m + span - 1 - i);
        }
    } else if (state == 1) {
        /* A check mark, T rows thick: down to its vertex a third of the way
           along, then up to the right; its lowest row is just above the
           shade, and no row is above the first inside the box. */
        int la = (span - 1) / 3, yv = side - 2 - T;
        for (i = 0; i < span; ++i) {
            int cy = i <= la ? yv - la + i : yv - (i - la);
            for (t = 0; t < T; ++t) {
                int yy = cy + t < 1 ? 1 : cy + t;
                CG_PUT(m + i, yy);
            }
        }
    } else if (state == 2 && !slate) {
        for (i = 0; i < span; ++i) CG_PUT(m + i, side / 2);
    } else if (state == 2) {
        /* A heavier dash, centred on the face. */
        int x1 = side - 2 - m, y0 = (side - 1 - T) / 2;
        if (x1 < m) x1 = m;
        if (y0 < 1) y0 = 1;
        for (i = m; i <= x1; ++i)
            for (t = 0; t < T; ++t) CG_PUT(i, y0 + t);
    }
#undef CG_PUT
}

/* One pixel of the capsule (xs along its three cells, y down the cell, w
   and h the cell's size): the track and the knob are counted in 8x8
   sub-samples, each sample a point at the middle of its sixty-fourth of the
   pixel, in integers -- positions in 1/22 of 1/16 of a pixel, so that the
   design's lengths, in 22nds of the height, multiply in exactly. The knob
   decides first: a pixel it covers 48 samples of or more is the line pen,
   16 or more the mix (whatever lies under it). Else the track: 48 or more
   is the foreground, 16 or more the mix. No outline, no pixel of any fixed
   colour: the application chooses the track's colour, in the foreground. */
static unsigned char cg_capsule_role(int on, int xs, int y, int w, int h)
{
    enum { S = BLIT_CAPSULE_SAMPLES, U = BLIT_CAPSULE_UNITS };
    long long u = (long long)h * 2 * S, total = 3LL * w * U * 2 * S;
    long long cy = U * u / 2, left = (BLIT_CAPSULE_MARGIN + BLIT_CAPSULE_RADIUS) * u;
    long long right = total - left, rt = BLIT_CAPSULE_RADIUS * u, rk = BLIT_CAPSULE_KNOB * u / 2;
    long long kx = on ? total - (BLIT_CAPSULE_MARGIN + BLIT_CAPSULE_KNOB_INSET) * u - rk :
                        (BLIT_CAPSULE_MARGIN + BLIT_CAPSULE_KNOB_INSET) * u + rk;
    int tc = 0, kc = 0, ix, iy;
    for (iy = 0; iy < S; ++iy) for (ix = 0; ix < S; ++ix) {
        long long X = (long long)U * (2LL * S * xs + 2 * ix + 1);
        long long Y = (long long)U * (2LL * S * y + 2 * iy + 1);
        long long dx = X < left ? left - X : X > right ? X - right : 0, dy = Y - cy;
        if (dx * dx + dy * dy <= rt * rt) ++tc;
        if ((X - kx) * (X - kx) + dy * dy <= rk * rk) ++kc;
    }
    if (kc >= BLIT_CAPSULE_FULL) return WR_LINE;
    if (kc >= BLIT_CAPSULE_EDGE) return WR_MIX;
    if (tc >= BLIT_CAPSULE_FULL) return WR_FG;
    if (tc >= BLIT_CAPSULE_EDGE) return WR_MIX;
    return WR_NONE;
}

/* One control glyph's roles, w x h, row-major; WR_NONE is transparent. */
static void blit_control_roles(uint32_t scalar, unsigned char *roles, int w, int h)
{
    unsigned c = scalar - BLIT_CONTROL_FIRST;
    int n, top, side, bx, by, x, y;
    WindowCell cell;
    cell.roles = roles; cell.w = w; cell.h = h;
    memset(roles, WR_NONE, (size_t)w * (size_t)h);
    blit_window_band(w, h, &n, &top, &side, &bx, &by);
    if (c <= CG_SLATE_CHECK_MIXED) {
        cg_checkbox(c >= CG_SLATE_CHECK_OFF, (int)(c % 3), &cell, side, bx, by);
    } else if (c <= CG_SLATE_SWITCH_ON_RIGHT) {
        unsigned k = c - CG_CHALK_SWITCH_OFF_LEFT;
        int slate = k >= 4, on = (k % 4) >= 2, part = (int)(k & 1);
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x)
            roles[y * w + x] = cg_switch_role(slate, on, part * w + x, y, w, side, by);
    } else {
        unsigned k = c - CG_CAPSULE_OFF_LEFT;
        int on = k >= 3, part = (int)(k % 3);
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x)
            roles[y * w + x] = cg_capsule_role(on, part * w + x, y, w, h);
    }
}

/* Draws one plane of a window, control or frame glyph as coverage: plane 0, every
   pixel a role inks; or WR_FG, WR_LINE or WR_MIX, that role's pixels alone.
   BG is never drawn. Returns 0 when the cell is too large for the scratch
   map. */
static int blit_chrome_draw(uint32_t scalar, int plane, unsigned char *pixels, int stride,
                            int w, int h)
{
    unsigned char roles[128 * 128];
    int x, y;
    if (blit_window_glyph(scalar)) return blit_window_draw(scalar, plane, pixels, stride, w, h);
    if (!blit_control_glyph(scalar) && !blit_frame_glyph(scalar)) return 0;
    if (w <= 0 || h <= 0 || w > 128 || h > 128) return 0;
    if (blit_frame_glyph(scalar)) blit_frame_roles(scalar, roles, w, h);
    else blit_control_roles(scalar, roles, w, h);
    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
        unsigned char role = roles[y * w + x];
        if (role == WR_NONE || role == WR_BG || (plane && role != plane)) continue;
        pixels[y * stride + x] = 255;
    }
    return 1;
}

#endif
