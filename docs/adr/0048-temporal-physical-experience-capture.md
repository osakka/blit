# ADR-0048: Temporal physical-experience capture is the troubleshooting boundary

Status: accepted

## Context

Reports such as “the pointer moves but the desktop blinks” combine several
independent boundaries: evdev input, pointer routing, Hibr surface commits,
atomic damage composition, and display presentation. A start/end snapshot or a
human observation cannot identify the first boundary that failed. A framebuffer
screenshot taken during the timing interval can also perturb the single-threaded
render loop.

## Decision

Use `tools/diagnostics/troubleshoot-experience.sh` as the operator boundary for
the real host. During a bounded window it records small runtime samples and
server-monotonic filtered traces. It captures screenshots only after the timing
window. `analyze-troubleshoot.py` emits one first-missing-boundary verdict and
retains the raw evidence for review.

The capture is observational: it does not start, stop, resize, focus, or alter
Hibr. It accepts both physical input and the existing API injection control
experiment, but only `device=evdev-pointer` plus positive evdev counters is
physical-input evidence.

## Consequences

Each physical reproduction has a reproducible directory containing the exact
server version, hardware/input inventory, 250 ms runtime samples, bounded
traces, and post-window screenshots. A screenshot can be compared with the
precise render and input boundaries without claiming that visual timing is a
frame-rate measurement.

The capture itself adds bounded API polling load. It therefore remains a
diagnostic transaction, not a performance benchmark; use the existing
screenshot-free gates for benchmark acceptance.
