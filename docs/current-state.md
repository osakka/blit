# Current state and restart point

## 2026-10-19 backend parity implementation starts

The operator requested a release checkpoint before parity implementation.
Annotated tag `v0.1.553` points to the live package's source `86d28b2`;
the API version was verified as0.1.553 before tagging. The annotation preserves
issue#101's unresolved physical-motion performance boundary. Candidate0.1.554
remains undeployed, and its existing generated manifest remains untracked.

[Issue104](https://git.home.arpa/itdlabs/blit/issues/104), ADR0137 and the
[parity matrix](backend-parity-matrix.md) govern the next delivery. The first
test increment makes the SDL wallpaper-damage fixture selectable by video
driver/renderer, asserts actual runtime identity, discovers the target display
and explicitly attaches the session. Local software pixel checks pass; a dummy
driver with GLES2 requested fails identity validation, as required. Actual
KMSDRM/software and KMSDRM/GLES2 physical runs remain pending. No live backend,
session or Hibr code was changed in this cycle.

## 2026-10-19 live motion sample: input arrives; pointer/frame path is slow

Correction to the prior investigation: the operator confirms three active
physical screens—two DP panes and one HDMI screen. Blit represents the two DP
panes as one `DP-1` KMS connector/canvas (`display-93`, 1920x2160, split 1x2)
and the HDMI screen as a second connector (`display-102`, 2560x1440). The
observation API's two KMS outputs are therefore not a count of physical
screens. A cached screenshot for `display-102` looked black but contradicted
the live screen; it is withdrawn as evidence of the actual HDMI image.

During a roughly4.1s operator-driven physical mouse movement, evdev counters
increased by1,541 total events and1,024 motion events. The pointer moved from
the DP canvas onto HDMI and back; last physical-event age stayed at1–9ms.
Input dispatch is not shown pausing in this capture. Global
`runtime.performance.frame_rate_hz` readings fluctuated from19.858 to60.349;
that is a server composition metric, not completed scanout per display. Per-
output `present_calls` increased129 on DP and41 on HDMI during the interval,
but the pointer crossed outputs and submissions are not flip completions.

Blit0.1.552 was built from commit54fa37a, packaged against the exact pinned
Hibr0.99.225/ABI16 archive, and deployed through the authenticated update API.
The package's two independent builds were byte-identical; hash and provenance
are recorded in `releases/0.1.552.yaml`. The modified sibling Hibr checkout was
not changed.

During a4.19s read-only sample while the operator moved the mouse, the live
API recorded124 dispatched input events,341 evdev mouse events and220 relative
axis events. The MOSART mouse device recorded329 pointer events,117 report
syncs and212 motion-axis events; Blit's pointer-position trace added20 changes.
The server's rolling composition metric was5.907Hz. DP canvas `display-93`
recorded84 submitted frames and85 completed flips; HDMI `display-102` recorded
zero changed-frame submissions during this interval. Their independently
measured vblank rates were59.909Hz and59.999Hz. The pointer ended on DP output
0 at(564,1762). The loop-gap window maximum was10.327ms. These are not
equivalent measures: stable vblank confirms monitor scanout cadence, while
flip counters count framebuffer changes; the sample does not establish whether
the pointer crossed onto HDMI or whether HDMI should have changed. It does show
that the display clocks are not running at5.9Hz while the server's visible
content updates are much less frequent than60Hz.

The same window contained22 Hibr frame-barrier timeout traces, but the current
telemetry cannot associate a pointer change with a specific submitted flip or
prove that those timeouts delayed pointer composition. Candidate0.1.553 adds
per-output pointer-change attempts,
pending-flip deferrals and pointer-bearing frame submissions. The direct-KMS
gate injects a pointer move and asserts that request and submitted-frame
counters advance. This gives the next physical run a way to distinguish
input-to-damage from damage-to-page-flip. Do not change renderer scheduling
based on the barrier count alone. No live settings, sessions, or Hibr state
were changed during the sample.

The complete local `tools/run-gates.sh all` run passed **96 gates, skipped 7,
failed 0**. Its 120-second terminal soak completed with four sessions,
11,748 snapshots, 1,068 resizes, and no reported leak or stalled renderer.
Skips were the three Hibr adapter gates (no pinned adapter checkout supplied),
the opt-in QEMU oracle, package-artifact validation (run separately), and the
two live-host screenshot/multi-display gates (not enabled in the local suite).
This is local evidence only; it does not establish physical behavior for0.1.553.

After deploying0.1.553, an approximately8s physical-motion interval recorded
2,303 raw mouse events (1,489 motion axes, 810 sync reports, and2 button
events). DP canvas `display-93` submitted114 frames, including100 pointer
frames (130 pointer-intersecting attempts,30 deferred); HDMI `display-102`
submitted57 frames, including54 pointer frames (66 attempts,12 deferred).
Both outputs completed approximately as many flips as they submitted. This
shows real pointer-bearing frames reach both KMS outputs, but does not establish
60Hz cursor presentation or explain the user's perceived pauses. The interval
also contained two button events despite the no-click instruction, so it is
not a clean mouse-only sample.

A separate bounded synthetic sweep stayed on the DP canvas, restored the
original pointer, and sent360 API-injected positions at a requested60Hz. The
HTTP request median/p95 were19.99/23.01ms; all but6 request slots were late,
and the run took7.43s. Blit submitted224 DP frames (30.16Hz), with2 missed
deadlines,0 physical mouse events, and8.88% measured process CPU. The last
frame reported1.314ms draw,1.183ms copy,0.037ms submit, and396 damaged cells.
This is network-paced synthetic evidence, not a physical-mouse benchmark.
The next instrumentation step (candidate0.1.554) adds a count of accepted
pointer-state changes between raw evdev reports and per-output KMS requests,
so those stages can be compared without conflating them.

Candidate0.1.554 now builds with TCC0.9.27 and passes the full local suite:
**96 pass, 7 skip, 0 fail**. The120-second four-PTY soak completed with
11,364 snapshots and1,034 resizes. The API schema and observation checks
require the new counter; the mocked direct-KMS gate injects one changed
position and waits for the position, output request, and output submission
counters to advance. This validates the telemetry chain under the mock only.
Package and physical acceptance remain pending; the live host is0.1.553.

## 2026-10-19 live follow-up: synthetic cadence improved, wallpaper test interrupted

Blit0.1.551 is live on10.54. The authenticated desktop currently has two Hibr
pane clients (`blit-DP-1-p2` primary and `blit-DP-1-p1` join) on one physical
DP output at59.997Hz. HDMI-A-1 was present in an earlier observation and absent
in a later one; no Blit topology-setting write was issued, and the reason for
the inventory change is unknown.

The expanded `tools/probes/host_pointer_cadence.rb` accepts
`BLIT_POINTER_STEP_PX=1..40`, follows a bounded bounce path, and reports actual
output submissions/s and missed-deadline deltas. On the current one-output
desktop, API-injected 60Hz pointer sweeps submitted305 frames/6.025s (50.62Hz)
at1px/event and356/6.024s (59.10Hz) at8px/event; both reported zero missed
scheduler deadlines and zero physical mouse events. These prove neither
physical-device behavior nor window dragging. A separate earlier two-output
run submitted123 frames/6.023s (20.42Hz); topology and desktop state differed,
so this is a lead, not a causal comparison. #101 remains open.

The live framebuffer also showed a Hibr `htop` window listing eight processes
named `desktop [sess…]` at12.4–19.8% CPU each. This is an observation, not proof
that they are duplicate desktop owners. They were not killed or otherwise
changed. Blit's API verified one native desktop owner and its pane clients;
that does not inventory unrelated host processes.

Wallpaper acceptance is **not complete**. During an attempted native-panel
round trip, API-injected keys/clicks navigated the Hibr wallpaper UI
incorrectly. A later surface snapshot showed `All Displays`, a pattern glyph
and no image, while an earlier panel capture showed `retro_car.png` for
`blit-DP-1-p2`. The exact persisted state is therefore uncertain and the
original appearance is not verified restored. The last screenshot also showed
a live Terminal/editor; injected keys may have reached it, and whether its
unsaved buffer changed cannot be determined. I am not sending further UI input
until the active editor state and target are unambiguous; do not save/close it
or alter Hibr state as a cleanup assumption. Blit#103 remains open. No Hibr
source or settings files were edited directly.

## 2026-10-19 release551: cursor updates no longer wait behind scene fences

Investigation on550 found that pointer dirtiness was scheduled behind both
the terminal-output quiet interval and the Hibr joined-PTY scene barrier.
The barriers correctly protect complete terminal/image scenes, but also
delayed an independent software cursor. Candidate551 presents only the cursor
over the last committed KMS buffer or SDL scene texture at the output's normal
refresh deadline. Pending cells, graphics, effects and overlays remain held;
their damage and presented revision remain unchanged until the normal atomic
scene commit. Outputs without a committed cache defer the cursor safely.

Red/green evidence: the pointer-fence scheduler assertion failed on550, then
passed after the change. Production KMS tests compare every pixel across64
held-cursor frames in page-flip and modeset paths, including 64px shadow,
clipping, changed scene metadata, overlays and damage retention. The SDL
software-renderer test compares every pixel across12 held frames and verifies
the pending overlay appears only on release. Focused KMS and SDL tests pass.
This proves compositor behavior under the tested synthetic frames, not physical
input-to-photon cadence or Hibr window-drag performance. Run the host pointer
sweep after deployment and leave#101 open until measured evidence supports
closure. Frozen full suite:100pass/3explicit skips/0fail. The120-second
four-PTY soak completed11,216 snapshots and1,020 resizes. Explicit skips are
the standalone QEMU oracle, physical multi-display gate and live screenshot
backpressure gate.

Stable names are deployed and were observed in the live native client list.
Do not describe the current wallpaper as `retro_car.png`: the latest surface
snapshot instead reports All Displays / pattern / empty image, and no
post-restart persistence round trip is verified. Restore and verify through
Hibr's native Wallpaper UI before continuing#103; do not edit Hibr settings
files or infer a saved value from Blit's stable client names.

This is the handoff document for resuming Blit work after a session change. It
is intentionally evidence-led and does not replace the OpenAPI contract,
source, ADRs, or the verification ledger. It tells the next engineer where the
work actually stopped.

## Status at the last review

### 2026-10-18 release550 API-deployed and native joins verified

The authorized updater verified0.1.550 on192.168.10.54. Runtime source and
packaged documentation are pinned to a1ce53d in `releases/0.1.550.yaml`;
package SHA256 is52f469477d386592420fd26018d956d9bf940df261ba60b30060bd68281be53e.
TCC0.9.27 server rebuild is byte-identical and package rebuild reproducible.
Full frozen collection:99pass/3skip/0fail;120s four-PTY soak11272 snapshots,
1025 resizes. Skips: QEMU, physical multi-display gate and live screenshot
backpressure gate. Actual SDL pointer check separately passes25472 pixels;
its runner registration was added after that loaded collection, not counted
retroactively. Actual native duplicate/suffix/colon checks pass separately too.

Complete settings document/revision72, font scale1.0, pure `drm-kms`, both
physical output geometries and lower preferred primary are unchanged. After
authentication, three quiet samples verify UID1000 and one held desktop owner,
with primary `blit-DP-1-p2` at(67,0), upper `blit-DP-1-p1` at(0,0), and
`blit-HDMI-A-1-p1` at(0,240). Both saved auxiliaries are joined. Native names
are now stable; per-display wallpaper persistence after another restart remains
the final physical acceptance for #103, not inferred from these observations.

Default arrow/pearl24px/shadow is active. Read-only canonical-art framebuffer
oracles match142 opaque pixels on DP-1 and142 on HDMI-A-1 with exact hotspots.
The first DP capture precedes desktop first paint (black except pointer);
later `desktop-dp.png`/`desktop-hdmi.png` show wallpaper and application windows.
These are framebuffer samples, not proof of every application pixel or glass.
Mouse artwork/controls are delivered; #101 performance is NOT resolved.
Two550 asynchronous60Hz-intent probes record110/6.039s and115/6.125s pointer
output submissions, Blit6.62/6.08% of one CPU core. Different pointer locations,
desktop state and coalescing preclude a normalized548 comparison or a physical
60Hz/input-to-photon claim. Evidence is ignored under `diagnostics/mouse-550/`.

Published catalogue bundle0.1.5 includes Blit Mouse; docs at hibr-apps:f9e1ce9.
Against final550 source a1ce53d, installed Hibr0.99.250 ABI16 passes actual
Mouse callbacks, shared credentials/quiet polling, both native panes' slider/
save interactions and signed isolated catalogue install/update/remove.
Publishing/testing its isolated installation is not updating the user's live
catalogue app. Use catalogue refresh/update if the Mouse pane is missing.
No Hibr-core changes, SSH, renderer switch, login-policy edit or second owner.

### 2026-10-18 release-review correction:550 replaces unpublished549

The final549 collection was stopped deliberately, not reported as a completed
pass. Release review found the new pointer parser used an unscoped key search:
an opaque extension's nested `pointer` could be mistaken for appearance, and
duplicate top-level members could be accepted. The added API regression fails
549 (nested extension rejected400) and passes550. The canonical member iterator
now selects only the top-level object, rejects duplicates (including escaped
equivalent names), and decodes member names with the existing JSON decoder.
Unknown extension values are preserved and rejected writes do not mutate state.

549 remains an archived candidate with reproducible artifact identity, never
deployed/tagged.548 remains running on10.54.550 retains all stable-name and
Mouse changes; its final source/package freeze, full collection and deployment
are the next required steps. No Hibr-core modifications or renderer changes.

### 2026-10-18 stable identity and Mouse549 release preparation

Blit#103 now uses one helper for both launch paths: connector (otherwise SDL
name) plus row-major pane ordinal, e.g. DP-1 lower primary `blit-DP-1-p2`,
upper auxiliary `blit-DP-1-p1`. TCC unit checks cover changed runtime IDs,
long/unsafe selectors, bounded grammar, ambiguous outputs and occupied names.
Actual installed Hibr0.99.250/ABI16 joins, survives a Blit restart with the
same held owner and exact two names, then cleans up without aliases. The old
binary fails the new primary-name assertion. Duplicate exact native names
(observed in this Hibr release) and suffixed aliases fail closed. No Hibr-core,
backend, font or pane geometry change. ADR0135 documents live-reassignment
identity and one-time migration from timestamp names.

