# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The source build for this cycle is `0.1.218`, which includes the
  Hibr-compatible Unicode 15.1 width table and regression vectors for
  variation selectors and zero-width joiners.
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
  executable; bundle, extracted Debian package, and QEMU KMS runs all report
  `server_version:0.1.218` with `api_version:0.1`.
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
- The exact 0.1.218 TCC build passes the two-output KMS pixel gate, the
  four-pane Hibr matrix, and the input-only fixture with
  `blit.input_wait=8`: keypad, multi-mouse, button, wheel, motion, stream, and
  PTY delivery all pass. The input-loss variant additionally reports
  `input_device_lost` diagnostics, retains the configured routes, and passes
  device-loss/reconnect recovery. Its terminal conformance gate additionally passes
  variation-selector and zero-width-joiner cursor-position vectors, plus the
  Hibr `DT_BIDI`/`HIBR_GFX` defaults and explicit override vectors.
- Surface and scrollback snapshots preserve complete zero-width UTF-8 cell
  sequences; a five-extension combining vector and its cursor position pass.
- Revisioned API cell updates accept wide UTF-8 glyphs and zero-width
  extensions, expose continuation width metadata, and pass the public API
  regression vector.

## Real-host evidence

The latest reachable observation of the authorized host at
`192.168.10.54:8080` reported:

- Blit `0.1.210`, healthy SDL3/OpenGL presentation.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh Hibr session reached `running`; its revisioned surface snapshot
  contained the complete 240×135 terminal grid with the expected amber theme
  (`#ffb000` foreground on `#1a0f00` background).
- The corresponding physical SDL screenshot was almost entirely black, with
  only a small glyph/pointer visible. This is a confirmed presentation defect
  in the installed `0.1.210` runtime, not evidence of missing Hibr output.
- A second capture waited 25 seconds after a uniquely named Hibr session
  reached `running`; the result was still black, with one isolated glyph.
  The host reported `renderer:opengl`, `refresh_hz:59.990`, and
  `missed_deadlines:77`, with no API errors. The bounded session was then
  deleted and the host returned to zero sessions.
- The same stock Hibr desktop, run against the current TCC `0.1.218` build
  with SDL's software renderer, produced a settled screenshot containing the
  desktop bar, wallpaper, icons, and themed cells. This isolates the physical
  failure to the installed old runtime; it is not reproduced by the current
  source path.
- The captured framebuffer contained no red diagnostic box. Any red box seen
  by an operator must be correlated with a timestamped screenshot and the
  overlay/graphics API state; it is not present in the current capture.
- A bounded upload of the verified `0.1.218-1` package was rejected with HTTP
  403 because the host requires `X-Blit-Update-Token`; the host therefore
  remains on `0.1.210`. No token was guessed and no unverified remote install
  was attempted.

## Open release gates

1. Provide/use the supervised update token or another authorized service path,
   then install and verify `0.1.218`. The physical renderer defect cannot be
   assessed against the current source until the current runtime is actually
   installed.
2. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.
3. Capture and inspect the complete physical-host framebuffer after installing
   the current package. The VKMS guest has generated and sampled both PPMs,
   but its kernel lacks 9p transfer, so binary screenshot inspection remains a
   separate physical-host gate.

Until all three gates have authoritative runtime evidence, the project must not be
described as fully released or physically multi-monitor validated.
