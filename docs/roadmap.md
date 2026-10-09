# Blit roadmap

The roadmap is ordered by dependency. A later milestone must not become a
second, parallel implementation of an earlier one.

## Current position — release 0.1.409

The current candidate closes the production Hibr launch contract by packaging
an explicit `HIBR_RC` runtime file for API-launched Hibr children. This keeps
the live systemd path aligned with the validated QEMU path without copying or
modifying Hibr's desktop implementation. The previous candidate added
surface-content counters and separates the
four-output Hibr visual gate into two explicit cases: stock Hibr content on
the primary with intentionally blank auxiliary joins, and a deterministic
window workload that populates all four outputs. The exact TCC build, package,
schema, terminal, input, lifecycle, four-output, Hibr, restart, producer
cadence, and soak
evidence is recorded in [`releases/0.1.409.yaml`](../releases/0.1.409.yaml)
and [`verification-status.md`](verification-status.md).

This is not a physical-host completion claim. The supplied live host is now
running 0.1.409 and exposes one output at 60 Hz. Explicit physical evdev
routes and live pointer motion have been observed; a source-qualified physical
button trace, multiple physical monitors, physical 120/240 Hz scanout, and
broader live Hibr geometry remain open. QEMU/VKMS remains the repeatable
multi-output acceptance oracle; the physical host remains the final
host-specific evidence boundary.

The next implementation boundary is measured cadence and physical experience:
separate producer/update rate from presentation/scanout rate, instrument any
slow Hibr producer path, and only then make a hardware claim. The current
0.1.405 settled local dummy-backend frame gate measures 59.996 Hz with zero
missed deadlines under its 60 Hz workload; that is useful renderer evidence,
but it is not a 60 Hz physical scanout claim and is intentionally not hidden
by changing the gate. The supplied live host remains separately measured at
60 Hz presentation with zero missed deadlines. Hibr producer cadence is
workload-dependent and must be reported separately from presentation cadence;
current live input evidence is recorded in the verification ledger.

The next engineering priority is therefore not another launcher or parallel
renderer. It is closing those named hardware evidence gaps while preserving
the single canonical terminal state, atomic frame boundary, API diagnostics,
and one-primary-Hibr-owner invariants below.

## Historical platform position

The platform capability gate is passing in the deterministic VKMS fixture:

- TCC `0.9.27` compiles and links the SDL3 probe.
- SDL3 initializes through KMSDRM without X11 or Wayland.
- One DRM card exposes two virtual outputs.
- A fullscreen SDL window is created successfully across that fixture.

The initial product slice is now running: the OpenAPI contract, TCP gateway
(network-bound by default, with an explicit bind override), PTY sessions,
revisioned surfaces, persistent WebSocket stream, SDL3 output initialization,
and explicit session-to-display routing are implemented.
The first renderer-neutral frame plan is now also implemented: a frame request
captures the selected surface revisions before the SDL adapter presents them.
The plan now carries each surface's accumulated dirty rectangle. Native text
grapheme shaping, richer grapheme/cursor composition, effects, authentication,
and VKMS gateway integration remain open. The SDL adapter now validates UTF-8,
stores one single-cell-width code point per cell, renders schema-defined cell
colors and basic attributes, carries explicit bounded cursor state, and uses
dirty rows. PTY output now feeds that same surface model for printable text,
core controls, basic ANSI cursor/erase commands, and bounded upward scrolling.
Session responses expose the current surface revision so clients can perform
optimistic updates and retry cleanly after PTY races.
Session, cell, and frame mutations have bounded idempotency-key replay with
payload conflict detection, preventing duplicate PTY creation on retries.
Broadcast stream events carry monotonic sequences with a bounded replay cursor
and explicit `replay_gap` recovery behavior. Surface snapshots provide the
authoritative HTTP resynchronization path, including text, styles, and cursor.
Optional bearer authentication now protects HTTP and WebSocket upgrades when
`BLIT_AUTH_TOKEN` is configured; TLS and token rotation remain deployment
work.
The Hibr desktop has passed the TCC-built PTY compatibility probe, and the
production `console-blit` module is operational over the OpenAPI/WebSocket
contract. Explicit focus and versioned input routes are exposed by the API.
The SDL adapter uses single-surface incremental frames and conservatively
redraws multi-surface outputs with deterministic vertical stacking. The
current 0.1.379 source passes the complete terminal, graphics, input,
two-display VKMS/QEMU, four-output Hibr primary/`--join`, mixed-refresh,
output-loss, and the 60 Hz emulator-like pixel-stream acceptance gates. SDL wallpaper
backgrounds are coalesced into horizontal runs before geometry submission;
the focused 128×48 regression workload now submits four background quads.
The bundled Hibr font contains 929 glyphs and 1,182 mappings; its manifest rejects
unresolved `.notdef` fallbacks and checks deterministic box/special-glyph
bitmaps. The authorized physical host is running 0.1.332 and has a healthy
one-output SDL3/OpenGL presentation at 60.000 Hz with zero missed deadlines
in the current diagnostic samples. Its input inventory exposes one physical
pointer and the activity-aware SDL fallback is deployed, but a physical
movement has not yet been captured by the acceptance harness. Physical
multi-monitor confirmation remains open because that host currently exposes
only one connector; physical 120/240 Hz confirmation remains
hardware-dependent.

The 0.1.335 diagnostic path delivers framebuffer screenshots asynchronously,
so a slow remote evidence download cannot block the render or input loop. The
live slow-reader gate passed; this is an operational correctness result, not a
claim of physical mouse activity.

