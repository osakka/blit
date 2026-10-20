# ADR0158: one bounded cell-cluster layout and cache identity

- Date: 2026-10-20
- Status: first shared layer implemented locally; shaping/segmentation pending
- Tracking: Blit107/110; layout regression [113](https://git.home.arpa/itdlabs/blit/issues/113); follows ADR0157

## Findings

The server stores sixteen attachments per cell, but native KMS hardcodes four
both as the maximum count and as the per-cell array pitch. After cell0, the
reader can therefore select another cell's attachment data. A real draw_view
pixel fixture fails BEFORE the fix at size8/count1; this is not a proposed
explanation inferred from a screenshot. Existing scalar-only raster tests
never supplied cell attachment arrays and could not expose this discrepancy.

## Decision

Define the existing sixteen-slot bound once in `src/blit_cell_cluster.h`.
The server's main/alternate grids, API decoder and scrollback keep their
existing layout through that constant. Native KMS consumes the same bound
for BOTH indexing and iteration. Do not add a second packing convention or
silently support four-slot arrays; BlitKmsSurfaceView is an internal view of
the canonical grid, not an independent persisted/wire ABI.

Define a bounded owned `BlitCellCluster`: base plus at most sixteen ordered
attachments and the cell owner's existing1/2-column span. Construction
validates scalar values and bounds before replacing its destination. Keep
the exact input sequence; do not normalize, infer segmentation, change Hibr
widths or introduce presentation-selector behaviour in this value layer.

Use the complete cluster value as the existing shared raster cache's key.
Currently public callers still supply a single scalar. The hash selects a
bucket ONLY; a hit compares length, columns and all occupied scalars. Profile
and pixel size remain properties of the owning raster context. No unbounded
allocations, cache expansion or per-backend key implementations are added.
This establishes storage/identity needed by shaping, not a claim that the
current public raster API renders multiple scalars as one glyph.

## Verification and next boundary

TCC value-contract gate round-trips all5,244 pinned Emoji18.0 entries at BOTH
owner-supplied spans. It tests prefixes, maximum capacity, ownership/no alias,
invalid scalars/columns/length, atomic rejection and deliberate bucket
collisions. This is value-contract evidence, not terminal emoji conformance.

The native KMS fixture compares actual draw_view against separately indexed
glyph composition with row padding/nonzero origins,0/1/5/16 attachments and
all57 font sizes:228 layout cases per outline profile. The shared raster
gate evicts colliding A/U+00C1 keys, rechecks exact glyph bytes and distinguishes
one/two-cell cache entries. Existing scalar/font/backend and terminal gates
remain required. Live hardware acceptance and deployment are separate.

Next implement the production shaping/colour-cluster API and its dependency
decision, then shared PTY/API cluster segmentation and renderer consumption.
Width coordination must be demonstrated, not inferred: read-only inspection
of local Hibr0527d3c mods/uni/uni.c shows `uni width` sums un_cw per scalar.
That is not proof about every producer/rendering path or the live version.
Test Hibr/API-owned cells separately from PTY terminal-width semantics before
choosing grouping/advance rules; request upstream coordination if required,
never patch Hibr or silently alter its cell ownership.
Do not activate grouping in the parser while renderers would still overlay
the sequence as individual scalars. Keep font107/emoji110 open.
