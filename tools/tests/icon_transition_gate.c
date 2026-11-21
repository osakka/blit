/* ADR 0243: Blit's icons and cell geometry moved from U+100000-U+100495 to
   U+101000-U+101495 (old + 0x1000, the same layout), and during the
   transition the old range draws exactly what the new one does. Checked
   where it is mapped (src/blit_icon_codepoints.h) and where it shows:
   - the raster: every old codepoint, in each icon pen, regular and bold,
     against its new one drawn by a second raster (so the pixels are drawn
     twice, not read back from one cache entry), and every Slate, frame,
     pressed-box and key glyph's three pen planes the same way;
   - the renderers' cell tests: blit_chrome_colour and blit_icon_codepoint
     answer alike for both;
   - the compositors: one view of chrome, icons in their pens, a spanning
     icon, bar corners, keys and player glyphs, written in the new range
     and again in the old, drawn by the CPU (draw_view) and the GPU
     (kms_gles_compose on Mesa's surfaceless platform): the old frame is
     the new one pixel for pixel on each. (GPU against CPU is
     gles_compose_gate's.)
   argv[1] is the number of the catalogue's plane-16 glyphs; each must
   draw at its new codepoint. Built with BLIT_ICON_OLD_RANGE_DROPPED (no
   mapping) it must fail: icon_transition_gate.sh checks that it does. */
#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"

#define OLD_FIRST 0x100000U
#define OLD_LAST 0x100495U
#define MOVE 0x1000U
#define CW 11
#define CH 22
#define COLS 40
#define ROWS 6
#define W (COLS * CW)
#define H (ROWS * CH)

static BlitFont font, wide;
static int failures;

#define FAIL(...) do { fprintf(stderr, "ICON_TRANSITION_FAIL " __VA_ARGS__); ++failures; } while (0)

static int same_glyph(const BlitRasterGlyph *a, const BlitRasterGlyph *b)
{
    int y;
    size_t row = (size_t)a->width * (size_t)a->format;
    if (a->width != b->width || a->height != b->height || a->format != b->format ||
        a->baseline != b->baseline || a->ink_offset_x != b->ink_offset_x ||
        a->ink_offset_y != b->ink_offset_y || a->fallback != b->fallback)
        return 0;
    for (y = 0; y < a->height; ++y)
        if (memcmp(a->pixels + (size_t)y * a->stride, b->pixels + (size_t)y * b->stride, row))
            return 0;
    return 1;
}

static BlitRaster *open_raster(void)
{
    BlitRaster *r = blit_raster_open(&font, "assets/fonts/text/Hack-Regular.ttf",
                                     "assets/fonts/NotoColorEmoji.ttf", CW, CH);
    if (!r) { fprintf(stderr, "ICON_TRANSITION_FAIL the raster does not open\n"); exit(1); }
    return r;
}

/* Every old codepoint against its new one; returns how many new ones draw
   a glyph of their own. */
static int raster_check(int *planes)
{
    BlitRaster *now = open_raster(), *then = open_raster();
    uint32_t old;
    int drawn = 0, pen, bold, plane;
    for (old = OLD_FIRST; old <= OLD_LAST; ++old) {
        uint32_t cp = old + MOVE;
        BlitRasterGlyph a, b;
        if (blit_chrome_colour(old) != blit_chrome_colour(cp) ||
            blit_icon_codepoint(old) != blit_icon_codepoint(cp))
            FAIL("U+%04X: the renderers' cell tests differ from U+%04X's\n", old, cp);
        memset(&a, 0, sizeof(a));
        if (blit_raster_get(now, cp, 1, BLIT_RASTER_ICON_COLOUR, &a) && !a.fallback) ++drawn;
        for (pen = BLIT_RASTER_ICON_COLOUR; pen <= BLIT_RASTER_ICON_WHITE; ++pen)
            for (bold = 0; bold < 2; ++bold) {
                int got_a = blit_raster_get_styled(now, cp, 1, (BlitRasterIconPen)pen, bold, &a);
                int got_b = blit_raster_get_styled(then, old, 1, (BlitRasterIconPen)pen, bold, &b);
                if (got_a != got_b || (got_a && !same_glyph(&a, &b))) {
                    FAIL("U+%04X pen %d bold %d does not draw as U+%04X\n", old, pen, bold, cp);
                    pen = BLIT_RASTER_ICON_WHITE + 1;
                    break;
                }
            }
        if (blit_chrome_colour(cp))
            for (plane = WR_FG; plane <= WR_MIX; ++plane) {
                int got_a = blit_raster_get_window_plane(now, cp, plane, &a);
                int got_b = blit_raster_get_window_plane(then, old, plane, &b);
                if (!got_a || got_a != got_b || !same_glyph(&a, &b)) {
                    FAIL("U+%04X plane %d is not U+%04X's\n", old, plane, cp);
                    break;
                }
                ++*planes;
            }
    }
    blit_raster_close(now);
    blit_raster_close(then);
    return drawn;
}

