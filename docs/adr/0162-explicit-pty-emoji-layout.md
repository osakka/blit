# ADR 0162: explicit session policy for streaming emoji layout

- Status: accepted
- Date: 2026-10-20
- Tracking: 107/110; follows ADR 0161

Add immutable creation-time TerminalSession.text_layout, using the same
scalar(default)/emoji policy vocabulary as positioned cell updates. Report
the chosen policy on session observations. Invalid policies fail before
spawning a process. Existing Hibr sessions remain scalar without changes.

Use the same corpus trie and canonical cell storage/raster. In emoji mode,
retain the last printed anchor across byte reads and append matching emoji
members. Validate anchor/cursor/owned value before extending. Controls and
API writes terminate eligibility; incomplete UTF-8 bytes do not. Widen a
completed cluster when needed; wrap the whole owned value at the right edge,
or retain one column where autowrap is disabled/no two-column space exists.
Retain emoji attachments through REP, overwrite, scroll and alternate buffers.

Do NOT advertise DEC 2027: it promises broader Unicode grapheme/width
semantics than the pinned emoji trie implements. Reference:
https://github.com/contour-terminal/terminal-unicode-core and
https://contour-terminal.org/configuration/ . This explicit Blit policy is
not a general Unicode-core implementation or a change to the scalar ABI.

Verify all 5244 sequences in contiguous and byte-fragmented output, legacy
compatibility, late widening, margins/wrap, insert, REP, control barriers,
overwrite/erase, scroll/alternate screens and real HTTP-created PTYs. No
Hibr edits, live writes or package rollout are implied by these local tests.
