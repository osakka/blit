# ADR 0049: Retire KMS work only after output submission

Status: Accepted

## Context

Direct KMS may have one page flip in flight for an output. When a render pass
encounters that condition, Blit defers the output and leaves its back buffer
untouched. The caller must therefore distinguish an output whose flip was
submitted from one whose work was deferred.

Previously, the server retired pending pixel-frame credits and cleared global
pointer damage for every requested output after the KMS call returned. That
could discard a producer update or lose a pointer redraw while the CRTC still
owned the previous frame.

## Decision

`blit_kms_present()` returns submitted and deferred output masks. Pixel-frame
credits are consumed only for submitted outputs. Pointer damage remains live
while any requested output is deferred. A page-flip or recovery modeset is the
presentation boundary; entering the renderer is not.

## Consequences

This preserves input and frame state across a slow driver without pretending
that a deferred output presented. It does not convert QEMU TCG/VKMS page-flip
latency into a high-refresh pass; that acceptance boundary remains separate.
