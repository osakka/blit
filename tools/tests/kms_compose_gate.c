/* Pixel gate for the KMS compositor's draw_view (Gitea #84, #88): the same
   z-band contract the SDL gate checks. It includes blit_kms.c so the real
   static function runs against an ordinary memory buffer; no DRM device. */
#include "../../src/blit_kms.c"

#define W 160
#define H 128
#define COLS 20
#define ROWS 8
#define CW 8
#define CH 16
#define RED 0xff0000U
#define WHITE 0xffffffU

static KmsOutput output;
static uint32_t pixels[W * H];
static uint32_t codepoints[COLS * ROWS];
static uint32_t foreground[COLS * ROWS];
static uint32_t background[COLS * ROWS];
static uint16_t attributes[COLS * ROWS];
static unsigned char image_pixels[4 * 8 * 3 * 2];
static BlitGraphicsImage images[BLIT_GRAPHICS_MAX_IMAGES];
static uint32_t front_pixels[W * H];
static uint32_t back_pixels[W * H];
static void place(int z);

static void mode_refresh_gate(void)
{
    drmModeModeInfo mode;
    double got;
    memset(&mode, 0, sizeof(mode));
    mode.clock = 148352; mode.htotal = 2200; mode.vtotal = 1125;
    mode.vrefresh = 60;
    got = mode_refresh_hz(&mode);
    if (got < 59.9401 || got > 59.9403) {
        fprintf(stderr, "KMS_REFRESH_FAIL got=%.6f expected=59.940202 rounded_vrefresh=60\n", got);
        exit(1);
    }
    mode.flags = DRM_MODE_FLAG_INTERLACE;
    if (mode_refresh_hz(&mode) < 119.8802 || mode_refresh_hz(&mode) > 119.8806) exit(1);
    mode.flags = DRM_MODE_FLAG_DBLSCAN;
    if (mode_refresh_hz(&mode) < 29.9700 || mode_refresh_hz(&mode) > 29.9702) exit(1);
    mode.flags = 0; mode.vscan = 3;
    if (mode_refresh_hz(&mode) < 19.9800 || mode_refresh_hz(&mode) > 19.9802) exit(1);
    mode.htotal = 0;
    if (mode_refresh_hz(&mode) != 60.0) exit(1);
    puts("KMS_REFRESH_PASS fractional=exact interlace=2 doublescan=half vscan=third invalid=fallback");
}

static void output_ready_mask_gate(void)
{
    BlitKms kms;
    memset(&kms, 0, sizeof(kms));
    kms.output_count = 3;
    kms.outputs[0].flip_pending = 1;
    kms.outputs[1].info.presentation_state = BLIT_KMS_PRESENTATION_PRESENTING;
    kms.outputs[2].info.presentation_state = BLIT_KMS_PRESENTATION_FAILED;
    if (blit_kms_ready_output_mask(&kms) != 2) {
        fprintf(stderr, "KMS_READY_FAIL pending or failed output marked ready\n");
        exit(1);
    }
    kms_flip_handler(-1, 1, 0, 0, &kms.outputs[0]);
    if (blit_kms_ready_output_mask(&kms) != 3) {
        fprintf(stderr, "KMS_READY_FAIL completed output not marked ready\n");
        exit(1);
    }
    puts("KMS_READY_PASS pending=excluded failed=excluded completed=ready per-output=1");
}

/* Only the kernel submission is replaced. The full production KMS present,
   scene, damage, pointer and buffer-swap code executes against real memory. */
int drmModePageFlip(int fd, uint32_t crtc, uint32_t fb,
                    uint32_t flags, void *data)
{
    (void)fd; (void)crtc; (void)fb; (void)flags; (void)data;
    return 0;
}

