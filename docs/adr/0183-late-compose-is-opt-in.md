# ADR 0183: late compose is opt-in; immediate is the default

- Status: accepted; in use since 0.2.21
- Date: 2026-10-25
- Scope: native KMS frame scheduling, `server.kms_frame_timing`

## Context

Under continuous change a native KMS frame is composed as soon as the
previous flip completes, right after a vblank, and waits most of a refresh to
be shown (~15.7 ms on the host). 0.2.20 added late compose: each output's
frame is composed a lead before its next vblank, predicted from the kernel's
vblank timestamps, with the lead adapting to late flips and to compose slots
the loop was busy through. Locally (DRM mock, periodic vblanks) it cut the
wait from ~15 ms to 4–8 ms at the same frame rate.

On the host the owner dragged a window under each mode and found immediate
smoother. Two causes:

1. Late flips were judged by `vblank timestamp − submission > one period`.
   A real driver stamps a flip event with the start of the new frame's
   scanout, after the vblank began, so on-time flips were counted late; the
   lead was driven to its cap (half a refresh) and the schedule kept missing
   real vblanks. The mock stamps the vblank boundary exactly, so the gate did
   not see it.
2. Late compose is structurally more sensitive to main-loop stalls: a stall
   through the slot costs a frame that immediate, with a whole refresh of
   slack, keeps. On a loaded build machine that is a few percent of frames.

## Decision

- `immediate` is the default for `server.kms_frame_timing` and for
  `BLIT_KMS_FRAME_TIMING`; `late` is opt-in and applies live.
- A flip is late when it completes two or more vblanks after the CRTC's
  vblank number read right after its submission (`drmCrtcGetSequence`); only
  without that counter does it fall back to "stamped more than 1.5 periods
  after submission", which separates the cases in either mode whatever the
  scanout offset.
- Per-output `flip` diagnostics keep reporting `timing`, `lead_us`,
  `on_time`, `late` and `slot_misses` in both modes, so a later trial of
  `late` on a host is measured, not felt.

## Consequences

Input latency stays as before by default. `late` can be re-trialled on a
host once its loop stalls are understood; the counter-based late detection
removes the false positives that defeated the first trial.

## Evidence

`kms_frame_timing_gate` (native suite): one server, default immediate,
switched live to late under continuous output through the DRM mock's
periodic-vblank mode (which now answers `drmCrtcGetSequence`): flip wait
~15 ms → 4–8 ms, 0 late flips, at least 85% of the frame rate, an invalid
value refused. Host A/B on 0.2.20: late 36 fps with 31 "late" flips (most
false), immediate preferred by the owner.
