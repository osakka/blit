# ADR 0105: KMS screenshots capture the composed frame

## Status

Accepted — 2026-10-16.

## Decision

The direct-KMS screenshot request forces one bounded present for its output.
The resulting capture reads the output after the current scene has been
composed and copied into the page-flip buffer, immediately before the buffer
is submitted. It must not read the previous scanout buffer while the current
frame is still in the private scene buffer.

## Context

Blit composes terminal cells, pointers, effects, and Kitty/Sixel graphics in a
RAM scene buffer and then copies that scene into the KMS back buffer. The
previous capture point was before that copy, and the API request did not force a
present when the surface itself was unchanged. A screenshot could therefore
show an older moving frame even though the authoritative surface and the next
page flip were correct. This made the diagnostic artifact indistinguishable
from a stale-pixel renderer failure.

## Consequences

- A screenshot is evidence of the frame being submitted by that present call.
- The screenshot remains a diagnostic observation and does not change the
  composition or timing policy.
- Capture cost is still visible in runtime timing; performance measurements
  must use the screenshot-free runtime counters.

## Verification

The controlled KMS motion test writes a moving terminal cell, then clears the
surface and writes `done`. The final screenshot must show `done` and must not
contain a prior motion frame. The KMS composition gate and live API capture
must agree on the same lower-pane surface state.
