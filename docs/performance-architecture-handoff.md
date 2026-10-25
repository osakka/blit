# Rendering architecture work: implementation handoff

Date: 2026-10-22. Scope: the architectural rendering/performance programme,
not an audit or closure of every other open Blit backlog item.

The internal Gitea issue bodies are the implementation work orders and contain
their full scope, code starting points, prerequisites, tests, acceptance and
non-goals. Numbers below are **internal Blit issue IDs**, not GitHub issue IDs;
do not manufacture corresponding GitHub issue links. Public code/docs live at
[osakka/blit](https://github.com/osakka/blit).

## Read first

- [Current state](current-state.md): dated host/version/config observations.
- [Verification ledger](verification-status.md): what passed, failed or remains
  unverified; local fixtures are not physical movement acceptance.
- [Performance programme](performance-0.2.md) and
  [source investigation](kitty-rendering-investigation.md).
- [Backend parity plan](backend-parity-plan.md),
  [matrix](backend-parity-matrix.md) and
  [ADR0137](adr/0137-presentation-backend-feature-and-performance-parity.md),
  superseded by [ADR 0184](adr/0184-sdl-is-removed.md), which removed SDL.
- [Proposed retained-rendering decision](adr/0180-retained-gpu-implementation-workstream.md):
  implementation mechanism and native ownership decision remain open in128.

## Checkpoint, not a performance verdict

Last deployed canary:0.2.1, runtime source7ca0dbc, identity in
[release manifest](../releases/0.2.1.yaml). Stable rollback tag:v0.1.589;
0.2.0 and589 archives have been retained locally. Discover current API version,
saved settings, Hibr/hold identity and output inventory before any experiment.
The last recorded Hibr runtime is0.99.285/ABI16, adapter pinned to compatible
0.99.225/ABI16. This is dated information, not permission to upgrade Hibr.

Already implemented: bounded glyph reuse (ADR0176), canonical last-composed
placement/producer damage (ADR0177), linear image ingestion/direct existing
RGBA submission (ADR0178), exact native blend arithmetic (ADR0179), and DECCRA
(ADR0175). Do not recreate these from the original historical issue wording.
Local blend gains and113 font/renderer checks do not establish a dragging win.
The owner reports approximately unchanged dragging; no matched end-to-end
performance acceptance exists.

Native KMS composes with GLES, with the CPU compositor as its fallback; SDL
was removed in ADR 0184. Existing damage and last-complete-frame safety must
not be removed merely because a GPU design is desired.
Hibr owns semantic desktop windows; Blit sees terminal updates/image placements,
not a new window-manager scene. Preserve that boundary.

The most recent observation lost the HDMI output after initially reporting
one primary/two joins. Cause is unconfirmed; do not infer a regression or a
successful unplug test. Confirm actual connected displays and freeze inventory
for comparison. Idle desktop, Matrix saver and active video/dragging are
different workloads. Existing last-frame counters can repeat stale samples.

## Work breakdown and dependencies

| Internal issue | Deliverable | Prerequisites / integration |
| --- | --- | --- |
|126 | Revision-correlated frame/latency observability | Extends existing observation/schema; no second diagnostics owner |
|127 | Deterministic protocol/input/video replay and independent pixel oracle |126 for instrumented acceptance; fixtures can start immediately |
|128 | Retained GPU scene/resource contract and tested capability slice |126/127; owns the explicit GL/native ownership decision |
|129 | Bounded real-GPU SDL/KMSDRM startup and independent multi-output presentation | Obsolete: SDL removed in ADR 0184 |
|130 | SDL-owned connector hotplug/replug and stable Blit reconciliation | Obsolete: SDL removed in ADR 0184 |
|131 | Retained cell/glyph resources and changed-range updates |128/127; integrate with133 |
|132 | Generation-aware image/video resource updates and lifetime |128/127; extend existing cache, integrate with133 |
|133 | Atomic retained GPU pane/layer composition |128/131/132; coordinate134 |
|134 | Fair input/PTY/API service and independent output cadence |126/128/127; coordinate133/130, reuse95/105 |
|135 | Matched real-host benchmarks, parity signoff and recoverable rollout |126–134; reuse104/20/21/79/112/120 |

Start126/127. Resolve128 before committing to dependent rendering interfaces.
Text/image implementation lanes can proceed in parallel after their
prerequisites.
Composition and scheduling converge before135's physical acceptance.

The ten issues also have24 native Gitea dependency edges, verified by API
read-back against their declared prerequisites. These are completion/integration
gates; a ticket may explicitly allow fixture/design work to start before its
instrumented acceptance prerequisite is complete.

## Existing parent and related ownership

- 122: programme and outcome signoff; links the complete work breakdown.
- 125: architecture implementation epic; completed increments are distinguished
  from remaining GPU/physical work.
- 104: three-path feature/presentation parity; obsolete since ADR 0184 left
  one presentation path.
- 120: implemented DECCRA's live Hibr/hold negotiation/forwarding and solid-drag
  acceptance; upstream release metadata is not a running-hold result.
- 109: independent startup recovery; do not rely on an API that may never
  start.
- 112: release/security/reproducibility/recovery; not replaced by135.
- 20/21/79: physical multi-output, high-refresh hardware and actual GPU evidence.
- 100/105/95: native hotplug, HTTP fairness and loop-stall evidence respectively.

These parent bodies were refreshed on2026-10-22. Their original descriptions
are retained under clearly labelled historical sections; none were closed.
Other unrelated backlog issues were not changed by this handoff.

## Implementation and acceptance rules

One canonical terminal/grid, image store, geometry, input/config owner and
atomic frame contract. Backend caches are derived resources, not alternative
terminal implementations. The retained-GPU mechanism is a proposal to resolve
in128, not approval for a second GL backend beside the KMS GLES compositor.
Keep the installed TCC boundary. No Hibr edits or SSH, and no silent live
backend/font/layout/session changes. Credentials stay in operator-provided
secret channels, never issues, examples, recordings or evidence artifacts.

Never trade correctness for speed: preserve Arabic/emoji/wide/overhang/alpha/
layering, bounded lifetimes, input edges, terminal bytes, deferred damage and
last-complete-frame pointer presentation. Do not insert full redraws to hide
damage or lower configured refresh to make a graph look better. Decisions
about workers, capability requirements or native performance tiers require an
explicit reviewed ADR and any necessary owner agreement.

135 reuses104's existing policy: for continuous work at target rate, at least
99% of target opportunities, no consecutive missed opportunities, p95 accepted
input-to-submission within two output intervals, plus the relative native
comparison. Native slowness does not waive the absolute criterion. Record
all backend/hardware rows honestly; an owner-approved future policy change
must be explicit, not silently applied by the implementation. Warmup10s,
measurement60s, three repeats per case. Count Blit/Hibr/hold and compositor/GPU
work; separate produced, accepted, committed, submitted and completed frames.
Test120/240Hz only where actual hardware exposes those modes. Unavailable
hardware or completion metrics remain pending/unavailable, not simulated passes.

## Reproducible starting points

Useful checked-in commands (scope is documented by each tool):

```bash
ruby tools/tests/graphics_placement_damage_gate.rb
ruby tools/tests/graphics_ingestion_gate.rb
ruby tools/tests/raster_blend_gate.rb --benchmark
sh tools/run-gates.sh font renderer gles
```

Use the existing live API observation/probes only with current operator-supplied
credentials and an explicitly scoped trial. Rehearse independent recovery before
a hazardous startup and respect authenticated-session restrictions. Do not
replace occupied desktop panes to obtain an easy synthetic benchmark.

Ignored `build/tests` captures/logs are local evidence, not a portable dependency.
Another engineer should reproduce from maintained fixtures/commands and attach
redacted source/config/workload identities plus results to the owning issue.
Each completed cycle includes its regression, accurate evidence, schema/docs/
ADR update, coherent commit and push. No stable0.2 tag or universal Kitty
performance claim is authorized by this handoff.
