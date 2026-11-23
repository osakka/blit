/* ADR 0237 (0.3.137): the three scroll bar styles (U+1014A9-1014D8), at
   every cell size from 8 to 64 px tall (and the owner's 12x24), through
   blit_chrome_glyph/blit_chrome_draw and the raster only. The codepoints
   and the shapes are written here, not read from src/blit_scrollbar_glyphs.h,
   so a build without the styles fails a check rather than the compile.

   A style is 16 glyphs: a horizontal bar's eight pieces at base + 0..7 and
   a vertical bar's at + 8..15, in the order cap-start, track, thumb (one
   cell), thumb-start, thumb-mid, thumb-grip, thumb-end, cap-end; classic
   at U+1014A9, modern U+1014B9, simple U+1014C9. Checked:
   - each is coverage, never a fallback, has ink, and draws as the raster
     does; U+1014D9, after them, draws nothing;
   - the track and the caps join edge to edge: cap-start -> track -> track
     -> cap-end meet across each cell boundary with the same profile
     (modern, simple), or (classic) with the same two edge lines and the
     checker between them carried on: at every seam its phase turns over
     as it does inside a cell, no run across a seam longer or shorter than
     the runs inside one, and its first square where the track's is, at
     every size, odd ones included;
   - a cap is the track closed where the thumb ends in that cell: its
     first (last) inked line is the thumb's, classic's a solid edge line;
   - the thumb's cell holds the thumb alone: classic's box is hollow but
     for three grip bars across its middle (always from 12 px wide), never
     a checker square; modern's capsule lies in the track's band, clear of
     the cell's ends, solid in its middle; simple's is one solid rectangle
     over the line, clear of the cell's ends;
   - the long thumb joins: start -> mid -> grip -> mid -> end, and start ->
     end, meet with the same profile, the middle pieces run end to end;
   - simple's track is Blit's U+2500 (horizontal) and U+2502 (vertical)
     pixel for pixel, from the same raster;
   - a horizontal bar is as thick as a vertical one. */
#include "../../src/blit_raster.h"
#include "../../src/blit_control_glyphs.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "SCROLLBAR_GLYPH_FAIL line=%d size=%dx%d cp=U+%X\n", \
                                           __LINE__, w, h, cp); return 1; } } while (0)

#define FIRST 0x1014A9U
#define NEXT_FREE 0x1014D9U
enum { CLASSIC, MODERN, SIMPLE };
enum { CAP0, TRACK, THUMB, TSTART, TMID, TGRIP, TEND, CAP1, PIECES };

static unsigned char cellbuf[128 * 128], pieces[PIECES][128 * 128];
static int w, h, vertical;
static unsigned cp;

static int draw(uint32_t scalar)
{
    memset(cellbuf, 0, sizeof(cellbuf));
    return blit_chrome_draw(scalar, 0, cellbuf, w, w, h);
}

/* Along the bar (0..len-1, from the left or the top) and across it. */
static int len(void) { return vertical ? h : w; }
static int cross(void) { return vertical ? w : h; }
static int at(const unsigned char *p, int a, int c) { return vertical ? p[a * w + c] : p[c * w + a]; }

/* The unit every style counts in: a pixel to 17 px wide, two from 18. */
static int unit(void) { int u = (w + 6) / 12; return u < 1 ? 1 : u; }

/* Whether the last line across of `a` and the first of `b` are the same at
   every pixel, with some ink. */
static int joins(const unsigned char *a, const unsigned char *b)
{
    int c, ink = 0;
    for (c = 0; c < cross(); ++c) {
        if (at(a, len() - 1, c) != at(b, 0, c)) return 0;
        ink += at(b, 0, c) != 0;
    }
    return ink > 0;
}

/* The first and the last line across with ink, along; -1 if none. */
static int first_ink(const unsigned char *p)
{
    int a, c;
    for (a = 0; a < len(); ++a) for (c = 0; c < cross(); ++c) if (at(p, a, c)) return a;
    return -1;
}
static int last_ink(const unsigned char *p)
{
    int a, c;
    for (a = len() - 1; a >= 0; --a) for (c = 0; c < cross(); ++c) if (at(p, a, c)) return a;
    return -1;
}

/* The ink's extent across, over the whole cell. */
static void across_extent(const unsigned char *p, int *c0, int *c1)
{
    int a, c;
    *c0 = cross(); *c1 = -1;
    for (a = 0; a < len(); ++a) for (c = 0; c < cross(); ++c)
        if (at(p, a, c)) { if (c < *c0) *c0 = c; if (c > *c1) *c1 = c; }
}

