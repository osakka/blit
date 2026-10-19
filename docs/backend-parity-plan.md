# Presentation backend parity plan

Status: implementation in progress, 2026-10-19. Physical acceptance remains open.

Tracking: [Blit #104](https://git.home.arpa/itdlabs/blit/issues/104).
Decision: [ADR0137](adr/0137-presentation-backend-feature-and-performance-parity.md).
Acceptance inventory: [backend parity matrix](backend-parity-matrix.md).

## Contract and baseline

Keep three first-class presentation paths: direct DRM/KMS; SDL3 using the
KMSDRM video driver with the software renderer; and SDL3 using KMSDRM with
OpenGLES2. “SDL KMS” alone is ambiguous and must not identify a test result.
Record backend, video driver, renderer, synchronization method, build identity,
output modes and topology for every run.

All three must implement the same published terminal, graphics, input,
configuration and display-lifecycle behavior. One terminal state, input owner,
geometry calculation, scene ordering and damage contract remain authoritative.
Backend adapters own resource allocation, drawing and presentation only.
Common features cannot silently disappear on a selected backend. Runtime
capabilities must describe actual support and failures explicitly.

Finish issue #101's physical pointer diagnosis first. The current direct-KMS
implementation is a comparison reference, not proof of refresh-rate performance.
Freeze a tested package and evidence bundle once that diagnosis is resolved.
Keep this baseline available for rollback throughout SDL work.

## Ordered implementation

1. Inventory every published operation against all three paths. Cover text and
   wide glyphs; font selection/scaling; Kitty and sixel placement/deletion,
   clipping, scrolling, transparency and layer order; overlays/effects; pointer
   appearance; keyboard/buttons/wheel/modifiers; pane geometry/rotation;
   primary/join routing; persistence; screenshots; hotplug; and shutdown.
   Mark unsupported and untested cases individually. Track gaps under the
   parity issue, linking existing #22, #79, #100 and #101 rather than duplicating
   their implementation tasks. Include SDL features absent in direct KMS:
   the acceptance contract is bidirectional.
2. Extend the existing OpenAPI diagnostics before implementation where needed.
   Expose per-output scene/input revisions, requested/submitted/completed work,
   deferrals, missed deadlines, queue age and compose/upload/present durations.
   Label SDL present return separately from confirmed physical presentation;
   unsupported completion timestamps are unavailable, never fabricated. Reuse
   `/v1/diagnostics/observation` and existing focused endpoints/serializers.
3. Drive each adapter from the same completed scene and accumulated damage.
   Preserve damage until its output accepts the frame. Keep pointer updates
   over the last complete scene responsive while scene commits wait. Exercise
   SDL's existing ping-pong textures (ADR0099), stale image removal and resource
   failure recovery before optimizing. Add shared helpers only where they
   remove duplicated policy; retain backend-specific drawing primitives.
4. Measure SDL compose, upload and blocking present time per output. Service
   input, PTYs, API and output deadlines fairly. Remove serialization that
   makes a slow output reduce another output's cadence. Use measured capability
   probes to choose synchronization; a worker/context change requires its own
   ADR and explicit SDL thread-ownership analysis. Prefer bounded damage,
   cached glyphs/images and pointer-only composition. Never reduce refresh,
   omit pending damage or insert periodic full redraws to conceal a defect.
5. Close SDL connector discovery at SDL's ownership boundary (ADR0129).
   Inspect the pinned dependency and released alternatives; use a verified
   upstream implementation if available, otherwise maintain a version-pinned
   local patch with connector lifecycle tests. SDL owns real display objects;
   Blit's existing reconciler owns saved bindings/panes and sessions. Preserve
   unrelated outputs, handle zero displays, and reject stale output-generation
   events. Document any dependency change before editing it.
6. Close remaining matrix gaps individually, including effects where the
   direct path currently lacks them. Each change includes its regression,
   schema/docs updates and evidence; commit and push each completed cycle.

## Acceptance and performance

Use one deterministic workload/replay runner parameterized by backend, with
backend-independent expected images and input assertions. Compare complete
frames and intermediate committed frames against an independent full redraw
oracle. Mask only declared time-varying cursor/effect pixels. Require zero
unexplained pixel differences, stale pixels, dropped input transitions or
incorrect hit-test coordinates. Test against the pre-fix build to demonstrate
that each new regression can detect its defect.

Run text/half-block, Kitty and sixel workloads separately and together;
rapid movement/deletion; simultaneous PTY and input traffic; held scene
barriers; screenshots under load; fractional scales; pane edges; failed
allocation/present; disconnect/reconnect and KVM input recovery. A dummy SDL
pass is scheduler/software evidence only. Run actual GLES2 gates on a GPU.

On 10.54, replay the same fixed workload for each backend at one physical
output, then the current two physical outputs/three panes. Preserve each
output's mode and saved bindings. Collect a 10-second warmup followed by
60-second measurements for pointer-only, text motion, wallpaper/window motion,
and concurrent graphics/input. Record per-output cadence and p50/p95/p99
latency, missed deadlines, CPU and memory, with captures tied to revisions.
Repeat each workload three times; retain every result.

ADR0006 governs the target: service pending visual work at each output's
active refresh independently. A lower-rate producer does not justify replaying
unchanged frames. For a continuous producer at output rate, require at least
99% of target presentation opportunities, no consecutive missed opportunities,
and p95 accepted-input-to-submission latency within two output intervals.
SDL must also remain within one output interval of the measured direct-KMS
p95 under the identical workload. Failure blocks performance acceptance; these
are test tolerances, not permission to lower the configured target cadence.
Report producer/input acceptance, submission and scanout as separate stages.

Validate 120/240 Hz only on hardware that exposes those active modes; keep
those acceptance rows open otherwise (ADR0044). Virtual timing cannot close
physical scanout acceptance. Physical clicks/dragging corroborate replay
tests and must be recorded separately.

## Rollout and boundaries

Keep the current live renderer during development. Switch backend explicitly
through the existing authenticated API/update mechanism only for bounded
comparison windows; verify exact runtime identity and restore the baseline
after a failed gate. Preserve one primary and its login-owned joins. No SSH
or Hibr modifications are part of this work.

Each release records the tested source commit/package checksum, configuration,
hardware, result and unresolved matrix rows in the verification ledger.
Feature parity closes only after every required matrix row passes on each
path. Performance acceptance is stated per tested hardware/mode combination.
