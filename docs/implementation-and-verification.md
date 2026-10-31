# Implementation and verification contract

This document states what Blit must do and what counts as evidence that it
does. It is normative for requirements, test evidence and the release
decision. What exists today is in [features](features.md); the evidence for
each release is in the [verification ledger](verification-status.md); the
build-to-deploy steps are in the [development
workflow](development-workflow.md). A change is not complete because a screen
looks correct; it is complete only when its automated and hardware acceptance
criteria pass.

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
6. The compositors consume read-only frame plans. They never parse PTY
   output or mutate canonical terminal state.
7. TCC builds all first-party production C code. A successful host-compiler
   build is never release evidence.
8. No stage may introduce a parallel implementation of an already accepted
   subsystem.

## Requirement identifiers

### Platform and process

- `BLIT-PLAT-001`: run without X11 or Wayland on a Linux DRM/KMS host.
- `BLIT-PLAT-002`: expose one stable API service over TCP.
- `BLIT-PLAT-003`: present through native DRM/KMS only (ADR 0184), composing
  with the GLES compositor (ADR 0181) by default or the CPU compositor.
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
- `BLIT-DISPLAY-002`: split each display into a logical pane grid of 1–8
  columns by 1–8 rows (ADR 0131).
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
  the GLES and CPU compositors.
- `BLIT-RENDER-002`: prevent stale pixels, pointer trails, cross-output
  cursors, and partial redraw corruption.
- `BLIT-EFFECT-001`: keep pane and presentation effects bounded and
  deterministic (ADR 0186–0188, 0190); with effects off, draw exactly what
  no effects draws.
- `BLIT-EFFECT-002`: keep effects downstream of canonical text and topology
  state.

## Acceptance

`tools/run-gates.sh` is the executable list of gates; its suites and the
change classes they prove are in the
[development workflow](development-workflow.md#required-gates). By area:

| Requirements | Local proof | Beyond the DRM mock |
|---|---|---|
| `BLIT-API-*` | `schema` suite | — |
| `BLIT-TERM-*` | `terminal` and `font` suites | Hibr capture |
| `BLIT-RENDER-*`, `BLIT-EFFECT-*`, `BLIT-INPUT-*` | `renderer` suite, on `BLIT_GATE_BACKEND=native` and `gles` | VKMS pixel and input fixtures ([KMS testing](testing-kms.md)) |
| `BLIT-DISPLAY-*` | `renderer` and `timing` suites | VKMS two- and four-output fixtures; a physical multi-monitor host |
| `BLIT-HIBR-*` | `hibr` suite | `tools/tests/qemu_hibr_lifecycle_gate.sh` |
| `BLIT-PLAT-*`, lifecycle and faults | `lifecycle` and `timing` suites | `physical_multi_display_gate.rb` with `BLIT_GATE_LIVE` |

Effects never change terminal cells, focus, routes or topology. The bounded
soak precursor is `tools/tests/terminal_soak_gate.rb`: multiple concurrent
PTYs, sustained VT/UTF-8 output, snapshots, resizes and zero leaked sessions.
The caller supplies `BLIT_SOAK_PID`; the gate enforces
`BLIT_SOAK_MAX_RSS_KIB` (128 MiB by default) throughout the run. A longer
soak is evidence only after it passes.

## Test evidence rules

Every acceptance result must record:

- commit ID;
- TCC version;
- kernel, QEMU, Alpine, libdrm, Mesa, and Hibr versions;
- exact command and environment;
- input fixture and expected result;
- machine-readable result;
- logs and, where relevant, framebuffer/surface snapshots;
- exit status and failure classification.

Evidence is invalid when it depends on manual visual inspection alone, an
unrecorded host configuration, an unaudited binary, or a test process that
continues after the claimed result.

## Release decision

The release is blocked if any requirement above lacks evidence, any matrix
case it claims is unexercised, any advertised API behavior is a stub, any
compositor owns canonical state, or any Hibr path requires manual geometry or
undocumented environment assumptions.
