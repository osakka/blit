# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## 2026-10-17: authenticated 0.1.534 fails; UTF-8 read-loss regression

The user's authenticated desktop has an incomplete wallpaper. Captured
canonical cells below row 22 are blank; the screenshot reflects that missing
state. Prior synthetic renderer/cadence passes did not establish desktop
correctness. Evidence: `diagnostics/authenticated-20261017/blit-auth-*`.

The actual PTY service test fails on 0.1.534 with
`first=0 tail=0 pending=1 bytes=4097`: a split U+2580 followed by a 4096-byte
read is discarded by the parser's small prefix-join buffer. The correction
passes this regression and a large mixed ANSI/UTF-8 corpus at ten external
fragment sizes from 1 to 65535 bytes, comparing codepoints, colours,
attributes, combining state, cursor and completed OSC title to continuous
parsing. The same controlled shell/PTY reproduction on the real 0.1.534 host
also fails: zero half-blocks, zero of 4000 text cells, and missing END marker.
Its temporary lower-pane session was deleted; the existing desktop remains.
Broad suite, reproducible package and corrected-host acceptance pending.

## 2026-10-17: exact 0.1.534 physical acceptance boundary

Source `8c6a26f`; manifest `releases/0.1.534.yaml`; reproducible package SHA-256
`4dc79e0deffbe7d2cb727f5c3dcb1821cf521551a2f90378ccc224afc159a9de`.
The API updater verifies this version. KMS callback/clock, fractional-mode,
readiness, full-reference pixel and canonical surface/deadline regressions
pass. The latest broad renderer/terminal suite was 51/51 on the 0.1.532 cycle;
0.1.533/534 additionally received their focused timing/callback regressions.

The 0.1.534 nominal-rate comparison initially **failed** at 59.906 Hz, but its
new clock evidence records 1980 completed flips across exactly 1980 vblanks:
zero missed display refreshes. Nominal mode timing is 59.997136 Hz; actual DRM
clock is 59.908173 Hz. This distinguishes clock variation from lost frames.
Earlier nominal-rate failures remain recorded; they are not retroactively
reported as passing tests.

The final independent acceptance run uses actual sequence counts, rejects
even one skipped vblank, and passes:

```text
PTY_POINTER_SCANOUT completed=1980 vblanks=1980 skipped=0 observed_hz=59.908161 window_s=33.050589
PTY_POINTER_REFRESH_PASS completed=1980 vblanks=1980 skipped=0 observed_hz=59.908161
PTY_POINTER_PASS producer_frames=6000 pointer_requests=466 blank_pixels=2051904 stale_pixels=0 revision=5998
```

Evidence: `diagnostics/rendering-20261017/validated-soak-534/`, including final
BMP/state, runtime samples, active vblank samples and `scanout-verdict.json`.
Final live screenshot: `diagnostics/rendering-20261017/0.1.534-final.png`.
It was inspected and shows the existing login manager's username prompt and
clock in the upper pane, with the intentionally empty lower pane. Temporary
sessions were removed and focus restored. Backend, topology and chosen mode
were preserved. Hibr source was unchanged and no SSH was used.

This closes the bounded renderer/cadence reproductions, not the whole delivery
contract. Authenticated Hibr wallpaper/window dragging, joins and other
hardware/protocol cases still require their own acceptance evidence.

## 2026-10-17: candidate 0.1.534 DRM clock evidence

Exact 0.1.533 is deployed from `bc1a0e5`, package SHA-256
`e1b69b90242dd991a38c37d7f29babefa05df0b7fd5555b294aaf2a6d165ab24`,
reproducible artifact check passed. Active-mode metadata is 59.997 Hz with
unchanged resolution/selection. The repeated 6000-frame soak checks 2,051,904
blank pixels with zero stale pixels but **fails** its strict rate check:
59.902 submissions/s over 32.820 s, minimum 59.967. Mode rounding alone does
not explain this. Evidence: `diagnostics/rendering-20261017/refresh-soak-533/`.

Candidate 0.1.534 records the actual DRM sequence/time without altering
presentation. The callback fixture distinguishes nominal 59.997 Hz from an
observed 60-Hz fixture with three completions spanning 120 vblanks. uint32
wrap, KMS ownership and full-reference pixel tests pass. Live clock/submission
correlation remains pending.

## 2026-10-17: candidate 0.1.533 fractional KMS refresh

Exact 0.1.532 is deployed: package SHA-256
`11cccbf128f97158fc51d5b075b17c3a4d10a22b2c321f55bdab82afa541b82c`,
source `0fb9b0a`, reproducible artifact gate passed. The short strict live test
passes at 59.832 submissions/s in 3.209 s, with 600 producer frames, 193 pointer
requests and zero stale pixels over 2,051,904 blank pixels. Evidence:
`diagnostics/rendering-20261017/refresh-required-532/`.

The 6000-frame soak's pixel check passes, but its refresh assertion **fails**:
1991 submissions over 33.232 s = 59.912 Hz; minimum against reported 60.0 Hz is
59.970. Window missed-deadline counters remain zero. Evidence:
`diagnostics/rendering-20261017/refresh-soak-532/`. This is not called a pass.

The active/enumerated direct-KMS rates came from Linux's integer-rounded
mode.vrefresh. A real-helper fixture fails at 60.000000 versus 59.940202 before
correction. Candidate 0.1.533 calculates the timing fraction and scan factors;
focused arithmetic, ownership and pixel gates pass. Exact live mode timing
and the unchanged long acceptance test remain pending. The renderer behavior
and chosen mode are unchanged from 0.1.532.

## 2026-10-17: candidate 0.1.532 direct-KMS vblank retries

