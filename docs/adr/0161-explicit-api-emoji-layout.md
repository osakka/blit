# ADR0161: explicit emoji layout without breaking scalar-width providers

- Date: 2026-10-20
- Status: implemented; focused local gates pass, not deployed
- Tracking:107/110

Existing Hibr API providers assign coordinates using Unicode15.1 scalar
widths and submit positioned cells. Silently folding every sequence would
change their ownership contract. Add optional CellUpdate.text_layout:
`scalar` (existing default) or `emoji` (explicit sequence grouping).
This is one decoder with a grouping policy, not another terminal engine.

Generate one bounded prefix trie from the pinned Emoji18.0 corpus. In emoji
layout, append matching sequence members to the current cell. Completed
multi-scalar corpus sequences occupy two columns; singleton widths and
ordinary combining/wide behaviour remain unchanged. Unknown sequences use
the existing scalar/combining handling; no visible scalar is dropped.
Both policies use the same grid, cluster raster and backend consumers.
Do not alter Hibr, its width table or the adapter's default.

Check every corpus sequence in both layouts, adjacent text, continuation
ownership, invalid-policy rejection before mutation and schema/runtime
agreement. Generate byte-identically and verify corpus identity. The trie
is reusable by subsequent streaming PTY integration; this cycle does not
change PTY semantics or claim complete emoji support/live acceptance.
