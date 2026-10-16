# ADR 0090: full-frame composition has an explicit workload budget

- Status: accepted
- Date: 2026-10-16
- Scope: SDL composition timing, performance evidence, and scene-cache policy

## Context

The live SDL3/OpenGLES2 backend cannot be assumed to preserve the contents of
a render target between passes. Blit therefore uses a complete scene rebuild
for changed terminal content on that backend (ADR 0070). That is the correct
pixel policy, but a complete redraw needs a measured workload budget rather
than a subjective claim that it is fast enough.

## Options considered

1. Re-enable the scene cache on OpenGLES2 to hide composition cost. This can
   reintroduce stale pixels because target persistence has not been proven.
2. Measure only a small terminal or rely on the configured interval. Neither
   exercises the full scene nor proves a presented frame met its deadline.
3. Keep full redraws, run a bounded full-surface workload, and assert both
   the refresh/deadline result and a named render-time threshold.

## Decision

Option 3. The timing gate accepts `BLIT_GATE_SURFACE_WIDTH` and
`BLIT_GATE_SURFACE_HEIGHT` and writes a bounded full-surface workload. A
physical release acceptance must run it against the actual endpoint with
authentication, a minimum refresh rate, and an explicit
`BLIT_GATE_MAX_RENDER_MS` threshold. For the supplied 60 Hz host the release
threshold is 17.000 ms: it is deliberately close to the 16 ms scheduler
target while allowing the measured fractional-vblank and timestamp overhead.
The authoritative cadence assertion remains zero missed deadlines.

The gate's local smoke default is 20 ms because dummy SDL is not the shipping
renderer. It is not a physical acceptance claim. A higher-refresh host must
provide its own threshold and evidence; this ADR does not promote the 60 Hz
number to 120/240 Hz hardware.

The OpenGLES2 scene cache remains disabled. Re-enabling it requires a separate
backend persistence gate and a budget result showing that full composition is
insufficient.

## Consequences

The performance claim is reproducible and includes the workload dimensions,
refresh, missed deadlines, and worst render time. The present path may still
report a render time slightly above the integer scheduler interval without a
missed deadline; that is visible rather than hidden. The cache cannot be
reintroduced merely because a future machine is slower.

## Evidence

The local timing gate passes with TCC-built Blit and the 20 ms smoke budget.
On `192.168.10.54`, the authenticated 240×135 full-surface workload passed:

```text
FRAME_RATE_PASS hz=59.916 min_hz=55.000 max_hz=75.000 frames=30 window_ms=500 age_ms=0 window_max_render_ms=16.789 render_budget_ms=17.000 target_interval_ms=16.000 window_missed_deadlines=0
```

The same workload failed with a 14 ms threshold at 16.848 ms, demonstrating
that the budget assertion is active. Hibr source was not modified.
