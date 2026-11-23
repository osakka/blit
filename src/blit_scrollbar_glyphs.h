#ifndef BLIT_SCROLLBAR_GLYPHS_H
#define BLIT_SCROLLBAR_GLYPHS_H

/* Scroll bar styles: three looks for Hibr's scroll bar (widgets/scrollbar,
   dt_scrollbar and dt_hscrollbar), for a Scroll Bar Style choice beside
   the Slider Style. Hibr draws a bar as a track of one glyph over every
   cell and a thumb of one glyph over the cell where the view is; these
   are that track and that thumb, and the pieces a bar needs beyond them:
   end caps at the first and last cell, and a thumb more than one cell
   long (start, middle, a middle with grips, end) for a program that sizes
   the thumb to the view. Each style is the same eight pieces in the same
   order, horizontal then vertical, so a style is a base codepoint and the
   same offsets:
     +0 cap-start (left, top)    +4 thumb-mid
     +1 track                    +5 thumb-grip (the middle cell of a long thumb)
     +2 thumb (one cell)         +6 thumb-end
     +3 thumb-start              +7 cap-end (right, bottom)
   and +8 to +15 the same down a column.
   As the player glyphs and the slider styles, coverage in the cell's
   foreground: the program colours each piece (Hibr pens the track idle
   and the thumb active). A thumb's cell holds the thumb alone, never a
   piece of track, since it is penned in a colour of its own and it takes
   an end's cell at the top and the bottom of its travel; so a cap ends
   where the thumb's end does in that cell.
   A horizontal bar is as thick as a vertical one (both from the cell's
   width), on the row's middle.

   48 glyphs from U+1014A9, in this order:
     classic: a fine checker track between two edge lines, closed at the
       caps; the thumb a hollow box on the edges with grip lines across its
       middle where they fit -- whole pixels, no antialiasing (16);
     modern: a capsule thumb about half the cell's width, round ends
       antialiased, on a faint track of the same width (16);
     simple: the light box-drawing line as the track, a solid rectangle
       the thumb, ending a pixel short of the cell's end (16).
   Sized from the cell, designed at the owner's 12x24. */

#include "blit_player_glyphs.h"

#define BLIT_SCROLLBAR_FIRST 0x1014A9U
#define BLIT_SCROLLBAR_COUNT 48U
#define BLIT_SCROLLBAR_PIECES 8U         /* a bar's pieces, one orientation */
#define BLIT_SCROLLBAR_FAINT 72          /* the modern track's coverage */
#define BLIT_SCROLLBAR_SAMPLES 8         /* sub-samples a pixel a side, for the capsule */

enum {
    SB_CLASSIC = 0,                      /* horizontal 0-7, vertical 8-15 */
    SB_MODERN = 16,
    SB_SIMPLE = 32
};

enum {
    SB_CAP_START, SB_TRACK, SB_THUMB, SB_THUMB_START,
    SB_THUMB_MID, SB_THUMB_GRIP, SB_THUMB_END, SB_CAP_END
};

static int blit_scrollbar_glyph(uint32_t scalar)
{
    return scalar >= BLIT_SCROLLBAR_FIRST && scalar < BLIT_SCROLLBAR_FIRST + BLIT_SCROLLBAR_COUNT;
}

/* A bar drawn along its length: a is along the bar (0..len, from the left
   or the top), c across it (0..cross). */
typedef struct {
    unsigned char *pixels;
    int stride, w, h, vertical, len, cross;
} SbCell;

static void sb_fill(const SbCell *s, int a0, int a1, int c0, int c1, unsigned char coverage)
{
    if (s->vertical) bp_fill(s->pixels, s->stride, s->w, s->h, 0, 0, c0, a0, c1, a1, coverage);
    else bp_fill(s->pixels, s->stride, s->w, s->h, 0, 0, a0, c0, a1, c1, coverage);
}

static void sb_dot(const SbCell *s, int a, int c, unsigned char coverage)
{
    sb_fill(s, a, a + 1, c, c + 1, coverage);
}