static void pointer_terminal_present_gate(void)
{
    BlitKms kms;
    KmsOutput *target;
    BlitKmsSurfaceView view;
    uint32_t submitted, deferred;
    uint32_t *shown;

    memset(&kms, 0, sizeof(kms));
    memset(&view, 0, sizeof(view));
    memset(codepoints, 0, sizeof(codepoints));
    memset(background, 0, sizeof(background));
    memset(foreground, 0, sizeof(foreground));
    memset(attributes, 0, sizeof(attributes));
    kms.output_count = 1;
    target = &kms.outputs[0];
    target->info.id = 93;
    target->info.width = W; target->info.height = H;
    target->mode.hdisplay = W; target->mode.vdisplay = H;
    target->front.map = front_pixels; target->back.map = back_pixels;
    target->front.pitch = target->back.pitch = W * 4;
    target->front.size = target->back.size = sizeof(front_pixels);
    view.display_id = 93; view.surface_id = 1; view.surface_revision = 1;
    view.columns = COLS; view.rows = ROWS; view.stride = COLS;
    view.cell_width = CW; view.cell_height = CH;
    view.codepoints = codepoints; view.background = background;
    view.foreground = foreground; view.attributes = attributes;
    view.default_background = 0x102030;
    background[COLS + 2] = RED + 1;
    blit_kms_present(&kms, &view, 1, 40, 32, 1, 1, NULL, 0,
                     0, 0, 0, 0, &submitted, &deferred);
    target->flip_pending = 0; /* completion of the fake kernel flip */
    background[COLS + 2] = 0;
    background[6 * COLS + 15] = RED + 1;
    view.surface_revision = 2;
    view.damage_valid = 1; view.damage_x = 2; view.damage_y = 1;
    view.damage_width = 14; view.damage_height = 6;
    blit_kms_present(&kms, &view, 1, 48, 32, 1, 1, NULL, 0,
                     0, 0, 0, 0, &submitted, &deferred);
    shown = target->front.map;
    if (shown[CH * W + 2 * CW] != 0x102030 ||
        shown[6 * CH * W + 15 * CW] != RED) {
        fprintf(stderr, "KMS_POINTER_TERMINAL_FAIL old=%06x new=%06x expected=102030,ff0000\n",
                shown[CH * W + 2 * CW], shown[6 * CH * W + 15 * CW]);
        exit(1);
    }
    free(target->scene);
    puts("KMS_POINTER_TERMINAL_PASS erase=exact replacement=exact simultaneous-pointer=1");
}

