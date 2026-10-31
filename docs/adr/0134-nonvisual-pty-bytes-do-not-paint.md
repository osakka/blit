# ADR 0134: nonvisual PTY bytes do not schedule painted revisions

- Status: accepted
- Date: 2026-10-18
- Issue: Blit#101

Real 547 phase samples show expensive frames dominated by scene drawing, not
the approximately 1ms scanout copy or approximately 0.03ms kernel submission.
Traces include changed surface revisions with no valid cell damage and a
hidden cursor. The production parser unconditionally returned 1 after every
nonempty chunk. `service_session_output` interpreted that as a visual change,
advanced the surface revision and armed rendering/barriers. Direct KMS then
correctly treated a changed revision without bounded damage as a full view
redraw, repeatedly repainting wallpaper for nonvisual controls/partial strings.

Return the canonical surface damage flag after parsing instead of reporting
every consumed byte as visual. The existing printable cell, visible cursor,
palette, completed image, deletion and placement handlers own damage marking.
All bytes still parse, protocol replies still execute and raw terminal-output
streams still receive the bytes. Fragmented UTF-8/CSI/APC state is retained.
DEC 2026 continues to retain actual damage until release. This is the shared
PTY boundary for SDL and KMS, not a backend-specific suppression heuristic.

The production parser regression is red before correction for hidden-cursor
controls and partial APC data; green after, while printable cells still mark
damage and control state/title updates remain correct. Existing completed
graphics, deletion, synchronization, fragmentation and terminal conformance
must remain green. Do not replace this with suppressing redraw solely because
the cursor is hidden, or ignoring a changed image. Physical CPU/cadence and
pointer latency remain separate measured acceptance, never inferred from the
parser regression alone. No Hibr source changes or renderer switches.