The exact-current 240 Hz VKMS scheduler fixture passes mode selection and
deadline calculation. The 0.1.379 240 Hz pixel-stream run accepted all 240
frames with zero transport errors but completed only 84 page flips and added
deadlines under QEMU TCG without `/dev/kvm`; the async-page-flip experiment
completed 72 and was worse. This is an explicitly open scanout gate, not a
product pass or a physical 240 Hz claim. See [ADR 0044](adr/0044-high-refresh-evidence-boundary.md).

Display timing is an explicit hard requirement. Each output must schedule at
its active mode's maximum refresh by default, with per-output deadlines. The
scheduler may wake more often for input and I/O, but must not intentionally
present below that rate. A lower cap is an explicit, diagnosed administrative
policy only. See [ADR 0006](adr/0006-display-refresh-invariant.md).

Kitty graphics and bounded sixel decoding are now implemented against the
canonical graphics-object model. Their parser, resource-limit, placement,
and SDL3/direct-KMS pixel gates pass in the deterministic fixture. Further
protocol grammar remains an explicit compatibility extension, not a second
renderer or image-lifetime model.

SDL glyph-atlas sampling is explicitly nearest-neighbor so adjacent bitmap
glyphs cannot bleed into one another when scaled. The filter policy has its
own SDL gate and preserves the existing direct-KMS path.

The production PSF now covers common terminal shading symbols and combining
acute/tilde/ring marks, with exact block-element row-shape assertions. This
improves useful terminal coverage without claiming universal Unicode shaping.

Interactive gaming is also a release requirement. Terminal-native games are
validated through the ordinary PTY/VT500/input path. Arcade and pixel-oriented
emulation require the API-owned graphic canvas/frame path described in
[ADR 0013](adr/0013-interactive-games-and-emulation.md); Kitty and sixel are
compatibility paths, not the high-rate emulator transport.

## Milestone 1: contract vertical slice

Resolve ADR 0002 before writing server behavior. It must select the canonical
machine-readable schema format, wire framing, compatibility policy, and the
code-generation/validation boundary.

The first contract should cover only:

- server identity and capabilities;
- output enumeration and topology;
- one text surface with dimensions and cell updates;
- a frame request and presentation acknowledgement;
- stable errors and revisions.

Effects, persistence, and deployment hardening remain later concerns; the
first bounded SDL and direct-KMS effects/overlay path is now exposed by the
schema and runtime, with pixel-level VM evidence gates for both outputs.

Exit criteria:

1. The schema is the only normative definition of these messages.
2. A generated or mechanically derived validation artifact is checked against
   the schema.
3. One TCC-built server accepts a request and returns a typed response.
4. The same request sequence works with the dummy backend and the VKMS backend.

## Milestone 2: authoritative state engine

Implement one state model behind all transports and renderers. Add revisions,
idempotency handling, deterministic ordering, damage tracking, and structured
diagnostics. The renderer receives immutable frame plans; it does not own API
state. The current SDL adapter has the first routing seam: a session without
an explicit output targets display one, and an explicit `display_id` targets
only that output.

## Milestone 3: multi-output presentation

Map one logical canvas to the two-output VKMS fixture, then add output loss,
mode changes, gaps, offsets, and atomicity behavior. The topology and frame
acknowledgement semantics must be tested through the public API, not through
SDL-specific test calls.

## Milestone 4: text fidelity

Add grapheme policy, cell attributes, cursor state, scrolling, and incremental
updates. Preserve text and damage as canonical state until the renderer boundary.

## Milestone 5: effects

The first bounded, deterministic SDL preset is implemented through
`/v1/effects`. Continue with the versioned effect graph, explicit frame/time
inputs, overlays, and additional presets only after preserving the same
revision and capability/error rules. CRT effects are downstream of correct
text and output semantics; they must not obscure presentation failures.

## Milestone 5a: terminal graphics protocols

Implement the canonical bounded graphics-object store and Kitty graphics
protocol first. It must support chunked upload, explicit image identifiers,
placement, clipping, z-order, deletion, diagnostics, and damage integration
without mutating terminal cells. Verify equivalent SDL3 and direct-KMS
results with deterministic pixel fixtures in the two-display VKMS guest.

After Kitty is accepted, add sixel decoding as a compatibility input path into
the same graphics-object model. The first sixel slice is bounded direct-color
raster decoding with deterministic SDL3/direct-KMS pixel gates; it must not
introduce a second renderer, layout owner, or image lifetime model. Expand its
accepted control grammar only with a corresponding parser and pixel fixture.

## Milestone 5b: interactive games and emulation

Lock down the measurable workload contract first: input timestamps and queue
depth, frame pacing, back-pressure, damage or buffer reuse, and diagnostics.
Then add the bounded graphic canvas/frame resource to the schema and implement
it through the existing SDL3/direct-KMS presentation boundary. The VKMS
fixture exercises a synthetic emulator-like producer at 60 Hz,
accepts all timed frames with zero missed deadlines, and proves stable
presentation on two outputs. Input injection, physical-host presentation, and
the physical multi-monitor arrangement remain required before release.

Terminal-native game acceptance is a separate gate and covers alternate
screen, cursor and colour state, UTF-8/wide/combining glyphs, mouse reporting,
resize, and the production font. No release note may claim arcade/emulation
support until both gates pass.

## Milestone 6: operational hardening

Add authentication, leases, persistence policy, hotplug behavior, observability,
resource limits, and a non-root deployment profile. Repeat the full contract
suite on VKMS and then on a physical DRM/KMS host.

## Immediate next action

Capture settled SDL screenshots and API diagnostics from the authorized
physical host, then validate physical pointer/button/wheel routing and refresh
cadence. The remaining hardware gates are a real two-monitor
primary/`--join` arrangement with independent connector discovery and
presentation, physical pointer/button/wheel activity, and physical 120/240 Hz
refresh validation. The current host exposes only one connector. The update
API requires its configured update token; no unauthenticated bypass is
acceptable.
