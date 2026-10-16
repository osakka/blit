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
