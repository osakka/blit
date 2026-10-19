# Blit roadmap

The roadmap is ordered by dependency. A later milestone must not become a
second, parallel implementation of an earlier one.

## Current position — held-scene pointer presentation and stable identity

Release0.1.551 lets software cursor updates reach the next output
refresh while a terminal quiet window or Hibr multi-PTY frame barrier holds
scene changes. The frame remains pixel-identical to the last complete scene
under the pointer; terminal/image/effect/overlay damage stays pending for the
normal atomic commit (ADR0136). Production KMS and SDL pixel gates pass. The
frozen suite is100pass/3explicit skips/0fail, including a120-second four-PTY
soak (11,216 snapshots/1,020 resizes). Physical cursor and window-drag cadence
on10.54 still need measurement before#101 can close. The 2026-10-19 API
pointer sweep measured50.62–59.10 submissions/s on one 59.997Hz physical
output, but had zero physical mouse events and no pressed-button drag. A prior
two-output run was20.42 submissions/s under a different state; investigate
without treating it as a normalized comparison. The host snapshot also shows
eight high-CPU `desktop [sess…]` processes; their ownership/relationship is
unverified and none were killed.

Wallpaper persistence uses Hibr's existing per-client `dt_walldisp` records,
keyed by the stable connector/pane names from ADR0135. A successful API
restart retaining the same Hibr process is not enough: acceptance must select
the intended named pane in Hibr's native Wallpaper panel, save a distinct
setting, restart Blit, and observe that setting on that pane. The latest
native-panel attempt did not produce a valid acceptance: the live Hibr surface
now reports All Displays/pattern/no image, and a terminal editor was visible
when input stopped. Treat the persisted wallpaper/editor state as uncertain;
do not send more UI input until it can be inspected safely. #103 remains open.

## Previous position — Mouse550 and stable native identity

