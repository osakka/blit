/* ADR 0181 step 3: the GPU view compositor against the CPU one. Each scene
   is drawn by the real draw_view (the reference) and by the GLES compositor
   on Mesa's surfaceless platform, read back, and compared pixel by pixel,
   allowing 1 per channel for blend rounding (ADR 0181). Uses the packaged
   Terminus and wide fonts, so glyphs are the ones the host draws. */
#include "../../src/blit_kms.c"
#include "../../src/blit_raster.h"

#define W 320
#define H 160
#define COLS 40
#define ROWS 10
#define CW 8
#define CH 16

static uint32_t codepoints[COLS * ROWS], foreground[COLS * ROWS],
    background[COLS * ROWS];
static uint16_t attributes[COLS * ROWS];
static uint32_t reference[W * H], gpu[W * H], retained[W * H];
static BlitGraphicsImage images[BLIT_GRAPHICS_MAX_IMAGES];
static unsigned char image_rgb[16 * 16 * 3], image_rgba[12 * 12 * 4];
static BlitFont font, wide;
static int max_diff, damage_scenes, scenes;

static void text(int x, int y, const char *s, uint32_t fg, uint32_t bg, uint16_t attr)
{
    for (; *s && x < COLS; ++s, ++x) {
        int i = y * COLS + x;
        codepoints[i] = (unsigned char)*s;
        foreground[i] = fg ? fg + 1 : 0;
        background[i] = bg ? bg + 1 : 0;
        attributes[i] = attr;
    }
}

static void reset(void)
{
    memset(codepoints, 0, sizeof(codepoints));
    memset(foreground, 0, sizeof(foreground));
    memset(background, 0, sizeof(background));
    memset(attributes, 0, sizeof(attributes));
    memset(images, 0, sizeof(images));
}

static void image(int slot, int x, int y, int z, int rgba)
{
    BlitGraphicsImage *im = &images[slot];
    im->used = im->complete = im->visible = 1;
    im->width = rgba ? 12 : 16;
    im->height = rgba ? 12 : 16;
    im->format = rgba ? 32 : 24;
    im->pixels = rgba ? image_rgba : image_rgb;
    im->opaque = !rgba;
    im->x = x; im->y = y; im->z = z;
    im->generation = (unsigned long long)slot + 1;
}

static void diff(const char *scene, const BlitKmsSurfaceView *view);

static void compare(const char *scene, BlitKms *kms, KmsOutput *out, BlitKmsSurfaceView *view)
{
    KmsOutput ref;
    memset(&ref, 0, sizeof(ref));
    ref.mode = out->mode;
    ref.back.map = reference;
    ref.back.pitch = W * 4;
    ref.back.size = sizeof(reference);
    memset(reference, 0, sizeof(reference));
    draw_view(&ref, &font, &wide, view);
    memset(gpu, 0x55, sizeof(gpu));
    if (kms_gles_compose(kms, out, view, 1, 0, 1, NULL, 0, 0, 0, 0, gpu) < 0) {
        fprintf(stderr, "GLES_COMPOSE_FAIL %s: GPU composition failed: %s\n", scene,
                out->info.compositor_reason);
        exit(1);
    }
    remember_output_views(out, view, 1, 1);
    memcpy(retained, reference, sizeof(retained));
    diff(scene, view);
}

/* A damage frame, as the present path makes one: the CPU clears the damage
   rectangle and redraws the view limited to it over the frame it retained;
   the GPU recomposes only that damage into its persistent scene. Cells that
   changed outside the damage must stay stale on both. */