static void incremental_matches_full_gate(void)
{
    BlitKms kms;
    BlitKmsSurfaceView view, reference_view;
    KmsOutput reference;
    KmsOutput *target;
    uint32_t submitted, deferred;
    int step, i, old_x = 0, old_y = 0;

    memset(&kms, 0, sizeof(kms));
    memset(&view, 0, sizeof(view));
    memset(&reference, 0, sizeof(reference));
    memset(codepoints, 0, sizeof(codepoints));
    memset(foreground, 0, sizeof(foreground));
    memset(background, 0, sizeof(background));
    memset(attributes, 0, sizeof(attributes));
    memset(images, 0, sizeof(images));
    kms.output_count = 1;
    target = &kms.outputs[0];
    target->info.id = 93;
    target->info.width = W; target->info.height = H;
    target->mode.hdisplay = W; target->mode.vdisplay = H;
    target->front.map = front_pixels; target->back.map = back_pixels;
    target->front.pitch = target->back.pitch = W * 4;
    target->front.size = target->back.size = sizeof(front_pixels);
    reference.mode = target->mode;
    reference.back.map = pixels; reference.back.pitch = W * 4;
    reference.back.size = sizeof(pixels);
    view.display_id = 93; view.surface_id = 1;
    view.columns = COLS; view.rows = ROWS; view.stride = COLS;
    view.cell_width = CW; view.cell_height = CH;
    view.codepoints = codepoints; view.background = background;
    view.foreground = foreground; view.attributes = attributes;
    view.default_background = 0x102030;
    view.graphics = images; view.graphics_count = BLIT_GRAPHICS_MAX_IMAGES;
    for (step = 0; step < 128; ++step) {
        int x = (step * 7 + 3) % (COLS - 1);
        int y = (step * 3 + 1) % ROWS;
        int px = (step * 13) % (W - KMS_POINTER_W);
        int py = (step * 11) % (H - KMS_POINTER_H);
        uint32_t *shown;
        background[old_y * COLS + old_x] = 0;
        codepoints[old_y * COLS + old_x] = 0;
        background[y * COLS + x] = 0x305070 + 1;
        foreground[y * COLS + x] = RED + 1;
        codepoints[y * COLS + x] = step & 1 ? 0x2580 : 0x2584;
        view.surface_revision = (unsigned long long)step + 1;
        view.damage_valid = 1;
        view.damage_x = x < old_x ? x : old_x;
        view.damage_y = y < old_y ? y : old_y;
        view.damage_width = (x > old_x ? x : old_x) - view.damage_x + 1;
        view.damage_height = (y > old_y ? y : old_y) - view.damage_y + 1;
        if ((step % 4) == 0) {
            place(-1);
            images[0].x = (step * 5) % (W - 2 * CW);
            images[0].y = (step * 7) % (H - 2 * CH);
            ++view.graphics_signature;
        } else if ((step % 4) == 3) {
            images[0].used = 0;
            ++view.graphics_signature;
        }
        target->flip_pending = 0;
        blit_kms_present(&kms, &view, 1, px, py, 1, 1, NULL, 0,
                         0, 0, 0, 0, &submitted, &deferred);
        memset(pixels, 0, sizeof(pixels));
        reference_view = view;
        reference_view.damage_valid = 0;
        draw_view(&reference, NULL, NULL, &reference_view);
        fill_rect(&reference, px, py, 2, 18, 0xffffff);
        fill_rect(&reference, px, py, 12, 2, 0xffffff);
        shown = target->front.map;
        for (i = 0; i < W * H; ++i) {
            if (shown[i] != pixels[i]) {
                fprintf(stderr, "KMS_FULL_REFERENCE_FAIL step=%d pixel=%d,%d got=%06x want=%06x\n",
                        step, i % W, i / W, shown[i], pixels[i]);
                exit(1);
            }
        }
        old_x = x; old_y = y;
    }
    free(target->scene);
    puts("KMS_FULL_REFERENCE_PASS frames=128 every-pixel=exact half-blocks=1 graphics=move-delete pointer=simultaneous");
}

static void fail(const char *what, long got, long want)
{
    fprintf(stderr, "KMS_COMPOSE_FAIL %s: got %ld, wanted %ld\n", what, got,
            want);
    exit(1);
}

static long count(uint32_t colour)
{
    long n = 0;
    int i;
    for (i = 0; i < W * H; ++i)
        if ((pixels[i] & 0xffffffU) == colour) ++n;
    return n;
}

static void reset(void)
{
    memset(pixels, 0, sizeof(pixels));
    memset(codepoints, 0, sizeof(codepoints));
    memset(foreground, 0, sizeof(foreground));
    memset(background, 0, sizeof(background));
    memset(attributes, 0, sizeof(attributes));
    memset(images, 0, sizeof(images));
}

static void buffer_coherence_gate(void)
{
    KmsOutput pair;
    int i;

    memset(&pair, 0, sizeof(pair));
    pair.mode.hdisplay = W;
    pair.mode.vdisplay = H;
    pair.front.map = front_pixels;
    pair.front.pitch = W * sizeof(uint32_t);
    pair.back.map = back_pixels;
    pair.back.pitch = W * sizeof(uint32_t);
    for (i = 0; i < W * H; ++i) {
        front_pixels[i] = 0x102030U + (uint32_t)i;
        back_pixels[i] = 0xa0b0c0U;
    }
    copy_front_to_back(&pair);
    for (i = 0; i < W * H; ++i)
        if (back_pixels[i] != front_pixels[i])
            fail("back buffer is not a complete front-buffer copy",
                 (long)back_pixels[i], (long)front_pixels[i]);
}

