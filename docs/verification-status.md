# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The source build for this cycle is `0.1.221`, which includes the
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
  executable; the Debian package, local conformance run, live host, and QEMU
  KMS run all report `server_version:0.1.221` with `api_version:0.1`.
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
- The exact 0.1.221 TCC build passes the two-output KMS pixel gate, the
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

- Blit `0.1.221`, healthy SDL3/OpenGL presentation.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh Hibr session reached `running`; its revisioned surface snapshot and
  settled screenshot contained the Hibr desktop bar, icons, controls, and
  pointer. Live diagnostics reported one `DP-1` output at 1920×2160 and
  59.99 Hz, one keyboard, one pointer, no errors, and a 16 ms refresh-aware
  presentation interval.
- The live host currently exposes one physical output. Multi-display behavior
  is therefore proven by the two-output VKMS/QEMU fixture, not by this host.

## Open release gates

1. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.
2. Complete the Gate 6 fault/reconnect and multi-hour soak campaign.

Until both gates have authoritative evidence, the project must not be
described as fully released or physically multi-monitor validated.
