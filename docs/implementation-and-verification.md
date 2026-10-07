# Implementation and verification contract

This document is the delivery contract for blit. It is normative for the
implementation sequence, test evidence, release gates, and handover. A stage
is not complete because a screen looks correct; it is complete only when its
automated and hardware acceptance criteria pass.

## Non-negotiable invariants

1. The OpenAPI document is the sole public-protocol source of truth.
2. The terminal state engine is the sole owner of cells, cursor state, styles,
   damage, revisions, and PTY resize state.
3. Blit is the sole owner of displays, topology, panes, placement, pointer
   coordinates, focus, and input routes.
4. Hibr is a client and lifecycle participant. It does not calculate pane
   geometry or maintain a second display/surface model.
5. Every mutation is revisioned, bounded, observable, and either committed or
   rejected with a typed problem response.
6. Renderer backends consume read-only frame plans. They never parse PTY
   output or mutate canonical terminal state.
7. TCC builds all first-party production C code. A successful host-compiler
   build is never release evidence.
8. No stage may introduce a parallel implementation of an already accepted
   subsystem.

## Requirement identifiers

### Platform and process

- `BLIT-PLAT-001`: run without X11 or Wayland on a Linux DRM/KMS host.
- `BLIT-PLAT-002`: expose one stable API service over TCP.
- `BLIT-PLAT-003`: support SDL3 for the optional renderer/effects boundary and
  direct DRM/KMS for the correctness-critical text path.
- `BLIT-PLAT-004`: run as a managed non-root service when the host grants the
  required DRM/input permissions.
- `BLIT-PLAT-005`: report backend, display, graphics, audio, and input
  capability failures through diagnostics rather than silently degrading.

### API and state

- `BLIT-API-001`: publish versioned OpenAPI schemas for every public resource,
  request, response, event, and problem.
- `BLIT-API-002`: support discovery of server, displays, topology, graphics,
  audio, input devices, input routes, sessions, surfaces, and diagnostics.
- `BLIT-API-003`: provide revision conflicts and idempotency behavior for
  mutations.
- `BLIT-API-004`: provide a persistent WebSocket stream for terminal, surface,
  input, frame, hardware, and diagnostic events.
- `BLIT-API-005`: keep HTTP and stream resource identities consistent.
- `BLIT-API-006`: bound request size, session count, stream count, surface
  dimensions, route count, and event replay memory.

### Displays and topology

- `BLIT-DISPLAY-001`: discover stable display IDs, bounds, modes, refresh, and
  presentation state; expose the complete mode inventory and active mode.
- `BLIT-DISPLAY-002`: support `1x1`, `2x1`, `1x2`, and `2x2` logical pane
  layouts.
- `BLIT-DISPLAY-003`: map each pane to exactly one display and authoritative
  cell bounds.
- `BLIT-DISPLAY-004`: support explicit display offsets, gaps, and output loss
  without corrupting unrelated panes.
- `BLIT-DISPLAY-005`: never render one session onto two displays unless the API
  explicitly requests that composition.
- `BLIT-DISPLAY-006`: this is a hard timing invariant: schedule each active
  output at its selected mode's maximum supported refresh rate. Blit must not
  intentionally present below that rate under sustained damage or pointer
  motion, and one slow output must not throttle a faster output. Rendering may
  prepare frames faster internally, but visible presentation remains bounded
  by the output's physical page-flip/vblank cadence.

### Terminal and Hibr

- `BLIT-TERM-001`: maintain one canonical PTY-backed terminal grid per
  session/surface.
- `BLIT-TERM-002`: correctly handle UTF-8, printable text, controls, cursor
  movement, erase, scrolling, colors, attributes, alternate screen, and
  resize.
- `BLIT-TERM-003`: accept fragmented escape sequences and reject malformed
  sequences deterministically without memory growth.
- `BLIT-TERM-004`: keep PTY window size, logical surface size, pane size, and
  rendered cell metrics coherent after resize.
- `BLIT-HIBR-001`: launch a primary Hibr desktop through the API.
- `BLIT-HIBR-002`: launch auxiliary `desktop --join` clients into named Blit
  panes without duplicating desktop state.
- `BLIT-HIBR-003`: support documented attach, resume, exit, and recovery
  behavior.
- `BLIT-HIBR-004`: keep the adapter transport-only; no duplicate layout,
  surface, revision, or terminal implementation.

### Input

- `BLIT-INPUT-001`: route keyboard input to the focused live session by
  default.
- `BLIT-INPUT-002`: primary-button selection focuses the pane under the
  pointer.
- `BLIT-INPUT-003`: support explicit keyboard and pointer input routes.
- `BLIT-INPUT-004`: route pointer coordinates through display bounds, pane
  origin, scale, and gaps exactly once.
- `BLIT-INPUT-005`: expose input events with device, session, output, cell,
  button, key, and modifier identity.
- `BLIT-INPUT-006`: define route behavior when a device, pane, or target
  session disappears.

### Presentation and effects

- `BLIT-RENDER-001`: present the same immutable frame-plan semantics through
  direct KMS and SDL3 adapters.
- `BLIT-RENDER-002`: prevent stale pixels, pointer trails, cross-output
  cursors, and partial redraw corruption.
- `BLIT-EFFECT-001`: implement bounded, deterministic CRT effects only after
  text/presentation acceptance passes.
- `BLIT-EFFECT-002`: keep effects downstream of canonical text and topology
  state.

## Delivery sequence and gates

### Gate 0 — repository and toolchain

Deliver: clean documentation map, ADRs, TCC build commands, pinned SDL3 and
libdrm artifacts, reproducible fixture scripts, and no undocumented generated
files.