static void compare_damage(const char *scene, BlitKms *kms, KmsOutput *out,
                           BlitKmsSurfaceView *view, int dx, int dy, int dw, int dh)
{
    KmsOutput ref;
    memset(&ref, 0, sizeof(ref));
    ref.mode = out->mode;
    ref.back.map = retained;
    ref.back.pitch = W * 4;
    ref.back.size = sizeof(retained);
    view->damage_valid = 1;
    view->damage_x = dx; view->damage_y = dy;
    view->damage_width = dw; view->damage_height = dh;
    fill_rect(&ref, view->origin_x + dx * view->cell_width,
              view->origin_y + dy * view->cell_height,
              dw * view->cell_width, dh * view->cell_height, 0);
    draw_view(&ref, &font, &wide, view);
    memcpy(reference, retained, sizeof(reference));
    memset(gpu, 0x55, sizeof(gpu));
    out->info.drawn_cells = 0;
    if (kms_gles_compose(kms, out, view, 1, 0, 0, NULL, 0, 0, 0, 0, gpu) < 0) {
        fprintf(stderr, "GLES_COMPOSE_FAIL %s: GPU composition failed: %s\n", scene,
                out->info.compositor_reason);
        exit(1);
    }
    remember_output_views(out, view, 1, 1);
    view->damage_valid = 0;
    if (out->info.drawn_cells >= (unsigned long long)view->columns * view->rows) {
        fprintf(stderr, "GLES_COMPOSE_FAIL %s: the GPU recomposed the whole view (%llu cells)\n",
                scene, out->info.drawn_cells);
        exit(1);
    }
    diff(scene, view);
    ++damage_scenes;
}

static void diff(const char *scene, const BlitKmsSurfaceView *view)
{
    int i, bad = 0;
    ++scenes;
    for (i = 0; i < W * H; ++i) {
        uint32_t a = reference[i] & 0xffffffU, b = gpu[i] & 0xffffffU;
        int c, worst = 0;
        for (c = 0; c < 24; c += 8) {
            int d = (int)((a >> c) & 255) - (int)((b >> c) & 255);
            if (d < 0) d = -d;
            if (d > worst) worst = d;
        }
        if (worst > max_diff) max_diff = worst;
        /* Bold is two strikes on the CPU, each rounded, and one blend of
           their combined coverage on the GPU: up to 2 on its antialiased
           edges. Everywhere else the contract is 1. */
        {
            int cx = (i % W - view->origin_x) / view->cell_width;
            int cy = (i / W - view->origin_y) / view->cell_height;
            if (worst == 2 && cx >= 0 && cy >= 0 && cx < view->columns && cy < view->rows &&
                (view->attributes[cy * view->stride + cx] & 1))
                worst = 1;
        }
        if (worst > 1) {
            if (bad < 3)
                fprintf(stderr, "GLES_COMPOSE_FAIL %s at %d,%d: cpu %06x gpu %06x\n",
                        scene, i % W, i / W, a, b);
            ++bad;
        }
    }
    if (bad) {
        fprintf(stderr, "GLES_COMPOSE_FAIL %s: %d pixels differ by more than 1\n", scene, bad);
        exit(1);
    }
}

