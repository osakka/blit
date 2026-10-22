# ADR0180: retained GPU workstream and renderer ownership decision

- Status: proposed; concrete mechanism/ownership approval belongs to internal128
- Date: 2026-10-22
- Tracking: programme122, architecture125, parity104; implementation126–135

## Context

The owner still reports approximately unchanged dragging after local CPU-cost
reductions through0.2.1. Native KMS builds glyph/image pixels on CPU and copies
the scene to scanout buffers. Existing SDL/GL has retained image textures,
glyph atlases, batching and ping-pong scene targets; it is not an unaccelerated
clone of the native renderer, but still constructs row-level work. The primary
GPU-stage proposal in the source investigation has not been implemented or
accepted on physical workloads. Microbenchmarks cannot establish that result.

## Proposed workstream, not a completed implementation decision

Prioritize retained GPU cell/image/composition work inside the existing
accelerated backend, with one canonical terminal/config/input/frame state.
Preserve existing native/software adapters and atomicity/correctness. No third
GL backend, second terminal model or competing writable presentation config.

Internal128 must prove which retained buffer/resource mechanisms are actually
available with pinned SDL3.4.16 and required GLES capabilities. SDL_RenderGeometry
batching is not proof of persistent GPU cell storage. If necessary, propose
an explicit replacement of accelerated drawing internals within the existing
SDL/GL path, including migration/removal of superseded code; do not mix raw GL
and SDL renderer state without a validated ownership contract.

128 also owns the unresolved relationship between GPU rendering and native/SDL
presentation ownership. ADR0137 retains three first-class paths; this handoff
does not silently downgrade native feature/refresh contracts or authorize dual
DRM-master ownership, speculative cross-context sharing or unconditional worker
threads. Any performance-tier/capability-policy change needs explicit review
and owner agreement. Until resolved, unachieved parity rows remain open.

## Ordering and verification

126 establishes stage-correlated metrics;127 deterministic replay/oracles.
129/130 address independent recovery, real SDL startup and SDL-owned hotplug
under104.131/132 implement retained text/image resources after128's decision;
133 integrates atomic GPU composition;134 addresses measured scheduling and
queue-age costs with valid SDL/GL thread ownership.135 supplies the matched
physical workload and rollout verdict, reusing existing20/21/79/112/120 work.

Preserve old/new/revealed damage, overhang sources, colours/alpha/z bands,
source/destination geometry, resource budgets/lifetime and last-complete-frame
cursor behaviour. Exact image/cell protocol semantics remain authoritative.
Hibr remains the semantic window manager and is not modified by this work.

Each ticket defines test/failure/acceptance requirements. Local software/offscreen
results remain separate from real GPU/scanout/input evidence. Stable589 and
dated canary manifests remain rollback references; no live backend switch or
stable0.2 release is implied by a planning ADR.

See [implementation handoff](../performance-architecture-handoff.md) for the
dependency index. Full task descriptions remain in the internal issue bodies;
the repository does not maintain a competing editable copy of those work orders.
