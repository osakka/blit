# ADR 0137: One feature contract across presentation backends

- Status: planned; implementation and acceptance pending
- Date: 2026-10-19
- Issue: [Blit #104](https://git.home.arpa/itdlabs/blit/issues/104)
- Related: ADRs0004,0006,0044,0099,0129,0130,0136

## Context

The user requires SDL/KMSDRM software and OpenGLES2 to deliver the features
and performance expected of direct DRM/KMS. Existing backend-specific gates
and startup readiness do not establish equivalent physical behavior. SDL
connector discovery is also a known dependency gap. Direct KMS currently has
an unresolved physical-motion diagnosis, so it is not an accepted performance
baseline merely because it is the reference implementation.

## Decision

Use one published API/terminal/input/geometry/scene contract for all three
presentation paths. Keep drawing and resource ownership in their respective
adapters. Implement the bidirectional feature matrix and measured acceptance
sequence in the [backend parity plan](../backend-parity-plan.md).

Presentation mechanisms may differ; observable feature behavior must agree.
Expose actual backend identity and timing evidence without equating an SDL
present call's return with a completed physical scanout. Active-mode refresh
remains the scheduling target under ADR0006. The plan's measurement tolerances
are release gates, not a lower configured cadence or a universal guarantee.

## Alternatives and consequences

- A second terminal/compositor policy per backend would duplicate authority
  and perpetuate divergent damage/input handling; rejected.
- Software/dummy-only acceptance cannot validate GLES2 or physical timing;
  require matching real-driver runs.
- Removing direct KMS would discard the current reference and does not solve
  SDL lifecycle or timing gaps; retain all three paths.
- Seamless SDL hotplug requires verified SDL-owned connector lifecycle;
  follow ADR0129's dependency boundary rather than a parallel DRM inventory.

No runtime or API compatibility change occurs in this documentation cycle.
Future schema additions are additive and implemented schema-first. Existing
selection/settings remain valid; any fallback is observable in runtime identity.

## Verification

Shared differential image/input tests, GPU-backed GLES2 runs, and identical
bounded one-/multi-output workloads on10.54 must pass. Each acceptance claim
names package, hardware, modes and workload. High-refresh rows remain open
until matching physical evidence exists. Follow the plan's rollout and
rollback procedure and preserve the existing live configuration meanwhile.
