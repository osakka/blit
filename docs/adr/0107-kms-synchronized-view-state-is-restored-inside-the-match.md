# ADR 0107: Restore synchronized KMS view state inside the match

- Status: accepted
- Date: 2026-10-16

## Context

The KMS presenter preserves the last presented revision and graphics identity
for a terminal view held by DEC 2026 synchronization. The restoration must be
performed while iterating the previous view records. The previous source had a
missing brace: revision restoration was inside the match loop, but graphics
identity restoration executed after the loop using the exhausted index. That
made joined or synchronized graphics state dependent on an out-of-range array
read and could cause a later frame to be classified as unchanged or changed
incorrectly.

## Decision

Match the synchronized view's surface ID, restore both its revision and its
graphics signature from the previous presented record, and stop the search at
the first match. If no previous record matches, retain the current record;
there is no valid prior state to restore.

## Consequences

- Synchronized KMS views have one well-defined revision/signature identity.
- Joined Kitty/Sixel graphics cannot inherit an array-index-dependent state.
- The change is local to Blit's KMS presenter and does not modify Hibr.
- This fix does not by itself prove physical fast-motion acceptance; that still
  requires a live evdev/render/screenshot correlation.

## Evidence

The TCC build, `kms_compose_gate.sh`,
`hibr_join_frame_barrier_gate.rb`, and `graphics_trace_gate.rb` pass with the
change. The live host remains on the previously deployed 0.1.517 package
until this source correction is packaged and deployed as a separate release.
