# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The previous fully validated source build is `0.1.222`, which includes the
  Hibr-compatible Unicode 15.1 width table and regression vectors for
  variation selectors and zero-width joiners.
- The current TCC source build and turnkey package are `0.1.232`; the complete
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
- The Debian artifact built from the current implementation is
  `blit_0.1.232-1_amd64.deb`, SHA-256
  `d411b975f8d4e494afe67b2f0fbb29b4f96a010b41723f89701489ba279a19aa`.
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
- Surface and scrollback snapshots preserve complete zero-width UTF-8 cell
  sequences; a five-extension combining vector and its cursor position pass.
- Revisioned API cell updates accept wide UTF-8 glyphs and zero-width
  extensions, expose continuation width metadata, and pass the public API
  regression vector.

## Real-host evidence

The latest reachable observation of the authorized host at
`192.168.10.54:8080` reported:

- Blit `0.1.232`, healthy SDL3/OpenGL presentation, reached through the
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

## Open release gates

1. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.
2. Complete the remaining Gate 6 process-restart cases for primary and joined
   Hibr processes; session termination during writes/resizes, stream
   reconnect/replay, input-device loss, output loss, and the two-hour PTY soak
   now have evidence above.

Until both gates have authoritative evidence, the project must not be
described as fully released or physically multi-monitor validated.
