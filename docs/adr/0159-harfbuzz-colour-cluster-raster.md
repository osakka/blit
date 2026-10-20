# ADR0159: shape colour clusters with the public HarfBuzz C API

- Date: 2026-10-20
- Status: shared raster implemented/verified locally; terminal integration pending, not deployed
- Tracking: Blit107/110; follows ADR0157/0158

## Decision

Use HarfBuzz through its supported C API for OpenType cluster shaping rather
than write another GSUB parser, freeze font-specific glyph-ID tables or embed
a second terminal engine. TCC still compiles Blit's C. Pin unmodified public
6.0.0 headers and provenance, and require the OS-maintained libharfbuzz.so.0
runtime>=6.0.0. Do not bundle an old6.0.0 binary or replace distribution security
updates. Add the explicit Debian dependency and fail font-context construction
if the runtime does not satisfy the compiled API baseline. No silent shaper
substitution or browser/Hibr changes.

One reusable HarfBuzz buffer and a referenced font on the existing FreeType
colour face belong to each shared raster context. Set the face's size before
creating the referenced font; release the shaper before the FreeType face.
All use stays on the existing single owner thread. Shape ordered bounded
cluster values, remove default-ignorable placeholders after shaping, require
one nonzero colour glyph and render its glyph index through the EXISTING
FreeType BGRA/premultiplied downsampler. Do not create backend-specific shapers
or colour compositors. Full keys and1/2-column allocation remain canonical.

Expose a private C cluster-raster entry point alongside the scalar wrapper,
using one cache/composition path. Preserve existing scalar selection policy
in this bounded step. Explicit text selectors must not become colour emoji;
unsupported cluster/text forms return failure rather than silently dropping
visible scalars. Comprehensive default presentation/text fallback and terminal
grouping remain acceptance work, not inferred from the new entry point.

## Verification / rollout boundary

Pin and check every header hash. Compile/link with the installed TCC. Test
actual colour rasters for every corpus sequence, premultiplication/ink,
cell allocation, cache reuse/collisions, profile/size switches and unsupported
forms. Treat single-scalar presentation policy separately from multi-scalar
colour glyph resolution. Check object lifetimes with Valgrind. All prior
font/native/SDL/backend/terminal gates remain required.

Keep this entry point unadvertised on the network until renderer consumption
and PTY/API segmentation/width coordination pass end-to-end. Do not activate
parser grouping before renderers can consume the result. No live/package
deployment follows from a module test. Font107/emoji110 remain open.

Primary lifecycle/API references:
https://harfbuzz.github.io/harfbuzz-hb-ft.html and
https://harfbuzz.github.io/harfbuzz-hb-buffer.html .
