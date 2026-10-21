# Current state and restart point

SDL multi-monitor autodetection/hotplug parity is still pending acceptance
under internal104: discovery, unplug/replug, stable identities, saved layouts,
pane/session placement and input routing. The0.2 shared-damage fixtures do
not certify it or authorize a backend switch. Live remains native KMS589.

## 2026-10-21: 125 protocol-image damage extension

Candidate0.2.0 no longer unconditionally damages an entire terminal on Kitty
upload completion/deletion or sixel completion. Producers mark current/removed
placement bounds to wake the existing scheduler; the shared last-composed
scene comparison adds old/revealed coverage. No duplicate scene or decoder.
Partial uploads stay hidden and terminal damage still accumulates (ADR0177).

The new parser regression fails on the previous full-surface upload handler.
Sixteen real protocol frames compare incremental output with full redraws:
chunked Kitty upload, multiple placements, placement-only/data deletion,
opaque/transparent sixel and removal over retained wallpaper. Software/GL
checks include synchronized, masked and pointer-held attempts; native retains
its36-step compositor/deferred-flip oracle. See verification ledger for receipts.

A read-only live5.512s window confirms589/native KMS, two outputs at roughly
60Hz,50.71% of one server CPU core and67 missed deadlines. Final sampled draw
phases18.109ms and20.595ms exceed a16.67ms budget; these may repeat stale frames
and are not per-frame percentiles or a controlled Kitty comparison. No live
writes/deployment. Native CPU composition/copying, GPU work and matched live
acceptance remain under125/122;104 still gates SDL multi-monitor hotplug parity.

## 2026-10-21: 125 stage two — image ingestion passes removed

Development0.2.0 computes each immutable Kitty chunk length once instead of
inside its per-byte loop. The TCC counted-call fixture falls from4,097 strlen
calls to1 for4,096 payload bytes (logical length-scan sum16,785,409 to4,097;
not hardware bandwidth or live frame timing). Binary/tail fragmentation,
padded endings, whitespace, rejected inputs and capacity guards are checked.
SDL now submits existing packed RGBA bytes directly to UpdateTexture, avoiding
an extra allocation/full-image copy. RGB24 retains exact RGBA conversion;
texture reuse, pixel generations, alpha and pitch are unchanged (ADR0178).
Driver-internal copying/GPU uploads are not claimed eliminated.

The counted-call regression fails on old code; baseline observation also
shows old RGBA submissions used a different staging buffer. New software/GL
fixtures pass exact SDL input/pitch/alpha and reuse checks. New fixture
binaries are invocation-isolated to prevent parallel-suite build collisions.
Live remains589/native KMS; no Hibr/settings/backend/session changes.125
remains open for bounded protocol-image-update damage, native copy/GPU work
and matched real video/input latency acceptance.104 still gates SDL hotplug.

## 2026-10-21: 125 stage one — retained placement damage implemented

Development0.2.0 now compares immutable presented placement descriptors at
the shared renderer boundary (ADR0177). Kitty placement commands accumulate
old/new coverage instead of invalidating the whole surface. SDL and native
KMS accept the same prepared graphics damage; native per-surface identity
avoids treating another pane's graphics as its own mutation. Unknown callers
retain conservative fallback. Snapshot acknowledgement follows actual surface
composition, not a deferred/held render attempt.

The new command regression failed on the previous full-surface behaviour.
Production pixel tests now compare36 incremental steps against forced full
redraws on native DRM-mocked KMS, SDL software and offscreen OpenGLES2:
exact pixels, translucent moving image, stationary overlapping image,
wallpaper, z/crop, changed pixels, deletion/reappearance and negative offsets.
SDL movement causes no image reupload or full renderer clear; native draws
fewer than the full480 fixture cells. Synchronization, masked outputs and
native flip deferral retain damage/snapshot state. These are local fixtures,
not a real-host FPS result. Existing renderer59pass/1skip, GLES11/11, and
schema/terminal/lifecycle34pass/1skip pass before adding the new suite hooks.

Live remains589. No backend/layout/Hibr changes or SSH.125 stays open:
protocol image-upload/delete paths are still conservatively full-damage;
image repacking, native whole-output copies, retained GPU batching and real
video/input-latency measurements remain. Do not call stage one a Kitty win
or a released0.2.0. See the verification ledger for the final timing receipt.

## 2026-10-21: deeper Kitty graphics architecture investigation

Internal125 under122: see[the source audit and production probe](kitty-rendering-investigation.md).
Placement-only Kitty commands preserve pixel generation but invalidate the
entire surface; native graphics-change bounds include unchanged wallpaper;
native composition is CPU pixel work followed by a whole-output copy.
SDL already reuses unchanged image textures, but still suffers blanket
surface invalidation and stages/copies complete changed images on the CPU.
This materially exceeds a glyph-cache issue. The local production-function
probe reproduces these work shapes without live writes. Proposed next step
is canonical per-placement damage and retained GPU scene work, preserving
atomicity/layers/panes and backend parity, not another parallel renderer.
Live remains589/native KMS; fresh last-frame draw25.937ms/copy0.854ms is an
uncontrolled sample, not a YouTube benchmark or measured cause attribution.
No source fix, Hibr changes, restart or backend switch in this investigation.

## 2026-10-21: 0.2 performance development; stable589 preserved

The existing annotated v0.1.589 tag is verified on the public mirror; do not
retag or overwrite it. packaging/VERSION is now0.2.0 for development, not a
released/deployed0.2.0. Live host remains589; no backend/settings/session
changes. Internal Blit122 tracks the[performance programme](performance-0.2.md).
ADR0176 establishes four-way shared raster reuse within the existing4MiB
pixel budget. The production cache workload eliminates28,800 repeat
rasterizations per39,200 warmed ASCII+Hibr-UI lookups for each profile;
glyph pixels/metrics are unchanged. This is not a measured live FPS gain.
Final GLES11/11 and isolated timing4pass/2live-only skips/0fail pass;
Arabic/emoji/all-size/colour-pressure/backend and Valgrind checks pass.
Initial standard142pass/9skip/3fail is preserved in the verification ledger,
with all failed gates subsequently passing corrected/isolated rechecks.
Input-fixture intermittency is tracked in124; no clean full-suite or live
speedup claim is made. Candidate remains undeployed.

Read-only live diagnostics show direct-KMS CPU drawing beyond a60Hz frame
budget in sampled frames. Existing APIs and the bounded read-only
`tools/probes/performance_baseline.rb` separate this observation from a
controlled benchmark. Next: function-level CPU/damage attribution and a
matched live workload before any deployment claim. Preserve native/SDL
correctness, Arabic/colour and font geometry; no Hibr edits or SSH.

Update to earlier historical notes: upstream Hibr337 is closed, released in
0.99.283/23fad83 with console/term/hold/desktop support. Actual installed
version/new hold and through-hold acceleration still need Blit120 acceptance.

## 2026-10-21:0.1.589 released and deployed by operator request

Supervised API update to192.168.10.54 succeeded. GET/v1/version reports589;
installed server SHA256 matches releases/0.1.589.yaml exactly. Complete saved
settings document is identical before/after, clean revision341: Retro22px,
Arabic Retro/baseline17px, direct KMS and DP-1 split1x2. One bottom-pane
login-manager owner is running; joins wait for authentication. Initial
diagnostics have no errors/warnings. No Hibr code edits or SSH were used.

This releases the validated DECCRA correctness/discovery changes, not proven
solid-drag acceleration through hold. Internal Hibr337 remains open for
emulator/hold/console/desktop integration;120 remains open for end-to-end
acceptance. Do not enable copy through hold solely from native DA28.
The older588 tag/package remains available and unchanged. Earlier candidate
checkpoints below retain their historical scope.

## 2026-10-21:588 tagged;120 complete-cell rectangle work in candidate589

Annotated v0.1.588 points to48c5fdf (documentation-inclusive checkpoint;
packaged source70999f2 remains in its release manifest). Tag is pushed and
verified on GitHub. Tagged package/source checks passed and live API reports
588; no user file or existing release/tag was overwritten.

Issue120 audit found existing DECCRA lost combining slots/count, underline
colour and hyperlinks, with unsafe wide-pair cleanup. Candidate589 corrects
the canonical copy path and reports standard DA1 feature28. ADR0175 records
one bounded-row snapshot with overlap-safe row order, wide-boundary damage,
page/clipping/default semantics and retained-graphics limits. Direct fixture
passes1048 cases including512x160; the old code fails the mark-count check.
Final standard suite149pass/5skip/0fail, GLES11/11, Valgrind0errors/0lost;
implementation is committed/pushed at01d6245. Package validation is next.
No Hibr code edits. Upstream Hibr337 requests console/term/hold/desktop
integration with fallback and real end-to-end measurement.120 stays open.

Candidate589 is packaged and reproducibly verified:
build/packages/blit_0.1.589-1_amd64.deb, release identity in releases/0.1.589.yaml,
source53b0a45. It is not deployed; live remains tagged588 with no
font/layout/backend changes. Read-only Hibr7dc5f74 audit confirms term's
intermediate handler ignores `$v` while relayed DA queries pass through.
Hibr337 now contains that concrete false-capability hazard. The next
end-to-end step requires upstream emulator/hold/console/desktop support;
do not enable solid-copy mode through hold from native DA28 alone.

## 2026-10-21: public documentation uses GitHub, not internal Gitea

Operator clarified the publication boundary: github.com/osakka/blit is the
public repository; Gitea remains internal origin/backlog. The public font
guide and hibr-apps pane guide return200. GitHub main already matched the
internal reference commit9781791 before this correction. The earlier
internal-browser404 investigation was not a public publishing blocker.

Public entry points now use GitHub/relative file links. Internal ticket IDs
remain explicitly internal, not guessed GitHub issue URLs. Workflow and
font-reference regression checks enforce this distinction. No remote,
runtime, font asset, host or Hibr change is involved.

## 2026-10-21:121 font developer reference

docs/font-reference.md is the Hibr application author entry point, committed
to Gitea main. docs/fonts contains generated category tables for all10,764
Nerd names/10,386 distinct scalars, seven original colour icons, all5244
Emoji18.0 corpus entries and exact six-face cmap coverage. The guide explains
API discovery, semantic IDs, JSON/UTF-8/one-cell rules, colour vs themed glyphs,
Arabic visual ordering and safe fallback. Browser glyphs are not Blit raster
evidence; the existing production-raster previews are linked explicitly.

Canonical catalogues/assets remain the source of truth. Generator --check,
font_reference_gate, asset identity and schema checks pass. This cycle changes
documentation/tooling only: no new package, host/backend/config/font changes,
Hibr edits or physical conformance claims. Live version remains588.
Internal Gitea's contents API confirmed the files. Its browser/raw404 results
were initially misclassified as a public publishing concern; the public
address correction above supersedes that conclusion. No separate standalone
site was created; the public reference is GitHub repository documentation.

## 2026-10-21:0.1.588 deployed; physical Arabic review next

The supervised API updater installed588 on192.168.10.54. GET/v1/version
confirms588; read-only API reporting confirms the installed server and both
Arabic face SHA256s match the release/provenance manifests exactly. No SSH
or Hibr code changes were used.

The operator changed the font before deployment: latest Retro22px, clean
revision335. The complete saved document is identical after deployment:
direct KMS, DP-1 split1x2, bottom-primary and auxiliary policy preserved.
Active inventory reports Blit Arabic Retro, baseline17px, visual-cell-marks.
One login-manager session owns pane-display-93-2; joins wait for login.
Capture build/tests/blit-0.1.588-login.png shows its idle animation, not an
Arabic desktop acceptance specimen. Initial diagnostics have no errors/warnings.

Standard suite147pass/5skip/0fail (120s/four-session soak included), separate
offscreen GLES11/11, reproducible artifact gate pass. Both font profiles
include companions and original colour-icon mappings; Hibr330 must adopt
those mappings.119/118 remain open for physical typography review and
remaining coverage. No universal Arabic/Quranic/all-emoji claim.

Next: operator login and Arabic/diacritics/digits visual review in each
profile. Shared-raster preview: images/blit-arabic-candidates.png.
Earlier checkpoints below describe historical rollout stages.

## 2026-10-21:119 candidate588 activation and rollout checkpoint

Canonical Modern/Retro profiles now require their Arabic companion and load
it atomically with the primary/emoji faces. GET/v1/fonts reports the actually
loaded Arabic family/path/baseline; bitmap profiles report null. Missing
companions reject replacement without losing the active font. Both faces
share the Latin baseline. Uncovered Arabic scalars use flagged shared PSF
fallback; unsupported clusters do not mutate cache/caller state.

Independent whole-word OpenType glyph positions equal concatenated visual
cells for all six captured Hibr specimens on both companions. The local real
PTY gate covers contiguous/bytewise Arabic marks/digits plus original icons,
with exact direct-API screenshot parity (24cases). SDL italic shear remains
anchored to the original cell when the ink canvas extends outside it.
See verification-status for final regression/package/live receipts.

