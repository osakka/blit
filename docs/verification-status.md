# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The source build for this cycle is `0.1.213`, which includes the
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
  pane layouts, Hibr display/window control, focus, resize, input routes,
  keyboard, pointer, multi-mouse, and WebSocket input checks.
- The exact 0.1.212 TCC build passes the two-output KMS pixel gate, default
  Hibr acceptance, the four-pane Hibr matrix, and the input-only fixture with
  `blit.input_wait=8`: keypad, multi-mouse, button, wheel, motion, stream, and
  PTY delivery all pass.

## Real-host evidence

The authorized host at `192.168.10.54:8080` currently reports:

- Blit `0.1.210`, healthy SDL3/OpenGL presentation.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh Hibr session rendered a legible amber desktop framebuffer at
  1920×2160 with no diagnostic Kitty overlay; the session was removed after
  capture.
- A single bounded 0.1.212 upload attempt timed out against the legacy
  receiver; the host remained on `0.1.210`. After the connection cleared, its
  update status became `failed` while the gateway remained healthy. No second
  upload or unverified remote shutdown was attempted.

## Open release gates

1. Recover the legacy receiver through the host’s supervised/manual service
   path, then install and verify `0.1.212`. The currently installed `0.1.210`
   cannot cancel its own stalled upload, so the new cancellation endpoint is
   not yet available on the physical host.
2. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.

Until both gates have authoritative runtime evidence, the project must not be
described as fully released or physically multi-monitor validated.
