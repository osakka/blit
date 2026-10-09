# ADR 0053: Expose surface-producer cadence separately from presentation cadence

- Status: accepted
- Date: 2026-10-09

## Context

Blit has two different rates that are easy to confuse during a physical
troubleshooting session:

1. presentation cadence: when the renderer submits a changed scene to an
   output; and
2. surface-producer cadence: when a PTY or external Hibr adapter commits new
   surface state.

An output can be presenting at its display refresh with zero missed deadlines
while an Hibr desktop is only producing changed frames intermittently. A
single frame-rate number cannot identify that boundary, especially after the
producer becomes idle and the presentation-rate measurement ages out.

## Decision

Runtime diagnostics expose a bounded, rolling surface-commit measurement:
`surface_commit_hz`, the number of committed surface updates in the latest
completed window; its frame count, window duration, sequence, age, and last/max
inter-commit gaps are exposed alongside the existing presentation counters.

The measurement is producer-side evidence. It does not claim physical scanout,
and a zero rate after the bounded age means the producer is idle or its
measurement is stale, not that the display stopped refreshing.

## Consequences

- A client can distinguish a slow Hibr/PTY producer from a renderer or scanout
  problem using one authenticated runtime endpoint.
- Existing presentation counters and per-output missed-deadline counters remain
  unchanged and retain their original meaning.
- The fields are additive to API version 0.1; clients must ignore unknown
  response fields as already required by the compatibility rules.
- The measurement is bounded and does not retain surface contents or PTY data.

## Verification

- API schema and terminal conformance require all producer-cadence fields.
- A TCC-built server reports the fields during the full terminal suite and
  bounded soak.
- QEMU/VKMS Hibr lifecycle and four-output content gates continue to pass.
- Physical acceptance still requires the exact deployed binary and host
  evidence; this ADR does not promote virtual results to hardware claims.