Catalogue0.1.5 is already published, with docs current at hibr-apps:ce8a5bc.
Actual Mouse callbacks, native drag/save, quiet15s shared polling/private
credential restoration and isolated install/update/remove pass with Hibr250
against549. Artwork source is08bddc9. Final frozen package/full run and live
deployment remain pending at this checkpoint; do not claim549 is installed.

The user has authenticated on548. A new bounded asynchronous60Hz-intent
probe applies360 motions/6.067s,0late/0skipped; pointer output211 submissions,
other output67, Blit9.81% of one CPU core. Last pointer-output draw0.101ms,
copy1.163ms, submit0.030ms,4requested cells. This is submission/counter evidence,
not completed scanout or physical input-to-photon/60Hz acceptance. Evidence:
`diagnostics/mouse-549/before548/pointer-cadence.json`. #101 stays open.

### 2026-10-18 mouse549 local checkpoint; stable names#103 next

548 is API-deployed on10.54, with complete settings/font and pure-KMS backend
preserved. Its focused terminal/renderer collection passes56/0skip/0fail;
package/manifest checks pass. Authentication was waiting at the last capture,
so no authenticated speedup claim follows.547 completed97/3skips/0fail.

Mouse549 adds shared antialiased arrow/hand/precision artwork, four palettes,
24–64px nominal size/shadow and shape-specific hotspots, independent of font.
Existing settings own appearance. Invalid objects are refused before mutation.
The initial local full run completes98pass/4skip/0fail (no package); new SDL
pixel test separately passes25472 pixels across3shapes/2sizes/4rotations.
Production KMS tests cover48 complete frames, edges, hiding and size/shadow
changes. A final frozen collection/package/deployment is still required.

