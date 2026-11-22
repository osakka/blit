#ifndef BLIT_SLIDER_GLYPHS_H
#define BLIT_SLIDER_GLYPHS_H

/* Slider styles: two more looks for the slider and the fader of ADR 0237,
   for a Slider Style choice (Hibr's Control Panel > Appearance > Controls).
   Each style is the same five pieces in the same order as the grooved
   knob's (slider-*, fader-*), so a style is a base codepoint and the same
   offsets, and the cells line up one for one: a slider is the left cap,
   the filled track up to the knob, the knob, the track ahead, the right
   cap, on the row's centre line; a fader the same top to bottom. As the
   player glyphs, coverage in the cell's foreground: the program colours
   each piece. The knob's cell holds the knob alone, never a piece of track,
   since a program pens the knob in a colour of its own and the knob takes
   an end's cell at 0% and 100%.

   20 glyphs from U+101495, in this order:
     circle: a round knob on a thin line, the track ahead lighter
       (BLIT_PLAYER_TRACK) -- slider cap-left, track, filled, knob,
       cap-right; fader cap-top, track, filled, knob, cap-bottom (10);
     groove: a channel, hollow ahead of the knob and solid behind it, and a
       square thumb standing proud of it, all full coverage on whole pixels
       -- the same ten (10).
   Sized from the cell, designed at the owner's 12x24. */

#include "blit_player_glyphs.h"

#define BLIT_SLIDER_STYLE_FIRST 0x101495U
#define BLIT_SLIDER_STYLE_COUNT 20U
#define BLIT_SLIDER_SAMPLES 8          /* sub-samples a pixel a side, for the circle */

enum {
    BS_CIRCLE = 0,                    /* slider 0-4, fader 5-9 */
    BS_GROOVE = 10                    /* slider 0-4, fader 5-9 */
};

static int blit_slider_style_glyph(uint32_t scalar)
{
    return scalar >= BLIT_SLIDER_STYLE_FIRST &&
        scalar < BLIT_SLIDER_STYLE_FIRST + BLIT_SLIDER_STYLE_COUNT;
}

/* Raises the cell's coverage by a disc: centre (cx, cy) and radius r in
   16ths of a pixel, sampled BLIT_SLIDER_SAMPLES x BLIT_SLIDER_SAMPLES a
   pixel, at most peak. */
static void bs_disc(unsigned char *pixels, int stride, int w, int h,
                    long cx, long cy, long r, unsigned char peak)
{
    const int n = BLIT_SLIDER_SAMPLES, step = 16 / BLIT_SLIDER_SAMPLES;
    int x, y, i, j;
    for (y = 0; y < h; ++y)
        for (x = 0; x < w; ++x) {
            int in = 0;
            unsigned char v;
            for (j = 0; j < n; ++j)
                for (i = 0; i < n; ++i) {
                    long dx = (long)x * 16 + i * step + step / 2 - cx;
                    long dy = (long)y * 16 + j * step + step / 2 - cy;
                    in += dx * dx + dy * dy <= r * r;
                }
            v = (unsigned char)((in * peak + n * n / 2) / (n * n));
            if (pixels[y * stride + x] < v) pixels[y * stride + x] = v;
        }
}

/* The circle: the line's thickness (2 px at 24, 1 at 16, 3 at 40) and the
   knob's diameter (the cell's width, at most half its height), the same in
   a slider and a fader. */
static int bs_line_thick(int across) { int t = bp_round(across, 12); return t < 1 ? 1 : t; }
static int bs_knob_diameter(int w, int h) { return w < h / 2 ? w : h / 2; }

static void bs_circle(unsigned piece, int fader, unsigned char *pixels, int stride, int w, int h)
{
    int d = bs_knob_diameter(w, h);
    if (!fader) {
        int t = bs_line_thick(h), y0 = (h - t) / 2, mid = w / 2;
        long cy = (long)(2 * y0 + t) * 8;           /* the line's centre, 16ths */
        switch (piece) {
        case 0:   /* cap-left: the filled line from the middle, a round end */
            bp_fill(pixels, stride, w, h, 0, 0, mid, y0, w, y0 + t, 255);
            bs_disc(pixels, stride, w, h, (long)mid * 16, cy, (long)t * 8, 255);
            break;
        case 1:   /* track */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + t, BLIT_PLAYER_TRACK);
            break;
        case 2:   /* filled */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + t, 255);
            break;
        case 3:   /* knob: a disc on the line's centre */
            bs_disc(pixels, stride, w, h, (long)w * 8, cy, (long)d * 8, 255);
            break;
        default:  /* cap-right: the track to the middle, a round end */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, mid, y0 + t, BLIT_PLAYER_TRACK);
            bs_disc(pixels, stride, w, h, (long)mid * 16, cy, (long)t * 8, BLIT_PLAYER_TRACK);
            break;
        }
    } else {
        int t = bs_line_thick(2 * w), x0 = (w - t) / 2, mid = h / 2;
        long cx = (long)(2 * x0 + t) * 8;
        switch (piece) {
        case 0:   /* cap-top: a round end at the middle, the track down */
            bp_fill(pixels, stride, w, h, 0, 0, x0, mid, x0 + t, h, BLIT_PLAYER_TRACK);
            bs_disc(pixels, stride, w, h, cx, (long)mid * 16, (long)t * 8, BLIT_PLAYER_TRACK);
            break;
        case 1:
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, h, BLIT_PLAYER_TRACK);
            break;
        case 2:
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, h, 255);
            break;
        case 3:
            bs_disc(pixels, stride, w, h, cx, (long)h * 8, (long)d * 8, 255);
            break;
        default:  /* cap-bottom: the filled line up to a round end */
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + t, mid, 255);
            bs_disc(pixels, stride, w, h, cx, (long)mid * 16, (long)t * 8, 255);
            break;
        }
    }
}

