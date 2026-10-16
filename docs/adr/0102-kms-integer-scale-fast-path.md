# ADR 0102: Use a bounded integer-scale fast path for opaque KMS graphics

- Status: accepted
- Date: 2026-10-16
- Scope: direct DRM/KMS graphics composition

## Context

The live direct-KMS configuration uses one physical 1920x2160 output split
into two 1920x1080 panes. Hibr's wallpaper was observed through the API as an
opaque 960x1080 image placed at 1920x2160. The KMS compositor's general scaler
recomputed source coordinates with integer division for every destination
pixel. That made a full wallpaper redraw compete with the 60 Hz presentation
deadline and was the measured common cause of slow pointer movement and broken
fast-motion redraws in this configuration.

The existing general scaler remains required for non-integer sizes, clipping,
and alpha images. Hibr is outside this change and remains unmodified.

## Decision

When an opaque 24-bit RGB or opaque 32-bit RGBA image has an exact integer
destination scale, and each scale factor is at most eight, replicate each
source pixel across its destination rectangle. Apply the same output and
damage clipping as the native path, and keep the general scaler for all other
images.

The fast path is an implementation optimization, not a change to the Kitty
placement contract. Source rectangles, z bands, clipping, and output
coordinates remain owned by the existing graphics collector and compositor.

## Consequences

- The current 2x wallpaper path avoids per-destination-pixel coordinate
  divisions during full-scene redraws.
- Alpha and non-integer image semantics remain in one general implementation.
- The direct-KMS compositor still begins incremental frames from the complete
  presented buffer as required by ADR 0101.
- The optimization must be validated by the KMS pixel gate and by live timing
  counters before visual or physical acceptance is claimed.

## Verification boundary

- `tools/tests/kms_compose_gate.sh` passes after the fast path is compiled.
- The TCC server build must report release `0.1.511`.
- Live verification must use the unchanged direct-KMS, one-output, lower-pane
  configuration and compare `max_present_ms`, missed deadlines, and the
  resulting screenshot against the 0.1.510 baseline.
- This does not by itself prove physical mouse or keyboard acceptance.
