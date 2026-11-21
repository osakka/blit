/* ADR 0241: Arabic runs shaped as words, drawn as slices, through the
   raster's own word cache. Lines of cells on stdin ("hex,hex;hex;..."),
   visual order. For each text font given, every cell height 8-64:
   - each run's slices, drawn cell by cell, compose exactly the whole word;
   - no ink dropped, none beyond one row above or below;
   - a word with tashkeel has more ink than the same word without its marks;
   - bold is the overstrike of the whole word;
   - a word with a scalar the face lacks (a Latin acute on a beh) is refused,
     drawn per cell;
   - negative control: the word differs from its cells shaped one by one.
   With BLIT_WORD_PICTURE=path, writes a PGM of the lines at 22 px with the
   first font. */
#include "../../src/blit_raster.h"
#include "../../src/blit_cell_word.h"
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define MAX_LINES 16
#define MAX_COLS 64
static BlitCellCluster cells[MAX_LINES][MAX_COLS];
static int filled[MAX_LINES][MAX_COLS], ncols[MAX_LINES], nlines;

static unsigned char over(unsigned char under, unsigned in)
{ return (unsigned char)(in + (under * (255 - in) + 127) / 255); }

static void put(unsigned char *plane, int pw, int ph, const BlitRasterGlyph *g, int x, int y)
{
    int r, c;
    for (r = 0; r < g->height; ++r)
        for (c = 0; c < g->width; ++c) {
            int px = x + c + g->ink_offset_x, py = y + r + g->ink_offset_y;
            if (px >= 0 && px < pw && py >= 0 && py < ph)
                plane[py * pw + px] = over(plane[py * pw + px], g->pixels[r * g->stride + c]);
        }
}

static long ink(const BlitRasterGlyph *g)
{
    long sum = 0; int r, c;
    for (r = 0; r < g->height; ++r) for (c = 0; c < g->width; ++c) sum += g->pixels[r * g->stride + c];
    return sum;
}

static int has_missing(const BlitCellWord *w)
{
    int i; size_t m;
    for (i = 0; i < w->count; ++i)
        for (m = 0; m < w->cells[i].length; ++m) if (w->cells[i].scalars[m] == 0x0301) return 1;
    return 0;
}

