# ADR 0160: consume owned clusters consistently in every compositor

- Status: accepted; in use since 0.1.577
- Date: 2026-10-20
- Tracking: 107/110; follows ADR 0159

## Decision

Expose the existing shared cluster raster through BlitFont's owned context.
Native KMS and SDL software/OpenGL consume the complete cell value, including
the block-cursor repaint, rather than painting joined emoji as overlapping
scalars. Use the same premultiplied compositor and existing SDL atlas/batches.
The SDL upload cache must compare complete cluster keys; flush pending
vertices before replacing a colliding tile. Reset keys on font/size changes.

For a supported cluster, draw once and suppress the old scalar overlays and
colour bold overstrike. Unsupported ordinary combining text retains its
existing base/mark path; do not drop visible attachments. Keep exact existing
owner-supplied 1/2-column allocation and do not modify Hibr or the Unicode
width ABI. Parser grouping is a subsequent shared-ingress change, not an
assumption inferred from backend tests.

## Verification

Compare actual native-KMS, SDL-software and offscreen-OpenGL pixels with the
shared raster, including multi-scalar sequences, atlas collisions, font/size
switches, styles and block cursors. Preserve scalar/combining regressions.
Test API/PTY-owned cell paths separately; backend consumption alone is not
complete terminal emoji support or physical-display acceptance. No live
deployment until local conformance and remaining presentation/width work pass.
