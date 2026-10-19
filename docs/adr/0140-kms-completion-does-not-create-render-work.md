# ADR 0140: KMS completion does not create new render work

- Status: implemented; full regression passes, physical revalidation pending
- Date: 2026-10-19
- Issues: Blit #101, #104
- Related: ADRs 0118, 0136, 0137

## Evidence

The 0.1.557 native pointer trace contains a submitted frame at relative
362.611ms, a no-op render at 377.867ms, input at 379.436ms and the next
submission at 395.784ms. A no-op advances the ordinary scheduling phase even
though no new output work was accepted. The KMS event service unconditionally
sets `render_pending` after completion; topology reconciliation and deferred
frame retry already mark their own work when needed.

The exploratory workload also has uneven concurrent-HTTP arrival times.
This evidence isolates a candidate scheduling defect, not the sole cause
of the measured cadence shortfall or a physical-input latency result.

## Decision

Keep completion bookkeeping, topology reconciliation and deferred-output retry
in one production event-service helper. Do not create a render request merely
because the previous submitted frame completed. Preserve already pending
pointer/scene work. Topology changes and ready deferred work continue to
request rendering through their existing owners.
Preserve the previous timeout-recovery render request explicitly when the
adapter reports `BLIT_KMS_PRESENTATION_RECOVERING`; this is not an ordinary
successful completion. Do not change the existing modeset recovery policy.

Do not alter atomic scene boundaries, retire damage early, change refresh
targets, redraw periodically or add a second scheduler. An idle completion
must not spend the next visual opportunity before new input arrives.

## Verification

The production-code scheduler fixture delivers a completion without new work
and checks that the pending flag and deadline are unchanged. It also checks
that existing pointer work survives completion. Existing retry, topology,
damage and held-scene fixtures remain required. Demonstrate the regression
before removing the unconditional render request, then repeat the bounded
physical comparison and cadence trace on 10.54. Acceptance remains bounded
by the sustained performance plan, not a local fixture or one short run.

The regression fails before the change with `pending=1 deadline=32666`
instead of the preserved deadline 16000. After removing only the
unconditional render request, it passes on software and offscreen GLES2,
alongside the existing deferred-retry and held-scene checks.
The fixture additionally supplies cached adapter metadata and verifies that
timeout recovery still requests rendering. The first full run was deliberately
stopped during review to add this case; that interrupted run is not a pass.
The fresh full run passes 106 gates, with 7 explicit skips and no failures.