/* Whether line `a` across is exactly `v` on [c0, c1) and 0 elsewhere. */
static int line_is(const unsigned char *p, int a, int c0, int c1, int v)
{
    int c;
    for (c = 0; c < cross(); ++c) if (at(p, a, c) != (c >= c0 && c < c1 ? v : 0)) return 0;
    return 1;
}

/* Whether p is q mirrored end for end. */
static int mirrored(const unsigned char *p, const unsigned char *q)
{
    int a, c;
    for (a = 0; a < len(); ++a) for (c = 0; c < cross(); ++c)
        if (at(p, a, c) != at(q, len() - 1 - a, c)) return 0;
    return 1;
}

/* Classic: the bar is the cell's width thick on the row's middle, two edge
   lines a unit thick; the checker between them. strip_at reads a strip of
   cap-start, track, track, cap-end, laid end to end. */
static int strip_at(int a, int c) { return at(pieces[a / len() == 0 ? CAP0 : a / len() == 3 ? CAP1 : TRACK], a % len(), c); }

static int classic(void)
{
    int u = unit(), t = w, c0 = (cross() - t) / 2, c1 = c0 + t, L = len(), a, c, p, i;
    int min_in = 1 << 20, max_in = 0;
    /* Every piece: both edge lines solid along the whole cell, nothing
       outside the bar. */
    for (p = 0; p < PIECES; ++p) {
        cp = FIRST + (vertical ? 8U : 0U) + (unsigned)p;
        for (a = 0; a < L; ++a) for (c = 0; c < cross(); ++c) {
            int v = at(pieces[p], a, c);
            if (c < c0 || c >= c1) CHECK(v == 0);
            else if (c < c0 + u || c >= c1 - u) CHECK(v == 255);
            else CHECK(v == 0 || v == 255);
        }
    }
    /* The track's checker: its first square where it always is, the first
       pixel inside the top (left) edge, at the cell's start. */
    cp = FIRST + (vertical ? 8U : 0U) + TRACK;
    CHECK(c1 - c0 - 2 * u >= 1 && at(pieces[TRACK], 0, c0 + u) == 255);
    /* Each line along, in the checker, is runs of on and off; inside a
       cell they are as long as each other give or take, and across a seam
       the phase turns over and the run is no longer or shorter than one
       inside a cell. A line across (a fixed) has both on and off. */
    for (c = c0 + u; c < c1 - u; ++c) {
        int start = u, run_start = u, v = strip_at(u, c);
        for (a = u + 1; a <= 4 * L - u; ++a) {
            int nv = a < 4 * L - u ? strip_at(a, c) : -1;
            if (nv == v) continue;
            /* A run [run_start, a): inside one cell, or across a seam. */
            if (run_start / L == (a - 1) / L && run_start > start && a < 4 * L - u) {
                if (a - run_start < min_in) min_in = a - run_start;
                if (a - run_start > max_in) max_in = a - run_start;
            }
            run_start = a;
            v = nv;
        }
    }
    CHECK(min_in >= u && max_in >= min_in);
    for (c = c0 + u; c < c1 - u; ++c) {
        int run_start = u, v = strip_at(u, c);
        for (i = 1; i < 4; ++i) {
            cp = FIRST + (vertical ? 8U : 0U) + (i == 3 ? (unsigned)CAP1 : (unsigned)TRACK);
            CHECK(strip_at(i * L - 1, c) != strip_at(i * L, c));
        }
        for (a = u + 1; a <= 4 * L - u; ++a) {
            int nv = a < 4 * L - u ? strip_at(a, c) : -1;
            if (nv == v) continue;
            if (run_start / L != (a - 1) / L || a == 4 * L - u || run_start == u) {
                /* A run that touches a seam (or an end): no longer than a
                   run inside, no shorter (but at the bar's closed ends). */
                cp = FIRST + (vertical ? 8U : 0U) + TRACK;
                CHECK(a - run_start <= max_in);
                if (run_start != u && a != 4 * L - u) CHECK(a - run_start >= min_in);
            }
            run_start = a;
            v = nv;
        }
    }
    for (a = 0; a < L; ++a) {
        int on = 0, off = 0;
        for (c = c0 + u; c < c1 - u; ++c) { on += at(pieces[TRACK], a, c) != 0; off += at(pieces[TRACK], a, c) == 0; }
        cp = FIRST + (vertical ? 8U : 0U) + TRACK;
        if (c1 - c0 - 2 * u >= 2 * u) CHECK(on > 0 && off > 0);
    }
    /* The caps: the track, closed by a solid line a unit thick at the end. */
    for (a = 0; a < L; ++a) for (c = c0; c < c1; ++c) {
        cp = FIRST + (vertical ? 8U : 0U) + CAP0;
        CHECK(a < u ? at(pieces[CAP0], a, c) == 255 : at(pieces[CAP0], a, c) == at(pieces[TRACK], a, c));
        cp = FIRST + (vertical ? 8U : 0U) + CAP1;
        CHECK(a >= L - u ? at(pieces[CAP1], a, c) == 255 : at(pieces[CAP1], a, c) == at(pieces[TRACK], a, c));
    }
    /* The thumb's pieces: a box on the edges, closed at the ends it has,
       hollow inside but for grip bars, never a checker square. A grip bar
       is a line across, solid and centred, clear of the edges. */
    for (p = THUMB; p <= TEND; ++p) {
        int has0 = p == THUMB || p == TSTART, has1 = p == THUMB || p == TEND, bars = 0, lines = 0, prev = 0;
        int g0 = -1, g1 = -1, first = -1, last = -1;
        cp = FIRST + (vertical ? 8U : 0U) + (unsigned)p;
        for (a = 0; a < L; ++a) {
            int closed = (has0 && a < u) || (has1 && a >= L - u), x0 = -1, x1 = -1, n = 0;
            if (closed) { CHECK(line_is(pieces[p], a, c0, c1, 255)); prev = 0; continue; }
            for (c = c0 + u; c < c1 - u; ++c)
                if (at(pieces[p], a, c)) { if (x0 < 0) x0 = c; x1 = c; ++n; }
            if (!n) { prev = 0; continue; }
            CHECK(p == THUMB || p == TGRIP);
            CHECK(n == x1 - x0 + 1 && x0 > c0 + u && x1 < c1 - u - 1 && (x0 - c0) == (c1 - 1 - x1));
            if (g0 < 0) { g0 = x0; g1 = x1; }
            CHECK(x0 == g0 && x1 == g1);
            if (!prev) ++bars;
            if (first < 0) first = a;
            last = a;
            ++lines;
            prev = 1;
        }
        CHECK(bars == 0 || (bars == 3 && lines == 3 * u && last - first + 1 == 5 * u));
        if (bars) CHECK(abs((first + last + 1) - L) <= 1);
        if ((p == THUMB || p == TGRIP) && w >= 12) CHECK(bars == 3);
    }
    return 0;
}

