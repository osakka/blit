# ADR0175: complete-cell rectangle copy and standard discovery

- Status: accepted for local candidate589; end-to-end Hibr integration pending
- Date:2026-10-21
- Scope: canonical terminal grid, DECCRA and DA1; internal Blit120

## Context

Issue120 requests cheap solid window movement. DECCRA already exists, but
only copies codepoint, attributes and foreground/background. It drops marks,
underline colour and link identity. Per-cell overlap cleanup can erase a
newly copied wide owner. DA1 does not report rectangular editing. These are
Blit defects, not a reason to introduce a second copy escape or modify Hibr.

## Decision

Keep the standard `CSI Pts;Pls;Pbs;Prs;Pps;Ptd;Pld;Ppd $ v` operation.
Copy every canonical cell field, including all16 attachment slots and their
count, legacy representation, colours, attributes and hyperlink identity.
Use one bounded row snapshot and copy rows bottom-up when moving downward,
top-down otherwise. This preserves whole-source snapshot semantics without
per-move allocation or a second full-grid buffer. Horizontal overlap reads
the captured row, never mutated destination cells.

Clear old wide partners at destination boundaries before installing the
complete copied row, not while installing individual cells. Source wide
fragments cut by either source bounds or destination clipping become empty
cells; complete pairs/clusters retain their owner/continuation metadata.
Damage includes destination and any cleared outside wide partners. Self-copy
is a no-op. Cursor, pen, scrollback and retained graphics do not move.

Use existing origin/margin coordinate conventions and clip to the effective
page. Oversized coordinates clamp to its last cell. There is one active page:
default/zero/one and larger page selectors resolve to that page (DEC's
last-available-page rule), never the inactive alternate buffer.

Once these tests pass, append standard feature28 (rectangular editing) to
DA1 while preserving Blit's existing identity and other features. DECRQM is
a mode query, not a command-support query; no fabricated DECCRA mode or
private escape is introduced. Hibr must query through its actual transport;
TERM identity alone is not proof of support.

## Boundary and consequences

DECCRA copies **cells**, not Kitty/sixel pixels, placements or the framebuffer.
It does not repair the source's uncovered strip. Applications still own
repainting uncovered/occluded areas and recompositing translucent content
whose destination background changes. Pixel-window placement needs its own
existing graphics protocol operations. A native parser microbenchmark is not
a physical frame-rate or Hibr drag-speed result.

Hibr console/term/hold/desktop integration is upstream work, requested by a
ticket only. Hold must retain this mutation semantically, keep its emulator
and diff grids coherent, forward only to capable clients and retain the
ordinary diff fallback for all other clients. End-to-end solid-motion
acceptance remains open until that path is released and measured.

## Verification

Extend terminal conformance and add a direct production-parser rectangle
fixture: full metadata, all overlap directions, clipping/default/page/origin
cases, wide-boundary damage, byte-fragmented CSI, cursor/pen preservation and
repeated copies compared to an immutable-source oracle. Verify DA1/DECID
exact bytes. Exercise final presentation through the shared renderer gates.
Do not deploy over the tagged588 desktop merely to test local parser changes.

The regression fixture reproduces dropped combining counts on the old code.
Candidate589 passes1048 immutable-grid/metadata/boundary/parser cases,
including512x160. Terminal suite5pass/0skip/0fail; renderer59pass/1skip/0fail;
offscreen GLES11pass/0skip/0fail. Valgrind reports0errors/0lost bytes.
The remaining renderer skip requires native VT/QEMU kernel fixtures; it is
not a pass. Hibr integration is tracked separately as internal337.

Primary references:

- [DEC DECCRA](https://vt100.net/docs/vt510-rm/DECCRA.html)
- [Xterm control sequences](https://invisible-island.net/xterm/ctlseqs/ctlseqs.html)
- [Xterm rectangle implementation](https://github.com/ThomasDickey/xterm-snapshots/blob/master/screen.c)
