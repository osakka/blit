#ifndef BLIT_PLAYER_GLYPHS_H
#define BLIT_PLAYER_GLYPHS_H

/* ADR 0237: the player glyphs drawn as cell geometry -- an LCD's digits, LED
   level meters, sliders and faders -- for a music player (huiamp) or any
   program that wants them. Coverage in the cell's foreground, so the program
   colours them (a green, amber or blue LCD; green, yellow, red LEDs). A lit
   part is full coverage; an unlit LCD segment or LED is drawn faint
   (BLIT_PLAYER_GHOST), as a real LCD shows its off segments; a slider's
   track ahead of the knob is lighter (BLIT_PLAYER_TRACK). Everything is
   sized from the cell, designed at the owner's 12x24, so the pieces of a
   digit, a meter or a slider meet without a seam at any cell size.

   105 glyphs from U+101314, in this order:
     big digits, two cells wide and two rows tall: for 0-9, minus, blank,
       the top-left, top-right, bottom-left, bottom-right cells (48);
       the big colon, one cell wide, its top and bottom cells (2);
     small digits, two cells wide, one row: for 0-9, minus, blank, the
       left and right cells (24); the small colon, one cell (1);
     LED meters: horizontal steps 0-8 (two LEDs a cell, filled left to
       right), vertical steps 0-8 (four LEDs a cell, filled bottom to top),
       the horizontal peak cap, the vertical peak cap (20);
     a slider: left cap, track, filled track, knob, right cap; a fader:
       top cap, track, filled track, knob, bottom cap (10).
   The catalogue's cell_geometry section names each; player_glyph_gate
   holds the drawing to its rules at every cell size. */

#include <stdint.h>

#define BLIT_PLAYER_FIRST 0x101314U
#define BLIT_PLAYER_COUNT 105U
#define BLIT_PLAYER_GHOST 51          /* an unlit segment or LED: 20% */
#define BLIT_PLAYER_TRACK 96          /* a slider's or fader's track ahead of the knob */

enum {
    BP_BIG = 0,                       /* 12 characters x 4 cells */
    BP_BIG_COLON = 48,                /* top, bottom */
    BP_SMALL = 50,                    /* 12 characters x 2 cells */
    BP_SMALL_COLON = 74,
    BP_LED_H = 75,                    /* steps 0-8 */
    BP_LED_V = 84,                    /* steps 0-8 */
    BP_PEAK_H = 93, BP_PEAK_V = 94,
    BP_SLIDER = 95,                   /* cap-left, track, filled, knob, cap-right */
    BP_FADER = 100                    /* cap-top, track, filled, knob, cap-bottom */
};

/* The seven segments of each character, a..g as on a seven-segment display:
   a top, b upper right, c lower right, d bottom, e lower left, f upper
   left, g middle. Characters 0-9, 10 minus, 11 blank. */
static const unsigned char blit_player_segments[12] = {
    0x3f, 0x06, 0x5b, 0x4f, 0x66, 0x6d, 0x7d, 0x07, 0x7f, 0x6f, 0x40, 0x00
};

static int blit_player_glyph(uint32_t scalar)
{
    return scalar >= BLIT_PLAYER_FIRST && scalar < BLIT_PLAYER_FIRST + BLIT_PLAYER_COUNT;
}

/* Raises the coverage of a rectangle given on a canvas that the cell is the
   part at (ox, oy) of, clipped to the cell. */
static void bp_fill(unsigned char *pixels, int stride, int w, int h, int ox, int oy,
                    int x0, int y0, int x1, int y1, unsigned char coverage)
{
    int x, y;
    x0 -= ox; x1 -= ox; y0 -= oy; y1 -= oy;
    if (x0 < 0) x0 = 0;
    if (y0 < 0) y0 = 0;
    if (x1 > w) x1 = w;
    if (y1 > h) y1 = h;
    for (y = y0; y < y1; ++y)
        for (x = x0; x < x1; ++x)
            if (pixels[y * stride + x] < coverage) pixels[y * stride + x] = coverage;
}

static int bp_round(int num, int den)
{
    return (num + den / 2) / den;
}

/* A character's seven segments in the box x0..x1, y0..y1 (canvas pixels),
   t thick, each shortened by a gap at both ends. */
