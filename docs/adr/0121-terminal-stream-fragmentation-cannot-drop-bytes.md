# ADR 0121: terminal stream fragmentation cannot drop bytes

- Status: accepted
- Date: 2026-10-17
- Issue: [Blit #94](https://git.home.arpa/itdlabs/blit/issues/94)

## Evidence

The authenticated 0.1.534 desktop has an incomplete half-block wallpaper.
Its cell snapshot also contains blank rows below row 22. This is different
from stale scanout pixels: the canonical state itself is incomplete.

`service_session_output` reads up to 65535 bytes, but the UTF-8 pending-prefix
join buffer holds only 772 bytes. If a previous read ended inside a scalar,
the next large read returned without parsing any of its bytes, leaving the
prefix pending. Later reads could continue being discarded. Byte counters
still increased, hiding the loss. A real-function pipe test reproduces it:
E2 followed by a 4096-byte read beginning 96 80 and ending with CUP/END loses
both U+2580 and END while reporting 4097 bytes received.

## Decision

Feed arbitrary-length input in bounded 768-byte slices through the existing
parser, whose scratch buffer also has room for the pending scalar. The
session remains the only owner of UTF-8, CSI, string, pen and cursor state.
There is no second parser, whole-read allocation, recursive feed, or change
to PTY read size, rendering, backend or presentation scheduling.

The core parser retains its bounded-buffer check. All production entry calls
pass through the slicing entry point. One-shot reference parsing and ten
external fragmentation sizes (1 through 65535 bytes, including 767/768/769)
must produce identical codepoints, colours, attributes, combining sequences,
cursor and completed OSC title for a large mixed ANSI/UTF-8 stream. The pipe
regression tests the actual read/service entry as well.

## Implementation research and limits

[Kitty's parser](https://github.com/kovidgoyal/kitty/blob/master/kitty/vt-parser.c)
keeps a persistent UTF8Decoder in parser state and advances by its consumed
byte count in consume_normal, independently of transport read boundaries.
That is the relevant invariant, not copying Kitty's renderer or code.
Blit's correction retains its own single state machine and equivalent
lossless incremental feeding. No Hibr source changes are needed.

The tests prove fragmentation invariance for their corpus, not universal
terminal conformance. The exact deployed package must separately pass live
wallpaper and window-movement checks; local rendering/cadence tests did not
expose this authenticated application's larger UTF-8 stream.