/* Raises the coverage by a capsule: the bar from a0 to a1 (16ths of a
   pixel along), c0 to c1 across (whole pixels), its ends rounded to half
   its width, sampled BLIT_SCROLLBAR_SAMPLES a side; at most peak. */
static void sb_capsule(const SbCell *s, long a0, long a1, int c0, int c1, unsigned char peak)
{
    const int n = BLIT_SCROLLBAR_SAMPLES, step = 16 / BLIT_SCROLLBAR_SAMPLES;
    long r = (long)(c1 - c0) * 8, cc = (long)(c0 + c1) * 8, s0 = a0 + r, s1 = a1 - r;
    int a, c, i, j;
    if (s1 < s0) s0 = s1 = (a0 + a1) / 2;
    for (a = 0; a < s->len; ++a)
        for (c = c0; c < c1; ++c) {
            int in = 0;
            for (j = 0; j < n; ++j)
                for (i = 0; i < n; ++i) {
                    long pa = (long)a * 16 + i * step + step / 2;
                    long pc = (long)c * 16 + j * step + step / 2;
                    long q = pa < s0 ? s0 : pa > s1 ? s1 : pa;
                    in += (pa - q) * (pa - q) + (pc - cc) * (pc - cc) <= r * r;
                }
            if (in) sb_fill(s, a, a + 1, c, c + 1, (unsigned char)((in * peak + n * n / 2) / (n * n)));
        }
}

/* The unit every style counts in: a pixel to 17 px wide, two from 18. */
static int sb_unit(int w) { int u = bp_round(w, 12); return u < 1 ? 1 : u; }

/* The classic: as thick as the cell is wide, two edge lines a unit thick,
   between them a checker of unit squares; the thumb a box on the edges,
   hollow, with three grip lines a unit apart across its middle.
   The checker's runs along the bar are an even number a cell, so its
   phase turns over at the cell's edge and the next cell carries it on at
   any length, odd ones included: unit-long runs where the cell is a
   whole number of unit pairs long (8x16, 12x24, 20x40); where it is not,
   the odd pixels go to runs toward the middle (one run a pixel longer at
   an odd length and a unit of 1), never to the runs at the cell's edges,
   so a seam looks like any other step of the checker. */
static void sb_classic(unsigned piece, const SbCell *s)
{
    int u = sb_unit(s->w), t = s->w, c0 = (s->cross - t) / 2, c1 = c0 + t, L = s->len, a, c;
    int runs = L / (2 * u) * 2 > 2 ? L / (2 * u) * 2 : 2;   /* the checker's runs along the cell */
    int g = bp_round(t, 6) > u ? bp_round(t, 6) : u;   /* the grips' margin from the edges */
    int box = piece >= SB_THUMB && piece <= SB_THUMB_END;
    /* The two edges run through every piece. */
    sb_fill(s, 0, L, c0, c0 + u, 255);
    sb_fill(s, 0, L, c1 - u, c1, 255);
    if (!box) {
        for (a = 0; a < L; ++a)
            for (c = c0 + u; c < c1 - u; ++c)
                if ((((a * runs + runs / 2) / L) + ((c - c0) / u)) & 1) sb_dot(s, a, c, 255);
        if (piece == SB_CAP_START) sb_fill(s, 0, u, c0, c1, 255);
        if (piece == SB_CAP_END) sb_fill(s, L - u, L, c0, c1, 255);
        return;
    }
    /* The box: hollow, closed at the ends it has. */
    if (piece == SB_THUMB || piece == SB_THUMB_START) sb_fill(s, 0, u, c0, c1, 255);
    if (piece == SB_THUMB || piece == SB_THUMB_END) sb_fill(s, L - u, L, c0, c1, 255);
    /* Grips where they fit: a unit clear of the box's ends in one cell,
       and anywhere in a long thumb's middle cell, which has no ends. */
    if (((piece == SB_THUMB && L >= 9 * u) || (piece == SB_THUMB_GRIP && L >= 7 * u)) &&
        t - 2 * u - 2 * g >= 2) {
        int a0 = (L - 5 * u) / 2, k;
        for (k = 0; k < 3; ++k) sb_fill(s, a0 + 2 * k * u, a0 + 2 * k * u + u, c0 + u + g, c1 - u - g, 255);
    }
}