static void bp_digit(unsigned char *pixels, int stride, int w, int h, int ox, int oy,
                     int x0, int y0, int x1, int y1, int t, unsigned lit)
{
    int bw = x1 - x0, bh = y1 - y0, gap = t >= 2 ? (t + 1) / 3 : 0, k;
    int mid = y0 + bp_round(bh - t, 2);
    int r[7][4];
    /* a, b, c, d, e, f, g: x0, y0, x1, y1 */
    r[0][0] = x0 + t; r[0][1] = y0; r[0][2] = x1 - t; r[0][3] = y0 + t;
    r[1][0] = x1 - t; r[1][1] = y0 + t; r[1][2] = x1; r[1][3] = mid;
    r[2][0] = x1 - t; r[2][1] = mid + t; r[2][2] = x1; r[2][3] = y1 - t;
    r[3][0] = x0 + t; r[3][1] = y1 - t; r[3][2] = x1 - t; r[3][3] = y1;
    r[4][0] = x0; r[4][1] = mid + t; r[4][2] = x0 + t; r[4][3] = y1 - t;
    r[5][0] = x0; r[5][1] = y0 + t; r[5][2] = x0 + t; r[5][3] = mid;
    r[6][0] = x0 + t; r[6][1] = mid; r[6][2] = x1 - t; r[6][3] = mid + t;
    (void)bw;
    for (k = 0; k < 7; ++k) {
        int a = r[k][0], b = r[k][1], c = r[k][2], d = r[k][3];
        if (k == 0 || k == 3 || k == 6) { a += gap; c -= gap; }
        else { b += gap; d -= gap; }
        if (c <= a || d <= b) continue;
        bp_fill(pixels, stride, w, h, ox, oy, a, b, c, d,
                (lit >> k) & 1U ? 255 : BLIT_PLAYER_GHOST);
    }
}

/* The thicknesses, from the cell: a big digit's segments (3 at 24 px), a
   small digit's (2 at 24 px). */
static int bp_big_thick(int h) { return h >= 12 ? (h + 4) / 8 : 1; }
static int bp_small_thick(int h) { return h >= 12 ? (h + 6) / 12 : 1; }

/* A slider's track and a fader's: 3 px at 24, centred. */
static int bp_track_thick(int h) { return h >= 12 ? (h + 4) / 8 : 1; }

/* Draws the glyph as coverage into a w x h cell (pixels zeroed by the
   caller). Returns 0 for a scalar not in the set or a cell too small. */
