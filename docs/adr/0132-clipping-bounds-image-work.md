# ADR 0132: clipping bounds image work, not just stores

- Status: accepted
- Date: 2026-10-18
- Related: ADRs 0112/0114; reported direct-KMS slowdown after hotplug
- Issue: [Blit#101](https://git.home.arpa/itdlabs/blit/issues/101)

The general fractional image scaler tested damage clipping inside a loop over
every destination pixel. This preserved pixels but made a tiny pointer repaint
scan an entire wallpaper. The native-size and opaque integer-scale paths
already clipped differently. A second monitor with a different placement
scale could therefore have a radically different repaint cost.

Intersect placement, damage and physical framebuffer before entering the
general scaler loops. Source sampling remains relative to the original image
placement, never rebased to the clip. Preserve RGB/RGBA alpha, source cropping,
negative origins and outside-damage pixels. No protocol or backend switch.

`tools/tests/kms_graphics_clip_gate.c` runs the actual compositor, compares
clipped output to a full-render reference, and requires a 24x32 repaint to
cost less than 10% of a 1919x1079 fractional repaint. The old loop fails at
38.1% (178884µs versus 469154µs for 20 repaints). This demonstrates a code
bottleneck, not the cause or complete resolution of a particular physical
host's slowdown. Real CPU/render cadence measurements remain required.
