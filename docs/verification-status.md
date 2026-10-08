# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The current candidate is `0.1.251`. It builds with TCC and passes terminal
  conformance, SDL screenshot/overlay pixels, the hidden-cursor damage gate,
  and the complete Hibr font manifest gate. Its SDL backend uses a per-output
  glyph atlas, suppresses unchanged cell damage, and does not schedule frames
  for hidden cursor movement. The
  dummy frame gate measured 42.898 Hz during its bounded PTY workload; that is
  a software-dummy result, not a physical refresh-rate claim. A bounded
  wallpaper workload on the prior source-equivalent 0.1.241 build measured
  26.865 ms maximum full render and 61.387 Hz over its active 504 ms window.
  The QEMU/KMS acceptance below is being rerun for the image-cache change. The
  preceding 0.1.245 package was accepted by the authorized physical host and
  verified through its version endpoint.

### Current 0.1.251 source evidence

The exact TCC-built 0.1.251 binary passes the full VT/UTF-8/Hibr terminal
conformance suite, the font manifest gate, and the refresh-aware dummy frame
gate. Its bounded binary pixel-frame WebSocket gate accepted 30/30 RGBA
frames, including one fragmented frame, with zero frame errors and reusable
texture storage. The measured dummy ingest rate was 21,353 frames/second;
the renderer coalesced those updates into six frames at a measured 16.468 ms
render time. This is an API-ingest and renderer-path result, not yet a claim
of physical 60/120/240 Hz presentation. Direct-KMS pixel-frame capture remains
the next required evidence before calling the graphical emulator path ready.

The pixel path now invalidates only the affected graphics rectangle in cell
coordinates instead of forcing a full terminal-surface redraw. Optional
placement fields default to zero as declared by the OpenAPI schema. The
runtime diagnostics expose `pixel_frames_accepted` and `pixel_frame_errors`.

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

## Open release gates

1. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The executable
   `tools/tests/physical_multi_display_gate.rb` is ready for this check; the
   current host exposes only one physical output and returns exit status 77.

All software-side Gate 6 cases currently in scope—including primary supervisor
restart, joined-client reconnect, session termination during writes/resizes,
stream reconnect/replay, input-device loss, output loss, and the two-hour PTY
soak—have authoritative evidence above. Until the physical-monitor gate has
authoritative evidence, the project must not be
described as fully released or physically multi-monitor validated.
