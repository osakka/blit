/* Icon clip sweep (ADR 0213 candidate): every fitted glyph through the
   production raster, at every cell size, must lose no ink.

   This file is not compiled alone. icon_clip_gate.rb appends it to a copy of
   src/blit_raster.c in which one probe call is inserted into draw_fitted
   just before it copies the placed bitmap into the glyph (left, top), so that the
   harness sees draw_fitted's statics and the position of the real bitmap
   against the real cell. The probe records, for the last render a glyph's
   draw made, the ink that falls outside the cell.

   A glyph that is one column of a spanning icon (ADR 0197) is the whole icon
   drawn shifted by its column, so its overhang toward a neighbouring column
   is the design, not a loss: only the span's outer edges and the cell's top
   and bottom count.

   Input: the spec file lists "KIND CODEPOINT COLUMNS COLUMN LABEL" lines
   (KIND is a class name) and the sweep cells. Enumerated classes ("nerd",
   "symbols2", "symbols", "math", "unifont", "unifont-upper") take the codepoints
   of that face's charmap at a stride.
   Output: one line per glyph that loses ink or is not drawn:
     BAD KIND U+CP LABEL WxH lost=N sides=LRTB fallback=F
   then "SWEPT n". */
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct {
    int calls, w, h, left, top, bw, bh, colour;
    int ix0, ix1, iy0, iy1;   /* the visible ink (alpha of 32 or more) in the cell's frame */
    long lost[4];   /* pixels beyond the left, right, top and bottom of the cell */
    FT_Face face;
} ClipProbe;
static ClipProbe probe;
static int probe_columns = 1, probe_column = 0;

void icon_clip_probe(FT_Face face, FT_Bitmap *bitmap, int left, int top, int w, int h, int colour);
void icon_clip_probe(FT_Face face, FT_Bitmap *bitmap, int left, int top, int w, int h, int colour)
{
    int x, y;
    probe.calls++; probe.face = face; probe.w = w; probe.h = h; probe.left = left; probe.top = top;
    probe.bw = (int)bitmap->width; probe.bh = (int)bitmap->rows; probe.colour = colour;
    probe.lost[0] = probe.lost[1] = probe.lost[2] = probe.lost[3] = 0;
    probe.ix0 = probe.iy0 = 1 << 20; probe.ix1 = probe.iy1 = -(1 << 20);
    for (y = 0; y < probe.bh; ++y) for (x = 0; x < probe.bw; ++x) {
        const unsigned char *in = bitmap->buffer + y * bitmap->pitch + x * (colour ? 4 : 1);
        int cx = left + x, cy = top + y;
        if ((colour ? in[3] : in[0]) >= 32) {
            if (left + x < probe.ix0) probe.ix0 = left + x;
            if (left + x > probe.ix1) probe.ix1 = left + x;
            if (top + y < probe.iy0) probe.iy0 = top + y;
            if (top + y > probe.iy1) probe.iy1 = top + y;
        }
        if (!(colour ? in[3] : in[0])) continue;
        if (cx < 0) probe.lost[0]++;
        if (cx >= w) probe.lost[1]++;
        if (cy < 0) probe.lost[2]++;
        if (cy >= h) probe.lost[3]++;
    }
}

static const char *face_name(const BlitRaster *r, FT_Face f)
{
    int i;
    if (f == r->icons) return "blit-icons";
    if (f == r->nerd) return "nerd";
    if (f == r->outline) return "text";
    for (i = 0; i < 3; ++i) if (f == r->symbols[i]) return i == 0 ? "symbols2" : i == 1 ? "symbols" : "math";
    for (i = 0; i < 2; ++i) if (f == r->unifont[i]) return i == 0 ? "unifont" : "unifont-upper";
    return "other";
}

typedef struct { char kind[24]; unsigned cp; int columns, column; char label[64]; } Item;

static ClipProbe prev_part;
static int prev_ok;

