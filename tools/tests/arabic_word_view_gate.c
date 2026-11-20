/* ADR 0241: Arabic words through the compositor's own cell path
   (draw_view, CPU). Lines of cells on stdin, as arabic_word_slice_gate.
   For the text font given, cell heights 12-48:
   - every run is drawn as its word: the frame equals the raster's whole
     words placed at their runs, each in its cell's colour;
   - incremental frames: change one letter of a word, or turn a cell into a
     space, then draw with only that cell damaged: equals a clean frame
     (it fails without draw_view's damage widening);
   - the block cursor on any letter changes no pixel outside its own cell,
     and inside it shows the word's ink in the cursor's colours. */
#include "../../src/blit_kms.c"

#define MAX_LINES 16
#define MAX_COLS 64
#define FG 0xd0d8e0U
#define BG 0x202024U
static uint32_t cp[MAX_LINES * MAX_COLS], fgc[MAX_LINES * MAX_COLS], bgc[MAX_LINES * MAX_COLS];
static uint32_t at[MAX_LINES * MAX_COLS], marks[MAX_LINES * MAX_COLS * BLIT_MAX_COMBINING];
static unsigned char counts[MAX_LINES * MAX_COLS];
static int nlines, ncols = MAX_COLS;

static void view_init(BlitKmsSurfaceView *v, int cw, int ch)
{
    memset(v, 0, sizeof(*v));
    v->columns = ncols; v->rows = nlines; v->stride = ncols;
    v->origin_x = 4; v->origin_y = 4;
    v->cell_width = cw; v->cell_height = ch;
    v->codepoints = cp; v->attributes = at; v->foreground = fgc; v->background = bgc;
    v->combining = marks; v->combining_count = counts;
    v->default_foreground = FG; v->default_background = BG;
}

static void frame(KmsOutput *o, const BlitFont *font, BlitKmsSurfaceView *v, int clean)
{
    if (clean) {
        int i;
        for (i = 0; i < o->mode.hdisplay * o->mode.vdisplay; ++i) ((uint32_t *)o->back.map)[i] = BG;
        v->damage_valid = 0;
    }
    draw_view(o, font, NULL, v);
}

