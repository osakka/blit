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
static unsigned char image_pixels[4 * 8 * 3];
static BlitGraphicsImage images[BLIT_GRAPHICS_MAX_IMAGES];
static uint32_t front_pixels[W * H];
static uint32_t back_pixels[W * H];

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

static void draw(uint32_t default_background, int damage_column)
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
        view.damage_y = 5;
        view.damage_width = 1;
        view.damage_height = 1;
    }
    draw_view(&output, NULL, NULL, &view);
}

int main(void)
{
    long full;
    long got;
    uint32_t face = 0x101820U;

    buffer_coherence_gate();
    scene_copy_gate();

    output.mode.hdisplay = W;
    output.mode.vdisplay = H;
    output.back.map = pixels;
    output.back.pitch = W * 4;
    output.back.size = sizeof(pixels);

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

    printf("KMS_COMPOSE_PASS full=%ld\n", full);
    return 0;
}
