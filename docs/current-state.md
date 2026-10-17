# Current state and restart point

This is the handoff document for resuming Blit work after a session change. It
is intentionally evidence-led and does not replace the OpenAPI contract,
source, ADRs, or the verification ledger. It tells the next engineer where the
work actually stopped.

## Status at the last review

### 2026-10-17 authenticated desktop exposes PTY byte loss, deployed 0.1.535

The user authenticated. The 0.1.534 capture shows only the upper wallpaper
portion; the surface snapshot itself has blank rows below row 22. This is
**not a correct complete desktop**, despite the prior bounded pixel/cadence
tests passing. [Issue #94](https://git.home.arpa/itdlabs/blit/issues/94) and
[ADR 0121](adr/0121-terminal-stream-fragmentation-cannot-drop-bytes.md) record
a reproduced Blit parser defect: pending UTF-8 plus a large PTY read silently
discards that entire read because its join buffer is only 772 bytes.
The actual service-entry pipe regression fails before and passes after
bounded slicing through the one parser; ten fragmentation sizes also agree
with continuous parsing. Corrected-source renderer/terminal gates pass
51/51 (no skips/failures). Reproducible package 0.1.535 from `01b57f5` is now
API-deployed; manifest `releases/0.1.535.yaml` records its exact identity.
The identical real-host PTY test fails on 0.1.534 and passes on 0.1.535:
one half block, all 4000 text cells and END retained.

The user logged in again and confirmed correct rendering and movement. The
new capture has wallpaper through row 66; Control Panel was moved via the
API. A revision-matched pixel check passes over 14,909 eligible blank/block
cells (13,527 block cells), 1,904,128 pixels, with zero disagreements.
Unsupported styled glyphs and pointer neighbourhoods are excluded, not
claimed. The separately reported right-edge shift was subsequently reported
fixed by the user; no geometry or backend change was made in this cycle.

This is the working **v0.1.535 checkpoint**, not universal conformance. The
6000-frame cadence recheck has zero stale pixels but **fails** zero-skipped
refresh acceptance: 1980 flips span 1986 DRM vblanks over 33.151181 seconds
(six skips). Runtime captures one 106.169-ms loop gap; its cause is not yet
established. [Issue #95](https://git.home.arpa/itdlabs/blit/issues/95) tracks
this separate performance follow-up, not a passing
rate claim. The one existing authenticated upper-pane session remains;
the lower pane is empty after probe cleanup.
Evidence is under ignored `diagnostics/authenticated-20261017/`. Backend,
pane arrangement and Hibr source remain unchanged.

### 2026-10-17 deployed and bounded-validated 0.1.534

The live host 192.168.10.54 runs reproducible package 0.1.534 from `8c6a26f`;
identity is in `releases/0.1.534.yaml`. The direct-KMS 1920x2160 split topology
is unchanged. Only its existing, focused upper-pane login manager remains;
the temporary test terminals are removed and the username prompt is visible.

The reproduced terminal damage-loss, concurrent-pointer damage overwrite,
reopened coalescing deadlines, and delayed KMS retries are fixed. Tests failed
before each correction. The production KMS presenter also matches a fresh
full render at every pixel over 128 mixed-content/pointer frames.

The final physical soak passes: 6000 producer frames, 466 pointer requests,
2,051,904 checked blank pixels, zero stale pixels; 1980 completed flips span
exactly 1980 DRM vblanks over 33.050589 s, with zero skipped refreshes. Observed
DRM cadence is 59.908161 Hz; nominal mode timing is 59.997136 Hz. This resolves
the small nominal-rate discrepancy without weakening a percentage threshold.
[ADR 0120](adr/0120-kms-vblank-clock-is-observable.md) defines the independent
clock evidence and zero-skipped-vblank acceptance rule. Trace API:
`/v1/diagnostics/trace?kind=kms_vblank&limit=16`.

Evidence and final screenshot are in ignored local
`diagnostics/rendering-20261017/`, especially `validated-soak-534/` and
`0.1.534-final.png`. Hibr source was not modified; no SSH was used. Remaining
acceptance requires the user to authenticate, then actual desktop wallpaper
and window-drag testing. Joins, other physical output counts/rates, and full
terminal-protocol coverage are not inferred from this one-host result.

The sections below are chronological evidence from prior cycles, not the
current deployed state.

### 2026-10-17 candidate 0.1.533: exact KMS mode refresh

0.1.532 is deployed and the short strict real-host test passes at 59.832 Hz
with zero stale pixels over 2,051,904 checked pixels. Its 6000-frame soak also
has zero stale pixels but fails the long refresh check: 59.912 submissions/s
over 33.232 s versus the reported 60.0-Hz mode. Linux rounds mode.vrefresh;
[ADR 0119](adr/0119-kms-refresh-keeps-the-mode-timing-fraction.md) corrects the
active/enumerated metadata from pixel clock and totals. The actual-mode
regression fails before and passes after; deployment and the same long soak
remain pending. No rate tolerance is weakened and mode selection is unchanged.

### 2026-10-17 candidate 0.1.532: retry deferred frames on vblank completion

[Issue #91](https://git.home.arpa/itdlabs/blit/issues/91) tracks the measured
52.6–54.1 Hz direct-KMS cadence on a 60-Hz output. The live refresh assertion
fails before correction. The trace shows a pending-flip deferral, then the
completed flip waits for another software deadline, producing some 32-ms
submission gaps. [ADR 0118](adr/0118-retry-deferred-kms-frames-at-flip-completion.md)
retries only previously deferred outputs whose buffers the canonical KMS
state now reports ready. Other output deadlines and SDL remain unchanged.
The readiness and per-output retry tests pass, as do 51 renderer/terminal gates
and the schema/observation API checks. KMS FPS now excludes rejected/no-op
attempts and global present_calls includes actual submissions. Live acceptance
is pending. The host still runs 0.1.531.

### 2026-10-17 deployed 0.1.531: bounded presentation deadlines

Follow-up [issue #90](https://git.home.arpa/itdlabs/blit/issues/90) reproduces
expired PTY/group deadlines being reopened by incoming output, and a zero
`select()` wait while an eight-millisecond quiet window still prohibits
rendering. Direct KMS also left the session batch flag set after consumption.
The real-function regressions fail before correction and pass afterward.
[ADR 0117](adr/0117-terminal-coalescing-deadline-closes-at-presentation.md)
keeps deadlines fixed until presentation/release and clears consumed batch
flags. The focused join barrier and local frame-rate gates pass (59.999 Hz
in the dummy-backend frame-rate fixture). This is not physical-host timing.

The live host now runs reproducible package 0.1.531 from source `0933cad`.
The two-corner and half-block probes pass. Two 600-frame PTY/pointer runs each
check 2,051,904 blank pixels across the whole measured pane with zero stale
pixels. Active submission cadence is 53.185 and 52.581 Hz on the 60-Hz output.
The captured render trace shows deferred KMS presents followed by retries at
the next software deadline, sometimes approximately 32 ms later. This remaining
vblank retry timing is under investigation; 60-Hz physical acceptance remains
unmet. Only the existing login manager remains, focused, with unchanged pane
topology/backend. An authenticated Hibr dragging test is still pending login.

An improved low-overhead PTY/pointer measurement on 0.1.530 records
47.908 page-flip submissions/s in an active 3.131-second window, with zero
stale pixels. The new benchmark polls terminal title metadata rather than
repeatedly serializing the whole cell grid during animation. The live
upper/lower half-block RGB pixel check also passes.

### 2026-10-17 rendering root defects, candidate 0.1.530

The pre-fix live 0.1.529 desktop was visually broken. Prior evdev fairness and
cadence measurements did not establish rendering correctness. Two actual
defects are now reproduced by failing-before/passing-after tests: damage
rectangles shrink on left/up updates, and concurrent KMS pointer work replaces
pending terminal damage before retiring the revision. Partial scroll/DEC
rectangle paths also overwrite outstanding damage. These are Blit defects;
Hibr source has not been modified.

Deployed 0.1.530 corrects these through one shared rectangle-union operation
and separate pointer accumulation; see [ADR 0116](adr/0116-damage-union-preserves-all-pending-work.md)
and [issue #89](https://git.home.arpa/itdlabs/blit/issues/89). The previous
uncommitted full-output redraw proposal was withdrawn before deployment.
The actual KMS presenter now matches a fresh complete render at every pixel in
128 mixed-content/pointer frames in a memory-backed test. The identical live
two-corner defect fails on 0.1.529 and passes on 0.1.530. A 600-frame real PTY
animation with concurrent pointer requests leaves zero stale pixels in
100,352 checked blank pixels. Three Hibr login-saver samples report zero stale
or missing cells. Authenticated Hibr window dragging and refresh-rate
acceptance remain pending; neither is inferred from these tests.
The TCC-built candidate passes renderer/terminal gates: 51 pass, 0 skip, 0 fail.
The exact deployed package is recorded in `releases/0.1.530.yaml` and is
reproducible. Live motion and Kitty image/deletion probes also pass. Temporary
probe sessions were removed and keyboard focus restored to the one existing
login-manager session. The topology/backend were preserved.

The sections below are historical evidence for 0.1.529 and earlier builds.

The current source candidate is `0.1.529`. Hibr source was not modified. The
latest release retains the causal PTY/direct-KMS traces and pointer-background
correction, and adds a bounded evdev service turn for mouse, keyboard, and
gamepad devices. It does not claim that the complete live Hibr desktop is
visually accepted; the login manager itself is already a valid reproducer for
the reported fast-movement case, so authentication is not a prerequisite for
this investigation.

The authenticated live host at `192.168.10.54` reports `0.1.529` with
`backend=drm-kms`, `direct_kms=true`, `video_driver=direct-kms`,
`presentation_sync=drm-page-flip`, and one 1920x2160@60 Hz output. The
persisted 1x2 topology currently has the supervised login manager in the
upper pane and no surface in the lower pane; this is the controlled
single-desktop test state, not a primary-plus-join acceptance result.

ADR 0108 narrows changed-view composition to surface-local damage while
retaining an atomic page-flip boundary. The live fast-terminal workload then
measured 61.957–63.137 Hz, 14.085–16.892 ms last frame gaps, and a 10.903 ms
maximum KMS present time, with zero new missed deadlines in its measurement
windows. The cumulative counter still contains startup/login-relocation
events, including a 4.646 s maximum historical gap; that is not reported as
motion-pass evidence. Direct-KMS frame-rate counters are now populated by the
same diagnostics path as SDL.

The package identity for the previous deployed build is recorded in
[`releases/0.1.528.yaml`](../releases/0.1.528.yaml). These are bounded
renderer measurements, not visual acceptance of the Hibr desktop, physical
input, wallpaper, graphics protocol, or joins.

The source commit `479f78c` makes equal-z retained-graphics ordering
deterministic by Kitty image and placement identity. The current source cycle
adds bounded direct-KMS graphics damage: a graphics mutation repaints the
union of its previous and current complete-cell coverage, with a complete
view fallback when the bound is not provable. ADR 0112 records the follow-up
cursor-damage correction. ADR 0113 records the new PTY-to-KMS causal traces;
the 0.1.527 package was the first release containing that observability. ADR
0114 records the direct-KMS pointer-background correction shipped in 0.1.528.

### 0.1.529 physical-input fairness correction

The live login-manager capture was taken without logging in. Before the fix,
two screenshots five seconds apart showed only changed cursor pixels while the
matrix saver scene remained visually unchanged. The user-confirmed expected
behaviour is a continuously advancing downward matrix, so that was the exact
producer-starvation symptom: pointer work was serviced while the single event
loop was draining physical evdev input instead of returning to PTY and render
service. API pointer injection cannot reproduce that boundary because it
bypasses the evdev reader.

The source fix bounds each evdev device service turn to 32 records and exposes
`performance.evdev_service_budget_yields`. After deployment, physical motion
raised `evdev_mouse_events` from 0 to 3,471 and the budget-yield counter from 0
to 5 while PTY reads continued and the windowed KMS rate recovered to about
62 Hz. The correlated direct-KMS trace showed changing pointer coordinates and
repeated page-flip submission; there was no continuing input-induced loop
starvation. A later quiet window reported zero new missed deadlines,
`loop_gap_window_max_ms=13.146`, and `frame_rate_max_render_ms=4.272`.

Two post-deployment screenshots five seconds apart contain different matrix
saver frames, confirming that the saver producer continues to advance without
login. The QEMU fairness oracle still remains to be run; these physical-host
results validate the real evdev boundary but do not claim an authenticated
Hibr desktop or complete visual acceptance.

### 0.1.528 pointer-trail correction live evidence

The local TCC build and the complete renderer/terminal gate run pass 50/50.
The package adds no Hibr changes. It restores the saved KMS pointer background
before every non-full scene repaint, so unrelated saver/terminal damage cannot
retain the pointer in the scene. It also records
PTY-driven surface revisions and damage rectangles in `surface_update` traces,
and direct-KMS output masks, view damage, cursor state, synchronization state,
and present duration in `render_path` traces. The exact package is deployed
and the live direct-KMS motion probe passes 12/12 accepted steps. The host is
still at the login manager, so this is not yet an authenticated Hibr desktop
visual-acceptance result.

The post-deployment screenshot shows the active login-manager saver in the
upper pane and the intentionally empty lower pane. The live session inventory
contains one `login_manager=true` session with no Hibr desktop role and no
graphics images. The next required acceptance boundary is one authenticated
primary desktop, followed by the same screenshot/trace capture while moving
the pointer and the saver.

### 0.1.524 live login-manager wake evidence

On 2026-10-17, `tools/diagnostics/wake-and-capture.sh` was run against the
live `0.1.524` host while the only Blit session was the supervised Hibr login
manager. A one-pixel API pointer motion was accepted and produced a changing
surface/screenshot, confirming the visible colored lines were an active Hibr
login-manager saver rather than a static framebuffer. An explicit API
button-0 press/release pair then produced two button trace events and a
screenshot containing Hibr's `User name` login box. Hibr's own login source
intentionally keeps the saver behind that box, so the remaining animated
background is expected login-manager composition; it is not evidence of an
authenticated desktop or of a complete desktop render.

The probe deliberately does not create a session, send a keyboard key, alter
input routes, or modify Hibr. Its screenshot and JSON verdict are temporary
live evidence under `/tmp`; the repeatable tool and its interpretation are
tracked in `tools/diagnostics/wake-and-capture.sh` and
`docs/diagnostic-capture.md`.

The live renderer-agreement probe was also corrected to discover the physical
display from topology; its previous `display-1` assumption caused a 404 on the
real `display-93` host. With that correction, six attempts against the active
login-manager saver were all discarded because the 12 MiB screenshot straddled
a changing surface revision. This is inconclusive evidence, not a renderer
pass or fail; a static controlled surface is required for a pixel agreement
assertion.

The required static controlled surface was then run in the free lower pane.
The earlier `TRAIL_PASS` verdict is not accepted: that probe added the containing
`pixel_region` to the already display-global `cell_grid.origin_*`, so lower
pane samples were outside the framebuffer, and the capture decoder treated
every live BMP as 24-bit even though direct KMS advertises 32-bit captures.
Both probe assumptions were corrected before accepting a result. The corrected live
moving-surface probe then updated a known block twelve times and passed with
`MOTION_PASS steps=12 accepted=12 discarded=0 direct_kms=true
display=display-93`. A separate live Kitty composition probe painted
`#102030`, emitted a 2x2 RGB image, verified exact red/green/blue/yellow image
quadrants plus an untouched background cell, deleted the image through Kitty,
and verified exact background restoration:
`KITTY_COMPOSITION_PASS display=display-93 image=9001 direct_kms=true
pixels=exact delete=exact`.

These results prove the canonical cell-to-pixel mapping, bounded damage path,
direct-KMS BMP capture decoding, and isolated Kitty composition/invalidation
for API-owned surfaces on this host. They do not prove Hibr's saver,
wallpaper/desktop scene, desktop windows, joined coordinates, or physical
input; those remain separate tests.

The follow-up live graphics discriminator was then read without changing the
host: the only remaining session was the supervised login manager
(`login_manager=true`, `graphics=[]`). Its filtered graphics trace contained
only the login manager's Kitty capability query; it contained no Hibr desktop
wallpaper upload. The current host therefore does not provide an authenticated
Hibr desktop scene from which to diagnose wallpaper or desktop composition.
This is a missing test precondition, not evidence that Blit's Kitty renderer
failed. The next live boundary is to capture one authenticated primary Hibr
desktop before starting any join.

### Architecture decision check

There is no accepted decision to deprecate the direct DRM/KMS or non-SDL
backend. ADR 0004 remains accepted and defines direct DRM/KMS as the
correctness-critical terminal presentation path; SDL3 is the adapter for the
optional canvas/effects path. The OpenAPI contract, source, package, tests, and
verification ledger all still describe both paths. Do not remove KMS code or
rewrite those references unless a new, explicitly accepted ADR supersedes ADR
0004. “SDL3/KMSDRM” and “direct DRM/KMS” are distinct paths and must not be
collapsed into one undocumented implementation.

The `0.1.510` package carries the authenticated
`GET /v1/diagnostics/observation` endpoint. It is a schema-first aggregate of
the existing serializers for one operational state dump. The endpoint is
installed and live-verified on `192.168.10.54`.

The exact live presentation-path mapping was revalidated on 2026-10-16:

- SDL/OpenGL means `backend=sdl3`, `video_driver=kmsdrm`,
  `renderer=opengles2`, and `presentation_sync=sdl-vsync`;
- SDL/software means `backend=sdl3`, `video_driver=kmsdrm`,
  `renderer=software`, and `presentation_sync=scheduler-only`;
- direct KMS means `backend=drm-kms`, `direct_kms=true`,
  `video_driver=direct-kms`, no SDL renderer, and
  `presentation_sync=drm-page-flip`.

All three live paths reached `ready=true` on the single 1920x2160@60 Hz
output with empty diagnostic errors and warnings. Focused synthetic and live
composition probes also completed without a focused deadline miss; the
software path had one startup scheduler miss during handoff. The host was then
restored to SDL/KMSDRM + OpenGLES2 with one Hibr primary in the lower pane and
one Hibr join in the upper pane, ready for physical observation.

The product is **not fully physically accepted**. The latest bounded live
capture showed a coherent two-pane Hibr primary/join composition, and the
Kitty image probe completed without rejection or stale-pixel evidence. It did
not exercise a real window drag, physical click/key delivery, or context-menu
placement, so those acceptance claims remain open.

The following narrower claims are supported:

- the server answers `/v1/version` and `/v1/diagnostics/runtime` over TCP;
- a fresh primary plus one `--join` can be created through the API when the
  primary establishes the Hibr hold first;
- the current live renderer matrix completed its focused synthetic workloads;
- the deterministic pointer-damage, motion-trail, wallpaper-stress, and Hibr
  frame-barrier gates pass locally;
- physical acceptance is **not measured**: the latest 20-second capture saw no
  physical evdev motion or button event, so it cannot diagnose real window
  dragging.

These results do not prove that the wallpaper, desktop scene, context menus,
window movement, physical mouse, keyboard, or joined coordinate system works.
An unchanged or incomplete screenshot is a failed visual result even when
timing counters are healthy.

### 0.1.514 performance and login-placement candidate

The source candidate for `0.1.514` retains the bounded integer-scale KMS
graphics path in [ADR 0102](adr/0102-kms-integer-scale-fast-path.md), adds
the persistent CPU scene described in [ADR 0103](adr/0103-kms-cpu-scene-cache.md), and
closes the auto-start login relocation invariant in [ADR 0104](adr/0104-login-target-relocation-and-pane-redraw.md): the session returned by
`/v1/login` is explicitly attached to the requested pane before topology
derives its surface assignment. This targets the observed mismatch where the
login resource named the lower pane while its session occupied the upper
pane. The package is not yet deployed and no live improvement is claimed.

### Latest live read-only boundary

On 2026-10-16, the final live observation query to `192.168.10.54` reported
`0.1.510`, SDL3/OpenGLES2, one 1920x2160@59.99 Hz output, one Hibr primary
surface in the lower pane, and one Hibr join surface in the upper pane. The
bounded final capture used for the renderer record had one inherited
startup missed deadline; the focused motion and composition probes added none.
A later readback showed five cumulative missed deadlines after the longer
service lifetime, while the current frame-rate window reported zero missed
deadlines and no diagnostic errors. This does not close physical interaction
acceptance.

## Non-negotiable claim rule

Do not use “stable”, “working”, “correct”, “complete”, or “fixed” without a
scope and matching evidence. In particular:

1. timing stability means only measured scheduling/presentation behaviour;
2. process stability means only lifecycle state and exit/restart behaviour;
3. visual correctness requires a screenshot or pixel assertion showing the
   expected complete scene;
4. physical input requires evdev traces from the device under test;
5. Hibr interaction requires evidence that Hibr received the event and changed
   its surface, not merely that Blit accepted or forwarded an event;
6. joined-desktop acceptance requires both pane screenshots, graphics state,
   coordinate traces, and a visible window/context-menu interaction;
7. if any required visual or interaction evidence is missing or contradictory,
   the overall result remains incomplete.

Never promote a synthetic API result, QEMU result, or partial screenshot to
physical-host acceptance. Record the exact evidence, limitation, and next
boundary instead.

## Correct live orchestration

The live host currently uses one physical output split into two panes: the
primary is the lower pane and the join is the upper pane. The safe sequence is:

1. query `/v1/version`, `/v1/diagnostics/runtime`, `/v1/topology`, and
   `/v1/terminal-sessions`;
2. remove only exited/stale Blit terminal-session resources;
3. start exactly one primary with the installed Blit Hibr entrypoint and the
   Hibr held-session name;
4. wait until the primary surface contains a real desktop/menu and has a
   nontrivial surface revision;
5. start one `--join` client against the same Hibr held-session name in the
   upper pane;
6. verify both sessions are `running`, both surfaces are distinct, and neither
   contains an exit/error line;
7. capture the screenshot and graphics/session snapshots before testing input;
8. perform physical input capture while a visible window is available.

Do not start a join against a dead or merely stale Blit primary. The observed
failure mode is a join with `hibr: hold: no such session`, followed by retained
old pixels if the exited surface is not removed. Do not interpret those pixels
as a current desktop.

The primary invocation is a normal create request using the installed
entrypoint, for example:

```sh
curl --fail --request POST \
  --header 'Authorization: Bearer TOKEN' \
  --header 'Content-Type: application/json' \
  --data '{"command":"/usr/share/blit/hibr/blit-session.hibr","arguments":["--session","desktop","--name","display-1"],"user":"osakka","display_id":"display-1","pane_id":"pane-display-1-2","surface_mode":"terminal"}' \
  http://192.168.10.54:8080/v1/terminal-sessions
```

The join uses the same command and session name with `--join` and the upper
pane. The exact pane IDs must always come from `/v1/topology`; never invent or
hard-code them in a client.

## Next investigation, in order

The next cycle is a rendering/interaction investigation, not a performance
claim cycle:

1. reproduce the complete expected Hibr scene in one primary pane and capture
   its terminal cells, graphics inventory, filtered traces, and screenshot;
2. determine whether Hibr emitted the expected Kitty payload at all; if it did
   not, stop at the Hibr boundary and do not blame SDL composition;
3. if the payload is present, compare graphics placement/source rectangles and
   scene signatures with the screenshot, first with one pane and then with the
   join;
4. only after a complete static scene is visible, run a physical capture with
   a visible window and correlate evdev motion/button -> Hibr surface revision
   -> render path -> presentation;
5. use the first missing boundary as the defect location and make one surgical
   change;
6. rebuild with TCC, run the focused renderer/Hibr gates, deploy the exact
   package, repeat the same capture, update the ledger, commit, and push.

Do not change Hibr source. If the evidence identifies a Hibr hold, viewport,
picture-propagation, or desktop-semantic defect, record or update the Hibr
issue and keep the Blit diagnosis separate.

## Open-backlog order

The open issues are worked in evidence order, not issue-number order:

1. **#86 — live Kitty wallpaper discriminator.** Capture Hibr's graphics
   decision and Blit's per-session graphics trace on the deployed binary. The
   first missing boundary determines whether the next change belongs in Blit
   or needs a separate Hibr issue.
2. **#84, #85, #87, #88 — renderer/protocol fixes.** These are implemented and
   pushed, but require package deployment and host acceptance before they can
   be closed.
3. **#76, #77, #78 — `console-blit` input/provider boundary.** Exercise only
   after a complete static scene is visible, so input evidence is not confused
   with a missing wallpaper or stale surface.
4. **#79, #20, #21 — hardware/renderer coverage gates.** These need GPU,
   multiple physical outputs, or high-refresh hardware and cannot be closed by
   a local SDL software run.
5. **#22, #27, #60, #64, #82, #83.** These are coverage, workflow, resize,
   persistent configuration, image-build, and login-manager work to schedule
   after the live rendering boundary is established.

This order is a triage record, not a second backlog. Gitea issues remain the
authoritative task records; this document records only the dependency order
and the evidence required to move between them.

## Evidence locations

Temporary captures are outside Git. The latest relevant local captures were:

- `/tmp/blit-clean-state-20261016/` — fresh primary+join screenshot; visually
  incomplete flat-colour scene;
- `/tmp/blit-physical-drag-20261016-174655/` — authenticated 20-second
  physical capture with no evdev motion observed;
- `/tmp/blit-physical-move-20261016-172710/` — earlier no-input capture whose
  screenshot contained the stale ended primary/join failure.

These paths are evidence for the current workspace only. Do not commit them,
their credentials, or generated packages. The durable summary belongs in
[`docs/verification-status.md`](verification-status.md).