The earlier stash0428727 has already been applied; do not apply it again.
Catalogue pane0.1.5 and our Mouse tests were committed/published by concurrent
hibr-apps work; current bundle/index checksum agrees at hibr-apps:962da97.
Preserve unrelated application updates. Retest both native panes, actual
Mouse sliders/save and signed catalogue before final delivery. Old docs
below describe the earlier unpublished checkpoint, not current publication.

The user now authorizes Blit#103: one shared stable native client name derived
from connector plus row-major pane ordinal, not startup time/runtime IDs.
Keep owner PID/start-time verification and fail closed on duplicate client
names. No Hibr-core changes, new API or geometry changes. Lower DP-1 primary
is p2, upper auxiliary p1. Implement/verify this before final549 packaging.

### 2026-10-18 pointer latency investigation:547 deployed,548 correction

The user still reports jerky pointer motion. The real host now has DP-1 and
HDMI-A-1, with three occupied native desktop panes.547 instrumentation is
API-deployed after58 focused schema/renderer checks and package/manifest checks;
its complete frozen collection is finishing separately. Preserve backend/font/
layout; no SSH or Hibr-core changes.547 is diagnostic, not a performance fix.

Live `kms_phases` separates draw (up to20.883ms in this collection), scanout
copy (about1.152ms) and submission (about0.032ms). Traces include changed
revisions with `damage_valid=0`, hidden cursor and full-view paint counts.
The common PTY parser unconditionally reported every consumed chunk as a
visual change.548 corrects that return to the canonical damage flag; parsing,
protocol replies and raw byte streams continue. Production regression is red
before correction for nonvisual controls/partial APC and green after, with
printable damage and fragmented UTF-8 preserved. ADR0134; Blit#101 stays open
until physical acceptance.548 package/deployment validation is pending here.

User also requests high-quality cursor appearance/native Mouse controls,
tracked in Blit#102/ADR0133 (working implementation). Its work is deliberately
isolated from the shared PTY correction in our named stash
`0428727a8127ab805481cfd35d984343112bfb19`; restore it after548 validation.
The hibr-apps worktree contains our unpublished pane0.1.5 Mouse changes and
tests. Do not discard those or claim they are deployed. Preview/evidence and
live phase samples are ignored under `diagnostics/mouse-547/`.

### 2026-10-18 deployed0.1.546 and catalogue pane0.1.4

The authorized API updater installed and verified **0.1.546** on192.168.10.54.
Pure `drm-kms`, font scale1.0, lower preferred primary, saved auxiliary intent
and both240x67 pane grids were preserved. Settings revision59 is clean. The
legacy DP-1 split is now durably stored as `display_layouts.DP-1={columns:1,
rows:2}`; a further API restart preserved the entire settings response and
revision. No SSH or Hibr-core changes were used.

The final frozen collection reports **97 pass,3 skip,0 fail**, including package
validation and a120-second four-PTY soak with11288 snapshots/1026 resizes.
The explicit skips are QEMU oracle, physical multi-display and live screenshot
backpressure. A separate byte-identical TCC server rebuild and reproducible
package rebuild pass. Source/package identity is pinned in
`releases/0.1.546.yaml`; the later fixture-only correction is commit7e4cd56.
The superseded545 was never deployed or tagged; its generated package was
moved into the ignored545 diagnostics archive rather than left as a handoff.

Canonical catalogue pane **0.1.4**, `hibr-apps:e309874`, is published with a
signed index. Actual callback/API, native slider-drag, private-token/native
preference-load-order and isolated catalogue install/update/remove tests pass
against frozen546. Update through `catalogue refresh` then
`catalogue update control-panel/blit`; reopen the pane. This publication does
not mean the user's installed catalogue app was updated by us. Font scale is
still global; independent per-pane font profiles are not delivered here.

Real-host bounded framebuffer validation passes21 cells/2688 pixels, including
right-edge cells and a bold diamond. Its temporary free-pane PTY was removed.
No additional process-CPU report was produced. The upper pane is now occupied
by the authenticated join and is not available for free-pane probes. Do not
delete session3: it is the policy-created join, not a leftover probe.

The user has now authenticated. A fresh native observation verifies one
desktop owner, lower primary at(row67,column0), upper joined client at(0,0),
and134x240 union. Session1 is focused; session3 is the join. Only DP-1 is
physically connected. The API screenshot shows the retro-car pixel wallpaper
on both panes and an About window below. This is visual evidence, not a
pixel oracle or general movement acceptance. The observation also reports
`input_device_missing`: open keyboards2/pointers2 are below this process's
peaks5/4. The source intentionally warns on that reduction, not just absent
inventory entries. Retain the observation; it does not prove current physical
input failure or successful KVM reattachment.

Two360-motion API probes are deliberately not60Hz stress tests. The login
screen run submitted711 output frames over24.83s at6.84% of one Blit CPU core.
After authentication, the same bounded pointer-only workload submitted369
frames over26.47s at3.55% of one Blit CPU core; sampled submission rates were
11.14–17.67Hz, native-owner CPU delta0, hotplug scan1.66ms/no worker error.
The pointer was near the pane boundary and no window drag/click was injected;
this does not establish Hibr drag performance or a maximum sustainable rate.
HTTP latency limits injection and the runs have different desktop workloads.
No physical multi-output speedup or display-refresh guarantee is claimed.

Next: finish physical second/third-output unplug/replug and authenticated
wallpaper/window-drag CPU/cadence acceptance in Blit#100/#101. Leave the
additional monitor connected for that measurement. The production fractional
image clipping bottleneck is fixed and locally proven (ADR0132); the user's
reported whole-machine slowdown remains open until that workload is measured.
Evidence: ignored `diagnostics/per-display-546/`, including `preservation.json`,
`final-live.json`, `authenticated-desktop.png`, `live-grid/`, `desktop-pointer/`
and `gates-final.log`.

### Historical pre-deployment checkpoint: candidate0.1.546

Latest validation progress: package `blit_0.1.546-1_amd64.deb` is built from
`d7515efd48988ed46e1bb266bb860c41bb4891a1`, SHA256
`c0d6e1c26970e4fef476b56b2f8ff6f6d6ac1dd1ec91347c20163b60f2a5223f`.
Manifest and reproducible package/server rebuild checks pass. The first546
collection reports95pass/4skip/1fixture failure: auxiliary policy sampled its
baseline before the newly durable startup save. Waiting for the existing
clean receipt fixes that attribution and the targeted controller gate passes.
No production binary change. A fresh full collection with package validation
enabled is running; do not deploy before its final result.

