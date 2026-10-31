# ADR 0146: Exactly representable integer glyph atlas UV boundaries

- Status: retired in 0.2.22 (ADR 0184)
- Date: 2026-10-19
- Scope: SDL glyph texture allocation and geometry batching

## Evidence

The new 1,575-glyph Spleen profile gives a 900px-high narrow atlas. Software
rendering of U+2581 loses its first lit row: observed row counts are
`[0,0,0,0,0,0,0,0,0,0,0,0,0,0,0,8]`, instead of two full bottom rows.
GLES2 passes the same fixture. Asset tests prove the protected U+2581 bitmap
is unchanged from Terminus; repeated software tests reproduce the failure.

Read-only inspection of pinned SDL software `SW_QueueGeometry` shows integer
source coordinates obtained by truncating normalized UV times texture extent.
Non-power-of-two extents can round an integer texel boundary downward during
the float round trip. Guard texels and nearest filtering do not fix a source
rectangle starting one row early. No SDL source modifications are made.

## Decision

Allocate narrow glyph atlases with power-of-two pixel extents. Normalized
integer texel boundaries are then exactly representable in binary float.
Cache actual texture dimensions in each output at creation/reconfiguration;
batch geometry consumes those dimensions, not an independently reconstructed
unpadded extent. Reset metadata on teardown. Keep existing guard texels and
nearest sampling. Unsupported extents use the existing immediate fallback.
Bound atlas extents to 8192; check glyph count before integer row arithmetic.

Do not add arbitrary epsilon offsets, renderer-specific glyph masks, texture
reuploads per frame, altered glyph bitmaps or a second geometry policy.
The immediate texture path already uses integer source rectangles. The wide
atlas already has a power-of-two 2048px extent and is unchanged.

## Consequences

Atlas allocation can increase (the current default becomes 512x1024).
It remains allocated/uploaded at font initialization, not per glyph/frame.
This is a correctness change, not proof of improved presentation speed.
Modern dynamic glyph cache design must retain exact source-boundary behavior.

## Verification

The same previously failing Spleen software fixture passes after the change,
as does GLES2. Expanded pixel oracle: 115 cells, 14,720 exact pixels including
ASCII and control icons on each local path. Production-code damage fixture
checks every integer UV round trip for representative power-of-two extents,
plus held-scene pointer and two-renderer counters on software/GLES2.
Full regression and physical validation remain pending.

The expanded oracle also passes for the original Terminus on both local
paths (14,720 pixels each). Full runner reports 108 pass/7 skip/0 fail, but its
dependency fingerprint guard rejects the run after Hibr 257->258 changes.
Do not label it frozen acceptance or deploy on that basis.