int main(void)
{
    BlitKms kms;
    KmsOutput *out;
    BlitKmsSurfaceView view;
    char error[160];
    int i;
    memset(&kms, 0, sizeof(kms));
    if (blit_font_load_path(&font, "assets/fonts/Blit-Hibr-Terminus16.psf") < 0 ||
        blit_font_load_wide(&wide) < 0) {
        fprintf(stderr, "GLES_COMPOSE_FAIL fonts do not load\n");
        return 1;
    }
    kms.font = &font;
    kms.wide_font = &wide;
    kms.gles = blit_gles_open_surfaceless(error, sizeof(error));
    if (kms.gles == NULL) { fprintf(stderr, "GLES_COMPOSE_FAIL %s\n", error); return 1; }
    kms.output_count = 1;
    out = &kms.outputs[0];
    out->info.id = 7;
    out->mode.hdisplay = W; out->mode.vdisplay = H;
    out->back.pitch = W * 4;
    out->gles_target = blit_gles_target_open(kms.gles, W, H, error, sizeof(error));
    if (out->gles_target == NULL) { fprintf(stderr, "GLES_COMPOSE_FAIL %s\n", error); return 1; }
    for (i = 0; i < 16 * 16; ++i) {
        image_rgb[i * 3] = (unsigned char)(i * 7);
        image_rgb[i * 3 + 1] = (unsigned char)(255 - i);
        image_rgb[i * 3 + 2] = (unsigned char)(i * 3);
    }
    for (i = 0; i < 12 * 12; ++i) {
        image_rgba[i * 4] = 250; image_rgba[i * 4 + 1] = 40;
        image_rgba[i * 4 + 2] = (unsigned char)(i * 5);
        image_rgba[i * 4 + 3] = (unsigned char)(i * 255 / 143);
    }

    memset(&view, 0, sizeof(view));
    view.display_id = 7; view.surface_id = 1;
    view.columns = COLS; view.rows = ROWS; view.stride = COLS;
    view.cell_width = CW; view.cell_height = CH;
    view.codepoints = codepoints; view.foreground = foreground;
    view.background = background; view.attributes = attributes;
    view.default_foreground = 0xd0d0d0; view.default_background = 0x101820;
    view.graphics = images; view.graphics_count = BLIT_GRAPHICS_MAX_IMAGES;

    reset();
    text(0, 0, "plain text on the default background", 0, 0, 0);
    text(0, 1, "colours", 0xff8040, 0x204060, 0);
    text(9, 1, "explicit black", 0xffffff, 0x000000, 0);
    text(0, 2, "bold", 0, 0, 1);
    text(6, 2, "italic", 0, 0, 128);
    text(14, 2, "underline", 0x40ff40, 0, 4);
    text(25, 2, "strike", 0, 0, 512);
    text(0, 3, "dim", 0, 0, 2);
    text(5, 3, "reverse", 0x80ff80, 0x302010, 8);
    text(0, 4, "box \xe2\x94\x80", 0, 0, 0);
    codepoints[4 * COLS + 4] = 0x2500; codepoints[4 * COLS + 5] = 0x2588;
    codepoints[4 * COLS + 6] = 0x2580;
    codepoints[5 * COLS + 0] = 0x4e2d; attributes[5 * COLS + 0] = KMS_ATTR_WIDE;
    attributes[5 * COLS + 1] = KMS_ATTR_CONTINUATION;
    view.cursor_visible = 1; view.cursor_x = 3; view.cursor_y = 0; view.cursor_style = 1;
    compare("text", &kms, out, &view);

    view.cursor_style = 4; view.cursor_x = 8; view.surface_revision = 2;
    compare("underline cursor", &kms, out, &view);
    view.cursor_style = 6; view.surface_revision = 3;
    compare("bar cursor", &kms, out, &view);

    /* Damage: rewrite part of row 2 and move the cursor inside the damage;
       also change a cell outside it, which both must leave stale. */
    view.cursor_style = 1; view.cursor_x = 3; view.cursor_y = 0; view.surface_revision = 30;
    compare("damage base", &kms, out, &view);
    text(2, 2, "NEW ital", 0xffe000, 0x402040, 128);
    text(30, 6, "stale", 0, 0, 0);
    view.cursor_x = 5; view.cursor_y = 2; view.surface_revision = 31;
    compare_damage("damage: italic run and cursor", &kms, out, &view, 0, 0, 12, 3);
    text(14, 2, "UNDER", 0x40ff40, 0, 4 | 1);
    view.surface_revision = 32;
    compare_damage("damage: bold underline mid-row", &kms, out, &view, 14, 2, 5, 1);

    reset();
    text(0, 2, "text over, under and between images", 0xffffff, 0, 0);
    text(2, 3, "coloured cell", 0, 0x0000c0, 0);
    image(0, 4, 20, -1073741825, 0);
    image(1, 40, 36, -1, 1);
    image(2, 90, 40, 5, 1);
    view.cursor_visible = 0;
    view.surface_revision = 4; ++view.graphics_signature;
    compare("image bands", &kms, out, &view);

    /* The host's font: Modern at 11x22 with colour emoji and the Arabic
       companion, whose ink overhangs its cells. */
    {
        BlitRaster *raster = blit_raster_open(&font, "assets/fonts/BlitModernNerd-Regular.ttf",
                                              "assets/fonts/NotoColorEmoji.ttf", 11, 22);
        if (raster == NULL || !blit_raster_set_arabic(raster, "assets/fonts/BlitArabicModern.ttf")) {
            fprintf(stderr, "GLES_COMPOSE_FAIL the Modern/Arabic raster does not open\n");
            return 1;
        }
        blit_raster_attach(&font, raster);
    }
    reset();
    view.cell_width = 11; view.cell_height = 22;
    view.columns = 29; view.rows = 7;
    text(0, 0, "Modern text, 11x22", 0, 0, 0);
    text(0, 1, "bold", 0, 0, 1);
    text(6, 1, "italic", 0xffd080, 0x203040, 128);
    codepoints[2 * COLS + 0] = 0x1F600; attributes[2 * COLS + 0] = KMS_ATTR_WIDE;
    attributes[2 * COLS + 1] = KMS_ATTR_CONTINUATION;
    codepoints[2 * COLS + 3] = 0x2764;
    codepoints[3 * COLS + 4] = 0xfeb2;
    codepoints[3 * COLS + 5] = 0xfe92;
    {
        static uint32_t combining[COLS * ROWS * BLIT_MAX_COMBINING];
        static unsigned char counts[COLS * ROWS];
        memset(combining, 0, sizeof(combining));
        memset(counts, 0, sizeof(counts));
        combining[(3 * COLS + 5) * BLIT_MAX_COMBINING] = 0x0650;
        counts[3 * COLS + 5] = 1;
        view.combining = combining;
        view.combining_count = counts;
        view.cursor_visible = 0;
        view.surface_revision = 10;
        compare("modern, emoji, arabic", &kms, out, &view);
        view.cursor_visible = 1; view.cursor_style = 1;
        view.cursor_x = 5; view.cursor_y = 3; view.surface_revision = 11;
        compare("block cursor on overhanging ink", &kms, out, &view);
        /* Damage on the overhanging font: the region widens for ink, and
           sources outside it redraw clipped. */
        codepoints[3 * COLS + 6] = 0xfeb3;
        codepoints[1 * COLS + 20] = 'Q';
        view.cursor_x = 6; view.surface_revision = 13;
        compare_damage("damage: arabic neighbour under the cursor, overhang", &kms, out,
                       &view, 6, 3, 1, 1);
        text(0, 4, "wide damage", 0xffffff, 0x305030, 1);
        view.cursor_visible = 0; view.surface_revision = 14;
        compare_damage("damage: bold row on overhanging font", &kms, out, &view, 0, 4, 11, 1);
        /* hibr's title bar, brackets style, against the view's right edge:
           a tee, "_ <white square> x", a tee, on the bar's colour. */
        {
            static const uint32_t bar[] = { 0x2524, '_', ' ', 0x25a1, ' ', 'x', 0x251c };
            int k, start = view.columns - 7;
            view.cursor_visible = 0;
            for (k = 0; k < view.columns; ++k) {
                codepoints[5 * COLS + k] = ' ';
                background[5 * COLS + k] = 0x3a3f48 + 1;
                foreground[5 * COLS + k] = 0xe0e0e0 + 1;
                attributes[5 * COLS + k] = 0;
            }
            for (k = 0; k < 7; ++k) codepoints[5 * COLS + start + k] = bar[k];
            foreground[5 * COLS + start + 5] = 0xff5050 + 1;
            view.surface_revision = 12;
            compare("title bar buttons at the right edge", &kms, out, &view);
        }
    }
    /* Scaled images, as a wallpaper is: odd ratios up and down. */
    {
        static const int sizes[][2] = { {37, 23}, {9, 41}, {200, 120}, {13, 13} };
        int k;
        reset();
        view.cell_width = 11; view.cell_height = 22; view.columns = 29; view.rows = 7;
        view.combining = NULL; view.combining_count = NULL; view.cursor_visible = 0;
        text(0, 1, "text over a scaled wallpaper", 0xffffff, 0, 0);
        for (k = 0; k < 4; ++k) {
            image(0, 3 + k * 50, 5, -1073741825, 0);
            images[0].placement_width = sizes[k][0];
            images[0].placement_height = sizes[k][1];
            image(1, 60, 70, -1, 1);
            images[1].placement_width = 31 + k * 17;
            images[1].placement_height = 5 + k * 9;
            view.surface_revision = 20 + k; ++view.graphics_signature;
            compare("scaled images", &kms, out, &view);
        }
    }
    printf("GLES_COMPOSE_PASS scenes=%d damage_scenes=%d max_channel_diff=%d overhang=%s "
           "renderer=\"%s\"\n", scenes, damage_scenes, max_diff, blit_font_has_overhang(&font) ? "yes" : "no", blit_gles_identity(kms.gles));
    return 0;
}
