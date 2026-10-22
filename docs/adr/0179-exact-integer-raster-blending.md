# ADR0179: exact integer raster blending without per-channel division

- Status: accepted for development; live acceptance pending
- Date: 2026-10-22
- Scope: internal125/122; canonical raster compositor, no protocol/font change

Native composition invokes the shared raster blend callback per glyph pixel.
The TCC coverage branch emits six integer divisions, including coverage0/255.
This is confirmed work, not function-level attribution of live frame time.

For8-bit channels and alpha, replace `(product+127)/255` with the exact
bounded identity `((product+128)*257)>>16`. Products are0..65025; the largest
intermediate fits unsigned32 bits. Keep one canonical production blender.
Coverage0/255 can return the masked24-bit background/foreground directly.
Do not apply an alpha0 shortcut to arbitrary premultiplied RGBA: existing
invalid-premultiplication saturation behaviour must also remain identical.
Retain invalid glyph/coordinate/format returns, foreground-independent colour
glyphs, source stride, clipping consumers and all antialiasing samples.

Require exhaustive production-versus-frozen-reference channel/alpha tests,
mixed RGB/high-byte and padded-stride/bounds tests, zero integer-division
instructions in the TCC production blend function, and a bounded same-build
CPU-time comparison. The reference exists only in tests, never runtime.
Keep normal font/native/software/GL composition and damage gates. No threshold
relaxation, compiler/backend switch, font mutation or reduced colour precision.
Local blend throughput is not whole-frame or physical input latency acceptance.