static uint32_t codepoints[COLS * ROWS], foreground[COLS * ROWS], background[COLS * ROWS],
    underline[COLS * ROWS], attributes[COLS * ROWS];
static uint32_t frame_new[W * H], frame_old[W * H], gpu_new[W * H], gpu_old[W * H];

static void put(int x, int y, uint32_t cp, uint32_t fg, uint32_t bg, uint32_t line, uint32_t attr)
{
    int i = y * COLS + x;
    codepoints[i] = cp;
    foreground[i] = fg ? fg + 1 : 0;
    background[i] = bg ? bg + 1 : 0;
    underline[i] = line ? line + 1 : 0;
    attributes[i] = attr;
}

/* A scene in the new range: Chalk and Slate title and edges, the frame,
   pressed boxes, controls, keys, player glyphs, colour icons in each pen
   (ADR 0221, SGR 68: the pen is bits 21-22), a spanning icon, the bar
   corners, and a block cursor on an icon. */
static void scene(void)
{
    static const uint32_t pens[] = { 0, 1U << 21, 2U << 21, 3U << 21 };
    int x;
    memset(codepoints, 0, sizeof(codepoints));
    for (x = 0; x < 35; ++x) {
        put(x, 0, 0x101231U + (uint32_t)x, 0xe0e0e0, 0x3a3f48, 0, 0);
        put(x, 1, 0x101254U + (uint32_t)x, 0xf0f0f0, 0x506070, 0x203040, 0);
    }
    for (x = 0; x < 13; ++x) put(x, 2, 0x1012AEU + (uint32_t)x, 0xd0d0d0, 0x404850, 0x101010, 0);
    for (x = 0; x < 10; ++x) put(13 + x, 2, 0x1012BBU + (uint32_t)x, 0xd0d0d0, 0x404850, 0x101010, 0);
    for (x = 0; x < 4; ++x) put(23 + x, 2, 0x1012CDU + (uint32_t)x, 0xd0d0d0, 0x404850, 0x101010, 0);
    for (x = 0; x < 10; ++x) put(27 + x, 2, 0x10130AU + (uint32_t)x, 0xd0d0d0, 0x404850, 0x101010, 0);
    for (x = 0; x < 20; ++x) put(x, 3, 0x10129AU + (uint32_t)x, 0x40c0ff, 0x202020, 0x808080, 0);
    for (x = 0; x < 4; ++x) put(20 + x, 3, 0x1013D1U + (uint32_t)x, 0xffffff, 0x305080, 0x102030, 0);
    put(24, 3, 0x101007U, 0x3a3f48, 0x101820, 0, 0);
    put(25, 3, 0x101008U, 0x3a3f48, 0x101820, 0, 0);
    put(26, 3, 0x101018U, 0, 0, 0, 0);
    put(27, 3, 0x101019U, 0, 0, 0, 0);
    for (x = 0; x < 12; ++x) put(28 + x, 3, 0x101314U + (uint32_t)(x * 9), 0xffe080, 0x101010, 0, 0);
    for (x = 0; x < 4; ++x) {
        put(x * 4, 4, 0x101003U, 0xffffff, 0x204060, 0, pens[x]);
        put(x * 4 + 1, 4, 0x10100BU, 0x000000, 0xe0e0e0, 0, pens[x]);
        put(x * 4 + 2, 4, 0x101015U, 0xff8040, 0x101820, 0, pens[x] | 1);
        put(x * 4 + 3, 4, 0x101001U, 0x80ff80, 0x303030, 0, pens[x]);
    }
    for (x = 0; x < 16; ++x) put(x, 5, 0x1013D5U + (uint32_t)x, 0, 0, 0, 0);
    put(16, 5, 0x101000U, 0, 0, 0, 0);
    put(17, 5, 'A', 0, 0, 0, 0);
    put(18, 5, 0x1F600, 0, 0, 0, KMS_ATTR_WIDE);
    put(19, 5, 0, 0, 0, 0, KMS_ATTR_CONTINUATION);
}

/* The same cells written in the old range. */
static void to_old(void)
{
    int i;
    for (i = 0; i < COLS * ROWS; ++i)
        if (codepoints[i] >= OLD_FIRST + MOVE && codepoints[i] <= OLD_LAST + MOVE)
            codepoints[i] -= MOVE;
}

