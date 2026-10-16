# ADR 0096: The Kitty APC buffer carries the full Hibr chunk

- Status: accepted
- Date: 2026-10-16
- Scope: terminal stream parsing, Kitty graphics, Hibr wallpaper uploads
- Related: Blit issue #83, [ADR 0084](0084-kitty-continuations-use-the-current-upload.md)

## Context

The live host received Hibr's wallpaper stream as Kitty APC strings, but the
graphics inventory stayed empty and the final chunk was rejected as
`incomplete-data`. The trace recorded each continuation at exactly 4095 bytes.
That was the parser's storage limit, not the producer's complete chunk: Hibr
uses 4096 base64 bytes per chunk and the Kitty control header occupies the
same APC string.

When the fixed 4096-byte terminal-string array filled, Blit silently dropped
the tail of every chunk. The image decoder therefore lost base64 quartets and
could not complete the wallpaper. Small Kitty fixtures passed, which is why
the defect escaped the earlier continuation gate.

## Decision

The terminal parser owns a bounded 16 KiB APC/DCS string buffer. It is large
enough for Hibr's 4096-byte base64 chunk plus its control header and leaves
bounded room for protocol metadata. It is only stream storage; graphics image
limits and decoded allocation limits remain unchanged.

The graphics trace gate now builds a TCC probe that emits both a 64x64 RGB
Kitty image and a real 64x64 RGBA PNG (`f=100`) in Hibr's 4096-base64-byte
chunk shape. Both paths require every payload to be accepted, the image to be
complete, and the PNG to decode to exact RGBA dimensions. A PNG is therefore
kept in the graphics scene; it is never converted into half-block or ordinary
terminal-cell text as an implicit fallback.

## Alternatives rejected

1. Truncate at 4096 and let the decoder recover: this loses data and makes
   wallpaper correctness depend on chunk alignment.
2. Change Hibr's encoder: Hibr remains an external, unchanged source of
   terminal semantics.
3. Allocate an unbounded string: a PTY is untrusted input and must keep a
   finite parser budget.

## Verification

- `tools/tests/graphics_trace_gate.rb` passes the 4096-byte-chunk boundary.
- The same gate passes a multi-chunk `f=100` PNG and verifies format 32,
  64x64 dimensions, and 64x64x4 decoded bytes.
- The gate records full payload lengths above 4095 and `rejected=0`.
- The live host must be redeployed with this Blit build before its wallpaper
  trace can be considered accepted.
