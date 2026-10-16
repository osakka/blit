# ADR 0101: KMS incremental frames begin from the presented buffer

- Status: accepted
- Date: 2026-10-16
- Scope: direct DRM/KMS page-flip composition

## Context

In the direct KMS double-buffer path, the buffer acquired after a page flip is
the previously displayed buffer. It is not guaranteed to contain the current
complete frame. The old compositor copied only the previous damage rectangle
into that buffer before applying new damage. When damage moved, pixels outside
that rectangle could therefore come from an older frame and remain as trails
or stale scene fragments.

SDL scene composition has a separate ping-pong scene-cache decision in
[ADR 0099](0099-ping-pong-scene-cache-for-sdl-damage.md). Hibr is outside this
change and remains unmodified.

## Decision

Before applying incremental damage to a KMS back buffer, copy the complete
currently displayed front buffer into it. Apply the canonical damage to that
coherent copy, then submit the resulting frame for page flip.

The compositor must not assume that a newly acquired KMS back buffer contains
the last complete scene. The full-frame copy is the correctness boundary;
performance optimization is subordinate to it until an equivalent buffer
coherence guarantee is established.

## Consequences

- Moving damage cannot leave stale pixels solely because the back buffer was
  previously displayed.
- The direct KMS path pays a full-frame copy before incremental composition.
- The SDL cache remains governed by ADR 0099; this ADR does not merge or
  collapse the two presentation paths.
- Physical window dragging and real evdev input remain separate acceptance
  requirements; this decision only establishes frame-buffer coherence.

## Verification

- `tools/tests/kms_compose_gate.sh` passes with `KMS_COMPOSE_PASS full=512`.
- The TCC server build passes with `tools/build-tcc.sh build/blit/blit-server server`.
- The live 0.1.510 direct-KMS motion probe reported no missed deadlines in
  the focused interval and produced coherent before/after captures.
- A physical display capture is still required for final visual acceptance.