/* The modern: a capsule about half the cell's width (an even number of
   pixels, so its sides are whole pixels), a unit short of the cell's end
   where it ends; the track the same band, faint. */
static void sb_modern(unsigned piece, const SbCell *s)
{
    int u = sb_unit(s->w), t = 2 * bp_round(s->w * 21, 100), c0, c1, L = s->len;
    long m = (long)u * 16, end = (long)L * 16, far = (long)(L + s->w) * 16;
    if (t < 2) t = 2;
    c0 = (s->cross - t) / 2;
    c1 = c0 + t;
    switch (piece) {
    case SB_CAP_START: sb_capsule(s, m, far, c0, c1, BLIT_SCROLLBAR_FAINT); break;
    case SB_TRACK: sb_fill(s, 0, L, c0, c1, BLIT_SCROLLBAR_FAINT); break;
    case SB_THUMB: sb_capsule(s, m, end - m, c0, c1, 255); break;
    case SB_THUMB_START: sb_capsule(s, m, far, c0, c1, 255); break;
    case SB_THUMB_MID:
    case SB_THUMB_GRIP: sb_fill(s, 0, L, c0, c1, 255); break;
    case SB_THUMB_END: sb_capsule(s, -far + end, end - m, c0, c1, 255); break;
    default: sb_capsule(s, -far + end, end - m, c0, c1, BLIT_SCROLLBAR_FAINT); break;
    }
}

/* The simple: the track is the light box-drawing line, as thick and where
   Blit draws U+2500 and U+2502; the thumb a solid rectangle centred on it,
   about half the cell's width, a unit short of the cell's end where it
   ends, and the caps stop there too. */
static void sb_simple(unsigned piece, const SbCell *s)
{
    int u = sb_unit(s->w), lt = (s->w + 5) / 10 > 0 ? (s->w + 5) / 10 : 1, L = s->len;
    int l0 = s->cross / 2 - lt / 2, tw = lt + 2 * bp_round(s->w - 2 * lt, 4);
    int t0 = l0 - (tw - lt) / 2;
    switch (piece) {
    case SB_CAP_START: sb_fill(s, u, L, l0, l0 + lt, 255); break;
    case SB_TRACK: sb_fill(s, 0, L, l0, l0 + lt, 255); break;
    case SB_THUMB: sb_fill(s, u, L - u, t0, t0 + tw, 255); break;
    case SB_THUMB_START: sb_fill(s, u, L, t0, t0 + tw, 255); break;
    case SB_THUMB_MID:
    case SB_THUMB_GRIP: sb_fill(s, 0, L, t0, t0 + tw, 255); break;
    case SB_THUMB_END: sb_fill(s, 0, L - u, t0, t0 + tw, 255); break;
    default: sb_fill(s, 0, L - u, l0, l0 + lt, 255); break;
    }
}

/* Draws the glyph as coverage into a w x h cell (pixels zeroed by the
   caller). Returns 0 for a scalar not in the set or a cell too small. */
static int blit_scrollbar_draw(uint32_t scalar, unsigned char *pixels, int stride, int w, int h)
{
    unsigned i, style, piece;
    SbCell s;
    if (!blit_scrollbar_glyph(scalar) || w < 4 || h < 6) return 0;
    i = scalar - BLIT_SCROLLBAR_FIRST;
    style = i / 16 * 16;
    piece = i % BLIT_SCROLLBAR_PIECES;
    s.pixels = pixels;
    s.stride = stride;
    s.w = w;
    s.h = h;
    s.vertical = i % 16 >= BLIT_SCROLLBAR_PIECES;
    s.len = s.vertical ? h : w;
    s.cross = s.vertical ? w : h;
    if (style == SB_CLASSIC) sb_classic(piece, &s);
    else if (style == SB_MODERN) sb_modern(piece, &s);
    else sb_simple(piece, &s);
    return 1;
}

#endif
