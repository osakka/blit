/* Exercise the actual terminal state's damage accumulators, not a copy. */
#define main blit_server_entry_for_damage_gate
#include "../../src/blit_server.c"
#undef main

static int failures;
static BlitSurface subject;
static BlitSession owner;

static void expect_bounds(const char *name, int x, int y, int w, int h)
{
    if (!subject.damage_valid || subject.damage_x != x ||
        subject.damage_y != y || subject.damage_width != w ||
        subject.damage_height != h) {
        fprintf(stderr, "SURFACE_DAMAGE_FAIL %s got=%d,%d,%dx%d want=%d,%d,%dx%d\n",
                name, subject.damage_x, subject.damage_y,
                subject.damage_width, subject.damage_height, x, y, w, h);
        ++failures;
    }
}

int main(void)
{
    BlitRectangle rectangle;
    int direction;
    subject.width = 80;
    subject.height = 40;
    for (direction = 0; direction < 2; ++direction) {
        int first = direction ? 2 : 60;
        int last = direction ? 60 : 2;
        subject.damage_valid = 0;
        mark_surface_damage(&subject, first, first / 2);
        mark_surface_damage(&subject, last, last / 2);
        expect_bounds("point union in both orders", 2, 1, 59, 30);
        subject.damage_valid = 0;
        mark_surface_damage_width(&subject, first, first / 2, 4);
        mark_surface_damage_width(&subject, last, last / 2, 4);
        expect_bounds("wide union in both orders", 2, 1, 62, 30);
        subject.damage_valid = 0;
        mark_surface_damage_rect(&subject, first, first / 2, 4, 3);
        mark_surface_damage_rect(&subject, last, last / 2, 4, 3);
        expect_bounds("rectangle union in both orders", 2, 1, 62, 32);
    }
    subject.damage_valid = 0;
    mark_surface_damage(&subject, 70, 35);
    rectangle.left = 2; rectangle.top = 1;
    rectangle.right = 5; rectangle.bottom = 3;
    terminal_damage_rectangle(&subject, &rectangle);
    expect_bounds("DEC rectangle preserves prior damage", 2, 1, 69, 35);
    subject.damage_valid = 0;
    mark_surface_damage(&subject, 70, 35);
    subject.scroll_top = 2; subject.scroll_bottom = 8;
    terminal_scroll_up(&owner, &subject);
    expect_bounds("scroll preserves outside damage", 0, 2, 80, 34);
    subject.damage_valid = 0;
    mark_surface_full_damage(&subject);
    mark_surface_damage(&subject, 70, 35);
    mark_surface_damage(&subject, 2, 1);
    expect_bounds("full damage never shrinks", 0, 0, 80, 40);
    if (failures) return 1;
    puts("SURFACE_DAMAGE_PASS points=both-orders wide=both-orders rectangles=both-orders DEC=union scroll=union full=monotonic");
    return 0;
}