/* Modern: a band about half the cell's width, an even number of pixels,
   on the middle; the track faint, the thumb a capsule. */
static int modern(void)
{
    int L = len(), c0, c1, a, c, p, faint, mid = L / 2;
    across_extent(pieces[TRACK], &c0, &c1);
    ++c1;
    cp = FIRST + 16U + (vertical ? 8U : 0U) + TRACK;
    faint = at(pieces[TRACK], 0, c0);
    CHECK(c0 >= 0 && (c1 - c0) % 2 == 0 && c1 - c0 >= 2 && c1 - c0 <= (w * 2 + 2) / 3 && 3 * (c1 - c0) >= w - 1);
    CHECK(c0 == (cross() - (c1 - c0)) / 2);
    CHECK(faint > 0 && faint < 128);
    for (a = 0; a < L; ++a) CHECK(line_is(pieces[TRACK], a, c0, c1, faint));
    for (p = 0; p < PIECES; ++p) {
        cp = FIRST + 16U + (vertical ? 8U : 0U) + (unsigned)p;
        for (a = 0; a < L; ++a) for (c = 0; c < cross(); ++c)
            if (c < c0 || c >= c1) CHECK(at(pieces[p], a, c) == 0);
        if (p == CAP0 || p == CAP1)
            for (a = 0; a < L; ++a) for (c = c0; c < c1; ++c) CHECK(at(pieces[p], a, c) <= faint);
    }
    /* The thumb alone: clear of the cell's ends (no track there), solid in
       its middle (at the smallest cells a dot, its middle the fullest), the
       same both ways. */
    cp = FIRST + 16U + (vertical ? 8U : 0U) + THUMB;
    CHECK(first_ink(pieces[THUMB]) > 0 && last_ink(pieces[THUMB]) < L - 1);
    if (L - 2 * unit() > c1 - c0)   /* long enough to have straight sides */
        CHECK(at(pieces[THUMB], mid, (c0 + c1) / 2) == 255 && at(pieces[THUMB], mid, (c0 + c1 - 1) / 2) == 255);
    else   /* a dot: its middle the most ink there is */
        for (a = 0; a < L; ++a) for (c = c0; c < c1; ++c)
            CHECK(at(pieces[THUMB], a, c) <= at(pieces[THUMB], mid, (c0 + c1) / 2) &&
                  at(pieces[THUMB], mid, (c0 + c1) / 2) > 128);
    CHECK(mirrored(pieces[THUMB], pieces[THUMB]));
    /* The long thumb: its middle pieces the band, solid. */
    for (p = TMID; p <= TGRIP; ++p) {
        cp = FIRST + 16U + (vertical ? 8U : 0U) + (unsigned)p;
        for (a = 0; a < L; ++a) CHECK(line_is(pieces[p], a, c0, c1, 255));
    }
    cp = FIRST + 16U + (vertical ? 8U : 0U) + TSTART;
    CHECK(first_ink(pieces[TSTART]) > 0 && line_is(pieces[TSTART], L - 1, c0, c1, 255));
    cp = FIRST + 16U + (vertical ? 8U : 0U) + TEND;
    CHECK(mirrored(pieces[TEND], pieces[TSTART]));
    cp = FIRST + 16U + (vertical ? 8U : 0U) + CAP1;
    CHECK(mirrored(pieces[CAP1], pieces[CAP0]));
    return 0;
}