static void scene_copy_gate(void)
{
    KmsOutput scene_output;
    size_t i;

    memset(&scene_output, 0, sizeof(scene_output));
    scene_output.mode.hdisplay = W;
    scene_output.mode.vdisplay = H;
    scene_output.back.map = back_pixels;
    scene_output.back.pitch = W * sizeof(uint32_t);
    scene_output.back.size = sizeof(back_pixels);
    if (!ensure_scene_buffer(&scene_output))
        fail("CPU scene buffer was not allocated", 0, 1);
    for (i = 0; i < sizeof(back_pixels) / sizeof(back_pixels[0]); ++i)
        ((uint32_t *)scene_output.scene)[i] = 0x203040U + (uint32_t)i;
    memset(back_pixels, 0, sizeof(back_pixels));
    copy_scene_to_back(&scene_output);
    for (i = 0; i < sizeof(back_pixels) / sizeof(back_pixels[0]); ++i)
        if (back_pixels[i] != 0x203040U + (uint32_t)i)
            fail("scanout copy does not contain the complete CPU scene",
                 (long)back_pixels[i], (long)(0x203040U + i));
    free(scene_output.scene);
}

static void place(int z)
{
    BlitGraphicsImage *image = &images[0];
    int i;
    for (i = 0; i < 4 * 8; ++i) {
        image_pixels[i * 3] = 255;
        image_pixels[i * 3 + 1] = 0;
        image_pixels[i * 3 + 2] = 0;
    }
    image->used = 1;
    image->complete = 1;
    image->visible = 1;
    image->opaque = 1;
    image->width = 4;
    image->height = 8;
    image->format = 24;
    image->pixels = image_pixels;
    image->placement_width = 2 * CW;
    image->placement_height = 2 * CH;
    image->z = z;
}

static void place_colour(BlitGraphicsImage *image, unsigned int id,
                         unsigned char red, unsigned char green,
                         unsigned char blue, int x)
{
    unsigned char *image_data = image_pixels + (id == 20 ? 0 : 4 * 8 * 3);
    int i;
    for (i = 0; i < 4 * 8; ++i) {
        image_data[i * 3] = red;
        image_data[i * 3 + 1] = green;
        image_data[i * 3 + 2] = blue;
    }
    image->used = 1;
    image->complete = 1;
    image->visible = 1;
    image->opaque = 1;
    image->id = id;
    image->width = 4;
    image->height = 8;
    image->format = 24;
    image->pixels = image_data;
    image->placement_width = 4;
    image->placement_height = 8;
    image->x = x;
}

static uint32_t pixel_at(int x, int y)
{
    return pixels[y * W + x] & 0xffffffU;
}

static void draw_damage(uint32_t default_background, int damage_column,
                        int damage_row, int damage_width, int damage_height)
{
    BlitKmsSurfaceView view;
    memset(&view, 0, sizeof(view));
    view.cell_width = CW;
    view.cell_height = CH;
    view.columns = COLS;
    view.rows = ROWS;
    view.stride = COLS;
    view.codepoints = codepoints;
    view.foreground = foreground;
    view.background = background;
    view.attributes = attributes;
    view.default_foreground = WHITE;
    view.default_background = default_background;
    view.graphics = images;
    view.graphics_count = BLIT_GRAPHICS_MAX_IMAGES;
    view.cursor_x = -1;
    view.cursor_y = -1;
    if (damage_column >= 0) {
        view.damage_valid = 1;
        view.damage_x = damage_column;
        view.damage_y = damage_row;
        view.damage_width = damage_width;
        view.damage_height = damage_height;
    }
    draw_view(&output, NULL, NULL, &view);
}

static void draw(uint32_t default_background, int damage_column)
{
    draw_damage(default_background, damage_column, 5, 1, 1);
}

