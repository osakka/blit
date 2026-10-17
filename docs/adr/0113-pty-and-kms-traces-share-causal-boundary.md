# ADR 0113: PTY updates and direct-KMS presents share a causal trace boundary

- Status: accepted
- Date: 2026-10-17
- Scope: diagnostics for Hibr PTY output and direct DRM/KMS presentation

## Context

The live artifact report is an observation about a running Hibr desktop, but
the previous diagnostics exposed only aggregate PTY byte counters and SDL
render-path events. The direct-KMS path—the production path on the supplied
host—did not identify which surface damage was submitted to which page-flip.
That made it impossible to distinguish a terminal damage-accounting defect
from a KMS scheduling or capture problem without guessing.

## Decision

Keep the existing API and bounded trace endpoint, but record two additional
causal boundaries:

1. each PTY read that changed a terminal surface records the session, surface
   revision, damage rectangle, cursor state, frame-batch state, and whether the
   session is synchronized;
2. each direct-KMS present records the output masks, view count, present time,
   pointer state, and the first four submitted views' revisions, damage,
   cursor, and synchronization state.

The records use the existing `surface_update` and `render_path` trace kinds, so
no API or schema version changes are required. Trace entries remain bounded;
the critical render ring is used for KMS path records and the ordinary surface
ring is used for PTY records. This is observability only: it does not change
Hibr, terminal parsing, damage decisions, scheduling, or page-flip behavior.

## Consequences

A live capture can now correlate:

`PTY bytes -> surface revision/damage -> KMS submitted/deferred mask -> scanout`

If the chain stops at PTY, the terminal producer/parser boundary is at fault;
if it reaches KMS but is deferred, refresh scheduling is implicated; if it is
submitted with the expected damage and the screenshot is still wrong, the
remaining investigation is in composition or capture. The trace is evidence,
not a visual acceptance claim; an authenticated Hibr desktop and screenshot
are still required for that claim.

## Evidence

- `GET /v1/diagnostics/trace?kind=surface_update&limit=32`
- `GET /v1/diagnostics/trace?kind=render_path&limit=32`
- `RUN_GATES=1 tools/run-gates.sh renderer terminal` passes 50/50 gates.