static int sweep(BlitRaster *r, const Item *it, int w, int h)
{
    BlitRasterGlyph g;
    int ok, bad = 0, seam = 0;
    long l, rr, t, b;
    memset(&probe, 0, sizeof probe);
    ok = blit_raster_get(r, it->cp, 1, 0, &g);
    if (!strcmp(it->kind, "cell")) {
        /* A full-cell glyph (block, box line, powerline, window chrome) is
           exact geometry, not fitted: it must reach the edges it should (bit 1
           left, 2 right, 4 top, 8 bottom of `column`) and, for 16, fill the
           cell. Its row or column of ink must not stop short at any size. */
        int x, y, need = it->column, seen = 0, full = 1;
        if (!ok && r->pixel) return 0;   /* a pixel font keeps its own PSF cells */
        if (!ok || g.format != BLIT_RASTER_COVERAGE) { printf("BAD cell U+%X %s %dx%d lost=0 sides=N fallback=%d drawn=%d via=-\n", it->cp, it->label, w, h, ok ? g.fallback : 0, ok); return 1; }
        for (y = 0; y < h; ++y) for (x = 0; x < w; ++x) {
            int v = g.pixels[y * g.stride + x];
            if (!v) { full = 0; continue; }
            if (x == 0) seen |= 1;
            if (x == w - 1) seen |= 2;
            if (y == 0) seen |= 4;
            if (y == h - 1) seen |= 8;
        }
        if ((need & 15 & ~seen) || ((need & 16) && !full)) {
            printf("BAD cell U+%X %s %dx%d lost=%d sides=%s%s%s%s fallback=0 drawn=1 via=exact\n", it->cp, it->label, w, h, need & 15 & ~seen,
                   (need & 1 & ~seen) ? "L" : "", (need & 2 & ~seen) ? "R" : "", (need & 4 & ~seen) ? "T" : "",
                   (need & 8 & ~seen) ? "B" : "");
            return 1;
        }
        return 0;
    }
    l = probe.lost[0]; rr = probe.lost[1]; t = probe.lost[2]; b = probe.lost[3];
    if (it->columns > 1) {                  /* ink toward a neighbour is the design */
        if (it->column > 0) l = 0;
        if (it->column < it->columns - 1) rr = 0;
    }
    if (getenv("ICON_CLIP_TRACE") && probe.calls)
        printf("TRACE %s U+%X %s %dx%d col=%d/%d left=%d top=%d bitmap=%dx%d calls=%d\n", it->kind, it->cp,
               it->label, w, h, it->column, it->columns, probe.left, probe.top, probe.bw, probe.bh, probe.calls);
    if (!ok && strncmp(it->kind, "icon-", 5) && strcmp(it->kind, "text-glyph")) return 0;
    /* The two halves of a spanning icon are one picture cut in two: the right
       half's visible ink is the left half's, one cell over, to the pixel. */
    if (it->columns == 2 && it->column == 0) { prev_part = probe; prev_ok = ok && probe.calls; }
    if (it->columns == 2 && it->column == 1 && ok && probe.calls && prev_ok &&
        (prev_part.ix0 != probe.ix0 + w || prev_part.ix1 != probe.ix1 + w ||
         prev_part.iy0 != probe.iy0 || prev_part.iy1 != probe.iy1)) seam = 1;
    if (seam) {
        printf("BAD %s U+%X %s %dx%d lost=%d sides=S fallback=0 drawn=1 via=%s ink=%d..%d,%d..%d|%d..%d,%d..%d calls=%d\n",
               it->kind, it->cp, it->label, w, h, 1, face_name(r, probe.face),
               prev_part.ix0, prev_part.ix1, prev_part.iy0, prev_part.iy1,
               probe.ix0 + w, probe.ix1 + w, probe.iy0, probe.iy1, probe.calls);
        bad = 1;
    }
    if (ok && !probe.calls && !strncmp(it->kind, "icon-", 5)) {
        printf("BAD %s U+%X %s %dx%d lost=0 sides=N fallback=0 drawn=1 via=- bitmap=0x0 at=0,0 calls=0\n",
               it->kind, it->cp, it->label, w, h);
        bad = 1;
    }
    if (!ok || g.fallback || l || rr || t || b) {
        printf("BAD %s U+%X %s %dx%d lost=%ld sides=%s%s%s%s fallback=%d drawn=%d via=%s bitmap=%dx%d at=%d,%d calls=%d\n",
               it->kind, it->cp, it->label, w, h, l + rr + t + b,
               l ? "L" : "", rr ? "R" : "", t ? "T" : "", b ? "B" : "",
               ok ? g.fallback : 0, ok, probe.face ? face_name(r, probe.face) : "-",
               probe.bw, probe.bh, probe.left, probe.top, probe.calls);
        bad = 1;
    }
    return bad;
}