static void cpu(BlitKmsSurfaceView *view, KmsOutput *out, uint32_t *pixels)
{
    KmsOutput ref;
    memset(&ref, 0, sizeof(ref));
    ref.mode = out->mode;
    ref.back.map = pixels;
    ref.back.pitch = W * 4;
    ref.back.size = W * H * 4;
    memset(pixels, 0, W * H * 4);
    draw_view(&ref, &font, &wide, view);
}

static void gpu(BlitKms *kms, KmsOutput *out, BlitKmsSurfaceView *view, uint32_t *pixels)
{
    memset(pixels, 0x55, W * H * 4);
    ++view->surface_revision;
    if (kms_gles_compose(kms, out, view, 1, 0, 1, NULL, 0, 0, 0, 0, pixels) < 0) {
        fprintf(stderr, "ICON_TRANSITION_FAIL GPU composition failed: %s\n", out->info.compositor_reason);
        exit(1);
    }
    remember_output_views(out, view, 1, 1);
}

static int differ(const uint32_t *a, const uint32_t *b, int tolerance)
{
    int i, c, bad = 0;
    for (i = 0; i < W * H; ++i)
        for (c = 0; c < 24; c += 8) {
            int d = (int)((a[i] >> c) & 255) - (int)((b[i] >> c) & 255);
            if (d > tolerance || d < -tolerance) {
                if (!bad) fprintf(stderr, "  first at (%d,%d), cell (%d,%d): %06x against %06x\n",
                                  i % W, i / W, i % W / CW, i / W / CH, a[i] & 0xffffffU, b[i] & 0xffffffU);
                ++bad;
                break;
            }
        }
    return bad;
}

int main(int argc, char **argv)
{
    BlitKms kms;
    KmsOutput *out;
    BlitKmsSurfaceView view;
    char error[160];
    int expected = argc > 1 ? atoi(argv[1]) : 0, drawn, planes = 0, bad, ink = 0, i;
    if (blit_font_load_path(&font, "assets/fonts/Blit-Terminus16.psf") < 0 ||
        blit_font_load_wide(&wide) < 0) {
        fprintf(stderr, "ICON_TRANSITION_FAIL fonts do not load\n");
        return 1;
    }
    drawn = raster_check(&planes);
    if (expected <= 0 || drawn != expected)
        FAIL("%d new codepoints draw a glyph of their own; the catalogue has %d\n", drawn, expected);

    blit_raster_attach(&font, open_raster());
    memset(&kms, 0, sizeof(kms));
    kms.font = &font;
    kms.wide_font = &wide;
    kms.gles = blit_gles_open_surfaceless(error, sizeof(error));
    if (kms.gles == NULL) { fprintf(stderr, "ICON_TRANSITION_FAIL %s\n", error); return 1; }
    kms.output_count = 1;
    out = &kms.outputs[0];
    out->info.id = 7;
    out->mode.hdisplay = W; out->mode.vdisplay = H;
    out->back.pitch = W * 4;
    out->gles_target = blit_gles_target_open(kms.gles, W, H, error, sizeof(error));
    if (out->gles_target == NULL) { fprintf(stderr, "ICON_TRANSITION_FAIL %s\n", error); return 1; }
    memset(&view, 0, sizeof(view));
    view.display_id = 7; view.surface_id = 1;
    view.columns = COLS; view.rows = ROWS; view.stride = COLS;
    view.cell_width = CW; view.cell_height = CH;
    view.codepoints = codepoints; view.foreground = foreground;
    view.background = background; view.underline_color = underline;
    view.attributes = attributes;
    view.default_foreground = 0xd0d0d0; view.default_background = 0x101820;
    view.cursor_visible = 1; view.cursor_style = 1; view.cursor_x = 1; view.cursor_y = 4;

    scene();
    cpu(&view, out, frame_new);
    gpu(&kms, out, &view, gpu_new);
    to_old();
    cpu(&view, out, frame_old);
    gpu(&kms, out, &view, gpu_old);
    for (i = 0; i < W * H; ++i) ink += (frame_new[i] & 0xffffffU) != 0x101820U;
    if (ink < W * H / 4) FAIL("the scene draws too little (%d pixels) to tell\n", ink);
    if ((bad = differ(frame_new, frame_old, 0)) != 0)
        FAIL("CPU: the old-range frame differs from the new one in %d pixels\n", bad);
    if ((bad = differ(gpu_new, gpu_old, 0)) != 0)
        FAIL("GPU: the old-range frame differs from the new one in %d pixels\n", bad);
    if (failures) return 1;
    printf("ICON_TRANSITION_PASS old=U+%04X-U+%04X new=U+%04X-U+%04X glyphs=%d "
           "pens=4 bold=2 planes=%d cpu=identical gpu=identical\n",
           OLD_FIRST, OLD_LAST, OLD_FIRST + MOVE, OLD_LAST + MOVE, drawn, planes);
    return 0;
}
