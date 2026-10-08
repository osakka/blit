# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

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
