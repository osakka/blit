#ifndef BLIT_POINTER_SHAKE_H
#define BLIT_POINTER_SHAKE_H

/* ADR 0234: shake to find the pointer. A pure detector and easing, fed
   positions and times, so a gate can drive it with synthetic times.

   Shaking is measured over the last BLIT_SHAKE_WINDOW_US: the length of the
   pointer's path against the diagonal of the box it covered. A straight
   sweep, however fast, has a ratio near 1; a shake goes back and forth over
   a small box, and its ratio rises with how hard it is shaken. Only turning
   points are kept: a point that continues the last segment's direction on
   both axes (1 px either way counts as any direction) replaces the last
   one. A box under BLIT_SHAKE_MIN_DIAGONAL pixels is a jitter, not a shake.

   The pointer's scale follows: 1 below the threshold, growing
   continuously with the ratio above it, up to the most the settings allow.
   It rises quickly (BLIT_SHAKE_ATTACK_US) and eases back as the shaking
   calms, over the user's shrink time (shrink_ms: about 95% of the way
   back in that time, a time constant of a third of it) -- the history ages out of
   the window, so the ratio falls on its own. Nothing here costs anything
   while the scale is 1 and nobody shakes. */

#include <math.h>
#include <stdint.h>

#define BLIT_SHAKE_POINTS 64
#define BLIT_SHAKE_WINDOW_US 750000ULL
#define BLIT_SHAKE_MIN_DIAGONAL 60.0f
#define BLIT_SHAKE_ATTACK_US 60000.0
#define BLIT_SHAKE_RAMP 3.0f        /* ratio above the threshold for the most */

typedef struct {
    int enabled;
    int max_px;                     /* the largest the pointer grows to */
    int sensitivity;                /* 1 (a hard shake) .. 10 (a light one) */
    int shrink_ms;                  /* time to ease back, BLIT_SHAKE_SHRINK_MIN..MAX */
} BlitShakeConfig;

#define BLIT_SHAKE_SHRINK_MIN 150
#define BLIT_SHAKE_SHRINK_MAX 3000

typedef struct {
    float x[BLIT_SHAKE_POINTS], y[BLIT_SHAKE_POINTS];
    uint64_t t[BLIT_SHAKE_POINTS];
    int n;
    float scale;                    /* drawn: eased toward target */
    float target;
    uint64_t ticked_us;
} BlitShake;

static BlitShakeConfig blit_shake_default(void)
{
    BlitShakeConfig c = {1, 160, 5, 900};
    return c;
}

static void blit_shake_init(BlitShake *s)
{
    s->n = 0;
    s->scale = s->target = 1.0f;
    s->ticked_us = 0;
}

/* Forget the history (a button pressed, a warp, the pointer locked or
   hidden): what is grown eases back from here. */
static void blit_shake_reset(BlitShake *s)
{
    s->n = 0;
    s->target = 1.0f;
}

/* The ratio above which the pointer grows, from the sensitivity. */
static float blit_shake_threshold(const BlitShakeConfig *c)
{
    int s = c->sensitivity < 1 ? 1 : c->sensitivity > 10 ? 10 : c->sensitivity;
    return 4.5f - 0.3f * (float)s;
}

static void blit_shake_prune(BlitShake *s, uint64_t now)
{
    int keep = 0, i;
    while (keep < s->n && now - s->t[keep] >= BLIT_SHAKE_WINDOW_US) ++keep;
    if (keep == 0) return;
    for (i = keep; i < s->n; ++i) {
        s->x[i - keep] = s->x[i];
        s->y[i - keep] = s->y[i];
        s->t[i - keep] = s->t[i];
    }
    s->n -= keep;
}

/* The path length over the box's diagonal, 0 for a box too small. */
static float blit_shake_ratio(const BlitShake *s)
{
    float path = 0, minx, maxx, miny, maxy, w, h, d;
    int i;
    if (s->n < 3) return 0;
    minx = maxx = s->x[0];
    miny = maxy = s->y[0];
    for (i = 1; i < s->n; ++i) {
        float dx = s->x[i] - s->x[i - 1], dy = s->y[i] - s->y[i - 1];
        path += sqrtf(dx * dx + dy * dy);
        if (s->x[i] < minx) minx = s->x[i];
        if (s->x[i] > maxx) maxx = s->x[i];
        if (s->y[i] < miny) miny = s->y[i];
        if (s->y[i] > maxy) maxy = s->y[i];
    }
    w = maxx - minx;
    h = maxy - miny;
    d = sqrtf(w * w + h * h);
    return d < BLIT_SHAKE_MIN_DIAGONAL ? 0 : path / d;
}