static int blit_player_draw(uint32_t scalar, unsigned char *pixels, int stride, int w, int h)
{
    unsigned i;
    if (!blit_player_glyph(scalar) || w < 2 || h < 4) return 0;
    i = scalar - BLIT_PLAYER_FIRST;
    if (i < BP_BIG_COLON) {
        /* A big digit: two cells by two rows, this the (part % 2, part / 2)
           cell of it. */
        unsigned ch = i / 4, part = i % 4;
        int W = 2 * w, H = 2 * h, ix = w >= 6 ? bp_round(w, 6) : 1, iy = bp_round(h, 8);
        bp_digit(pixels, stride, w, h, (int)(part % 2) * w, (int)(part / 2) * h,
                 ix, iy, W - ix, H - iy, bp_big_thick(h), blit_player_segments[ch]);
        return 1;
    }
    if (i < BP_SMALL) {
        int d = bp_big_thick(h), oy = (int)(i - BP_BIG_COLON) * h, x = (w - d) / 2;
        int y_top = bp_round(2 * h * 30, 100) - d / 2, y_bottom = bp_round(2 * h * 66, 100) - d / 2;
        bp_fill(pixels, stride, w, h, 0, oy, x, y_top, x + d, y_top + d, 255);
        bp_fill(pixels, stride, w, h, 0, oy, x, y_bottom, x + d, y_bottom + d, 255);
        return 1;
    }
    if (i < BP_SMALL_COLON) {
        unsigned ch = (i - BP_SMALL) / 2, part = (i - BP_SMALL) % 2;
        int W = 2 * w, ix = w >= 4 ? bp_round(w, 4) : 1, iy = bp_round(h, 8);
        bp_digit(pixels, stride, w, h, (int)part * w, 0, ix, iy, W - ix, h - iy,
                 bp_small_thick(h), blit_player_segments[ch]);
        return 1;
    }
    if (i == BP_SMALL_COLON) {
        int d = bp_small_thick(h) + 1, x = (w - d) / 2;
        int y_top = bp_round(h * 30, 100) - d / 2, y_bottom = bp_round(h * 66, 100) - d / 2;
        bp_fill(pixels, stride, w, h, 0, 0, x, y_top, x + d, y_top + d, 255);
        bp_fill(pixels, stride, w, h, 0, 0, x, y_bottom, x + d, y_bottom + d, 255);
        return 1;
    }
    if (i < BP_LED_V) {
        /* Two LEDs a cell, each four steps, a gap round each. */
        int step = (int)(i - BP_LED_H), led, lw = w / 2, gap = lw >= 3 ? 1 : 0;
        int y0 = h / 4, y1 = h - h / 4;
        for (led = 0; led < 2; ++led) {
            int x0 = led * lw + gap, x1 = (led == 1 ? w : (led + 1) * lw) - gap;
            int lit = step - 4 * led, cut;
            if (lit < 0) lit = 0;
            if (lit > 4) lit = 4;
            cut = x0 + bp_round((x1 - x0) * lit, 4);
            bp_fill(pixels, stride, w, h, 0, 0, x0, y0, cut, y1, 255);
            bp_fill(pixels, stride, w, h, 0, 0, cut, y0, x1, y1, BLIT_PLAYER_GHOST);
        }
        return 1;
    }
    if (i < BP_PEAK_H) {
        /* Four LEDs a cell, bottom to top, each two steps. */
        int step = (int)(i - BP_LED_V), led, x0 = bp_round(w * 2, 10), x1 = w - x0;
        for (led = 0; led < 4; ++led) {
            int bottom = h - (h * led) / 4, top = h - (h * (led + 1)) / 4;
            int gap = bottom - top >= 3 ? 1 : 0, lit = step - 2 * led, cut;
            if (lit < 0) lit = 0;
            if (lit > 2) lit = 2;
            top += gap;
            bottom -= gap;
            cut = bottom - bp_round((bottom - top) * lit, 2);
            bp_fill(pixels, stride, w, h, 0, 0, x0, cut, x1, bottom, 255);
            bp_fill(pixels, stride, w, h, 0, 0, x0, top, x1, cut, BLIT_PLAYER_GHOST);
        }
        return 1;
    }
    if (i == BP_PEAK_H) {
        int pw = w >= 6 ? w / 6 : 1, x = bp_round(w * 4, 10);
        bp_fill(pixels, stride, w, h, 0, 0, x, h / 4, x + pw, h - h / 4, 255);
        return 1;
    }
    if (i == BP_PEAK_V) {
        int ph = h >= 12 ? h / 12 : 1, x0 = bp_round(w * 2, 10), lh = h / 4;
        int y = (lh - ph) / 2;
        bp_fill(pixels, stride, w, h, 0, 0, x0, y, w - x0, y + ph, 255);
        return 1;
    }
    if (i < BP_FADER) {
        /* A slider: the track through the middle of the row. */
        unsigned piece = i - BP_SLIDER;
        int t = bp_track_thick(h), y0 = (h - t) / 2, bar = w >= 6 ? w / 6 : 1;
        int reach = bp_round(h * 5, 24), mid = w / 2;
        switch (piece) {
        case 0:   /* cap-left: an end bar, the filled track from it */
            bp_fill(pixels, stride, w, h, 0, 0, mid, h / 2 - reach, mid + bar, h / 2 + reach, 255);
            bp_fill(pixels, stride, w, h, 0, 0, mid, y0, w, y0 + t, 255);
            break;
        case 1:   /* track */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + t, BLIT_PLAYER_TRACK);
            break;
        case 2:   /* filled */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + t, 255);
            break;
        case 3: { /* knob, with a groove down its middle */
            int kx = w >= 6 ? w / 6 : 0, ky = bp_round(h * 5, 24), gy = bp_round(h * 8, 24), x, y;
            bp_fill(pixels, stride, w, h, 0, 0, kx, ky, w - kx, h - ky, 255);
            if (w >= 6 && h - 2 * gy > 0)
                for (y = gy; y < h - gy; ++y)
                    for (x = mid - bar / 2; x < mid - bar / 2 + bar; ++x) pixels[y * stride + x] = 0;
            break;
        }
        default:  /* cap-right: the track to an end bar */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, mid, y0 + t, BLIT_PLAYER_TRACK);
            bp_fill(pixels, stride, w, h, 0, 0, mid - bar, h / 2 - reach, mid, h / 2 + reach, 255);
            break;
        }
        return 1;
    }
    {
        /* A fader: the track down the middle of the column. */
        unsigned piece = i - BP_FADER;
        int t = w >= 6 ? bp_round(w, 4) : 1, x0 = (w - t) / 2, bar = h >= 12 ? h / 12 : 1;
        int reach = w >= 6 ? w / 6 : 0, mid = h / 2;
        switch (piece) {
        case 0:   /* cap-top: an end bar, the track down from it */
            bp_fill(pixels, stride, w, h, 0, 0, reach, mid, w - reach, mid + bar, 255);
            bp_fill(pixels, stride, w, h, 0, 0, x0, mid, x0 + t, h, BLIT_PLAYER_TRACK);
            break;
        case 1:   /* track */
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, h, BLIT_PLAYER_TRACK);
            break;
        case 2:   /* filled */
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, h, 255);
            break;
        case 3: { /* knob, with a groove across its middle */
            int ky = h / 4, kx = w >= 8 ? 1 : 0, gx = w >= 6 ? w / 4 : 0, x, y;
            bp_fill(pixels, stride, w, h, 0, 0, kx, ky, w - kx, h - ky, 255);
            if (h >= 12 && w - 2 * gx > 0)
                for (y = mid - bar / 2 - (bar < 2); y < mid - bar / 2 + bar; ++y)
                    for (x = gx; x < w - gx; ++x) pixels[y * stride + x] = 0;
            break;
        }
        default:  /* cap-bottom: the filled track up to an end bar */
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, mid, 255);
            bp_fill(pixels, stride, w, h, 0, 0, reach, mid - bar, w - reach, mid, 255);
            break;
        }
        return 1;
    }
}

#endif