The catalogue pane is being advanced to0.1.4 for an actual native load-order
gap: Hibr sources CP panes before loading saved desktop preferences. Loading
the token at source time therefore used the default rather than the saved
network URL. A native-order regression fails0.1.3.0.1.4 loads on first use,
after URL restore, reloads on origin changes and explicit Reload, and preserves
the same private data format. No Hibr-core edits. Follow the latest
`hibr-apps` commit/evidence, not the earlier0.1.3 checkpoint below.

The0.1.545 binary completes96 gates with four explicit skips and zero failures
(120-second soak:11156 snapshots/1014 resizes), but an added startup-only
regression subsequently proves its migrated split was not written without
another settings mutation. It is **not deployed or tagged**. Its package
identity remains immutable in `releases/0.1.545.yaml`; do not install that
superseded candidate as the handoff.

0.1.546 adds the missing dirty transition to the existing settings writer for
non-default legacy grids. Default1x1 does not need an eager write. The added
test is red against the archived545 binary and green against546: the map is
present on disk before any API settings mutation. Hotplug tests wait for the
startup save to settle before asserting that hotplug itself does not write
settings. No additional persistence owner/controller is introduced.

The frozen546 binary is SHA256
`24596e3cfd2fde4eae443759609aefebc547599973288e3e7382bd8e7c755d2f`.
Its complete suite is running from the canonical binary; every gate's default
path therefore selects this same artifact. Logs are in
`diagnostics/per-display-546/`. Commit/package/deployment checks remain pending
at this source checkpoint. The panel implementation remains0.1.3 at
`hibr-apps:535cace`, with its signed catalogue and native-slider/token tests.

**Preserve fresh host state, not the earlier baseline:** latest read-only
pre-deployment observation still reports pure KMS0.1.544 and onlyDP-1, but
the user has changed scale back to1.0 (240x67 panes, originY4/1084), settings
revision58, with the same lower preferred primary and saved upper/HDMI-A-1
auxiliary intent. Re-read immediately before deployment; do not restore1.5
from the earlier diagnostic capture. The physical slowdown remains#101.

### 2026-10-18 candidate0.1.545: independent grids and bounded image repaint

Implementation is complete locally; final frozen-suite/package/deployment
checks are in progress, not yet physical acceptance. New `display_layouts`
settings entries are connector-keyed1–8 columns/rows, with64 live panes and16
configured-output/session limits. Startup migrates the legacy split for present
outputs only; newly hotplugged monitors default to1x1. Same-ordinal terminals
resize without process replacement; removing an occupied pane returns409.
Legacy bulk layout refuses exceeding saved capacity, including offline entries.

The fractional direct-KMS image scaler walked a full destination image even
for tiny damage. The production-scaler regression fails the old loop at38.1%
of full-render cost and passes the clipped-loop fix at0.041% (20 tiny repaints
183µs versus447398µs full). RGB/RGBA, source crop, negative origin and
outside-damage pixels agree. Existing128-frame full-reference composition
passes. This is a demonstrated local bottleneck/improvement, not a claim that
the real host slowdown is fixed. See ADR0132 and Blit#101.

The real host remains pure KMS0.1.544 during this candidate validation. Read-only
samples see onlyDP-1 at1920x2160/59.997Hz, scale1.5, two160x45 panes,
authenticated primary below and joined auxiliary above. Settings revision51
also requests HDMI-A-1/pane1, currently absent. Worker scans are2–3ms. A
bounded360-motion API workload produced492 presents over23.96s; its HTTP
latency limits injection, so it does not establish60Hz capacity or the cause
of the reported slowdown. Additional monitor reconnection was requested.
New runtime CPU counters separate Blit, verified native owner and host load.

Canonical catalogue pane0.1.3 adds native Hibr font/grid sliders, latest-value
font coalescing and private URL-bound token persistence. Actual Hibr pointer
drags reach columns8/rows3/scale1.2; URL survives `dt_keep`/`cp_save`; token
survives a new process, is masked/export-excluded and insecure files are
refused. The URL is normal desktop settings; token JSON is0600 inside0700
`$XDG_CONFIG_HOME/hibr/blit`, never the privileged updater token. Hibr core,
SDL sources and the host backend remain untouched. The pane requires
Hibr0.99.244 and exposes a clear server-upgrade notice on older Blit.

Mandatory next checkpoint: finish frozen suite, commit/package/verify/reproduce,
deploy through the API preserving font/lower-primary policy, verify the migrated
DP-1 grid and obtain actual multi-output CPU/cadence evidence. No SSH.
Ignored evidence lives in `diagnostics/per-display-545/`.

### 2026-10-18 deployed0.1.544: direct-KMS live output reconciliation

The user explicitly selected pure KMS/DRM and direct-backend hotplug, not an
SDL patch. The API created a scoped root operator process to back up
`/etc/blit/blit.env` as `blit.env.pre-kms-20261018` and change only
`BLIT_DISPLAY_BACKEND=kms`, followed by a supervised service restart. Fresh
sysfs connector observation identifies `DP-1` as DRM connector93; the saved
primary preference was deliberately translated to `pane-display-93-2` before
restart. Other saved policy, font and pane geometry remain unchanged.

The backend-switch checkpoint reported0.1.543, backend `drm-kms`, `direct_kms:true`,
video driver `direct-kms`, ready, lower login and both240x67 pane grids.
After the user authenticated, three read-only association samples pass:
verified UID/GID1000, native owner296618/starttime67930595, lower primary
`(67,0,67,240)`, upper managed auxiliary `(0,0,67,240)`, union134x240,
confirmed initial relative offset(-67,0). An earlier waiting-state probe was
invalidated by that authentication; it was not a backend defect or a reason
to restart another desktop. Evidence: `diagnostics/kms-hotplug-544/live/`.

The0.1.544 source implements ADR0130 in Blit only: one startup/hotplug output
lifecycle, read-only background DRM observer, stable slots and immutable
generation-tagged flip cookies, connected inventory/topology reconciliation,
zero-output recovery, and API hotplug diagnostics. Failed or unknown probes
retain last-known output state. Failed teardown quarantines its resources;
failed additions do not reset unrelated outputs. The current policy controller
handles saved auxiliary disappearance/return. Primary PTYs retain their pane
intent rather than silently migrating. SDL and Hibr sources are unchanged.

The regression against0.1.543 fails waiting for second/third outputs. The
frozen release binary passes93 suite checks with four explicit skips and zero
failures, including the120-second four-PTY soak (11,672 snapshots/1,062 resizes).
A separate64-cycle lifecycle/cookie stress gate passes386/386 buffer releases
with primary buffers unchanged and late events ignored. That additional gate
is now registered in the renderer suite; it is not part of this run's93 count.
Package artifact/manifest checks and reproducible rebuild pass against pinned
Hibr0.99.225/ABI16. Installed integration tests use Hibr0.99.244/ABI16.

The authorized updater installs/verifies exact0.1.544 on10.54, package SHA256
`bd9521943e054efb6ed25088b9c8f2b7d53fdd1c8ede4dc5b3046343082b5771`, built
from source/documentation commit `8f45ccc45d37407d3e207e32e629013dfb552b17`.
Before/after checks preserve the full saved document (clean revision44), active
font and both pane grids. The backend is ready `drm-kms`, `direct_kms:true`,
observer `uevent+periodic`, no diagnostic errors/warnings. Three background
samples show scans20/21/22, inventory revision1 unchanged, scan durations
2.811/2.218/2.196ms. The live framebuffer oracle passes21cells/2688pixels,
including the complete bold diamond at the right edge; its temporary upper
terminal is removed and lower focus restored. One lower login remains; the
saved upper `DP-1` pane1 binding waits for authentication. No manual join.