At this checkpoint the live host is587, saved Retro26px, two sessions on
display93. No host settings, backend, layout or Hibr code have been changed.
Deploy588 only after package checks; preserve the current operator settings.
Physical Arabic review and Hibr330's colour-icon selection remain outstanding.
Earlier dated entries below describe historical checkpoints, not current
activation behaviour.

## 2026-10-21:119 backend ink composition/damage implemented, not deployed

KMS and SDL now consume raster ink offsets without squeezing the result back
into a terminal cell. `blit_ink.h` owns bounded write/source halos: clear and
restore layers inside the write clip, sample neighbouring source cells, and
clip their ink to that rectangle and the pane. KMS counts restored cells after
expansion. Existing zero-margin profiles retain the existing damage policy.

SDL atlas tiles fit the maximum canvas and use actual texture dimensions.
Power-of-two extents are required for reliable software UV sampling; a
416×208 negative-control atlas drops a real8px mark pixel. Ink-enabled atlas
allocation is bounded at32MiB/output for64px. Block cursors preserve nearby
ink by repainting intersecting glyph sources inside the cursor clip; normal
SDL/cursor glyph dispatch is shared, not duplicated.

New direct-KMS mock, SDL software and offscreen GLES fixtures compare moving,
erased and marked glyphs against clean frames and a full-raster pixel oracle,
including pane guards, cursor overlap, a negative-z image and deliberate
Latin/Arabic/colour atlas collisions. Nine representative sizes cover8..64px;
the earlier shared Arabic module still covers every integer height. Negative
controls reproduce missed damage and non-binary atlas UV failures. Final
results and limitations are recorded in verification-status.

All standard gates:146pass/5skip/0fail, including120s terminal soak; offscreen
GLES11pass/0skip/0fail. Final backend fixtures117 scenarios per face per path,
702 total; native backend Valgrind0errors/0lost bytes (18,804bytes/9blocks
reachable). TCC588 rebuild passes. Skipped VM/package/live checks are not
acceptance. See verification-status for scope, logs and exact comparisons.

Next: independent word/mark semantics, mixed-script baselines and styles/coverage review, then atomic
profile attachment plus truthful API identity/availability, package and real
host review. Server profiles still do not attach Arabic companions. Live
version remains587; no deployment, settings changes, SSH or Hibr edits.
Do not confuse these offscreen/mock fixtures with physical acceptance or
claim that all Arabic/Unicode/Quranic forms are covered.119/118 remain open.

## 2026-10-21:119 shared Arabic module implemented; server remains unchanged

ADR0174 adds explicit, pre-first-use Arabic companion attachment to the shared
rasterizer. It preserves Hibr's visual/presentation forms, uses HarfBuzz for
cell-local vowel-mark positioning and FreeType grayscale coverage, with one
common face transform/advance. Measured ink offsets preserve cursive overhang
and marks instead of clipping or shrinking individual letters. This exposed
a real missing backend contract: KMS/SDL currently assume cell-bounded ink.
Do NOT attach companions in server profiles until both composition and damage
handle those offsets; the normal rendering paths remain unchanged.

Experimental Retro=Kawkab Mono0.501; Modern=GNU FreeMono20120503. Cascadia and
Vazir Code fail the one-glyph lam-alef alias requirement. DejaVu Sans Mono
lacks U+0670 superscript alef. Source/derivative hashes, notices and preferred
FreeMono source are included, not host-dependent font substitutions.
Canonical specimens capture actual read-only Hibr0.99.277/ABI16 output.
No Hibr source or installation changes. Both candidates pass16,986 finite
scalar/mark/specimen cases each across8..64px; preview generated from the real
raster is build/font-preview/blit-arabic.png, not a scanout screenshot.
Selected regressions:67pass/0skip/0fail; offscreen GLES10pass/0skip/0fail;
TCC rebuild passes. Final Arabic Valgrind:0errors/0lost bytes, with reachable
library allocations recorded in verification-status. These do not enable or
validate Arabic in the real backend; module/adapter acceptance stay separate.

Next: independently validate positioning/joins, implement shared ink-overhang
composition and damage parity, then enable atomic profile selection/inventory,
package and real-host review. No deployment: live587/settings untouched,
candidate588 remains unreleased.119/118 stay open; Arabic/Quranic completeness
and "exquisite" physical appearance have NOT been established by these tests.

## 2026-10-21: Arabic is now the priority; no deployment yet

Operator requires exquisite Arabic in both Retro/Modern, with Arabic-Indic
"Hindi" digits U+0660..U+0669 (٠١٢٣٤٥٦٧٨٩), not Devanagari. Blit119 tracks
this core requirement under118. Both current Latin/Nerd outline faces contain
0/42 audited Arabic-range scalars and0/10 required digits; PSF fallback is not acceptable
Arabic typography. Hibr uni already emits shaped display-order forms and
marks; inspect/read only, never edit Hibr or double-apply bidi. Kawkab Mono
and Cascadia Mono Arabic are candidates being audited, not selected fixes.

Icon/coverage candidate588 remains LOCAL; live587 is untouched. The stricter
UI ink gate exposed lost thin strokes/checkboxes at8px. ADR0173 preserves
shrinking native fallback coverage without changing exact half-blocks or
bitmap-only profiles. Final selected schema/terminal/font/lifecycle run:
66pass/0skip/0fail (build/tests/blit118-final-regression.log). Independent
module half-block/known stroke checks pass. Initial package was rejected by
an overbroad filename guard, fixed in38e2aaa; no package deployment followed.
Hold deployment until Arabic rendering is implemented/validated as requested.

## 2026-10-21: candidate588 original icons /118

ADR0172 adds seven original colour glyphs to Modern/Retro: Blit, Hibr pen,
Wi-Fi levels1..4 and Bluetooth, with canonical SVG sources/catalogue,
COLRv0/CPAL and authenticated `GET /v1/fonts/icons`. Standard text U+16D2
was genuinely uncovered in both old faces/PSF; Retro U+21CC was uncovered.
Both now resolve without question-mark substitution. U+270E is an original
themed pen; colour originals require explicit private-use selection.
Hibr330 tracks that selection; never edit Hibr or overwrite Nerd meanings.

Custom raster tests cover798 icon/size/profile cases and23,028 Hibr GL
scalar/size/span/profile cases; real local PTY/API parity covers12 scenarios.
Native/SDL software/GLES composition passes both profiles/all57 sizes;
both fonts rebuild byte-identically. Valgrind reports0 errors/0 lost bytes,
18,804bytes/9blocks reachable per face. Full Nerd catalogue and Emoji18
cluster regressions pass at their documented scopes. Broader selected suite
is running; packaging/deployment and physical aesthetic acceptance pending.
Preview: docs/images/blit-fonts.png. No live settings or Hibr changes yet.

## 2026-10-21:118 font coverage investigation

Live587 remains Modern22/native KMS; no deployment or settings change. Current
primary cell snapshot has no literal question marks; the operator's missing
icons still need captured affected codepoints. A separate reproduced defect
routes two-cell U+2615 through monochrome Unifont despite colour coverage in
the selected native font. ADR0171 corrects the shared native/SDL selection;
expanded gates now load the actual wide font rather than bypassing selection.
Custom branded artwork and live acceptance remain pending;118 is not complete.

## 2026-10-21:587 deployed; dedicated native VT verified on10.54

Authenticated updater reports587; exact installed binary SHA256
79e706f10673c921a0c5105599c0298f7e092f30da354748b95d7de74fb965e1 matches
the manifest. Native KMS ready,2outputs/3sessions,5keyboards/4pointers.
Settings document equals preinstall: Modern22 and DP lower-primary/upper plus
HDMI auxiliary policy preserved. No Hibr edits, backend/font/layout writes,
manual desktop/join creation or SSH.