Evidence:

- OpenAPI parses.
- TCC compiles every first-party C target.
- `git diff --check` passes.
- dependency hashes and build paths are recorded.

### Gate 1 — schema and protocol

Deliver: complete OpenAPI resources, typed problem responses, revisions,
idempotency rules, HTTP routes, and WebSocket event schemas.

Tests:

- schema parse and structural validation;
- every route has success and failure contract tests;
- stale revision, malformed JSON, unknown resource, oversized request, and
  idempotency conflict tests;
- HTTP and WebSocket identity/revision consistency test.

Exit condition: no production route exists that is absent from the schema, and
no schema property is advertised as supported while returning a placeholder
response.

### Gate 2 — canonical terminal engine

Deliver: one PTY-to-grid engine with explicit parser state, damage, cursor,
alternate screen, styles, and resize behavior.

Tests:

- deterministic byte-vector tests for every supported control sequence;
- fragmented-input tests at every byte boundary;
- malformed-sequence and bounded-resource tests;
- golden surface snapshots for colors, attributes, cursor, scrolling, and
  alternate screen;
- PTY `TIOCSWINSZ` tests proving dimensions and redraw agree.

Exit condition: the Hibr desktop conformance workload produces stable surface
snapshots across repeated runs and no renderer is involved in the assertions.

### Gate 3 — presentation adapters

Deliver: direct KMS and SDL3 adapters consuming the same frame plan.

Tests:

- empty, single-surface, multi-surface, damage-only, cursor-only, and full
  redraw frames;
- pixel/framebuffer assertions for known cell fixtures;
- pointer movement with previous-position invalidation;
- renderer failure and output-loss diagnostics.

Exit condition: both adapters produce equivalent logical output and neither
  owns terminal state.

### Gate 4 — topology and geometry

Deliver: authoritative pane layout for all four supported matrices, including
font/grid metrics, offsets, gaps, and display loss behavior.

Test matrix:

| Layout | Displays | Panes | Required clients |
|---|---:|---:|---|
| `1x1` | 1 | 1 | primary |
| `2x1` | 2 | 2 | primary + join |
| `1x2` | 1 or 2 | 2 | primary + join |
| `2x2` | 2 or 4 | 4 | primary + three joins |

For each case assert topology JSON, pane bounds, surface ownership, PTY
dimensions, rendered cell coordinates, and absence of duplicate surfaces.

Exit condition: all matrix cases pass in dummy and VKMS environments, with
physical-host confirmation for at least one multi-monitor arrangement.

### Gate 5 — input and Hibr integration

Deliver: focus, explicit input routes, pointer hit-testing, SGR mouse reports,
keyboard routing, primary/join orchestration, attach, resume, and recovery.

Tests:

- focus transitions by API and primary click;
- explicit keyboard and pointer route precedence;
- route target exit and device disappearance;
- pointer movement/click in every pane of every topology matrix;
- Hibr primary plus joins with keyboard, mouse, resize, redraw, and recovery;
- WebSocket input events correlated with the PTY receiving the bytes.

Exit condition: one Hibr desktop can be controlled across all supported panes
without a second layout or terminal state owner.

### Gate 6 — fault and soak behavior

Deliver: lifecycle state machine, output loss, restart, reconnect, replay,
resource limits, diagnostics, and bounded recovery.

Tests:

- kill/restart primary and join processes;
- terminate sessions during writes and resizes;
- disconnect/reconnect stream clients;
- force replay gaps and verify snapshot repair;
- remove or disable one VKMS output;
- run multi-hour PTY output and pointer-motion soak tests under fixed memory
  limits.

The repeatable bounded precursor is `tools/tests/terminal_soak_gate.rb`. It
must pass with multiple concurrent PTYs, sustained VT/UTF-8 output, snapshots,
resizes, runtime diagnostics, and zero leaked sessions before a longer soak is
accepted as evidence.

Exit condition: every injected failure has a deterministic state transition,
typed diagnostic, and documented recovery path.

### Gate 7 — effects and release

Deliver: bounded CRT/retro effects, optional overlays, packaging, service
unit, security configuration, and handover bundle.

Effects must pass with effects disabled first, then prove bounded frame time,
memory, and deterministic parameters. Effects may never change terminal cells,
focus, routes, or topology.

## Test evidence rules

Every acceptance result must record:

- commit ID;
- TCC version;
- kernel, QEMU, Alpine, SDL3, libdrm, and Hibr versions;
- exact command and environment;
- input fixture and expected result;
- machine-readable result;
- logs and, where relevant, framebuffer/surface snapshots;
- exit status and failure classification.

Evidence is invalid when it depends on manual visual inspection alone, an
unrecorded host configuration, an unaudited binary, or a test process that
continues after the claimed result.

## Handover package

The final handover must contain:

1. the pinned source commit and clean working tree status;
2. OpenAPI schema and generated/validated contract report;
3. TCC build instructions and dependency manifest with hashes;
4. server binary build recipe and deployment/service configuration;
5. Hibr adapter/module and exact primary/join launcher configuration;
6. QEMU/VKMS fixture kernel, initramfs recipe, and complete matrix results;
7. physical-host KMS test report with GPU, connectors, modes, permissions,
   input devices, and rollback procedure;
8. conformance, integration, fault, and soak test reports;
9. known limitations and explicit non-goals;
10. operator runbook for startup, shutdown, authentication, logs, recovery,
    and safe display-manager restoration.

## Release decision

The release is blocked if any requirement above lacks evidence, any matrix
case is skipped, any advertised API behavior is a stub, any renderer owns
canonical state, or any Hibr path requires manual geometry or undocumented
environment assumptions.