Evidence: `diagnostics/kms-hotplug-544/`, manifest `releases/0.1.544.yaml`.
Next: normal login, then connect an additional physical monitor without
restarting Blit. It should enter API inventory and the panel's next quiet
refresh. Select its auxiliary panes, Apply, and Save if desired. New outputs
inherit the current global split/font settings. Validate secondary removal/
return and a third physical output before closing#100. Physical multi-output
hotplug, multi-GPU support and automatic primary migration are not accepted
by this one-output checkpoint. SDL and Hibr source remain untouched; no SSH.

### 2026-10-18 quiet Control Panel and issue100 dependency finding

Panel0.1.2 is published at `hibr-apps:cd417d0`. A regression fails against
0.1.1 because an ordinary draw starts a request after four idle seconds. It
passes after replacing three-second polling/permanent one-second wakes with
quiet15-second refresh deadlines,250ms pending-job checks and15/30/60-second
failure backoff. Scratch search rows no longer start I/O. Callback/API,
catalogue lifecycle and actual installed Hibr UI gates pass; the generated
catalogue signature verifies. Existing installs can use `catalogue refresh`
and `catalogue update control-panel/blit`. No server restart is needed.

Issue100 requires more than Blit display event handling. Bundled SDL's
`KMSDRM_PumpEvents()` only polls input; connector enumeration/internal display
creation occurs in startup `KMSDRM_InitDisplays()`. There is no runtime
connector-to-SDL-display reconciliation to consume. Direction has been
requested for a documented version-pinned local SDL extension, considering
the vendored contribution guidance; no SDL edits have been made. See proposed
[ADR0129](adr/0129-sdl-kmsdrm-hotplug-dependency-boundary.md). Issue100 remains
unimplemented; do not claim that changing panel polling implements hotplug.
The accepted live0.1.543 desktop, renderer and native clients are untouched.

### 2026-10-18 authenticated acceptance and Control Panel rename

The user reports the joined setup looks perfect. A read-only live observation
corroborates the automatic attachment: verified UID/GID1000, native owner
293391/starttime67784223, lower named login client at `(67,0,67,240)` primary,
upper managed client `blit-aux-9cc7bc41f5-4` at `(0,0,67,240)` non-primary,
one134x240 native union. `/v1/auxiliary-displays` reports `joined` with
confirmed relative offset `(-67,0)`. No manual join or diagnostic movement
was needed for this checkpoint. This accepts the observed one-output split,
not arbitrary physical monitor counts or hotplug.

The Control Panel is now named **Blit**, published as pane0.1.1 in
`hibr-apps:2b2608e`. Callback/API, actual Hibr UI and catalogue lifecycle gates
pass; generated metadata/checksums and catalogue signature are updated.
Use `catalogue refresh` then `catalogue update control-panel/blit` for an
existing install. The catalogue identifier remains `control-panel/blit`.
No Blit server package change or live restart is needed for the rename.

