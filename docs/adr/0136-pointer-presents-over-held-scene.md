# ADR 0136: present pointer motion over the last committed scene

- Status: accepted
- Date: 2026-10-19
- Issue: Blit#101

Terminal quiet batching and the native Hibr multi-PTY frame barrier protect
scene atomicity. They must not hold software-pointer motion behind a scene
that has already been presented. While either fence is active, a due output
may copy its existing committed scene and composite the current pointer at
that output's normal refresh deadline.

This path does not draw terminal cells, images, effects or overlays from
pending state; update scene identity or cached view metadata; consume terminal
or pixel damage; or advance a screenshot's presented revision. KMS defers the
pointer when the output has no initialized committed frame. SDL uses its
committed scene texture and likewise defers if none exists. When the fence
releases, the ordinary scene commit consumes and presents accumulated damage.

The existing compositor and software pointer are reused for both backends.
This is not a hardware cursor, a second renderer or a relaxation of atomic
scene commits. It addresses input-driven pointer stalls; Hibr window-drag
cadence and physical input-to-photon latency remain measured acceptance items.

Memory-backed production-compositor gates verify every pixel for repeated
pointer positions and clipped 64px art, flip and modeset paths, overlays,
uninitialized outputs, retained damage/metadata, and the complete frame after
release. The SDL software-renderer gate checks the same held-frame/release
contract. Physical acceptance uses the host bounded pointer sweep and remains
open until measured on the user's active display.