API-deployed550 adds antialiased pointer shapes/palettes/size/shadow
shared by SDL and direct KMS (ADR0133), plus stable connector/pane native
names for Hibr per-display preferences (ADR0135, #103). The canonical
catalogue bundle0.1.5 includes Blit Mouse, sharing the existing settings draft,
worker and credential with Blit. Focused native/UI/pixel tests pass; final
package/deployment acceptance is tracked in the verification ledger. The
frozen full run is99pass/3explicit skips/0fail, plus the separate25472-pixel
SDL pointer check. Host cursor pixels and all three stable native names are
verified. Per-display wallpaper restart acceptance and #101 performance remain.
549 was superseded before deployment: release review found unscoped pointer
key lookup;550 adds a red/green scoped/duplicate/escaped-key regression.

550 is currently deployed on10.54 with two physical outputs, pure KMS and
saved layout/font intact. Nonvisual PTY bytes no longer request full repaint
(ADR0134). The newest bounded pointer workload records115 pointer-output
submissions/6.125s and Blit6.08% of one CPU core; this does not establish60Hz
physical latency or resolve #101. Wallpaper persistence after restart needs
authenticated desktop acceptance; no Hibr-core modifications are permitted.

## Previous position — independent grids and bounded direct-KMS repaint

Deployed0.1.546 retains pure KMS and adds connector-keyed1–8 column/row grids,
new-output1x1 defaults, durable legacy migration and validated occupied-pane
conflicts. The canonical settings document remains the sole desired-state
store. It also bounds fractional-image work to the actual damaged intersection
(ADRs0131/0132) and exposes process/native-owner CPU plus host load. The final
frozen suite is97pass/3explicit skips/0fail; reproducible package/server builds
and physical framebuffer grid/font samples pass. See the verification ledger
for limits and exact identities.

Published catalogue pane0.1.4 adds native horizontal font/grid sliders and
private URL-bound token persistence, including correct native preference load
ordering. Scale is still global. The user has logged in on546: native inventory
verifies lower primary plus upper join belonging to one desktop owner.
Only DP-1 is physically connected; bounded pointer/CPU measurements are not
window-drag or display-refresh-capacity acceptance. Blit#100 remains open for
physical second/third-output hotplug, #101 for the reported whole-machine
slowdown with authenticated wallpaper/drag and extra monitors. No universal
performance claim, multi-GPU support or automatic primary migration is added.

## Previous position — direct-KMS live monitor discovery

The user requested pure KMS/DRM and implementation of Blit#100 in that backend.
The physical host was switched to pure KMS0.1.543, then API-updated to0.1.544,
keeping the lower primary and upper auxiliary policy. The authenticated split
was observed before the package restart; the current login waits below and
its saved upper auxiliary waits for authentication. SDL is not patched. ADR0130 defines the direct-backend
lifecycle; ADR0129 records the deferred SDL alternative.

Deployed0.1.544 adds background connector snapshots and main-thread output/
topology reconciliation, stable page-flip lifetimes, explicit failure status
and zero-output recovery. Frozen suite93/4skips/0fail, separate64-cycle stress,
reproducible package validation and real-host framebuffer/observer checks pass.
Actual second/third monitor unplug/replug acceptance remains open in#100.
The0.1.545 candidate removes global-layout inheritance through independent
connector-keyed grids (ADRs0131/0132), adds native panel sliders and private
token persistence, and bounds fractional-image damage work. Final package/live
acceptance is recorded in current-state and verification-status, not implied
by this implementation milestone. The reported physical slowdown is tracked
in[Blit#101](https://git.home.arpa/itdlabs/blit/issues/101).
Selected-device limits remain; this is not multi-GPU support or automatic
primary migration. Font/profile scale remains global.

## 0.1.543 policy and catalogue checkpoint

Before the requested direct-KMS switch, the host ran0.1.543 SDL GL. The edge/font correction has bounded
physical pixel acceptance; the user previously accepted the 0.1.535 movement
checkpoint. These are not universal renderer or refresh-rate guarantees.
See current-state.md and verification-status.md for exact evidence.

Issue #99 and ADR 0124 sequence the next delivery: reliable API persistence
and login startup policy, authenticated native association/standby integration
(#96), stable bindings and explicit recovery policy, then a native Control
Panel distributed through `itdlabs/hibr-apps`. No Hibr-core modifications.
The panel must not own a second configuration store or controller. Hibr
standby's deliberate-release and explicit-reattach behavior now has a
documented Blit integration rule: provision only for a fresh authenticated
owner, apply initial placement once, and leave later Hibr Displays choices
alone. This rule is implemented in0.1.543 under ADRs0124/0128; authenticated
physical one-output split auto-join is now corroborated by native inventory
and the user's visual acceptance. General physical hotplug remains open.

Version 0.1.537 implements the first foundation only, with physical API
save/restart persistence and unchanged geometry/backend/font verified.
Joins, live failover,
arbitrary layouts, and catalogue UI acceptance remain separate deliverables.

The current implementation extends native association to expose all named
clients and their signed native rectangles from that same read-only query,
and adds validated, restart-persistent auxiliary bindings to the existing
settings document (`auxiliary_displays` stable output plus pane ordinal).
Both are locally gated, packaged and deployed. ADR0128 pins the live Apply
contract separately from durable settings:
volatile current-session policy, asynchronous per-binding effective state,
exact native join and one-time placement proof, and conservative no-respawn
after a standby exits. 0.1.543 implements this API/controller and its local
synthetic, installed-Hibr desktop auto-invite/standby and pending-font-resize
gates pass. The catalogue pane in `hibr-apps` issue #1 has callback/API,
catalogue and actual Hibr UI/keyboard acceptance. Reproducible package
validation and catalogue publication are complete. After normal login the
host reports its saved upper `DP-1` pane1 auxiliary joined with confirmed
offset(-67,0), lower pane2 primary, and a134x240 native union; the user reports
the setup looks perfect. Pane0.1.1 is named **Blit**. General multi-output/
hotplug and active-primary failover are not delivered by this checkpoint.
Blit#100 tracks startup-only SDL discovery becoming live output/topology
reconciliation; do not promise that a newly plugged monitor appears merely
because the Control Panel refreshes API observations.

Version 0.1.538 adds a private post-authentication identity handoff (ADR 0125),
not automatic joins. The deployed installed hook reports verified UID/GID 1000
after login. The next boundary is native per-user held-owner readiness before
standby/release integration. Hibr Displays
actions remain session-only until explicitly saved into the same Blit store.
Its complete local runtime run passes 86/86 executed checks (four skipped),
and package validation/reproducible rebuild pass separately. Authorized API
deployment preserves saved settings, pane geometry, renderer and font; the
physical edge/font oracle passes again. Native owner association and standby
joins remain separate, unimplemented acceptance boundaries.

Version 0.1.539 adds exact named-client association with expiring native
observations and a bounded asynchronous per-user query. Local identity/native
CLI/controller tests pass; the full local/package run passes89 tests with
three explicit skips and no failures. Authorized API deployment preserves
settings, geometry, renderer and font. The physical pre-authentication probe
and repeated edge/font oracle pass. Following the user's normal login, exact
named-client readiness and quiet background refresh across the freshness TTL
also pass on10.54: verified UID1000 and the same native owner lifetime across
three samples. No join was started. Next retire obsolete native horizontal
projection and integrate standby/release joins (#96). Never guess an old
unnamed client into this registry.

Version0.1.540 implements the native-boundary prerequisite: recognized
login-owned joins, derived authenticated credentials, per-client local PTY
input instead of the obsolete horizontal/primary redirection, and generation
cleanup (ADR0127). Raw-PTY and installed native CLI/actual-launcher oracles pass;
the final controller/broad/package run passes90 checks with three explicit
skips and no failures, including reproducible rebuild and120-second soak.
Authorized API deployment preserves settings, geometry, font and SDL GL;
the pre-authentication and repeated physical font/edge checks pass. After
login, named association/ownership projection and a recognized upper attachment
are verified against native CLI with unchanged desktop owner and134x240 union.
A passive real upper press targets that client's own PTY/local coordinates.
Physical visual acceptance remains incomplete: a dark lower rectangle was
captured in both the image and terminal cell model, then disappeared after
the user's window movement. The latest fixed scene has no separate orphan
rectangle. Investigate stream/parser evidence across that repaint transition;
do not infer either a pure SDL cache fault, a Hibr defect or a completed fix.
Manual join commands are not the requested deliverable. Continue with saved
auxiliary policy and native standby/initial-placement reconciliation in the
same API-owned controller/store after this boundary is accepted.

## Historical position — 0.1.507 live visual-acceptance blocker

0.1.507 is deployed on the authorized one-output host and its API, package
identity, deterministic renderer gates, and synthetic pointer timing path have
been checked. That is not visual or physical acceptance. The latest live
capture showed an incomplete flat-colour scene with no graphics in either
session; a separate capture observed no physical evdev input. A previous live
state also retained an ended primary surface and a failed orphan join.

The next work must establish a complete static Hibr scene first, prove the
Kitty payload and placement path, then measure real window movement with a
physical evdev trace. Do not call timing stability “UI stability”, and do not
modify Hibr source. The full restart sequence is in
[current-state.md](current-state.md).

## Previous position — 0.1.503 Hibr session-entrypoint candidate

The ABI contract is accepted on the live host. The next blocker was proven by
the live screenshot: Hibr's installed session was sourced with the wrong
`$0`, so its sibling window-manager imports resolved in Blit's directory and
the authenticated child exited normally. The fix executes Hibr's installed
session at its own path. Acceptance requires the API-created desktop and the
login-manager handoff to remain running with an owner registered.

## Previous position — 0.1.502 Hibr ABI compatibility candidate

Blit now uses Hibr's documented numeric module ABI as its runtime
compatibility contract. The exact Hibr release remains a build and runtime
provenance field, so a compatible host patch release does not falsely block
the login handoff. The next acceptance boundary is a rebuilt, gated package
deployed to the live host, followed by one authenticated login attempt whose
`login_session_exit` or desktop-owner evidence is collected through the API.

## Previous position — 0.1.500 installed-Hibr ABI candidate

The live deployment of 0.1.498 proved the direct installed Hibr session path.
The 0.1.499 candidate was then rejected by its own compatibility diagnostic:
the host had advanced to Hibr 0.99.212 while that package was pinned to
0.99.204. This candidate rebuilds the adapter against the host's current
released version. Physical Hibr acceptance remains blocked until the
candidate reports `compatible=true` on the live host and the desktop menu,
input, joins, and rendering are retested on that exact package.

## Previous position — 0.1.497 authenticated-diagnostics candidate

The live troubleshooting path now has one shared authenticated transport and
one canonical capture contract. Captures include capability/settings state,
runtime and filtered traces, screenshot frame/revision headers, authentication
mode, completion metadata, and mechanical completeness validation. This
removes the previous ambiguity between an unauthenticated capture failure and
a rendering failure. The renderer still requires live OpenGLES2/KMS evidence;
this tooling makes that next diagnosis trustworthy.

## Previous position — 0.1.496 generated-release-identity candidate

The release workflow now generates the package identity fields from the
Debian artifact itself: package/version, archive checksum, shipped binary
hashes, and the packaged Hibr pin. The package gate fails on drift, while
physical, GPU, Hibr-behaviour, and refresh claims remain explicit measured
evidence. This closes the provenance portion of issue #27. CI and automatic
live-update rollback/restore remain open parts of that issue.

## Previous position — 0.1.495 composition-budget candidate

The full-redraw SDL path now has an executable budget gate (issue #80). The
gate can drive a full 240×135 terminal surface, authenticate to a remote host,
assert refresh cadence and missed deadlines, and enforce an explicit maximum
render time. On the supplied 60 Hz OpenGLES2 host it measured 59.916 Hz,
zero missed deadlines, and 16.789 ms worst render against a 17 ms threshold.
The OpenGLES2 scene cache is now implemented with two render targets and an
explicit copy-before-damage boundary (ADR 0099), so it no longer depends on
implicit render-target persistence. Live rapid-update acceptance remains
pending; higher-refresh hardware remains covered by #21.

## Historical position — 0.1.494 server-settings candidate

Blit now moves the presentation settings that used to be environment-only
behind the API-owned settings document (issue #70). Render cadence, requested
renderer, direct-KMS async pageflip policy, empty-output expectation, and font
profile/scale have a documented precedence rule: the environment is the boot
default only while the document is silent. Pane layout remains owned by
`/v1/topology` and is persisted through the same document. The focused
settings, font, topology-geometry, and schema gates pass; package and physical
host acceptance remain pending for this candidate. `BLIT_EVDEV_MOUSE` is
documented as a compatibility boot override and is not duplicated in the API;
stable input-device resources are the supported ownership boundary.

## Historical position — 0.1.492 input-deadline candidate

Blit now has the complete implementation boundary for issue #71: opt-in
per-pointer left-plus-right middle-button emulation, bounded latency, and a
deadline integrated into the event-driven loop. The API schema, settings
round-trip, idle flush, motion preservation, chord translation, release, and
restart behavior are covered by the focused pointer gate. The package and
physical-host acceptance remain pending; hardware multi-monitor and high-
refresh claims remain separate acceptance gates.

## Historical position — 0.1.470 refresh-bound presentation candidate

The next release closes the refresh-scheduling defect found on the physical
host: Hibr window movement produced surface commits about every 5 ms, and the
live GLES2 path rebuilt the full scene for each one. Blit now preserves each
output's refresh phase, coalesces terminal/pointer/pixel work to the next due
slot, and retains pointer damage independently across outputs. The local TCC
frame-rate workload reaches 60 Hz with zero missed deadlines and passes a
producer-burst upper bound. Live deployment must still prove the trace and
physical Hibr motion on the exact package; no physical high-refresh claim is
made for the one-output host.

## Historical position — 0.1.466 input-boundary candidate

The next linear release addresses physical-host findings #76, #77, and #78
without changing Hibr. Blit's diagnostic trace and the adapter log proved that API
pointer edges and F10 reached the one running Hibr session. The adapter then
forwarded one-based terminal cells into Hibr's zero-based console API and
discarded all function keys because it had no Hibr key-name mapping. It also
swapped middle/right buttons, discarded the wheel, ignored Hibr pane ownership,
and never woke for watched application output. ADRs 0068 and 0069 make
`console-blit` the single translation/provider boundary. The focused gates now
pass pointer origin, font-scaled geometry, all buttons, wheel, F1 through F12,
pane ownership/stacking/clipping, UTF-8 width, shadows, and watched-descriptor
wakeup against released Hibr 0.99.191. Resize detection is now SIGWINCH-driven
instead of one synchronous HTTP request per input event. Physical-host acceptance remains
pending until the exact package keeps the menu and windows stable, opens
Hibr's F10 menu, activates a visible control with a click, scrolls with the
wheel, and redraws application output immediately.

Release 0.1.465 fixed issue #75, found on the physical host before this input
run: returning an input property to its default changed live state but the
settings merge resurrected the old non-default value. The focused gate proves
both sides of the corrected ADR 0067 rule—known applicable values reset
cleanly, while unknown future values and known values made temporarily
inapplicable by a device re-probe remain preserved. The physical host passed
the disable, re-enable, persistence, restart, and evdev reopen sequence; the
exact revisions and package hash are in the verification ledger and release
manifest.

The preceding releases corrected two other boundaries relevant to the same
live acceptance cycle. Release 0.1.463 stopped assuming that an SDL
OpenGL/OpenGLES render target preserves pixels between frames and therefore
uses full-scene redraws on the live `opengles2` backend (issue #72). Release
0.1.464 replaced alphabetic evdev range arithmetic with the actual QWERTY key
positions used by Linux input. Neither result is promoted to final physical
acceptance until one traced Hibr session is measured on the exact deployed
artifact.

The supplied host currently exposes one 1920x2160 output at 60 Hz. With no
session attached it reports the correct 16 ms refresh-aware target, SDL vsync,
and no sustained frame rate because an idle output presents no frames. Its
persisted keyboard lockout is removed, and the 0.1.465 post-restart state has
one enabled, open keyboard and one pointer.

The immediate sequence remains deliberately linear: package 0.1.466 from the
gated source, prove the package artifact, commit and push that reproducible
candidate, deploy it through the supervised API, attach exactly one Hibr
primary, then exercise #76-#78 with API input and physical input. Producer and
presentation cadence, screenshots/cell agreement, and pointer-cache counters
are captured in the same run. Rendering issue #72 and hotplug issue #74 remain
open until that evidence says otherwise; physical multiple monitors (#20) and
120/240 Hz scanout (#21) remain hardware gates rather than claims inferred
from this one-output host.

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
refresh validation. The current evidence checkpoint has two physical outputs
on10.54; 120/240 Hz physical validation still needs matching hardware. The
update API accepts only its configured update token.