Kernel sysfs active tty3. Console API: owner native,VT3,previousVT2,active,
keyboard_mode4/K_OFF,keyboard_read_errno0,recovery registered,timeout10s.
Duplicating the actual owner's fd3 with pidfd_getfd independently returns
keyboard_mode4 (unlike586's EIO). getty@tty3 inactive; original getty@tty2
active. Evidence /tmp/blit-rollout587.SKzt0W settings/diagnostics/update.log.
Physical Ctrl+Alt+F2 leaves Blit, Ctrl+Alt+F3 returns; Ctrl+Alt+Backspace stops
it using the existing shutdown chord. VM validates these paths; operator
physical handover/typing confirmation remains pending. Do not remotely force
VT changes or kill processes merely to turn that boundary into a pass.

117 remains open for operator confirmation, SDL daemon ownership and any
required pointer/gpm exclusivity. Native keyboard muting does not grab evdev
devices. Do not claim586 was accepted: its failed real-host check led to587.

## 2026-10-21:586 physical acceptance FAILED;587 dedicated VT correction

Source89e54e8 pushed. Package587 SHA256
3c294a089495b63e204456a76e8969201c3e8596409ffdc1fdc90cae9d08df9d
passes artifact/provenance and byte-identical rebuild. Exact packaged payload
passes expanded module/full-server VM again, including getty hangup/reset and
owned-descriptor recovery; /tmp/blit-vt-kernel.HuOiVe/serial.log. Focused
schema/terminal/lifecycle33pass/0skip/0fail. Physical rollout pending here.

586 updated successfully viaAPI and preserved Modern22/settings/layout, but
console diagnostics reported keyboard_mode=-1. Duplicating the actual owner's
fd3 via pidfd_getfd confirms EIO; a fresh tty2 descriptor reads K_UNICODE3.
getty@tty2 restarted at00:48:07 and00:49:12. Its login VT cannot be borrowed
as a durable graphics owner. This was absent from the getty-free VM, so586
is NOT an accepted keyboard isolation release. Hibr is not being changed.

Candidate587 reserves an unused VT, keeps it busy before activation, remembers
the previous VT, uses a global owner lock, and restores the previous VT on
exit only if Blit's is still active. Periodic actual mode/descriptor validation
stops on lost isolation; diagnostics add read errno and previous VT. Tests
now simulate getty's TIOCVHANGUP on the original console during full-server
presentation. Its own descriptor hangup also triggers safe recovery using a
fresh open of only the reserved VT. Real module/full-server VM passes with
getty-style hangup/reset, native scanout equality, physical VT chords, queue
isolation, normal/crash/watchdog recovery and the install lease. Evidence
/tmp/blit-vt-kernel.kAxkrF/serial.log. Package/rollout pending; live586 reachable.
The exact586 package is red under the expanded fixture: original-console
hangup/reset produces the same active=true/keyboard_mode=-1/unmuted diagnostic
as10.54. Evidence /tmp/blit-vt-kernel.ptvwkj/serial.log. This is a reproduced
mechanism, not an inferred Hibr fault.

## 2026-10-21:117 native keyboard isolation, candidate586

Source44e3597 committed/pushed. Package586 SHA256
8cbdce7f5442d832f8dc624b70b14a5ed235a9d6c2802c11af07d7af0cca66d8
passes artifact/provenance checks and byte-identical rebuild. Exact extracted
server payload also passes the full native VM gate; evidence
/tmp/blit-vt-kernel.yWCkCY/serial.log. Guard added to reject symlink inputs:
the archive's absolute /usr/bin/blit link otherwise resolves to the developer
host's installed binary. That mistaken invocation was discarded, not a pass.
No live deployment at this checkpoint; physical acceptance remains pending.

Native KMS now owns its active text/automatic Linux VT, uses K_OFF to stop
kernel console key delivery, and handles VT release/acquisition with DRM
master handover and full repaint. Physical Ctrl+Alt+F1..F12 switches VTs.
While away, physical events are discarded, including old queued events on
return (monotonic evdev timestamps); API injection remains independent.
A pre-thread recovery child uses pidfd death notification and a10s main-loop
watchdog. Explicit recovery-channel shutdown and flock unlock prevent inherited
pre-exec descriptors from blocking cleanup/restart. SDL cannot silently fall
back to native KMS without acquiring VT ownership before I/O threads start.
See ADR0170 and GET /v1/diagnostics/console; this is NOT evdev pointer exclusivity.

Actual Linux6.1.190/QEMU, TCC-built module and complete native server pass:
baseline console leak observed; two keyboards including hotplug muted while
evdev delivery continues; release/acquire; normal/SIGKILL/stopped-owner
watchdog recovery; inherited descriptor handling; competing owner refused.
Full server on Bochs DRM1280x800: physical key reaches PTY, not console;
physical Ctrl+Alt+Fn handover works; away key reaches only the other console;
queued input while SIGSTOPped is not replayed after SIGCONT/acquisition;
scanout before/after is byte-identical; API shutdown restores keyboard.
Final evidence /tmp/blit-vt-kernel.FHvism/serial.log includes the bounded120s
install lease held beyond the normal10s timeout, followed by a separately
expired ordinary watchdog. VM powers off after each run. Broad suite140pass,
5explicit skips,0fail; final update-restart gate passes after the lease change.
Targeted schema/terminal/lifecycle32pass,1skip (pinned headers not supplied),0fail.

Read-only10.54 API inspection: live584, VTtty2, K_UNICODE3, KD_TEXT0,
VT_AUTO0. No TTYPath, null stdin. Old mirrored Blit desktop/wm files absent;
installed /usr/share/hibr/desktop/session.hibr belongs to Hibr. No Hibr edits,
SSH, live restart, font/layout mutation or second desktop. Physical rollout,
SDL daemon isolation and pointer/gpm exclusivity remain unverified;117 stays
open. Fresh preflight settings select Modern22, native KMS and the existing
DP lower primary/upper and HDMI auxiliary policy; preserve these, not older
font checkpoints. Package/physical rollout remains pending at this entry.

## 2026-10-20:116 active, candidate585 negotiated keyboard support

Final source1206741 canonical build passes combined terminal/schema9/0/0;
32-case raw-PTY gate, existing keyboard settings and Super stream checks pass.
Package585 SHA25612d6746d57ae9bbc4b7f7e976575f19af3bcfe16d85d4a8912788a099954ed6d
passes artifact/provenance gate and byte-identical rebuild. Manifest recorded;
not deployed. Live version584 verified through API after candidate build.
Hibr326 received exact integration details;116 remains open for physical and
native hold/desktop acceptance. Do not force Hibr flags or claim Start works.

584 live desktop left unchanged.116 requests lone Super/chords. Candidate585
implements Kitty query/set/push/pop, independent bounded main/alternate stacks,
flags1/2/8 only (supported mask11;4/16 explicitly unimplemented), real
press/repeat/release encoding via existing evdev/API dispatch, per-keyboard
Super/remaps, capability readback and input-stream super. ADR0169 supersedes
ADR0059's zero-enhancement policy. Raw-PTY exact-byte regression is red on584
and green for the tested585 scenarios; normal terminal suite3/0/0 and existing
keyboard settings/remap tests pass. Expanded32-case fixture/remap and WebSocket
Super-edge checks pass, including a legacy printable-remap regression guarding
against routing disabled/remapped letters through the old text encoder.
Terminal suite4/0/0 includes the new gate. Final package verification is above.
No Hibr changes or live deployment.
Hibr326 must negotiate/decode/relay through hold; do not claim desktop Start
menu works from terminal tests. No browser frontend exists here, and OS-reserved
Meta cannot be guaranteed by the server; existing API injection permits an
explicit Send Super client control.116 stays open for integration acceptance.

## 2026-10-20:584 deployed,321 verified;320 upstream fix pending

Package584 from9c18b25 passes checksum/provenance, byte-identical rebuild and
actual packaged provider tests. Authenticated update on10.54 succeeds. Ready
native DRM/KMS; saved settings document unchanged. The user-selected font
at this preflight/readback is Retro22, not the previous Modern16 checkpoint;
no font/configuration change was made by this rollout. Login reports
authentication_required after restart; do not manually create another owner.

Live isolated PTYs with inherited installed Blit RC, explicit terminal mode
and missing surface/session IDs: most /etc/os-release renders expected content
and exits0 (2430 output bytes); hvi renders it and exits0 via :q! (2878 bytes).
Both have empty stderr and no missing-surface error. Interactive provider
readback lists console, not console-blit.321 resolved for reproduced cause;
already-open shells may retain the old loaded adapter and should be reopened.
No Hibr edits, user-file edits, SSH, backend switch or visual desktop claim.
Evidence /tmp/blit-rollout584.GPERT2 before/after observations/update.log.

320 root cause reproduced without Blit and posted upstream; no core fix here.
Console signal cleanup in forked watchdogs needs Hibr-owner implementation;
ticket stays open. Local test transient unit cleaned all its children. See
docs/diagnostics-login-and-provider.md and Hibr320 for actionable evidence.

## 2026-10-20: Hibr320 diagnosed, Blit-owned321 fix candidate584

320 reproduced without Blit in an isolated root PTY using installed Hibr270:
stock lg_as cancels watchdog with SIGTERM; inherited cn_onfatal issues TCSETS
from the background PGID, causing kernel SIGTTOU/group stop. strace confirms
the sequence; live stopped leader syscall corroborates it. Hibr320 has full
repro/actionable ownership guidance. No Hibr changes/live signals; upstream
fix pending. See docs/diagnostics-login-and-provider.md.

321 is our RC injection, not a current module-directory collision. Live583
only has adapter under /usr/lib/blit/hibr, native console sole discovered
display provider. But launcher exported adapter RC into native interactive
children. Explicit reproduction emits missing BLIT_SURFACE_ID. Candidate584
limits automatic RC injection to explicit API mode; guards RC adapter load
likewise, preserving caller RC and protecting old inherited RC paths. Actual
native/stale-RC/API/user-RC test passes; packaged583 RC is the negative test.
API-only diagnostic report probe adds lossless chunking/cleanup rather than
silently truncating large evidence. Live583 unchanged during investigation;
candidate584 package/deployment pending. ADR0168 records the boundary.

## 2026-10-20:583 deployed;115 socket visibility verified, stopped children persist

Authenticated API rollout succeeds; version583, health ready native DRM/KMS.
Saved settings document equals preflight; Modern16 retained. One native
primary on DP lower, DP upper and HDMI joins verified. No backend/layout/font
write or manual desktop/join spawn. Live service readback: User=root,
PrivateTmp=no,RestartUSec10s,StartLimitIntervalUSec1min,StartLimitBurst3.
Separate transient systemd User=osakka process (outside Blit's unit) runs
`hold clients desktop` successfully and lists exactly these three clients.
Diagnostics use short-lived unbound1x1 API terminals, removed afterward;
no SSH or Hibr edits. Evidence /tmp/blit-rollout583.1Il4gP before/after/final
observations and update.log. Package manifest records exact identity.

Stopped root descendants still occur after shared-tmp rollout: live login
session leader140029, root children140058/140060 stateT, PGID140058,
foreground PGID140029. Added to Hibr320;115 stays open, no guessed signal fix.
Desktop owner/login IDs changed between after/final observations; no manual
restart was requested by these probes and cause is not assigned. Final
observation remains native-ready with one owner and three clients. Browser
visual/font-size acceptance111 and operator review still pending. The browser
policy request is Hibr319; pixel placement alone is not a browser zoom fix.

## 2026-10-20: new backlog115 service-boundary fix, candidate583

Artifact583 is built from e605bd8; package gate passes with a byte-identical
rebuild. SHA2564baf7253764f55cd94b707dea422d2acd321edab3bf7c223d2361e41d74ee96a.
Schema suite5/0/0, terminal suite3/0/0, credential-drop identity,
shutdown/restart, socket/retry/log and GLES2 placement gates pass. Live
preflight582/update idle captured in /tmp/blit-rollout583.1Il4gP. Rollout and
operator acceptance pending; no full browser sizing claim from these gates.

Latest backlog contains115 (new),114 (known odd-height wallpaper) and closed113.
115 claims root is an override; current unit/ADR0065 deliberately run root.
Confirmed local systemd namespace fault: private tmp hides a same-UID user
socket. Candidate583 disables private tmp, retains shared update staging and
adds10s restart delay/3-start60s limit. Real transient-unit tests pass socket
positive/negative controls and actual failed state after3 starts. The initial
test wrongly expected Result=start-limit-hit; systemd252 retains exit-code,
so the corrected assertion checks ActiveState/SubState/NRestarts instead.
Default Hibr log paths now use unique session-user-owned0700 directories
after credential drop; explicit DT_LOG preserved. Local API test passes.
See ADR0167. Hibr320 requests support for undiagnosed stopped root children;
no Hibr edits or guessed SIGCONT/kill fix.115 remains open for that subproblem
and physical-host verification.

Read-only live API582 confirms one UID1000 primary with DP upper/HDMI joins;
operator has selected Modern16 (8x16), superseding the earlier Retro16
checkpoint. Preserve this current selection/configuration at rollout. No live
restart, renderer switch or configuration write for this triage yet.

## 2026-10-20: candidate583 image placement; Hibr319; pane0.1.8 published

User requests browser/font sizing work now. Hibr319 tracks the browser's
fixed8x16 CSS-pixels/cell viewport policy and pixel-native sizing/hit-testing
request. No Hibr source or installed files edited. Blit111's adapter defect
is fixed in candidate583: preserve destination dimensions separately from
source dimensions in pixel_frame_begin. See ADR0166 and API contract.
Production adapter metadata, black-box software/GLES2 presented quadrants,
odd/even sizes, source-buffer reuse, shrink erasure, malformed destination
rejection and legacy no-placement behavior pass. Old582 fails the new
destination regression. Desktop OpenGL did not initialize on this offscreen
driver; do not report it as verified. Native clipping unit gate passes,
not physical browser acceptance. No package/deployment/live font changes yet;
live582/Retro16 remains the last recorded host state. Browser policy remains
separate from the corrected Blit contract;111 and319 remain open.

Blit Control Panel0.1.8 published in hibr-apps6b458b3, signed catalogue;
hibr-apps#2 tracks missing native dropdown hooks. Both actual UI gates,
callback gates and isolated catalogue lifecycle pass. Live pane installation
and operator acceptance pending. Hibr core unchanged.

## 2026-10-20 live font review — both16 accepted; odd-height wallpaper114

Operator confirms the582 desktop, mouse and keyboard work correctly across
the current setup. Baseline Spleen16 rollout acceptance is recorded. Modern
was then selected through PUT /v1/fonts at16px: native antialiasing/colour
support active, same8x16 cells, native KMS health and all pane geometries
unchanged. Operator reports that all look perfect in that Modern16 review.
This is representative desktop appearance/input acceptance, not every glyph
or every terminal/graphics protocol combination.

Operator also reports Retro16 is perfect. At17px, text/grid looked correct
but wallpaper disappeared on all panes;17px is NOT fully accepted. Controlled
six-second observations show wallpaper image counts1/1/1 at16/18/20 and0/0/0
at17/19, with zero payload rejections.18px shares9px cell width with17px and
works, isolating a tested height-parity correlation rather than odd width.
Trace shows explicit Kitty image deletions and subsequent capability probes
at failing sizes, not an existing retained image merely drawn black.

Current live font restored to Retro16; image counts return1/1/1 and operator
confirms wallpaper is back. Track first failing boundary in Blit114; fault
ownership remains unassigned (Hibr behavior may depend on Blit replies).
Next: isolate PTY size/protocol negotiation and minimal pixel producer before
claiming a Hibr fault. Hibr source was read only at matching tag v0.99.270.
Evidence: /tmp/blit-fontcraft.IHU9AK/wallpaper-*.json, wallpaper-parity.log and
wallpaper-17-display93.bmp. No restart or renderer/topology switch occurred.
Font API changes use the canonical persisted settings, not
another screen-scale control. Baseline and profile responses/observations:
/tmp/blit-fontcraft.IHU9AK/font-live-*.json. No Hibr edits, SSH, renderer switch,
manual joins or service restart for these font comparisons.

## 2026-10-20 rollout582 — installation record

Latest follow-up: authenticated native desktop is now ready. Observation shows
exactly one primary on DP lower pane and two joins: DP upper pane and newly
present HDMI-A-1. One native owner, three clients, no manual joins created by
this rollout. This supersedes the waiting-for-login/one-output checkpoint
below. Subsequent operator/font reviews are recorded above.
Evidence: rollout-authenticated.json in the same scratch directory.

Authenticated API update on192.168.10.54 completed; `/v1/version` reports582,
update status is ready/idle and native DRM/KMS health is ready. Active Spleen
16px, saved settings document and both physical-pane/cell-grid geometries
match the immediately-before snapshot. Retro/Modern are available, not
selected. One1920x2160 output remains split1x2 with lower login preference.
At the first post-install check, login below required authentication; no join was
started before an authenticated primary. User was asked to sign in and
confirm desktop/input before changing font or size.

Fresh startup probes Hibr0.99.270/ABI16, compatible with the pinned ABI16
adapter. Before restart572 had cached0.99.265; that is not evidence of when
Hibr changed. This Blit package did not install or edit Hibr. No SSH used.
Package/source identities are in releases/0.1.582.yaml; baseline/after/API
update logs are under /tmp/blit-fontcraft.IHU9AK/rollout-*.
No physical pixel, new-font live appearance, multimonitor or high-refresh
acceptance is claimed. Retained572 package is rollback material, not an
independently tested automatic startup rollback. Operator-only update polling
is now connection/request bounded; that tool is not installed in the package.

## 2026-10-20 rollout582 preparation — historical pre-install checkpoint

Host192.168.10.54 reports572, healthy native DRM/KMS, Spleen16px, one1920x2160
output split1x2 with primary below/join above. Runtime Hibr0.99.265/ABI16
matches the adapter ABI16 (build provenance remains0.99.225). A short-lived,
read-only external diagnostic PTY confirms required FreeType/HarfBuzz minimum
versions; it was deleted, with no additional Hibr desktop spawned.

Built582 from0d6cc98; package artifact/reproducibility gate passes. Generated
identity: releases/0.1.582.yaml. Package SHA
01e4346c5f8b73344f68937ad89c4223b1f2aec22a481c6e83afa466f4eaef7f.
Retained572 package matches its recorded SHA. Snapshot and logs are in
/tmp/blit-fontcraft.IHU9AK/rollout-*.json and rollout-*.log (not Git).
No installation yet at this checkpoint. Deploy through the authenticated API;
keep saved renderer/topology/font/login intent unchanged. The API updater now
has bounded connection/request waits. API rollback requires a responding
management plane; this is not independent automatic package rollback.

## 2026-10-20 candidate582 — whole-cell resize and transition oracle

ADR0165 fixes a reproduced shared resize defect: shrinking through a wide
emoji retained its anchor past the right edge. Resize now clips that owner
whole in active/saved alternate buffers, clears clipped link metadata, and
ends in-flight emoji-extension eligibility without resetting terminal/REP.
One-column VS15 owners wholly inside the grid survive shrink/grow.

New real local PTY transition tests use independent expected grids, OSC-title
completion barriers and revision-matched presented BMP comparisons against
positioned API references. Software/GLES cover both fonts at15/16px, nine
representative sequences and move/overwrite/erase/scroll/alternate/clear/
shrink/grow. This is not an independent raster oracle, all5244 entries end
to end, Kitty/sixel image-placement conformance or physical scanout proof.
See verification-status.md for exact evidence.107/110 stay open for remaining
terminal/graphics/full-corpus/live acceptance before browser111/SDL104.
No Hibr edits, SSH, package, live writes or deployment. Preserve the user's
untracked releases/0.1.554.yaml.

## 2026-10-20 candidate581 — core Powerline cell geometry

ADR0164 corrects a demonstrated blank-row/base-seam failure in core Powerline
U+E0B0..U+E0B3. One shared-raster coverage implementation fits the allocated
cell, with exact reflected/symmetric geometry and antialiased slopes. Solid
base columns are fully opaque; font files, global metrics, other Nerd icons
and legacy bitmap-only profiles are unchanged. Regression fixtures cover all
57 sizes and both spans/profiles, plus backend bold/right-edge guards.
Selected46 gates have passing evidence:45 passes before an interrupted long
run, plus a fresh complete9/0/0 GLES run. The ledger records exact scope and
Valgrind evidence; no claim of one clean full-suite run. This is not all Powerline
Extras/italic or full font acceptance.107/110 remain open; next is remaining
terminal/graphics integration and reviewed appearance before browser111/SDL104.
No live writes, SSH, Hibr edits or package/deployment. Preserve
the user's untracked releases/0.1.554.yaml.

## 2026-10-20 candidate580 — Unicode emoji presentation

ADR0163 adds generated Unicode18.0 default-presentation/variation data and
the pinned Noto Emoji3.002 monochrome fallback through existing FreeType.
Standard VS15 requests text coverage; VS16/default-emoji requests colour.
Caller-owned columns and scalar-layout compatibility remain unchanged.
Actual ink overhang is fitted by reducing em, never clipping the glyph.
The production specimen includes side-by-side text/colour selectors.
Full presentation matrix passes371 selector bases and1228 default-colour
scalars at all57 sizes, both spans/profiles. Selected45 gates have passing
final evidence across the suite and corrected compositor-reference reruns;
do not describe this as an uninterrupted clean suite. Real PTY/capture tests
include VS15; font changes roll back if either shared emoji asset is missing.

This is offline work; no host writes, SSH, Hibr changes or package deployment.
See the verification ledger for completed tests and remaining acceptance.
107/110 remain open for geometry and full end-to-end/live review before
browser111, then SDL104. Preserve the user's releases/0.1.554.yaml.

## 2026-10-20 candidate579 — explicit streaming PTY emoji policy

ADR0162 adds creation-time text_layout=emoji to terminal sessions, using the
same trie/grid as positioned updates. Default scalar compatibility remains;
GET reports the policy and PATCH rejects changes with immutable_text_layout.
No DEC2027 claim: this is pinned emoji grouping, not full grapheme handling.
Late members survive byte reads; widening, whole-value wrap, REP and buffer
copies preserve owned scalars. Controls/API writes end extension eligibility.
One-column geometry now bounds wide scalar writes too; RIS clears the repeat
glyph while retaining the configured layout policy. Width data is unchanged.

All5244 entries pass both PTY policies, contiguous and byte-fragmented, plus
focused wrap/scroll/margins/ASCII-insert/REP/erase/alternate/control tests.
Real local PTYs pass42 representative cases and14 fresh-capture comparisons
against positioned API rendering. Terminal/schema8/0/0 plus three emoji gates
pass; Valgrind0 errors/lost bytes, reachable allocations explicitly recorded.
Fresh TCC rebuild byte-identical. Next: VS15/default presentation and remaining
font/geometry/end-to-end acceptance.107/110 remain open. No live writes,
SSH, Hibr changes or package. Preserve releases/0.1.554.yaml.

## 2026-10-20 candidate578 — explicit API emoji grouping

ADR0161 adds schema-first CellUpdate.text_layout: scalar (unchanged default)
or emoji. One decoder uses a generated6157-node trie from the pinned5244
Emoji18.0 corpus entries. Complete multi-scalar matches own two columns;
singleton/ordinary widths and positioned Hibr providers remain unchanged.
No joining across cells/requests; no PTY changes. Invalid layout values/types
return400 invalid_text_layout before mutation. All5244 sequences pass both
decoder policies/exact scalar preservation and adjacent-text checks. Real API
tests cover seven families, continuations/overwrite, whole-edge clipping,
default compatibility and atomic rejection. Terminal/schema suite8/0/0;
two new focused gates also pass. Trie/server rebuild byte-identically.
Next: streaming PTY grouping with explicit width negotiation, VS15/default
presentation, full end-to-end coverage.107/110 remain open. No live writes,
SSH, Hibr edits or package. Preserve releases/0.1.554.yaml.

## 2026-10-20 candidate577 — shared clusters reach all compositors

ADR0160 wires the existing raster's cluster callback through BlitFont into
native KMS and SDL software/OpenGL. Normal cells and block-cursor code use
the complete owned value. Unsupported combining text retains scalar overlays.
SDL's atlas compares full keys, flushes before collision replacement, and
clears keys on font/size changes. No new cache/shaper or width ABI change.

Ten focused gates pass: Modern/Retro on each of three backends, font API,
cell paint/cursor, motion trail and KMS composition. Per backend/profile:
1,596 representative cluster cases across57 sizes/both spans; native KMS
tests actual draw_view/block cursor and exact guard pixels, SDL tests the
shared atlas helper with both background cases (2-channel-level tolerance).
This is not full-corpus backend or API/PTY sequence acceptance. Next: shared
ingress grouping and complete presentation/width policy.107/110 remain open.
No live changes, package, SSH or Hibr edits. Preserve releases/0.1.554.yaml.

## 2026-10-20 candidate576 — production colour-cluster raster, not deployed

ADR0159 adds HarfBuzz through its public C API to the existing shared
FreeType raster. Unmodified6.0.0 headers are pinned with per-file hashes;
the OS-maintained runtime>=6.0.0 is an explicit build/package dependency.
No second terminal engine, backend-specific shaper or Hibr changes.

The internal cluster entry point shapes bounded ordered values to one colour
glyph, then uses the same premultiplied downsampler/cache/compositor as scalar
emoji. It rejects explicit VS15/text and unsupported multi-glyph forms rather
than dropping visible characters. Existing scalar selection is unchanged.
All5,244 pinned corpus entries pass actual raster tests at16px with both
owner spans for each profile; a colour-only context also checks every entry.
Seven sequence families pass both spans at every8..64px size per profile.
The regenerated production specimen includes joined emoji. These are module
tests, not proof of terminal integration or physical-display acceptance.

Next: shared renderer consumption and PTY/API segmentation/width contract,
text-presentation/default-emoji policy, then overwrite/wrap/erase/resize and
backend end-to-end conformance.107/110 remain open. Do not advertise complete
emoji support or activate parser grouping before all renderers support it.
No SSH, live write, package/deploy or browser/Hibr change; host stays last
verified572. Preserve user untracked releases/0.1.554.yaml.
Selected regression run120pass/0skip/0fail. Server and two fresh rebuilds are
byte-identical; exact identity and test scope are in verification-status.md.
Final font rerun24/0/0; full-corpus raster Valgrind checks pass for both
profiles with0 errors/lost bytes. GLib's reachable initializer allocations
are explicitly recorded, not omitted from the memory evidence.

## 2026-10-20 candidate575 — shared cluster value and proven KMS attachment bug

Font107/emoji110 remain active. ADR0158 defines one existing16-slot attachment
bound and an owned base+attachments+column-span cache key. Native KMS previously
indexed16-slot server arrays as groups of4. New real draw_view pixel fixture
fails before the fix at8px/count1, passes after with0/1/5/16 attachments across
all57 sizes for both Modern/Retro. This is a demonstrated internal layout bug,
not evidence explaining every previously reported display symptom.

The shared raster cache now compares complete bounded values (length, span,
scalars) after hashing. Its public calls remain scalar-only. TCC contract
gate round-trips all5,244 Emoji18.0 entries at both spans, checks ownership,
invalid input/atomic rejection and hash collisions. Raster collision tests
verify eviction/re-render bytes and1/2-column separation. No new shaper/link
dependency, grouping, width ABI or Hibr change. Emoji sequences still do not
render as joined glyphs; do not advertise otherwise or close107/110.

Next boundary is the production shaping/colour-cluster API plus its build
decision, then shared PTY/API segmentation and complete backend consumption.
Do not change parser grouping before all renderers can consume the result.
No live writes, browser/desktop restart, SSH, package or deployment. Host
remains last verified572. Preserve user untracked releases/0.1.554.yaml.
Final frozen run118/0/0; server SHA
dac8e240a03dd3a96dba6d038abe7cec50ca0707ae036ece9787fd4622db6132.
Both outline raster memory checks have0 errors/leaks. Details in verification-status.md.

## 2026-10-20 agreed queue and emoji baseline — candidate574, not deployed

Canonical execution order is docs/roadmap.md: font107/emoji110 FIRST, browser
image-sizing111 next, SDL104 with recovery109/latency105 after that; then
performance101/95/21, correctness/API/hardware22/64/20 and release112/27.
Do not work on browser/SDL ahead of the active font acceptance. Browser111
records the read-only adapter contract discrepancy; do not infer the entire
reported browser symptom from it, alter Chrome zoom or modify Hibr.

ADR0157 pins official Unicode Emoji18.0 corpus/licence. Independent HarfBuzz
6.0.0 audit: old Noto2.042 misses/splits174 RGI and10 minimally-qualified
entries; unmodified pinned Noto2D2.057 shapes all5,244 entries to one nonzero
glyph. The candidate replaces the ONE shared colour asset and provenance,
not the renderer. Existing shared-raster tests for both faces pass all57 sizes.
Current BlitRaster remains scalar-only. Next: bounded shared cluster/width
contract, production shaping/build decision, real colour-cluster raster tests,
PTY/API ingress, cache, all compositors and end-to-end conformance. Font
shaping is NOT renderer acceptance. Full completion criteria are in the font
evolution plan; no claim that font107/110 is complete.

No live write/deploy, SSH, browser restart or Hibr changes. Last read-only
host font query was572/Modern16px/8x16/pixel-size mode, not the older22px
historical setting. One monitor is temporarily away. Preserve user untracked
releases/0.1.554.yaml. Candidate574 has no new Debian package yet.
Verification covers55 distinct gates (font21, schema/lifecycle25, GLES9),
zero failures/skips. See verification-status.md for exact run scope/identities.

## 2026-10-20 offline font candidate573 — live host unchanged

Operator requests display-independent font work while one monitor is away.
Add smooth rounded `blit-retro` (renamed Nerd Fonts3.4.0 3270 Mono), expand
Modern to the complete pinned10,386-codepoint Nerd catalogue. One generator,
one provenance manifest, same FreeType/cache/compositor and unchanged Noto
Colour Emoji2.042. Existing bitmap/default fonts remain unchanged. ADR0156;
tracking107. No Hibr edits, SSH, deployment or live configuration change.

`ruby tools/fonts/preview.rb` creates `build/font-preview/blit-fonts.png`
from actual production rasters, without a display. Every Nerd codepoint is
tested at each of57 sizes per face (1,184,004 cases total); both fonts rebuild
byte-identically. The intentionally empty `cod-blank` is checked as empty.
API tests cover both profiles at all sizes and saved Retro23px process restart.
Native-KMS/mock, SDL-software and offscreen-GLES raster checks pass for Retro.
These are offline tests, not physical-display approval or performance claims.
Font/schema/lifecycle44/0/0 and offscreen-GLES9/0/0; expanded API rerun also
verifies BOTH Modern/Retro restart persistence and missing-colour rollback.

Full emoji clusters are NOT complete: issue110 tracks flags, ZWJ, skin tones,
keycaps and VS15/16. Powerline seamless-edge joins are not guaranteed by the
optically fitted Nerd repertoire. Do not call this universal Unicode/emoji.
Host remains572/native KMS/settings237; no new package or deployment is implied.
Preserve operator's untracked releases/0.1.554.yaml.

## 2026-10-20 config-source-108 reconciled into canonical main

Merge13551e4 includes the complete maintenance tip456f354 as its second parent.
Configuration implementation2fb6f7f was already merged via803728a. The
maintenance tip456f354 still lacked ancestry integration: its563 manifest and
deployment ledger had equivalent main copies, while its historical current-state
entry retained an unreconciled completion update. Merge the complete tip, keep
one563 ledger/manifest, preserve every newer572 entry, and correct that stale
historical status. No runtime/schema/assets/dependency/release downgrade.

Pre-merge and merged production paths are identical. Fresh TCC server/helper
hashes exactly match the572 manifest (serverfe6041327ebdbd461460c59dff307c283f5af248743ce1e42824533a71b507b1,
helperb0736e2aceb7c77916470e279e7e9219c2219d565a6b6d77a2348f27d6034cf1).
Schema/lifecycle25pass/0skip/0fail, including all11 provenance/API-repair cases.
No live deployment or configuration change is needed for this history/docs
reconciliation. Canonical development is main. The old config108 worktree is
detached/locked at historical456f354 with ignored build/evidence preserved.
The merged local/remote fix branch is retired; remote deletion used an explicit
expected-tip lease atomically with the main push. Only main remains active.

## 2026-10-20 deployed572: one display temporarily removed by operator

Operator needs the additional monitor elsewhere for a few hours. Do not infer
an SDL enumeration defect from that physical topology change. Pause physical
multi-output acceptance until it returns; local/mock multi-output work can
continue. Do not change saved output/pane bindings to fit the temporary setup.

572 installed via API, package byte-identical SHA256
8b692eee6896c954516822be499d58e77832b2250f8a0e1894d0f20ca2037371;
sourcee1558b6, release93c6d96. Artifact/manifest checks pass. Latest trial's
baseline ALREADY has one native DP output, not two. SDL/GLES reaches ready API
with one DP output and sdl_kms_mode=legacy; timer restores native with one
output, exact saved settings237, Modern22px and original split/bottom primary.
This is SINGLE-OUTPUT startup/recovery proof only. No new two-output acceptance,
pixel, performance or hotplug claim. Earlier571 two-output crash evidence stands.

Evidence: build/trial572-gles/{baseline,trial,restored}-observation.json.
Future oracle verdicts now explicitly carry baseline/trial output counts and
single-/multi-output scope to prevent count-matching from implying two monitors.
Schema+lifecycle25/0/0 and renderer+GLES67/0/0. Additional actual-discovery
fixture verifies compatibility selection precedes SDL_Init, explicit conflict
is refused before enumeration, and another driver is not rejected; no DRM
opened in that fixture. These local results are not multi-output acceptance.
No Hibr/SDL edits or SSH; no further physical backend trials while operator
is changing topology. Native572 is the working live backend.

## 2026-10-20 candidate572: exact SDL fault and supported compatibility path

571 timed GLES trial restored native/settings237 again. Fault at second
initial SDL_RenderPresent, maps+remote/library SHA verified. ELF151bc1 resolves
to add_plane_property(), pinned SDL_kmsdrmvideo.c:248. Per-display plane setup
is guarded by global GBM initialization, leaving the second atomic plane unset.
This is not a Hibr/font cause. Read ADR0155 before changing the dependency.

572 selects SDL's public legacy-KMS hint before init for exact version3.4.16;
native untouched, no vendor edits. Explicit conflicting atomic=1 is refused
for kmsdrm; other drivers/versions untouched by refusal. API render.sdl_kms_mode
reports the effective path. Actual-hint/mocked-identity gate passes. Full
regression passes schema+lifecycle25pass/0skip/0fail; GLES startup-boundary
and actual helper protocol/dispatcher gates also pass. Timed physical trial
remains pending at this source checkpoint;
do not call SDL functional/parity achieved until the corresponding evidence.

## 2026-10-20 candidate571: locate the library fault

570 deployed natively with exact saved revision237. Protected GLES trial
still fails; independent recovery restores native. Journal now has one
startup attempt and `RUNTIME ERROR: invalid memory access` at an unsymbolized
shared-library address, no reliable caller. Do not infer stack corruption or
faulting function from the failed unwind. Evidence build/trial570-gles/ and
build/trial570-journal.txt. Trace-enabled local frame gate passes60.371Hz,
16.501ms maximum render, zero window missed deadlines; not physical timing.

571 adds startup-only executable maps and before/after stage markers, gated by
the validated trial ID. Normal startup is silent. Map/source gate passes and
actual two-renderer startup boundary runs with protected logging enabled.
No rendering behavior fix yet. Exact bundled/local SDL SHA256 both
d91f2936576a0fee7bf5cb231f8528be65ffa44e2ed7cbea480d599a0bbd08c2;
library contains debug_info for offline symbolization. ADR0154 extended.
Schema+lifecycle24pass/0skip/0fail; physical mapping/trace is still pending.

## 2026-10-20 candidate570: obtain the fault trace before patching SDL

Canonical server build adds installed TCC `-g -bt8` diagnostics, not `-b`
bounds instrumentation. Deliberate subprocess gate verifies source/function/
caller reporting and unchanged normal exit. Presentation trials temporarily
set Restart=no to avoid the repeated startup crashes seen on569. Existing
native configuration/service ownership remains unchanged. ADR0154.
Physical trace is pending: this is observability, not an SDL correctness fix.
Schema+lifecycle23pass/0skip/0fail, including real TCC fault reporting and
restart/shutdown. API-only host_presentation_journal.rb reads the service
journal via a temporary free-pane PTY, combines scrollback/visible cells and
cleans up; default is the bounded current-boot tail, not a wall-clock window.

## 2026-10-20 deployed569: recovery works; SDL startup SIGSEGV reproduced

API-installed/version569 verified; source76ddc7f, release7d00015. Package
rebuild byte-identical (SHA78f7db0c7b46d9675353a8cef9a7b58483c4df75003d11d4482e0041c888b5c0);
artifact/manifest checks pass. Schema+lifecycle22pass; renderer+GLES67pass;
all zero skips/failures. Not a new whole-all/timing run;568 timing miss remains.

Actual API-only30s software and GLES trials both failed before listener startup.
Independent systemd expiry restored ready native KMS, both outputs and identical
saved revision237 in both cases. Software poll window31.25s. No terminal/SSH
intervention, canonical writes or Hibr modifications. Root helper reports
restored/inactive; bottom primary login awaits authentication. Native remains
the working live backend. Do not call either SDL trial a pass.

Journal evidence: SDL creates display1 at1920x2160 and display2 at1920x1080,
then process exits status11/SEGV, followed by service failure retries until
expiry. No claim that the faulting function is known: obtain a pre-listener
fault trace before choosing a patch. No coredumpctl/gdb found on host; core
pattern is `core`, no `/core` observed. Avoid guessing at font/GPU causation.
SDL second-output mode also differs from native HDMI2560x1440; track separately.

Evidence: build/trial569-software/, build/trial569-gles/,
build/trial569-startup-journal.txt. API-only reproducible startup/recovery
oracle: tools/probes/host_presentation_trial.rb. Startup and recovery verdicts
are deliberately distinct. Issues109/104 retain remaining coverage/SDL work.

## 2026-10-20 candidate569: independent presentation recovery

Implements Blit109 / ADR0153: authenticated bounded presentation-trial API,
existing root helper, independent systemd timer and owned runtime override.
Normal settings are never overwritten. Trial settings are explicitly read-only;
login is disabled; stale recovery IDs cannot affect newer trials. Failed kill
or restart commands retain active failed state rather than claiming recovery.
Helper `restored` means restart queued, not native readiness confirmed.

TCC helper/server compile. Focused actual-helper Unix protocol, separate-process
recovery CLI, blocked frontend, helper restart, command timeout/reaping, real
API dispatcher, canonical snapshot preservation and schema checks pass locally.
Root systemd commands/native API runtime are mocked in those fault tests.
New gates are wired into the lifecycle suite: schema+lifecycle22pass/0skip/0fail.
The API-only `host_presentation_timer.rb` canary confirms a real independent
systemd timer/service executes on10.54 without restarting Blit or changing its
configuration. The first probe attempts exposed operator-tool mistakes (argument
length and stopped-state naming); corrected probe passes. This is scheduling
evidence, not actual Blit rollback. Physical recovery remains pending; no live
SDL startup has been attempted or claimed safe yet.
Live API rechecked567, bottom DP login awaiting authentication. Preserve that
native baseline until independent recovery has physical evidence. Candidate568's
earlier21.754ms timing failure remains recorded below; this work does not erase it.

## 2026-10-20 candidate568: SDL output creation binding

Found and corrected a concrete Blit adapter defect: SDL_CreateWindow used
default-display allocation followed by an unchecked move, but pinned KMSDRM
rejects moves and allocates its GBM/EGL resources during creation. Bind the
display in SDL_CreateWindowWithProperties, verify actual binding, and retain
fullscreen/renderer policy. Existing runtime logs now retain distinct window,
binding, fullscreen and renderer errors with display IDs. ADR0152.

Production initialize_outputs gate is red before (moves2/bindings0), green
after (distinct IDs11/57, moves0). Real software/offscreen GLES renderers,
mocked display identity; four injected failure paths validate logs and window
cleanup without opening DRM. Not physical multi-output proof. Earlier SDL
inventory exposed only DP despite two connected native outputs: enumeration
is separate and NOT claimed fixed. Native567 stays live, Modern22px, clean
settings237 and bottom login awaiting authentication. No vendor/Hibr edits.
Source5ee47fa, candidate568 built/packaged byte-reproducibly; artifact and
manifest checks pass. Combined all+GLES regression:122pass/4skip/1fail.
The failure is frame_rate_gate:21.754ms maximum render vs20ms bound; no
limit relaxed. Fixed three baseline567/candidate568 pairs subsequently all
pass at59.998..60.006Hz on local dummy/software, candidate maximum render
16.446..16.604ms. The original miss remains unresolved, not erased by those
results; no clean whole-suite or physical cadence acceptance is claimed.
Evidence: build/sdl568-timing-comparison/. The120s/four-PTY soak passes
12080 snapshots/1098 resizes. Package is held, NOT deployed or tagged stable.
Live remains native567; font22px/settings237 unchanged. See verification ledger.
Before physical startup trials, address Blit109: presentation initialization
precedes the API listener, so API-only rollback cannot recover a blocked
initialization call. An independent bounded recovery must protect the native
baseline. Do not silently switch the host or equate a timeout with recovery.

## 2026-10-20 deployed567: colour proportions; SDL startup next

Live567 is ready/native KMS, Modern22px, login awaits authentication. No live
desktop visual approval is inferred. Production-raster metrics at22px show
H9x13 ink, pencil11x11, bell20x20 in its22x22 two-cell allocation. At19/23px
the bell similarly fills almost the cell height. This is optical sizing,
not an observed overflow or proof of the odd-size wallpaper report.

ADR0151 bounds colour artwork to the fitted text em, keeping cell size,
two-cell advance, text/Nerd glyphs, settings and backend unchanged. New
22px bell optical-bound regression fails before/passes after. Its resulting
ink is17x18, allocation still22x22. The read-only source metrics probe is
tools/probes/font_glyph_metrics.c. Font suite15pass/0skip/0fail; expanded
native/software/GLES1026 samples each pass (native exact, SDL tolerance2).
Package rebuild byte-identical; artifact/manifest gates pass; supervised API
deployment/version567 verified, saved settings237 clean/unchanged. Visual
approval remains pending; source raster metrics are not live screenshots.

Next: SDL104 connected-monitor startup first, then connector hotplug. These
are separate boundaries: SDL's known missing hotplug observer is not proof
of the reported already-connected second-monitor startup cause. Preserve
native567 as rollback, capture complete startup identity/errors on both
outputs, and establish a failing regression before editing the adapter or
dependency. No live backend switch occurred during this font cycle.

## 2026-10-20 deployed566: Modern Hibr Nerd icons and menu pencil

Supervised API install/version verification succeeded on10.54. Active font is
BlitModernNerd-Regular.ttf at22px/11x22 cells; native drm-kms ready. Saved
revision237 remains clean and unchanged across installation, with both outputs,
DP1x2/HDMI1x1 regions and lower-primary preference retained. Login manager is
waiting for authentication on pane-display-93-2. Operator visual approval of
the new menu pencil and Nerd symbols awaits login. Package rebuild SHA256 is
5eb77e95475cbd3e644b288566d8e13ce6b97002cf9af5bcdc4ebcd62e6fead4,
byte-identical to the first build; artifact/manifest gates pass. No SDL switch.

Native565 mouse is operator-accepted. Candidate566 changes only Modern's
outline asset/profile path and verification/documentation, not mouse code,
backend, settings schema or Hibr. BlitModernNerd-Regular.ttf is a renamed
derivative of pinned Nerd Fonts3.4.0 Hack Mono, retaining Hack Unicode plus
1475 Font Awesome mappings; other Nerd icon families are excluded.
Hibr's existing U+270E menu character uses the same complete outline as
nf-fa-pencil. All icons are aspect-fitted inside fixed advance/cap-height;
this corrects measured negative-sidebearing rejection at small sizes.

The new production-raster gate fails on old Hack and passes1140 samples
(Hibr's18 distinct Nerd PUA symbols plus two pencil mappings,57 sizes8..64).
Native and SDL software/GLES pixel gates pass969 samples each, native exact
including guard pixels, SDL tolerance2. Full font suite15pass/0skip/0fail;
font generation byte-reproducible, asset/license identities pass. This is
not an all-suite rerun. TCC server/helper566 build. Packaging and API deployment
are verified as above; aesthetic approval remains pending. Colour bell sizing
is unchanged/unresolved. See ADR0150 and font-evolution-plan.md.

## 2026-10-19 operator accepts565 mouse motion; Modern fonts next

Operator reports "It's perfect!" for physical movement after565. Next priority
is Modern visual sizing/width and Nerd coverage under107, then SDL104. Native
KMS/layout stay unchanged. See font-evolution-plan.md for the bounded sequence
and confirmed distinction between Hibr's glyph-set and notification selectors.
The reported oversized bell has not yet been measured; do not claim a fix.

## 2026-10-19 pointer565 fix: idle output consumed another output's motion

Clean three-second API60Hz sweep on564:180 injections,73 confirmed DP flips
over3.090s (~23.6Hz), zero physical mouse events. Last native present1.52ms,
Blit CPU10.8%; network-arrival jitter remains part of this workload, not a
physical-input latency measurement. Evidence build/mouse101-564-clean60/.

Confirmed code defect: KMS updated a global previous-pointer snapshot even
when only the idle HDMI output was serviced and no pixels were submitted.
The DP output later saw an unchanged pointer and skipped presentation. Content
redraw can mask this, consistent with the YouTube clue. ADR0149 replaces the
global snapshot with per-output state updated only on successful submission.

New production-compositor/mock-DRM gate fails before the fix at "idle output
consumed active pointer motion"; passes afterward for120 idle-first motions,
both crossing orders, hide, pending flips/latest motion, modeset and no-trail
pixels. Existing native composition/art/clipping and64 hotplug cycles pass.
565 builds with TCC; focused API pointer geometry/pixel checks pass, as do
reproducible package/artifact checks. Source3bac488/package SHA256
f2e732b3d63e7df4c757fae1fb5b5e5b1bab7fc47acf35f7816a53c28877e89e
is API-deployed/version-verified. Native KMS, Modern22px and layout retained.
After verified desktop login, the same requested60Hz/3s sweep produced170
confirmed DP flips in3.100s (~54.8Hz), versus73/~23.6Hz before. Both clean
runs had zero physical mouse events; absolute pointer position and desktop
scene were not identical. A requested120Hz/2s capacity check produced122
submissions/121 confirmed completions in2.080s (~58.2 confirmed Hz) on the
59.997Hz output. Network arrival/order remains part of these API workloads;
they are not physical input-to-photon measurements or universal60Hz acceptance.
Evidence: build/mouse101-565-clean60/ and build/mouse101-565-capacity120/.
Pointer restored; saved revision230 remains clean, Modern22px/native KMS and
bottom-primary policy unchanged. Physical feel still needs operator acceptance;
keep101 open. Next is SDL104, without silently switching the working backend.
No font, Hibr, schema or SDL changes. This was
targeted565 verification, not a new full112-gate run (that belongs to564).

## 2026-10-19 revised priority: mouse101, then SDL104

Operator prioritises choppy/inconsistent mouse motion and SDL multi-output
over further font polish, explicitly requesting bounded effort. Keep working
native KMS, two physical outputs, three panes and current Modern font choice.
The menu pen, notification and restart glyph refinements are deferred on107.
Odd font sizes may corrupt wallpaper while even sizes appear better: this is
an unconfirmed user observation, not an established cause or even-only policy.

One three-second API60Hz sweep used the existing host_pointer_cadence probe;
position restored, no clicks/keys/sessions/settings changes. It delivered180
injections and122 confirmed DP flips over3.160s (~38.6Hz). However461 raw
physical mouse events also arrived; fresh-connection network timing varied,
and async request arrival order is not guaranteed. This is NOT an isolated
input-versus-presentation diagnosis. Evidence: build/mouse101-564-api60/.
Asked operator briefly to leave input untouched for a clean baseline. Do not
repeat expensive broad suites for read-only measurements or change backend to
make timing look better. Start with bounded workload/phase evidence; introduce
a focused failing regression before implementing a demonstrated defect.

## 2026-10-19 font release564 deployed; native antialiasing measured

Live10.54 is564, native KMS unchanged, both physical outputs presenting and
all three physical pane regions retained. Non-font saved settings checksum is
unchanged; bottom-primary login preference remains pane-display-93-2.
Both new profiles are installed/available. API previews of Retro and Modern
at17/19/23px pass three-pane geometry/capture checks and restore the original
profile/policy. Agent then selected Modern19px; another client changed it to
22px, which was preserved (clean saved revision230 at observation).

Actual native-KMS menu glyph F at19px: Retro2 colours/0 intermediate coverage
levels; Modern13 colours/11 intermediate levels, all matching foreground-over-
background coverage. This is real captured pixel evidence, not an API-only
claim, and not universal glyph acceptance. Evidence: build/font564-evidence/.

Full exact-binary regression112pass/3skip/0fail; GLES7pass/0skip/0fail;
new reversible-host-probe gate passes separately. Reproducible final archive
source da7e32d/SHA256712b0b72897d9db244ecb2a702bf1ccd0a9026491697a3e75ce9d5f812d60556.
Native font draw Valgrind:7,478 allocations/frees, zero errors/leaks.
Hibr0.99.262/ABI16 executable unchanged during checks; no core edits or SSH.
Installed panel0.1.7 discovers both profiles; actual modern-font UI drag19px
and keyboard+1/-1 passed in hibr-apps8006a7c. See verification-status.md.

Preview images include intermediate/incomplete application redraws; they are
not accepted whole-desktop reference images. Sessions1–3 were replaced by4–6;
the operator explicitly confirmed restarting Hibr during testing. Do not infer
a Blit-caused desktop failure from those replacements. Aesthetic/whole-desktop
resize review remains operator-facing; do not claim it passed from geometry.
Pointer choppiness remains open on101; foreground YouTube observation is a
reproduction clue, not a diagnosis. Next phase is measured pointer cadence,
preserving native KMS. The following resumed/held-font entries are historical.

## 2026-10-19 font release564 resumed; pointer observation recorded

The operator confirms563 looks correct on native KMS but pointer motion remains
inconsistent; foreground YouTube appears to reduce choppiness. Recorded on101,
not diagnosed. User prioritises finishing antialiasing/new fonts before mouse
performance. Native backend/layout/login must remain unchanged; never edit Hibr.

Candidate564 includes the existing Spleen retro and smooth Hack/Noto modern
profiles plus108 config ownership fix. The archive was recut at sourceda7e32d
to clarify the first-production version boundary in the packaged API guide;
no server/input/rendering code changed from7db553e. Final reproducible package
SHA256712b0b72897d9db244ecb2a702bf1ccd0a9026491697a3e75ce9d5f812d60556;
server SHA25655fa017d190add2dd4777487e19d4f79bc7bc8b4642941dc8b850c1e432efec6.
GLES7pass/0skip/0fail; full regression is running, live remains563. Original
Terminus16 is retained until deployment and an announced font preview.
The actual live probe now accepts a profile and safely restores original
profile/size; its isolated actual-server gate passes both profiles/refusal.
The actual Hibr Control Panel passes19px drag and+1/-1 with modern selected;
captures under build/font564-panel-evidence were inspected. The live host has
libfreetype6 installed at2.13.3+dfsg-1+deb13u1 (API diagnostic, no SSH).
See font-evolution-plan.md. Older held-font entries below are historical.

## 2026-10-19 configuration ownership fix (#108)

The operator confirms native KMS restores all monitors and the panel's mode is
correct. Config-only maintenance563/source2fb6f7f is based on deployed561,
excluding the unfinished font candidate. It adds provenance and invalid-default
recovery under ADR0148. Signed catalogue panel0.1.7/source2cd8f7c displays this
metadata and has passed callbacks, old-server fallback, UI, Mouse and catalogue
lifecycle checks. The identical server patch is merged here; main564 retains
pending font work and is NOT the maintenance deployment artifact.

The live fallback line was normalized from `kmsdrm` to `kms`, preserving
`/etc/blit/blit.env.before-108` and all credentials. Saved API settings remain
authoritative; the deployment service restart reloaded the validkms fallback.
Package563 is deployed/version-verified. Full regression104pass/5skip/0fail,
GLES6pass/0skip/0fail; skipped package/update-restart checks pass separately.
Native health is ready; both outputs present, bottom-primary login retained.
Saved document checksum and clean revision213 are unchanged. Panel0.1.7 is
installed with catalogue checksum verified. See verification-status.md for
scope/skips. SDL multi-output startup issues100/104 remain open.


## 2026-10-19 priority: restore native KMS; font release held

The operator reported second-monitor startup trouble and environment/UI
backend disagreement, and explicitly requested returning to native KMS.
No562 package was deployed. Live remains0.1.561. API confirmed SDL3 video
driver `kmsdrm`, renderer `opengles2`, one exposed DP-1 while sysfs reported
both DP-1 and HDMI-A-1 connected. The service/process environment contains
`BLIT_DISPLAY_BACKEND=kmsdrm`, which is invalid for Blit's own backend enum;
only exact `kms` selects native KMS. Saved document.server.display_backend
was `sdl`, which takes precedence under ADR0138. The UI's single backend row
did not explain driver, renderer or configuration source. Issue108 tracks
explicit source/layer reporting and invalid-environment handling. SDL's
second-output startup cause is not established; #100/#104 remain open.

Using only the existing API, saved `server.display_backend=kms` and requested
supervised restart. Native health is ready, both physical outputs present:
DP-1/display-93 at1920x2160 and HDMI-A-1/display-102 at2560x1440. Native hotplug
is `uevent+periodic`. DP retains1x2 and HDMI defaults1x1. Backend-scoped pane
IDs changed; the old SDL `pane-display-1-2` login preference was explicitly
remapped to native DP bottom `pane-display-93-2`, saved and restarted again.
Login and preference now agree; storage clean revision213, no restart pending.
The saved SDL renderer preference remains inactive while native KMS is active.
At that recovery stage no environment edits, SSH, Hibr changes, font selection
or size changes were made. The user subsequently confirmed all monitors work;
the later environment correction is recorded above.

Font candidate562 identity is recorded in `releases/0.1.562.yaml`, source
`b63d7f4`, package SHA256
`d9836e635b625d722a5d93267498c4629d78c84f1e55d6d4db13a27eade4543b`.
Reproducible package and GLES7/0/0 pass. Exact-binary full run reports
110pass/3skip/1fail (pointer-composition fixture). The failure was reproduced
by batching create/defer/commit before initial presentation: a correct first
full redraw cannot satisfy an incremental-redraw assertion. The fixture now
seeds/commits a baseline and waits for its captured revision before testing
incremental updates; assertions are retained and binary override is honoured.
Ten candidate repeats, released561 and conservative full-rebuild mode pass.
A complete rerun with that fixture correction and physical font review remain
pending. Do not deploy/tag562 as accepted or resume fonts before the operator's
native KMS priority is resolved. The entries below are prior checkpoints.

## 2026-10-19:562 modern/retro production integration; release gate running

`blit-modern` now uses the shared native-size FreeType coverage/colour module
on native KMS, SDL software and SDL/OpenGLES2. Both new profiles are integrated;
Spleen is deliberate pixel-art, modern Hack/Noto is antialiased plus scalar
colour, and existing Terminus remains default. API metadata exposes renderer
policy/cache counters; the canonical size/profile settings still own saving.
Atlas replacement is prepared transactionally; SDL cache collisions flush
pending geometry before texel replacement. Build/runtime FreeType dependency
and pinned licensed assets are declared. No Hibr core edits.

Production KMS pixels/guards match exactly and both SDL paths match within2
channel levels across all57 heights8..64 and513 glyph samples each. SDL slot
collision checks and API profile/size/save/restart/missing-colour rollback pass.
The actual installed Hibr Control Panel passes slider/+1/-1 with modern font;
captured sample inspected at `build/font-modern-panel-evidence/`. Native KMS
Valgrind:7,364 allocations/frees, zero errors/leaks. This is not physical KMS
or universal Unicode approval. Full regression/reproducible package/API deploy
and physical capture remain pending; live host remains561, existing layout
and font unchanged. The earlier module-only checkpoint below is historical.
Final scalar review fixed invisible selectors/joiners previously falling
through to a visible missing-glyph marker; module and all three adapter gates
now cover them. Modern legacy scale writes1.1/1.7/2.3/3.0 pass too. The earlier
exploratory full run reports107pass/7skip/0fail, but is not frozen acceptance.

## 2026-10-19: smooth/colour raster foundation; not deployed

The user's jagged-size report exposes unfinished native-size rasterization,
not a Control Panel setting defect. ADR0147 adds `src/blit_raster.c/.h` as the
shared FreeType-backed coverage/colour cell module, with PSF fallback, fitted
baseline/advance, bounded cache and common premultiplied blend semantics.
TCC checks every cell height8..64. Font suite with explicit reviewed inputs:
9pass/0skip/0fail. The server does not yet link/call it. No new font package,
live change or claim of smooth desktop rendering. Follow the remaining work
in `docs/font-evolution-plan.md`; do not mistake component tests for backend
integration or physical approval. Hibr is untouched.
Valgrind reports zero errors/leaks across all57 sizes. Installed Hibr changed
258 to259 during this work (not by this agent); full frozen acceptance is not
established. The isolated raster module does not depend on Hibr.

## 2026-10-19: Control Panel0.1.6 exposes font size in pixels

Canonical catalogue bundle in `itdlabs/hibr-apps` commit `33be7b3` replaces
the old scale slider with **Font size**,8–64px via existing `PUT /v1/fonts`
`size_px`. Keyboard arrows select every1px; the native11-position drag track
samples5–6px steps. Font profile selection remains independent. Blit owns
automatic persistence; no second preferences store or Hibr core changes.
Successful font replies replace only the draft's font policy, preserving
unrelated edits and the original revision guard. Unsupported servers show an
upgrade notice. See hibr-apps ADR0005 and Blit ADR0144; issues Blit#107 and
hibr-apps#1.

Actual callbacks, limits, queued keyboard/drag coalescing, staged-edit
preservation and older-server capability handling pass against the extracted
released0.1.561 binary. Native installed Hibr UI pointer drag19px and keyboard
20/19px pass; shared Mouse callbacks/UI and catalogue install/update/remove
pass. Hibr0.99.258/ABI16 binary fingerprint stayed
`a17716c1a56eb4ca9f0e691b4323dbd4cef300526f6a0b99ffaadda084a3ceeb`.
Generated catalogue checksum and detached signature verify. Publication is not
proof the app is installed on10.54: update via `catalogue refresh` then
`catalogue update control-panel/blit`; no new Blit Debian package is needed.
Live display/settings were not changed during these isolated tests.
Candidate562's separate frozen full-regression gate below remains pending.

## 2026-10-19 candidate562: selectable retro font and exact SDL atlas boundaries

ADR0145 adds optional `blit-hibr-spleen16`: pinned Spleen2.2.0 plus the exact
checked-in Hibr fallback.1,575 scalar mappings retain1,181 unique fallback
mappings;92 printable ASCII masks differ. Protected box/block, combining and
control-diamond geometry is unchanged. Regeneration is byte-identical and
notices/provenance are bundled. Terminus remains default.

The new asset exposes a software-only missing U+2581 row. Asset masks match
the default, GLES2 passes, and software repeatedly fails. SDL's integer UV
truncation with900px atlas height is the identified boundary. ADR0146 uses
power-of-two extents plus cached actual dimensions; the same fixture passes
afterward on both SDL paths. No SDL/Hibr code changed. Production damage/
UV round-trip tests pass on software/GLES2. Expanded local grid oracle checks
115 cells/14,720 exact pixels on both paths, including ASCII. Its request
batching is bounded to16cells to stay inside the existing HTTP envelope.
Full regression is running. Live remains561 SDL/GLES2 with original font
restored. No562 package, physical or new-font aesthetic approval yet. The
modern antialiased/colour profile remains pending.

The candidate's full run reports108pass/7skip/0fail, with a120-second
four-PTY soak of10,996 snapshots/1,000 resizes. However its final dependency
fingerprint guard fails: installed Hibr changed0.99.257 to0.99.258/ABI16
during the run. This is a mixed-dependency run, not frozen acceptance; do
not package/deploy562 on that claim. Live remains561. Operator was asked to
pause Hibr upgrades at258 for a fresh verification. No Hibr changes were made
by this work. Source and focused pixel evidence can be committed as a
checkpoint; full frozen verification/package/physical approval remain open.

## 2026-10-19:561 pixel-size API deployed; next is the rich glyph pipeline

Source `65e785d`, identity `3c5371b`, package SHA256
`728dc380db96faf5fd3859ed820eabaa4fd0ef5bbd3f10bb4108b8cda4a1d147`.
Independent builds are byte-identical; artifact/provenance test passes;
API updater verifies561 on10.54. Physical SDL/GLES2 probe exercises17/19/23px,
checks unchanged physical regions for both panes and complete grid bounds,
captures BMPs and restores original font policy. Evidence:
`/tmp/blit-font-size-561-20261019/`. Verified after restoration: SDL/GLES2,
bottom login, scale1.0/16px/8x16 cells, clean storage revision162. The23px
capture shows a screensaver rather than a desktop glyph specimen; no font
aesthetic/coverage or desktop pixel-perfect claim is made from it.

Both new profiles remain pending. Next implement shared grayscale/colour
glyph data, baseline fitting and bounded caching before wiring modern fonts
into either rendering adapter; retain PSF support and current default.
TCC/FreeType source feasibility, candidate source hashes/licenses and the
23em-versus27px-line-height finding are in `font-evolution-plan.md`.
## 2026-10-19 config-only maintenance563 (Blit108)

The user confirms all monitors work after restoring native KMS on10.54;
the panel reports the correct backend. Saved API settings own this choice.
The invalid environment fallback `kmsdrm` was corrected to `kms`, with a
root-owned backup at `/etc/blit/blit.env.before-108`; credentials were preserved.
Deployment's service restart reloaded the corrected environment fallback.

The historical maintenance release was based on deployed561, not the unfinished new-font
candidate562. It adds explicit API provenance, invalid-default diagnostics and
API-accessible recovery; it does not change rendering, input or font engines.
The eleven-case provenance gate passes; full regression104pass/5skip/0fail
and GLES6pass/0skip/0fail. Package/update-restart checks pass separately.
The reproducible563 package is API-deployed and verified: both physical outputs
presenting, bottom-primary login retained, clean revision213 and unchanged
saved document checksum. Installed panel0.1.7 matches the signed catalogue.
See ADR0148 and verification-status.md for scope/skips. Historical entries below
describe earlier candidates, not current live state.


## 2026-10-19 candidate561: explicit font pixel height, new profiles still pending

Blit#107/ADR0144 extend the canonical font API with integer `size_px`8..64,
exclusive with legacy scale. One shared size-policy parser/dimension function
serves live requests, saved settings and all renderer geometry. GET reports
sizing policy and resolved pixel metrics; settings store only one control.
This is exact cell sizing for current bitmap fonts, not native outline or
colour-glyph rendering. Both requested new profiles remain unimplemented.

The new font API assertion is red before implementation and green afterward.
Actual glyph pixels at8,16,17,19,23,32,48,64px, legacy compatibility,
profile-only retention, malformed/duplicate/competing controls,23px persistence
and unchanged saved revision on refusal pass. TCC, schema, split/pointer
geometry and production damage fixtures pass. Full regression is running.
Installed local Hibr is now0.99.257/ABI16; no Hibr changes were made by this
work. Its binary fingerprint is checked before/after. Live remains560 SDL/GL
at saved scale1.2; no candidate deployment or new-font visual acceptance yet.

Full run completes107pass/7skip/0fail; four-PTY120-second soak records11,816
snapshots/1,074 resizes. Installed0.99.257/ABI16 fingerprint is unchanged:
`572f88f60eac0656bf6bd86b9a89a9a939de05caf03c5bd1175e87dce8dd4735`.
Updated font fixture passes separately on software and offscreen GLES2;
updated schema assertions and three pinned-header adapter gates pass.
`host_font_size.rb` passes locally on two panes, captures17/19/23px and
restores its starting policy. Packaging/live validation are next. A read-only
live topology check now reports8x16 cells; preserve the actual current font
choice rather than assuming the earlier1.2 configuration is still active.

## 2026-10-19 operator configuration: SDL/GLES2 and scale1.2

Operator requested SDL/GL and then prioritised font sizing, a new selectable
font, and performance in that order. API verifies SDL3/KMSDRM/OpenGLES2 on
0.1.560. The old native preferred pane ID did not resolve under SDL; it was
corrected durably to `pane-display-1-2`, with login running on the bottom.
Live font API accepts scale1.2 and reports10x19 cells; complete settings
replacement confirms clean storage at revision157. DP1x2 layout is preserved.
No Hibr changes. The operator confirmed both retro and modern multicolour
profiles. Native-size and glyph-format implementation decisions remain
pending; see `font-evolution-plan.md`. Existing font remains available. These
configuration checks do not claim new visual/font or performance acceptance.

## 2026-10-19:560 deployed; initial-read delay reduced on physical SDL paths

Source `71d55cf`, package identity `4c7a629`; package SHA256
`8cdaa78a852484e25e58535f781e0e8320be5bb4fe9c3aae01ccfa1ec9b0a22a`.
Independent packages match byte-for-byte and the artifact/provenance gate
passes. API updater verifies installed0.1.560. All three actual backends
pass bounded motion12/12, exact Kitty composition/deletion,2,688 font/grid
pixels and sixel assertions. Native/login/pointer baseline is restored.
Evidence: `/tmp/blit-backend-cadence-560-240hz-20261019/`.

Ten-second240-slot/s sweep: native2400delivered/0client skips, request
p95=19.78ms, confirmed flips59.39/s; SDL/software2400/0,p95=32.88ms,
submissions59.82/s; SDL/GLES22394/6,p95=33.92ms,submissions59.32/s.
Previous559 SDL client skips171/154 and p95=51.76/51.45ms respectively.
Connection-setup p95=11.66/11.92/12.80ms; post-connect exchange
p95=11.84/26.14/25.52ms for native/software/GLES2. Those exchange values
include transport and response handling, not server-only or input-to-photon
latency. All samples report0missed deadlines; SDL confirmed completion
timestamps remain unavailable. This is one DP canvas at approximately60Hz,
API-injected motion only, not multi-connector/high-refresh or sustained
full performance acceptance. Keep105 open for residual pressure; SDL
hotplug dependency decision and HDMI availability remain pending.

## 2026-10-19 candidate560: one nonblocking initial read per admitted client

Issue105/ADR0142 continue from559's remaining client-pressure result. A
production socket regression sends a complete request before admission and
fails because the ready request receives no service in that turn. Accepted
clients now explicitly use `O_NONBLOCK` and get one call to the existing
request-service function inside the existing eight-admission limit. Partial
and empty clients retain the normal readiness loop. No second parser, SDL
thread or renderer policy is introduced.

The new assertion passes after the change, as do socket capacity/descriptor/
budget checks, the software/GLES2 production fixture and the delayed-present
burst test. TCC build succeeds. Full regression, packaging and physical
revalidation are pending; live remains559 with native/login restored.
The initial full run exposed a font-test race: its `ready` PTY marker can
arrive between revision GET and cell PUT, correctly causing409. The fixture
now waits for the actual marker before taking the revision; this also parks
its cursor outside the glyph oracle. The focused font gate passes. Product
font/cursor/revision code is unchanged; the interrupted run is not a pass.
Transport spot checks also fluctuate (connect102–949ms, ICMP43ms); they are
not a stable baseline or proof of a server defect. Keep network RTT and
server-stage latency distinct. SDL patch direction and HDMI remain pending.

A subsequent full run completed106pass/7skip/1fail, exposing initial join
placement against a nonzero native-primary origin. This is a Blit helper
coordinate error (ADR0143), not evidence of a Hibr defect: supported native
movement is primary-relative, whereas our helper passed an absolute target.
The helper now subtracts the native primary origin from the login-relative
target without changing the selected primary. Focused nonzero-primary and
actual-desktop/resize restart/rejoin tests pass. Full regression is being
repeated; neither earlier run is release acceptance. The operator has
confirmed keeping local Hibr0.99.256/ABI16 unchanged for this verification;
check its binary fingerprint before and after the run.

Fresh frozen-baseline run completes107pass/7skip/0fail, including six
offscreen GLES2 gates. The120-second four-PTY soak records11,504 snapshots
and1,046 resizes. All three pinned-header adapter gates pass separately.
Hibr0.99.256/ABI16 fingerprint remains
`1dd031259b4d16ccbfc3c0ffe6335d28fdaaa6be4d2cfe0f6187b6d1680a73b9`.
Packaging and physical revalidation remain pending; live is still559.

## 2026-10-19: 0.1.559 deployed; physical SDL admission throughput triples

Package559 is installed/version-verified; builds match byte-for-byte. Source
`f87def0`, manifest `releases/0.1.559.yaml`. All three actual paths pass motion
12/12, exact Kitty composition/deletion,2,688 grid/font pixels and sixel
assertions. Native/login/pointer restoration succeeds. Evidence:
`/tmp/blit-backend-cadence-559-240hz-20261019/`.

At240 requested slots/s, native delivers all2,400 injections and records59.15
confirmed flips/s over the server observation window. SDL/software delivers
2,229 (220.71/s), skipping171 at the client queue; SDL/GLES2 delivers2,246
(223.46/s), skipping154. Their request p95 values are51.76 and51.45ms, versus
about135ms before559. SDL submission rates are58.30 and58.65/s; completion
rates remain unavailable. This is substantial throughput improvement, not
full physical performance acceptance. Issue105 stays open for the remaining
read/admission-turn latency and client skipping; next reproduce initial
request-read deferral before further changes. Multi-connector/SDL hotplug
acceptance still requires HDMI availability and the SDL dependency decision.

## 2026-10-19 candidate 0.1.559: bounded API admission between presents

Issue105/ADR0141 address the independently reproduced SDL request-admission
delay. The listener is nonblocking and each turn accepts at most eight
connections, preserving existing request/auth/capacity handling and normal
input/PTY/output servicing. No SDL dependency or terminal policy changes.

The controlled actual-renderer fixture fails before the change (eight
responses one refresh apart, last139.39ms), then passes after it (all within
17.83–18.29ms). The production socket fixture also verifies the accept bound,
finite draining, untouched render deadlines, close-on-exec,32-client capacity
and five overflow503 replies. TCC build succeeds. Full regression passes
107 gates with7 explicit skips and0 failures; its120-second four-PTY soak
completes11,320 snapshots and1,030 resizes. Three pinned-header adapter gates
pass separately, as does the focused GLES admission red/green test.
Package construction and physical SDL throughput revalidation are pending. Live
10.54 remains0.1.558, direct KMS with login restored. No performance acceptance
claim follows from the simulated-present fixture.

## 2026-10-19: 0.1.558 deployed; idle completion defect verified fixed

Package 0.1.558 is installed and version-verified on 10.54. Its two builds
are byte-identical; source `c411650`, identity in `releases/0.1.558.yaml`.
All three actual backends again pass motion 12/12, Kitty composition/deletion,
2,688 font/grid pixels and the sixel protocol/pixel checks. Original native
backend, login policy and pointer pose are restored. Evidence:
`/tmp/blit-backend-cadence-558-20261019/`.

Native confirmed flips measure 51.26/s with 59.20 offered motions/s, versus
38.72 and 40.85/s in the earlier 557 runs. Its retained trace has 0 no-op
render attempts (63 render-tail events), versus 14 in the prior 557 tail
(64 events). This validates removal of the targeted idle-completion defect,
not the entire performance goal. SDL/software and GLES2 submissions measure
52.83/s and 52.24/s; completion timing remains unavailable for SDL.

Next: issue105's independently reproduced API admission latency under a
16ms SDL present. Eight queued requests return one refresh apart, taking
approximately 139ms for the last response. The next fix must be a bounded
nonblocking accept turn, without changing renderer/terminal policy. Sustained
cadence/latency, multi-connector behavior and SDL hotplug remain open.

## 2026-10-19 candidate 0.1.558: idle KMS completion preserves visual deadlines

ADR0140 isolates the completion-induced no-op from the physical trace. A
production-code regression fails before the fix: completion fabricates
`render_pending=1` and advances deadline 16000 to 32666 without new work.
The event-service helper now retains existing pending work but leaves topology
and deferred retries responsible for requesting their own renders. No damage,
scene boundary, refresh target or SDL scheduler change is introduced.

The focused regression passes on software and offscreen GLES2, including
retained pointer work, existing ready-retry and held-scene checks. TCC build
succeeds. Cached-adapter metadata also verifies that timeout recovery still
requests rendering; the helper explicitly preserves this separate case.
The first full run was stopped during review to add that guard and is not
counted as a pass. The fresh full run passes 106 gates, with 7 explicit skips
and no failures. Its 120-second four-PTY soak completes 11,416 snapshots and
1,038 resizes; all three pinned-header adapter gates pass separately.
Package construction, deployment and physical cadence revalidation are
pending. Live remains 0.1.557 with native/login
restored after the trace comparison. Candidate558 is not claimed to solve
the entire performance gap; HTTP arrival bunching is separately observable.

## 2026-10-19: retain pointer/render trace tails for scheduling diagnosis

The pointer probe now retains existing API `pointer_motion` and `render_path`
trace tails alongside raw observations. The measurement regression verifies
these artifacts and still passes all six scenarios. No runtime change.
The new native trace under
`/tmp/blit-backend-cadence-trace-557-20261019/native/` shows completed flips
followed by no-op render attempts; input arriving just afterward waits for
the next advanced deadline. Example relative timestamps: submit at 362.611ms,
no-op at 377.867ms, input at 379.436ms, submit at 395.784ms. Source inspection
finds completion service unconditionally sets `render_pending`, even though
topology reconciliation and deferred-output retry already mark their own work.
This is a candidate scheduling defect to isolate with a production-code
regression; it is not yet a demonstrated fix or the sole cause of low cadence.

## 2026-10-19: cadence measurement corrected; refresh acceptance remains open

The operator pointer probe now uses the server observation interval for its
counter rates, separates submissions from confirmed native flip completions,
retains raw observations, and rejects restart/topology/refresh/clock-invalid
samples. Its regression fails against the previous probe and passes the
corrected measurement, including four invalid-sample cases. Timing suite:
3 pass, 2 explicit physical-host skips, 0 fail; the 120-second four-PTY soak
completes 11,876 snapshots and 1,080 resizes.

An exploratory 10-second API pointer sweep on each backend offers approximately
59 requests/s. Native KMS reports 38.72 confirmed flips/s; SDL/software reports
54.44 submissions/s; SDL/GLES2 reports 50.78 submissions/s. SDL completion
timing is unavailable. All motion/Kitty/font/sixel correctness checks still
pass; native/login/pointer are restored. Evidence:
`/tmp/blit-backend-cadence-557-20261019/`.

Native receives 601 pointer changes and reports 409 submissions/completions
with no physical mouse events or PTY bytes in its observation window. It
records 147 pointer-affecting deferred attempts. These counters do not yet
identify the cause or prove physical-input latency. The reference backend
is not an accepted refresh-performance baseline. Next work is scheduling
diagnosis with correlated input/deadline/submission/completion evidence,
followed by the sustained acceptance workloads. SDL dependency changes await
explicit direction; additional-connector tests await HDMI availability.

## 2026-10-19: split-aware sixel acceptance on all three presentation paths

The existing operator sixel probe now selects a discovered free pane, uses
its actual cell-grid origin and framebuffer metadata, and requires a
revision-correlated presented capture. Decoder-bound, palette/HLS, fill and
cursor-mode assertions are unchanged. The local wrapper occupies the upper
pane and checks pixels in the lower pane (origin 0,384), through both SDL
software and offscreen GLES2. No runtime or Hibr code changed in this cycle.

On live 0.1.557, direct KMS, SDL/KMSDRM software and SDL/KMSDRM OpenGLES2 all
pass the sixel probe, plus the previous motion, Kitty and grid/font checks.
Physical pixel origin is 0,4 on the free upper pane. Captures and metadata:
`/tmp/blit-backend-parity-557-sixel-20261019/`. The original native backend,
login policy and pointer pose are restored. Comparison failure cleanup also
passes locally. Broader renderer/GLES regression passes 63 gates with no
skips or failures, including six GLES cases. Cadence/latency,
multi-connector hotplug, physical-input parity and broader effects remain open.

## 2026-10-19: 0.1.557 physical graphics comparison passes all three backends

Package 0.1.557 is deployed and version-verified on 10.54. Its two independent
builds are byte-identical; identity is recorded in `releases/0.1.557.yaml`.
Actual direct KMS, SDL/KMSDRM software and SDL/KMSDRM OpenGLES2 each pass
12/12 revision-correlated motion captures, exact Kitty quadrant composition
and deletion, and 2,688 font/grid-edge pixels including a bold diamond.
The prior SDL/software colour-filtering failure is no longer reproduced.

Evidence: `/tmp/blit-backend-parity-557-20261019/`. Every profile retains
identity observations, motion/Kitty/grid captures and probe verdicts. The
original direct-KMS backend, login policy and pointer position are restored.
This validates these bounded cases on the currently connected DP canvas
(1920x2160, split 1x2), not the full parity matrix. HDMI hotplug, multiple
connectors, sustained refresh/latency, sixel and broader effects remain open.

## 2026-10-19 candidate0.1.557: physical comparison exposes graphics filtering mismatch

Reproducible0.1.556 is installed and version-verified on10.54. Its package
SHA256 is recorded in `releases/0.1.556.yaml`. API-owned backend selection and
restoration worked on the actual host. The first native trial included a
visible software pointer in the sampled cells; that failed fixture result is
not evidence of native rendering debris. The comparison now parks the pointer
outside the tested region and checks its state without masking pixels.

The second trial passed12/12 coherent motion captures on direct KMS and
SDL/KMSDRM software. Native Kitty composition/deletion passed. SDL/software
Kitty returned blended colours instead of exact source quadrants. KMS and
the original login policy were restored automatically. Evidence is retained
under `/tmp/blit-backend-parity-556-20261019-b/`.

Candidate0.1.557 sets terminal graphics texture sampling to nearest-neighbor,
matching native source-pixel lookup (ADR0139). The production-code regression
failed before the fix at software pixel2,0 (223,31,0 instead of255,0,0), then
passed all256 enlarged-image pixels across two renderers on software and GLES2.
The expanded GLES suite passes5/0/0. The three adapter gates also pass
separately against the pinned Hibr0.99.225/ABI16 archive, without editing
Hibr. Full regression passes 103 gates with 7 explicit skips and 0 failures;
the 120-second four-PTY soak completes 11,560 snapshots and 1,051 resizes.
The skips are the three adapter checks (passed separately with the pinned
headers), QEMU, package validation and two physical-host gates. Packaging,
deployment and physical revalidation of 557 remain pending.

## 2026-10-19 candidate0.1.556: API-owned backend comparison and shared GL fixtures

ADR0138 adds saved `server.display_backend` (`kms` or `sdl`) to the existing
settings resource. Saving preserves the active backend and terminal process;
effective settings report requested/active backend and presentation restart
requirement. The existing supervised restart applies the choice. Stored
settings override boot defaults; reset and `BLIT_SETTINGS=ignore` retain their
existing recovery semantics. A missing KMS device leaves the API available.

The focused backend gate was observed failing before implementation and now
passes deferred apply, durable save, eight invalid inputs, failed-save recovery,
precedence in both directions, renderer restart flag, reset and ignore recovery.
Schema/lifecycle suites pass17/0/0. The expanded GLES suite passes5/0/0:
glyph/half-block geometry, Kitty z bands/default-background transparency,
motion-trail pixel agreement, wallpaper damage/deletion, held-scene pointer
and independent two-renderer metrics. One shared SDL profile verifies identity
and selects the output; software and GLES use the same assertions.

The full regression run passes99 gates,7 explicit skips,0 failures (including
the two GLES gates present when that run began). Its120-second four-PTY soak
completed10,744 snapshots and977 resizes. The final expanded GLES suite
separately passes5/0/0; the new comparison-cleanup gate passes separately.
The [physical comparison tool](testing-backend-parity.md) restores the original
backend/login choices after either success or failure and retains captures.
Candidate0.1.556 is not yet packaged
or deployed. Live10.54 remains direct KMS0.1.553, with one DP canvas observed.
Physical comparison and HDMI reconnection acceptance remain pending.

## 2026-10-19 candidate0.1.555: SDL output diagnostics are independent

Parity implementation found SDL output diagnostics publishing global
presentation counts/timings for every output. Candidate0.1.555 stores those
values on each output, updates only the presented output, and resets them
when its resources are destroyed. Aggregate runtime metrics remain aggregate.
The existing OpenAPI fields now document their exact semantics. No SDL
present-return measurement is described as physical scanout latency.

A production-code fixture creates two independent SDL software windows and
renderers. Presenting only the first previously caused the idle second to
report one present: the new regression was observed failing before the fix.
It now verifies counts2/1 with aggregate3, independent timing, distinct60/120Hz
scheduling periods and teardown reset. These are local software and scheduler
results, not physical multi-monitor acceptance. Schema suite passes5/0/0;
the full suite passes96 gates with7 explicit skips and0 failures. Its120-second
four-PTY soak completed11,252 snapshots and1,023 resizes. Live10.54 remains on
the tagged0.1.553 checkpoint.

The shared wallpaper-damage fixture also passes with actual SDL
`video_driver=offscreen`, `renderer=opengles2` and software GL. This is now
repeatable as `tools/run-gates.sh gles`, an explicit suite requiring EGL/GLES.
That suite also passes the two-renderer counter/timing-isolation fixture with
both renderers verified as OpenGLES2. It proves these GL paths, not physical
KMSDRM scanout. The live read-only observation still reports direct KMS0.1.553
and currently one DP canvas at59.997Hz.

The existing held-scene pointer fixture now uses the selected renderer too;
its12 positions pass every-pixel comparison on both software and GLES2,
while preserving pending visual damage until the scene release.

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