[Issue #91](https://git.home.arpa/itdlabs/blit/issues/91) records the real
pre-fix refresh failure: 54.123 submissions/s versus an initial 58.2-Hz
discriminator. The captured trace identifies deferred attempts that wait for
another software deadline after flip completion. Candidate 0.1.532 remembers
the actual deferred outputs and retries each when KMS reports its buffer ready.

The production KMS readiness mask excludes pending/failed outputs and becomes
ready through the actual flip callback. The state-helper test verifies that
only a ready deferred output is rescheduled immediately; a second still-busy
output retains its deadline, and a consumed retry cannot repeat. Focused
tests and the Hibr join barrier pass. The TCC renderer/terminal run passes
51/51 with no skips, and API schema/observation checks pass. Deployment remains
pending. Direct-KMS FPS now excludes deferred/no-op attempts and the existing
global present_calls field counts KMS submissions. Their semantics are explicit
in OpenAPI and tested against the actual submission-counter helper.

The final live refresh assertion permits only one-event quantization over the
measured interval (`refresh_hz - 1/window_seconds`), rather than accepting an
arbitrary percentage of lost frames.

## 2026-10-17: candidate 0.1.531 bounded presentation deadlines

[Issue #90](https://git.home.arpa/itdlabs/blit/issues/90) is reproduced with
actual functions: an expired quiet deadline `1` is rearmed by a PTY read;
an active eight-millisecond quiet window returns a zero wait; and an expired
primary/join group barrier is rearmed by another member update. All three
regressions fail before correction and pass afterward. The direct-KMS batch
flag is now cleared at the same consumption boundary as SDL.

The focused Hibr join-barrier gate passes. The local dummy-backend frame-rate
fixture records 59.999 Hz with zero window missed deadlines. The TCC
renderer/terminal suites also pass: 51 pass, 0 skip, 0 fail. Deployment was
pending at that source-verification stage; physical follow-up is below.

### Physical follow-up: exact 0.1.531 deployed

The API updater verifies 0.1.531, built from `0933cad`, SHA-256
`9d4b891c7340f20e7aaf0b94104aae51175a967106bca04c4bbae48094d5d54c`.
Its manifest is `releases/0.1.531.yaml`; artifact verification reports
`reproducible=yes`. The two-corner and half-block RGB probes pass again.
Two whole-pane PTY/pointer runs each check 2,051,904 blank pixels with zero
stale pixels. Cadence is 53.185 and 52.581 submissions/s in active windows.
The captured render ring contains 14 deferred attempts among 82 entries;
examples at 608506458026 and 608506490916 us retry only at 608506477782 and
608506510188 us. A completed flip still waits for the software phase, making
some consecutive submissions approximately 32 ms apart. This is the next
timing defect; 60-Hz physical acceptance remains unmet. Evidence is under
`diagnostics/rendering-20261017/pty-pointer-low-overhead-531-repeat/`.

The improved live PTY/pointer benchmark on 0.1.530 polls small title metadata
while drawing, then obtains the large cell snapshot only after the producer
finishes. This avoids contaminating cadence with repeated multi-megabyte JSON
serialization. Its active window records 47.908 submissions/s over 3.131 s,
600 producer frames, 186 pointer requests, and zero stale pixels. Evidence:
`diagnostics/rendering-20261017/pty-pointer-low-overhead-530/`.
The live half-block probe also confirms exact upper/lower red/navy halves.

## 2026-10-17: candidate 0.1.530 damage accounting correction

Live 0.1.529 remains visually broken. [Issue #89](https://git.home.arpa/itdlabs/blit/issues/89)
records two deterministic root defects reproduced against actual Blit code:
left/up damage expansion discards old bounds, and simultaneous KMS pointer
movement replaces terminal damage before consuming its revision. Partial
scroll/DEC rectangle operations can also discard earlier pending work.

Before correction, `surface_damage_gate.sh` reports five failures, including
`got=2,1,1x1 want=2,1,59x30`. The production-presenter regression reports
`KMS_POINTER_TERMINAL_FAIL old=ff0000 new=102030 expected=102030,ff0000`.
After correction both pass, and 128 mixed half-block/graphics/pointer frames
match a fresh full render at every pixel. The test simulates only the DRM page
flip; it executes the actual scene, pointer, damage, and buffer-swap code.

The TCC-built 0.1.530 binary passes the complete renderer/terminal suites:
`RUN_GATES_PASS pass=51 skip=0 fail=0`. Package and live acceptance are pending.
The identical defect was also reproduced on the real 0.1.529 host with
`tools/probes/host_damage_union.rb`: one API request writes bottom-right first
and top-left last; the screenshot reports the new revision but returns
`pixels=[[255,0,0],[16,32,48]] expected=red,red`. The temporary shell surface
is created in the free pane and deleted by the probe. The exact same probe
must pass after deployment before this defect is closed.
### 0.1.530 exact package and physical-host follow-up

Source `590ea31` and manifest `releases/0.1.530.yaml` identify the TCC-built
package. Artifact verification passes with `reproducible=yes`; SHA-256 is
`0f62c457dc0cadf78731c51271a0023353668c0d0dc3696384a849b10f811071`.
The API updater confirms live `server_version=0.1.530` on 192.168.10.54.

- Identical two-corner probe: `LIVE_DAMAGE_UNION_PASS revision=3
  display=display-93 both-corners=exact` (the pre-update run failed).
- Existing motion probe: 12/12 accepted steps, no discarded captures.
- Kitty probe: exact RGBy quadrants, untouched background, and deletion.
- New `host_pty_pointer_stress.rb`: 600 genuine PTY animation frames,
  106 concurrent pointer API requests, 100,352 checked blank pixels,
  zero stale pixels. Its final state and BMP revisions agree.
- Existing live Hibr saver state/pixel probe: three samples, zero stale cells,
  zero missing cells, with continuing state churn (520, 1164, 736 cells).

The stress evidence is under the ignored local directory
`diagnostics/rendering-20261017/pty-pointer/`; screenshots before and after
deployment are in `diagnostics/rendering-20261017/`. Temporary sessions were
deleted; keyboard focus and pointer position were restored. Backend and pane
topology were unchanged. Hibr source was not changed.

The bounded stress capture measured 36.57 page-flip submissions per second over
3.391 seconds including the producer's initial one-second wait. That mixed
interval does **not** establish 60-Hz motion. Authenticated Hibr dragging,
steady producer timing, and complete protocol conformance remain separate
acceptance items. The login manager is the only remaining Blit session.

Historical entries below describe their own versions and do not override
this current failure status.

## 0.1.524 live API-owned cell and Kitty composition boundary

### Source correction pending live deployment: direct-KMS cursor damage

The source comparison against Kitty, xterm, and suckless `st` found a concrete
direct-KMS damage violation. Blit's SDL path already restricted terminal-cursor
drawing to damaged rows; direct KMS drew the cursor unconditionally after the
partial cell repaint. The new `tools/tests/kms_compose_gate.sh` regression
reproduced the defect by repainting `(0,0)` and observing an unrelated cursor
cell change. The correction bounds the cursor to the damaged row and its
one/two-cell damaged-column footprint; see [ADR 0112](adr/0112-kms-partial-repaint-must-bound-the-terminal-cursor.md).

The correction is locally verified with `KMS_COMPOSE_PASS` and the complete
TCC renderer/terminal run `RUN_GATES_PASS pass=50 skip=0 fail=0`. It has not
yet been packaged or deployed to `192.168.10.54`, so no live visual claim is
made for it.

On 2026-10-17, the authenticated direct-KMS host at `192.168.10.54` reported
one 1920x2160@60 Hz output and the persisted 1x2 topology. The corrected
`tools/probes/host_motion_agreement.rb` updated a known block twelve times in
the free lower pane and passed:
`MOTION_PASS steps=12 accepted=12 discarded=0 direct_kms=true
display=display-93`. The probe now uses the topology's display-global cell
origin and reads the BMP bit depth from the capture header; the earlier
lower-pane result made before those corrections is not evidence.

`tools/probes/host_kitty_composition.rb` then created a separate API-owned
surface, painted a known `#102030` cell background, emitted a 2x2 RGB Kitty
image from the child PTY, and captured the direct-KMS framebuffer. All four
image quadrants matched exact RGB values and an untouched cell matched the
known background. A Kitty `a=d,d=I` delete was then observed and the former
image location returned exactly to the background:
`KITTY_COMPOSITION_PASS display=display-93 image=9001 direct_kms=true
pixels=exact delete=exact`.

The probes cleaned up their own sessions; the host was left with only the
supervised login-manager session. This is isolated renderer/protocol evidence,
not acceptance of Hibr's complete desktop, wallpaper, joins, physical mouse,
physical keyboard, or window interaction. The live runtime still reports a
large cumulative missed-deadline count from the long-lived login-manager and
diagnostic workload; no 60-Hz physical-performance claim is made here.

The subsequent read-only graphics discriminator found the same boundary:
`session-1` is `login_manager=true` with `graphics=[]`, and the filtered trace
contains only its Kitty capability query. No authenticated Hibr desktop
wallpaper payload is present in this live state. The next required capture is
therefore one authenticated primary desktop, followed by its graphics state
and screenshot, before any join is started.

## 0.1.523 live direct-KMS surface-local damage and cadence

The exact `0.1.523` package is deployed on `192.168.10.54`. Hibr source was
not modified. The live API reports `drm-kms`, `direct_kms=true`,
`direct-kms`, `drm-page-flip`, and one 1920x2160@60 Hz output. The persisted
1x2 test topology places the supervised login manager in the lower pane and
leaves the upper pane empty.

The compositor changed from full-output rebuilding on every changed terminal
view to surface-local damage in [ADR 0108](adr/0108-surface-local-kms-damage-boundary.md).
Startup, view-count, origin, and output-wide changes still force a complete
output rebuild. The KMS composition gate passes, including a moved graphics
placement with no pixels left at its old position.

The controlled live fast-terminal workload measured 61.957–63.137 Hz,
14.085–16.892 ms last frame gaps, and 10.903 ms maximum KMS present time.
Its measurement windows added zero missed deadlines. The lifetime diagnostic
counter contains startup/login-relocation gaps and is not used as motion-pass
evidence. Direct-KMS frame-rate counters now update through the same path as
SDL, so `0 Hz` is no longer reported merely because the active backend is KMS.

This is bounded renderer evidence. It does not claim a visually accepted Hibr
desktop, physical mouse/keyboard acceptance, wallpaper/Kitty/Sixel acceptance,
or a working join.

## 0.1.517 candidate: atomic KMS scene rebuild, login-pane invariant, and screenshot evidence

The source candidate targets the measured direct-KMS workload in which Hibr's
opaque 960x1080 wallpaper is placed at 1920x2160. The compositor now uses a
bounded integer-scale replication path for opaque RGB/RGBA images and retains
the general scaler for alpha or non-integer placements. This avoids the
per-destination-pixel source-coordinate divisions that were present during
full wallpaper redraws. It also makes the session returned by `/v1/login`
explicitly inherit the requested pane before topology derives the surface
assignment, closing the live mismatch in which the lower-pane request
returned a session on the upper pane. Direct-KMS composition now renders into
a persistent CPU scene before the sequential scanout copy; see [ADR 0102](adr/0102-kms-integer-scale-fast-path.md)
and [ADR 0103](adr/0103-kms-cpu-scene-cache.md). An already-running
auto-started login manager is relocated when `/v1/login` explicitly names a
different pane, and an origin change forces a complete KMS redraw; see [ADR 0104](adr/0104-login-target-relocation-and-pane-redraw.md).
The KMS screenshot endpoint now requests one bounded present for the requested
output and captures after the persistent scene is copied to the page-flip
buffer, so a capture cannot report the previous scanout frame; see [ADR 0105](adr/0105-kms-screenshot-capture-boundary.md). A changed unsynchronized terminal view now rebuilds the complete persistent KMS scene before the page flip, preventing stale pixels from the incremental path.

Local evidence: `tools/tests/kms_compose_gate.sh` passes with
`KMS_COMPOSE_PASS full=512`; the TCC server build reports `0.1.517`. The
package gate passes for `build/packages-517/blit_0.1.517-1_amd64.deb`, whose
SHA-256 is recorded in [`releases/0.1.517.yaml`](../releases/0.1.517.yaml).

Live evidence on `192.168.10.54` is complete for this renderer boundary:

- `/v1/version` reports `0.1.517`; `/v1/server` reports ready direct
  `drm-kms`.
- The API-only handoff and `/v1/login` request place one login session in
  `pane-display-93-2`; `pane-display-93-1` has no surface.
- A lower-pane shell moved a coloured label through 120 rapid terminal
  updates, cleared the grid, and wrote `done`. The final surface had six
  non-empty cells and the API screenshot showed only `done`; the screenshot
  contained no earlier moving label. The artifact is
  `/tmp/blit-kms-0.1.517/motion-screen.png`.
- The screenshot-free timing sample measured 131 frames over 5121 ms, a
  4.533 ms maximum KMS present, and a 13.316 ms maximum rolling loop gap.
  These are compositor measurements for the controlled workload, not a claim
  of physical-input acceptance or sustained 131 FPS.
- The restored Hibr login capture is
  `/tmp/blit-kms-0.1.517/hibr-login-screen.png`; it shows a clean lower login
  pane and a black upper pane.

The required live configuration remains direct KMS, one physical output, one
lower-half pane, and one primary Hibr session; no join is part of this
performance check. Physical mouse/keyboard acceptance and Hibr desktop
interaction remain separate acceptance evidence.

## 0.1.510: direct-KMS frame-boundary fix and live renderer matrix

The exact `0.1.510` package is deployed on `192.168.10.54`. Hibr source was
not modified. The direct KMS compositor now copies the complete displayed
front buffer into the acquired back buffer before applying damage; see
[ADR 0101](adr/0101-kms-incremental-frames-copy-the-presented-buffer.md).

The local coherence gate and TCC build passed. The live renderer matrix was
then exercised through the authenticated API:

| Path | Runtime identity | Focused probe | Result |
| --- | --- | --- | --- |
| SDL/OpenGL | `sdl3`, `kmsdrm`, `opengles2`, `sdl-vsync` | cell updates, pointer motion, Hibr primary/join, Kitty image movement | no missed deadline in focused probes |
| SDL/software | `sdl3`, `kmsdrm`, `software`, `scheduler-only` | cell updates and pointer motion | one startup scheduler miss; no subsequent focused miss |
| direct KMS | `drm-kms`, `direct-kms`, `drm-page-flip` | full-frame coherence gate and pointer motion | no missed deadline in focused probe |

The live SDL/OpenGL run accepted one Hibr primary in the lower pane and one
Hibr `--join` in the upper pane as distinct running surfaces. The Kitty image
probe recorded a complete 32x32 image, one texture upload, cache reuse, and
zero rejected graphics payloads. The final live observation was restored to
SDL/KMSDRM + OpenGLES2 at 59.99 Hz with the same primary/join topology.

An API-driven context-menu probe then moved the pointer to `(500,1500)` and
sent a right-button press/release. The trace identified `target_session=2`
through pane hit-testing, and the display capture showed the menu at that
location. After dismissal, the `surface-2` cell snapshot returned to the
expected desktop background cells. This proves the Blit API dispatch and
surface/screenshot path; it is not a physical evdev click acceptance test.

Evidence captures and JSON observations are retained outside Git under:

- `/tmp/blit-direct-kms-fixed-motion-20261016/`
- `/tmp/blit-sdl-gl-motion-20261016/`
- `/tmp/blit-live-hibr-join-sdl-gl-20261016/`
- `/tmp/blit-live-kitty-motion-20261016/`
- `/tmp/blit-final-live-20261016/`

This is bounded renderer/composition evidence, not a claim of complete
physical acceptance. Physical fast window dragging, physical button/key
traces, context-menu placement, and end-to-end Hibr interaction remain open
acceptance boundaries.

## 0.1.509: canonical observation API and live presentation-path matrix

The exact `0.1.509` package is deployed on `192.168.10.54`. The authenticated
observation endpoint was used to run each presentation path and record the
identity fields needed to distinguish them:

| User-facing path | Backend | Video driver | Renderer | Presentation sync | Ready | Output |
| --- | --- | --- | --- | --- | --- | --- |
| SDL/OpenGL | `sdl3` | `kmsdrm` | `opengles2` | `sdl-vsync` | yes | `DP-1`, 1920x2160@60 Hz |
| SDL/software | `sdl3` | `kmsdrm` | `software` | `scheduler-only` | yes | `DP-1`, 1920x2160@59.99 Hz |
| direct KMS | `drm-kms` | `direct-kms` | none | `drm-page-flip` | yes | `KMS connector 93`, 1920x2160@60 Hz |

Each observation reported an empty health error and empty diagnostic warning
list. “SDL/KMS” is not a third top-level backend here: it is the SDL3 video
path using the KMSDRM video driver. The server was restored after the matrix
to SDL/KMSDRM + OpenGL ES2 with the login manager running. This is exact
backend/readiness evidence, not visual acceptance; the user’s login is the
next physical desktop observation.

### Manual physical comparison, 2026-10-16

The three modes were then presented one at a time for user login and direct
visual comparison. In SDL/KMSDRM + OpenGLES2, the static picture-rendered
background was reported correct, but movement of approximately one block per
second caused rendering to break. SDL/KMSDRM + software produced the same
reported result. Direct KMS displayed the background image and desktop
content, but left more persistent artefacts during movement, including at very
slow movement. These are physical visual observations; they do not by
themselves identify the failing composition boundary or replace screenshot,
damage, and input traces.

## 0.1.508: canonical observation API

Source now defines the schema-first `GET /v1/diagnostics/observation` endpoint
and composes it from the existing focused resource serializers. The OpenAPI
schema gate, TCC build, focused observation gate, and complete schema/API gate
pass locally. The exact package was installed through the supervised update
path and the authenticated Bash client confirmed `0.1.508` on
`192.168.10.54`. The live response reported SDL3/OpenGLES2, one
1920x2160@60 Hz output, one running login-manager session, 158 presentation
calls, zero missed deadlines at capture, and one graphics payload.

## Latest review: 0.1.507 is not visually accepted

The 0.1.507 package is deployed on `192.168.10.54` and `/v1/version` reports
`server_version=0.1.507`, `api_version=0.1`. The live server can create one
primary Hibr session and one `--join` session through the API, but the latest
fresh-session screenshot showed a flat-colour scene with the menu/status line
and no Kitty graphics in either session. The sessions were alive and had
distinct surface revisions; that is process/surface evidence, not visual
correctness.

A preceding live capture showed the more specific stale-resource failure:
the retained primary surface contained `[desktop ended, status 0]`, while a
new join reported `hibr: hold: no such session`. Removing the exited resources,
starting a fresh primary without `--resume`, waiting for its desktop surface,
and then creating the join produced running sessions. This is now the
documented recovery sequence; it is not an acceptance result.

The controlled API pointer workload delivered 600 synthetic motion events and
produced 531 presentations with zero new missed deadlines in that interval.
The local pointer-damage, motion-trail, wallpaper-stress, and Hibr
frame-barrier gates passed. These facts narrow the timing path but do not prove
that Hibr emitted the expected scene or that a real window can be dragged.

The authenticated 20-second physical capture recorded zero evdev motion and
zero evdev button events. Physical window movement therefore remains
unmeasured, not passed. The next acceptance transaction must include a visible
window, physical evdev traces, Hibr surface revisions, render-path traces, and
a screenshot. See [current-state.md](current-state.md).

The claim vocabulary is strict: timing stability is only timing stability;
process liveness is only process liveness; an incomplete screenshot is a
failed visual result. No broader claim may be inferred from these observations.

## Unreleased: rendering contract fixes (#84, #85, #87) -- validated, not host-accepted

Three server defects behind the Sixel/half-block/Kitty inconsistency are fixed
in source: Kitty z bands and default-background transparency in the SDL path
(#84), sixel slots never released so the ninth picture was refused (#85), and
a DA1 reply that did not advertise sixel (#87). New gates
`sdl_graphics_band_gate.rb` and `sixel_slot_gate.rb` fail on the 0.1.507
binary (band gate: 0 of 512 wallpaper pixels visible; slot gate: 5 pictures
rejected with `image-store-full`) and pass on the fix; the renderer and
terminal suites pass (49 gates). Validated on the dummy SDL backend, and for
direct KMS by `kms_compose_gate` on a memory buffer (it fails on the 0.1.507
compositor at the explicit-black case).
Not deployed, not host-accepted. The live half-block fallback on the host is
not yet explained by these defects and needs a Hibr debug log from a restored
desktop.

## 0.1.507 candidate: ping-pong SDL scene composition

The real 0.1.506 host reproduced the reported fast-update symptom through a
controlled API workload: approximately 55 pointer updates per second produced
about 28 presentations per second and 16 additional missed 60 Hz deadlines
in five seconds. The OpenGLES2 path had disabled incremental scene damage
because a single render-target texture did not preserve its contents when
rebound, so changed Hibr surfaces rebuilt both panes from scratch.

0.1.507 keeps two SDL scene render targets. Each content update copies the
last complete scene into the work texture, applies the canonical damage, and
swaps the textures only before presentation. This removes the implicit
render-target persistence assumption without changing Hibr or the terminal
protocol boundary. The TCC renderer/Hibr suites passed 54 gates, the schema
and package gates passed, and the exact package was deployed to the live host.

The same five-second high-rate control workload after deployment produced no
new missed deadlines, a maximum present time of 16.882 ms at a 60 Hz output,
and a maximum loop gap of 17.026 ms. Three one-second idle screenshots had
the identical SHA-256 (`f240696d9fe8b5b58c8515564220806b68875566a3c7e68495ebba6bf06b9390`)
and one live two-pane screenshot showed the Kitty wallpaper on both panes.
That was a bounded historical composition observation, not current visual
acceptance. It did not prove a complete Hibr desktop, physical fast-window
dragging, or a persistent join. The later live review above supersedes any
broader interpretation of that screenshot.

## 0.1.505 candidate: reassert the Hibr graphics contract after login

The live surface showed half-block wallpaper and literal truecolor control
text while Blit's Kitty trace contained only Hibr's capability query and a
delete. Source inspection of Hibr's unchanged authentication boundary proved
that `auth run` clears the environment before launching the selected desktop;
the desktop therefore never received Blit's `HIBR_GFX=kitty` default. Blit's
own `blit-session.hibr` now reasserts `HIBR_GFX=kitty`, `DT_BIDI=on`, and
`COLORTERM=truecolor` after that boundary, preserving explicit overrides.

The local terminal and package gates must pass, then the live host must be
restarted from the 0.1.505 package. Acceptance is not complete until the
authenticated live trace records a complete Kitty upload and texture upload;
the old half-block screenshot is retained as the pre-fix evidence.

## 0.1.504 candidate: preserve complete Hibr Kitty PNG chunks

Live graphics traces identified a concrete transport defect: Hibr's Kitty
wallpaper chunks carry 4096 base64 bytes, while Blit's terminal APC buffer was
4096 bytes total. The control header consumed part of that capacity, so every
chunk was truncated to 4095 bytes and the final PNG transmission was rejected
as incomplete. The parser buffer is now a bounded 16 KiB buffer; graphics and
decoded-image limits remain unchanged.

The focused TCC graphics gate now emits the same wire shape for both raw RGB
and a real multi-chunk `f=100` RGBA PNG. It verifies complete Kitty images,
exact 64x64 dimensions, 64x64x4 decoded bytes, full trace chunk lengths, and
zero new rejections. The renderer gate passes 43/43 with no skips. This is
local evidence only until the package is deployed and the live Hibr wallpaper
trace and screenshot show a complete Kitty image.

## 0.1.503 candidate: preserve Hibr session-relative imports

The live login attempts provided the missing evidence: keyboard input was
delivered, authentication completed, and both desktop children exited with
status 0. A screenshot of a controlled API-created desktop showed Hibr
looking for `/usr/share/blit/hibr/desktop.hibr` and then reporting its desktop
functions missing. The cause was Blit sourcing Hibr's installed
`session.hibr` through the wrapper, which changed Hibr's `$0` path. Blit now
executes the installed session as the script entrypoint for the native
terminal-provider path; the API-provider path remains separate. Hibr source
and installation are not modified. The candidate must be rebuilt, gated,
deployed, and retested through login.

## 0.1.502 candidate: use Hibr ABI as the compatibility contract

Hibr `--version` reports both its release and its module ABI, and Hibr's
module documentation defines `HIBR_ABI` as the load-time compatibility
boundary. Blit now packages that ABI in `HIBR_ABI`, validates it when building
the TCC adapter, reports both pinned and runtime identities through
`/v1/diagnostics/runtime`, and decides compatibility from ABI equality. Exact
release values remain provenance. This allows a host to advance from Hibr
0.99.212 to 0.99.213 while ABI 16 remains stable, while still failing closed
on a future ABI change. Hibr source and installation are not modified.

This candidate also renames the mismatch event to `hibr_abi_mismatch` and
retains the structured `login_session_exit` event from 0.1.501. The package,
schema, focused gates, and live deployment must be completed before the
authenticated desktop login is retested.

The TCC build, schema/network/login focused gates, package artifact gate, and
reproducibility check pass. The complete local run recorded 75 passes, three
explicit environment skips, and one transient frame-budget sample at 20.756
ms against the 20 ms limit. Three immediate frame-rate retries and the timing
suite passed without changing the threshold. This is recorded as evidence,
not converted into a false full-suite pass.

## 0.1.501 candidate: retain login child exit evidence

The live Hibr login loop exposed an observability gap: Blit retired the
authenticated login child before `/v1/diagnostics/runtime` could identify why
it exited. This candidate records one bounded `login_session_exit` diagnostic
event with the session ID, state, and exit code before the login manager
restarts. It changes no Hibr code or login behavior; it makes the existing
handoff falsifiable through the API.

## 0.1.500 candidate: match the installed Hibr ABI

The 0.1.499 package was built and gated against Hibr 0.99.204 after the live
host reported that version. Before deployment, the host advanced to Hibr
0.99.212. After deployment, Blit's diagnostics correctly reported
`hibr_compatibility.compatible=false`; 0.1.499 is rejected as a physical-host
artifact. No Hibr source or installed file was modified by Blit.

The current candidate follows ADR 0093 and pins Hibr 0.99.212, using the
released tag from the local Hibr checkout. It must be rebuilt, gated, and
deployed before any physical desktop behavior is accepted.

The exact 0.1.500 package passed the full local suite (`76 passed, 3
explicitly skipped, 0 failed`), was deployed through the supervised update
API, and reports `hibr_compatibility.compatible=true` on the physical host.
The host is currently at Blit's Hibr login manager with no registered Hibr
desktop owner, so menu, input, and join behavior are not yet physical-host
accepted for this candidate. No second desktop was started for this check.

## 0.1.499 candidate: match the installed Hibr ABI

The first direct-installed-tree package was deployed to the live host as
0.1.498. Its post-deployment diagnostics exposed a second, independent
boundary issue: the package was built against Hibr 0.99.191 while the host
was running Hibr 0.99.204. The server correctly reported this as
`hibr_compatibility.compatible=false`; the package is not accepted as the
final host artifact.

The packaging pin was 0.99.204, selected from the Hibr release tag that
matched the host at that point. The adapter was compiled from the exact pinned
checkout, and the launcher sourced `/usr/share/hibr/desktop/session.hibr`
directly. No Hibr source, installed file, or Hibr repository state was changed.

The 0.1.498 deployment remains useful evidence: the update path, direct
installed-session boundary, and diagnostic mismatch reporting all worked. The
0.1.499 package is retained as rejected compatibility evidence; 0.1.500 is the
candidate for the current host.

## 0.1.498 candidate: use Hibr's installed desktop tree

The live menu investigation found that Blit's packaged integration tree
omitted Hibr's `system/` sibling. The packaged launcher therefore registered
ordinary applications but not Hibr's own Control Panel, Applications
Catalogue, Modules, or Task Manager entries. This was a Blit packaging/path
defect; Hibr source, modules, and desktop files were not modified.

The launcher now sources Hibr's installed
`/usr/share/hibr/desktop/session.hibr` directly. Blit no longer creates a
second symlink tree beside Hibr's installation. QEMU retains an explicit
`BLIT_HIBR_DESKTOP_SESSION` override for its complete staged fixture. The
package regression gate asserts both the direct installed-session path and the
absence of the old mirrored session/system paths.

Implementation commit: `c514f50` (`source Hibr desktop from its installed
tree`). The TCC-built 0.1.498 package passed the packaging regression and
reproducibility gates against the exact pinned Hibr 0.99.191 checkout:

```text
PACKAGE_ARTIFACT_PASS version=0.1.498-1 reproducible=yes
RELEASE_MANIFEST_PASS package=0.1.498-1 manifest=releases/0.1.498.yaml
```

## 0.1.497 candidate: authenticated diagnostic bundles

The live troubleshooting path now uses one shared bearer-aware transport for
all diagnostic tools. The canonical collector records capability/settings
documents, capture metadata/completion state, filtered wheel and graphics
traces, and screenshot response headers containing frame and surface-revision
identity. `validate-capture.py` rejects incomplete or malformed bundles, and
the local bearer-file regression gate passes. Hibr source was not modified.

The 0.1.497 package is built after this source commit; the exact artifact and
reproducibility result will be recorded in `releases/0.1.497.yaml`.

The strengthened collector was exercised read-only against `192.168.10.54`
with bearer authentication. It discovered one 1920×2160 OpenGLES2 output at
60 Hz, one session and one surface, and produced a validated screenshot whose
headers identified frame `443107` and surface revision `surface-1=561712`.
The two-second timing window observed no active input or surface work, so it
does not diagnose the visual artifact; it proves the evidence path is now
usable and makes the next reproduction falsifiable.

## 0.1.496 candidate: generated release identity

The release tooling slice of issue #27 now derives the package identity from
the Debian artifact rather than trusting hand-copied hashes. The focused gate
passed against the 0.1.495 package and existing release record, and a
deliberately stale server hash was rejected. The package artifact gate now
invokes the same check. The adapter build-method field remains explicit
evidence because an archive cannot prove which external checkout or compiler
produced its embedded adapter. Hibr source was not modified.

Focused evidence:

```text
RELEASE_MANIFEST_PASS package=0.1.495-1 manifest=releases/0.1.495.yaml
RELEASE_MANIFEST_GENERATOR_PASS fields=9
NEGATIVE_DRIFT_PASS
PACKAGE_ARTIFACT_PASS version=0.1.495-1 sha256=de362664dba1949f0952d1d7906a1a16dfd9e61f17a4014f99a8399e55703333 reproducible=skipped
```

The 0.1.496 package is built after this source commit; CI and automatic
live-update rollback/restore remain open portions of issue #27.

## 0.1.495 candidate: full-frame composition budget

0.1.495 adds the executable evidence boundary for Blit issue #80. The timing
gate accepts authenticated remote endpoints and configurable surface
dimensions, so it can exercise a full 240×135 terminal surface instead of a
small smoke workload. The release host threshold is 17 ms at 60 Hz; the gate
also requires at least 55 Hz and zero missed deadlines.

Physical evidence on `192.168.10.54` (SDL3/OpenGLES2, 1920×2160 at 60 Hz):

```text
FRAME_RATE_PASS hz=59.916 min_hz=55.000 max_hz=75.000 frames=30 window_ms=500 age_ms=0 window_max_render_ms=16.789 render_budget_ms=17.000 target_interval_ms=16.000 window_missed_deadlines=0
```

The same workload failed with a 14 ms threshold at 16.848 ms, proving the
budget assertion is not decorative. No scene-cache change is claimed; the
OpenGLES2 cache remains disabled under ADR 0070.

## 0.1.494 candidate: API-owned server presentation settings

0.1.494 addresses Blit issue #70. The settings document now persists render
cadence, pane layout, requested renderer, direct-KMS async pageflip policy,
empty-output expectation, and the selected font profile/scale. The environment
is used only as the boot default when the document is silent. `/v1/settings`
reports the requested and active renderer separately; `/v1/topology` remains
the live owner of pane layout and persists that choice through the document.
`BLIT_EVDEV_MOUSE` is explicitly retired from the API because stable input
device resources supersede a node-path override. Hibr source remains
unchanged.

Focused evidence so far:

```text
SETTINGS_PERSISTENCE_PASS
font API gate: PASS (profiles, tenth-step scales 1.0..3.0, rounded pixels, reflow, rejection, restore)
PANE_SPLIT_GEOMETRY_PASS output_grid=78x29 pane_grid=78x14 top_origin=5,10 bottom_origin=5,394
API_SCHEMA_PASS paths=41 terminal_input=/v1/terminal-sessions/{session_id}/input
```

The reproducible package, full renderer/terminal gates, and physical-host
acceptance are still pending for this candidate.

## 0.1.492 candidate: opt-in deferred middle-button emulation

0.1.492 addresses Blit issue #71. Pointer devices now expose an implemented,
disabled-by-default `middle_button_emulation` setting with a bounded 50..500
ms window. When enabled, a left/right chord emits one middle press and
release; a single press is flushed as itself when its deadline expires, even
when the event loop is otherwise idle. Motion is not swallowed, state is per
device, and API injection with `device_id` uses the same path as evdev input.
Hibr source remains unchanged.

Focused evidence so far:

```text
API_SCHEMA_PASS paths=41 terminal_input=/v1/terminal-sessions/{session_id}/input
INPUT_DEVICE_CONFIG_PASS ...
POINTER_SETTINGS_PASS
```

The exact pinned-Hibr package is reproducible and the full local gates pass:

```text
RUN_GATES_PASS pass=43 skip=0 fail=0
RUN_GATES_PASS pass=3 skip=0 fail=0
PACKAGE_ARTIFACT_PASS version=0.1.492-1 sha256=142b6994b6f9170ad99a1958968e6b1f3771f664bfe30e5ead2851d6aaa0f291 reproducible=yes
```

Physical-host deployment and acceptance remain pending for this candidate.

## 0.1.491 candidate: graphics follow terminal-row scrolling

0.1.491 addresses Blit issue #62. Kitty and sixel placements now carry an
explicit page-anchor state and move with later index, reverse-index,
scroll-region, and insert/delete-line operations. Destination and source
rectangles clip together at the active page boundary. API-owned pixel frames
remain absolute scene objects. Hibr source is unchanged.

Focused gates pass:

```text
SIXEL_DECODER_PASS
KITTY_PLACEMENT_PASS
```

The full renderer gate and reproducible package evidence are pending for this
candidate.

## 0.1.490 candidate: pinned Hibr adapter provenance and runtime compatibility

0.1.490 closes the release-integrity part of Blit issue #81. The Debian
builder now requires `BLIT_HIBR_ROOT` and compiles `console-blit.so` into the
package staging tree with TCC; it cannot copy an arbitrary adapter from the
ignored build directory. The server binary embeds the same
`packaging/HIBR_VERSION` value and `/v1/diagnostics/runtime` reports the
packaged pin, the version returned by the configured Hibr command at startup,
the probe state, and compatibility. A mismatch is also a structured runtime
log event. Hibr source remains untouched.

The exact pinned fixture is Hibr `0.99.191` from `/tmp/hibr-0.99.191`. The
negative package gate rejects an omitted checkout, and the positive package
build compiles the adapter from that checkout. The package artifact gate also
rebuilds the package byte-identically. The TCC/API/renderer evidence is:

```text
API_SCHEMA_PASS paths=41 terminal_input=/v1/terminal-sessions/{session_id}/input
NETWORK_AUTH_PASS ... runtime_diagnostics=200 ...
RUN_GATES_PASS pass=43 skip=0 fail=0
PACKAGE_ARTIFACT_PASS version=0.1.490-1 reproducible=yes
```

This candidate has not yet been deployed to the physical host. The live
acceptance must report the host's actual Hibr version through the new API
field before issue #81 can be closed.

## 0.1.489 candidate: rotation rebuilds pane geometry

0.1.489 closes Blit issue #73. A display rotation changes the logical pixel
rectangle and therefore the complete-cell pane grid. `apply_display_rotation`
now recomputes panes in the same accepted operation; live surfaces are still
refused with the existing resize-protocol conflict because their owner must
repaint the new geometry.

The display settings gate now asserts both directions: the dummy 1024×768
output reports a 96×64 pane after 90° rotation and returns to a 128×48 pane
after rotation back to 0. The full gate result is:

```text
DISPLAY_SETTINGS_PASS capabilities=derived rotation=presented refusal=in-use persistence=connector-name brightness=seam
```

The 0.1.489 package is installed on `192.168.10.54`; `/v1/version` reports
`0.1.489`, and the live topology reports matching `1920×2160` display and
`240×135` pane bounds. The live rotation write was deliberately not forced:
the login surface is attached and the API correctly returns the existing
`display_rotation_in_use` refusal. Hibr source is unchanged.

## 0.1.488 candidate: Hibr-compatible Kitty graphics deletion and KMS invalidation

0.1.488 fixes two independent stale-pixel causes at the Blit boundary. Hibr's
header-only Kitty delete (`a=d,d=I,q=2,i=<id>`) is now accepted without a
semicolon, so a deleted wallpaper/image is removed from the live graphics
scene. Direct KMS now includes the graphics-scene signature in its unchanged
view decision, so an image-only move or delete triggers the repaint needed to
clear the pixels it exposed. Hibr source is unchanged.

The focused pixel gate passes with exact evidence:

```text
SDL_WALLPAPER_DAMAGE_PASS before=0,0,255 after=0,0,255 deleted=0,0,0
```

The full renderer suite passes:

```text
RUN_GATES_PASS pass=43 skip=0 fail=0
```

The package is deployed on `192.168.10.54`; `/v1/version` reports `0.1.488`.
The post-restart live trace contains one Kitty capability query, zero image
uploads, and zero rejections. The captured live screen is a clean Hibr login
saver, so it confirms the deployment and presentation path but is not a
Kitty/Sixel image-transport acceptance.

This closes the confirmed parser/invalidation defects. It does not claim that
every live overlap is caused by Kitty/Sixel: a live capture must still show a
non-empty graphics trace before graphics transport is assigned as the cause.

## 0.1.487 candidate: Hibr-compatible Kitty continuation uploads

The live 0.1.486 capture established that the login screen and desktop cells
were present, but the authenticated graphics trace recorded 768 Kitty
`unsupported-action` rejections. Hibr's unchanged Kitty encoder sends the
opening wallpaper chunk with `i=<id>` and deliberately omits `i` from each
`q=2,m=1` / `q=2,m=0` continuation. Blit was incorrectly interpreting the
omitted id as image zero, so the visible wallpaper record remained
`complete:false`.

0.1.487 keeps the current upload id per terminal session and resolves those
id-less continuations before decoding. This is a Blit-only correction; the
Hibr source tree is unchanged. The focused TCC probe and
`tools/tests/graphics_trace_gate.rb` both pass with the exact Hibr wire form:
the continuation is accepted, the image reaches `complete:true`, and the
trace records `rejected=0`. The terminal and renderer suites pass
(`RUN_GATES_PASS pass=46 skip=0 fail=0`).

The live host still requires a fresh 0.1.487 deployment before its wallpaper
trace and screenshot can be called accepted. The current captured screenshot
is evidence that the login box renders, not proof that the multi-chunk
wallpaper path is fixed on the host until that deployment is installed.

## 0.1.504 candidate: preserve complete Hibr Kitty chunks

The 0.1.503 live capture showed the actual rendering failure shape: Hibr sent
thousands of Kitty wallpaper chunks, Blit recorded them at 4095 bytes, and
the final upload was rejected as `incomplete-data`. The terminal string buffer
was exactly 4096 bytes, so it could not hold Hibr's 4096 base64 bytes plus the
Kitty APC header. The resulting image inventory was empty while the desktop
continued rendering its terminal stream.

0.1.504 raises only the bounded terminal-string capacity to 16 KiB. Hibr and
its encoder are unchanged; decoded image limits are unchanged. The graphics
trace gate now builds a TCC probe that emits four 4096-byte chunks and passes
only when the image is complete, all four trace payloads are accepted, and no
rejection is added. The package and live-host acceptance remain pending until
this build is deployed and the authenticated live trace reports the same
successful boundary.

## 0.1.486 candidate: Blit login-manager root entrypoint

The root login failure found during the 0.1.485 Hibr acceptance attempt was
reproduced exactly: Hibr's unchanged `login.hibr` reached `need sys` with
`SYS_MOD` undeclared and exited before opening the login screen. This is the
expected result when the root entrypoint has not loaded Hibr's `sys` module.

0.1.486 makes the boundary explicit in the Blit package. Its
`blit-login.hibr` shim runs `mod load sys`, sources Hibr's installed
`login/login.hibr`, and invokes its existing `lg_main`. No Hibr source or
login behavior is copied. `hibr -n hibr/blit-login.hibr` passes, and the
package gate requires the shim and its explicit module load.

The controlled lifecycle gate remains unchanged; live PAM/logind acceptance
still requires Hibr #79 on the provided host or a VM. This change removes the
known missing-module blocker but does not claim that root PAM is complete.

## 0.1.485 candidate: Blit login-manager boundary

Blit #83 now has a first-class `/v1/login` resource. The root-owned service
supervises Hibr's login script in one PTY, exposes its state and underlying
`login_manager` session, stops it deterministically, and recreates a fresh
login session after the login process exits. The package configuration enables
this manager with `BLIT_LOGIN_AUTOSTART=1`; Hibr remains an external runtime
and its source tree is unchanged.

The focused root-service gate passed with TCC-built 0.1.485 source using a
controlled `/bin/true` login command:

```text
LOGIN_MANAGER_GATE_PASS restart=1->2 stop_status=204
```

This proves Blit's lifecycle, privilege gate, API resource, and restart
behavior. It does not prove PAM, logind, or a real authenticated Hibr desktop;
that acceptance remains Hibr #79 on a VM or real host. Hibr provider and join
issues remain separate and are not modified by this Blit change.

The reproducible package `blit_0.1.485-1_amd64.deb` has SHA-256
`0376c45fdcc07957d791d4b09462dbc1e8033b9203da3becd0b9ff1741fbe368`; the
package artifact gate rebuilt it byte-identically and verified the packaged
server, updater, adapter, schema, documentation, service, and login hook.

## 0.1.482 Hibr native-PTY live evidence

0.1.482 is deployed on `192.168.10.54`. The live API reports one Hibr
desktop session named `blit` with one primary client on the lower pane and one
`--join` client on the upper pane. It does not create two desktop instances.
The live renderer reports 60 Hz, a 16 ms refresh-aware interval, zero missed
deadlines, and zero API/render errors. An injected motion at physical `(100,
100)` in the upper pane produced the diagnostic coordinate `column=253,
row=6`: the pane-local column is translated into the single desktop's
projected union coordinate before delivery to the primary owner.

The live screenshot still fails visual acceptance: the upper joined viewport is
mostly flat/incomplete and the full wallpaper/desktop scene is not present.
This is not a presentation-rate failure and is not fixed by treating the
panes as separate terminals. It is the remaining Hibr 2-D union and picture
propagation boundary tracked by Hibr #240 and #238. The screenshot is evidence
that the implementation is not complete; it is not a claim of success.

Blit 0.1.483 applies a bounded one-refresh presentation barrier to the primary and
joined PTYs of one Hibr desktop. The barrier prevents a dirty member from being
presented alone while its sibling's redraw is already in flight, releases
immediately when all active members contribute damage, and times out at the
display interval so a quiet join cannot freeze the primary. The focused gate
`tools/tests/hibr_join_frame_barrier_gate.rb` passes and verifies that no
`render_path` event occurs between barrier open and release. This fixes the
Blit-side partial-frame presentation boundary only; it does not create the
missing Kitty/Sixel payload that Hibr did not send.

The subsequent live capture also exercised the Kitty capability boundary. Hibr's
deployed probe is the documented one-pixel `a=q ...;AAAA` form; 0.1.483 already
accepted it. 0.1.484 additionally accepts the header-only form and proves the
raw-PTY `OK` reply without allocating an image, but that compatibility change
is not the cause of the missing live wallpaper. After 0.1.484 deployment the
live trace records six Kitty queries, zero rejections, and zero image uploads.
That is direct evidence that Hibr did not send the picture in this run; Blit
cannot render a bitmap absent from the PTY stream. Picture/theme state and
joined picture propagation remain the Hibr boundary tracked by #238.

The earlier 0.1.481 visual acceptance is revoked by the subsequent
physical capture. The live 1x2 result is one Hibr desktop, not two terminal
instances, but the lower primary and upper join do not form a complete,
coordinate-consistent physical desktop: the upper pane is mostly empty, the
wallpaper is incomplete, and pointer/context-menu coordinates disagree. The
fresh capture also shows that distinct PTY surfaces and a nominal 60 Hz output
are not sufficient acceptance evidence. Hibr #240 now tracks the required 2-D
union/layout contract; Hibr #238 tracks spanning picture propagation.

The live renderer-versus-state comparison was then corrected to use the
topology-reported origin of each pane. The former probe always sampled the
top-left pane, which produced a false failure for the lower surface. With the
correct origin, `host_render_agreement.rb` passes for both live surfaces
(`surface-1` and `surface-2`): the pixels in every checked blank cell equal
the background reported by that surface's API snapshot. This is strong
evidence that Blit is painting the incomplete Hibr cell scene without stale
pixels or a pane-origin error; it does not turn absent Hibr picture payloads
into a complete joined wallpaper.

The same live capture also isolates the apparent display-wide shadow. Blit's
effects are disabled and its overlay list is empty. The Hibr cell snapshots
contain `#070f17` in 69.6% of the lower pane and 83.7% of the upper pane,
while the wallpaper colour is `#0d1b2a`. Hibr's desktop settings define a
55% shadow darkening, and `#070f17` is the corresponding darkened colour.
Therefore the dark regions are Hibr shadow cells, not an SDL effect, stale
frame, or Blit overlay. Hibr's `DT_SHADOW` is enabled by default and its
window/bar shadow geometry is calculated in the joined desktop union; the
wallpaper remains primary-display-confined. This combination explains the
large shadow-like regions in the physical photograph and remains part of the
Hibr #240 joined-union contract, not a Blit compositor defect.

Live 0.1.477 evidence identified the remaining join defect: the primary
`console-blit` adapter painted its API surface, but Hibr's hold PTY had no
primary stream to fan out, leaving the joined pane black. A native-Hibr probe
then produced real joined PTY cells, while the primary was incorrectly
rejected by Blit's adapter-owner requirement. The fix makes the packaged
entrypoint use Hibr's native console and makes Blit parse both primary and join
PTYs. The adapter remains available only for explicit `surface_mode: external`
clients.

The focused TCC server gate now runs both clients in `terminal` mode and proves
distinct PTY output on distinct surfaces, joined-surface write rejection, and
joined-pane input routing to the primary. The 0.1.481 live deployment on
`192.168.10.54` has exactly one Hibr hold session with one primary and one
join; `hold clients` reports the primary at `0 0 67 240` and the join at
`0 240 67 240`. The API evidence proves distinct PTY surfaces and
primary-owner routing, but not a correct physical desktop. The focused Blit
fix now translates joined-pane input into the projected Hibr union column;
its local gate asserts the exact logical coordinate.

Spanning Kitty/Sixel wallpaper is not yet accepted: Hibr's hold picture rule
does not send a bitmap to a client whose rectangle does not contain the whole
picture, so the upper viewport lacks that wallpaper region. This is recorded
with exact reproduction and acceptance criteria in
[Hibr #238](https://git.home.arpa/itdlabs/hibr/issues/238). Blit source does not
patch Hibr's picture semantics. The 2-D desktop/input contract is tracked in
[Hibr #240](https://git.home.arpa/itdlabs/hibr/issues/240). The exact Hibr
QEMU oracle, context menus, window placement, and reconnect behavior remain
open.

The preceding 0.1.477 artifact and evidence remain recorded in
[`releases/0.1.477.yaml`](../releases/0.1.477.yaml); its mirror-primary live
behavior is superseded by ADR 0079 and must not be used as the current join
contract.

## 0.1.472 fractional font-scale contract

The font API now accepts `1.0` through `3.0` in exact tenth-step increments;
integer requests remain compatible. The runtime stores tenths as fixed-point
state and applies one nearest-pixel rule to every physical cell boundary, so
an 8x16 font at 1.1 is reported and rendered as a 9x18 cell. That same metric
drives SDL, direct KMS, panes, PTY pixel sizes, display-session reflow,
surface origins, and pointer hit-testing. Hibr source is unchanged.

The focused suite exercised all 21 valid values, representative rendered
pixels at 1.0, 1.1, 2.0, 2.3, and 3.0, display-session reflow, and rejection
of 0.9, 1.05, and 3.1. The full focused font/renderer suite passed 47/47.
The exact package was then installed through the supervised updater on
`192.168.10.54`; `/v1/version` reports 0.1.472. The live API sweep accepted
all 21 values on one running primary Hibr session, including 1.1 as 9x18,
2.3 as 18x37, and 3.0 as 24x48. The live session remained registered and
painted, the SDL3 output reported 60 Hz with zero missed deadlines, and
1920x2160 screenshots were captured at 1.1 and 3.0. The exact package and
live evidence are recorded in [`releases/0.1.472.yaml`](../releases/0.1.472.yaml).

## 0.1.471 font-scale physical-pixel correction candidate

The live scale-3 screenshot exposed a gap in the previous 0.1.470 evidence:
`/v1/fonts` reported 24x48 cells and the pointer changed size, but the SDL
terminal glyphs remained at the base 8x16 raster size. The display-attached
Hibr surface also retained its old grid. This was a real renderer defect, not
an Hibr defect; Hibr source was not changed.

0.1.471 moves font scale into one physical cell-metric boundary. SDL scene
textures stay at physical output size, SDL drawing uses the scaled metrics
directly, pointer hit-testing uses those same metrics, and a display-attached
Hibr session reflows to the new display grid. The strengthened local font API
gate now checks pixels at scale 1 and scale 2, not only JSON metadata. The
focused font/renderer run passed 47/47, and the reproducible package is
`blit_0.1.471-1_amd64.deb` with the exact checksum in
[`releases/0.1.471.yaml`](../releases/0.1.471.yaml).

Live deployment and scale-3 screenshot acceptance remain pending until the
exact package is installed on `192.168.10.54`. No physical multi-monitor or
physical high-refresh claim is made here.

## 0.1.470 refresh-bound damage coalescing candidate

The live trace identified the jerky-motion root cause: Hibr surface commits
arrived about every 5 ms while the GLES2 backend rebuilt the full 1920x2160
scene for each event. The event-driven scheduler was re-phasing after each
completed frame and allowed pointer/pixel damage to bypass the output deadline.
0.1.470 retains pending work per output and presents the newest complete state
only when that output's refresh deadline is due; input and API handling remain
immediate. Pointer damage is now retained independently for every affected
output, which preserves the cursor across differently phased displays.

The TCC build and focused frame-rate, pointer atomicity, pointer-cache,
full-rebuild, and motion-trail gates pass. The frame-rate workload reached
60.000 Hz with zero missed deadlines while its faster-than-refresh producer
burst stayed below the bounded 125% guard. The reproducible 0.1.470 package is
deployed through the supervised API on `192.168.10.54`; `/v1/version` reports
0.1.470, exactly one primary Hibr owner is registered and painted, and a
120-event API pointer burst produced contiguous live render-trace intervals
of 15.209–16.486 ms (mean 15.971 ms) with zero missed deadlines. Physical
window-motion acceptance remains pending because the API burst isolates the
presentation scheduler but does not prove the operator's hand movement or
Hibr's drag action.

The live font sweep then exercised the existing ADR 0047 API one step at a
time. Scale 1 was accepted with 8x16 cells, scale 2 with 16x32 cells, and
scale 3 with 24x48 cells. Scale 4 and scale 0 were rejected with
`invalid_font_scale`. The three installed profiles—`blit-hibr-terminus16`,
`uni2-terminus16`, and `lat15-terminus16`—were each selected successfully via
`PUT /v1/fonts`; the original Hibr profile at scale 1 was restored. The Hibr
primary remained registered and painted, proving this is a live geometry/font
operation rather than a restart requirement.

## 0.1.469 KVM source identity diagnostics

The live KVM check showed that raw mouse and keyboard counters could increase
while `last_evdev_*_path` named an event node no longer present in the current
inventory. The path is historical after Linux re-enumerates a USB device, so
0.1.469 adds the associated input resource ID and a current `present` flag for
both mouse and keyboard sources. The OpenAPI schema, runtime implementation,
diagnostic guide, ADR 0071, and evdev scan gate are updated together. Hibr
source and input ownership are unchanged.

The focused schema, network, evdev scan, renderer, input, Hibr, terminal, and
hotplug-cycle gates pass. The complete local suite reached 69 passes, 3
explicit skips, and 0 product-test failures; its package gate was rerun after
the 0.1.469 release manifest was added. The reproducible package was then
installed through the supervised update API on `192.168.10.54`, where
`/v1/version` reports 0.1.469 and exactly one Hibr primary is registered and
painted. A post-deployment KVM input capture is still pending.

## 0.1.468 drag-action correction

The live drag investigation found a second, narrower defect at the Blit-owned
Hibr adapter boundary. Hibr's documented console protocol requires a held
button in every drag action (`mouse drag left row column`); the adapter had
sent `mouse drag row column`. Hibr therefore parsed the column as the row and
an empty column, which explains directional movement that appeared vertical
only. The correction adds the deterministic held-button name to plain and
modifier-prefixed drags. Hibr source remains untouched. The focused adapter
gate passes the corrected contract. The live host now runs 0.1.468 with one
registered, painted Hibr primary; a synthetic diagonal drag moved the window
with both axes. Physical pointer motion has also been observed with both axes,
but a physical button/drag acceptance is still pending. A separate capture
also shows a stale-render artifact that must be diagnosed independently of
the drag-coordinate correction.

## 0.1.467 live Alt-drag correction

The live host was updated through the supervised API to `0.1.467`, and the
existing primary was restarted cleanly before one new primary was created.
The new session registered its Hibr owner and painted 32,400 cells on the
single 60 Hz output.

The root cause of the window-movement report was then confirmed at the source
boundary: Hibr's window manager requires `alt-press` followed by `alt-drag`,
but the Blit pointer stream previously omitted pointer modifier state. The
new live WebSocket stream carries `alt:true` while the held Alt state is
active, and the adapter gate translates it to `mouse alt-drag`. Hibr source
was not changed. Physical Alt-drag after this deployment remains the final
operator check.

## 0.1.466 live acceptance

The reproducible `0.1.466-1` package was uploaded through the supervised update
API to `192.168.10.54` and `/v1/version` returned `0.1.466`. The live host
reported one 60 Hz 1920x2160 output with refresh-aware scheduling at 16 ms.

The API then created exactly one Hibr primary using the packaged
`blit-session.hibr` entrypoint. Session 1 reported `hibr_desktop.role=primary`,
held session `blit`, a registered owner, an owner-painted surface, and 32,400
populated cells at 240x135. A cached framebuffer capture contained the full
menu bar, wallpaper, icons, cursor, and Hibr menu. API-injected pointer motion,
left-button press/release, and F10 key events were accepted and appeared in
the diagnostic trace; the probe recorded zero missed deadlines and render
times between 0.6 and 13.9 ms.

The operator subsequently confirmed that the physical mouse and keyboard work
on the live display. This closes the single-output manual input smoke check,
but not sustained physical-motion cadence, multi-output joins, hotplug, or
the outstanding screensaver trail issue (#72). Those require separate
evidence and remain open.

## 0.1.465 current candidate

Issue #75 was reproduced on the physical host while preparing the next input
and rendering run. Blit 0.1.464 accepted a revision-matched
`configuration.enabled=true` write for the only keyboard and opened its evdev
descriptor, but after the coalescing interval `/v1/settings` still held
`enabled:false` at revision 14. The settings merge could not distinguish a
known property omitted because it returned to its default from an unknown
future property that must survive a downgrade; it copied the old value back,
so the next restart would disable the keyboard again.

The serializer now uses the same input-property vocabulary as request
validation and makes the ADR 0067 distinction explicit: known and applicable
properties absent from live non-default state are removed, known but presently
inapplicable properties remain dormant, and unknown properties are preserved.
The focused gate fails twice against 0.1.464 (`enabled:false` remains and is
restored after restart) and passes against the candidate, including a separate
future-property preservation scenario.

The TCC-built candidate passed the canonical full suite with 64 passes, five
explicit skips, and zero failures, including the 120-second four-session soak
and a settled 59.999 Hz renderer workload. Two independent Debian builds were
byte-identical; the artifact gate matched the package, server, update helper,
adapter, schema, and documentation hashes. The package SHA-256 is
`c977de538edbb9c5ab75f4644138223e9d22c0f2f20246a351ad8c8db0d70828`.

The supervised update API deployed 0.1.465 to the physical host. The exact
live sequence then persisted `enabled:false` at settings revision 26,
re-enabled the keyboard and removed that property at revision 27, requested an
in-process restart, and required the reported uptime to reset. After the
asynchronous evdev opener settled, the keyboard reported `enabled:true` and
`open:true`, runtime reported one keyboard, and settings revision 27 still had
no keyboard override. This closes issue #75 on the named host. No Hibr session
was running during the settings acceptance; rendering and physical input
acceptance remain separate next steps.

## 0.1.410 current candidate

The terminal-input JSON decoder now handles the complete bounded JSON escape
set used by API clients, including Unicode escapes that represent VT control
bytes. The regression sends JSON `\\u001bX` to a PTY and verifies that the
terminal receives ESC followed by `X`, rather than the literal characters
`u001bX`. The full TCC-built terminal conformance suite passes, including
VT500, UTF-8, Hibr parity, Kitty, Sixel capability, and the new
`network-terminal-input-json-escapes` case. This candidate has not yet been
deployed to the physical host; the host remains on 0.1.409 until the package
and live lifecycle gates complete.

## 0.1.409 current candidate evidence

The production Hibr launch path now installs and selects an explicit
`blit-runtime-console.hibr` RC for API-launched Hibr children. The RC matches
the validated QEMU contract and loads only `console-blit`; the parent Blit
entrypoint already loads Hibr's native `img`, `pty`, `term`, and `hold` modules.
This avoids duplicate native module loads while closing the systemd/deployment
gap where QEMU supplied `HIBR_RC` but the live service did not. The package
gate requires this RC and checks that it loads the configured Blit adapter.
Hibr source remains unchanged.

The current launcher also opens the Blit provider before the stock desktop
enters its held-session transition. A focused live API experiment proved the
distinction: the explicit pre-open path registered the owner and produced 229
printable cells, while the previous implicit desktop path failed closed with
`hibr_attach_unresolved` and an empty surface. The fix is Blit-owned and does
not alter Hibr.

The TCC-built 0.1.407 candidate passed the OpenAPI schema gate, full terminal
conformance, Hibr singleton checks, and the exact Hibr 0.99.100 QEMU lifecycle
matrix: `LAYOUT_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`,
`HIBR_JOIN_RECONNECT_PASS`, and `HIBR_PANE_MATRIX_PASS`. The live external
image probe also registered its owner and completed a 512x512 graphics upload
through the deployed adapter. Physical multi-monitor, physical button,
physical high-refresh, and TCG 240 Hz deadline-capacity claims remain open.
The 0.1.407 package is deployed on the supplied host and `/v1/version` reports
0.1.407. Its live SDL3 output reports 60 Hz and zero missed deadlines. A
single-primary desktop launch was tested through the API; Hibr reported its
desktop startup, but no Blit adapter owner registered and the fail-closed
`hibr_attach_unresolved` path retired the empty surface. The host was cleaned
back to zero sessions afterward. This is an open Hibr integration failure, not
a renderer or frame-rate pass claim.

The 0.1.409 candidate supersedes that failed live attempt. After explicitly
retiring the stale detached Hibr hold left by the failed run, the ordinary
packaged `blit-session.hibr` API launch stayed `running`, registered its owner,
and reached 32,400 printable cells at 240x135. The captured 1920x2160
framebuffer contained the Hibr menu bar, wallpaper, desktop icons, and status
line **as terminal cell art**: that capture holds ten distinct colours in
total and no bitmap was presented at any point -- `graphics_images` was 0,
`graphics_texture_uploads` 0 and `pixel_frames_accepted` 0. The wording above
used to stop at "contained the Hibr menu bar, wallpaper, desktop icons, and
status line", which is true of cell art and easy to read as a picture having
been presented. It had not been (issue #19). A versioned API input write was
accepted while the owner remained registered. This is live one-output evidence; physical pointer events and
additional physical outputs remain open.

The same live 0.1.409 service now has explicit API routes for the discovered
physical pointer and keyboard to the single primary session. The runtime
reported 2,505 evdev mouse events, including 1,573 motion events and 10 button
events, with 927 dispatched input events and no missed presentation deadlines.
The filtered trace showed API-injected button delivery to the primary; the
physical button trace contract remains open until a button event is captured
with an evdev source in the trace. Device counters prove that Blit read input;
the source-qualified trace proves which device and target session received a
specific event.

Fresh QEMU input/render evidence was also rerun against the exact 0.1.409
server and Hibr 0.99.100 fixture. With `blit.input_soak=1`, the guest passed
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`,
`BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_MOUSE_REPEAT_PASS`,
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS`, and
`BLIT_INPUT_RENDER_SOAK_PASS`. The raw fixture observed 926 events, 547
motions, 44 buttons, one ignored repeat, and two aggregate button-state
coalesces. These are exact-current virtual input/render results; they do not
close the source-qualified physical button trace on the supplied host.
The same run now also passes `BLIT_INPUT_POINTER_BUTTON_TRACE_PASS`, proving
that the critical trace retains an evdev source, button transition, and routed
target session rather than merely incrementing aggregate counters.

The exact-current two-output 240 Hz pixel-stream fixture accepted all 240
complete frames with zero pixel-frame errors. Under this host's QEMU TCG
execution it recorded 22 additional deadline misses and 78 page-flip presents,
so `PIXEL_FRAME_RATE_240_FAIL` remains an honest
`open_tcg_deadline_capacity` result. This is not evidence of transport loss or
a physical renderer defect; KVM-backed or physical hardware evidence is still
required before changing the production scheduler or closing the 240 Hz gate.

## 0.1.405 current candidate evidence

The TCC-built 0.1.405 candidate adds bounded producer-side surface cadence
diagnostics to `/v1/diagnostics/runtime`: `surface_commit_hz`, its settled
window counters, age, and inter-commit gaps. This is intentionally separate
from output presentation cadence, so a slow or idle Hibr producer cannot be
misreported as an SDL/KMS refresh failure. An explicit eight-update API
workload measured `surface_commit_hz=7.998` with approximately 158 ms gaps.

The corrected frame-rate gate now discards the cold-start presentation window
and requires a settled window. The rebuilt 0.1.405 server measured `59.996 Hz`
at a 60 Hz SDL dummy target with zero missed deadlines. The API schema (33
paths), full VT/UTF-8/Kitty/Sixel/Hibr terminal conformance, singleton,
restart, and 20-second four-session soak gates passed; the soak remained below
the 131072 KiB RSS ceiling.
The focused current-source damage gates also passed:
`SDL_WALLPAPER_DAMAGE_PASS` preserved the wallpaper pixel across an
incremental cell commit, and `POINTER_WALLPAPER_STRESS_PASS` restored the
under-pointer scene across six samples with three pointer-cache hits. These
results cover Blit's local composition path; they do not convert the live
host's absent graphics objects or zero physical evdev events into a Hibr or
hardware acceptance claim.
The current PTY/API graphics gates also passed: Kitty texture-cache reuse
reported five cache hits without an additional upload, and the fragmented
RGBA pixel-frame gate accepted and drained all 30 frames (`26766 Hz` ingest
rate, zero pending frames). These validate the controlled graphics transport;
they do not imply that an external Hibr session actually emitted a Kitty or
Sixel image.

Fresh QEMU/VKMS images staged with the exact Hibr 0.99.100 checkout passed the
four-output Hibr window-content gate on all four 1024x768@240 Hz outputs and
the two-output layout/pane/primary-restart/join-reconnect lifecycle matrix.
The fresh two-output input/render soak also passed against this exact binary:
`LAYOUT_MATRIX_PASS`, `BLIT_INPUT_GAMEPAD_DISCOVERY_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`,
`BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_MOUSE_REPEAT_PASS`,
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS`, and
`BLIT_INPUT_RENDER_SOAK_PASS`. It observed 905 raw mouse events, 533 motion
events, 44 button events, one ignored repeat, and two aggregate button-state
coalesces. This is virtual input/render evidence, not physical evdev
acceptance.
Fresh four-output QEMU runs with the same binary and Hibr checkout also
passed independent red/green/blue/yellow output pixels and the stock Hibr
primary-content case. All four outputs reported 1024x768@240 Hz and
`KMS_FOUR_OUTPUT_PASS`; the Hibr run reported one populated primary and three
`HIBR_CONTENT_AUXILIARY_EXPECTED_BLANK` joins followed by
`KMS_FOUR_HIBR_PRIMARY_CONTENT_PASS`. The blank auxiliary state is an
intentional Hibr desktop-placement result, not a renderer pass-through.
The exact server SHA-256 is
`3b45d994ac80ae65588e4981546c31ebe6b42a8f664a42c3176a750677e14d46`.
Physical deployment remains deliberately unperformed; the supplied host is
still 0.1.401, one 60 Hz output, and its read-only evidence shows 60 Hz
presentation with zero missed deadlines but roughly 7 Hz Hibr surface commits
and zero physical evdev mouse events.

The remaining 240 Hz pixel-stream claim was characterized again against the
same 0.1.405 binary. QEMU accepted all 240 complete frames with zero pixel
frame errors, but the two-output VKMS guest accumulated additional page-flip
deadline misses in every TCG run: 11 with one vCPU, 12 with two, 18 with four,
and 26 with eight vCPUs (the earlier two-vCPU run measured 12). This host has
no `/dev/kvm`, so these are TCG capacity results, not evidence of physical
240 Hz behavior or a proven renderer defect. The production scheduler remains
unchanged; KVM-backed or physical hardware evidence is required before this
acceptance can be closed.

## 0.1.404 current candidate evidence

The TCC-built 0.1.404 candidate adds compact `codepoint_cells` and
`printable_cells` fields to `SurfaceSummary`, and separates the four-output
Hibr visual gate into the stock-desktop primary-content case and the
deterministic populated-window case. Exact Hibr 0.99.100 QEMU evidence passed
both `KMS_FOUR_HIBR_PRIMARY_CONTENT_PASS` and
`KMS_FOUR_HIBR_WINDOW_CONTENT_PASS`; the latter recorded visible screenshot
content on all four 1024x768@240 Hz outputs. The stock case correctly recorded
blank auxiliary joins as an expected Hibr state. Hibr source was not modified.

Focused TCC/local evidence passed: OpenAPI schema (33 paths), full terminal
conformance, Hibr singleton/owner/restart, font/scale, pointer composition,
54.368 Hz frame-rate gate with zero missed deadlines, and a 20-second
four-session soak (`2532` snapshots, `230` resizes, peak RSS `66704 KiB`).
Exact QEMU lifecycle, four-output independent pixels, and input/render soak
also passed. The 0.1.404 Debian artifact passed the package gate; its exact
checksum and source relationship are recorded in
[`releases/0.1.404.yaml`](../releases/0.1.404.yaml). The TCC server SHA-256 is
`7f44f7aa883fc99a2f987d92473105b0ae62db960b5ac67286aba043df291187`.
Physical-host deployment remains not performed.

## 0.1.403 current candidate evidence

The strengthened four-output Hibr visual gate is now correctly split into two
acceptance cases. The stock desktop passes
`KMS_FOUR_HIBR_PRIMARY_CONTENT_PASS`: the primary contains desktop content,
all four Hibr surfaces and KMS views exist, and auxiliary joins are explicitly
allowed to be blank because Hibr keeps its desktop chrome on the primary until
a window is placed there. The deterministic Hibr one-window workload passes
`KMS_FOUR_HIBR_WINDOW_CONTENT_PASS` with visible screenshot content on all
four outputs. The earlier single threshold was invalid because it treated an
intentional Hibr blank join as a Blit renderer failure.

The TCC-built 0.1.403 server makes the Hibr owner handshake fail closed for
fresh primaries as well as joins and resumes. If the adapter never registers
its token-scoped owner and bounded discovery finds no matching process, Blit
marks the session failed, retires its surface, disables its routes, and emits
`hibr_attach_unresolved`. A legitimate registered owner may restart and
reconnect; the restart path is not mistaken for an ownerless launch. Hibr
source was not modified.

Exact build hashes: server
`f4b8396118b55da28d82bbf0e8856ef23c5b19b3a7fa979960d05e40ff0efaac`, update
helper
`cff624c8da3a24c3867e7230407b9dcf49cfae5597e74cb1974d63ea6f21dcad`, and
adapter
`c1ac8a2cc2898b27411f79a665ce3c0c3156b227b9a2c4d77347c072b6a70db9`.
The ownerless local probe passed; local singleton, restart, font/scale,
pointer-composition, schema, and full terminal-conformance gates passed.
The exact Hibr 0.99.100 QEMU two-output lifecycle passed layout, four-pane,
primary-restart, join-reconnect, and final-topology markers. The independent
four-output 240 Hz pixel fixture, stock-desktop primary-content gate, and
four-output Hibr window-content gate passed. The input soak also passed.

This candidate is not deployed to the physical host. The host remains on
0.1.401; its current read-only evidence and the roughly 7 Hz Hibr producer
cadence are recorded below. Physical evdev, multiple physical monitors,
physical Hibr geometry, and physical high-refresh acceptance remain open.

## 0.1.402 prior candidate evidence

The TCC-built 0.1.402 server adds diagnostic classification to every
non-deferred API cell commit: `transport=api-cells` and the owning
`surface_mode` (`terminal`, `external`, or `mirror`). This distinguishes a
producer-side API update from PTY terminal output in the bounded trace; it
does not change rendering, Hibr behavior, or frame pacing. The optional Hibr
adapter was rebuilt against the exact configured Hibr 0.99.100 ABI; the
development checkout at `/home/osakka/hibr` is 0.99.120 and was not used or
modified.

The exact TCC server binary SHA-256 is
`1a6cf13e4a26c6a68d2e729f9c34047209b09ecd8db5fedccdac4c8021a5793f`; the
adapter SHA-256 remains
`c1ac8a2cc2898b27411f79a665ce3c0c3156b227b9a2c4d77347c072b6a70db9`.
The API schema gate passed with 33 paths, the full terminal conformance
suite passed, and the new external-surface trace assertion passed. Restart
of the exact serving binary passed, session write/resize passed, and the
bounded frame-rate gate measured `54.370 Hz` over `514 ms` with zero missed
deadlines. The 20-second four-session soak passed with `2372` snapshots,
`216` resizes, and peak RSS `18540 KiB` under the `131072 KiB` limit.

This candidate has not been deployed to the physical host yet; its live
0.1.401 evidence remains below. QEMU and package acceptance are run against
the final 0.1.402 artifact before release publication.

The 0.1.402 Debian artifact passes the package allowlist, embedded-version,
binary, schema, and packaged documentation gate. Its SHA-256 and source
commit are recorded in [`releases/0.1.402.yaml`](../releases/0.1.402.yaml).
The exact Hibr 0.99.100 QEMU lifecycle fixture boots two VKMS outputs, passes
the 1x1/2x1/1x2/2x2 layout matrix and four-pane session matrix, then passes
primary restart, joined-client reconnect, and final topology cleanup. The
separate adapter probe also passes. Earlier QEMU evidence using a 0.99.120
development Hibr fixture is not interchangeable with this 0.99.100
package-ABI result.

The exact 0.1.402 four-output VKMS fixture passes independent red/green/blue/
yellow screenshot pixel assertions on four 1024x768@240 Hz outputs. Its
four-output Hibr mode passes with one primary and three `desktop --join`
clients and four live surfaces. The exact QEMU input-soak fixture passes
stream input, gamepad discovery, raw evdev events, mouse-repeat filtering,
multi-device button aggregation, and render soak. These are virtual-display
and virtual-input results; physical monitor, evdev, and high-refresh
acceptance remain host-specific and open.

## 0.1.401 prior release evidence

The current rebuilt package is `blit_0.1.401-1_amd64.deb`. Its exact
checksum/source relationship is recorded exclusively in
[`releases/0.1.401.yaml`](../releases/0.1.401.yaml). The TCC server binary
SHA-256 remains
`8cade338f37f1374549af5d56a2803c083ebbda9ee5a82eedaf6bc5fe2f872a7`.

The 0.1.401 source and artifact pass the OpenAPI structural gate (33 paths,
including the versioned terminal-input subresource), full terminal
conformance, Hibr singleton ownership, font/scale, resize, SDL glyph/pixel,
frame-rate, binary pixel-frame, package, and 20-second four-session soak
gates. The latest local soak recorded `2300` snapshots and `210` resizes with
peak RSS `80636 KiB` under the `131072 KiB` limit. No Hibr source was changed.
The restart gate also re-executes the exact TCC-built binary through
`/v1/control/restart`, preserves its port, and returns a ready runtime; it no
longer depends on the package installation path.

Fresh QEMU evidence also passes: the lifecycle/rejoin wrapper emitted all
four required markers, and the four-output direct-KMS/Hibr fixture reached
four independent 1024x768 outputs, four sessions, independent pixel checks,
and `KMS_FOUR_OUTPUT_PASS`/`KMS_FOUR_HIBR_PASS`. These are virtual-display
results; the fixture's TCG deadline misses are retained as VM timing evidence
and are not described as physical 240 Hz scanout.

The authorized live host reports 0.1.401, one SDL3/OpenGLES2 output at
1920x2160 and 60 Hz, and zero renderer errors. The network frame-rate gate
measured `60.071 Hz` over a 516 ms window with `0` missed deadlines and a
16 ms target interval. This is valid live presentation evidence for that one
output under an active bounded workload; it is not a claim that an unchanged
terminal scene is continuously re-presented at vblank.

A fresh live primary was then created through the documented API using the
unchanged Hibr installation. The one primary reached `running`, its pointer
and keyboard routes were installed for `session-4`, and the canonical
10-second experience capture recorded `106` surface commits and `106`
damage-driven presents in `14120 ms` (`7.507 Hz`), with zero missed
deadlines. The runtime's selected target remained 60 Hz/16 ms. This lower
number is producer/damage cadence, not scanout rate; it identifies the live
Hibr scene-update path as the next performance/visual-fidelity boundary.
The cached 1920x2160 framebuffer artifact is recorded outside the repository
at `/tmp/blit-live-audit-primary-20261009T110254Z/screenshots/display-1.bmp`
with SHA-256
`14237d62536919fcda0cf642ff022ee2f8bc0d75354281db25c7e4e8feb57b46`.
The capture observed no physical evdev mouse events, so physical button and
motion acceptance remains open. The host exposes one physical output;
physical multi-monitor Hibr geometry and physical 120/240 Hz scanout remain
open and are not inferred from QEMU or synthetic API input. Visual Hibr
geometry/fidelity remains an open acceptance boundary pending a deliberate
comparison against the expected desktop image.

A subsequent read-only API framebuffer capture from that same live 0.1.401
host made the visual boundary concrete. The 1920x2160 image is mostly black
with sparse colored dot glyphs; its matching `surface-4` snapshot is 240x135,
revision `1340583`, with 1,114 non-space cells and zero graphics images. The
capture hash is `5a39acb2fa382e8d52bf843f28a7077ba7d86a49d23793ce3280feca637887d7`
and it was saved outside the repository at
`/tmp/blit-live-capture-20261009.bmp`. This proves the API surface and
framebuffer agree for that scene; it does not prove that an expected Hibr
wallpaper was delivered to Blit, and it is not evidence for the undeployed
0.1.405 candidate.

A later read-only runtime sample on the same unchanged 0.1.401 host reports
`present_calls=12022`, `surface_commits=11964`, `frame_rate_hz=7.190`,
`missed_deadlines=0`, one 60 Hz output, one session, two input routes, and
zero physical evdev events during the sample. This is consistent with the
earlier finding: the service can schedule/present at the display cadence, but
the active Hibr producer is only committing changed scenes at roughly 7 Hz
in this workload. It is live evidence for 0.1.401, not validation of the
un-deployed 0.1.402 artifact.

## 0.1.400 deployed baseline and 0.1.401 contract correction (historical)

The deployed baseline TCC build embeds version `0.1.400` and has server SHA-256
`73a07f5a0cce7148c77aa673f2b14cf1defab629d36e8011a8660673eef36cf4`.
The Debian artifact
`blit_0.1.400-1_amd64.deb` has SHA-256
`3881dd591e41e87acd61a973ce47bd37e1636a23957b82ff667ed0572daf9a58` and
passes the package allowlist, embedded-version, schema, binary, and packaged
documentation checks. It was deployed through the supervised update API and
the live host now reports 0.1.400.

The 0.1.400 binary passes the local font, resize, glyph, frame-rate,
pixel-frame, Hibr singleton, full terminal-conformance, and 20-second
four-session soak gates. Fresh QEMU images staged with the 0.1.400 binary
also passed the two-output lifecycle/rejoin wrapper, raw multi-device input
(`BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_MOUSE_REPEAT_PASS`, and
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS`), and four-output Hibr marker
(`KMS_FOUR_HIBR_PASS`) runs. These remain virtual evidence; no physical
multi-monitor or physical high-refresh claim is made.

The deployment also closed the live terminal-input observation: the canonical
`POST /v1/terminal-sessions/{session_id}/input` route accepted a 9-byte
`LIVE400` request and the session was deleted cleanly. The earlier `405` came
from probing the undocumented parent session path; the OpenAPI file and ADR
0022 had incorrectly described that parent path. 0.1.401 corrects the schema,
API guide, gateway guide, ADR wording, and adds `api_schema_gate.rb`, which
rejects both the wrong parent POST and a missing `/input` route.

The corrected 0.1.401 package was then deployed through the supervised update
API and `/v1/version` reported 0.1.401. A valid plain UTF-8/box-drawing live
capture produced a 1920x2160 24-bit BMP with SHA-256
`4647a5073882e2040dfca4d255b9f6735d133a8857717a8a50996830800f2d58`.
The authoritative surface snapshot and framebuffer both showed `BLIT401 TEST`,
UTF-8 characters, and box-drawing text legibly. The canonical `/input` route
accepted terminal data, synthetic pointer motion/button events were accepted,
and cleanup returned sessions, surfaces, streams, and input routes to zero.
The host still has one physical 60 Hz output; this is not multi-monitor or
physical-evdev acceptance.

The 0.1.401 TCC binary has SHA-256
`633167c51397f0a7c129406a5ecb192e24f0fb16919e815d2ac0e991f75a500f`.
Its corrected-schema Debian artifact checksum is recorded in
`releases/0.1.401.yaml`.
The artifact passes the package gate, API schema gate, terminal conformance,
font/resize/glyph/pixel/frame gates, singleton ownership, and the 20-second
four-session soak. Fresh 0.1.401 QEMU images pass the lifecycle/rejoin
wrapper, raw multi-device input markers, and four-output Hibr marker.

## 0.1.399 focused local acceptance after commit 6888dc5

The canonical TCC rebuild produced the 0.1.399 server binary with SHA-256
`dd7095814328ed384027ccfc90969ad5dab81af96827ffa2889ed37d65fc4071`.
The following local SDL dummy/software gates passed against that binary:

- pointer composition, pane hit-testing, free-pane fallback, hidden-cursor
  damage, pointer-plus-terminal damage atomicity, and wallpaper-pointer stress;
- SDL background batching, graphics-image cache reuse, and wallpaper damage;
- authenticated network access, HTTP fragmentation/back-pressure, WebSocket
  stream replay, session deletion escalation, clean API shutdown, update
  cancellation/idle timeout/reset, and screenshot overlay invalidation;
- font selection/scale reflow, session write/resize, SDL glyph rendering,
  frame-rate scheduling (`54.373 Hz` over a `30 Hz` minimum), and binary pixel
  frames (`30/30` accepted with `0` ingest errors);
- the complete terminal conformance suite, Hibr singleton ownership, and a
  20-second four-session terminal soak (`2240` snapshots, `204` resizes,
  peak RSS `80652 KiB` under the `131072 KiB` limit).

The QEMU reruns used the exact current staged 0.1.399 fixtures and exited
cleanly. The two-output lifecycle fixture emitted
`LAYOUT_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`,
`HIBR_JOIN_RECONNECT_PASS`, and `HIBR_PANE_MATRIX_PASS`. The input fixture
emitted `BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_MOUSE_REPEAT_PASS`, and
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS` with 800 raw events. The
four-output fixture emitted `KMS_FOUR_HIBR_PASS`. These are virtual QEMU/VKMS
results; they do not close the physical monitor, physical evdev, or physical
120/240 Hz scanout gates described below.

## Live host 0.1.398 physical/API observation: 2026-10-09

The authorized read-only/API probe at `192.168.10.54` reported Blit 0.1.398,
one `1920x2160` output at 60 Hz, SDL3/OpenGL ES2, and one discovered evdev
pointer. A temporary ordinary `/bin/sh` session was created on `display-1`
with the display-derived `240x135` cell geometry. Its screenshot was a
1920x2160 24-bit BMP (SHA-256
`a10c539a70cb4f4c63a5258b94241b2be13a13460f40844228a9867dedd95153`); visual
inspection showed legible ANSI-colored text, UTF-8/box-drawing characters, a
correctly placed cursor, and a correctly placed pointer. Two synthetic API
input events (motion and button) were accepted and appeared in runtime input
counters. Physical evdev counters remained zero, as expected for a synthetic
probe.

The 0.1.398 host initially returned `405 method_not_allowed` for the then-
incorrectly documented parent session POST. After deployment of 0.1.400, the
correct `/input` route accepted terminal data; the temporary session was
deleted and a follow-up runtime query reported zero sessions, surfaces,
streams, and input routes. No Hibr process or Hibr source was touched.

## Current 0.1.399 Hibr singleton-owner and lifecycle acceptance

The TCC-built server now classifies the Hibr desktop entrypoints and enforces
one primary owner per Hibr held-session name. The local API gate passed all
four cases: an orphan `--join` returns `409 hibr_desktop_owner_missing`, one
primary is accepted and reports `hibr_desktop.role=primary`, a duplicate
primary returns `409 hibr_desktop_exists`, and a join against that primary is
accepted and reports `hibr_desktop.role=join`. Ordinary non-Hibr shell
sessions remain unaffected. The request argument buffer is cleared for every
request; this prevents a prior `--join` argument from leaking into a later
shorter request. Hibr source was not modified.

The exact 0.1.399 binary was also staged with the unchanged Hibr 0.99.100
tree into the four-output VKMS fixture. It reached four displays, one primary
Hibr desktop plus three `--join` clients, four running sessions and four
surfaces, and emitted `KMS_FOUR_HIBR_PASS`. The QEMU fixture ran at its
configured 240 Hz mode but reported TCG scheduling misses; this validates
topology and ownership, not physical 240 Hz performance.

The exact current TCC binary also passes the restart/rejoin variant in the
two-output VKMS fixture: `LAYOUT_MATRIX_PASS`,
`HIBR_PRIMARY_RESTART_PASS`, `HIBR_JOIN_RECONNECT_PASS`, and
`HIBR_PANE_MATRIX_PASS`. The fixture uses the real staged stock Hibr desktop
launcher, kills the registered primary owner, kills a joined attach client,
and then creates a replacement join. The primary remains running while the
dead join becomes `failed`, allowing the replacement join to be accepted.
The fixture's `qemu_exit=0` is checked alongside all four markers; no Hibr
source was modified. The host-side
`tools/tests/qemu_hibr_lifecycle_gate.sh` wrapper independently asserts the
four markers, so a guest poweroff cannot mask a missing lifecycle result.

The 0.1.399 direct-KMS 240 Hz pixel-stream run accepted all 240 frames with
zero pixel-frame errors and selected a real KMS-reported 240 Hz mode, but the
visible cadence gate correctly failed under QEMU TCG: the two-vCPU run grew
missed deadlines from 20 to 40. A comparison with eight vCPUs and
multithreaded TCG improved the run to 3 baseline and 16 post-stream misses,
but still did not pass. `/dev/kvm` is absent on this test host. This is
retained as VM-capability evidence; it is not converted into a physical
240 Hz claim or “fixed” by enabling asynchronous tearing.

## Current 0.1.396 live glyph geometry acceptance

The live host accepted 0.1.396 and reported SDL3/OpenGL ES2 with
`render.glyph_path=geometry-batch`. A fresh API-launched Hibr desktop produced
a clean framebuffer with the wallpaper, menu, icons, and terminal status
legible; no coloured-dot glyph corruption was observed. The 60 Hz output
reported zero missed deadlines. During a five-second live sample the output
recorded 232 presents (about 46 Hz) while the desktop workload was active.
The Hibr sessions were deleted afterward and the live resource counts returned
to zero. `BLIT_SDL_GLYPH_BATCH=off` remains the explicit rollback path.

## Current 0.1.394 font API and left-edge artifact evidence

The TCC-built 0.1.394 server passes the font API gate: profile enumeration,
runtime scale 2 (8x16 to 16x32 cells), pane/PTy reflow, profile selection, and
restoration to scale 1. The packaged 0.1.394-1 artifact passes the package
gate and was deployed to the live host.

On the live Hibr session, the full authoritative surface snapshot contained
the text `0` at cells `(0,20)` and `(0,21)`, and the surface cursor was at
`(0,22)` with a visible block cursor. The cached framebuffer screenshot shows
the same two glyphs followed by that cursor at the left edge. This establishes
that the reported `0`/block is Hibr-owned terminal content and cursor state,
not an uninitialized Blit framebuffer or an extra compositor overlay. The
session was deleted after capture; the live resource counts returned to zero.
No Blit change was made to conceal or rewrite those cells.

## Current 0.1.392 troubleshooting and refresh-boundary evidence

## Current 0.1.393 Hibr lifecycle and tall-surface evidence

### Live host boundary capture: 2026-10-09

The live host at `192.168.10.54` was queried through the network API using
Bash `curl`. It reported 0.1.393, one SDL3/OpenGL ES2 output at 1920x2160 and
60 Hz, zero missed deadlines, and a 16 ms refresh-aware target. A controlled
ordinary `/bin/sh` session at the same `240x135` surface size returned
`stty size` as `135 240`, and its surface cell snapshot contained the marker;
this proves the Blit PTY geometry and full-height terminal path independently
of Hibr.

The unchanged packaged Hibr entrypoint was then launched through the API with
the same `240x135` surface. The captured framebuffer showed Hibr's scene in
the upper region and a uniformly black lower region. Runtime evidence showed
the Hibr session running with `xterm-kitty` and Kitty/Sixel capabilities,
nonzero terminal bytes and surface commits, SDL presents, zero missed
deadlines, and no physical evdev events during the automated window. The
capture is retained under
`diagnostics/live-hibr-screenshot-20261009T084834Z/`; it is a diagnostic
failure for Hibr full-height geometry, not a Blit renderer pass. Hibr source
was not modified. The physical Hibr geometry gate therefore remains open and
requires Hibr-side investigation/support, while Blit retains the evidence and
the API control path.

The exact TCC-built 0.1.393 source passes terminal conformance and the
focused atomic pointer, wallpaper, and background-batch gates. The canonical
four-output VKMS fixture now explicitly stages the Blit Hibr adapter path;
the tall 240x135 fixture passes `KMS_FOUR_HIBR_TALL_PASS` with one primary and
three joins. This closes the previously open tall-surface boundary in QEMU.

The fix keeps API-launched Hibr sessions running after the short launcher
process exits, continues bounded owner reassertion after the launcher PTY is
closed, and exposes owner registration/discovery state in `hibr_attachment`.
The adapter registration path is Blit-owned and token-scoped. Hibr source was
not modified. Physical Hibr geometry, physical multi-monitor behavior,
physical evdev behavior, and physical high-refresh acceptance remain open.

The exact TCC-built 0.1.392 SDL renderer passes the focused pointer-plus-damage
atomicity, wallpaper-pointer stress, wallpaper preservation, and batched
full-screen background gates. These gates exercise the artifact shape reported
on the physical host and preserve untouched scene pixels across pointer motion
and incremental terminal updates.

The authorized live host reports 0.1.392 with SDL3/OpenGL, one 60 Hz output,
and approximately 16 ms SDL presents. A five-second canonical troubleshooting
capture recorded no physical evdev events and no active Hibr session; that is
an input/session observation, not a renderer failure. An API-injected pointer
control sample accepted and presented four motions, proving the API input and
SDL composition path independently of physical-device access.

The live 0.1.392 frame-rate gate then created a bounded temporary terminal
session through the network API and measured 61.433 Hz on the 60 Hz output,
with a 16.954 ms maximum render window and zero missed deadlines. The gate
deleted its session successfully; the host returned to zero sessions and zero
surfaces afterward.

The exact direct-KMS VKMS fixture accepted all 120 and all 240 producer frames
with zero protocol errors, but did not sustain visible page-flip cadence under
the available two-output QEMU TCG environment. The 240 Hz synchronous run
retired 81 presents in its capture window; the asynchronous comparison retired
68 and had larger loop gaps. The 120 Hz run also failed the visible-presentation
gate. No production scheduler change is justified by this evidence: the
failure is at virtual VKMS/TCG page-flip delivery, and enabling asynchronous
flips would trade correctness for possible tearing. Physical 120/240 Hz
acceptance remains open and requires the real display hardware.

The fresh 0.1.392 TCC binary also passes the canonical four-output Hibr
fixture: four 240 Hz VKMS outputs, one primary session plus three joins, four
independent surfaces, and clean guest shutdown. The tall 240x135 diagnostic
variant remains intentionally open as the held-session boundary described
above.

The canonical capture protocol is documented in `docs/diagnostic-capture.md`.
Hibr source was not modified.

The TCC adapter build now rejects the development Hibr checkout at `0.99.116`
when the package target is `0.99.100`, and accepts the exact unchanged
`0.99.100` checkout. The resulting 0.1.392 package passes the artifact
allowlist and checksum gate; its adapter and package provenance are recorded
in `releases/0.1.392.yaml`.

## Current 0.1.388 compositor and multi-output evidence

## Current 0.1.389 KMS submission-boundary evidence

The exact TCC-built 0.1.389 binary passes the complete terminal, renderer,
pointer, pixel-frame, glyph, timing, and package gates. Direct KMS now returns
submitted and deferred output masks: pending pixel-frame credits are consumed
only after a page-flip or recovery modeset is submitted, and pointer damage is
retained while an output remains deferred. This fixes a real state-retirement
bug without treating a deferred page flip as a visible presentation.

The exact 0.1.389 Hibr QEMU input fixture passes the layout, keypad, stream,
gamepad, raw evdev, mouse-repeat, and multi-mouse button markers. The exact
four-output Hibr fixture passes with four 240 Hz VKMS outputs, four Hibr-backed
surfaces, and one primary plus three `desktop --join` clients. The 240 Hz
scheduler still passes; the producer-driven pixel stream remains open because
the no-KVM TCG/VKMS guest misses virtual page-flip deadlines.

The physical host remains on 0.1.385. Physical deployment, real evdev input,
physical multi-monitor behavior, and physical high-refresh behavior remain
unverified. Hibr source was not modified.

The exact TCC-built 0.1.388 binary passes terminal conformance, including
malformed UTF-8 replacement as one U+FFFD cell per invalid leading byte. The
SDL wallpaper-damage gate, external-pointer composition gate, pointer-damage
atomicity gate, wallpaper pointer-stress gate, background batching gate,
60 Hz frame-rate gate, and API pixel-frame gate all pass.

The exact 0.1.388 Hibr-staged QEMU input fixture passes the layout matrix,
keypad, stream, gamepad, raw evdev, mouse-repeat, and multi-mouse button
state markers. The exact four-output fixture passes with four 240 Hz VKMS
outputs, four Hibr-backed surfaces, and one primary plus three
`desktop --join` clients. The exact KMS fixture passes the 240 Hz scheduler,
wide Kitty pixel oracle, two-output capture, and API screenshot checks.

The sustained 240 Hz pixel-stream acceptance remains open: QEMU is running
without `/dev/kvm`, and TCG/VKMS misses visible page-flip deadlines. This is
not converted into a product pass. The physical host at `192.168.10.54`
remains on 0.1.385, so live deployment, physical input, and physical
multi-monitor acceptance remain open. The live 0.1.385 screenshot evidence
showed sparse scene corruption; commit `0053a34` restores under-text
wallpaper during incremental damage and is included in 0.1.388. Hibr source
was not modified. A controlled QEMU comparison with 8 vCPUs and
multithreaded TCG completed 240 frame uploads but only 83 visible presents,
versus 81 with 2 vCPUs; no safe renderer change is justified by that result.

## Current 0.1.387 terminal-parity and high-refresh boundary

The exact TCC-built 0.1.387 binary passes the complete PTY terminal
conformance suite, including Hibr-parity `DECRQM` reports for alternate-screen
modes `47/1047/1049` and legacy X10 mouse mode `9`. The corresponding package
passed the exact-artifact gate; Hibr source was not modified.

The exact 0.1.387 direct-KMS QEMU rerun selects two 240 Hz VKMS outputs and a
4.166 ms refresh-aware deadline, and passes `REFRESH_SCHEDULER_240_PASS`. Its
pixel producer accepts all 240 frames with zero frame errors. The guest
completes only 81 visible page flips and records 10 missed deadlines under
QEMU TCG; `/dev/kvm` is absent on this test host. Therefore
`qemu_high_refresh_acceptance` and physical high-refresh acceptance remain
open. This result is retained as a virtual-driver/VM-capability boundary, not
converted into a product pass and not addressed with asynchronous tearing.

The exact 0.1.387 Hibr/input fixture also passes `LAYOUT_MATRIX_PASS`,
`BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_KEYPAD_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`,
`BLIT_INPUT_MOUSE_REPEAT_PASS`, and
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS`; the guest powered down cleanly.

The exact 0.1.387 four-output Hibr fixture passes `KMS_FOUR_HIBR_PASS` with
four independent 240 Hz VKMS outputs, four Hibr-backed sessions, and four
surfaces. The guest powered down cleanly. This closes the virtual
multi-output/orchestration evidence for the release; physical multi-monitor
and physical high-refresh acceptance remain open.

## Current 0.1.366 renderer-isolation result

The live API screenshot from 0.1.365 showed the wallpaper/menu correctly but
the terminal glyphs as coloured dot patterns. A controlled API pointer probe
changed only the cursor-sized pixel region, so pointer motion was not the
source of the large corruption. The live renderer is SDL `opengles2`.

0.1.366 adds `render.glyph_path` to `/v1/diagnostics/runtime` and defaults
GLES2 to the immediate atlas-texture path (`immediate-texture`). The prior
`SDL_RenderGeometry` path remains selectable with
`BLIT_SDL_GLYPH_BATCH=on`; `BLIT_SDL_GLYPH_BATCH=off` is explicit. Local TCC
glyph, pointer-atomicity, and wallpaper stress gates pass. The live visual
acceptance gate is open until the 0.1.366 package is installed and its
post-install screenshot confirms legible terminal glyphs.

The package was subsequently accepted by the live supervised updater on
`192.168.10.54` and `/v1/version` reported `0.1.366`. The live diagnostics
reported `renderer=opengles2` and `glyph_path=immediate-texture`. The API
pointer artifact capture found only cursor-sized pixel differences between
positions, and the live synthetic motion/button verifier passed. These are
not physical evdev or physical-display visual acceptance: the automated
captures recorded zero physical evdev events, and the visual gate remains
open pending an operator-visible confirmation of the real display.

The attempted current QEMU input rerun is explicitly not recorded as a Blit
result: the available `/home/osakka/new_project/build-inputs/bzImage` ignored
both the newly staged and an older known initramfs, then panicked trying to
mount `/dev/root` before `/init` ran. This was reproduced with and without
`root=/dev/ram0`; the guest emitted no Blit marker. The staged 0.1.366 image
and unchanged Hibr checkout therefore remain untested in that particular
kernel fixture until a kernel with working external-initrd support is used.

Using the workspace VKMS kernel `/tmp/linux-vkms-build/arch/x86/boot/bzImage`
(`6.19.14`) instead, the current 0.1.366 binary reached the Hibr input
fixture. It passed `LAYOUT_MATRIX_PASS` for 1x1, 2x1, 1x2, and 2x2 layouts,
`BLIT_INPUT_KEYPAD_PASS`, `BLIT_INPUT_STREAM_PASS`,
`BLIT_INPUT_RAW_EVDEV_PASS`, `BLIT_INPUT_MOUSE_REPEAT_PASS`,
`BLIT_INPUT_MULTI_MOUSE_BUTTON_STATE_PASS`, and `BLIT_INPUT_GAMEPAD_PASS`.
The fixture observed 656 raw evdev events, 392 motion events, 27 button
events, 230 synchronization events, and one ignored button repeat. Hibr was
staged from the unchanged `/home/osakka/hibr` checkout.

The same current binary then passed the full Hibr pane workload under the
same VKMS kernel: `HIBR_PANE_MATRIX_TOPOLOGY` configured a 2x2 logical layout,
four Hibr sessions reached `running` with four independent surfaces, and the
guest emitted `HIBR_PANE_MATRIX_PASS`. This validates primary plus three-join
orchestration on two VKMS outputs; it is not evidence for four physical
monitors.

## Current 0.1.363 diagnostic correctness

The TCC build corrects the per-output presentation timing source in the API:
SDL-backed outputs now report SDL present durations, while direct KMS outputs
continue to report DRM present durations. The terminal conformance suite now
asserts this distinction when SDL has presented a frame. The local gates pass:
terminal conformance, pointer-damage atomicity, wallpaper/pointer stress, and
the 60 Hz frame-rate gate.

The canonical live troubleshooting transaction is
`tools/diagnostics/experience-capture.sh`. Its timed phase excludes screenshot
readback; screenshots are collected afterward for every discovered display.
The first live 0.1.362 capture completed against `192.168.10.54`, but observed
no physical evdev events because no pointer movement occurred during the
window. Physical input and physical multi-monitor acceptance therefore remain
open. This release does not claim those gates are closed.

After deployment, the live Hibr session was restored with `--resume` and the
explicit API routes `hibr-keyboard` and `hibr-pointer` were installed against
`session-4`. Before that operation `/v1/input/routes` was empty; a discovered
evdev device alone is not sufficient to deliver input to Hibr. The synthetic
input verifier now passes through the configured route. Physical movement and
clicks still require a real operator action during a capture and remain open.

## Current 0.1.364 pointer target evidence

The pointer trace now records the selected session and routing mode. On the
live one-pane Hibr desktop, an injected click selected `session-1` with
`routing=pane-hit-test` after the fixed `evdev-pointer` route was removed. This
is the required multi-pane behavior: pointer targeting follows global pointer
position and pane geometry, while keyboard focus remains an explicit route.
A fixed pointer route remains available for dedicated input-probe workloads,
but is not used for the Hibr desktop topology.

The deterministic `tools/tests/pointer_target_gate.rb` now protects this
invariant and passes alongside pointer-damage atomicity, external pointer
composition, and wallpaper stress. Physical evdev input remains a separate
hardware gate and is not inferred from these API-injected tests.

The exact 0.1.364 TCC binary was also rerun in the two-output VKMS fixture
using the refreshed initroot: `PPM_DISPLAY_47_WIDE_KITTY_PASS`,
`PPM_DISPLAY_48_PASS`, `KMS_API_SCREENSHOT_PASS size=3145782`, and
`KMS_CAPTURE_PASS`. The first attempted rerun was rejected as invalid evidence
because an empty temporary initroot produced a 4 KiB initramfs and a kernel
panic before Blit started; it was not recorded as a Blit failure.

The exact same 0.1.364 binary was then staged with the unchanged Hibr
0.99.100 checkout into the four-output VKMS fixture. One primary Hibr desktop
and three `desktop --join` clients reached four live sessions and four
presented surfaces across four independent 240 Hz VKMS outputs. The run
returned `KMS_FOUR_HIBR_PASS` and powered the guest down cleanly. This closes
the current software orchestration/topology gate; it is not physical
multi-monitor or sustained physical-240-Hz evidence, and the Hibr checkout
was not modified.

## Current 0.1.365 live diagnostic result

0.1.365 adds recognition of UTF-8-encoded C1 terminal controls at the PTY
parser boundary. The TCC terminal conformance suite passes both UTF-8 C1 CSI
and UTF-8 C1 OSC isolation cases, in addition to the existing raw-C1 cases.
SDL glyph, graphics-cache, and wallpaper/pointer stress gates also pass.

The exact 0.1.365 package was accepted and verified by the supervised update
API on `192.168.10.54`. A post-deployment experience capture measured `212`
presents over `8683 ms` (`24.416 Hz`) against the live `60 Hz` target, with
zero missed deadlines and no physical evdev events during the window. The
capture's surface snapshot contained no regex-detectable CSI/SGR fragments,
but the visual screenshot still requires further renderer/lifecycle
investigation; this release therefore does not claim the physical visual gate
closed. The primary Hibr session was reattached to
`pane-display-1-1` after restart. The unchanged Hibr checkout remains outside
the Blit change set.

## Current 0.1.362 live Hibr evidence bundle

The live host was started through Blit's API with the documented primary
desktop command. The first attempt was intentionally retained as evidence: it
created a surface containing only 125 non-empty cells and Hibr exited with
code 1 after reporting that the held desktop was already running. This is a
launch-state failure, not a renderer failure. Deleting that failed API session
and retrying with Hibr's `--resume` option produced a running session whose
`240x135` surface advanced to more than 17,000 non-empty cells.

The synchronized capture is stored under
`diagnostics/live-0.1.362-hibr-resume-capture-20261008T233122Z/`. Its
screenshot shows the full-screen animated Hibr scene. During the capture Blit
recorded `326` presents, zero missed deadlines, and a maximum render duration
of `22.149 ms` on the physical host's `60 Hz` output. The measured
damage-driven frame rate is workload-dependent and must not be confused with
the output scanout rate.

The same evidence bundle recorded zero physical evdev events. Therefore the
physical pointer/click acceptance gate remains open; no claim is made about
mouse behavior from this run. The canonical troubleshooting sequence is now:
capture topology and session state, capture surface summary/revision, capture
runtime timing and render-path trace, capture a screenshot, then compare raw
evdev counters and filtered pointer traces over the same window. A failed
Hibr launch is repaired with `--resume` or Hibr's own lifecycle command; Blit
does not alter Hibr state or source.

The exact TCC-built 0.1.362 input fixture was also rerun after making QEMU
staging self-contained. It passed raw evdev delivery (`800` events, `479`
motion events, `33` button events), mouse autorepeat rejection, multi-device
routing, per-device button-state coalescing, keyboard/gamepad delivery, and
stream routing. The staging helpers now include their Hibr workload probes and
normalize the guest shell interpreter, so this result does not depend on a
historical initroot tree.

The exact current candidate also passes the refreshed two-output KMS capture
(`KMS_CAPTURE_PASS`) and the four-output Hibr fixture
(`KMS_FOUR_HIBR_PASS`). The four-output run reports four independent 240 Hz
VKMS outputs, four sessions, and four surfaces representing one primary Hibr
desktop plus three joined clients. It records startup deadline misses under
the virtual 240 Hz workload, so this is topology and presentation evidence,
not sustained physical 240 Hz acceptance.

The exact current Hibr adapter fixture also completed with `ACCEPTANCE_PASS`.
It rebuilt `console-blit.so` against Hibr `0.99.100`, loaded the adapter probe,
started a primary and joined Hibr client, verified both Hibr control displays,
installed keyboard and pointer routes, and shut the guest down cleanly. This
closes the software adapter gate; it does not change the rule that Hibr source
is external and unmodified.

A further ten-second physical observation was run against the same live Hibr
session on 0.1.362. The session remained running and the surface revision
continued advancing; the 60 Hz output recorded zero missed deadlines and the
capture-window maximum loop gap was `9.049 ms`. No physical evdev motion,
button, or wheel event arrived, so the physical-input gate remains open. The
result is preserved under
`diagnostics/live-0.1.362-physical-observation-20261008T234837Z/`.

The current pixel-stream matrix is explicit. The 60 Hz producer stream passes
with all 60 frames accepted, no additional missed deadlines, and
`KMS_API_PIXEL_PASS`. The 240 Hz producer stream accepts all 240 frames with
zero frame errors, but the QEMU TCG/VKMS guest presents only 79 before the
bounded run ends; KMS reports a maximum present duration of `27.137 ms` and
additional missed deadlines. `/dev/kvm` is unavailable on this test host, so
the 240 Hz result is recorded as a VM capability limit rather than a physical
hardware result. No scheduler relaxation or asynchronous tearing workaround
was introduced.

The same 240 Hz run was repeated with QEMU TCG multi-threaded translation
explicitly enabled. It improved visible presents only from `79` to `81`, but
still recorded `16` missed deadlines and a `23.473 ms` maximum KMS present.
This rules out the default TCG threading mode as the sole cause; the result
remains a virtual VKMS/TCG limitation rather than a production scheduler
change.

The intervening 120 Hz producer stream has the same boundary: all 120 frames
were accepted with zero frame errors, but missed deadlines increased from `3`
to `22`, with a maximum KMS present duration of `35.558 ms`. Therefore the
current validated virtual cadence is 60 Hz only; 120/240 Hz presentation must
be validated on hardware or a VM with working KVM acceleration before any
production claim is made.

## Current 0.1.359 live diagnostics and physical-input boundary

The authorized live host reports `0.1.359` and remains reachable at
`192.168.10.54:8080`. The release adds per-device evdev counters to the
runtime diagnostics endpoint. The live service currently discovers one pointer
at `/dev/input/event5` (fd `8`), but the capture window recorded zero events,
zero motion events, zero wheel events, and zero button events. Consequently,
the physical mouse symptom is still open: this evidence proves device
discovery, not that a physical event reached Blit's process.

The live synthetic API input probe passes motion, button, dispatch, pointer
position, and render-path checks. The renderer reports SDL OpenGL ES 2 with a
60 Hz display mode, a 16 ms refresh-aware target interval, no missed deadlines,
and sub-14 ms maximum observed loop/render gaps in the current window. The
reported frame rate is damage-driven and is zero while idle; it must not be
interpreted as a monitor-refresh measurement.

A real-host renderer isolation run created a temporary API-owned `80x30`
opaque surface, committed all `2400` cells in bounded batches, moved the API
pointer through six positions, and captured six distinct screenshots. All
sampled scene pixels remained non-black, and the temporary session was deleted
cleanly. This proves the deployed SDL composition path can render and update a
surface on the real host; it does not prove physical evdev input or Hibr's
wallpaper/graphics scene.

The TCC-built `0.1.359` package passed the local pointer-damage,
external-pointer-composition, hidden-cursor, glyph, wallpaper-stress, and
terminal-soak gates. The corrected two-output VKMS QEMU fixture passed KMS
capture, the wide Kitty grid, the 48-cell display capture, and the API
screenshot gate. QEMU was powered down and no QEMU process remains. Physical
mouse, physical multi-monitor, and sustained physical high-refresh acceptance
remain open.

Package SHA-256: `faecc11058d4d19c6d0b28f4650d44c033af744bff31c3702d0f1d84abac255a`.

## Current 0.1.360 input-protocol correction

The TCC-built `0.1.360` candidate ignores Linux mouse `EV_KEY` value `2`
(autorepeat) instead of forwarding it as another button press. The QEMU Hibr
input fixture emits this event deliberately and passes
`BLIT_INPUT_MOUSE_REPEAT_PASS`, while also passing raw evdev delivery and the
two-mouse routing checks. This addresses a concrete duplicate-click failure
mode without changing Hibr or the SDL compositor.

The candidate has passed the pointer-damage, external-pointer-composition,
hidden-cursor, full-surface wallpaper stress, and glyph gates. Physical host
mouse acceptance remains open because the live device capture has not observed
a physical evdev event.

The exact `0.1.360` binary also passes terminal conformance and the bounded
two-session PTY soak (`356` snapshots, `32` resizes, `53580 KiB` peak RSS).
The exact binary passes the two-output VKMS capture with
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`; the guest powered down and
no QEMU process remained. The live host now reports `0.1.360`, and its
synthetic API input probe passes. Its physical pointer inventory is present,
but its physical counters remain zero outside an actual movement window.

## Current 0.1.361 cached-scene pointer correction

The `0.1.361` candidate no longer treats pointer motion plus a committed Hibr
cell damage rectangle as a full-screen SDL redraw. The pointer is composed
after the complete cached scene is copied, so the renderer updates only the
committed damage region and presents one complete frame. The atomic pointer
gate, full-surface wallpaper stress gate, and external-pointer-composition
gate all pass with the `incremental-damage` trace path and intact untouched
pixels. Hibr remains unmodified; physical host acceptance remains open.

The deployed live `0.1.361` service also passed an API-owned real-host
`80x30` surface test: pointer motion and committed cell damage produced the
`incremental-damage` trace in one attempt, with zero missed deadlines, and the
temporary session was cleaned up. The live synthetic input probe passes. This
still does not close physical mouse acceptance because the live evdev counters
remain zero without a real movement window.

## Current 0.1.362 multi-device button-state correction

The `0.1.362` candidate maintains button state per evdev mouse and emits a
logical press or release only when the aggregate state changes. A release from
one mouse can no longer clear a button still held by another. Runtime
diagnostics expose `evdev_mouse_button_state_coalesced`. The exact QEMU Hibr
input fixture passes with `state_coalesced=2`, autorepeat rejection, raw evdev
delivery, and multi-mouse routing. Hibr remains unmodified; physical host
acceptance remains open.

## Current 0.1.358 real-host evidence and remaining visual gate

The authorized host now reports `0.1.358`. The attach PTY geometry transition
is active: the primary Hibr session surface is `240x135` with all `32400` cells
populated, correcting the earlier rows `25..132` omission. The six-second
timing capture recorded zero missed deadlines and render/present calls with
sub-millisecond steady-state render cost; it recorded zero physical evdev
events because no physical movement or click occurred during capture.

An earlier API screenshot is a valid `1920x2160` BMP and is saved as
`diagnostics/live-358-visual/display-1.png`; that older capture showed a
mostly black composition with a small Hibr panel at the lower-left. The later
`diagnostics/live-358-layout/` capture has the complete `240x135` cell grid,
but its visible scene is Hibr's animated coloured-dot content rather than a
static desktop reference image. The surface-layout gate therefore remains
partial rather than accepted. This is now a visual composition/presentation
investigation, not the former incomplete Hibr surface. A final visual gate
must correlate pane bounds, cell coordinates, screenshot pixels, pointer
damage, and physical evdev events in the same timestamped window.

The live API pointer control capture produced five distinct screenshots. Pixel
deltas were confined to the old and new cursor regions; the API path showed no
cursor trail or full-screen damage. The TCC-built binary also passed the full
terminal conformance suite and a five-second two-session soak (`356` snapshots,
`32` resizes, `53744 KiB` peak RSS). These are API/control-path results, not
physical mouse acceptance: the real-host physical capture still observed zero
evdev events.

The exact TCC-built `0.1.358` binary was rerun through the VKMS QEMU fixture:
the two-output capture returned `KMS_CAPTURE_PASS`, and the four-output fixture
returned `KMS_FOUR_HIBR_PASS` with one primary Hibr desktop plus three
`desktop --join` clients. The four virtual outputs independently reported a
240 Hz active mode and four KMS presentations; the guest powered down cleanly
and no QEMU process remained. This is virtual KMS/topology evidence, not
physical monitor or sustained physical 240 Hz acceptance. The dedicated
240-frame pixel-stream gate remains open.

The new TCC-built full-surface pointer stress gate also passes. It submits a
`120x40` opaque coloured terminal scene in bounded deferred chunks, commits
updates while moving the pointer, checks six untouched screenshot samples for
black holes, and requires both the pointer-cache and pointer-plus-damage
full-redraw trace paths. This closes the deterministic compositor regression
boundary; it does not close physical mouse acceptance.

The exact `blit.pixel_stream_240=1` gate was rerun on `0.1.358`: all `240`
frames were accepted without frame errors, but synchronized KMS produced only
`83` visible presents and recorded a `16.661 ms` maximum KMS present. The
diagnostic-only `blit.kms_async=1` comparison produced `85` presents, so async
page flips do not solve the bottleneck and remain unsuitable as a production
substitute for atomic presentation. No scheduler change was retained from
this experiment.

The same fixture's cadence matrix is explicit: `blit.pixel_stream=1` passed
with `60/60` visible presents and `KMS_CAPTURE_PASS`; the `120` and `240`
variants accepted every input frame but failed their visible-present count
under VKMS/QEMU. The current validated boundary is therefore the 60 Hz virtual
stream, while 120/240 Hz remains an open KMS timing requirement rather than an
API or terminal-state failure.

The exact current Hibr VKMS input fixture also passed on `0.1.358`: layout
matrix, keypad and keyboard PTY delivery, multi-mouse routing, WebSocket input,
gamepad discovery/input, and raw evdev input all passed. The raw evdev probe
recorded `728` events, `431` motion events, and `34` button events. This is
QEMU input evidence; physical host mouse/button acceptance remains open.

## Current 0.1.357 release and live-validation gate

The TCC-built `0.1.357` package contains the attach-time PTY notification
transaction from ADR 0033. A controlled live resize experiment established the
causal test: a fresh Hibr desktop initially attached at `240x25` populated only
the startup rows; changing the authoritative surface to `240x135` caused all
`32400` cells to become available. A fresh desktop attached directly at
`240x135` reproduced the partial-row state when only same-size reassertion was
used. The release therefore forces one real PTY geometry transition after the
bounded Hibr descendants are identified.

Local TCC pointer-damage, external-pointer-composition, and hidden-cursor gates
pass for `0.1.357`. The package is built and pushed, but the live host has not
yet been upgraded and this release is not marked live-accepted until the API
capture confirms rows `0..134`, stable screenshots, and runtime timing. Physical
mouse/button, physical multi-monitor, and sustained physical high-refresh gates
remain open.

## Current 0.1.356 live and QEMU evidence

The live host now reports `0.1.356`. The SDL raw-evdev pointer feedback loop
was removed: raw evdev updates the Blit software cursor directly and no longer
warps SDL's native pointer into an asynchronous second motion stream. TCC
pointer-damage, external-composition, and hidden-cursor gates pass after the
change. API-injected live motion reached five pointer-cache renders with zero
missed deadlines; physical pointer acceptance remains open because no physical
event has been observed through the live API capture.

The canonical Hibr QEMU input soak was rerun with the 0.1.356 binary and
current Hibr staging. It passed `BLIT_INPUT_RENDER_SOAK_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`,
`BLIT_INPUT_MULTI_MOUSE_PASS`, and `BLIT_INPUT_RAW_EVDEV_PASS` with `914`
events, `546` motion events, and `40` button events. The same run passed the
two-output KMS screenshot checks. No QEMU process remained after shutdown.

The current four-output Hibr fixture also passed `KMS_FOUR_HIBR_PASS` with
four independent VKMS outputs, one primary Hibr desktop, and three
`desktop --join` clients reaching four live surfaces and four-output KMS
presentation. This proves virtual topology/orchestration for 0.1.356; the
fixture recorded initial deadline misses under the synthetic 240 Hz workload,
so it is not evidence of physical high-refresh acceptance.

The dedicated mixed 60/240 Hz scheduler fixture passed
`REFRESH_SCHEDULER_240_PASS` with the 240 Hz output reporting a 4.166 ms
target interval, plus the KMS capture and API screenshot checks. This proves
refresh-mode discovery and scheduler selection in VKMS; startup composition
misses remain visible in the fixture and physical sustained high-refresh
acceptance remains open.

The TCC-built 0.1.356 binary also passed the public PTY conformance suite:
VT500 controls, fragmented UTF-8, wide/combining cells, colors, alternate
screens, synchronized output, Kitty graphics, Hibr defaults, and input/report
queries all passed. A two-session terminal soak passed for five seconds with
`346` snapshots, `32` resizes, and `53580 KiB` peak RSS under a `131072 KiB`
limit, with clean session and render-queue teardown.

The authorized host reports `0.1.356`. A fresh ordinary Hibr desktop started
through `POST /v1/terminal-sessions` reached the authoritative `240x135`
surface. Restarting Blit intentionally terminates its child Hibr process; the
primary desktop was restored through the documented API launch contract after
deployment. Hibr source was not modified.

Physical pointer/button acceptance, multiple physical monitors, and physical
high-refresh acceptance remain open.

## Historical 0.1.355 real-host Hibr handoff evidence

The authorized host reports `0.1.355`. A fresh ordinary Hibr desktop started
through `POST /v1/terminal-sessions` reached the authoritative `240x135`
surface with `32400` non-empty cells automatically. The evidence is in
`diagnostics/live-355-handoff-1/`; six synchronized `1920x2160` screenshots
have identical hashes, and the runtime reports a 60 Hz display, a 16 ms
refresh-aware target, zero missed presentation deadlines, and no renderer
errors.

The cause of the earlier partial desktop was a Hibr hold-process handoff:
the exported identity could identify the owner, but the desktop child lived
in a descendant process group. Blit now uses a unique session token, follows
only that bounded descendant tree, and performs the delayed PTY resize plus
the proven negative-PID `SIGWINCH` operation. Hibr source was not modified.

The same TCC-built binary passed the VKMS QEMU boundaries using the 6.19.14
test kernel: `KMS_CAPTURE_PASS` for two outputs,
`KMS_FOUR_OUTPUT_PASS` with independent red/green/blue/yellow output pixels,
and `KMS_FOUR_HIBR_PASS` with one Hibr primary plus three `desktop --join`
clients. These are virtual KMS/topology acceptances; physical multi-monitor
and physical high-refresh acceptance remain separate open gates.

The current-Hibr QEMU input soak also passed: UTF-8 keyboard output,
multi-pointer/button/wheel routing, WebSocket input stream, render soak,
gamepad discovery, and raw evdev delivery (`914` events, `546` motion, `40`
buttons). Physical input acceptance is still open because virtual evdev does
not prove the particular host's physical devices or user-visible click path.

The normal current-Hibr QEMU integration fixture also reaches
`ACCEPTANCE_PASS`. Its external `console-blit` probe, native primary and join
sessions, input probes, and surface checks all complete after the fixture
passes the staged adapter path explicitly. This closes virtual production
adapter acceptance; the physical host remains on the native terminal path and
does not yet claim physical adapter acceptance.

Physical pointer/button acceptance, production adapter acceptance, multiple
physical monitors, and high-refresh physical acceptance remain open; this
capture records input inventory but no physical user event.

## Current 0.1.345 real-host boundary evidence

The authorized host reports `0.1.345` and the reproducible capture is in
`diagnostics/live-345-snapshot/`. The SDL3 output is `1920x2160` at `60 Hz`,
uses the refresh-aware `16 ms` interval, and recorded zero missed deadlines
over the capture window. The screenshot API returned two identical valid
`1920x2160` BMPs, proving that framebuffer capture and presentation are
observable independently of the user's visual report.

The remaining rendering defect is now isolated to the Hibr handoff: the live
surface is `240x135`, but only `5659` cells are populated and the desktop
cursor remains at row `23`. The PTY and Hibr held-client reports both say
`135x240`; Hibr's `desktop` command creates a detached `hold` owner, so a
resize signal sent only to Blit's attach client cannot resize the actual held
desktop state. Blit's bounded retry does not change that result. This is not
currently evidence of an SDL frame-rate failure or a missing physical mouse
event.

The capture recorded zero physical evdev events because no physical movement
or click occurred during that interval. Physical input acceptance remains
open and must be tested with the capture running. The production adapter path
also remains open: its stream connects, but its surface received no cell
commit in this host run, so it is not being called accepted.

## Historical 0.1.341 real-host troubleshooting status

The 0.1.339 packaging correction keeps the optional `console-blit.so` provider
out of `/usr/lib/hibr`, so the ordinary `/usr/bin/desktop` path selects Hibr's
native console provider. The authorized host now reports `0.1.341`; a fresh
ordinary desktop starts with `5659` non-empty cells and its Hibr log reports
`desktop: started` without `console-blit` flush failures. Pointer and keyboard
routes are configured through the API.

The live timing capture is now topology-aware and records session details,
surface summaries, runtime timing, filtered traces, input inventory, and route
state in one directory. The latest four-second capture recorded zero missed
deadlines and zero physical evdev events; physical mouse acceptance therefore
remains open until movement is observed on the real machine. The SDL API
framebuffer snapshot still returns `screenshot_failed` on this host and is
not claimed as accepted; this is a separate capture-backend issue from the
clean native Hibr terminal path.

## Current 0.1.338 Hibr ABI packaging status

The packaging build now requires an explicit `BLIT_HIBR_ROOT` and has a TCC
`adapter` target. This prevents a stale `console-blit.so` from being copied
into a package after Hibr's headers or modules have changed. The 0.1.337 live
deployment exposed this gap: its Hibr process ran and produced PTY bytes, but
the external surface stayed empty. The corrected package must be rebuilt and
deployed against the current Hibr checkout before Hibr rendering is accepted.

## Current 0.1.337 atomic pointer-damage status

The renderer now treats pointer state plus concurrent external terminal damage
as one complete scene boundary. Pointer-only motion continues to use the
cached scene; when both pointer state and surface damage are present, Blit
redraws the complete scene after clearing the target. This prevents the black
holes, trails, and blinking caused by clearing an SDL target and then drawing
only the damaged rectangle.

The deterministic `POINTER_DAMAGE_ATOMIC_PASS` gate queues pointer input and an
external commit together, verifies the `pointer_dirty=1` full-redraw trace,
and checks framebuffer pixels outside the changed cell. The exact TCC gates
remain required before packaging. Physical mouse, multi-monitor, and
high-refresh acceptance remain open; Hibr source was not modified.

## Current 0.1.336 diagnostic-window status

The runtime now exposes a completed bounded event-loop timing window through
`loop_gap_window_ms`, `loop_gap_window_sequence`, and
`loop_gap_window_max_ms` in `GET /v1/diagnostics/runtime`. The sequence advances
once per completed 500 ms interval, so a capture can correlate a user-visible
artifact with a known observation interval instead of inferring timing from
unrelated lifetime counters. Existing lifetime and last-sample counters remain
unchanged for compatibility.

The local TCC server passed terminal conformance, frame-rate, HTTP
backpressure, session-lifecycle, and slow-reader screenshot checks with the
new fields present. The exact package passed the package artifact gate and is
deployed on the authorized physical host; `/v1/version` reports `0.1.336`.
The live timing-only capture recorded 49 frame advances in five seconds, zero
missed deadlines, a 500 ms completed loop window, and a 16.989 ms window
maximum gap. The primary Hibr session and focused pointer route were restored
through the API. The physical mouse, multi-monitor, and high-refresh
acceptance gates remain open; Hibr source was not modified. A screenshot-heavy
pointer probe was stopped after its 16 MB framebuffer transfer exceeded the
deliberate 30-second diagnostic transfer bound; this confirms that screenshots
must be taken outside the timing/input interval.

## Current 0.1.335 screenshot-delivery status

The source change for 0.1.335 moves framebuffer screenshot transmission out of
the SDL/KMS event loop. Encoded bytes are queued per HTTP client and drained
only on writable readiness. The local TCC binary passed the glyph-render gate
and the slow-reader screenshot gate: while a deliberately throttled
framebuffer download was in progress, `/v1/version` completed in 12 ms.
This directly protects input and presentation from a slow diagnostic client.

Package `blit_0.1.335-1` is built; its externally recorded SHA-256 and source
commit are in [the 0.1.335 release manifest](https://git.home.arpa/itdlabs/blit/src/branch/main/releases/0.1.335.yaml).
The hash is intentionally kept outside this ledger because the ledger itself
is included in the Debian package.
The exact TCC binary passes terminal conformance, frame-rate, HTTP
backpressure, session-lifecycle, and the slow-reader screenshot gate. The
The package is deployed on the authorized physical host and `/v1/version`
reports `0.1.335`; the primary Hibr session and focused pointer route were
restored. The real-host slow-reader gate kept `/v1/version` responsive in
96 ms while a deliberately throttled screenshot was downloading. The runtime
then reported zero missed deadlines and a 39.572 ms maximum loop gap. The
latest physical sample still reports zero raw evdev mouse events, so this is
not a physical-mouse acceptance result. Exact 0.1.335 QEMU/KMS gates remain
now pass. The exact staged TCC binary SHA-256 was
`ec8657f8cea3adb2deb9507c56a9791b51b6fb5264630606739174c0a58bbd1f`.
The normal VKMS run passed `PPM_DISPLAY_47_WIDE_KITTY_PASS`,
`PPM_DISPLAY_48_PASS`, `KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.
The mixed, high-refresh, sixel, and pixel-stream variants passed
`REFRESH_SCHEDULER_MIXED_PASS`, `REFRESH_SCHEDULER_240_PASS`,
`PPM_DISPLAY_47_SIXEL_PASS`, `PIXEL_FRAME_RATE_PASS`,
`KMS_API_PIXEL_PASS`, and their corresponding capture/API markers.
The exact Hibr QEMU fixture passed `LAYOUT_MATRIX_PASS`,
`HIBR_PRIMARY_RESTART_PASS`, `HIBR_JOIN_RECONNECT_PASS`, and
`HIBR_PANE_MATRIX_PASS` with staged Hibr SHA-256
`f8dbc10779193c27d2eb47f0e858cf656912f8b96f56a45af820ad05d18cdb7e`.
Hibr source was not modified.
The exact 0.1.335 input-only QEMU fixture also passed
`BLIT_INPUT_MULTI_MOUSE_PASS`, `BLIT_INPUT_KEYPAD_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_DISCOVERY_PASS`,
`BLIT_INPUT_GAMEPAD_PASS`, and `BLIT_INPUT_RAW_EVDEV_PASS`. Those are
deterministic uinput/evdev results and remain distinct from physical-host
mouse evidence.
The exact 0.1.335 TCC binary also passed a 20-second four-session PTY soak
with 2,584 snapshots and 235 resizes; maximum RSS was 66,776 KiB against the
131,072 KiB limit, and all sessions drained cleanly.

## Current 0.1.334 diagnostic-observability status

The source change for 0.1.334 makes diagnostic observation independently
bounded: `GET /v1/diagnostics/trace` returns the 32 newest matching events by
default and accepts `limit=1..128`. A TCC-built local server returned exactly
one event for `kind=render_path&limit=1`; the version endpoint reported
`0.1.334`. This prevents a trace read from becoming an unnecessarily large
single-threaded HTTP response while investigating live pointer artifacts.

Package `blit_0.1.334-1` is deployed on the authorized physical host;
`/v1/version` reports `0.1.334`. The bounded live trace check returned one
event in 306 bytes in 41 ms. Hibr was restored through the API as the primary
external session and the focused evdev-pointer route was restored.

The live synthetic pointer transaction then injected four motions and captured
the framebuffer after each one. It observed four input events, zero missed
deadlines, stable `incremental-damage` paths, and `graphics_changed=0` for
every motion. The five framebuffer hashes differed as expected for cursor
movement. This does not prove physical evdev correctness, but it does not
reproduce the reported artifact through Blit's API input path; the physical
capture must now correlate raw evdev counters with the same render and pixel
evidence.

The exact 0.1.333 Hibr QEMU restart fixture completed with
`LAYOUT_MATRIX_PASS`, `HIBR_PANE_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`,
and `HIBR_JOIN_RECONNECT_PASS`. The staged Hibr executable SHA-256 was
`f8dbc10779193c27d2eb47f0e858cf656912f8b96f56a45af820ad05d18cdb7e`;
Hibr source was not modified, and no QEMU process remained after the fixture.

The authorized physical host reports one 60 Hz output, zero missed deadlines,
and millisecond-scale rendering. The latest physical sample still reports zero
physical evdev mouse events. That is not a physical-mouse pass: the remaining
live step is to capture while the real mouse is moved and correlate raw evdev
receipt, input routing, pointer state, render-path trace, and post-interaction
pixels using `docs/diagnostic-capture.md`.

## Current 0.1.333 live diagnostic status

The 0.1.333 package is deployed on the authorized physical host at
`192.168.10.54:8080`. A disposable Hibr image probe completed with exit code
zero and was then deleted. The live API reported one complete `api` graphics
object (`512x512`, `786432` bytes), `pixel_frames_accepted=1`, and
`pixel_frame_errors=0`. This proves the packaged Blit-owned Hibr adapter can
submit and retain a pixel frame without changing Hibr.
Package SHA-256 is
`0bc069e6245844d85764e1cfd6b0d69ae3ab8bbc172e75ff484db062e8f14cf3`.

The same live sample reported `last_render_ms=4.411`, `max_render_ms=30.148`,
`missed_deadlines=0`, and a 60 Hz display target. Its `input_events` count was
zero because no physical movement occurred during that capture. Therefore it
does not claim that physical pointer movement is correct. The required next
evidence is `tools/diagnostics/verify-live-physical.sh` run while reproducing
the pointer movement; its exit status 2 means the capture was valid but no
physical movement was observed, while status 1 identifies an actual invariant
failure.

The 0.1.331 TCC binary also passes the two-output VKMS capture fixture with
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. The mixed-refresh and
high-refresh variants pass `REFRESH_SCHEDULER_MIXED_PASS` and
`REFRESH_SCHEDULER_240_PASS`. These are deterministic VKMS/QEMU results, not
claims about physical 120/240 Hz hardware.

The live runtime now exposes raw physical-mouse receipt counters alongside
post-routing `input_events`: `evdev_mouse_events`, sync, motion, wheel, and
button counters, plus the last evdev path and event age. This closes the
diagnostic distinction between a kernel event that was read but did not change
the pointer and a device that produced no records during the capture.
It also reports SDL mouse events discarded or processed. A nonzero discarded
SDL count with unchanged raw-evdev counters identifies backend delivery or
competing input ownership rather than a compositor failure.
The exact 0.1.332 TCC binary passes the full terminal conformance suite,
including VT500 controls, UTF-8/wide glyph handling, Hibr defaults, Kitty
image placement, synchronized output, and network terminal input.
The exact 0.1.332 binary also passes the two-output VKMS capture with
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.
Its direct-KMS 60-frame pixel-stream workload also passes
`PIXEL_FRAME_RATE_PASS`, `KMS_API_PIXEL_PASS`, and `KMS_CAPTURE_PASS`; all 60
frames were accepted with zero additional missed deadlines in the stream
workload.
The exact binary's sixel fixture passes `PPM_DISPLAY_47_SIXEL_PASS`,
`PPM_DISPLAY_48_PASS`, `KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.
Its mixed-refresh and high-refresh scheduler fixtures pass
`REFRESH_SCHEDULER_MIXED_PASS` and `REFRESH_SCHEDULER_240_PASS` respectively.

The 0.1.332 live synthetic probe passes with zero missed deadlines. With the
physical evdev node present but idle, one SDL mouse event was accepted through
the bounded fallback and reported as `sdl_mouse_fallback_events=1`; this is
controlled evidence that the fallback is reachable, not physical-mouse
acceptance evidence.
The subsequent probe after warp-echo suppression reports
`sdl_mouse_fallback_events=0`, confirming that Blit's own cursor warp is not
re-injected as user motion.

The latest live Hibr timing sample contained 128 incremental render paths over
20.778 seconds; the largest path-to-path gap was 1.205 seconds, while the
reported render samples remained in the millisecond range and missed deadlines
stayed at zero. This is damage-driven composition: a static terminal scene is
not presented continuously at the panel refresh rate. `frame_rate_hz` must
therefore be read together with the display's `refresh_hz`, render timings, and
deadline counters; it is not evidence that the renderer itself took seconds.

The 0.1.333 package adds the read-only OpenAPI
`GET /v1/surfaces/{surface_id}/summary` endpoint. The live Hibr surface
returned a compact response of 214 bytes with the current observed revision
`111633`, dimensions `240x135`, and 32,400 non-empty cells; the full `/cells`
repair endpoint remains separate.

The exact 0.1.329 binary with the unchanged Hibr checkout also passes the
QEMU/VKMS Hibr pane matrix: `LAYOUT_MATRIX_PASS` and
`HIBR_PANE_MATRIX_PASS`. The restart variant additionally passes
`HIBR_PRIMARY_RESTART_PASS` and `HIBR_JOIN_RECONNECT_PASS`; the staged Hibr
executable SHA-256 was
`6cec6189cde303a030d6bf8e3936d5c03e2914a47e6c2e80765f62b0cb53356a`.
Hibr source was not modified.

The 0.1.329 input fixture passes `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_KEYPAD_PASS`, `BLIT_INPUT_STREAM_PASS`, and
`BLIT_INPUT_GAMEPAD_PASS`; its new raw-evdev assertion passes with
`events=680`, `syncs=241`, `motion=399`, and `buttons=34`. This confirms the
diagnostic counters against deterministic uinput devices, while remaining
distinct from physical-host mouse evidence.

## Current 0.1.326 live diagnostic-boundary status

Package `blit_0.1.326-1_amd64.deb` is deployed on the authorized host;
`/v1/version` reports `0.1.326`. Package SHA-256 is
`fbbafb832d0465f14b8bd38f2df596bea333c8cd4e41fbb9b22bf6b9431aec8d`.

The live timing-only input probe passed API motion, both button edges, input
dispatch accounting, pointer position, and render-path evidence. After the
initial scene transition, the pointer workload stayed on
`incremental-damage` with `graphics_changed=0`, `graphics_images=0`, a stable
graphics signature, and zero missed refresh deadlines. The capture mechanism
now records the session graphics list beside each trace. Screenshot captures
are explicitly excluded from timing conclusions because the screenshot route
forces a fresh scene and can block the single-threaded loop during transfer.

The fix is Blit-only: an empty graphics scene no longer incorporates session
membership into its graphics signature. Hibr source remains unmodified.

The exact 0.1.326 TCC binary also passes the two-output VKMS capture fixture:
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. The mixed-refresh and
high-refresh variants pass `REFRESH_SCHEDULER_MIXED_PASS` and
`REFRESH_SCHEDULER_240_PASS`. The fixture used the checked VKMS kernel
`/tmp/linux-vkms-build/arch/x86/boot/bzImage`, SHA-256
`17ffba47e1e21d902c71c331bca474fb2de1209f03176d316b333d488bd3fcea`.
The dedicated sixel variant also passes `PPM_DISPLAY_47_SIXEL_PASS` and
`KMS_CAPTURE_PASS`.

The exact 0.1.326 Hibr VKMS fixture also passes `LAYOUT_MATRIX_PASS`,
`HIBR_PANE_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`, and
`HIBR_JOIN_RECONNECT_PASS` with four active Hibr surfaces across the two
outputs and a final `2x2` pane topology. The staged Hibr tree was unchanged;
its executable SHA-256 was
`c2f6522354394bdb443d00d15ec0eb425009c07115fd105f98270eb5c0b040ef`.
The exact-version input-only fixture also passes
`BLIT_INPUT_MULTI_MOUSE_PASS`, `BLIT_INPUT_STREAM_PASS`,
`BLIT_INPUT_GAMEPAD_PASS`, and the keypad/PTTY delivery checks.

## Current 0.1.320 live graphics-probe status

Package `blit_0.1.320-1_amd64.deb` adds the Blit-owned, non-Hibr-source probe
`/usr/share/blit/probes/blit-live-image-probe.hibr` and is deployed on the
authorized host. Package SHA-256 is
`f79f7263bc9bf6c6d79e5aef9dc190d5196a4084b099f5c317533217d0b7e417`.

The live API pixel path passes independently: the TCC-built WebSocket probe
returns `PIXEL_FRAME_PROBE_PASS`; the real host reports one accepted frame,
one texture upload, zero pixel-frame errors, and an empty pending queue. A
1920x2160 live SDL screenshot contains the expected 32x32 red probe image at
the translated surface origin `(20,20)`.

The live input probe also passes on 0.1.320. An API-launched Hibr probe was
attempted with both `hibr -c` and the packaged script; both entered Hibr's
desktop startup path and produced desktop cells but no image upload. This is
recorded as an unresolved Hibr invocation/configuration boundary, not a Blit
graphics failure. Hibr source remains unmodified.

The exact 0.1.320 TCC binary also passes the QEMU VKMS primary/`--join`
restart fixture: `LAYOUT_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`,
`HIBR_JOIN_RECONNECT_PASS`, and `HIBR_PANE_MATRIX_PASS`.

## Current 0.1.322 live composition status

Package `blit_0.1.322-1_amd64.deb` is deployed and `/v1/version` reports
`0.1.322`. Package SHA-256 is
`370abcffdae41bb5556348490ed4dccd4450dbc4e7cce9a32e33029bcec70ae8`.
The authorized host reports a ready SDL3/OpenGL ES 2 renderer, a 60 Hz
1920x2160 active mode, refresh-aware 16 ms scheduling, and zero missed
deadlines.

The primary Hibr desktop was restored through the API as external session 1
with `display-1`, focused input, and the `live-pointer-322` evdev-pointer
route. A controlled 30-motion live run produced
`incremental-damage target_changed=1 external_changed=1 graphics_changed=0`
paths rather than full-scene rebuilds; the affected frames measured about
3.65 ms render time and the runtime reported zero missed deadlines. The
controlled HTTP injection rate is not a physical refresh-rate claim. The
temporary pixel probe image is absent from the restored session.

The exact 0.1.322 TCC binary passes the QEMU VKMS primary restart, join
reconnect, and 2x2 pane matrix markers. Hibr source remains unmodified.

## Current 0.1.321 external-pointer composition status

The TCC-built `0.1.321` binary changes the external-surface compositor so
committed cell damage is applied incrementally to the atomic scene cache.
Graphics-scene changes still force a complete rebuild, identified by the
`graphics_changed` diagnostic field. The new
`EXTERNAL_POINTER_COMPOSITION_PASS` gate records pointer-cache hits after an
external cell commit, while the full terminal, graphics-cache, glyph, batch,
frame-rate, and shutdown gates also pass. The package SHA-256 is
`958e000608ae91e73cfd83341774fd7bb48c1b410f49900420517814abaed9df`.

The exact source state also passes the QEMU VKMS primary/`--join` restart and
2x2 pane fixture with `LAYOUT_MATRIX_PASS`, `HIBR_PRIMARY_RESTART_PASS`,
`HIBR_JOIN_RECONNECT_PASS`, and `HIBR_PANE_MATRIX_PASS`. Hibr source remains
unmodified.

The live pointer-artifact transaction reproduced the physical symptom through
the controlled API path. All sampled render traces used
`full-redraw target_changed=1 external_changed=1`; pointer-cache hits stayed
at zero while pointer-cache misses increased. During the four-position sweep
the reported frame-rate window ranged from 0.65 to 3.32 Hz, with zero missed
refresh deadlines. This isolates the current bottleneck to external-surface
recomposition under pointer activity; it does not implicate evdev discovery
or SDL refresh timing. Capture output was kept outside the repository.

## Current 0.1.319 terminal lifecycle status

The TCC-built terminal path now preserves the final parsed PTY frame after a
child exits. `waitpid(WNOHANG)` marks the session stopped but does not clear
its surface before the PTY reaches EOF; explicit API deletion still releases
the surface. This fixes the short-lived `stty -echo; dd` race that previously
made the final `NETWORK` input disappear from snapshots.

The complete terminal-conformance suite passes, including raw and bracketed
network input, final PTY bytes, VT500 controls, fragmented UTF-8, wide cells,
truecolor, alternate screens, Kitty queries, Kitty image placement, and Hibr
integration defaults. Focused TCC gates also pass session write/resize,
hidden-cursor damage, shutdown, glyph rendering, graphics cache, chunked
pixel frames, and the 60 Hz frame-rate workload (54.4 Hz measured, zero
missed deadlines). Package `blit_0.1.319-1_amd64.deb` was built from this
source state; SHA-256 is
`46b4a88b03fc3918deada6d203aa25b9fde36ccc11d18564c2a170e8c8961c4f`.

The package is deployed on the physical host and `/v1/version` reports
`0.1.319`. The live input probe passes again with approximately 0.017 ms
dispatch latency, zero missed deadlines, and the active 1920x2160/60 Hz
output. Physical pointer trails/click behavior, live image activation, and
physical multi-monitor acceptance remain open.

## Current 0.1.318 diagnostic and graphics status

The exact TCC-built server `0.1.318` is deployed on the authorized physical
host at `192.168.10.54:8080`. The live API reports the active SDL output as
1920x2160 at 60 Hz with a 16 ms refresh-aware interval and zero missed
deadlines. The deterministic live input probe passes motion, both button edges,
dispatch accounting, pointer position, and render-path tracing; input dispatch
is approximately 0.016 ms. A non-invasive capture mode was added because the
large screenshot endpoint can block the single-threaded gateway and must not be
used for timing claims.

The local TCC gates pass the 60 Hz frame-rate workload (54.4 Hz measured,
zero missed deadlines), glyph rendering, graphics-cache reuse, fragmented
chunked pixel frames, and session lifecycle. The provider-level Hibr image
probe accepts one chunked image with `pixel_frames_accepted=1`,
`graphics_texture_uploads=1`, and zero pixel-frame errors. The exact current
binary plus unchanged Hibr checkout also passes the QEMU two-output pane
matrix with `LAYOUT_MATRIX_PASS` and `HIBR_PANE_MATRIX_PASS`.

The physical host still reports approximately 1 Hz of Hibr surface damage
because the running desktop is producing commits at that cadence; this is not
an input-dispatch or missed-refresh failure. Its graphics counters remain zero
because this session has not generated an image command, so live wallpaper
transport is not claimed as validated. Physical pointer trails/click behavior,
live image activation, and physical multi-monitor acceptance remain open.

## Superseded 0.1.300 Hibr integration status

The TCC-built server retains the API restart recovery path from 0.1.296 and
adds a focused SDL scene-cache correction. When Hibr changes DECTCEM cursor
visibility, the affected surface is fully redrawn once; ordinary cursor
movement remains incremental. A live 0.1.296 screenshot proved that the API
could report a hidden cursor and blank canonical cells while the old cursor
pixels remained in the cached scene. The new screenshot assertion reproduces
that boundary and passes on the TCC-built 0.1.297 binary.

Local 0.1.297 evidence: full terminal conformance, hidden-cursor pixel and
frame-suppression gate, and frame-rate gate pass. The authorized physical host
was then running 0.1.297; its settled screenshot no longer contained the
stale left-edge cursor artifact, and runtime reported zero missed deadlines at
the active 60 Hz mode. The live 0.1.297 diagnostics then exposed a separate
input ownership defect: KMSDRM SDL and Blit both consumed the same evdev pointer.
0.1.299 makes raw evdev the single pointer owner, hides SDL's native cursor
after each output window is created,
and retains SDL mouse handling only as a no-evdev fallback. This directly
addresses duplicate clicks, two initial pointers, and pointer trails. The
physical host now reports 0.1.299 with the Hibr session running; a further
hit-testing defect was then confirmed: the primary display-attached session
had no pane, so its pointer reports were discarded. 0.1.300 routes display-
attached sessions using their rendered stacked bounds.

## Superseded 0.1.295 Hibr integration status

The current TCC-built server and exact Hibr `0.99.79 abi 16` adapter add an
atomic cell-frame boundary. Hibr flushes are sent as bounded `defer` requests
followed by one `commit`; deferred revisions do not set the renderer pending
state. The focused API gate verified this behavior, and the update-recovery
gate verified that an invalid package can be reset from `failed` to `ready`
through the authenticated API without restarting the gateway.

The 0.1.295 QEMU VKMS stock Hibr fixture completed with `ACCEPTANCE_PASS` and
reported no `flush failed` or `revision_conflict` errors. This is QEMU evidence;
the physical host still needs the service restarted and 0.1.295 deployed before
the live screenshot/overlap claim can be made.

## Superseded 0.1.293 Hibr integration status

The current TCC-built server and exact Hibr `0.99.79 abi 16` adapter were
validated without modifying the Hibr checkout. The two-output VKMS/QEMU stock
desktop fixture completed with `LAYOUT_MATRIX_PASS` and `ACCEPTANCE_PASS`; it
started the unchanged primary desktop and `desktop --join` client, exercised
the control displays, and reported no `flush failed`, `revision_conflict`, or
`console-blit` adapter errors. Both desktop surfaces reported
`cursor.visible:false`, confirming that the adapter now matches Hibr's
zero-initialized hidden-cursor default.

On the authorized physical host, the corrected API request used the schema
field `arguments` (not the non-contract `args` spelling). It produced a real
Hibr desktop on the 1920x2160 display: the surface contained the menu bar in
row 0, a populated row 1, desktop content through row 15, and the control
strip at row 108. The corresponding 1920x2160 API screenshot was captured
after the surface settled. Diagnostics reported SDL3/OpenGL presentation at
60 Hz with zero missed deadlines. This is one-display/60 Hz evidence only;
physical multi-monitor and physical 120/240 Hz gates remain open.

The revision-conflict retry in `console-blit` is retained as a focused
correctness fix: a stale surface revision is refreshed from the structured
409 response and the bounded pending batch is retried without writing the
error back into the same Hibr PTY. The adapter also now starts with the
cursor hidden and only exposes it after an explicit Hibr cursor request. The
package for this source state is `blit_0.1.293-1_amd64.deb`; SHA-256:
`cdb237081f9d2e8d282a1faf8031a419765d7c9fc91044740f495dfa5dbca046`.

## Historical 0.1.289 Hibr integration status

The authorized physical host reports Hibr `0.99.79 abi 16`, while the local
development checkout is `0.99.100`. The `console-blit` adapter was therefore
rebuilt against the exact local `v0.99.79` Hibr worktree without modifying the
Hibr checkout, and package `0.1.289` was deployed to the host. The exact
0.99.79 adapter/server combination passes the QEMU Hibr primary/`desktop
--join` fixture with `ACCEPTANCE_PASS`.

The physical-host `-1`/black screenshot was superseded by the settled `.291`
through `.293` verification above; it remains historical failure evidence, not a
current acceptance result.

The exact 0.1.287 high-refresh fixture also completed with
`REFRESH_SCHEDULER_240_PASS`, `KMS_CAPTURE_PASS`, and the Kitty wide-glyph
capture markers. Physical 120/240 Hz and physical multi-monitor gates remain
open.

## Previous 0.1.287 Hibr integration correction

The exact 0.1.287 TCC server and `console-blit.so` adapter were staged with
the unchanged Hibr checkout into the populated QEMU VKMS initroot. The
two-output Hibr primary plus `desktop --join` fixture completed with
`ACCEPTANCE_PASS`. The corrected adapter handled the full wallpaper write
followed by Hibr's top-bar writes, and the API-launched child diagnostics did
not appear as `desktop:` terminal content. The Debian package
`build/packages-287-final/blit_0.1.287-1_amd64.deb` contains both the server
and adapter; SHA-256:
`6b87137c405d028595c188d05f92890917e3c1a4ab2d561773bd7c9f52b8b265`.

This closes the identified queue-overflow and startup-stderr defects in the
QEMU integration gate. It does not close the separate physical multi-monitor
or physical 120/240 Hz hardware gates.

## Current 0.1.286 live Hibr presentation evidence

On 2026-10-08, the installed 0.1.286 TCC build on the authorized physical
host was exercised through the public API with the unchanged Hibr executable.
The request explicitly created a `240x135` terminal surface for the
1920x2160 `display-1` output. After Hibr startup settled, the surface had
revision 138, 31,993 non-blank cells, and populated cells through row 133;
the API screenshot showed the complete Hibr desktop, including its wallpaper,
top bar, desktop icons, control strip, and pointer. Runtime diagnostics
reported a 1920x2160 viewport at the physical 60 Hz display mode, refresh-aware
16 ms scheduling, and zero missed deadlines in the current frame-rate window.

An immediate screenshot taken while the session was still starting was black;
this is startup timing, not a settled-render result. The live acceptance
procedure therefore waits for the surface revision and populated cell rows to
advance before treating a screenshot as presentation evidence. The session
was deleted through the API and the host returned to zero sessions, surfaces,
streams, and input routes. This is single-display 60 Hz evidence only; it does
not close the physical multi-monitor or physical 120/240 Hz gates. No Hibr
source or installed Hibr file was modified.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The current source candidate is `0.1.300`. It builds with TCC and passes
  the complete terminal conformance suite, SDL screenshot/overlay pixels,
  the hidden-cursor damage gate, and the complete Hibr font manifest gate.
  It includes the versioned network terminal-input endpoint at the canonical
  PTY boundary. Its SDL backend uses a per-output glyph atlas with nearest
  sampling and one-pixel guard borders, suppresses unchanged cell damage, and
  does not schedule frames for hidden cursor movement. The current physical
  host reports 0.1.283 through its version endpoint and exposes actual
  render/viewport dimensions in diagnostics. Its settled Hibr smoke test
  preserved VT500 text, box/block glyphs, combining `Á`, and wide `界`; this
  is one-output evidence, not a multi-monitor or 120/240 Hz claim. The prior
  candidate `blit_0.1.281-1_amd64.deb` was built from its matching binary and passed
  package allowlist, embedded-version, byte-identity, asset-presence, and
  extracted-runtime checks. The final artifact SHA-256 is recorded in the
  package handoff because this ledger is included inside the package itself.
  It is not claimed as a fully validated 240 Hz release: the real-mode QEMU
  stream gate still fails its strict page-flip deadline requirement.

The 0.1.283 source change adds windowed performance diagnostics:
`frame_rate_max_render_ms` and `frame_rate_missed_deadlines`. The frame-rate
gate now accepts `BLIT_GATE_SERVER` and uses those windowed values, so a remote
physical-host measurement cannot be mistaken for the local dummy backend or be
contaminated by an older lifetime maximum. The window sequence counter also
prevents a remote gate from accepting a stale completed window from a previous
workload. The rebuilt Debian package is
`blit_0.1.283-1_amd64.deb`; its SHA-256 is recorded in the external release
handoff because embedding a package's own hash inside its payload is
self-referential.
Its server binary matches the TCC build and the live host reports 0.1.283.

The canonical TCC build entrypoint now has explicit `server` and `helper`
targets. The exact rebuild produced distinct server and root-update-helper
artifacts (`b2d8ec4b...9626be` and `65ebfebf...dee0` respectively); the Debian
packaging allowlist rejects an updater that contains server routes or is byte
identical to the server. The rebuilt package passed this target-separation
check. The helper was smoke-checked to its expected privileged socket bind
boundary; its unprivileged development-shell bind failure is expected because
`/run/blit` is root-owned in deployment.

The 0.1.281 stream-service correction bounds each WebSocket service turn and
parses already-buffered data after `EAGAIN`, preventing a fast graphics client
from starving presentation or spinning on a user-space receive buffer. The
exact package was installed through the supervised live updater. On the real
1920×2160@60 OpenGLES2 host, the no-ACK paced pixel gate accepted 30 frames,
rendered 24, recorded zero pixel-frame errors and zero missed deadlines, and
  returned to zero sessions/surfaces/routes after cleanup. The package SHA-256
  is `2cf122c7e1a9340a5c3e95f5fb3d520d962b34777df4d77d96d598e7109df5b3`.
This is physical one-output 60 Hz evidence; it does not establish physical
120/240 Hz support.

The exact current 0.1.281 TCC binary passed the four-output VKMS Hibr fixture
with one primary and three joins. Its exact 240 Hz pixel-stream fixture also
selected 240 Hz and accepted all 240 frames, but strict presentation measured
117.83 Hz with eight virtual missed deadlines under QEMU TCG, so the fixture
remains scheduler/ingestion evidence rather than a 240 Hz presentation pass.

The installed 0.1.283 host passed the corrected remote terminal frame-rate
gate at 60.382 Hz with a 55 Hz minimum, 31 frames in a fresh 513 ms window,
9,685 terminal bytes, and zero missed deadlines. This is physical one-output
60 Hz evidence, not 120/240 Hz evidence.

The exact 0.1.283 staged binary and unchanged Hibr executable passed the
QEMU/VKMS four-pane fixture: one primary plus three `desktop --join` sessions
were running, each surface reached 1,537 cells, and the guest powered down
cleanly. The exact high-refresh fixture selected the 240 Hz branch and passed
its scheduler gate. Its 60-frame pixel-stream variant accepted all 60 frames,
but strict presentation failed under QEMU TCG with virtual deadline misses;
this remains scheduler/ingestion evidence, not a physical 240 Hz claim.

The exact current 0.1.283 direct-KMS pixel-stream fixture also passed its
standard acceptance path: 60 frames were accepted at a 60 Hz producer cadence,
there were no additional missed deadlines beyond the startup baseline, and
the KMS pixel/screenshot oracles passed for both outputs. The VKMS mode exposed
240 Hz in that run, but the producer remained 60 Hz; this is not a 240 Hz
presentation claim.

The exact current 0.1.283 Hibr input fixture also passed: evdev keyboard and
keypad delivery, pointer motion/buttons with SGR mouse reports, two-mouse
routing, WebSocket input delivery, and gamepad discovery/routing. The PTY
probes received the expected bytes and the guest powered down cleanly.

The authorized physical host also ran the installed Hibr desktop through the
public API on its single DP-1 output. `/usr/bin/desktop` reached `running`,
the surface revision advanced to 69, the API captured a 1920×2160 BMP, and
the active workload measured 59.848 Hz with zero missed deadlines. Cleanup
returned the host to zero sessions, surfaces, streams, and routes with no
diagnostic errors. This is physical single-display 60 Hz evidence only.

The 0.1.281 release-qualification cycle also passed authenticated network
authorization, HTTP fragmentation and back-pressure, session lifecycle and
write/resize, stream replay/snapshot repair, hidden-cursor damage, screenshot
overlay, API shutdown, font manifest, glyph geometry, and full terminal
conformance gates. These checks ran against the exact TCC-built binary; no
Hibr source was modified.

The same live host sustained 59.894 measured frames per second during a
controlled 120×50 full-screen coloured terminal redraw workload carrying
99,433 terminal bytes, with zero missed deadlines. Its maximum observed render
sample was 17.890 ms against the 16 ms target, so this is stable 60 Hz cadence
evidence with a remaining worst-case render-budget issue, not a claim of
unbounded performance.

After deployment of 0.1.276, the physical host ran the installed Hibr desktop
with the `retro_car` theme at a 240×135 terminal surface. The live API reported
100,574 terminal bytes, 91 background batches, 228 background quads, 138
presents, zero missed deadlines, and a maximum complete render sample of
17.272 ms against the 16 ms target. A separate bounded continuous full-screen
terminal workload measured 59.965 Hz over a fresh 500 ms window with zero
missed deadlines; it processed 294,038 cumulative terminal bytes, reached 30
frames in that window, and reported a 20.090 ms maximum complete render sample.
These are physical one-output results. The maximum sample includes the SDL
presentation boundary, so it is retained as a budget signal pending separate
composition/present timing attribution; it is not evidence of a missed display
refresh when the scheduler reports zero missed deadlines.

The focused input/render regression cycle rebuilt the exact 0.1.274 server with
TCC. Complete terminal conformance passed, including VT500 controls, UTF-8,
combining and wide cells, box-drawing, truecolour, Hibr integration, Kitty
graphics, and input-route lifecycle cases. The SDL glyph geometry, nearest
filter, background batching, graphics-cache, hidden-cursor damage, and stream
replay gates also passed. The live host reported the same 0.1.274 version,
60.000 Hz display mode, zero missed deadlines, and zero active sessions after
the controlled inspection. No input or glyph defect was reproduced in this
cycle, so no speculative input change was made.

The follow-up dummy-backend workload passed the pixel-frame gate with all 30
frames accepted and 27 rendered, including fragmented RGBA input and frame
reuse. The frame-rate gate measured 43.758 Hz over a 502 ms window with zero
missed deadlines and a 15.102 ms maximum render sample against a 16 ms target.
Network fragmentation, back-pressure, and authentication gates also passed.
These are deterministic software-backend measurements and do not replace
physical 120/240 Hz evidence.

After ADR-0021 aligned the bounded sixel advertisement with the runtime, the
exact 0.1.274 TCC binary was re-run through terminal conformance; its
`kitty,sixel` capability assertion, lifecycle termination, nearest-filter, and
Hibr font-manifest gates all passed. This confirms the documentation correction
did not diverge from the shipped capability or test contract.

An API screenshot of the live SDL output was also captured with the
display-derived 240×135 surface geometry at 1920×2160. Visual inspection of
the enlarged glyph crop showed clean box-drawing, block, combining, wide-cell,
and colour placement with no adjacent-atlas bleed. Emoji outside the bundled
fixed-cell coverage remains an explicit visible fallback; this is not a claim
of universal Unicode shaping.

### Current 0.1.279 evidence

The 0.1.279 correction makes each SDL display window enter exclusive
fullscreen after it is positioned on its target display. This is required by
the SDL KMSDRM backend for a complete scanout surface; a borderless window
alone can leave a startup-sized composition area even when the display mode is
taller. The versioned diagnostics now expose the actual renderer output size,
viewport size, and render scale per output. The authorized physical host
reported `render_width=1920`, `render_height=2160`,
`viewport_width=1920`, `viewport_height=2160`, and unit scale after the
upgrade.

A fresh Hibr `retro_car` session reached the settled surface revision 138 in
three bounded polls. Its API screenshot was 1920×2160 and had no sustained
black rows; visual inspection showed the wallpaper and desktop content from
the first through the last output row. Capturing before Hibr reached that
revision produced a partial startup frame, so the validation procedure now
waits for the surface revision to settle before treating a screenshot as
render evidence. The session was deleted and the host returned to zero
sessions, surfaces, streams, and input routes with no diagnostics errors.

The installed 0.1.279 physical host passed the API-owned pixel-frame gate with
30 fragmented/reused RGBA frames accepted at 61.034 Hz and zero pixel-frame
errors, and passed the damage-driven terminal frame gate at 44.498 measured
frames over a 516 ms window with zero missed deadlines and a 12.304 ms maximum
render sample against the 16 ms target. These are one-output 60 Hz results;
they do not claim physical 120/240 Hz support.

The exact TCC-built 0.1.279 binary also passed a 12-second four-session SDL
terminal soak with 747 rendered frames at 60.060 Hz, zero missed deadlines,
3.999 ms maximum terminal-processing time, and 17.515 ms maximum complete
render time. Its maximum SDL presentation sample was 17.433 ms, identifying
the display synchronization boundary—not terminal parsing or glyph
composition—as the dominant part of the refresh budget in this dummy
workload. The graphics-cache, background-batch, and SDL glyph gates passed in
the same cycle; the graphics test recorded five cache hits with no repeated
texture uploads, and the background test coalesced the wallpaper-like runs.
These are controlled SDL measurements, not physical 120/240 Hz evidence.

The exact TCC build passed the OpenAPI parse, Hibr font manifest, SDL glyph,
background batching, and graphics-cache gates. No Hibr source was modified.

An additional bounded workload was run against the authorized physical host at
1920×2160 and 60 Hz. It processed 192,855 terminal bytes, submitted 376
background batches and 778 coalesced quads, sustained 60.078 Hz, and recorded
zero missed deadlines. The worst complete render sample was 23.438 ms and the
worst SDL presentation sample was 18.876 ms. The runtime also reported a
684.914 ms cumulative historical maximum loop gap; because that counter is not
reset per workload, it is not attributed to this run. During the observed
workload the current loop gap was 9.759 ms, maximum select time was 13.989 ms,
and maximum SDL event-service time was 0.147 ms. This confirms the physical
60 Hz path is active and measurable while leaving physical 120/240 Hz and
multi-display validation open.

The exact staged 0.1.279 binary (SHA-256
`b75c962524efeb182ff0e83e870c4df02556afb6ffb36535c470c8eb9161e87a`) and the
unchanged Hibr executable passed the four-output VKMS orchestration fixture.
The guest selected 1024×768 at 240 Hz on four connectors, started one primary
Hibr desktop and three `desktop --join` clients, reached four sessions and
four surfaces, and reported `KMS_FOUR_HIBR_PASS`. This is reproducible
multi-output orchestration evidence, not physical multi-monitor evidence.

The exact same current binary and Hibr executable passed the corrected QEMU
input fixture after the staging helper was made to refresh the canonical
`qemu_hibr_init.sh` entrypoint instead of preserving a historical init script.
The non-soak run passed layout, gamepad discovery, keypad, multi-mouse, stream,
and gamepad-routing checks. The 12-second soak passed layout, gamepad
discovery, keypad observation, multi-mouse, stream, gamepad routing, and
render-soak checks, then powered down cleanly. This matters because the stale
init script omitted the gamepad route and could otherwise produce a misleading
partial input result. Hibr source remained unchanged.

With the same staged artifacts, the pane lifecycle fixture passed
`HIBR_PRIMARY_RESTART_PASS`, `HIBR_JOIN_RECONNECT_PASS`, and
`HIBR_PANE_MATRIX_PASS`: one primary desktop and three `desktop --join`
clients were placed across the Blit-owned pane matrix, the primary was
restarted through Hibr's supervisor, and an auxiliary join was reconnected.
The current four-output fixture also passed `KMS_FOUR_HIBR_PASS` with four
presenting VKMS outputs and one primary plus three joins. Both guests powered
down cleanly; Hibr remained unchanged.

The unchanged stock Hibr session entrypoint was also exercised with
`blit.hibr_stock=1` and the four-pane matrix. All four stock Hibr sessions
reached `running`, attached to Blit-owned panes, and the guest powered down
cleanly with no reported Hibr errors. This is stock-entrypoint integration
evidence; it does not replace physical multi-display validation. The acceptance
check now also reads each attached surface: all four sessions advanced their
surface revision and exposed 1,537 cells each. This proves that the stock
entrypoint produced populated terminal surfaces, rather than merely creating
four live processes.
The same assertions passed again with the exact current TCC rebuild after
the build-target correction, and the four-output fixture returned
`KMS_FOUR_HIBR_PASS`; both guests powered down cleanly.

The current 0.1.279 two-output high-refresh fixture passed
`REFRESH_SCHEDULER_240_PASS` and accepted all 240 API pixel frames, but failed
the strict presentation-rate assertion: QEMU TCG presented 165.222 measured
frames per second, recorded 36 missed deadlines, and reached a 19.684 ms
maximum KMS-present sample. This remains an honest virtual-driver limitation;
the result does not establish or disprove physical 240 Hz capability. The
physical multi-display gate returned `PHYSICAL_MULTI_DISPLAY_UNAVAILABLE
displays=1` on the authorized host, so real multi-display and real
120/240 Hz acceptance remain open.

### Current 0.1.277 evidence

The exact TCC-built `0.1.277` binary passed complete terminal conformance,
including raw and bracketed-paste network input, the Hibr font manifest,
SDL glyph geometry/filtering, background batching, graphics-cache reuse, and
the frame-rate gate. The new runtime fields
`last_sdl_present_ms` and `max_sdl_present_ms` separate SDL presentation
synchronization from the existing complete-path render timing. The OpenAPI
schema and conformance gate require both fields. No Hibr source was modified.

The exact staged binary reported `server_version:"0.1.277"` and passed the
two-output direct-KMS visual fixture with
`PPM_DISPLAY_47_VISUALS_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. It was staged with binary
SHA-256 `2a6762189e5ea3c1d8da93c498a4d63b47d699beda728a9e55dfec7cb8eae82d`.

The authorized physical host was upgraded to 0.1.277 and ran a bounded
full-screen workload at its 60 Hz mode. The live API measured 59.861 Hz with
zero missed deadlines, `max_render_ms=17.378`,
`max_sdl_present_ms=17.259`, and returned to zero sessions and surfaces after
cleanup. This confirms that the observed worst-case sample is at the SDL
presentation boundary; it does not claim physical 120/240 Hz support.

The same live host passed the API-owned pixel-frame gate against the installed
0.1.277 service: 30 fragmented/reused RGBA frames were accepted at 61.058 Hz,
with zero pixel-frame errors and 16 refresh-bound presentations during the
bounded test. This validates the game/arcade frame transport independently of
the terminal-cell glyph path; it is still one physical 60 Hz output evidence.

The exact staged 0.1.277 binary and unchanged Hibr checkout also passed the
current one-vCPU QEMU input/presentation soak with
`LAYOUT_MATRIX_PASS`, `BLIT_INPUT_GAMEPAD_DISCOVERY_PASS`,
`BLIT_INPUT_KEYPAD_SOAK_OBSERVED`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`, and
`BLIT_INPUT_RENDER_SOAK_PASS`. The fixture exercised two synthetic mice,
keyboard VT/UTF-8 traffic, three mouse buttons, wheel events, pointer motion,
gamepad buttons and axes, API input routes, and the WebSocket input stream.
It used Blit SHA-256
`2a6762189e5ea3c1d8da93c498a4d63b47d699beda728a9e55dfec7cb8eae82d` and the
same Hibr SHA-256 recorded above, then powered the guest down cleanly. This is
end-to-end protocol and routing evidence for terminal games and API-owned
game input; QEMU TCG timing in this run is not physical refresh-rate evidence.

The exact same 0.1.277 binary was staged into the current one-vCPU VKMS
four-output fixture with the unchanged Hibr executable
(`8903e0c1addf065e54646bfbab9db0e532cc6d028feb5b9def94b32e52c2ba06`). It
reported four 1024×768 outputs at their selected 240 Hz modes, created four
Hibr-backed sessions and surfaces, rendered one primary plus three unchanged
`desktop --join` clients, and finished with `KMS_FOUR_HIBR_PASS`. The run
recorded virtual QEMU deadline misses during startup and is therefore
multi-output orchestration evidence, not physical 240 Hz timing evidence.

An attempted live Hibr reattach also exposed an important test precondition:
the host already had a held `blit` desktop, so a fresh primary invocation
correctly exited with Hibr's "already running" result. Reattaching with
`desktop --session blit --resume` remained running, but the existing held
session's attached surface was blank in the API cell snapshot. This is not
treated as a rendering pass; the held-session lifecycle/state must be reset or
inspected through Hibr before using that host run as current glyph evidence.

That stale state was then inspected and removed through Hibr's own `mod load
hold; hold kill` interface; no Hibr source or installed Hibr file was changed.
A fresh primary named `blit-clean` reached `running`, advanced its surface to
revision 138, populated all 135 rows of the 240×135 cell grid, and produced a
1920×2160 API screenshot with the menu bar, wallpaper grid, desktop icons,
control strip, and pointer visible. A second clean run processed 860,270
terminal bytes, 895 background batches, and 3,350 background quads with zero
missed deadlines. Its complete-path worst sample was 19.027 ms and its SDL
presentation worst sample was 17.259 ms; this is a successful one-output Hibr
render validation, not a physical multi-monitor or high-refresh result.

### Current 0.1.276 evidence

The exact TCC-built `0.1.276` binary passed the complete terminal
conformance suite, including the new network-input capability identity, raw
PTY input, and bracketed-paste framing cases. Session lifecycle, network
fragmentation/back-pressure/authentication, SDL glyph filtering, and the Hibr
font manifest gates also passed. No Hibr source was modified.

The exact same staged binary was run through the two-output direct-KMS QEMU
visual fixture. It reported `server_version:"0.1.276"`, two ready DRM/KMS
outputs, and the checked-in Hibr font, then passed
`PPM_DISPLAY_47_VISUALS_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. This proves current
two-output composition, glyph/colour capture, and API screenshot correctness
in the reproducible VKMS fixture. The fixture selected 240 Hz with a 4.166 ms
target interval, but its startup page-flip workload is not a sustained 240 Hz
timing proof: QEMU TCG recorded virtual deadline misses and a worst-case
startup present span of 89.732 ms. Physical high-refresh acceptance remains
open.

The exact 0.1.276 binary and the unchanged Hibr checkout were also run through
the four-output orchestration fixture using the stable one-vCPU QEMU topology.
The fixture created four VKMS outputs, started one primary Hibr desktop and
three `desktop --join` clients, and reached four live sessions and four live
surfaces before reporting `KMS_FOUR_HIBR_PASS`. The staged Hibr executable
hash was `8903e0c1addf065e54646bfbab9db0e532cc6d028feb5b9def94b32e52c2ba06`.
This validates the Blit-owned multi-display/session orchestration boundary;
the QEMU TCG run recorded virtual deadline misses and is not physical
high-refresh timing evidence.

The HTTP input endpoint is deliberately bounded to 768 data bytes per request
and reports the actual PTY write count. It is an API transport into the
canonical PTY/terminal path, not a second input or terminal implementation.

### Current 0.1.275 evidence

The 0.1.275 network-input implementation was superseded by 0.1.276 after a
small PTY-boundary correction: API and WebSocket input now return a scrolled
surface to live view, and interrupted nonblocking writes are retried. The
public contract is unchanged.

### Current 0.1.274 evidence

The exact TCC-built `0.1.274` binary was rebuilt from the pushed source and
passed `HIBR_FONT_MANIFEST_PASS` (`required=447`, `mappings=1181`),
`SDL_GLYPH_RENDER_PASS` (light/heavy/double box geometry, blocks, and wide
glyphs), `SDL_GLYPH_FILTER_PASS` (`nearest`), dummy frame-rate,
`PIXEL_FRAME_PASS`, complete terminal conformance, background
batching, graphics-cache, hidden-cursor damage, and a 20-second four-session
terminal soak. The soak completed 244 snapshots and 22 resizes with a maximum
RSS of 66,328 KiB.

The SDL atlas now uses a one-pixel duplicated guard border around each bitmap
cell. This closes the adjacent-texel leak that can produce stray strokes when
scaled glyph geometry lands on normalized atlas boundaries. The canonical PSF
font, cell geometry, and direct-KMS renderer remain unchanged.

The exact same binary passed the direct-KMS two-output visual gate:
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.

The exact `0.1.273` binary independently re-ran that two-output fixture and
reported `server_version:"0.1.273"`, two DRM/KMS outputs,
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.

The current source also contains the native-size direct-KMS graphic-frame
copy path, including an alpha-safe packed conversion for opaque API RGBA
frames. The paced dummy pixel gate accepted 30 frames at 60.953 Hz and
rendered 27 of them, satisfying its minimum 24-frame presentation bound; the
burst gate accepted all 30 while coalescing to the newest complete frame. The
two-output VKMS API pixel fixture passed `PIXEL_FRAME_PROBE_PASS` and
`KMS_API_PIXEL_PASS` after this change. The nominal-60-Hz producer stream also
passed `PIXEL_FRAME_RATE_PASS` and `KMS_API_PIXEL_PASS`, accepting all 60
frames with no additional missed deadlines over the two-output startup
baseline. The custom fixture selected its active 240 Hz mode; this validates
the emulator-facing stream contract but does not close the unresolved strict
240 Hz KMS gate or make a 60 Hz run a physical 120/240 Hz release claim.

The exact same binary and current external Hibr checkout passed the dedicated
Hibr VKMS orchestration fixture: `LAYOUT_MATRIX_PASS`,
and `HIBR_PANE_MATRIX_PASS`. The four-output current-source fixture also passed
`KMS_FOUR_OUTPUT_PASS`, presenting four independent KMS outputs and four
attached terminal surfaces.

The exact `0.1.273` binary and unchanged Hibr checkout additionally passed
the input/presentation soak: `LAYOUT_MATRIX_PASS`,
`BLIT_INPUT_GAMEPAD_DISCOVERY_PASS`, `BLIT_INPUT_KEYPAD_SOAK_OBSERVED`,
`BLIT_INPUT_MULTI_MOUSE_PASS`, `BLIT_INPUT_STREAM_PASS`,
`BLIT_INPUT_GAMEPAD_PASS`, and `BLIT_INPUT_RENDER_SOAK_PASS`.
The runtime diagnostics contract additionally exposes `input_events`,
`last_input_dispatch_ms`, and `max_input_dispatch_ms`; the full terminal
conformance gate verified their presence through the versioned API. It also
exposes per-output `last_present_ms`, `max_present_ms`, and `present_calls` so
multi-display presentation bottlenecks are attributable to one output.

The forced-timeout KMS recovery fixture also passed `KMS_RECOVERY_PASS`:
both outputs entered and completed modeset recovery after a one-millisecond
flip timeout, then passed the normal two-output visual and screenshot gates.

The current Hibr VKMS input fixture also passed keypad soak observation,
`BLIT_INPUT_MULTI_MOUSE_PASS`, `BLIT_INPUT_STREAM_PASS`,
`BLIT_INPUT_GAMEPAD_PASS`, `BLIT_INPUT_DEVICE_LOSS_PASS`, and
`BLIT_INPUT_RENDER_SOAK_PASS`. Input routes survived device reconnect when
their target sessions remained live; routes targeting the intentionally
completed soak sessions were correctly disabled by session lifecycle.

The corrected real-mode high-refresh fixture selected 240 Hz from the VKMS
mode itself (`refresh_source:"display-mode"`) and accepted all 240 pixel
frames. Its scheduler metadata gate passed. In the fresh 0.1.274 run, only
85 frames reached presentation in the 240-frame workload (`frame_rate_hz`
169.900); the maximum select gap was 13.277 ms and maximum KMS present span
was 20.859 ms. The diagnostic async page-flip comparison was also red
(168.102 Hz, 14.977 ms maximum select gap, 13.046 ms maximum KMS span), so
async page flips are not an acceptable fix and remain comparison-only.
Therefore 240 Hz presentation is not yet validated and no package claim is
made for it. The remaining bottleneck is QEMU TCG/VKMS scheduling rather than
WebSocket ingestion, terminal glyph rendering, or pixel-frame acceptance.
Changing the VM topology did not produce a usable workaround: four-vCPU
multi-threaded TCG caused the guest kernel to panic in `sched_clock_cpu` before
Blit started, while stable one-vCPU TCG presented only 162.714 Hz with a
17.771 ms maximum select gap and a 47.778 ms maximum KMS span. The two-vCPU
TCG configuration remains the reproducible baseline for this gate.

### Current 0.1.267 release-candidate evidence

The exact 0.1.267 TCC binary passed the strengthened frame-rate gate with
43.418 measured damage-driven frames per second, 15.464 ms maximum render time
against a 16.000 ms target interval, and zero missed deadlines. The gate now
fails if the runtime reports a missed deadline or a render over the active
output budget; this is a scheduler-budget assertion, not a claim that a
60 Hz panel scans at 43.418 Hz.

SDL background geometry now coalesces contiguous equal-color cells into
horizontal runs. The focused 128×48 wallpaper-style gate passed with four
background quads and a pixel-correct screenshot, proving the optimization
without changing cell colors or damage bounds.

The new SDL glyph-render gate passed through the public screenshot endpoint:
light/heavy/double box lines, light vertical geometry, full and lower blocks,
and both halves of a wide glyph all matched their pixel topology. This closes
the previously missing renderer-boundary assertion for deterministic glyph
shapes; it does not claim universal Unicode shaping.

The same exact binary passed the complete terminal conformance suite and the
Hibr font manifest gate (`required=447`, `mappings=1181`). The conformance
vectors include combining marks, wide cells, variation selectors, zero-width
joiners, box drawing, DEC special graphics, cursor state, and malformed
control recovery.

The exact 0.1.267 direct-KMS QEMU fixture passed `KMS_CAPTURE_PASS`, including
two distinct output captures, wide-glyph pixels, API screenshot validation,
and the 929-glyph/1,182-mapping bundled font. The exact four-output Hibr
fixture passed `KMS_FOUR_HIBR_PASS` with one primary and three
`desktop --join` clients and four rendered views. These are VKMS/QEMU results,
not physical multi-monitor evidence; the fixture recorded two virtual
page-flip deadline misses during startup/composition, which remain visible in
diagnostics and are not treated as physical refresh evidence.

The exact 0.1.267 Hibr input fixtures passed layout-matrix discovery,
multi-mouse routing, keypad delivery, stream delivery, gamepad discovery and
button delivery, render soak, and synthetic keyboard/mouse/gamepad loss and
reconnect. The device-loss run preserved surviving input routes and reported
explicit reconnect diagnostics. These are deterministic evdev/QEMU results;
physical pointer/button/wheel behavior remains a separate hardware gate.

The exact 0.1.267 local lifecycle gates also passed authenticated gateway
authorization, HTTP fragmentation/backpressure, supervised update cancellation,
supervised upload idle timeout, and API shutdown. That historical record
predates the authorized 0.1.274 live deployment documented above.

### Current 0.1.265 source evidence

The bundled Hibr font now assigns dedicated deterministic 8×16 glyphs to the
light, heavy, double, rounded, and dashed box-drawing characters, rejects
`.notdef` fallback output, and covers the previously unresolved Hibr symbols
through ordered build-time fonts and four explicit terminal pictograms. The
font manifest gate checks exact bitmap topology independently of the
generator. The 447 required scalars now occupy 353 distinct bitmap shapes;
the previous large `.notdef` alias group is absent.

The exact 0.1.265 TCC binary passed the full terminal conformance suite,
SDL background/cache and pixel gates, network/authentication gates, and the
direct-KMS two-output capture. The latter passed
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS` with the regenerated font;
the guest loaded 929 glyphs and 1,182 mappings.

The authorized live host accepted and restarted on 0.1.265. A controlled
240×135 Hibr desktop workload produced 43,568 wallpaper background quads in
91 batches, with 17.379 ms maximum render time, 6.551 ms maximum terminal
processing time, zero missed deadlines, and no diagnostics errors. The session
was deleted and the live resource counts returned to zero. Its idle
`frame_rate_hz=0` sample is not interpreted as a scanout rate: the changed
content window had expired after Hibr stopped emitting updates.

The follow-up 0.1.265 live Hibr run produced 43,413 wallpaper background
quads in 93 batches, with 17.638 ms maximum render time, 7.189 ms maximum
terminal processing time, zero missed deadlines, and no diagnostics errors.
Its active changed-content sample measured 5.284 Hz; after Hibr became idle,
the rate correctly aged out to zero. Cleanup returned sessions, surfaces, and
input routes to zero.

The exact 0.1.265 direct-KMS fixtures additionally passed the 60 Hz
emulator-like stream (`PIXEL_FRAME_STREAM_SENT frames=60`,
`PIXEL_FRAME_RATE_PASS`, `KMS_API_PIXEL_PASS`), the 240 Hz scheduler branch
(`REFRESH_SCHEDULER_240_PASS`), and the four-output Hibr orchestration
(`KMS_FOUR_HIBR_PASS`) with one primary and three `desktop --join` clients.
The four-output TCG/VKMS run recorded virtual page-flip misses during startup
and Hibr composition; this is retained as virtual-driver timing evidence and
is not a physical refresh-rate claim.

The exact 0.1.265 Hibr/VKMS input fixtures passed `LAYOUT_MATRIX_PASS`,
`BLIT_INPUT_KEYPAD_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, `BLIT_INPUT_GAMEPAD_PASS`, and
`BLIT_INPUT_RENDER_SOAK_PASS`. The device-loss variant additionally passed
`BLIT_INPUT_DEVICE_LOSS_PASS`, including route continuity for surviving
devices and input loss/reconnect events. The staged runtime loaded the 929
glyph / 1,182 mapping font; Hibr source was not modified.

The exact 0.1.265 TCC binary also passed the bounded endurance set: the
120-second four-PTY soak completed 1,460 snapshots and 133 resizes with
66,492 KiB peak RSS under the 131,072 KiB ceiling and clean teardown;
session deletion completed in 146 ms, stream replay repaired a one-event gap,
shutdown exited cleanly, hidden-cursor damage passed, and the screenshot/
overlay pixel gate passed.

The exact 0.1.265 direct-KMS fixture additionally passed the mixed-refresh
branch (`REFRESH_SCHEDULER_MIXED_PASS`) with independent 60/120 Hz scheduler
metadata, and the output-loss branch (`OUTPUT_LOSS_PASS`) with the surviving
second output continuing to capture successfully. Both runs also passed the
wide-glyph, API-screenshot, and complete KMS capture assertions.

### Current 0.1.263 source and physical evidence

The exact 0.1.263 TCC build passes the complete 0.1.262 regression set plus
the runtime stream-telemetry schema gate. The live host reports zero current
stream queue bytes, zero dropped event deliveries, and zero back-pressure
disconnects at idle. During a real Hibr run it reported 58 SDL background
batches and 5,960 background quads, with the same 59.990 Hz display mode and
zero missed deadlines; teardown returned zero sessions, surfaces, and routes.

The exact 0.1.263 binary also passed the current high-refresh VKMS branch:
`REFRESH_SCHEDULER_240_PASS`, `PPM_DISPLAY_47_WIDE_KITTY_PASS`,
`PPM_DISPLAY_48_PASS`, `KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.
The virtual 240 Hz output selected a 4.166 ms target interval; its three
emulated page-flip misses remain virtual timing evidence, not a physical
240 Hz claim.

### Current 0.1.262 source and physical evidence

The exact 0.1.262 TCC binary passes the SDL background-batch screenshot gate,
SDL graphics-cache gate, fragmented/back-pressure network gates, pixel-frame
gate, full VT/UTF-8/Hibr terminal conformance, and the 447-glyph font manifest.
The API now reports `background_batches` and `background_quads` so wallpaper
optimization is directly observable.

On the authorized physical host, 0.1.262 reports one DP-1 output at 59.990 Hz,
refresh-aware 16 ms scheduling, and zero missed deadlines. A real Hibr desktop
run reported 170 background batches and 51,819 background quads, with a last
render time of 16.477 ms and a changed-content rate of approximately 8.21 Hz.
The latter is not the panel scanout rate: Blit presents only when terminal
damage exists, while the display remains synchronized by SDL vsync. The
changed-content rate is recorded as an unresolved Hibr workload cadence, not
claimed as 60 Hz animation.

The same 0.1.262 host accepted 30 API pixel frames from a paced 60 Hz producer
and 30 frames with presentation acknowledgements; the acknowledged run
rendered 29 frames with zero pixel-frame errors. This is physical one-output
game-canvas evidence. A physical second connector and physical 120/240 Hz
hardware remain unverified.

The exact 0.1.262 TCC binary also passed the direct-KMS two-output VKMS capture
fixture: `PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. It reported two outputs at
60 Hz with refresh-aware scheduling and zero initial missed deadlines. This is
current two-output virtual-KMS evidence, not physical multi-monitor evidence.

### Current 0.1.260 source and physical evidence

The SDL glyph atlas now explicitly uses nearest-neighbor sampling. SDL3
defaults texture scaling to linear, which can bleed adjacent atlas glyphs into
scaled cell edges; the new filter gate passes the set/get policy check and the
renderer falls back to its existing per-pixel path if nearest mode is
unavailable. This removes a concrete source of malformed glyph shapes without
changing the canonical font or terminal model.

An isolated live 0.1.260 screenshot of box drawing, block shading, arrows,
common symbols, and combining acute/tilde/ring marks showed those mapped shapes
without replacement `?` cells. Emoji and CJK samples still select the explicit
visible fallback glyph; they are not represented as universal Unicode shaping
support by this fixed-cell PSF renderer.

After the supervised 0.1.260 upgrade, the physical host's existing held Hibr
state was correctly recovered with `/usr/bin/desktop --resume`: the session
reached `running`, reported 59.922 Hz over its active window, and recorded zero
missed deadlines. A bare `desktop` invocation exited 1 because that held state
already existed; this is Hibr's documented lifecycle distinction, not a Blit
rendering failure. Deleting the Blit session then returned the host to zero
sessions, surfaces, routes, errors, and warnings.

The TCC-built 0.1.260 runtime derives a terminal surface's cell geometry from
its authoritative pane or display when the API request omits `surface`. A
request for the physical host's `display-1` therefore created a 240×135 cell
surface for its 1920×2160 mode, eliminating the earlier 120×40 partial-screen
integration mistake. The same request launched the stock `/usr/bin/desktop`
without modifying Hibr.

The SDL renderer now batches atlas-backed glyph quads with
`SDL_RenderGeometry`. On the authorized host's physical 0.1.258 Hibr workload
before this candidate, the API reported an
active 517 ms window at 59.948 Hz, last render 16.420 ms, maximum render
17.234 ms, maximum event-loop gap 17.311 ms, and zero missed deadlines. The
captured 1920×2160 screenshot filled the output and retained the Hibr
wallpaper, desktop icons, menu controls, and pointer without visible geometry
corruption. This is physical one-output evidence at the host's active 59.990
Hz mode; physical multi-monitor and 120/240 Hz hardware evidence remain open.

The full VT/UTF-8/Hibr terminal suite, SDL frame gate, graphics-cache gate,
Hibr font manifest, and OpenAPI derived-geometry check pass from the same
0.1.260 source. The manifest contains 447 required glyphs and 1,181 Unicode
mappings, including exact row-shape assertions for U+2580–U+2588. The
full-screen Hibr workload's measured frame rate is
distinct from the idle diagnostic value: idle servers do not present frames.

The exact 0.1.260 TCC binary was staged into the two-output VKMS/QEMU capture
fixture. It discovered two 1024×768 KMS outputs, passed the known-glyph and
pixel assertions on both screenshots, passed the API screenshot assertion, and
produced `KMS_CAPTURE_PASS`. This confirms the current direct-KMS binary; the
fixture's emulated page-flip timing is not physical high-refresh evidence.

The exact same 0.1.258 TCC binary also passed the four-output VKMS fixture;
the nearest-filter change is SDL-only and does not alter that direct-KMS
path. The fixture discovered and presented four independent 1024×768 outputs,
producing `KMS_FOUR_OUTPUT_PASS`. The QEMU/VKMS run
reported one startup deadline miss per output while its page-flip work took
9–32 ms; this is recorded as virtual-driver timing evidence, not as a claim
of 60 Hz hardware cadence. The high-refresh fixture produced
`REFRESH_SCHEDULER_240_PASS`, but its virtual 240 Hz output recorded three
misses because the emulated page-flip path took tens of milliseconds. It proves
the 240 Hz deadline calculation and API metadata only; physical 120/240 Hz
hardware remains unverified.

The exact 0.1.260 Hibr input fixture then passed `LAYOUT_MATRIX_PASS`,
`BLIT_INPUT_KEYPAD_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, and `BLIT_INPUT_GAMEPAD_PASS`. It exercised four
VKMS outputs, independent keyboard and pointer routing, multiple mice, left /
middle / right buttons, wheel events, held-button motion, and gamepad
discovery. This is deterministic evdev evidence; physical click behavior on a
second real connector remains open.

The exact 0.1.260 four-output Hibr orchestration fixture also passed
`KMS_FOUR_HIBR_PASS`: one primary and three `desktop --join` clients reached
four Hibr-backed presented surfaces. The TCG/VKMS run recorded six timing
misses per output under that workload; this is retained as virtual page-flip
evidence and is not presented as physical refresh-rate evidence.

With Hibr staged from its existing checkout only, the same four-output fixture
then launched one primary Hibr desktop and three `desktop --join` clients.
All four Hibr-backed surfaces reached the running/presented state and the
fixture produced `KMS_FOUR_HIBR_PASS`. The Hibr checkout was not modified;
this remains virtual multi-display evidence rather than physical connector
evidence.

The live 0.1.258 input inventory and route lifecycle were also exercised with
the physical keyboard and pointer devices. All discovered sources were
reported with their aggregate route IDs; a disposable pointer route was bound
to and removed from a PTY session with HTTP 204, leaving zero routes and no
diagnostic errors or warnings.

### Current 0.1.256 source evidence

The input inventory now exposes `route_ids` alongside each physical
`event-*` device ID. This makes the canonical aggregate route identity
explicit to API clients; all discovered sources of a kind remain active and
feed that route. The OpenAPI schema and live-host route lifecycle were
  validated without modifying Hibr. Input routes now also support DELETE;
  the conformance gate proves a removed route is absent from subsequent
  inventory snapshots.

### Current 0.1.254 source evidence

The 0.1.254 TCC build fixes a JSON-scope defect in API cell updates. Each
cell's style object is isolated before parsing, so omitted foreground,
background, and attribute fields cannot inherit values from later cells. A
regression gate proves independent green foreground, red background, inverse,
and unstyled cells. The full VT/UTF-8/Hibr suite, pixel-frame gate, and dummy
frame-rate gate pass after the fix. Direct-KMS and Hibr input fixtures were
also rerun from this changed source and pass. The physical host now reports
0.1.254; its live 60 Hz stream accepted 60/60 frames with zero pixel errors,
zero missed deadlines, and 60 texture uploads. A live style-scope request
confirmed that green foreground, red background, inverse, and unstyled cells
remain independent. The temporary sessions were deleted with HTTP 204.

The authorized physical host also ran the real `/usr/bin/desktop` command
through the API with the stock Hibr session contract. It reached `running`,
advanced to surface revision 24 after 16,095 terminal bytes, rendered with a
17.106 ms maximum sample and zero missed deadlines, and produced a readable
Hibr desktop screenshot containing its wallpaper, icons, and control UI. The
session was deleted with HTTP 204. This is physical one-output Hibr evidence;
physical pointer/button interaction and multi-monitor Hibr evidence remain
open.

The exact 0.1.256 TCC build also passed the Hibr/VKMS input fixture with
`LAYOUT_MATRIX_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, and `BLIT_INPUT_GAMEPAD_PASS`, including keypad,
surface key/pointer, and device-loss diagnostics. The authorized physical host
was left with zero sessions, surfaces, streams, and routes.

The current refresh fixture also passed `REFRESH_SCHEDULER_240_PASS` with a
4 ms deadline on both VKMS outputs, followed by the two framebuffer oracles
and `KMS_CAPTURE_PASS`. This proves the scheduler's 240 Hz deadline branch in
the current source; it is not physical 240 Hz hardware evidence.

The current-source four-output VKMS fixture exposed four independent 1024×768
outputs at 60 Hz, created one terminal session per output, and passed
`KMS_FOUR_OUTPUT_PASS`. The mixed-refresh fixture then passed
`REFRESH_SCHEDULER_MIXED_PASS`, the two-output pixel oracles, and
`KMS_CAPTURE_PASS` with the test scheduler's 60/120 Hz metadata. These are
virtual-output scheduling results; physical multi-monitor and physical
120/240 Hz evidence remain open.

The same current-source four-output fixture was run with Hibr orchestration:
one primary session and three `--join` sessions produced four Hibr-backed
surfaces and passed `KMS_FOUR_HIBR_PASS`. Hibr source was copied into the
guest staging area only; the repository checkout was not modified.

### Current 0.1.253 source evidence

The exact TCC-built 0.1.253 binary passes the full VT/UTF-8/Hibr terminal
conformance suite, the font manifest gate, and the refresh-aware dummy frame
gate. Its bounded binary pixel-frame WebSocket gate accepted 30/30 RGBA
frames, including one fragmented frame, with zero frame errors and reusable
texture storage. The measured dummy ingest rate was 21,353 frames/second;
the renderer coalesced those updates into six frames at a measured 16.468 ms
render time. The direct-KMS QEMU fixture then accepted a complete 4,096-byte
RGBA frame through the network API, found the known red canvas in the
framebuffer oracle through the direct-KMS screenshot endpoint, preserved the
second output, and passed `KMS_API_PIXEL_PASS`, `PPM_DISPLAY_48_PASS`, and
`KMS_CAPTURE_PASS`. This proves the API-to-VKMS pixel path; it does not yet
claim physical 120/240 Hz hardware presentation.

The timed direct-KMS stream variant sent 60 complete RGBA frames at a 60 Hz
producer cadence. It accepted all 60 with zero missed deadlines, passed
`PIXEL_FRAME_RATE_PASS`, and then passed `KMS_API_PIXEL_PASS`,
`PPM_DISPLAY_48_PASS`, and `KMS_CAPTURE_PASS`. This validates the
emulator-facing 60 Hz stream and bounded back-pressure path on VKMS; physical
120/240 Hz hardware evidence remains open.

The pixel path now invalidates only the affected graphics rectangle in cell
coordinates instead of forcing a full terminal-surface redraw. Optional
placement fields default to zero as declared by the OpenAPI schema. The
runtime diagnostics expose `pixel_frames_accepted` and `pixel_frame_errors`.

The authorized physical host was then exercised through its network API with
a disposable external surface and a TCC-built remote WebSocket probe. It
accepted 60/60 RGBA frames from a 60 Hz producer with zero pixel-frame errors,
reported `frame_rate_hz: 59.980`, `last_frame_gap_ms: 17.042`,
`last_render_ms: 16.701`, and `missed_deadlines: 0` at the host's active
59.990 Hz mode. The probe also verified 60 graphics texture uploads. The
temporary session was deleted with HTTP 204 and the host returned to zero
sessions and zero surfaces. This is physical one-output evidence; a physical
multi-monitor and 120/240 Hz result remains open.

### Current 0.1.250 source/package evidence

The exact TCC-built 0.1.250 binary preserves the 0.1.249 terminal and KMS
gates. It adds independent evdev gamepad discovery and a versioned input-route
value, `evdev-gamepad`. The QEMU/VKMS input fixture passed
`BLIT_INPUT_GAMEPAD_PASS`: a synthetic gamepad was discovered, routed to the
selected session, and delivered timestamped axis and button events on the
input stream. Gamepad loss was also reported through the existing diagnostic
event path. No keyboard escape-sequence mapping was invented.

The exact 0.1.250 dummy gates passed `terminal conformance: PASS` and
`FRAME_RATE_PASS`; the direct-KMS capture passed `PPM_DISPLAY_47_WIDE_KITTY_PASS`,
`PPM_DISPLAY_48_PASS`, `KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`.
The package is `blit_0.1.250-1_amd64.deb`; the handoff records its final SHA-256
outside the package because the ledger itself is packaged. This candidate still
does not claim the future high-rate pixel canvas for
graphical emulators; that remains the Milestone 5b implementation.

### Current 0.1.249 source/package evidence

The exact TCC-built 0.1.249 binary and Blit Hibr entrypoint pass the stock
Hibr QEMU acceptance path. The fix loads Blit's `console-blit` provider in
the API-launched Hibr child itself; relying on `HIBR_RC` was invalid because
non-interactive children do not consume it. The two-output fixture passed
`LAYOUT_MATRIX_PASS`, started one primary and one unchanged stock `--join`
desktop, exposed both Hibr control displays, and finished with
`ACCEPTANCE_PASS`. The primary reached surface revision 152 and the join
surface revision 10. Hibr source remained unchanged.

The launcher now defaults Hibr's `DT_TERMMS` to zero unless explicitly
overridden, removing Hibr's 70 ms program-redraw throttle while leaving
Blit's refresh-aware scheduler authoritative. The exact 0.1.249 dummy frame
gate measured 43.989 Hz over a 500 ms window. The full 0.1.249 two-output
orchestration variant also passed
`HIBR_PANE_MATRIX_PASS`: four Hibr clients ran across a Blit-owned 2×2 pane
layout, the primary desktop supervisor restarted successfully, and an
auxiliary `--join` client was killed and reconnected on a new pane. The final
topology contained five attached surfaces. The package is
`blit_0.1.249-1_amd64.deb`; the final artifact hash is recorded with the
release handoff because this ledger is included inside the package itself.

The exact 0.1.249 input/render QEMU soak passed
`LAYOUT_MATRIX_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, and `BLIT_INPUT_RENDER_SOAK_PASS`. It exercised
multiple mice, keyboard and pointer routing, all three buttons, releases,
wheel input, PTY delivery, and synthetic device-loss handling.

The exact 0.1.249 direct-KMS/VKMS capture fixture, staged with the new
TCC-built helper set, passed `REFRESH_SCHEDULER_MIXED_PASS`,
`PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. The staging helper now
builds `pty-input-echo`, `http-put`, and `ppm-assert` from the repository on
each run, preventing incomplete historical initroots from invalidating the
visual result.

The exact 0.1.249 four-output VKMS fixture also passed `KMS_FOUR_HIBR_PASS`:
four independent 60 Hz outputs presented simultaneously with one primary Hibr
desktop and three unchanged stock `--join` clients. Hibr source remained
unchanged.

The exact 0.1.249 direct-KMS graphics fixtures also passed sixel pixel
rendering (`PPM_DISPLAY_47_SIXEL_PASS`, `KMS_CAPTURE_PASS`) and Kitty
z-order/clipping (`PPM_DISPLAY_47_WIDE_STACK_CLIP_PASS`, `KMS_CAPTURE_PASS`).

### Current 0.1.248 source/package evidence

The 0.1.248 provider-bootstrap correction was superseded by 0.1.249 after
the Hibr redraw-throttle fix above.

### Current 0.1.247 source/package evidence

The exact TCC-built 0.1.247 binary passes the focused font fallback gate,
the expanded 441-glyph Hibr manifest gate, and the complete terminal
conformance suite. The fallback gate proves that a valid but unmapped scalar
outside the PSF range selects the visible `?` glyph rather than blank PSF
glyph zero. The manifest now covers Hibr's Arabic source characters and the
242 presentation forms emitted by its `uni` shaping table. The package is
`blit_0.1.247-1_amd64.deb`, SHA-256
`2b19daa4d8efb91dcf827ccff3e9851005cd702a005fac7bcb5d3516b828c9e0`.
With the expanded bundled font selected, the dummy refresh gate measured
43.975 Hz during its bounded workload and the SDL graphics-cache gate passed
with five cache hits and no additional texture uploads.
The physical host has not yet been upgraded to 0.1.247; its accepted runtime
evidence remains explicitly recorded below for 0.1.245.

The exact 0.1.247 TCC binary and bundled font were then staged into the VKMS
QEMU capture fixture. It reported `server_version:0.1.247`, two ready DRM
outputs, `PPM_DISPLAY_47_WIDE_KITTY_PASS`, `PPM_DISPLAY_48_PASS`,
`KMS_API_SCREENSHOT_PASS`, and `KMS_CAPTURE_PASS`. This is new QEMU/KMS
evidence only; it does not upgrade or replace the physical-host evidence.
The same exact 0.1.247 binary passed the single-vCPU synthetic input/render
soak with `LAYOUT_MATRIX_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
`BLIT_INPUT_STREAM_PASS`, and `BLIT_INPUT_RENDER_SOAK_PASS`; the fixture
exercised two mice, all three buttons, releases while buttons remained held,
wheel input, PTY routing, and device-loss diagnostics.

### Current 0.1.245 QEMU/KMS evidence

The exact TCC-built 0.1.245 binary and regenerated runtime font were staged
into the rebuilt VKMS kernel
fixture and passed the two-output capture gate with independent 1024×768
outputs, wide UTF-8 glyphs, Kitty pixels, API BMP capture, graphics inventory,
refresh-aware scheduling, and `KMS_CAPTURE_PASS`. The same binary passed the
following variants without modifying Hibr:

- `REFRESH_SCHEDULER_MIXED_PASS` for independent 60 Hz and 120 Hz schedules.
- `OUTPUT_LOSS_PASS` with the surviving output continuing to present.
- `PPM_DISPLAY_47_WIDE_STACK_CLIP_PASS` for Kitty z-order and clipping.
- `PPM_DISPLAY_47_SIXEL_PASS` for sixel pixel rendering.
- The exact 0.1.245 binary also passed the two-output input/render soak:
  `LAYOUT_MATRIX_PASS`, `BLIT_INPUT_MULTI_MOUSE_PASS`,
  `BLIT_INPUT_STREAM_PASS`, and `BLIT_INPUT_RENDER_SOAK_PASS`.
- A current-version 30-second four-PTY soak passed 364 API snapshots, 34
  revisioned resizes, clean session teardown, advancing frames, no runtime
  errors, and 66,312 KiB peak RSS under the 131,072 KiB limit.
- The SDL graphics-cache gate passed with one initial image upload and five
  cache hits during later terminal damage, with no additional upload.
- `KMS_FOUR_HIBR_PASS` with four presenting VKMS outputs, one primary Hibr
  desktop, three unchanged stock `--join` clients, and four running Hibr
  surfaces. Hibr source remained unchanged.

The QEMU VKMS kernel was rebuilt from the repository's existing Linux VKMS
source/build tree because the prior `bzImage` artifact was missing. The
The authorized physical host accepted package `0.1.245-1` and reports
`server_version:0.1.245`, one 1920×2160 output at 59.99 Hz, SDL/OpenGL
readiness, zero missed deadlines, and no diagnostics errors. A fresh Hibr
desktop capture remains clean and the session advertises Kitty and sixel
capabilities. The expanded 148-glyph font is loaded in the runtime. A
controlled two-surface renderer probe sustained 59.943 Hz with zero missed
deadlines and 15.504 ms last render time against the 16 ms target. After probe
cleanup, Hibr remained the sole active session. Hibr source was not changed.
- The previous fully validated source build is `0.1.222`, which includes the
  Hibr-compatible Unicode 15.1 width table and regression vectors for
  variation selectors and zero-width joiners.
- The current TCC source build and turnkey package are `0.1.240`; the complete
  terminal conformance suite passes with the canonical 16x16 Terminus font.
  The package defaults the Blit service to a UTF-8 locale for Hibr's Unicode
  desktop output.
- The runtime font is a checked-in PSF2 merge of the bundled Terminus font and
  the missing Hibr Unicode desktop glyphs. Its required glyph manifest is
  validated by `tools/build-hibr-font.py` before packaging.
- The exact 0.1.232 TCC binary passed the two-output VKMS/KMS capture gate and
  the Hibr pane matrix with one primary plus three `--join` clients. The same
  staged fixture reports the merged font, two ready DRM outputs, refresh-aware
  16 ms scheduling, and `KMS_CAPTURE_PASS`/`HIBR_PANE_MATRIX_PASS`.
- The exact 0.1.232 TCC build passes the fragmented-UTF-8 vector and a
  controlled Hibr API launch with zero visible question-mark cells. The parser
  retains valid UTF-8 prefixes across PTY reads instead of replacing them.
- The version identity gate also passes when the host contains an older
  installed Blit package: a freshly built 0.1.232 binary reports 0.1.232
  through both version endpoints. Runtime identity comes from the compiled
  binary, not from stale host documentation metadata.
- The Debian artifact built from the current implementation is
  `blit_0.1.232-1_amd64.deb`, SHA-256
  `e6b4e85c56d4761560dd9cd4f7afd8f62fdda564bae3d9c20c004b1b0909c59b`.
- The exact 0.1.232 update supervisor passed cancellation and idle-timeout
  gates. The package installs through real `dpkg --unpack` and `dpkg
  --configure` on the local VM. The authorized physical host accepted the
  package through the API, restarted, and reports 0.1.232 ready with no
  runtime errors.
- The exact 0.1.232 TCC gateway also passed the lifecycle, shutdown,
  fragmented/back-pressured HTTP, authenticated-network, and authenticated
  update-cancellation gates. Session deletion completed in 128 ms with
  responsive diagnostics; unauthenticated requests returned 401, authorized
  requests returned 200, and unauthorized command creation returned 403.
- The exact 0.1.232 TCC build passed the 120-second terminal soak with four
  concurrent PTYs, sustained VT/UTF-8 output, 1,460 API snapshots, 133
  revisioned resizes, no runtime errors, no leaked sessions, and 65,996 KiB
  peak RSS. The reusable gate is
  `tools/tests/terminal_soak_gate.rb`.
- The same exact build additionally passed a 600-second run of that gate with
  four concurrent PTYs, 7,296 API snapshots, 664 revisioned resizes, no
  runtime errors, no leaked sessions, and 65,996 KiB peak RSS under the
  enforced 131,072 KiB RSS ceiling.
- The same exact build additionally passed a 1,800-second run with four
  concurrent PTYs, 21,888 API snapshots, 1,990 revisioned resizes, no runtime
  errors, no leaked sessions, and 66,120 KiB peak RSS under that same ceiling.
- The same exact 0.1.232 TCC build additionally passed a 3,600-second run
  with four concurrent PTYs, 43,800 API snapshots, 3,982 revisioned resizes,
  no runtime errors, no leaked sessions, and 66,112 KiB peak RSS under the
  enforced 131,072 KiB RSS ceiling.
- The same exact 0.1.232 TCC build additionally passed a 7,200-second run
  with four concurrent PTYs, 87,576 API snapshots, 7,962 revisioned resizes,
  no runtime errors, no leaked sessions, and 66,104 KiB peak RSS under the
  enforced 131,072 KiB RSS ceiling. The server exited cleanly after the gate.
- The exact 0.1.232 TCC QEMU input fixture additionally passed its combined
  render/input soak: two KMS outputs continued presenting while the existing
  keyboard, two mice, three buttons, wheel, pointer routes, and WebSocket
  input stream were exercised during approximately ten seconds of bounded
  UTF-8/VT output. It produced both `BLIT_INPUT_STREAM_PASS` and
  `BLIT_INPUT_RENDER_SOAK_PASS`.
- The exact 0.1.232 TCC stream replay gate passed disconnect/reconnect,
  bounded-history gap signaling, replayed terminal output, and the documented
  HTTP surface snapshot repair path: `STREAM_REPLAY_PASS gap=1
  terminal_output=1 snapshot_repair=1 frames=66`.
- The exact 0.1.232 TCC session fault gate passed active PTY termination during
  output and six revision-checked resizes: `SESSION_WRITE_RESIZE_PASS
  writes=1 resizes=6 delete=1 sessions=0`.
- The exact 0.1.232 TCC QEMU input-loss fixture passed deterministic evdev
  fault/reconnect coverage with two KMS outputs: it emitted typed
  `input_device_lost` diagnostics, retained enabled surviving input routes and
  ready diagnostics, and produced `BLIT_INPUT_STREAM_PASS` plus
  `BLIT_INPUT_DEVICE_LOSS_PASS` before powering down cleanly.
- The exact 0.1.232 TCC build passed the two-output KMS pixel/screenshot gate,
  the four-layout Hibr pane matrix with four running sessions, and the
  one-vCPU input fixture. The input fixture passed keypad bytes, independent
  multi-mouse routing, three-button presses/releases, wheel, motion,
  WebSocket stream delivery, and device-loss diagnostics.
- The exact 0.1.232 TCC build also passed the four-output KMS boundary fixture:
  four independent VKMS outputs were discovered and presenting, one terminal
  session was attached to each, and the API reported four displays, outputs,
  sessions, and no diagnostics errors.
- The same four-output fixture additionally passed the Hibr orchestration
  variant: one primary Hibr desktop plus three `--join` clients produced four
  running Hibr-backed surfaces across four presenting outputs, with no API
  diagnostics errors (`KMS_FOUR_HIBR_PASS`). Hibr source was unchanged.
- The exact 0.1.232 TCC build passed the mixed-refresh fixture with independent
  60 Hz/16 ms and 120 Hz/8 ms output schedules. It also passed the output-loss
  fixture: a failed connector was reported as `display_presentation_failed`,
  the surviving connector continued presenting, and its API/PPM capture
  remained valid.
- SDL pointer-only presentation now reuses a per-output scene cache instead of
  forcing a full terminal rasterization on every pointer motion. Runtime
  diagnostics expose pointer-cache hits and misses.
- `POST /v1/control/update/cancel` now cancels a stalled authenticated upload,
  and update status exposes active/received/expected byte counts.
- The package allowlist rejects runtime shell launchers and Hibr startup
  wrappers.
- Hibr source and files were not modified.
- A Debian `0.1.240-1` package built from the current TCC binary and source
  commit `353e051` passed the package allowlist audit. Its SHA-256 is
  `99d22f41935d75abe15bc7bd3fa5d583fd6a73747d937e2f311e20ba16ddeb26`.

### Current 0.1.240 VKMS evidence

The exact TCC 0.1.240 binary staged with the current Hibr runtime passed the
following current QEMU fixtures:

- Two-output direct-KMS capture: two independent 1024×768/60 Hz outputs,
  distinct PPM captures, Kitty pixel assertions, and the API screenshot gate.
- Four-output boundary: four independent outputs at x=0, 1024, 2048, and
  3072, with one running terminal surface per output.
- Mixed refresh: independent 60 Hz/16 ms and 120 Hz/8 ms schedules.
- Output loss: connector 47 was marked failed, connector 48 remained
  presenting, the API returned the typed `display_presentation_failed`
  warning, and the surviving PPM/API capture remained valid.
- Hibr four-output orchestration: one primary Hibr desktop plus three
  unchanged stock `desktop --join` clients produced four running Hibr-backed
  surfaces and four presenting KMS outputs (`KMS_FOUR_HIBR_PASS`).
- Current-version input-loss and reconnect: the 0.1.240 input fixture passed
  keypad bytes, independent multi-mouse routing, stream delivery, typed loss
  and reconnect diagnostics, and route re-enablement after reconnect
  (`BLIT_INPUT_DEVICE_LOSS_PASS`). The fixture explicitly holds the
  reconnecting synthetic devices open so the assertion cannot sample after a
  second intentional teardown.
- Current-version Hibr lifecycle: the 0.1.240 pane/restart fixture passed
  primary supervisor restart, auxiliary `desktop --join` termination and
  reconnection, and the final five-surface topology
  (`HIBR_PRIMARY_RESTART_PASS`, `HIBR_JOIN_RECONNECT_PASS`, and
  `HIBR_PANE_MATRIX_PASS`).

These are QEMU/VKMS results. They prove the four-output software path, not a
physical four-monitor arrangement.

## Passing automated evidence

- OpenAPI and TCC build checks pass.
- The canonical `tools/build-tcc.sh` injects `packaging/VERSION` into the
  executable; the exact 0.1.232 local conformance run reports
  `server_version:0.1.232` with `api_version:0.1`. The live host now reports
  the same version through its TCP API.
- Terminal grids are allocated per live session rather than embedded in all
  session slots. The exact 0.1.222 TCC build passed the terminal suite and a
  100,000-line PTY soak with no diagnostic errors; peak RSS was 25,156 KiB
  versus 11,256 KiB idle in the comparable local run.
- The exact 0.1.222 Debian package contains the byte-identical TCC server
  binary and passes the package allowlist check.
- The exact 0.1.222 Hibr primary/join QEMU fixture passed two-output KMS
  presentation, the Hibr session path, and both PPM pixel assertions.
- The stalled-upload gate passes: progress is exposed and authenticated
  cancellation returns 202 without stopping the gateway.
- Terminal conformance passes, including UTF-8/wide cells, VT controls,
  full Hibr Unicode 15.1 width semantics for combining marks, joiners, and
  variation selectors,
  alternate screens, colors, fragmented and malformed controls, Kitty image
  placement, ordinary-session graphics isolation, and immediate final PTY
  bytes.
- Direct-KMS/VKMS passes two-output pixel capture, mixed 60/120 Hz scheduling,
  output-loss recovery, Kitty resource limits, Kitty clipping/z-order, and
  sixel pixel rendering.
- The exact 0.1.232 capture fixture independently passed the mixed-refresh
  predicate (`60 Hz/16 ms` and `120 Hz/8 ms`) and output-loss predicate
  (failed connector retained, surviving connector presenting, typed diagnostic,
  and surviving screenshot/API capture).
- The Hibr fixture passes primary plus `--join` sessions, all four logical
  pane layouts, input routes, keyboard, pointer, multi-mouse, and WebSocket
  input checks. Against the current Hibr 0.99.86 runtime, the ABI-16 adapter
  probe, Hibr display-control socket, and dedicated window workload also pass:
  `move`, `resize`, and `focus` succeed across the joined display. The stock
  Hibr session intentionally starts with zero application windows; that empty
  `windows` response is not treated as a renderer failure. The guest produces
  both 1024×768 PPMs, and their top-left probes are dark theme pixels rather
  than a red diagnostic square.
- The corrected two-display Hibr fixture passes in both modes: the stock
  primary/`--join` session accepts an empty application-window list, while the
  explicit `blit.hibr_window_control=1` workload additionally passes window
  `move`, `resize`, and `focus` operations. The acceptance predicate now
  distinguishes those two intentional workloads.
- The exact 0.1.221 TCC build passes the two-output KMS pixel gate, the
  four-pane Hibr matrix, and the input-only fixture with
  `blit.input_wait=8`: keypad, multi-mouse, button, wheel, motion, stream, and
  PTY delivery all pass. The input-loss variant additionally reports
  `input_device_lost` diagnostics, retains the configured routes, and passes
  device-loss/reconnect recovery. Its terminal conformance gate additionally passes
  variation-selector and zero-width-joiner cursor-position vectors, plus the
  Hibr `DT_BIDI`/`HIBR_GFX` defaults and explicit override vectors.
- The exact 0.1.221 Hibr input-loss fixture passes primary/join startup,
  `1x1`/`2x1`/`1x2`/`2x2` pane layouts, route retargeting, device-loss
  diagnostics, device reconnect, and the final Hibr acceptance predicate.
- The exact 0.1.232 TCC Hibr restart fixture, staged with Hibr 0.99.92,
  exercises the unchanged stock `examples/desktop/session.hibr` through the
  repository-owned launcher that preloads Hibr's canonical `console`, `pty`,
  `term`, and `hold` modules. Four stock sessions reach `running`; the gate
  kills the Hibr `[desktop]` process and observes
  `HIBR_PRIMARY_RESTART_PASS`, then kills an auxiliary `--join` client, keeps
  the primary running, attaches a fresh join on the next pane, and observes
  `HIBR_JOIN_RECONNECT_PASS` and `HIBR_PANE_MATRIX_PASS`. Hibr source was not
  modified.
- The same exact TCC/QEMU restart gate also passes the repository's dedicated
  Hibr window-control workload against Hibr 0.99.92: four sessions reach
  `running`, the Hibr `[desktop]` supervisor restores the primary after the
  process is killed, and an auxiliary `--join` client can be replaced on a
  fresh pane. It emits `HIBR_PRIMARY_RESTART_PASS`,
  `HIBR_JOIN_RECONNECT_PASS`, and `HIBR_PANE_MATRIX_PASS`; Hibr source was
  not modified.
- The 0.1.240 runtime diagnostics add an explicit measured presentation-rate
  window to `/v1/diagnostics/runtime`: `frame_rate_hz`,
  `frame_rate_frames`, `frame_rate_window_ms`, and `frame_rate_age_ms`, plus
  event-loop/presentation counters for root-cause diagnosis. The local TCC
  frame-rate gate passes at 44.860 Hz with a 512 ms window while a continuous
  PTY producer is active. Event-loop and `select()` timing counters are also
  exposed to isolate host scheduling stalls.
- The real-host one-frame-per-second defect was traced to the synchronous
  periodic evdev reconnect scan: it blocked the gateway for approximately
  1.0–1.03 seconds. Version 0.1.239 replaces that scan with an inotify-driven
  `/dev/input` watch. On the live host, the continuous PTY workload then
  measured 44.657 Hz, 234 present calls, zero missed deadlines, and a 12.548
  ms maximum loop gap.
- Surface and scrollback snapshots preserve complete zero-width UTF-8 cell
  sequences; a five-extension combining vector and its cursor position pass.
- Revisioned API cell updates accept wide UTF-8 glyphs and zero-width
  extensions, expose continuation width metadata, and pass the public API
  regression vector.

## Real-host evidence

The latest reachable observation of the authorized host at
`192.168.10.54:8080` reported:

- Blit `0.1.240`, healthy SDL3/OpenGL presentation, reached through the
  versioned TCP API.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh API-created PTY session reached `running`, emitted `BLIT_BASH_OK`,
  and its surface snapshot contained the expected cells; the session was then
  deleted and resources returned to zero. Live diagnostics report one
  `DP-1` output at 1920×2160 and 59.99 Hz, one keyboard, one pointer, no
  errors, and a 16 ms refresh-aware presentation interval.
- The live host currently exposes one physical output. Multi-display behavior
  is therefore proven by the two- and four-output VKMS/QEMU fixtures, not by
  this host.
- The 0.1.232 local Hibr full-grid workload settled a 240×135 surface in
  approximately three seconds; runtime diagnostics measured 100,264 terminal
  bytes and no pending damage afterward. This is local/QEMU-style evidence,
  not a claim about a physical multi-display arrangement, which this host
  currently does not expose.
- After the 0.1.240 input-scan fix, the installed stock Hibr desktop reached
  261 presentation calls in the live-host validation interval, with a 16.459
  ms last frame gap, zero missed deadlines, and a 17.348 ms maximum loop gap. The
  Hibr session and its resources were then deleted cleanly.
- A later live-host query while Blit was idle reported one active 59.99 Hz
  physical output, a 16 ms target interval, 0.034 ms maximum evdev scan,
  0.154 ms maximum SDL event-pump time, and 17.348 ms maximum event-loop gap.
  Its measured frame-rate window was stale because there were no active
  sessions; this is intentionally not interpreted as a live frame-rate
  measurement.
- A fresh active-PTY measurement on the same host reported 46.724 Hz over a
  513 ms window with 12 ms sample age, 24 measured frames, zero missed
  deadlines, a 17.348 ms maximum event-loop gap, and a 0.076 ms maximum
  evdev scan. The session was deleted after sampling and resources returned
  to zero.
- A live-host logical-pane check temporarily changed the single output to a
  2×2 topology, attached four API-created PTYs, and observed four running
  sessions with a healthy runtime and zero missed deadlines. The topology was
  restored to 1×1 and sessions, surfaces, streams, and routes returned to
  zero. This validates logical pane orchestration, not physical multi-monitor
  behavior.
- The live discovery API reports SDL3 readiness, one AMDGPU DRM/KMS card with
  page flipping, direct scanout, and hardware acceleration, three input
  devices (keyboard, pointer, and consumer control), and no audio devices.
  The absence of audio is an observed host capability, not a missing API
  response; `/v1/audio` returns an empty device list.

- Version 0.1.285 adds per-output graphics-upload timing and a per-output
  pending pixel-frame scheduler. The physical host accepted a sustained 120
  frame API stream at 59.568 Hz and produced 125 presentation passes, drained
  `pixel_frames_pending` to zero, and reported a 0.398 ms maximum texture
  upload. The same host's refresh gate measured 61.507 Hz with zero missed
  deadlines; test sessions and streams returned to zero.
- The physical Hibr smoke test initially exited with status 1 because an
  existing held Hibr desktop was already running. Using Hibr's documented
  `--resume --session blit-279-shot --name display-1` invocation reached a
  running session with surface revision 309 and 279,575 terminal bytes. Blit
  and Hibr were both left clean after the session was deleted; Hibr source was
  not modified.
- The deployed 0.1.286 package was verified through the live API after the
  supervised restart. The active-PTY frame gate measured 59.991 Hz at the
  display-mode cadence with zero missed deadlines; the 120-frame pixel gate
  accepted 120 frames at 59.514 Hz and rendered 121 presentation passes. A
  real Hibr `--resume` session reached `running`, emitted 52,186 terminal
  bytes, measured 59.973 Hz with zero missed deadlines, and was deleted cleanly.
  Live resources returned to zero afterward. The host still exposes one
  physical output.
- Exact-current 0.1.285 QEMU fixtures pass the API pixel/KMS capture gate,
  Hibr four-pane primary/join matrix, keyboard/multi-mouse/gamepad input
  matrix, and 240 Hz scheduler gate. These remain virtual-display evidence;
  the physical host still exposes only one output.
- Exact-current 0.1.286 TCC binaries pass the complete terminal conformance
  suite, Hibr font manifest and SDL glyph-shape gates, local 54.367 Hz
  refresh gate with zero missed deadlines, 120-frame pixel gate at 59.502 Hz,
  graphics-cache reuse, and the four-output VKMS fixture. The four-output
  fixture reports four independently presented 1024×768@240 outputs with
  contiguous placements and four terminal sessions. The direct-KMS cadence
  fix avoids replaying a 60 Hz producer at every 240 Hz slot. These are still
  virtual-display results; the physical multi-monitor gate remains open.
- The strengthened exact-current four-output fixture additionally placed
  distinct red, green, blue, and yellow overlays on the four discovered
  outputs and passed
  `FOUR_OUTPUT_INDEPENDENT_PIXEL_PASS` through four API screenshot captures.
  The same binary passed the two-output KMS capture/pixel gate, Hibr pane
  matrix, keyboard/multi-mouse/gamepad input matrix, and 240 Hz scheduler
  fixture after restaging.
- The remaining exact-current local regression gates are also green:
  wallpaper background batching, hidden-cursor damage suppression, screenshot
  overlay removal, session lifecycle escalation, write/resize cleanup, stream
  replay repair, HTTP backpressure, fragmented HTTP requests, authenticated
  network access, clean shutdown, update cancellation, and updater idle
  timeout. No active local test resources remained after the pass.
- The exact-current 240 Hz producer-driven pixel workload exposes an open
  high-refresh limitation: all 240 frames are accepted with zero pixel-frame
  protocol errors, but the synchronized direct-KMS VKMS run presents only
  89 frames and records 20 additional virtual page-flip deadline misses.
  Diagnostic async page flips and multi-threaded QEMU TCG produce the same
  result (86 and 82 presentations respectively). This is not treated as a
  passing 240 Hz gaming/emulation result; hardware/KVM-backed validation or a
  runtime fix is still required.
- An SDL3 isolation run at the same 240-frame producer cadence accepted all
  240 frames at 227.154 Hz with zero protocol errors and reused the graphics
  image. The dummy renderer presented 183 frames at its own virtual cadence;
  this separates network/graphics ingestion from the unresolved direct-KMS
  page-flip limitation and is not physical 240 Hz evidence.
- The new intermediate 120 Hz producer fixture also accepted all 120 frames
  with zero protocol errors, but the two-vCPU VKMS run increased page-flip
  misses from 3 to 15 and failed the strict cadence gate. This is diagnostic
  TCG/VKMS evidence, not a claim that 120 Hz hardware is unsupported.

## Open release gates

1. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The executable
   `BLIT_GATE_SERVER=http://192.168.10.54:8080 tools/tests/physical_multi_display_gate.rb`
   is ready for this check; the current host exposes only one physical output
   and returned exit status 77 in the latest run.
2. Validate the producer-driven 240 Hz pixel path on hardware or a
   KVM-backed VKMS guest. The current host is 60 Hz and the available QEMU
   TCG guest cannot sustain the 240 Hz page-flip workload without additional
   deadline misses; the failing result is recorded above.

All software-side Gate 6 cases currently in scope—including primary supervisor
restart, joined-client reconnect, session termination during writes/resizes,
stream reconnect/replay, input-device loss, output loss, and the two-hour PTY
soak—have authoritative evidence above. Until the physical-monitor gate has
authoritative evidence, the project must not be
described as fully released or physically multi-monitor validated.
# Current 0.1.367 diagnostics-instrumentation result

The exact TCC-built 0.1.367 binary exposes producer-side
`surface_commits` and `frame_commits` in `/v1/diagnostics/runtime`; both fields
are required by `openapi/blit-0.1.yaml`. The local frame-rate workload passed at
54.381 Hz with a 16 ms target and zero missed deadlines, and the deployed live
workload passed at 63.000 Hz with zero missed deadlines.

The exact 0.1.367 package was deployed to `192.168.10.54` and
`/v1/version` reported 0.1.367. The four-output Hibr QEMU fixture passed
`KMS_FOUR_HIBR_PASS` with the 6.19.14 VKMS kernel when booted with
`vkms.create_default_dev=0`; omitting that argument selects VKMS's default
single-output card and is now diagnosed explicitly by the fixture.

The physical visual and physical evdev gates remain open. A live capture
recorded a 60 Hz display, zero missed deadlines, and zero physical evdev mouse
events; API-injected pointer motions were accepted and traced. Therefore this
release does not claim physical mouse or visual acceptance.

# Current 0.1.382 live and QEMU result

The exact TCC-built 0.1.382 binary passed the terminal conformance suite,
package artifact gate, QEMU input/render soak, and four-output Hibr fixture.
The four-output fixture reported four independently presented 1024x768@240
outputs with a 4.166 ms target interval and no diagnostic errors.

The package was supervised-updated on `192.168.10.54`; `/v1/version` reports
0.1.382. Starting Hibr through the packaged
`/usr/share/blit/hibr/blit-session.hibr` entrypoint produced a 240x135
terminal surface and one complete 1920x2160 graphics image. A free topology
pane no longer swallows input: API motion and both button edges reached the
active session and were recorded in the trace with `target_session=1`.

The live evidence bundle is
`diagnostics/live-0.1.382-experience-20261009T031158Z`. Its screenshot shows
the correctly aligned wallpaper and desktop, plus a reproducible stray
`0`/block at the left edge. The timing capture measured damage-driven presents,
not continuous vblank: 32 presents in about 10 seconds, zero missed deadlines,
and individual presents within the 60 Hz budget. This is an observed cadence
warning, not proof of a 3 Hz display; the scheduler currently presents on
damage and input. Physical evdev activity, physical multi-monitor behavior,
and hardware high-refresh gaming remain open gates.

The API pointer artifact capture at
`diagnostics/live-0.1.382-pointer-20261009T031414Z` produced four framebuffer
captures. After the intentional first screenshot initialization, each adjacent
capture differed in only the cursor region (92 pixels) and had zero changed
pixels outside the old/new cursor bounds. The render trace also recorded the
`pointer-cache` path. This proves API pointer motion does not leave trails in
the live SDL scene; it does not yet prove physical evdev motion on the host.

# Current 0.1.383 live routing result

The 0.1.383 deployment retained the graphics and input behavior while making
routing diagnostics explicit. On the live host, the packaged Hibr entrypoint
produced one complete 1920x2160 graphics image, and API motion plus both button
edges reached session 1 with `routing=display-fallback`. This is the expected
route for a primary display-attached session with no pane ID.

## 0.1.449: the graphics boundary is Hibr's side, measured

Issue #19 asked whether the live desktop presents no bitmap because Hibr never
sends one or because Blit decodes one and discards it. Before 0.1.449 that
question could not be answered from Blit's evidence: both leave a session with
zero images, no errors and a healthy document. It can be now.

Measured on 192.168.10.54 running 0.1.449, with the packaged
`blit-session.hibr` desktop launched through the API, owner registered, 32,400
printable cells at 240x135, revision 860:

```
performance.graphics_payloads            0
performance.graphics_payloads_kitty      0
performance.graphics_payloads_sixel      0
performance.graphics_payloads_rejected   0
performance.graphics_texture_uploads     0
performance.graphics_cache_hits          0
performance.pixel_frames_accepted        0
surface-1.graphics_images                0
performance.terminal_bytes           33454
trace?kind=graphics_payload              0 events
```

Three independent arrival paths, all zero: no graphics escape sequence reached
the terminal parser, no API pixel frame arrived, and nothing was rejected.
33,454 bytes of terminal output did arrive. The desktop is complete and
correct **cell art** -- the captured 1920x2160 framebuffer holds **9 distinct
colours**, dominated by `#0d1b2a` over 4,030,905 pixels:

```
#0d1b2a 4030905   #16324a 64208   #1b3a5c 29730   #000000 16324
#101820    3775   #cbd5e0  1438   #63b3ed   640   #4a5568   154
#ffffff      26
```

The instrument was proven live in the same process rather than assumed: a
probe session on the same host emitting one sixel moved `graphics_payloads`
from 0 to 1 and produced

```
protocol=sixel session=session-2 bytes=21 header=DCS0;0;0q
  images=0->1 rejected=0 reason=-
```

So a zero beside the desktop is a measurement, not a dead counter. The
boundary is on Hibr's side: the desktop emits no image by any route Blit can
receive one. Blit's graphics decoders are not implicated, and the sixel and
Kitty gaps fixed in 0.1.446 through 0.1.448 were real but are not the cause of
this symptom.

## 0.1.450: the update restart race, measured on both sides of its own fix

Issue #63. Two mechanisms restarted the service after a package install and
neither knew about the other, so systemd's one `SIGTERM` was consumed by a
process that then re-exec'd. The unit waited out `TimeoutStopSec` and
`SIGKILL`ed its whole cgroup, taking an attached Hibr desktop with it 90
seconds after the deployment reported success.

The 0.1.450 package was deployed to 192.168.10.54 twice. The first deployment
is performed by the running 0.1.449, which still carries the race; the second
by the fixed 0.1.450.

Before -- 04:55:17 to 04:56:48, 91 seconds:

```
04:55:17 systemd[1]: Stopping blit.service ...
04:55:17 blit[14469]: blit gateway listening ...        <- the same pid, re-exec'd
04:56:48 (TimeoutStopSec expires, SIGKILL, unit restarted)
```

After -- 04:57:30 to 04:57:30, under one second:

```
04:57:30 systemd[1]: Stopping blit.service ...
04:57:30 blit[14788]: blit: package installed; exiting for the unit restart
04:57:30 systemd[1]: blit.service: Main process exited, code=exited, status=1/FAILURE
04:57:30 systemd[1]: blit.service: Stopped blit.service ...
04:57:30 systemd[1]: blit.service: Started blit.service ...
04:57:30 blit[15012]: blit gateway listening ...
```

No `stop-sigterm` timeout, no `SIGKILL`, no child killed, and one `listening`
line from one pid instead of two. Two `listening` lines from one pid is the
signature of the old behaviour.

The non-zero exit is deliberate -- `Restart=on-failure` must remain a safety
net if the helper dies before it reaches `systemctl`, because the package must
never be installed with the service down -- and its cost was measured rather
than assumed, since the ticket itself objected to a permanently noisy unit
result. It is not sticky:

```
systemctl is-active blit   -> active
systemctl is-failed blit   -> active
Result=success  NRestarts=0  ActiveState=active  SubState=running
```

The whole trace is one journal line, immediately preceded by Blit's own
`package installed; exiting for the unit restart`.

A desktop launched right after the clean deployment reached 32,400 printable
cells at revision 859 and was sampled 30 times over five minutes: zero uptime
resets, one session throughout, `errors` and `warnings` empty every time.

How this was found is part of the evidence. The host has no SSH, so the
journal came back through the API: a terminal session running `journalctl`
with a 200x60 surface, read off `GET /v1/surfaces/{id}/cells`. Blit's own
in-process log ring cannot answer a question about why the previous process
ended, because it died with it -- all it could say was that uptime was 5.7
minutes and there were no sessions. A 20-minute watch of the clean process
(118 readable samples, zero uptime resets, one session throughout) is what
established that the kill was a deployment race and not a crash, and so what
to fix.

## 0.1.451 and 0.1.452: a device setting survives a release

Epic #64 stage 1 (#65), ADR 0067. Until these releases a device's
configuration lived only in memory, so every supervised update returned every
pointer speed, keyboard layout and default audio device to its default. This
is the first measurement on this host of one surviving.

Measured on 192.168.10.54:

```
PATCH /v1/input/devices/input-0003-25a7-0124-...-input3
  -> sensitivity_percent 215, natural_scroll true
PATCH /v1/audio/audio-device-pcm0-p
  -> default true

/var/lib/blit/settings.json   -rw-r----- 1 root root 191
{"version":1,"input_devices":{"input-0003-25a7-0124-usb-0000-e6-00.4-1.1.4-input3":
 {"sensitivity_percent":215,"natural_scroll":true}},
 "audio_devices":{"audio-device-pcm0-p":{"default":true}}}

supervised update to 0.1.452  -> both still applied
POST /v1/control/restart      -> both still applied, settings revision 1
```

Only what differs from a default is written, and only against ids derived from
what a device reports about itself. The document's own revision is stored with
it, which 0.1.452 added after reading the live host and finding the revision
back at 0 in the fresh process: it is the `If-Match` a controller writes the
whole document against, and a counter that restarts at zero would let a stale
write match by accident. Measured both ways afterwards -- a document written
by 0.1.451 carries no revision and loads as 0, which is the right answer for
an older document, and a write then records 1 that the restart reads back.

The deployments themselves were clean, which is issue #63 holding across three
releases now: stop, exit and start inside one second, one `listening` line per
process, no `stop-sigterm` timeout and no `SIGKILL` of the unit's children.

**An honest gap in the same window.** The first desktop launch after the
0.1.452 deployment did not take. Blit created session 1, logged
`hibr_resize_discovery session=1 attempt=1 matches=1` and then `matches=0` for
attempts 2 through 4, and retired the surface -- while Hibr's own
`[attached: blit-0-1-452]` and `[hold: blit-0-1-452]` processes stayed alive as
orphans, which were killed by hand afterwards. A second launch under a
different session name reached 32,400 printable cells at revision 860 in about
ten seconds and stayed healthy with `errors` and `warnings` both empty. That is
the fail-closed `hibr_attach_unresolved` shape; it is recorded on issue #32
rather than explained here, because neither release touched the attach path and
one occurrence is not a diagnosis.

## 0.1.453: a keyboard's repeat is the kernel's, and Blit owns it

Epic #64 stage 2 (#66). Two facts were measured on 192.168.10.54 before any of
the implementation was written, because they decided its shape.

`EVIOCGREP` and `EVIOCSREP` both work on the `O_RDONLY` descriptor the opener
thread already holds:

```
/dev/input/event4 O_RDONLY GET=(250, 33) SET=ok
/dev/input/event8 O_RDONLY GET=(250, 33) SET=ok
```

So owning the repeat rate needed no change to how an input device is opened.
The LED is the other half and does need a writable descriptor, so that was
measured too, with the lamp restored:

```
/dev/input/event4 numlock before=0 set->1 (write ok) now=1 restored=write ok now=0
```

The repeat round-trip on four real keyboards, with the quantisation measured
rather than asserted:

```
keyboards reporting EV_REP: 4 of 28 devices

set delay=400 rate=20  ->  kernel delay=400 period= 50 rate=20
set delay=250 rate= 7  ->  kernel delay=250 period=142 rate= 7
set delay=180 rate=30  ->  kernel delay=180 period= 33 rate=30

a device without EV_REP   ->  422 device_property_not_applicable
stored                    ->  {"repeat_delay_ms":180,"repeat_rate_hz":30,
                               "caps_lock_boot":false,"num_lock_boot":true}
restored                  ->  delay=250 rate=30
```

7 Hz is 142.857 ms and the kernel keeps 142, which reads back as 7 Hz; 20 Hz
is 50 ms exactly. This is why what Blit reports is the read-back and not the
request, and why `repeat_period_ms` is reported beside the rate.

`tools/probes/host_keyboard_settings.rb` restores every keyboard it touched.

**What the local gate proves and what it cannot.** A fabricated node is a
regular file and fails every ioctl, so `tools/tests/keyboard_settings_gate.rb`
tests a stand-in for the kernel -- the sidecar's `repeat=` line -- and says so
in its header. What it does prove, and the host probe does not, is the
*effect* of the remap and the boot lock states: keys are injected through the
same dispatch a real device uses and the session echoes what reached its PTY,
so a setting that is stored but changes no byte fails. The LED is skipped for
a fabricated node and is only ever measured on the host.

The deployment itself was clean, which is issue #63 holding across a fourth
release, and the desktop came back with 32,400 printable cells at revision 859
with `errors` and `warnings` both empty -- on the first launch, unlike the
0.1.452 window recorded above.

## 0.1.454: the acceleration curve, on real hardware

Epic #64 stage 3 (#67). Pointer travel is the one measurement a gate
environment cannot make: a fabricated evdev node is a regular file, nothing
opens it as a pointer, and `update_evdev_pointer` deliberately publishes
nothing when no pointer is open. Issue #52's gate records the same limitation
about `sensitivity_percent` and sends that measurement here.

Measured on 192.168.10.54, injecting through a real pointer's own
configuration:

```
pointers: 3 of 28 devices
subject:  input-0003-25a7-0124-usb-0000-e6-00.4-1.1.4-input3

flat      delta 20 travels 20.0 at 100%, 40.0 at 200%
adaptive  delta  2 travels  2.0
          delta 11 travels 22.0
          delta 20 travels 60.0

buttons        272 -> button=2, 273 -> button=0 at the dispatch
aliasing map   400 invalid_device_buttons
scroll_step    1 -> 1 report, 3 -> 3 reports
```

The flat numbers are #52's measurement unchanged, re-run under an explicit
`flat` profile: `sensitivity_percent` means exactly what it meant before an
acceleration profile existed, which is what #67 promised about the property it
did not change. The adaptive numbers are the documented curve exactly --
untouched at the floor of 2, doubled at the midpoint of 11, tripled at the
ceiling of 20.

`tools/tests/pointer_curve_gate.c` pins the same curve at its boundaries
without a server, including its sign, its monotonicity, and that it is never
slower than flat.

**A recorded gap.** This host has no gamepad: its one device reporting an
`axes` capability reads as `other`. So `deadzone_percent` and `invert_axes`
are measured only against a fabricated gamepad, and there is no gamepad
injection kind, so even that is an API contract rather than an effect. Their
behaviour on real hardware is untested and is not claimed.

**Middle-button emulation is absent by decision**, recorded as issue #71 with
its design: it needs a deferred press, and so a time-driven obligation in an
event loop that is otherwise entirely event-driven.

One real bug was found while gating this release and fixed in it: a settings
write that was still coalescing could sit in memory indefinitely on a quiet
server, because nothing woke the loop. The `select` wait is now clamped to the
remaining flush window, the same shape the attach reassert already used, and a
write that cannot reach the disk says so once rather than failing in silence.

## 0.1.455: a card's mixer, on real hardware

Epic #64 stage 4 (#68). The ALSA control ioctls are the thing under test here,
and no gate can serve one: a fabricated control node is a regular file, so
`tools/tests/audio_card_gate.rb` reads a stand-in and this is where the real
`SNDRV_CTL_IOCTL_*` round-trip happens.

Measured on 192.168.10.54, all six cards:

```
cards: 6                                    pcms: 12
  Device      6 elements  shares=2  volume=PCM Playback Volume
  Generic    28 elements  shares=4  volume=(none)
  Generic_1  13 elements  shares=2  volume=Master Playback Volume
  RC          3 elements  shares=1  volume=(none)
  QuadCast    8 elements  shares=2  volume=(none)
  Meet        2 elements  shares=1  volume=(none)

the most shared mixer: Generic, 4 PCMs
```

Four PCMs on one mixer is the case the card-owned model exists for, and it is
on this hardware rather than hypothetical. The resolution rule's three
interesting shapes are all real: a card resolving its sole playback volume, a
card where `Master` beats a `PCM Playback Volume` that also exists, and a card
with two candidates and no `Master` that is refused with
`audio_control_ambiguous`.

Both range classes, because the quantisation is only visible on one:

```
Device     PCM Playback Volume  0..255  1 channel
  asked  25% -> [64]       -> reads back 25%
  asked  50% -> [128]      -> reads back 50%
  asked  75% -> [191]      -> reads back 75%

Generic_1  Master Playback Volume  0..87  2 channels
  asked  25% -> [22, 22]   -> reads back 25%
  asked  50% -> [44, 44]   -> reads back 51%
  asked  75% -> [65, 65]   -> reads back 75%
```

A span of 255 holds every percentage; a span of 87 cannot — 88 values against
101 percentages — so 50% comes back as 51%. That is why what Blit reports is
the read-back. Writing the read-back settles rather than drifting, asserted on
both cards.

The probe originally tested only the first resolved card, which measured only
half of that; it exercises every resolved card now. Every value it touches is
restored — `[102]` and `[60, 60]`.

**What is not claimed.** Every control device was accessible, which is the
root profile working. The `audio` group is now in both unit files, but that
the *unprivileged* profile can open `/dev/snd/controlC*` is a group in a unit
file and not a measurement: the gate runs as the developer against fabricated
files and this host runs root. `state.control_accessible` is reported
separately from a PCM's `accessible` so an operator who switches profiles can
see the difference immediately.

Control values are deliberately not persisted — they are the card's own state
and the card keeps them, so storing and re-applying would make Blit fight
`alsactl` for something that already survives a reboot.


## 0.1.456: a display rotates, and what power costs

Epic #64 stage 5 (#69). A display's `configuration` was `{}`, and ADR 0064
recorded that as a decision rather than an omission: selecting a mode resizes
every surface, and a Hibr desktop's cells come from its owner (ADR 0063), so it
needs issue #60's resize protocol first. This stage is the settings that resize
nothing — and the measurements came before the design, which is why the shape
is what it is rather than what the ticket guessed.

Four facts settled the stage, all measured on 192.168.10.54 (kernel 6.12.101,
SDL 3.4.16):

```
/sys/class/drm/card0-DP-1/dpms   mode 444; a root write returns EACCES
/dev/dri/card0                   one open fd, pid 17721 = blit (SDL has master)
/sys/class/backlight             exists, empty
SDL_kmsdrmvideo.c:2201           SDL publishes its master fd per window
```

The first is stronger than the ticket's claim, and in a useful direction: the
`dpms` attribute is read-only **to root**, so sysfs is not an avenue on any
backend — that is the kernel, not SDL holding master. The fourth is the one
that turned the stage from a gap into a feature:
`SDL_PROP_WINDOW_KMSDRM_DRM_FD_NUMBER` is ADR 0067's "cooperation with SDL"
branch, and it exists in the vendored build.

### `capabilities` is derived, and that is the contract

```
mode-report   always
rotation      the SDL path. Blit's own render transform, no DRM master.
brightness    only where a backlight is attributed.
```

`configuration` offers exactly what `capabilities` claims and a PATCH accepts
exactly that. `mode` is still refused, with `invalid_display_property`. This is
ADR 0064's own reasoning applied per property instead of per resource, and
`display_device_api_gate.rb` was rewritten to assert the new rule rather than
deleted: a property offered without the capability, or a capability with
nothing to set, now fails.

### Rotation: the contract is a corner, not an enum

`rotation` is the angle, clockwise, by which Blit rotates the image before
presenting it; at 90 what was drawn at logical (0, 0) appears at the physical
**top-right**. It is stated as a corner because every enum that could stand in
for it disagrees — DRM's `ROTATE_90` is anticlockwise, RandR calls the same
transform "right", SDL's render angle is clockwise.

`bounds` became the *logical* rectangle and `modes` stayed physical, with
`state.physical` beside them. That one choice is why rotation needed no new
plumbing: the cell grid, the pointer hit test and the topology placements all
already work in that space. `xrandr` reports the same split.

Two sites learn about rotation. The scene-to-panel draw, which all three
present paths now share — the pointer-cache fast path presents the cached scene
without rebuilding it, so a rotation applied only in the full-redraw path would
un-rotate itself the moment the pointer moved. And `render_pointer`, which
draws after the scene in panel coordinates, and transforms both its hotspot and
the arrow's own shape.

### What the gates assert, and what they do not

`display_geometry_gate.c` re-implements SDL's draw model — a destination
rectangle rotated clockwise about its own centre — and asserts that a texel
placed by that model lands exactly where `blit_rotate_point` says it does, for
four rotations and five positions each. The two are used by different code (one
presents the scene, the other places the pointer on top of it), and a
disagreement is a cursor that drifts away from where it is clicking, which a
screenshot gate cannot see because the pointer is drawn after the capture it
would be compared against. Teeth: the anticlockwise point transform fails 8
assertions; the output rectangle in place of the scene's fails the footprint at
90 and 270.

`display_settings_gate.rb` is six scenarios, and the one that carries the stage
is the screenshot. A full block is drawn in the first cell, and the ink is
counted in all four corner rectangles at every rotation:

```
  0: 0=128 90=0   180=0   270=0
 90: 0=0   90=128 180=0   270=0
180: 0=0   90=0   180=128 270=0
270: 0=0   90=0   180=0   270=128
```

128 is exactly a cell, 8x16. The diagonal is what a server that stores the
number and presents unrotated cannot produce: with the present reverted, the
ink stays in the 0 corner for all four and six assertions fail. Requiring
*exactly* the cell area rather than most of it matters — a scene texture left
at the renderer's output size scales instead of rotating and yields 120, which
any looser threshold would have accepted.

Five teeth builds, each a working-tree copy with one change reverted:

```
rotation stored, presented unrotated   6 failures, the screenshot scenario
no refusal under a live surface        3 failures
nothing persisted                      3 failures
scene texture at the output's size     2 failures, 120 lit pixels not 128
brightness claimed with no backlight   1 failure
```

### Power is reported and not settable, and that is deliberate

`state.power` carries the connector's DPMS state, read through SDL's published
master fd, and `state.connector` names the connector it was matched to. The
match is **by name** and not by index, because SDL enumerates only the
connectors it drives while the kernel enumerates all eight on this host — so
display 0 is connector 0 only by luck.

`power` is not in `capabilities`, because nothing can set it yet. Shipping the
read first was not caution for its own sake: it is the only way to prove the fd
and the attribution without the write, and the write carries a hazard that is
worth stating before taking it. SDL's `KMSDRM_WaitPageflip` polls with
`timeout = -1`. A display turned off while a flip is outstanding, if the kernel
did not deliver that flip's event, is an unbounded wait on the single thread
that also serves the API — and the API is the only channel to this host. The
remainder of #69 suspends presentation before the write and resumes after it,
and the host test will prove the output quiescent, by `present_calls`, before
turning anything off.

### What is not claimed

- `/sys/class/graphics/fb0/blank` was not tried. fbdev emulation is one device
  for the whole card, so it could not express "turn off this one display" even
  if it worked behind a master. That is a structural argument, not a
  measurement, and it is recorded as one.
- The direct-KMS path has no rotation transform and says so by omitting the
  capability. No reachable machine runs that path — the development container
  is an unprivileged LXC with no `/dev/dri` and `CapEff=0`, the host runs SDL,
  and `vkms` exists on neither — so writing it would be code that stays at
  *implemented* forever. It joins #20/#21's hardware-blocked list instead.
- Whether the unprivileged profile can write
  `/sys/class/backlight/*/brightness` is not measured: the gate uses fabricated
  files and the host has no backlight. Same unmeasured-group caveat as #68's
  control device.
- On a multi-display host, rotating one display changes its width and a stored
  topology placement may stop tiling; re-`PUT /v1/topology`. Both machines here
  have one output, so that is a stated consequence.
- Done-when 2 of #69 asks for a gate for display power on the direct-KMS path.
  That half is unmeetable as written, for the reason above, and is recorded on
  the ticket rather than reinterpreted.

### Host-accepted on 192.168.10.54

`tools/probes/host_display_settings.rb`. The three things no gate can reach:
SDL's published fd really yielding a connector, the rotated present on
`opengles2` rather than SDL's software renderer, and a real Hibr desktop being
handed the rotated grid.

```
display display-1 name=DP-1 bounds=1920x2160 physical=1920x2160 cell=8x16 inset=4
capabilities mode-report,rotation
state power="on" connector="DP-1" backlight=nil
```

The connector was matched **by name** out of eight on this card, through
`SDL_PROP_WINDOW_KMSDRM_DRM_FD_NUMBER`, and its DPMS state read. That is the
whole of what 0.1.456 claims about power, and it is the fact the rest of #69
needs.

The same diagonal as the gate, on a 1920×2160 panel and a real GL renderer:

```
  0: 0=128 90=0   180=0   270=0
 90: 0=0   90=128 180=0   270=0
180: 0=0   90=0   180=128 270=0
270: 0=0   90=0   180=0   270=128
```

A stored rotation applied at boot after an orderly restart — `bounds=2160x1920`,
`configuration={"rotation"=>90}` — and then the desktop:

```
rotated desktop 270x120 printable=32173
upright desktop 240x135 printable=32203
```

270×120 is 2160/8 by 1920/16, which is the rotated grid. Both are 32,400 cells,
which is why the *count* was never the evidence and the dimensions are: a
server that stored the rotation and handed out the unrotated grid would report
the same 32,400. Everything was restored: rotation 0, the stored entry removed,
a fresh upright desktop, `errors=[]` and `warnings=[]`.

### #69 is not closed by this release

Done-when 2 asks for display power on the direct-KMS path with a gate. Its
premise has changed rather than failed: power is reachable **under SDL** on
this host, through the fd above, so it is worth implementing rather than
recording as a gap — which is the opposite of what the ticket expected and
better than it. The write, the presentation suspension it needs, and the
quiescence protocol for testing it safely are the remainder of the stage.

## 0.1.457 through 0.1.460: the screensaver's trail, and what found it

Issue #72, reported from the screen rather than by a gate: the Hibr desktop's
screensaver was leaving a solid trail of itself across the panel.

### The measurement that framed it

The authoritative cell snapshot and the pixels disagreed, which is the whole
finding — the owner was authoring correctly (ADR 0063) and the renderer was
keeping old pixels:

```
GET /v1/surfaces/surface-2/cells   336 cells carry ink, box x 89..112 y 41..54
the panel                          8706 cells painted, box x 0..239 y 0..133
painted but blank in the data      8407
```

A no-op `PUT /v1/topology` forces a full redraw; it dropped the count from 8706
to 333, matching the data, and the trail then rebuilt at about 96 cells a
second. So the full-redraw path was correct and the incremental one was not.

### Three defects

**The incremental path cleared a rectangle and repainted rows** (0.1.457). The
repaint walks `column = 0` to `surface->width` for every damaged row; the clear
covered only the damage rectangle's columns. A blank cell with the default
background draws nothing — `if (background == 0) { ++column; continue; }` — so
any column repainted outside the cleared rectangle kept its old pixels. A
sprite moving diagonally is exactly that case: consecutive positions overlap in
rows, not in columns. Host effect: 8407 stale cells to 2832.

**Damage was cleared for surfaces that were never drawn** (0.1.458). The tail
cleared `damage_valid` for every surface that *targets* an output, which is a
different question from whether it was painted. Four paths in the per-output
loop skip drawing, and each discarded damage that had never reached a pixel.
The renderer now records what it painted and the tail consults that record, so
every skip path is covered by construction.

**Clearing a wide glyph's partner cell marked no damage** (0.1.460).
`terminal_clear_wide_overlap` blanks a neighbouring cell; only the caller's own
target was marked. Marked at the point of clearing rather than asking every
caller to remember a neighbour it did not know it touched.

### What actually found it

Not reading the code. Three theories came from reading it and all three were
wrong. What settled it was shipping the damage rectangle into both traces
(0.1.459) and reading them off the live host:

```
surface_update  damage=93,97,1x1 valid=1  presentation=immediate
render_path     damage=93,97,1x1  path=incremental-damage
```

The owner writes **one cell per request**, immediate, so every write is its own
frame with a 1×1 damage rectangle. Every reproduction built before that wrote
in batches, which is why none of them reproduced anything.

### Host-accepted

Twelve samples over three minutes with the screensaver running, spanning two
full clear-and-redraw cycles:

```
   drawn   painted  stale  missing
 1  2906     2913      7      0
 3    52       58      6      0     <- screensaver cleared the screen
 7  1059     1068      9      0
10  1813     1819      6      0
11   103      109      6      0     <- and again
12   363      369      6      0
```

The residual 5–9 is the race between reading `/cells` and reading the
screenshot; `missing=0` throughout says nothing is under-painted. The number
does not grow, which is the question.

### The gate gap, stated rather than papered over

Four reproductions were built: `deferred_damage_gate.rb`, a 12×8 host probe,
one at the screensaver's exact 240×135 with deferred-commit batches, and one
writing a cell per request as the trace showed. **None failed before the fix
and passed after.** The development container is an unprivileged LXC with no
GPU, so every local run uses SDL's software renderer, and this failure has only
been observed on the host's `opengles2` path.

`deferred_damage_gate.rb` is in the suite and guards the invariant going
forward, and its header says in as many words that it has no teeth and why. A
regression test with teeth needs a gate host with a GPU — #20/#21's territory.
Claiming the suite covers this would be worse than the gap.

`motion_trail_gate.rb` asserts the same property and passes either way: it
writes immediate and its erase-plus-draw fits one chunk. Issue #72's done-when
asked for it to be fixed so it would have caught this; that is recorded as not
achievable here rather than quietly dropped.

## 0.1.460: input robustness across hotplug

Issue #74, and the last of #72. Asked for directly after the feature work was
not making the server usable.

### Two defects, found by asking about the fiftieth cycle rather than the first

**Retiring an absent device only ever happened on a filesystem event.** A
rescan is the only caller of `retire_absent_input_devices`, and inotify is the
only thing that arms a rescan. The flag coalesces, so a hub dropping thirteen
nodes at once produces one scan; if it runs mid-burst it marks the nodes that
still exist as seen, and nothing arms another. Measured: a deleted node's
device stayed `present: true` for 56 seconds and counting. A watch event now
sets a one-second resettle deadline as well, and the select wait is clamped to
it — a hotplug is exactly when the loop has no other reason to wake, which is
ADR 0067's settings-flush shape.

**A probe that learned nothing erased what the device was.** Opening a node
being torn down succeeds while every `EVIOC*` ioctl fails, so `kinds` came back
zero and that zero was written over a known device. Measured: a pointer
demoted to `other` with empty capabilities. A keyboard in that state is never
claimed by the poll loop again even once the hardware returns.

**`input_device_missing` fired only at zero.** On the live host a microphone's
remote-control interface counted as both a keyboard and a pointer, so while a
USB hub had taken the real ones away `/v1/diagnostics` reported `warnings: []`
and the operator had no usable input. It now fires below the peak this process
has seen.

### The first teethed gate of this stretch

`tools/tests/input_hotplug_cycle_gate.rb`, thirty cycles of two fabricated
devices:

```
fixed    3 runs, 3 passes
pre-fix  3 runs, 3 failures, at cycles 15, 0 and 4
```

always as `unplugging left keyboard=false pointer_present=true`.

It does not cover the poll set: a fabricated node is a regular file and is
never opened as a keyboard, so `state.open` stays false. The host was measured
directly instead — 19 descriptors, 2 on event nodes, zero deleted.

### Host-accepted

The hub returned during this release. Blit picked up the full set
unaided — `input_devices: 28`, `keyboards: 4`, `pointers: 3`, every one
`open: true`, which is the pre-incident state.

The new warning, end to end on real hardware:

```
one keyboard disabled   keyboards 3 of peak 4   warnings ['input_device_missing']
re-enabled              keyboards 4 of peak 4   warnings []
```

The two stale probe settings left on the microphone's HID interfaces by
earlier `host_*` probes were cleared with `PUT /v1/settings`, leaving the
document carrying the audio default alone.

## 0.1.461 through 0.1.464: a keyboard stop chord, the trail's real cause, and a retraction

Four releases driven by an operator using the machine rather than by the
ticket list, and one claim withdrawn.

### The retraction, first

0.1.457 and 0.1.458 were recorded above as resolving issue #72. **They did
not.** Each fixed a real defect with a real mechanism, and the symptom
survived both. With a line saver running, the host measured residue climbing
432, 469, 551, 605, 740 and collapsing to 44 when a whole frame landed —
`stale` far exceeding `missing` throughout, which is residue and not sampling.

The clean readings behind the earlier claim were taken while the desktop was
static and during a quieter phase of a different saver. The operator was right
and the instrument was being read wrong.

Four separate measurement faults produced confident wrong answers during this
work, and they are recorded in `tools/probes/host_cell_pixel_agreement.rb` as
the reasons that probe is shaped the way it is:

1. sampling one pixel per cell — reported `painted=0` on a screen a full scan
   saw as 1536 cells, and produced the quoted 2832-stale figure
2. non-atomic sampling — `stale` and `missing` in roughly equal numbers, which
   is the signature of the sampling, not the renderer
3. the software mouse pointer — three stale cells that survived a forced
   redraw, a fresh capture and deleting every surface turned out to be the
   arrow
4. `?cached=1` versus a fresh capture — the cached form shows the panel, a
   fresh one forces a render that repairs the state and hides the fault

### 0.1.461: a chord that stops the server, and a key release that was not a keystroke

**Host-accepted.** The operator used the chord on the physical machine:
*"ctrl-alt-backspace works, I've killed it, looks nice!"*

`GET`/`PUT /v1/control/shutdown-key` programs a chord, defaulting to
Ctrl+Alt+Backspace and enabled. It performs the same orderly stop as
`POST /v1/control/shutdown` and exits 0, so a unit with `Restart=on-failure`
leaves it stopped. `tools/tests/shutdown_key_gate.rb` asserts the ways it
could be useless or dangerous rather than merely absent: a bare Backspace is
inert, near misses are inert, it fires on press and not on release or
autorepeat, every modifier must be held on the keyboard the key came from, it
is programmable, a reprogrammed chord survives a restart, disabling works, and
the exit status is 0.

Persistence repeated stage 1's ordering bug — the default was assigned after
the stored document was applied, so it silently overwrote it. Invisible until
the first restart; the gate caught it.

The same release fixed `console-blit` translating a key *release* into a
keystroke. Blit publishes all three transitions and labels them, verified on
the live stream:

```text
{"kind":"key","code":1,"pressed":true,"repeat":false}    press
{"kind":"key","code":1,"pressed":true,"repeat":true}     autorepeat
{"kind":"key","code":1,"pressed":false,"repeat":false}   release
```

The adapter emitted a key for every one. A single tap of Escape arrived as two
keystrokes, so anything the desktop toggled went on and straight back off, and
whether it landed on depended on how many autorepeats fitted inside the hold.
Autorepeat deliberately still produces a keystroke.

`packaging/HIBR_VERSION` said 0.99.100 while the host ran 0.99.127, and
`build-deb.sh` does not rebuild the adapter — so every release since had
shipped a `console-blit.so` built against headers two dozen point versions
behind the runtime loading it. The ABI was 16 in both, which is why it worked;
nothing was checking. Issue #81 tracks the missing check.

### 0.1.462 and 0.1.463: the scene cache's precondition

**Host-accepted** for the trail: *"The screen looks better"*. The menu bar
flicker reported immediately afterwards is a separate matter — screen and cell
data agree exactly and the menu bar is absent from the cell data, so that is
the desktop owner's behaviour, which the cache had been hiding.

Recorded in full in [ADR 0070](adr/0070-the-scene-cache-needs-a-backend-that-preserves-render-targets.md).

Five reproductions were built for #72 and every one passed — all of them
against SDL's software renderer, where the cache's precondition holds. 0.1.462
added `BLIT_SDL_SCENE_CACHE` so the precondition could be measured on the
hardware where it does not, and the A/B settled it:

```text
cache on     stale 170 of 329 cells drawn; 740 under a busier saver
cache off    stale 0 across six samples, on a harder workload --
             10875 cells drawn and 1015 changing between the bracketing reads
```

0.1.463 made the cache conditional on the `software` renderer. Cost measured
on the same host before choosing the default: 3.3 ms to render, 0.6 ms to
present, against a 16 ms budget, no missed deadlines. After deployment with no
override set, the host reports `renderer = opengles2`,
`render paths = {'full-redraw': 6}` and `pointer_cache_hits = 0`.

### 0.1.464: evdev numbers letters by QWERTY row

**Validated, not host-accepted.** The gate has teeth (55 failures against the
old arithmetic) and the fix is deployed, but the operator has not confirmed
that the keyboard types correctly on the machine; their next report concerned
the menu bar and the mouse. Host acceptance for this release is outstanding
and must not be inferred from the gate. The standing reason this distinction
is kept: the person who can see the screen is not the one running the tests.

Reported as "the keyboard does not type qwerty, g is , and f is d". The
adapter computed a letter as `'a' + code - KEY_A`, which assumes the codes run
alphabetically. evdev numbers keys by position in the QWERTY rows:

```text
KEY_A 30  KEY_S 31  KEY_D 32  KEY_F 33  KEY_G 34
KEY_Q 16  KEY_W 17  KEY_E 18  KEY_Z 44  KEY_M 50
```

F came out as `d`, the reported symptom exactly. Because `KEY_Z` is 44, the
range `KEY_A..KEY_Z` spanned the home row and its punctuation rather than the
alphabet, so q w e r t y u i o p x c v b n m and every punctuation key
produced no character at all. The digits were right by luck.

This survived because the keyboard measurement taken earlier in the session
used a plain terminal session, which goes through Blit's own keycode table and
is correct. A desktop goes through the adapter's — a different path with no
gate at the time.

The mapping is now a table in `src/blit_evdev_char.h`, shared by the adapter
and `tools/tests/evdev_char_gate.c`. The gate asserts all 26 letters rather
than a sample for a specific reason: the old arithmetic was correct for
exactly one letter, `KEY_A`, where the offset is zero, so a spot check would
have passed against it. Teeth: with the arithmetic restored the gate reports
55 failures, including `code 33 gave 'd' (100), wanted 'f'`.

### What this sequence says about the suite

Two faults in four releases were invisible to a green suite for the same
reason: the gate environment is not the deployment environment. The renderer
fault needed a GPU the container does not have (#79); the keyboard fault
needed the adapter's key path, which had no gate until #76/#77 added one.

A passing suite means the behaviour the suite can reach is correct. It is not
a statement about the behaviour it cannot.

## 0.1.466 candidate: Hibr input translation, before host acceptance

Issues #76 and #77 came from one instrumented Hibr primary on the physical
0.1.465 host, not from visual inference. API injection placed the pointer over
the visible Theme control and sent both primary-button edges. Blit's bounded
trace routed them to session 4 and focused that session; the adapter log
accepted both events in Hibr mouse mode 2; the authoritative surface revision
did not change. An injected Linux F10 event followed the same route and was
accepted by the adapter without changing the surface, while Escape advanced
the revision and opened Hibr's menu.

Issue #78 records the provider-state defects found while following that same
frame from Hibr's public console contract to Blit's surface boundary.

The native Hibr console decoder names mouse cells from zero and accepts
`f1` through `f12`. `console-blit` was forwarding Blit's one-based stream
cells unchanged, deriving fallback rows with an 8-pixel divisor despite its
16-pixel cell height, mapping no function keys, swapping normalized middle and
right buttons, and dropping wheel events. Fallback geometry now comes from
the session's authoritative cell pixels, so font scale does not create a
second mismatch. Hibr source was read for its public provider vocabulary and
was not modified.

`tools/tests/adapter_input_gate.sh` feeds the shipped adapter actual WebSocket
frames. The pre-fix source failed 17 checks: four coordinate cases, twelve
function keys, and modified F10. The candidate passes against the exact
package-pinned released Hibr 0.99.191 checkout:

```text
ADAPTER_INPUT_PASS pointer_origin=zero buttons=3 wheel=2 function_keys=12 resolved_text=6
```

Issue #78 extended the audit from input vocabulary to provider semantics.
Against the same released checkout, the provider gate proves pane ownership,
drop/clipping/stack order, watched application output, quiet waits, wide and
combining UTF-8, idempotent shadows, and the absence of steady-state resize
polls. The old `console resizing` implementation issued a synchronous geometry
request after each input wait, directly coupling mouse event rate to API work:

```text
ADAPTER_PROVIDER_PASS pane_ownership=1 watched_wakeup=1 utf8_width=1 resize_poll=0
```

The production adapter and server compile with TCC 0.9.27 against that
checkout. The complete local pre-deployment run on 2026-10-14 reports 69
passed, 0 failed, and four explicit skips: the separately invoked QEMU oracle,
a not-yet-built package artifact, and two physical-live gates. These are still
implementation and local-gate results only. Package reproducibility,
deployment, F10 activation, visible button/wheel interaction, immediate
terminal output, and stable physical rendering remain pending and must be
recorded before #72, #76, #77, or #78 is closed.
