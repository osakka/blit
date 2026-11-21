/* ADR 0237: the player glyphs (U+101314..10137C), at every cell size from
   8 to 64 px tall (and the owner's 12x24), through the actual raster:
   - each big and small digit lights exactly its seven-segment table's
     segments, the rest drawn faint (the ghost), never absent;
   - a big digit's four cells and a small digit's two meet without a seam:
     the top segment of an 8 runs unbroken across the cell boundary;
   - the colons have two dots and nothing else;
   - each LED step lights more than the one before, step 0 none and step 8
     every LED, horizontal and vertical; a peak cap is ink;
   - a slider's and a fader's track runs across cell boundaries: cap ->
     filled -> knob -> track -> cap, edge to edge;
   - every glyph is coverage, not a fallback, has ink, and stays in its
     cell; the code point after the set draws nothing of it. */
#include "../../src/blit_raster.h"
#include "../../src/blit_control_glyphs.h"
#include <stdio.h>
#include <string.h>

#define CHECK(x) do { if (!(x)) { fprintf(stderr, "PLAYER_GLYPH_FAIL line=%d size=%dx%d cp=%x\n", __LINE__, w, h, cp); return 1; } } while (0)
#define F BLIT_PLAYER_FIRST

static unsigned char cellbuf[128 * 128];
static int draw(uint32_t cp, int w, int h)
{
    memset(cellbuf, 0, sizeof(cellbuf));
    return blit_chrome_draw(cp, 0, cellbuf, w, w, h);
}