/* The groove: a channel a quarter of the row tall (6 px at 24), its edges
   1 px (2 at 40); the thumb a square the cell's width less a margin each
   side (1 px at 12), proud of the channel above and below. The channel's
   ends line up with the thumb's at either end of its travel. */
static void bs_groove(unsigned piece, int fader, unsigned char *pixels, int stride, int w, int h)
{
    int g = w >= 6 ? bp_round(w, 12) : 0, s, ks;
    if (g < 1 && w >= 6) g = 1;
    s = w - 2 * g;
    ks = s < h - 2 ? s : h - 2;
    if (!fader) {
        int c = bp_round(h, 4), lw = bp_round(h, 24), y0, ky = (h - ks) / 2;
        if (c < 3) c = 3;
        if (lw < 1) lw = 1;
        y0 = (h - c) / 2;
        switch (piece) {
        case 0:   /* cap-left: the solid channel from the thumb's left edge */
            bp_fill(pixels, stride, w, h, 0, 0, g, y0, w, y0 + c, 255);
            break;
        case 1:   /* track: the channel's two edges */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + lw, 255);
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0 + c - lw, w, y0 + c, 255);
            break;
        case 2:   /* filled: the channel solid */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w, y0 + c, 255);
            break;
        case 3:   /* knob: the square thumb */
            bp_fill(pixels, stride, w, h, 0, 0, g, ky, w - g, ky + ks, 255);
            break;
        default:  /* cap-right: the edges to the thumb's right edge, closed */
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0, w - g, y0 + lw, 255);
            bp_fill(pixels, stride, w, h, 0, 0, 0, y0 + c - lw, w - g, y0 + c, 255);
            bp_fill(pixels, stride, w, h, 0, 0, w - g - lw, y0, w - g, y0 + c, 255);
            break;
        }
    } else {
        int c = bp_round(w, 2), lw = bp_round(w, 12), x0, ky = (h - s) / 2;
        if (c < 3) c = 3;
        if (c > w) c = w;
        if (lw < 1) lw = 1;
        if (ky < 0) ky = 0;
        x0 = (w - c) / 2;
        switch (piece) {
        case 0:   /* cap-top: closed at the thumb's top edge, the edges down */
            bp_fill(pixels, stride, w, h, 0, 0, x0, ky, x0 + c, ky + lw, 255);
            bp_fill(pixels, stride, w, h, 0, 0, x0, ky, x0 + lw, h, 255);
            bp_fill(pixels, stride, w, h, 0, 0, x0 + c - lw, ky, x0 + c, h, 255);
            break;
        case 1:
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + lw, h, 255);
            bp_fill(pixels, stride, w, h, 0, 0, x0 + c - lw, 0, x0 + c, h, 255);
            break;
        case 2:
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + c, h, 255);
            break;
        case 3:
            bp_fill(pixels, stride, w, h, 0, 0, g, ky, w - g, ky + s, 255);
            break;
        default:  /* cap-bottom: solid down to the thumb's bottom edge */
            bp_fill(pixels, stride, w, h, 0, 0, x0, 0, x0 + c, h - ky, 255);
            break;
        }
    }
}

/* Draws the glyph as coverage into a w x h cell (pixels zeroed by the
   caller). Returns 0 for a scalar not in the set or a cell too small. */
static int blit_slider_style_draw(uint32_t scalar, unsigned char *pixels, int stride, int w, int h)
{
    unsigned i, k;
    if (!blit_slider_style_glyph(scalar) || w < 2 || h < 4) return 0;
    i = scalar - BLIT_SLIDER_STYLE_FIRST;
    k = i % 10;
    if (i < BS_GROOVE) bs_circle(k % 5, k >= 5, pixels, stride, w, h);
    else bs_groove(k % 5, k >= 5, pixels, stride, w, h);
    return 1;
}

#endif
