# ADR 0044: Separate high-refresh scheduling from scanout evidence

- Status: accepted

## Context

Blit must schedule each output at the maximum refresh reported by its active
mode, but a reported mode is not proof that a virtual machine or driver can
deliver page-flip events at that cadence. The QEMU fixture runs without KVM
on this host and uses VKMS; its timer and page-flip delivery are not a
physical monitor or a hardware scanout path.

For pixel streams, accepting frames proves transport and bounded ingestion;
completed DRM page flips prove presentation. Treating the first as proof of
the second hides the timing failure this project is intended to expose.

## Decision

Maintain separate acceptance boundaries:

1. scheduler acceptance proves the selected mode, per-output deadline, and
   refresh-aware API diagnostics;
2. frame-transport acceptance proves accepted frames and zero frame errors;
3. scanout acceptance proves completed page flips and deadline behavior on the
   target driver; and
4. physical high-refresh acceptance remains open until a real DRM/KMS host
   with the target monitor mode is measured.

QEMU TCG/VKMS results record actual present counts and missed deadlines. A
failed virtual scanout workload is not converted into a pass, and a scheduler
pass is not reported as 120/240 Hz physical performance. Async page flips are
diagnostic experiments only.

## Consequences

The release manifest distinguishes policy, transport, and scanout evidence.
The current QEMU high-refresh pixel workload accepted all 240 frames with no
transport errors, but the no-KVM VKMS run completed only 84 presents and
recorded additional deadlines. The next performance work must either improve
the direct-KMS submission path or demonstrate that the limitation is entirely
in the virtual driver, followed by real-hardware validation.