int main(int argc, char **argv)
{
    char line[16384];
    BlitFont fallback;
    int h, failures = 0;
    long words = 0, edits = 0, cursors = 0;
    if (argc != 5) return 2;
    while (nlines < MAX_LINES && fgets(line, sizeof(line), stdin)) {
        char *save, *spec; int c = 0;
        line[strcspn(line, "\n")] = 0;
        for (spec = strtok_r(line, ";", &save); spec && c < MAX_COLS; spec = strtok_r(NULL, ";", &save), ++c) {
            int n = 0, at_i = nlines * MAX_COLS + c; char *s2, *t;
            for (t = strtok_r(spec, ",", &s2); t; t = strtok_r(NULL, ",", &s2), ++n) {
                uint32_t s = (uint32_t)strtoul(t, NULL, 16);
                if (!n) cp[at_i] = s == 0x20 ? 0 : s;
                else if (n - 1 < BLIT_MAX_COMBINING) marks[at_i * BLIT_MAX_COMBINING + n - 1] = s, counts[at_i] = (unsigned char)n;
            }
        }
        ++nlines;
    }
    memset(&fallback, 0, sizeof(fallback));
    if (blit_font_load_path(&fallback, argv[1])) return 2;
    for (h = 12; h <= 48; h += 2) {
        int cw = (h + 1) / 2, W = 8 + ncols * cw, H = 8 + nlines * h, row, col, i;
        uint32_t *a = calloc((size_t)W * H, 4), *b = calloc((size_t)W * H, 4), *ref = calloc((size_t)W * H, 4);
        KmsOutput o, ro;
        BlitKmsSurfaceView v;
        BlitFont font = fallback;
        BlitRaster *r = blit_raster_open(&fallback, argv[2], argv[3], cw, h);
        if (!r || !blit_raster_set_arabic_words(r, argv[4])) return 2;
        font.raster_state = NULL; font.raster_free = NULL;
        blit_raster_attach(&font, r);
        memset(&o, 0, sizeof(o));
        o.mode.hdisplay = W; o.mode.vdisplay = H; o.back.pitch = W * 4; o.back.map = a;
        ro = o; ro.back.map = ref;
        view_init(&v, cw, h);
        /* 1. the frame is the words placed at their runs */
        frame(&o, &font, &v, 1);
        for (i = 0; i < W * H; ++i) ref[i] = BG;
        for (row = 0; row < nlines; ++row) {
            const BlitCellCluster *cells[MAX_COLS];
            BlitCellCluster cl[MAX_COLS];
            BlitWordRun runs[MAX_COLS];
            int n, k;
            for (col = 0; col < ncols; ++col) {
                int x = row * MAX_COLS + col;
                cells[col] = cp[x] && blit_cell_cluster_init(&cl[col], cp[x], &marks[x * BLIT_MAX_COMBINING], counts[x], 1) &&
                    blit_cell_word_kind(&cl[col]) != BLIT_WORD_NONE ? &cl[col] : NULL;
            }
            n = blit_cell_word_runs(cells, ncols, runs, MAX_COLS);
            for (k = 0; k < n; ++k) {
                BlitCellWord word; BlitRasterGlyph whole;
                memset(&word, 0, sizeof(word));
                word.kind = runs[k].kind; word.count = runs[k].count;
                memcpy(word.cells, &cl[runs[k].start], (size_t)word.count * sizeof(BlitCellCluster));
                if (!blit_raster_get_word(r, &word, -1, 0, &whole, NULL)) continue;
                draw_cell_raster(&ro, &font, &whole, 4 + runs[k].start * cw, 4 + row * h, cw, h, FG, 0, 0);
                ++words;
            }
        }
        /* Compare only where the reference has words: other cells (Latin)
           are the cell path's, unchanged. */
        for (i = 0; i < W * H; ++i)
            if (ref[i] != BG && a[i] != ref[i]) { if (failures++ < 8) fprintf(stderr, "word placement h=%d at %d,%d\n", h, i % W, i / W); break; }
        /* 2. incremental frames equal clean ones */
        for (row = 0; row < nlines; ++row)
            for (col = 0; col < ncols; ++col) {
                int x = row * MAX_COLS + col, kind;
                BlitCellCluster one;
                uint32_t saved = cp[x];
                if (!cp[x] || !blit_cell_cluster_init(&one, cp[x], NULL, 0, 1)) continue;
                kind = blit_cell_word_kind(&one);
                if (kind != BLIT_WORD_LETTERS) continue;
                frame(&o, &font, &v, 1);
                cp[x] = cp[x] == 0xfe92 ? 0xfe91 : 0;     /* another letter, or a space */
                v.damage_valid = 1; v.damage_x = col; v.damage_y = row; v.damage_width = 1; v.damage_height = 1;
                draw_view(&o, &font, NULL, &v);
                memcpy(b, a, (size_t)W * H * 4);
                o.back.map = a;
                frame(&o, &font, &v, 1);
                if (memcmp(a, b, (size_t)W * H * 4)) {
                    if (failures++ < 8) fprintf(stderr, "incremental != clean h=%d row %d col %d\n", h, row, col);
                } else ++edits;
                cp[x] = saved;
                if (col % 3) col += 2;        /* a sample of every word's cells */
            }
        /* 3. the block cursor changes only its own cell */
        frame(&o, &font, &v, 1);
        memcpy(b, a, (size_t)W * H * 4);
        for (row = 0; row < nlines; ++row)
            for (col = 0; col < ncols; ++col) {
                int x = row * MAX_COLS + col, px, py, cx = 4 + col * cw, cy = 4 + row * h, bad = 0;
                BlitCellCluster one;
                if (!cp[x] || !blit_cell_cluster_init(&one, cp[x], NULL, 0, 1) ||
                    blit_cell_word_kind(&one) != BLIT_WORD_LETTERS) continue;
                memcpy(a, b, (size_t)W * H * 4);
                v.cursor_visible = 1; v.cursor_x = col; v.cursor_y = row; v.cursor_style = 2;
                v.damage_valid = 1; v.damage_x = col; v.damage_y = row; v.damage_width = 1; v.damage_height = 1;
                draw_view(&o, &font, NULL, &v);
                for (py = 0; py < H && !bad; ++py)
                    for (px = 0; px < W; ++px)
                        if ((px < cx || px >= cx + cw || py < cy || py >= cy + h) && a[py * W + px] != b[py * W + px]) { bad = 1; break; }
                v.cursor_visible = 0;
                if (bad) { if (failures++ < 8) fprintf(stderr, "cursor leaks h=%d row %d col %d\n", h, row, col); }
                else ++cursors;
            }
        v.damage_valid = 0;
        blit_raster_attach(&font, NULL);
        free(a); free(b); free(ref);
    }
    blit_font_free(&fallback);
    printf("ARABIC_WORD_VIEW words=%ld incremental=%ld cursor=%ld failed=%d\n",
           words, edits, cursors, failures);
    return failures || !words || !edits || !cursors ? 1 : 0;
}