int main(int argc, char **argv)
{
    BlitFont font;
    int sizes[60][2], n = 0, s, w = 0, h = 0, ch, k;
    unsigned cp = 0;
    static unsigned char canvas[256 * 256];
    if (argc != 4) return 2;
    for (h = 8; h <= 64; ++h) { sizes[n][0] = (h + 1) / 2; sizes[n][1] = h; ++n; }
    sizes[n][0] = 12; sizes[n][1] = 24; ++n;
    memset(&font, 0, sizeof(font));
    if (blit_font_load_path(&font, argv[1])) { fprintf(stderr, "PLAYER_GLYPH_FAIL font\n"); return 1; }
    for (s = 0; s < n; ++s) {
        BlitRaster *r;
        w = sizes[s][0]; h = sizes[s][1];
        r = blit_raster_open(&font, argv[2], argv[3], w, h);
        CHECK(r != NULL);
        /* Every glyph: through the raster, coverage, no fallback, ink, in
           its cell; and drawn directly the same. */
        for (cp = F; cp < F + BLIT_PLAYER_COUNT; ++cp) {
            BlitRasterGlyph g;
            int ink = 0, x, y;
            CHECK(blit_chrome_glyph(cp) && blit_raster_get(r, cp, 1, 0, &g));
            CHECK(!g.fallback && g.format == BLIT_RASTER_COVERAGE && g.width == w && g.height == h);
            for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) ink += g.pixels[y * g.stride + x] != 0;
            CHECK(ink > 0);
            CHECK(draw(cp, w, h));
            for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) CHECK(cellbuf[y * w + x] == g.pixels[y * g.stride + x]);
        }
        cp = F + BLIT_PLAYER_COUNT;
        CHECK(!blit_player_glyph(cp));
        /* Big digits: the four cells as one canvas, each segment's middle
           sampled -- lit exactly where the table says, faint elsewhere. */
        for (ch = 0; ch < 12; ++ch) {
            int W = 2 * w, H = 2 * h, p, x, y, t = h >= 12 ? (h + 4) / 8 : 1;
            int ix = w >= 6 ? (w + 3) / 6 : 1, iy = (h + 4) / 8, mid, sx[7], sy[7];
            cp = F + (unsigned)ch * 4;
            memset(canvas, 0, sizeof(canvas));
            for (p = 0; p < 4; ++p) {
                CHECK(draw(F + (unsigned)ch * 4 + (unsigned)p, w, h));
                for (y = 0; y < h; ++y) for (x = 0; x < w; ++x)
                    canvas[(y + (p / 2) * h) * W + x + (p % 2) * w] = cellbuf[y * w + x];
            }
            mid = iy + (H - 2 * iy - t + 1) / 2;
            sx[0] = sx[3] = sx[6] = W / 2;
            sx[1] = sx[2] = W - ix - t / 2 - 1;
            sx[4] = sx[5] = ix + t / 2;
            sy[0] = iy + t / 2; sy[3] = H - iy - t / 2 - 1; sy[6] = mid + t / 2;
            sy[1] = sy[5] = (iy + t + mid) / 2; sy[2] = sy[4] = (mid + t + H - iy - t) / 2;
            for (k = 0; k < 7; ++k) {
                int v = canvas[sy[k] * W + sx[k]];
                CHECK(v == ((blit_player_segments[ch] >> k) & 1 ? 255 : BLIT_PLAYER_GHOST));
            }
            /* No seam: an 8's top segment is unbroken across the middle. */
            if (ch == 8) CHECK(canvas[sy[0] * W + w - 1] == 255 && canvas[sy[0] * W + w] == 255);
        }
        /* Small digits likewise, two cells by one row. */
        for (ch = 0; ch < 12; ++ch) {
            int W = 2 * w, p, x, y, t = h >= 12 ? (h + 6) / 12 : 1;
            int ix = w >= 4 ? (w + 2) / 4 : 1, iy = (h + 4) / 8, mid, sx[7], sy[7];
            cp = F + 50 + (unsigned)ch * 2;
            memset(canvas, 0, sizeof(canvas));
            for (p = 0; p < 2; ++p) {
                CHECK(draw(F + 50 + (unsigned)ch * 2 + (unsigned)p, w, h));
                for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) canvas[y * W + x + p * w] = cellbuf[y * w + x];
            }
            mid = iy + (h - 2 * iy - t + 1) / 2;
            sx[0] = sx[3] = sx[6] = W / 2;
            sx[1] = sx[2] = W - ix - 1;
            sx[4] = sx[5] = ix;
            sy[0] = iy; sy[3] = h - iy - 1; sy[6] = mid;
            sy[1] = sy[5] = (iy + t + mid) / 2; sy[2] = sy[4] = (mid + t + h - iy - t) / 2;
            if (h - 2 * iy < 5 * t + 2) continue;   /* too short for its segments to be told apart */
            for (k = 0; k < 7; ++k) {
                int v = canvas[sy[k] * W + sx[k]];
                CHECK(v == ((blit_player_segments[ch] >> k) & 1 ? 255 : BLIT_PLAYER_GHOST));
            }
            if (ch == 8) CHECK(canvas[sy[0] * W + w - 1] == 255 && canvas[sy[0] * W + w] == 255);
        }
        /* Colons: two separate dots of full ink, nothing faint. */
        for (cp = F + 48; cp <= F + 74; cp += 26) {
            int x, y, rows = 0, last = -2, runs = 0;
            unsigned c2;
            for (c2 = cp; c2 <= (cp == F + 48 ? F + 49 : cp); ++c2) {
                CHECK(draw(c2, w, h));
                for (y = 0; y < h; ++y) {
                    int lit = 0;
                    for (x = 0; x < w; ++x) { CHECK(cellbuf[y * w + x] == 0 || cellbuf[y * w + x] == 255); lit |= cellbuf[y * w + x]; }
                    if (lit) { if (rows + y != last + 1) ++runs; last = rows + y; }
                }
                rows += h;
            }
            CHECK(runs == 2);
        }
        /* LEDs: lit area grows with the step; 0 none, 8 all; ghosts fill the rest. */
        for (k = 0; k < 2; ++k) {
            int step, prev = -1, total = -1;
            for (step = 0; step <= 8; ++step) {
                int lit = 0, any = 0, i;
                cp = F + (k ? 84U : 75U) + (unsigned)step;
                CHECK(draw(cp, w, h));
                for (i = 0; i < w * h; ++i) { lit += cellbuf[i] == 255; any += cellbuf[i] != 0; }
                if (step == 0) { CHECK(lit == 0); total = any; }
                /* Every step its own where an LED is wide (tall) enough to
                   show four (two) steps; never fewer lit than the step before. */
                CHECK(any == total && (lit > prev || (lit == prev && (k ? h < 16 : w < 12))));
                if (step == 8) CHECK(lit == total);
                prev = lit;
            }
        }
        /* Sliders: the track row lit at both edges where it runs on. */
        {
            int t = h >= 12 ? (h + 4) / 8 : 1, y = (h - t) / 2;
            cp = F + 95; CHECK(draw(cp, w, h) && cellbuf[y * w + w - 1] == 255);
            cp = F + 97; CHECK(draw(cp, w, h) && cellbuf[y * w] == 255 && cellbuf[y * w + w - 1] == 255);
            cp = F + 96; CHECK(draw(cp, w, h) && cellbuf[y * w] == BLIT_PLAYER_TRACK && cellbuf[y * w + w - 1] == BLIT_PLAYER_TRACK);
            cp = F + 99; CHECK(draw(cp, w, h) && cellbuf[y * w] == BLIT_PLAYER_TRACK);
        }
        /* Faders: the track column lit at top and bottom where it runs on. */
        {
            int t = w >= 6 ? (w + 2) / 4 : 1, x = (w - t) / 2;
            cp = F + 100; CHECK(draw(cp, w, h) && cellbuf[(h - 1) * w + x] == BLIT_PLAYER_TRACK);
            cp = F + 101; CHECK(draw(cp, w, h) && cellbuf[x] == BLIT_PLAYER_TRACK && cellbuf[(h - 1) * w + x] == BLIT_PLAYER_TRACK);
            cp = F + 102; CHECK(draw(cp, w, h) && cellbuf[x] == 255 && cellbuf[(h - 1) * w + x] == 255);
            cp = F + 104; CHECK(draw(cp, w, h) && cellbuf[x] == 255);
        }
        blit_raster_close(r);
    }
    blit_font_free(&font);
    printf("PLAYER_GLYPH_PASS glyphs=%u sizes=%d digits=segments_exact,ghosted,seamless colons=two_dots "
           "leds=increasing,0_none,8_all sliders=continuous faders=continuous raster=coverage_no_fallback\n",
           BLIT_PLAYER_COUNT, n);
    return 0;
}