/* Simple: the track is Blit's light box-drawing line; the thumb one solid
   rectangle over it. */
static int simple(BlitRaster *r)
{
    BlitRasterGlyph g;
    int L = len(), a, c, p, x0 = 1 << 20, x1 = -1, y0 = 1 << 20, y1 = -1, l0, l1;
    cp = FIRST + 32U + (vertical ? 8U : 0U) + TRACK;
    memset(&g, 0, sizeof(g));
    CHECK(blit_raster_get(r, vertical ? 0x2502 : 0x2500, 1, 0, &g) && !g.fallback &&
          g.format == BLIT_RASTER_COVERAGE && g.width == w && g.height == h);
    for (a = 0; a < h; ++a) for (c = 0; c < w; ++c) CHECK(pieces[TRACK][a * w + c] == g.pixels[a * g.stride + c]);
    across_extent(pieces[TRACK], &l0, &l1);
    ++l1;
    for (a = 0; a < L; ++a) CHECK(line_is(pieces[TRACK], a, l0, l1, 255));
    /* The one-cell thumb: a solid rectangle over the line, clear of the
       cell's ends, the same both ways, nothing else. */
    cp = FIRST + 32U + (vertical ? 8U : 0U) + THUMB;
    for (a = 0; a < L; ++a) for (c = 0; c < cross(); ++c)
        if (at(pieces[THUMB], a, c)) {
            if (a < y0) y0 = a;
            if (a > y1) y1 = a;
            if (c < x0) x0 = c;
            if (c > x1) x1 = c;
        }
    CHECK(y0 > 0 && y1 < L - 1 && x0 <= l0 && x1 >= l1 - 1 && (l0 - x0) == (x1 + 1 - l1));
    for (a = 0; a < L; ++a) CHECK(a >= y0 && a <= y1 ? line_is(pieces[THUMB], a, x0, x1 + 1, 255)
                                                      : line_is(pieces[THUMB], a, 0, 0, 0));
    CHECK(mirrored(pieces[THUMB], pieces[THUMB]));
    /* The long thumb: the same rectangle across; start and end closed
       where the one-cell thumb is, the middle end to end. */
    for (p = TSTART; p <= TEND; ++p) {
        cp = FIRST + 32U + (vertical ? 8U : 0U) + (unsigned)p;
        for (a = 0; a < L; ++a) {
            int in = (p != TSTART || a >= y0) && (p != TEND || a <= y1);
            CHECK(in ? line_is(pieces[p], a, x0, x1 + 1, 255) : line_is(pieces[p], a, 0, 0, 0));
        }
    }
    /* The caps: the line from where the thumb ends. */
    for (a = 0; a < L; ++a) {
        cp = FIRST + 32U + (vertical ? 8U : 0U) + CAP0;
        CHECK(a >= y0 ? line_is(pieces[CAP0], a, l0, l1, 255) : line_is(pieces[CAP0], a, 0, 0, 0));
        cp = FIRST + 32U + (vertical ? 8U : 0U) + CAP1;
        CHECK(a <= y1 ? line_is(pieces[CAP1], a, l0, l1, 255) : line_is(pieces[CAP1], a, 0, 0, 0));
    }
    return 0;
}