int main(int argc, char **argv)
{
    char line[16384];
    int f, failures = 0;
    long words = 0, slices = 0, marked = 0, refused = 0, joined = 0;
    unsigned long long dropped = 0;
    const char *picture = getenv("BLIT_WORD_PICTURE");
    BlitFont fallback;
    if (argc < 5) return 2;
    while (nlines < MAX_LINES && fgets(line, sizeof(line), stdin)) {
        char *save, *spec; int c = 0;
        line[strcspn(line, "\n")] = 0;
        for (spec = strtok_r(line, ";", &save); spec && c < MAX_COLS; spec = strtok_r(NULL, ";", &save), ++c) {
            uint32_t s[BLIT_CLUSTER_MAX_SCALARS]; int n = 0; char *s2, *t;
            for (t = strtok_r(spec, ",", &s2); t && n < BLIT_CLUSTER_MAX_SCALARS; t = strtok_r(NULL, ",", &s2))
                s[n++] = (uint32_t)strtoul(t, NULL, 16);
            filled[nlines][c] = n && s[0] != 0x20 &&
                blit_cell_cluster_init(&cells[nlines][c], s[0], s + 1, (size_t)(n - 1), 1);
        }
        ncols[nlines++] = c;
    }
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 2;
    for (f = 4; f < argc; ++f) {
        int h;
        for (h = 8; h <= 64; ++h) {
            int w = (h + 1) / 2, l;
            /* "pixel": the pixel font's own raster (ADR 0199), which borrows
               the stack and takes the word face like any other. */
            BlitRaster *r = strcmp(argv[f], "pixel") ? blit_raster_open(&fallback, argv[f], argv[2], w, h)
                                                    : blit_raster_open_pixel(&fallback, argv[2], w, h);
            unsigned char *plane = NULL;
            int pw = (MAX_COLS + 2) * w, ph = (MAX_LINES * 2 + 2) * h;
            if (!r || !blit_raster_set_arabic_words(r, argv[3])) { fprintf(stderr, "open %s %d\n", argv[f], h); return 2; }
            if (picture && f == 4 && h == 22) plane = calloc((size_t)pw * ph, 1);
            for (l = 0; l < nlines; ++l) {
                const BlitCellCluster *row[MAX_COLS];
                BlitWordRun runs[MAX_COLS];
                int c, n, k;
                for (c = 0; c < ncols[l]; ++c) row[c] = filled[l][c] ? &cells[l][c] : NULL;
                n = blit_cell_word_runs(row, ncols[l], runs, MAX_COLS);
                for (k = 0; k < n; ++k) {
                    BlitCellWord word, bare;
                    BlitRasterGlyph whole, bold, slice;
                    unsigned char *a, *b;
                    int i, x, y, W = runs[k].count * w, cut = 0;
                    memset(&word, 0, sizeof(word));
                    word.kind = runs[k].kind; word.count = runs[k].count;
                    memcpy(word.cells, &cells[l][runs[k].start], (size_t)word.count * sizeof(BlitCellCluster));
                    ++words;
                    if (has_missing(&word)) {
                        if (blit_raster_get_word(r, &word, -1, 0, &whole, NULL)) {
                            if (failures++ < 8) fprintf(stderr, "missing scalar not refused %s h=%d\n", argv[f], h);
                        } else ++refused;
                        continue;
                    }
                    if (!blit_raster_get_word(r, &word, -1, 0, &whole, NULL)) {
                        if (failures++ < 8) fprintf(stderr, "word refused %s h=%d line %d run %d\n", argv[f], h, l, k);
                        continue;
                    }
                    /* the canvas stays within a row above and below */
                    if (whole.height && (whole.ink_offset_y < -h || whole.ink_offset_y + whole.height > 2 * h)) {
                        if (failures++ < 8) fprintf(stderr, "overhang %s h=%d top=%d height=%d\n", argv[f], h, whole.ink_offset_y, whole.height);
                    }
                    /* slices compose the whole word */
                    a = calloc((size_t)W * 3 * h, 1); b = calloc((size_t)W * 3 * h, 1);
                    put(a, W, 3 * h, &whole, 0, h);
                    for (i = 0; i < word.count; ++i) {
                        if (!blit_raster_get_word(r, &word, i, 0, &slice, NULL) || slice.width != w) { ++failures; break; }
                        put(b, W, 3 * h, &slice, i * w, h);
                        ++slices;
                    }
                    if (memcmp(a, b, (size_t)W * 3 * h)) {
                        if (failures++ < 8) fprintf(stderr, "slices differ %s h=%d line %d run %d\n", argv[f], h, l, k);
                    }
                    /* bold is the overstrike of the whole word */
                    if (!blit_raster_get_word(r, &word, -1, 0, &whole, NULL) ||
                        !blit_raster_get_word(r, &word, -1, 1, &bold, NULL)) ++failures;
                    else
                        for (y = 0; y < whole.height; ++y)
                            for (x = 0; x < whole.width; ++x) {
                                unsigned char want = whole.pixels[y * whole.stride + x];
                                if (x) want = over(want, whole.pixels[y * whole.stride + x - 1]);
                                if (bold.pixels[y * bold.stride + x] != want) { cut = 1; }
                            }
                    if (cut && failures++ < 8) fprintf(stderr, "bold differs %s h=%d\n", argv[f], h);
                    if (plane) {
                        blit_raster_get_word(r, &word, -1, 0, &whole, NULL);
                        put(plane, pw, ph, &whole, (runs[k].start + 1) * w, (2 * l + 1) * h);
                    }
                    /* tashkeel: the marks add ink */
                    bare = word;
                    for (i = 0; i < bare.count; ++i) {
                        BlitCellCluster one;
                        if (blit_cell_cluster_init(&one, bare.cells[i].scalars[0], NULL, 0, 1)) bare.cells[i] = one;
                    }
                    if (!blit_cell_word_equal(&bare, &word) && word.kind == BLIT_WORD_LETTERS) {
                        BlitRasterGlyph plain; long with_marks;
                        blit_raster_get_word(r, &word, -1, 0, &whole, NULL);
                        with_marks = ink(&whole);
                        if (!blit_raster_get_word(r, &bare, -1, 0, &plain, NULL) || ink(&plain) >= with_marks) {
                            if (failures++ < 8) fprintf(stderr, "tashkeel lost %s h=%d line %d\n", argv[f], h, l);
                        } else ++marked;
                    }
                    /* negative control: shaped as a word, not cell by cell */
                    if (word.kind == BLIT_WORD_LETTERS && word.count > 1) {
                        unsigned char *cellwise = calloc((size_t)W * 3 * h, 1);
                        for (i = 0; i < word.count; ++i) {
                            BlitCellWord one; BlitRasterGlyph g;
                            memset(&one, 0, sizeof(one));
                            one.kind = word.kind; one.count = 1; one.cells[0] = word.cells[i];
                            if (blit_raster_get_word(r, &one, -1, 0, &g, NULL)) put(cellwise, W, 3 * h, &g, i * w, h);
                        }
                        if (memcmp(a, cellwise, (size_t)W * 3 * h)) ++joined;
                        free(cellwise);
                    }
                    free(a); free(b);
                }
            }
            {
                unsigned long long d;
                blit_raster_word_counts(r, NULL, NULL, NULL, &d);
                dropped += d;
            }
            if (plane) {
                FILE *o = fopen(picture, "wb");
                if (o) { fprintf(o, "P5\n%d %d\n255\n", pw, ph); fwrite(plane, 1, (size_t)pw * ph, o); fclose(o); }
                free(plane);
            }
            blit_raster_close(r);
        }
    }
    blit_font_free(&fallback);
    if (dropped) { fprintf(stderr, "ink dropped: %llu\n", dropped); ++failures; }
    printf("ARABIC_WORD_SLICE words=%ld slices=%ld tashkeel=%ld refused=%ld joined=%ld dropped=%llu failed=%d\n",
           words, slices, marked, refused, joined, dropped, failures);
    return failures || !marked || !refused || !joined ? 1 : 0;
}