int main(int argc, char **argv)
{
    BlitFont font;
    FILE *spec;
    Item *items = NULL;
    size_t count = 0, cap = 0, i;
    char line[256];
    int stride, a, swept = 0, bad = 0;
    if (argc < 8) { fprintf(stderr, "usage: psf outline emoji spec stride w h [w h]...\n"); return 2; }
    memset(&font, 0, sizeof font);
    if (blit_font_load_path(&font, argv[1])) return 2;
    spec = fopen(argv[4], "r");
    if (!spec) return 2;
    stride = atoi(argv[5]);
    while (fgets(line, sizeof line, spec)) {
        Item it;
        memset(&it, 0, sizeof it);
        if (sscanf(line, "%23s %x %d %d %63s", it.kind, &it.cp, &it.columns, &it.column, it.label) < 4) continue;
        if (count == cap) { cap = cap ? cap * 2 : 512; items = realloc(items, cap * sizeof *items); }
        items[count++] = it;
    }
    fclose(spec);
    for (a = 6; a + 1 < argc; a += 2) {
        int w = atoi(argv[a]), h = atoi(argv[a + 1]), f;
        BlitRaster *r = strcmp(argv[2], "-") ? blit_raster_open(&font, argv[2], argv[3], w, h)
                                              : blit_raster_open_pixel(&font, argv[3], w, h);
        if (!r) { printf("BAD open %dx%d\n", w, h); bad++; continue; }
        for (i = 0; i < count; ++i) { swept++; bad += sweep(r, &items[i], w, h); }
        /* enumerated faces, at a stride */
        for (f = 0; f < 7; ++f) {
            FT_Face face = f == 0 ? r->nerd : f < 4 ? r->symbols[f - 1] : f == 4 ? r->unifont[0] : f == 5 ? r->unifont[1] : r->outline;
            const char *kind = f == 0 ? "nerd" : f == 1 ? "symbols2" : f == 2 ? "symbols" : f == 3 ? "math" : f == 4 ? "unifont" : f == 5 ? "unifont-upper" : "text-overhang";
            FT_UInt gi; FT_ULong c; int n = 0;
            if (!face || stride <= 0) continue;
            for (c = FT_Get_First_Char(face, &gi); gi; c = FT_Get_Next_Char(face, c, &gi), ++n) {
                Item it;
                if (f == 6 ? !(c >= 0x27F5 && c <= 0x27FF) : n % stride) continue;
                { size_t k; for (k = 0; k < count && items[k].cp != (unsigned)c; ++k) ; if (k < count) continue; }
                memset(&it, 0, sizeof it);
                strcpy(it.kind, kind); it.cp = (unsigned)c; it.columns = 1; strcpy(it.label, "-");
                swept++; bad += sweep(r, &it, w, h);
            }
        }
        blit_raster_close(r);
    }
    printf("SWEPT %d BAD %d\n", swept, bad);
    return 0;
}
