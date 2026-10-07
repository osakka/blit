# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The source build for this cycle is `0.1.212`; package hash is recorded
  `05a26d6d33c05602f1e44cc04c1c5c4d3a3ecd9c0a1cfa3b4251a2486ce34057`.
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
  Hibr acceptance, and the four-pane Hibr matrix.
- The QEMU input-only fixture currently reaches `BLIT_INPUT_READY` but does
  not reach its completion marker for either 0.1.211 or 0.1.212; this is an
  unresolved test-harness/runtime gate, not counted as a pass.

## Real-host evidence

The authorized host at `192.168.10.54:8080` currently reports:

- Blit `0.1.210`, healthy SDL3/OpenGL presentation.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh Hibr session rendered a legible amber desktop framebuffer at
  1920×2160 with no diagnostic Kitty overlay; the session was removed after
  capture.

## Open release gates

1. Install and verify `0.1.212` on the real host. The currently installed
   `0.1.210` has not yet been replaced by the new package, so the scene-cache
   and pointer diagnostics are not yet physical-host evidence.
2. Repair or isolate the QEMU input-only fixture after `BLIT_INPUT_READY`, then
   obtain exact-build evidence for keyboard, pointer, button, wheel, and
   multi-device routing.
3. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.

Until both gates have authoritative runtime evidence, the project must not be
described as fully released or physically multi-monitor validated.
