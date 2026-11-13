#ifndef BLIT_POINTER_BUSY_H
#define BLIT_POINTER_BUSY_H

/* ADR 0235: the busy pointer's settings. `enabled` and `after_ms` are what
   blit-compositor is told on each route (`INPUT busy`): a program that has
   not answered a ping for after_ms is busy, and its windows show the busy
   pointer. `spin_ms` is how long the busy pointer takes to go round once,
   wherever it is shown (a busy program, or a program asking `wait` or
   `progress` by OSC 22). */

#define BLIT_BUSY_AFTER_MIN 500
#define BLIT_BUSY_AFTER_MAX 10000
#define BLIT_BUSY_SPIN_MIN 400
#define BLIT_BUSY_SPIN_MAX 3000

typedef struct {
    int enabled;
    int after_ms;
    int spin_ms;
} BlitBusyConfig;

static BlitBusyConfig blit_busy_default(void)
{
    BlitBusyConfig c = {1, 2000, 1000};
    return c;
}

#endif