int main(int argc, char **argv)
{
    BlitFont font;
    int sizes[60][2], n = 0, s, style, thick[3][2];
    if (argc != 4) return 2;
    for (h = 8; h <= 64; ++h) { sizes[n][0] = (h + 1) / 2; sizes[n][1] = h; ++n; }
    sizes[n][0] = 12; sizes[n][1] = 24; ++n;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1])) { fprintf(stderr, "SCROLLBAR_GLYPH_FAIL font\n"); return 1; }
    for (s = 0; s < n; ++s) {
        BlitRaster *r;
        w = sizes[s][0]; h = sizes[s][1]; cp = 0;
        r = blit_raster_open(&font, argv[2], argv[3], w, h);
        CHECK(r != NULL);
        for (style = CLASSIC; style <= SIMPLE; ++style)
            for (vertical = 0; vertical < 2; ++vertical) {
                unsigned base = FIRST + 16U * (unsigned)style + (vertical ? 8U : 0U);
                int p, x, y, c0, c1;
                /* Every piece: through the raster, coverage, no fallback,
                   ink; drawn directly the same. */
                for (p = 0; p < PIECES; ++p) {
                    BlitRasterGlyph g;
                    int ink = 0;
                    cp = base + (unsigned)p;
                    memset(&g, 0, sizeof(g));
                    CHECK(blit_chrome_glyph(cp) && blit_raster_get(r, cp, 1, 0, &g));
                    CHECK(!g.fallback && g.format == BLIT_RASTER_COVERAGE && g.width == w && g.height == h);
                    CHECK(draw(cp));
                    for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
                        CHECK(cellbuf[y * w + x] == g.pixels[y * g.stride + x]);
                        ink += cellbuf[y * w + x] != 0;
                    }
                    CHECK(ink > 0);
                    memcpy(pieces[p], cellbuf, (size_t)(w * h));
                }
                /* Edge to edge: the track and the caps (the classic's
                   checker is held below, where the profiles differ at a
                   seam by design), and the long thumb. */
                cp = base + TRACK;
                if (style != CLASSIC) {
                    CHECK(joins(pieces[CAP0], pieces[TRACK]));
                    CHECK(joins(pieces[TRACK], pieces[TRACK]));
                    CHECK(joins(pieces[TRACK], pieces[CAP1]));
                }
                cp = base + TMID;
                CHECK(joins(pieces[TSTART], pieces[TMID]));
                CHECK(joins(pieces[TMID], pieces[TMID]));
                CHECK(joins(pieces[TMID], pieces[TGRIP]));
                CHECK(joins(pieces[TGRIP], pieces[TMID]));
                CHECK(joins(pieces[TMID], pieces[TEND]));
                CHECK(joins(pieces[TSTART], pieces[TEND]));
                CHECK(joins(pieces[TSTART], pieces[TGRIP]) && joins(pieces[TGRIP], pieces[TEND]));
                /* A cap ends where the thumb does in that cell. */
                cp = base + CAP0;
                CHECK(first_ink(pieces[CAP0]) == first_ink(pieces[THUMB]) &&
                      first_ink(pieces[TSTART]) == first_ink(pieces[THUMB]));
                cp = base + CAP1;
                CHECK(last_ink(pieces[CAP1]) == last_ink(pieces[THUMB]) &&
                      last_ink(pieces[TEND]) == last_ink(pieces[THUMB]));
                cp = base;
                if (style == CLASSIC) CHECK(!classic());
                else if (style == MODERN) CHECK(!modern());
                else CHECK(!simple(r));
                across_extent(pieces[TRACK], &c0, &c1);
                thick[style][vertical] = c1 - c0 + 1;
            }
        /* A horizontal bar is as thick as a vertical one. */
        for (style = CLASSIC; style <= SIMPLE; ++style) {
            cp = FIRST + 16U * (unsigned)style + TRACK;
            CHECK(thick[style][0] == thick[style][1]);
        }
        /* Nothing after the styles. */
        {
            BlitRasterGlyph g;
            cp = NEXT_FREE;
            CHECK(!blit_chrome_glyph(cp) && !draw(cp));
            memset(&g, 0, sizeof(g));
            CHECK(!blit_raster_get(r, cp, 1, 0, &g) || g.fallback);
        }
        blit_raster_close(r);
    }
    blit_font_free(&font);
    printf("SCROLLBAR_GLYPH_PASS glyphs=48 styles=classic+modern+simple sizes=%d raster=coverage_no_fallback "
           "joins=track,caps,long_thumb checker=phase_continuous_every_size caps=end_with_thumb "
           "thumb=alone simple_track=U+2500/U+2502_exact thickness=h_equals_v U+%X_none\n", n, NEXT_FREE);
    return 0;
}