Newly attached monitors are not yet reconciled live by the SDL server:
`discover_displays()` is startup-only and `service_sdl_events()` has no display
add/remove handling. Startup-detected outputs appear as selectable panes;
unknown outputs do not automatically become requested auxiliaries. Explicit
Apply/Save authorizes them. Issue[Blit#100](https://git.home.arpa/itdlabs/blit/issues/100)
tracks the missing live output/topology reconciliation and physical unplug/
replug acceptance. The existing working desktop was left untouched.

### 2026-10-18 deployed 0.1.543: automatic auxiliary policy and published panel

The authorized API updater installed and verified `0.1.543` on
`192.168.10.54`. Package SHA256 is
`c77a52a0ce12a71cf054fb9286cb3246647cd06edeca93bf4b6601e4ed4de96d`;
source/documentation commit is `61d4db1bfa90c67786a8890f100d3a500aa38c1b`.
Manifest and package artifact gates pass, including reproducible rebuild
against pinned Hibr0.99.225/ABI16. The host runs Hibr0.99.241/ABI16 and reports
ABI compatibility. Before/after observations preserve the complete saved
document, SDL `kmsdrm`/`opengles2`/`sdl-vsync`, font identity/scale and both
240x67 pane grids. No diagnostic errors or warnings were reported.

After that preservation check, a revision-protected settings replacement
adds the intended `{output:"DP-1",pane_ordinal:1}` auxiliary, preserving all
other settings. The durable receipt is clean at revision40. A separate live
Apply returns policy revision2 and `waiting_authentication/authentication_required`.
The single login terminal is focused on lower `pane-display-1-2`; the upper
pane is empty until authentication. A cached screenshot shows the login
manager's screensaver in the lower pane, not an authenticated desktop.

The canonical Blit Displays pane0.1.0 is published in `itdlabs/hibr-apps`
commit `2844df1`, with a generated, verified signed catalogue index. Install
using `catalogue install control-panel/blit`, then select Hardware → Blit
Displays in Control Panel. Its connection defaults to localhost; set the API
token in its masked editor. It never requests the privileged updater token.
Actual Hibr callback/API, catalogue install/update/remove, and UI/keyboard
gates pass against the final server binary. Local UI screenshots are in
`diagnostics/control-panel-543/`; deployment observations and screenshot are
in `diagnostics/auxiliary-543/deploy/` (ignored generated evidence).

**Resume here:** after normal user authentication, inspect `/v1/login` and
`/v1/auxiliary-displays` for exact native owner/client identity, automatic
upper attachment and confirmed one-time placement. Do not launch another
primary or a manual join to make this test pass. Physical post-login
acceptance, catalogue mirror availability, and general multi-output/hotplug
acceptance remain unverified. Blit#99 and hibr-apps#1 retain that distinction.
No SSH, Hibr-core changes, renderer change or credential bypass was used.

The entries below are dated historical checkpoints, not instructions to
repeat their older manual-join procedures or remaining implementation work.

### 2026-10-17 local 0.1.543: live Apply and catalogue pane delivery

The source now implements saved-default boot activation and volatile live
Apply with per-binding state, authenticated native standby launch, one-time
placement confirmation, user-move preservation and explicit stopped-client
retry. Synthetic controller and installed-Hibr standby gates pass locally.
The catalogue pane is implemented in `itdlabs/hibr-apps`; its callback/API,
catalogue lifecycle and actual Hibr UI/keyboard gates pass locally.
Physical host remains0.1.540; its history below must not be read as a543
deployment. Package publication and physical acceptance are the remaining steps.

API route correction: `identity` and `desktop` are objects returned by
`GET /v1/login`, not separate dotted URL paths. OpenAPI and the implementation
have one login resource; documentation now uses that canonical resource.

### 2026-10-17 deployed 0.1.540: correct the native join boundary

After the user's explicit deployment request and a preflight confirming they
were logged out, the API updater installed the exact validated0.1.540 archive
below and verified its live version. Before/after observations preserve SDL
`kmsdrm`/`opengles2`/`sdl-vsync`, font identity/scale, both240x67 pane grids and
the complete saved settings document (clean revision38). One lower login
terminal(id1) is focused; its identity is waiting and native association is
inactive/authentication_required with null owner/client. Three read-only
pre-authentication samples pass. The physical font/edge oracle passes21cells/
2688pixels, removes its temporary upper terminal and restores lower focus.
The post-update observation reports no diagnostic errors or warnings.
Evidence: `diagnostics/native-join-540/deploy/`.

After the user's normal login, the three-sample ready probe passes: UID/GID1000,
native owner254198/starttime64069385, exact named login client and projected
ownership role primary/desktop. One recognized API join(id3), with credentials
derived from that login (no explicit user), is attached to the upper pane.
Initial logical coordinates are derived from its actual67-row grid, not an
installed hardcoded layout. A temporary scoped native CLI observer confirms
the exact name `blit-upper` at `(0,0,67,240)`, non-primary, in union134x240.
The login remains native primary at `(67,0,67,240)` with the same owner lifetime.
The observer terminal is deleted; only lower login1 and upper join3 remain.
A passively collected real upper-pane press reports target3/input3/local
column95,row21, corroborating the input boundary without injected clicks.

**Visual acceptance is not complete:** both joined captures show a dark empty
rectangle in the lower pane beneath the Control Panel's upper-pane position.
The lower surface snapshot contains1820 blank cells in the sampled rectangle,
all with background `#120428`. It is already in terminal cell state, so stale
SDL pixels alone cannot explain it. This does not decide between Blit's parser
and incoming terminal output; do not blame Hibr or change its source on that
evidence. Capture/stream comparison is the next rendering investigation.
The screenshots show wallpaper in both panes and the menu bar on the lower
primary, not a universal rendering/input pass. Evidence:
`.../deploy/{joined,settled}.{bmp,png,json}`, `lower-surface.json`,
`upper-native-query.json`, `pointer-buttons.json`.

The user then reported that moving the window cleared the orphan background.
The new read-only `after-user-move` capture no longer shows the separate empty
rectangle: About is in the upper pane, Control Panel and Task Manager in the
lower, with the lower primary menu bar. The old sampled region is no longer
uniformly blank: it contains wallpaper half-blocks and actual window contents.
Remaining dark cells overlap the current Control Panel, so their count must
not be called residual damage. The same native owner and layout remain active;
no restart, move or injected input was performed by Blit diagnostics. This
supports repaint-dependent stale content, not a proven detached layer or a
fixed defect. Capture the responsible join/move output and compare stream
versus parser state before assigning cause. The current surface captures have
no Kitty/sixel records; the wallpaper sample includes `▀` cells. That is
evidence for this half-block scene only, not acceptance of pixel graphics.

Saved automatic auxiliary policy is still unimplemented: this is one explicit
API attachment for physical validation, not boot/login provisioning. No
Hibr-core edit, SSH or renderer change. The validation history below describes
the pre-deployment checkpoints, including the old preserved0.1.539 desktop.

The 0.1.541 implementation extends native association with the bounded named
client list from the same `hold clients` query, including signed origins. The
0.1.542 source candidate adds validated, restart-persistent
`document.auxiliary_displays` bindings in Blit's existing settings store. TCC,
the full settings persistence gate (including restart and invalid-replacement
cases), and OpenAPI schema gate pass locally. These are prerequisites, not
placement automation or a second layout store. Neither candidate is newly
deployed to 10.54. Standby lifecycle and the control panel remain outstanding.

0.1.543 now implements ADR 0128: volatile API Apply separate from saved
defaults, per-binding reconciliation status, bounded asynchronous native
standby startup, and one-time placement confirmed through native inventory.
The local synthetic controller and actual installed-Hibr standby gates pass,
including user-move preservation, native release, explicit retry, logout
cleanup, and saved defaults at restart. These are local results, not a 10.54
deployment or desktop auto-invite acceptance. The next delivery is the native
catalogue Control Panel plus package/broad validation and physical acceptance.

The next policy boundary is now pinned in ADR0124 after reviewing Hibr's
documented `desktop --standby` lifecycle: it waits, can autojoin, remains alive
when Hibr deliberately lets it go, and waits for explicit reattachment instead
of forcing itself back in. Blit should provision that named client only after
fresh authenticated ownership, then apply a configured initial position once
when native `hold clients` first reports it attached. Later Hibr Displays moves
must remain session choices. A user-quit standby is not automatically respawned.
The API must report desired/effective states and preserve stable output plus
pane-ordinal bindings. This is a design decision, not implemented behavior;
no Hibr source or state files were changed/read for runtime control.

The user clarified that manual join commands are not the deliverable. Continue
ADR0124's saved API-owned auxiliary lifecycle, not an operator recipe. Its
first necessary dependency is now implemented under ADR0127: remove obsolete
horizontal placement/input projection, admit recognized joins against the
fresh authenticated native login association, derive the correct child user,
and retire clients asynchronously on login/owner-generation replacement.
Explicit initial native coordinates never move the primary. Native standby
is classified as an auxiliary, not another desktop owner. No Hibr-core edits.

The revised raw-PTY regression fails against0.1.539 (target2/input1/column129)
and passes against the TCC candidate (target2/input2/local column1). An installed
Hibr native CLI oracle independently verifies a134x240 vertical union and
exactly one SGR coordinate rewrite, preserving press/release/modifier/wheel
and keyboard bytes. The credential-dropping fixture checks login-owned joins,
identity/occupied-pane/placement rejection, token removal and logout cleanup.
Expanded coverage also passes owner-replacement cleanup, preservation across
temporary query failure and refusal of new joins after observation expiry;
maximum API latency in that controlled run is56.43ms. Derived native children
additionally close inherited listener/device/PTY descriptors before exec.
Full local/package validation passes90 checks with three explicit skips and
zero failures. This is local boundary evidence, not physical acceptance or
completion of automatic policy.

Package `blit_0.1.540-1_amd64.deb` is built from source/documentation commit
`22acbd61e76806f62c33e630dd24e8c3a3c5d8ba`, SHA256
`7df991c9e2e724ffa44ee3f9b471222f613ae9359e1f0db5daabe85253e137ec`.
Manifest/artifact validation and a reproducible rebuild pass. The package
gate now pins the changed `blit-session.hibr` bytes as well as the login and
observer entrypoints. Later test-only improvements exercise the actual Blit
launcher against installed Hibr0.99.240 ABI16, all three button press/release
pairs, modified drag, both wheel directions and keyboard. They do not alter
the production source/archive identity. The final run includes the120-second
four-PTY soak:11,312 snapshots and1,028 resizes. Skips are QEMU, physical
multi-output and live screenshot backpressure, not implied passes. Evidence:
`diagnostics/native-join-540/all-gates.log`.

Before deployment, the host remained0.1.539 with its authenticated lower
desktop preserved pending safe user clearance.
A final read-only observation shows one focused lower login terminal(id3),
verified UID1000 and native owner251045; the user has authenticated again
during development. No join or other live mutation was performed by this
cycle. Evidence: `diagnostics/native-join-540/live-preserved.json`.
Next: implement the ADR0124 saved auxiliary policy, native standby lifecycle,
and one-shot initial placement in the existing controller/store. The API must
apply/save intent; the user must not launch joins manually. Deliberate Hibr
release/rearrangement must not be undone by continuous placement assertions.
Stable selectors/failover and the catalogue panel remain staged.

### 2026-10-17 deployed 0.1.539: native login association

Following the user's logout/restart clearance, the API updater installed and
verified the immutable 0.1.539 archive below on 192.168.10.54. Before/after
observations preserve the complete saved settings document (clean revision38),
font identity/scale, both pane grids and SDL `kmsdrm`/`opengles2`/`sdl-vsync`.
One login terminal (id1) is focused on the lower pane. The read-only physical
pre-authentication probe passes three samples: identity waiting, association
inactive/authentication_required, null owner/client and no native query.
The upper pane remains empty after the temporary font oracle passes21cells/
2688pixels and removes its diagnostic terminal. A screenshot shows the lower
login prompt with its animated background, not an authenticated desktop.
Evidence: `diagnostics/association-539/deploy/`, `.../waiting/`, `.../pixels/`.

After the user's normal login, the installed native association passes its
read-only ready check: verified UID/GID1000, named client
`blit-login-638499997805-1`, held owner PID249984/starttime63853831 and a primary
67x240 client at native row0/column0. Three samples separated by3.2s preserve
the same owner lifetime and observe refreshed timestamps; their ages are
789203,150910,625710us, below the3s TTL. This is actual installed native-client
acceptance, not an inference from fixtures or the old unnamed client.
Evidence: `diagnostics/association-539/ready/`. The desktop screenshot shows
retro_car wallpaper and the menu bar on the lower pane, with the upper empty.

**Next boundary:** retire obsolete native horizontal input projection and
integrate supported standby/release joins (#96), using this verified identity
and association rather than guessing an owner. Automatic joins are not yet
implemented. No join was started, no Hibr core was edited, and no SSH was used.
The paragraphs below retain candidate validation history; references to the
old host state and pending physical checks are historical.

ADR 0126 adds the `desktop` object in `GET /v1/login`: a read-only native hold observation,
separate from the historical handoff sender PID. The packaged hook now names
its desktop client uniquely. One bounded, credential-dropped helper uses
supported Hibr CLI queries outside the presentation loop. It verifies the
exact client, geometry and native program's kernel UID/start-time identity;
observations expire, and logout/replacement cancels queries without touching
the held desktop. No legacy role/input-projection flags or renderer changes.

The 0.1.538 binary fails the new association regression (API absent). The TCC
candidate passes identity tests, a real installed-Hibr isolated hold/client
oracle and synthetic native-result controller tests, including UID/geometry/
malformed/size/deadline failures, shell/wait gating, stale observation and
nonblocking logout. Full local/package gates now pass; physical deployment
and named-client acceptance remain pending. The candidate
does not automatically join or rearrange anything.

A scoped API query on the unchanged 10.54 host reports UID1000, native session
`desktop attached 247920 67x240`, client `client-4 0 0 67 240 1`. Its temporary
upper diagnostic terminal is removed and lower focus restored. This is actual
native CLI evidence, not yet association with a Blit-owned name: 0.1.538's
existing client is unnamed. Do not retrofit/guess that binding. The host remains
0.1.538, logged in; safe deployment and a fresh login will be required after
candidate gates pass. Evidence: `diagnostics/association-539/live-query/`.

The package is built from source/documentation commit `5735627`, SHA256
`30daa8356f5068e8af101f8e90c6c62d4bc89754e256d69f5bfad91198f7eb35`.
Artifact validation and reproducible rebuild pass. The package gate also
compares both changed Hibr entrypoints to the manifest's source commit.
The full run with this exact package finishes89 pass / 3 skip / 0 fail,
including package verification and the120-second four-PTY soak (11,480 snapshots,
1,044 resizes). Skips are QEMU, physical multi-output and live screenshot
backpressure. No live update yet; user logout/restart clearance and a fresh
login are needed for the actual named-client association check.
`tools/probes/host_native_association.rb` is a read-only physical acceptance
check for pre-authentication waiting and exact named-client readiness, including
refresh after quiet intervals longer than the observation TTL. Its first run
against live 0.1.538 rejects the absent contract without mutations. Use distinct
evidence paths for subsequent waiting/ready samples.

The read-only acceptance client now passes against the controlled local
association fixture, including refresh across3.2s quiet intervals. An isolated
fixture startup race was diagnosed and reproduced: its synthetic observer used
exit1 before the target published its PID, unlike the production helper's
waiting/owner_missing response. The corrected fixture delays publication500ms
to exercise that ordering, and retains exact rejection-reason assertions.
Production source/package identity is unchanged. See the verification ledger
for the failed isolated run and deterministic old-fixture reproduction.
The final broad run uses these corrected fixtures and passes. Later commits
change tests/evidence only; the production source and package remain pinned
to5735627 and the recorded SHA256.

Next after physical named-client acceptance: retire obsolete native horizontal
projection and integrate supported standby/release joins in one coherent
semantic change (#96), without competing placement controllers (#99).

### 2026-10-17 deployed 0.1.538: authenticated identity boundary

ADR 0125 and the schema add `identity` in `GET /v1/login`. Only Blit's default login
hook participates: a private inherited datagram capability crosses Hibr's
environment-clearing authentication boundary, then kernel sender credentials
and login ancestry are verified. The capability is closed before desktop or
shell exec. This changes no renderer, font, geometry or Hibr source. It does
not set legacy desktop-role flags or create joins.

The 0.1.537 binary fails the new regression because identity is absent.
The TCC 0.1.538 candidate passes actual credential-dropping/clean-environment
Hibr-hook fixtures for desktop and shell, rejects root/invalid/oversized/FD
passing/foreign-ancestry messages, leaves custom commands unchanged and drops
identity on login destruction. This is not actual PAM acceptance. The full
local run passes 86 tests, skips four and has zero failures, including the
120-second four-PTY soak. Package verification passes separately. Following
the user's restart clearance, the authorized API updater verified 0.1.538.

The source/documentation commit is `30cdfdb`; the 0.1.538 archive and generated
`releases/0.1.538.yaml` have passed artifact verification and reproducible
rebuild (SHA256 `46ea81c1c116592d04b727f6934f51a787be94ab5e455c29cd1f0bdb83cf6130`).
The four suite skips are package (verified separately), QEMU, physical
multi-output and live screenshot backpressure. Before deployment, the existing
logged-in desktop was preserved until the user gave restart clearance.
Evidence: `diagnostics/identity-538/before.*`.

The deployed host preserves SDL `kmsdrm`/`opengles2`/`sdl-vsync`, font identity
and scale, both 240x67 pane grids and the complete saved settings document
(clean revision 38). A single focused lower login terminal initially reports
identity waiting with null credentials. The physical edge/font oracle passes
21 cells/2688 pixels again; its temporary upper terminal is removed and lower
focus restored. After login, the `/v1/login` identity reports verified UID/GID
1000, sender PID 247917, session kind desktop. This sender PID is historical
handoff identity, not the held desktop owner. The final observation has one
login terminal (id1), zero diagnostic errors/warnings, and the capture shows
the lower desktop with retro_car wallpaper; upper pane remains empty.
Evidence: `diagnostics/identity-538/deploy/` and `.../pixels/`. No Hibr-core edit,
SSH, renderer change or join start. This verifies the installed login handoff,
not native owner association, automatic joins or universal rendering quality.

Next: query native per-user owner readiness and integrate standby/release
semantics (#96). Identity alone is
not a held owner and must not trigger joins. Issue #99 and hibr-apps #1 remain
open; no control-panel bundle is published.

### 2026-10-17 deployed 0.1.537: persistent desktop policy foundation

The user approved a general API-owned lifecycle/configuration system and a
native Hibr Control Panel distributed via `itdlabs/hibr-apps`. Issue #99 and
ADR 0124 record the staged contract. The physical host now runs 0.1.537;
renderer, font and pane geometry are unchanged.

The source adds startup-applied `document.login`, explicit preferred
versus assigned pane, missing-target wait/first-available policies and
effective/restart reporting. The shared settings store now exposes save
status and returns 503 on forced-save failure, preserving the old file until
atomic replacement instead of unlinking it first. The old staged binary
fails the actual disk-error regression; corrected focused tests pass including
root login selection. The all-suite execution has 85 passing tests and three
explicit hardware/QEMU skips; its one failure was the package gate comparing
documentation against newer HEAD instead of its recorded commit. The corrected
package gate subsequently passes, including a reproducible rebuild with the
same archive hash. See the verification ledger for the exact chronology.

The updater verified 0.1.537. `host_login_policy.rb` then saved through the API
and restarted it once: a new login child appears on the lower pane, preference
and assignment both `pane-display-1-2`, clean saved revision 38, no pending
login-policy restart. SDL `kmsdrm`/`opengles2`/`sdl-vsync`, 930 font glyphs at
8x16 scale 1, a 1920x2160 output and two 240x67 panes remain unchanged. The
physical pixel oracle again passes 21 cells/2688 pixels. Only one focused lower login terminal remains;
the temporary probe is deleted and the upper pane is empty. Evidence:
`diagnostics/policy-537/`. The host reports Hibr 0.99.235, ABI 16 compatible;
no Hibr update or source edit was performed by this cycle.

This is not automatic joins or failover. Hibr's existing `desktop --standby`
distinguishes waiting from deliberately released clients; reuse its supported
contract after verified authentication/owner association (#96). The local
hibr-apps repo is clean and points to itdlabs/hibr-apps; its cp_pane framework,
catalogue generators and async job patterns have been inspected, not modified.
The panel is tracked in hibr-apps #1 and is not published. The user has agreed:
move/primary/detach in Hibr's existing Displays pane stay session-only until
explicitly saved into Blit. No automatic persistence or competing placement
controller. Apply versus Save is the intended UX; automatic remembering can
later be a reversible policy, not a second implementation. The next step is
verified authenticated association before standby joins (#96), not more
manual joins against an unregistered login session.

### 2026-10-17 deployed 0.1.536: edge fit and complete glyphs

The user requested correction of the clipped right edge and icon shapes.
[ADR0123](adr/0123-centred-grids-and-complete-font-rasters.md) records two
reproduced defects: redundant4px presentation inset beyond the already
centred grid (#97), and overwide fallback outlines cropped into8px (#98).
Both regressions fail before correction. Version 0.1.536 sets the extra
inset to 0 without changing logical dimensions; fallback rasters fit complete
ink bounds, and diamonds have symmetric regular/bold geometry. The generated
font has 930 glyphs at unchanged 8x16 metrics. The complete renderer/terminal/font
rerun passes 58/58 with no skips; package reproducibility also passes.
`releases/0.1.536.yaml` records the deployed artifact identity.

The authorized API updater verified 0.1.536 on 192.168.10.54. SDL GL remains
`kmsdrm`/`opengles2`/`sdl-vsync`, scale 1, with no diagnostic errors/warnings
at the post-restart check. Each pane remains 240x67; origins are (0,4) and
(0,1084), with diagnostic extra inset 0. The physical-host edge/font oracle
passes all 2688 pixels across 21 cells, including both edge cells, a bold diamond
in the last column and 18 selected normal glyphs. This proves those bounded
geometry/bitmap assertions, not universal font quality or frame-rate acceptance.
Original base glyph bitmap bytes 0–511 are unchanged. Issues #97/#98 are
closed with this regression and physical-host evidence.

The restart left exactly one lower login-manager terminal (id 1 at this check),
focused, and the upper pane empty after temporary-probe cleanup. The boot
configuration now explicitly selects `BLIT_LOGIN_PANE_ID=pane-display-1-2`.
The user has been asked to authenticate before the native upper join is
restored; do not attach it to the login manager or start a second primary.
Evidence: ignored `diagnostics/grid-font-536/`, including physical verdict,
framebuffer and post-update topology/font/backend identity. No SSH or Hibr
source changes. Issue #96 remains the separate native-join lifecycle gap.

### 2026-10-17 requested bottom-primary/top-join live setup

SDL GL and package 0.1.535 are unchanged. The existing authenticated login
PTY initially id5/surface-5 moved to `pane-display-1-2` (bottom). One native
`desktop --join` terminal initially id9 ran as osakka in the upper pane,
named `blit-upper` in Hibr. At that attachment the held owner stayed PID239437:
primary client-4 at `(67,0,67,240)`, join at `(0,0,67,240)`, union134x240.
This is a real vertical join, not a mirror or a second primary desktop.

[ADR 0122](adr/0122-native-hibr-join-orchestration.md) records the native
per-client PTY/input boundary and [issue #96](https://git.home.arpa/itdlabs/blit/issues/96)
tracks the API role/association gap: both sessions currently report role
none, because the login wrapper is not registered as a desktop primary.
Do not mistake those flags for evidence that Hibr has two desktop owners.
Hibr on this host is now0.99.232 ABI16; #238/#240 are closed and its native
2-D hold contract was inspected. Blit/Hibr binaries and Hibr source were not
modified for this operation. Temporary diagnostic terminals were removed.

Evidence: ignored `diagnostics/join-20261017/`, including before/after
topology/sessions, native hold placement/unchanged owner, screenshots and
context-attempt cell captures. Upper input trace reports target/input id9.
Earlier captures showed dark residual rectangles in the lower pane after
relayout; their cause and interactive acceptance are not established. The user has been
asked to validate both panes; no more injected input is being sent during
their test. Restart/login-before-join automation is not implemented here.

During user testing the lower login PTY was replaced by id11 and Hibr's held
owner changed to PID240371; `hold clients` then showed only the primary. The
old join9 was explicitly deleted and native join13 attached to this current
authenticated desktop. Final verified native rectangles are again primary
`(67,0,67,240)` and join `(0,0,67,240)`, owner240371, union134x240. Evidence:
`final-native-geometry.json`, `final.png` and headers. This demonstrates the
missing reconnect orchestration in issue96; it must not be hidden by the
earlier single-owner-continuity result or described as automatic recovery.
The final empty-desktop capture has wallpaper in both panes and the lower
menu bar, without those earlier window rectangles. That does not prove their
cause fixed under application movement.

### 2026-10-17 operating-mode change: SDL GL, same 0.1.535 package

After tagging the direct-KMS working checkpoint, the user requested SDL GL.
The host now reports `backend=sdl3`, `direct_kms=false`,
`video_driver=kmsdrm`, `renderer=opengles2`, and
`presentation_sync=sdl-vsync`, ready with no diagnostic errors/warnings.
This is SDL on bare-console KMSDRM, not X11 or Wayland. The 1920x2160 output
and 1x2 split are unchanged; SDL names the output `display-1` rather than the
direct-KMS connector identity `display-93`. There is one focused upper-pane
login-manager session and an empty lower pane. The post-switch framebuffer
already shows the Hibr desktop with Control Panel, Terminal and Task Manager;
no inference is made about how the authentication/reattachment occurred.

The renderer request was persisted through `/v1/settings`, preserving the
rest of the document. The existing boot-owned backend selector was changed
to SDL in `/etc/blit/blit.env`, with `SDL_VIDEODRIVER=kmsdrm`; its protected
backup is `/etc/blit/blit.env.pre-sdl-0.1.535-20261017`. This narrowly scoped
configuration/restart ran through the authorized terminal API using Hibr,
not SSH. No binary, Hibr source or tag was changed. Live two-corner damage,
upper/lower half-block colours and large split-UTF-8 PTY probes pass on SDL.
Evidence: ignored `diagnostics/renderer-switch-20261017/`.
The earlier movement acceptance below is for direct KMS, not automatically
SDL GL; the captured SDL desktop alone does not establish interactive motion.
A broader cell-colour probe first straddled replacement of surface-1 by
surface-4. Later samples flagged one cell and four cells, but the operator
was moving the pointer and the checker cached its exclusion positions only
once. These are unclassified observations, not established SDL defects.
The operator probe now brackets each capture with pointer state and discards
motion-skew samples. The next attempt correctly refuses retained graphics,
now present in the live surface. Do not report complete SDL pixel agreement
from the focused probe passes. No runtime code/configuration changes followed.

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
