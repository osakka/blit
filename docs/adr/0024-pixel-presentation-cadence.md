# ADR-0024: Pixel presentation cadence follows the producer and output path

- Status: retired in 0.2.22 (ADR 0184)
- Date: 2026-10-08

## Context

API pixel frames are used by games and emulators. A single boolean
`render_pending` flag allowed several accepted frames to collapse before the
SDL scene cache presented them. On the physical host this produced fewer
presentation passes than the 60 Hz producer, even though terminal damage still
reached the display cadence.

Direct KMS has a different constraint: a 60 Hz producer must not cause the
latest image to be replayed at every slot of a 240 Hz output mode. That adds
page-flip work and artificial deadline misses without producing new content.

## Decision

SDL external pixel surfaces maintain a per-output pending count. The refresh
scheduler continues presenting while accepted pixel work remains, and expires
stale pending work after 100 ms without a new frame so a disconnected producer
cannot create a busy loop. The image itself remains latest-frame-wins for low
latency; the count controls presentation service, not historical frame
retention.

Direct KMS retains its producer-driven presentation behavior. It does not use
the SDL pending replay policy, so a 60 Hz stream is not replayed at a 240 Hz
mode. Its page-flip cadence remains governed by the KMS path and output mode.

## Consequences

- SDL physical pixel workloads can sustain output-refresh presentation without
  silently losing scheduler turns between frames.
- A stalled network client cannot leave Blit presenting forever.
- KMS high-refresh fixtures do not report artificial deadline failures caused
  by replaying unchanged images.
- Accepted, presented, and pending counts remain distinct diagnostics; this
  does not promise that every historical network frame is scanned out.

## Verification

- Physical 1920×2160@60: 120 accepted frames at 59.568 Hz, 125 presentation
  passes, and `pixel_frames_pending=0` after drain.
- Exact-current two-output VKMS: `PIXEL_FRAME_RATE_PASS`,
  `KMS_API_PIXEL_PASS`, and `KMS_CAPTURE_PASS` with the 240 Hz output mode.
- Hibr four-pane, input, font, and terminal conformance fixtures remain green;
  Hibr source is unchanged.
