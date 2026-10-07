# Verification status

This ledger records the latest evidence for the Blit delivery contract. It is
an evidence record, not a claim that the complete product is released.

## Source and package

- Repository: `main` at the source commit for this ledger update.
- Production C was compiled with TCC.
- The source build for this cycle is `0.1.211`; package hash is recorded
  `bd4f821888f738a7569e93f41d8812b34232ba743b5b2c557643e5538d8244cc`.
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
- The exact 0.1.211 TCC build passes the two-output KMS pixel gate, default
  Hibr acceptance, multi-device input routing, and the pane matrix.

## Real-host evidence

The authorized host at `192.168.10.54:8080` currently reports:

- Blit `0.1.208`, healthy SDL3/OpenGL presentation.
- One physical output, `DP-1`, active at 1920×2160 and 59.99 Hz.
- A fresh Hibr session rendered a legible amber desktop framebuffer at
  1920×2160 with no diagnostic Kitty overlay; the session was removed after
  capture.

## Open release gates

1. Install and verify `0.1.211` on the real host. The currently installed
   `0.1.208` upload handler blocks during large package uploads, so the fixed
   update path cannot bootstrap itself through that process.
2. Validate at least one physical multi-monitor arrangement, including
   connector discovery, independent presentation, pointer routing, and Hibr
   primary/join behavior. The current host exposes only one physical output.

Until both gates have authoritative runtime evidence, the project must not be
described as fully released or physically multi-monitor validated.
