# ADR 0192: A session's output is taken in full buffers, not one PTY read per loop pass

- Status: accepted
- Date: 2026-10-26
- Scope: main loop, session output service

## Context

The owner reported that YouTube on the Hibr desktop played at a low frame
rate. Measured live on 0.2.32, with the effects on and off:
- **The video reached Blit at about 8 frames a second** in every effects
  state. Each frame is a 1771×902 RGB kitty image, 4.8 MB, sent as about
  6.4 MB of base64 chunks through the PTY.
- **Blit's counters:** 9,523 loop passes a second, 9,430 PTY reads, 38.7 MB
  a second, 4,103 bytes per read.

`service_session_output` read once per pass into a 64 KiB buffer. Its
comment explains the size: a Hibr flush is one write, and small reads would
fragment it. But a PTY hands over at most one line-discipline buffer, about
4 KiB, per `read`, so the buffer never filled. A frame cost about 1,560
passes of the loop, about 105 µs each on the owner's machine: a ceiling of
6 to 8 frames a second.

Hibr found a matching ceiling on its own side at the same time: hold parsed
the APC byte by byte, about 150 ms a frame. Hibr 0.99.352 fixes that, and
makes hold drain its own program's PTY the same way. Either ceiling alone
held the video at about 8 frames a second.

## Decision

**Each service call fills the buffer from what is already queued,** reading
until it is full or the PTY reports nothing more (`EAGAIN`). Then it
consumes the buffer once, as before.
- The work per pass stays bounded by the buffer's 64 KiB.
- The drain never waits: a small write is consumed as soon as it is queued.
  After the first read, it reads again only while a zero-timeout `poll`
  reports data. So it cannot block even on a descriptor left in blocking
  mode. Blit's PTYs are always non-blocking, but `surface_damage_gate`
  feeds a blocking pipe, and an unguarded drain hung it there.
- End of file or an error is reported only when nothing was read in that
  call, so data already in hand is consumed first, and the next pass sees
  the condition again.

This is what the 64 KiB buffer was meant to do: take a flush in as few
pieces as possible.

## Consequences

- **Fewer loop passes per byte of bulk output:** about 10 times fewer on
  the gate machine (Linux 7.0), at 42.6 KB per read instead of 4.1. How
  much a pass takes depends on how fast the kernel refills the line
  discipline between reads. The owner's machine runs Linux 6.12, so its
  live bytes per read is the number that decides.
- A pass that takes a full buffer parses up to 64 KiB, as the old code
  already allowed. The parser contract is unchanged.
- Base64 over the PTY, about 51 MB a second at 8 frames a second, may be
  the next ceiling. Shared-memory or file transfer (kitty `t=s`, `t=f`) is
  the option then, a separate decision with Hibr.

## Evidence

- **`terminal_ingest_gate` (new):**
  - three video-sized kitty frames, chunked as Hibr sends them, arrive
    complete;
  - the bytes per read average at least 16 KiB;
  - a small write is still taken at once.
  - It passes on 0.2.33 at 42,597 bytes per read, and fails on 0.2.32 at
    4,088.
- **A local benchmark of 40 such frames:** 0.4 frames a second on 0.2.32
  against 2.6 on 0.2.33. The gate machine's DRM mock sleeps 1 ms on every
  pass, so the absolute rates are the mock's. The ratio is the change.
- **On the owner's machine:** to be measured with Hibr 0.99.352. Frames per
  second from the image IDs, hold's CPU, and Blit's bytes per read.
