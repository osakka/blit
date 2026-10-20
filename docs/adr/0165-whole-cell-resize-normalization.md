# ADR0165: resize preserves whole cell ownership

- Date: 2026-10-20
- Status: accepted; offline implementation, live acceptance pending
- Tracking:107/110

Resizing is not a terminal reset or reflow. Preserve overlapping whole cells
and clip cells outside the new rectangle. If the right edge cuts a wide
cell, remove its retained anchor as well as its now-invisible continuation.
Apply this independently to the active and saved alternate-screen buffers,
through the existing shared resize function. Do not duplicate backend logic
or shorten the logical owner while retaining its wide flag.

Clear every clipped cell field, including combining values, styles and link
IDs. Active link IDs were previously omitted from out-of-rectangle clearing.
A resize also terminates streaming cluster-extension eligibility; it does
not clear the last printable value used by REP. Retain existing margin/tab/
cursor handling and whole-viewport damage/revision publication.

The new real local PTY test reproduces the defect on581: shrinking16 columns
to4 leaves a two-column heart at column3. An independent expected grid and
revision-matched presented BMP comparisons test the fix, including narrow
VS15 owners, shrink/grow, movement, erase, overwrite, scrolling and alternate
restore. A module fixture additionally checks clipping while both buffers
contain a cluster and verifies that links/attachments cannot resurrect.

Pixel references are rendered through the positioned API from independently
specified grids. They prove PTY/API parity, not an independent font raster
implementation or exhaustive graphics-protocol conformance. No Hibr edits,
SSH, package or live deployment during this cycle.