static void cursor_damage_gate(void)
{
    BlitKmsSurfaceView view;
    uint32_t face = 0x101820U;

    memset(&view, 0, sizeof(view));
    view.cell_width = CW;
    view.cell_height = CH;
    view.columns = COLS;
    view.rows = ROWS;
    view.stride = COLS;
    view.codepoints = codepoints;
    view.foreground = foreground;
    view.background = background;
    view.attributes = attributes;
    view.default_foreground = WHITE;
    view.default_background = face;
    view.cursor_x = 10;
    view.cursor_y = 7;
    view.cursor_visible = 0;

    reset();
    view.damage_valid = 0;
    draw_view(&output, NULL, NULL, &view);

    /* Repaint a cell nowhere near the terminal cursor. A cursor write outside
       this damage rectangle would be an out-of-bounds scene mutation. */
    view.damage_valid = 1;
    view.damage_x = 0;
    view.damage_y = 0;
    view.damage_width = 1;
    view.damage_height = 1;
    view.cursor_visible = 1;
    draw_view(&output, NULL, NULL, &view);
    if (pixel_at(10 * CW, 7 * CH) != face)
        fail("partial KMS repaint drew the terminal cursor outside damage",
             (long)pixel_at(10 * CW, 7 * CH), (long)face);
}


static void pointer_background_gate(void)
{
    int i;
    uint32_t face = 0x102030U;

    memset(&output, 0, sizeof(output));
    output.mode.hdisplay = W;
    output.mode.vdisplay = H;
    output.back.map = pixels;
    output.back.pitch = W * sizeof(uint32_t);
    output.back.size = sizeof(pixels);
    for (i = 0; i < W * H; ++i)
        pixels[i] = face;

    /* Establish the same retained-scene state as a presented pointer. */
    save_pointer_background(&output, 40, 32);
    fill_rect(&output, 40, 32, 12, 18, WHITE);

    /* A saver/terminal update elsewhere must not turn the pointer pixels
       into the next frame's background. The production helper is the exact
       non-full repaint boundary used by blit_kms_present(). */
    fill_rect(&output, 0, 0, CW, CH, RED);
    prepare_repaint_buffer(&output, 0, 1);
    if (pixel_at(40, 32) != face || pixel_at(51, 49) != face)
        fail("stationary pointer background was not restored before repaint",
             (long)pixel_at(40, 32), (long)face);
}

static void graphics_bounds_gate(void)
{
    BlitKmsSurfaceView view;
    int x;
    int y;
    int width;
    int height;

    memset(&view, 0, sizeof(view));
    view.cell_width = CW;
    view.cell_height = CH;
    view.columns = COLS;
    view.rows = ROWS;
    view.graphics = images;
    view.graphics_count = BLIT_GRAPHICS_MAX_IMAGES;
    reset();
    place(-1073741825);
    images[0].x = 40;
    images[0].y = 17;
    images[0].placement_width = 4;
    images[0].placement_height = 8;
    if (!graphics_bounds_for_view(&view, &x, &y, &width, &height) ||
        x != 5 || y != 1 || width != 1 || height != 1)
        fail("graphics bounds did not cover pixel-offset placement",
             (long)(x * 1000000 + y * 10000 + width * 100 + height),
             5010101L);
    images[0].placements[0].used = 1;
    images[0].placements[0].id = 9;
    images[0].placements[0].x = 80;
    images[0].placements[0].y = 32;
    images[0].placements[0].width = 4;
    images[0].placements[0].height = 8;
    if (!graphics_bounds_for_view(&view, &x, &y, &width, &height) ||
        x != 5 || y != 1 || width != 6 || height != 2)
        fail("graphics bounds did not union retained placements",
             (long)(x * 1000000 + y * 10000 + width * 100 + height),
             5010602L);
}

