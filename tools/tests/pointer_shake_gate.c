/* ADR 0234: the shake detector's behaviour, with synthetic positions and
   times (no display, no sleeps). Prints POINTER_SHAKE_PASS or the first
   failure. */
#include <stdio.h>
#include <stdlib.h>
#include "blit_pointer_shake.h"

static int failures;

static void check(int ok, const char *what, double got)
{
    if (!ok) {
        printf("POINTER_SHAKE_FAIL %s (got %.3f)\n", what, got);
        ++failures;
    }
}

/* Motion along a path at 1 kHz for `ms`, f(t) giving the position; the
   tick run every 16 ms as the loop would. Returns the highest scale seen. */
static float drive(BlitShake *s, const BlitShakeConfig *c, uint64_t *now, int ms,
                   float (*fx)(double), float (*fy)(double))
{
    float most = 1.0f;
    int i;
    for (i = 0; i < ms; ++i) {
        double t = i / 1000.0;
        *now += 1000;
        blit_shake_motion(s, fx(t), fy(t), *now, c, 24);
        if (i % 16 == 0) blit_shake_tick(s, *now, c, 24);
        if (s->scale > most) most = s->scale;
    }
    return most;
}

/* Time passing with no motion. */
static void rest(BlitShake *s, const BlitShakeConfig *c, uint64_t *now, int ms)
{
    int i;
    for (i = 0; i < ms; i += 16) {
        *now += 16000;
        blit_shake_tick(s, *now, c, 24);
    }
}

static float sweep_x(double t) { return (float)(100 + t * 3000); }      /* 3000 px/s, straight */
static float flat_y(double t) { (void)t; return 400; }
static float wander_x(double t) { return (float)(300 + 200 * sin(t * 1.5)); }
static float wander_y(double t) { return (float)(300 + 150 * sin(t * 1.1)); }
static float jitter_x(double t) { return (float)(500 + ((int)(t * 1000) % 2 ? 1.5 : -1.5)); }
static float jitter_y(double t) { return (float)(500 + ((int)(t * 1000) % 3 ? 1.0 : -1.0)); }
static float gentle_x(double t) { return (float)(500 + 70 * sin(t * 2 * 3.14159 * 3)); }   /* ±70 px, 3 Hz */
static float hard_x(double t) { return (float)(500 + 90 * sin(t * 2 * 3.14159 * 7)); }     /* ±90 px, 7 Hz */
static float shake_y(double t) { return (float)(500 + 15 * sin(t * 2 * 3.14159 * 1.3)); }

int main(void)
{
    BlitShakeConfig c = blit_shake_default(), off = blit_shake_default();
    BlitShake s;
    uint64_t now = 1000000;
    float most, gentle, hard;
    off.enabled = 0;

    blit_shake_init(&s);
    most = drive(&s, &c, &now, 400, sweep_x, flat_y);
    check(most == 1.0f, "a fast straight sweep grew the pointer", most);

    blit_shake_init(&s);
    most = drive(&s, &c, &now, 2000, wander_x, wander_y);
    check(most == 1.0f, "slow wandering grew the pointer", most);

    blit_shake_init(&s);
    most = drive(&s, &c, &now, 1000, jitter_x, jitter_y);
    check(most == 1.0f, "a jittering mouse grew the pointer", most);

    blit_shake_init(&s);
    gentle = drive(&s, &c, &now, 1200, gentle_x, shake_y);
    check(gentle > 1.2f, "a moderate shake did not grow the pointer", gentle);

    blit_shake_init(&s);
    hard = drive(&s, &c, &now, 1200, hard_x, shake_y);
    check(hard > gentle + 0.3f, "a harder shake did not grow it more", hard - gentle);
    check(hard <= (float)c.max_px / 24.0f + 0.001f, "the pointer grew past its most", hard);
    check(blit_shake_size(&s, 24) <= c.max_px && (blit_shake_size(&s, 24) - 24) % 4 == 0,
          "the drawn size is not a 4 px step within the most", (double)blit_shake_size(&s, 24));

    rest(&s, &c, &now, 400);
    check(s.scale > 1.0f && s.scale < hard, "it did not ease back smoothly (gone or held at 400 ms)", s.scale);
    rest(&s, &c, &now, 2000);
    check(s.scale == 1.0f && blit_shake_tick(&s, now, &c, 24) == 0,
          "it did not settle back to 1 and idle", s.scale);

    /* The shrink time is the user's: a quick one is back sooner than a
       slow one, from the same shake. */
    {
        BlitShakeConfig quick = blit_shake_default(), slow = blit_shake_default();
        BlitShake a, b;
        uint64_t ta = now, tb = now;
        quick.shrink_ms = 300;
        slow.shrink_ms = 2400;
        blit_shake_init(&a);
        blit_shake_init(&b);
        drive(&a, &quick, &ta, 1200, hard_x, shake_y);
        drive(&b, &slow, &tb, 1200, hard_x, shake_y);
        rest(&a, &quick, &ta, 1200);
        rest(&b, &slow, &tb, 1200);
        check(a.scale == 1.0f, "a quick shrink was not back at 1.2 s", a.scale);
        check(b.scale > 1.5f, "a slow shrink was already back at 1.2 s", b.scale);
        rest(&b, &slow, &tb, 3000);
        check(b.scale == 1.0f, "a slow shrink never settled", b.scale);
        now = ta > tb ? ta : tb;
    }

    blit_shake_init(&s);
    most = drive(&s, &off, &now, 1200, hard_x, shake_y);
    check(most == 1.0f, "switched off, it grew", most);

    blit_shake_init(&s);
    drive(&s, &c, &now, 800, hard_x, shake_y);
    blit_shake_reset(&s);
    check(s.target == 1.0f && s.n == 0, "a reset kept its history", s.target);

    if (failures) return 1;
    printf("POINTER_SHAKE_PASS sweep=1 wander=1 jitter=1 gentle=%.2f hard=%.2f most=%dpx eases-back settles shrink=quick,slow off=1 reset\n",
           gentle, hard, c.max_px);
    return 0;
}