/* The target scale for the history now, base the pointer's own size. */
static void blit_shake_aim(BlitShake *s, const BlitShakeConfig *c, int base_px)
{
    float most = base_px > 0 ? (float)c->max_px / (float)base_px : 1.0f, over;
    if (!c->enabled || most <= 1.0f) { s->target = 1.0f; return; }
    over = (blit_shake_ratio(s) - blit_shake_threshold(c)) / BLIT_SHAKE_RAMP;
    if (over < 0) over = 0;
    if (over > 1) over = 1;
    s->target = 1.0f + over * (most - 1.0f);
}

static int blit_shake_same_way(float a, float b)
{
    return (a >= -1.0f && b >= -1.0f) || (a <= 1.0f && b <= 1.0f);
}

/* The pointer at (x, y) at time now. */
static void blit_shake_motion(BlitShake *s, float x, float y, uint64_t now,
                              const BlitShakeConfig *c, int base_px)
{
    if (!c->enabled) return;
    blit_shake_prune(s, now);
    if (s->n >= 2) {
        int a = s->n - 2, b = s->n - 1;
        if (blit_shake_same_way(s->x[b] - s->x[a], x - s->x[b]) &&
            blit_shake_same_way(s->y[b] - s->y[a], y - s->y[b])) {
            s->x[b] = x;
            s->y[b] = y;
            s->t[b] = now;
            blit_shake_aim(s, c, base_px);
            return;
        }
    }
    if (s->n == BLIT_SHAKE_POINTS) {
        int i;
        for (i = 1; i < s->n; ++i) { s->x[i - 1] = s->x[i]; s->y[i - 1] = s->y[i]; s->t[i - 1] = s->t[i]; }
        --s->n;
    }
    s->x[s->n] = x;
    s->y[s->n] = y;
    s->t[s->n] = now;
    ++s->n;
    blit_shake_aim(s, c, base_px);
}

/* Time passes: the history ages, the target follows it, and the scale eases
   toward the target. Returns 1 while there is anything to animate. */
static int blit_shake_tick(BlitShake *s, uint64_t now, const BlitShakeConfig *c, int base_px)
{
    double dt, tau;
    /* At rest -- not grown, not growing -- nothing to do: a rise only ever
       comes from a motion, so ordinary motion asks for no ticks. */
    if (s->scale == 1.0f && s->target == 1.0f) {
        s->ticked_us = now;
        return 0;
    }
    blit_shake_prune(s, now);
    blit_shake_aim(s, c, base_px);
    dt = s->ticked_us && now > s->ticked_us ? (double)(now - s->ticked_us) : 0.0;
    s->ticked_us = now;
    if (s->target > s->scale) {
        tau = BLIT_SHAKE_ATTACK_US;
    } else {
        int ms = c->shrink_ms < BLIT_SHAKE_SHRINK_MIN ? BLIT_SHAKE_SHRINK_MIN :
                 c->shrink_ms > BLIT_SHAKE_SHRINK_MAX ? BLIT_SHAKE_SHRINK_MAX : c->shrink_ms;
        tau = ms * 1000.0 / 3.0;
    }
    s->scale += (float)((s->target - s->scale) * (1.0 - exp(-dt / tau)));
    /* At rest once the drawn size can no longer differ from the base: no
       ticks spent easing what nobody can see. */
    if (s->target == 1.0f && (s->scale - 1.0f) * (float)base_px < 2.0f) s->scale = 1.0f;
    return s->scale != 1.0f || s->target != 1.0f;
}

/* The size to draw at for the scale: the base at rest, else in steps of
   4 px, so a level's art is built once and reused. */
static int blit_shake_size(const BlitShake *s, int base_px)
{
    int px;
    if (s->scale <= 1.0f) return base_px;
    px = (int)((float)base_px * s->scale + 0.5f);
    px = base_px + (px - base_px) / 4 * 4;
    return px < base_px ? base_px : px;
}

#endif