int main(void)
{
    long full;
    long got;
    uint32_t face = 0x101820U;

    buffer_coherence_gate();
    mode_refresh_gate();
    output_ready_mask_gate();
    scene_copy_gate();
    pointer_terminal_present_gate();
    incremental_matches_full_gate();
    graphics_bounds_gate();

    output.mode.hdisplay = W;
    output.mode.vdisplay = H;
    output.back.map = pixels;
    output.back.pitch = W * 4;
    output.back.size = sizeof(pixels);

    cursor_damage_gate();
    pointer_background_gate();

    reset();
    place(-1073741825);
    draw(0, -1);
    full = count(RED);
    if (full < 100) fail("reference image not visible", full, 512);

    reset();
    place(-1073741825);
    draw(face, -1);
    got = count(RED);
    if (got != full) fail("A: under-background image hidden by default quad", got, full);
    if (count(face) < 1000) fail("A: default background is not the floor", count(face), 1000);

    reset();
    place(-1073741825);
    background[0] = 1;  /* explicit black: 0 + 1 */
    draw(face, -1);
    got = full - count(RED);
    if (got < 100 || got > 160) fail("B: explicit black cell not opaque", got, 128);

    reset();
    place(-1073741825);
    codepoints[0] = 'X';
    foreground[0] = WHITE + 1;
    draw(face, -1);
    got = count(WHITE);
    if (got < 5) fail("E: glyph painted under the wallpaper", got, 5);

    reset();
    place(-1);
    codepoints[0] = 'X';
    foreground[0] = WHITE + 1;
    background[1] = 0x0000eeU + 1;
    draw(face, -1);
    if (count(RED) < full - 80) fail("C: coloured cell over z=-1 image", count(RED), full - 80);
    if (count(WHITE) < 5) fail("C: glyph not on top of z=-1 image", count(WHITE), 5);

    reset();
    place(0);
    codepoints[0] = 'X';
    foreground[0] = WHITE + 1;
    draw(face, -1);
    if (count(WHITE) != 0) fail("G: z=0 image must cover text", count(WHITE), 0);

    /* Damage: a redraw of one cell far from the wallpaper must not repaint
       the wallpaper over text it lies under elsewhere. */
    reset();
    place(-1073741825);
    codepoints[0] = 'X';
    foreground[0] = WHITE + 1;
    draw(face, -1);
    got = count(WHITE);
    if (got < 5) fail("F: setup glyph missing", got, 5);
    codepoints[10 * 1] = 'X';
    foreground[10] = WHITE + 1;
    draw(face, 10);
    if (count(WHITE) < got) fail("F: damaged redraw erased text under the wallpaper", count(WHITE), got);

    /* A moving graphics placement must clear its old rectangle and paint the
       new one within one surface-local atomic damage region. */
    reset();
    place(-1073741825);
    images[0].y = 5 * CH;
    images[0].x = 0;
    draw(face, -1);
    got = count(RED);
    images[0].x = 4 * CW;
    draw_damage(face, 0, 5, 6, 2);
    if (count(RED) != got)
        fail("H: moved graphic left stale pixels", count(RED), got);

    /* Kitty equal-z references are not ordered by array slot. The higher
       image id is composited later, so protocol allocation order cannot
       change the visible result. */
    reset();
    place_colour(&images[0], 20, 255, 0, 0, 0);
    place_colour(&images[1], 10, 0, 0, 255, 0);
    images[0].z = 0;
    images[1].z = 0;
    draw(0, -1);
    if (pixel_at(1, 1) != RED)
        fail("I: equal-z image ordering is not image-id deterministic",
             (long)pixel_at(1, 1), RED);

    /* Numbered placements coexist. Replacing one must expose the old
       background and paint the new position in the same committed scene. */
    reset();
    place_colour(&images[0], 30, 255, 0, 0, 0);
    images[0].placements[0].used = 1;
    images[0].placements[0].id = 7;
    images[0].placements[0].x = 40;
    images[0].placements[0].y = 0;
    images[0].placements[0].width = 4;
    images[0].placements[0].height = 8;
    images[0].placements[0].z = 0;
    draw(0x102030U, -1);
    if (pixel_at(41, 1) != RED)
        fail("J: numbered placement was not drawn", (long)pixel_at(41, 1), RED);
    images[0].placements[0].x = 80;
    draw(0x102030U, -1);
    if (pixel_at(41, 1) != 0x102030U || pixel_at(81, 1) != RED)
        fail("K: placement replacement left stale pixels",
             (long)pixel_at(41, 1), 0x102030L);
    images[0].placements[0].used = 0;
    draw(0x102030U, -1);
    if (pixel_at(81, 1) != 0x102030U)
        fail("L: placement delete left stale pixels",
             (long)pixel_at(81, 1), 0x102030L);

    printf("KMS_COMPOSE_PASS full=%ld\n", full);
    return 0;
}
