/* ADR 0241: two displays' worth of distinct Arabic words (3840x2160 at
   11x22, about 5,700 words) on the GLES compositor. The first frame shapes
   every word once and fills the atlas (at most one reset: the empty atlas's
   first fill); every frame after it draws from the caches -- no atlas
   reset, no sprite made, no word shaped again -- and no ink is dropped.
   Counts, not times: the frame times are printed, never judged. */
#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"
#define W 3840
#define H 2160
#define COLS (W / 11)
#define ROWS (H / 22)
static uint32_t cp[COLS * ROWS], fg[COLS * ROWS], bg[COLS * ROWS], at[COLS * ROWS];
static uint32_t marks[COLS * ROWS * BLIT_MAX_COMBINING];
static unsigned char counts[COLS * ROWS];
static uint32_t pixels[W * H];
static BlitFont font, wide;
static long long now_us(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return t.tv_sec * 1000000LL + t.tv_nsec / 1000; }
int main(void)
{
    BlitKms kms; KmsOutput *out; BlitKmsSurfaceView view; char error[160];
    /* letters with initial/medial/final forms: beh..yeh bases from the table */
    static const uint32_t ini[] = {0xfe91,0xfe97,0xfe9b,0xfe9f,0xfea3,0xfea7,0xfeb3,0xfeb7,0xfebb,0xfebf,0xfec3,0xfec7,0xfecb,0xfecf,0xfed3,0xfed7,0xfedb,0xfedf,0xfee3,0xfee7,0xfeeb,0xfef3};
    static const uint32_t med[] = {0xfe92,0xfe98,0xfe9c,0xfea0,0xfea4,0xfea8,0xfeb4,0xfeb8,0xfebc,0xfec0,0xfec4,0xfec8,0xfecc,0xfed0,0xfed4,0xfed8,0xfedc,0xfee0,0xfee4,0xfee8,0xfeec,0xfef4};
    static const uint32_t fin[] = {0xfe90,0xfe96,0xfe9a,0xfe9e,0xfea2,0xfea6,0xfeb2,0xfeb6,0xfeba,0xfebe,0xfec2,0xfec6,0xfeca,0xfece,0xfed2,0xfed6,0xfeda,0xfede,0xfee2,0xfee6,0xfeea,0xfef2};
    static const uint32_t vow[] = {0x64e,0x64f,0x650,0x652,0x651};
    unsigned seed = 12345; int row, col, words = 0, f, failures = 0;
    BlitRaster *r;
    memset(&kms, 0, sizeof(kms));
    if (blit_font_load_path(&font, "assets/fonts/Blit-Terminus16.psf") < 0 || blit_font_load_wide(&wide) < 0) return 2;
    r = blit_raster_open(&font, "assets/fonts/text/Hack-Regular.ttf", "assets/fonts/NotoColorEmoji.ttf", 11, 22);
    if (!r || !blit_raster_set_arabic_words(r, "assets/fonts/NotoNaskhArabic-Regular.ttf")) return 3;
    blit_raster_attach(&font, r);
    kms.font = &font; kms.wide_font = &wide;
    kms.gles = blit_gles_open_surfaceless(error, sizeof(error));
    if (!kms.gles) { fprintf(stderr, "%s\n", error); return 4; }
    kms.output_count = 1; out = &kms.outputs[0];
    out->info.id = 7; out->mode.hdisplay = W; out->mode.vdisplay = H; out->back.pitch = W * 4;
    out->gles_target = blit_gles_target_open(kms.gles, W, H, error, sizeof(error));
    if (!out->gles_target) { fprintf(stderr, "%s\n", error); return 5; }
    for (row = 0; row < ROWS; ++row) {
        col = 0;
        while (col < COLS) {
            int len = 3 + (int)((seed = seed * 1103515245 + 12345) >> 16) % 5, k;
            if (col + len > COLS) break;
            /* visual order: final form on the left, initial on the right */
            for (k = 0; k < len; ++k) {
                int idx = (int)((seed = seed * 1103515245 + 12345) >> 16) % 22, x = row * COLS + col + k;
                cp[x] = k == 0 ? fin[idx] : k == len - 1 ? ini[idx] : med[idx];
                if (((seed = seed * 1103515245 + 12345) >> 16) % 2) {
                    marks[x * BLIT_MAX_COMBINING] = vow[((seed = seed * 1103515245 + 12345) >> 16) % 5];
                    counts[x] = 1;
                }
            }
            col += len + 1; ++words;
        }
    }
    memset(&view, 0, sizeof(view));
    view.display_id = 7; view.surface_id = 1;
    view.columns = COLS; view.rows = ROWS; view.stride = COLS;
    view.cell_width = 11; view.cell_height = 22;
    view.codepoints = cp; view.foreground = fg; view.background = bg; view.attributes = at;
    view.combining = marks; view.combining_count = counts;
    view.default_foreground = 0xd0d0d0; view.default_background = 0x101820;
    {
        /* set occupancy of the screen's words, as the raster's cache sees them */
        static int occ[4096]; int x, worst = 0, over = 0;
        for (row = 0; row < ROWS; ++row) {
            const BlitCellCluster *cells[COLS]; BlitCellCluster cl[COLS]; BlitWordRun runs[COLS]; int n, k;
            for (x = 0; x < COLS; ++x) { int i2 = row * COLS + x;
                cells[x] = cp[i2] && blit_cell_cluster_init(&cl[x], cp[i2], &marks[i2 * BLIT_MAX_COMBINING], counts[i2], 1) ? &cl[x] : NULL; }
            n = blit_cell_word_runs(cells, COLS, runs, COLS);
            for (k = 0; k < n; ++k) { BlitCellWord w; memset(&w, 0, sizeof w); w.kind = runs[k].kind; w.count = runs[k].count;
                memcpy(w.cells, &cl[runs[k].start], (size_t)w.count * sizeof(BlitCellCluster));
                ++occ[blit_cell_word_hash(&w) % 4096]; }
        }
        for (x = 0; x < 4096; ++x) { if (occ[x] > worst) worst = occ[x]; if (occ[x] > 8) over += occ[x] - 8; }
        printf("sets: busiest %d words, %d words beyond 8 ways\n", worst, over);
        if (over) failures++;
    }
    for (f = 0; f < 3; ++f) {
        unsigned long long resets0, sprites0;
        long long t0;
        KmsGlesResources *res = kms.gles_resources;
        resets0 = res ? res->atlas_resets : 0; sprites0 = res ? res->sprites_rendered : 0;
        view.surface_revision = 100 + f;
        t0 = now_us();
        if (kms_gles_compose(&kms, out, &view, 1, 0, 1, NULL, 0, 0, 0, 0, pixels) < 0) { fprintf(stderr, "compose failed: %s\n", out->info.compositor_reason); return 6; }
        res = kms.gles_resources;
        printf("frame %d: %.1f ms, atlas resets %llu, sprites made %llu, sprites now %d\n", f, (now_us() - t0) / 1000.0,
               res->atlas_resets - resets0, res->sprites_rendered - sprites0, res->sprite_count);
        if (f == 0 && res->atlas_resets - resets0 > 1) failures++;
        if (f > 0 && (res->atlas_resets != resets0 || res->sprites_rendered != sprites0)) failures++;
    }
    {
        unsigned long long hits, misses, dropped; size_t bytes;
        blit_raster_word_counts(r, &hits, &misses, &bytes, &dropped);
        printf("words on screen %d, raster word cache: hits %llu misses %llu bytes %zu dropped %llu\n", words, hits, misses, bytes, dropped);
        if (dropped || misses > (unsigned long long)words) failures++;
    }
    printf(failures ? "ARABIC_SCREEN_FAIL %d\n" : "ARABIC_SCREEN_PASS words=%d frames=3 resets_after_first=0 reshaped=0\n", failures ? failures : words);
    return failures ? 1 : 0;
}
