# Rectangle-copy integration contract

Since 0.1.589 (internal #120, Hibr #337). Blit implements DECCRA; using it
for solid window moves through Hibr's hold transport is still to be
confirmed on screen (see the [roadmap](roadmap.md)).

## Capability and wire format

Use standard DECCRA, not a new Blit escape:

```text
CSI Pts ; Pls ; Pbs ; Prs ; Pps ; Ptd ; Pld ; Ppd $ v
```

Source top/left/bottom/right are one-based, inclusive. Destination top/left
name its upper-left cell. Omitted/zero source corners default to page edges;
omitted/zero destination coordinates default to the effective origin.
Origin mode uses the established margin-relative coordinates; without it,
scrolling margins do not restrict page-relative copies. Out-of-page coordinates
clamp to the effective page and destination overflow is clipped. Inverted
normalized rectangles do nothing. Page selectors clamp to the single active
page; they never address the inactive alternate buffer.

Example: copy rows2..5, columns3..10 to row8,column4 on page1:

```text
ESC [ 2;3;5;10;1;8;4;1 $ v
```

Spaces above separate notation only; emit `\x1b[2;3;5;10;1;8;4;1$v`.
The original rectangle is not erased. Destination pixels are repainted from
the canonical grid, not copied directly from a framebuffer.

Query primary device attributes with `CSI c` (`ESC [ c`). Blit589 replies
`ESC [ ?62;4;22;28c`; feature28 reports rectangular editing. Keep the feature
test separate from device identity62 and TERM. Legacy DECID uses the same
reply. This is not a DECRQM mode, a claim of every VT400 function, or a claim
that an intermediary can forward it. A client through hold must negotiate
the complete route and each attached terminal separately.

## What is copied

Every stored cell value: base scalar, all16 attachment slots and count,
one/two-column ownership, character attributes/protection, foreground,
background, underline colour, link identity and legacy representation.
Whole-source snapshot semantics apply to overlaps in any direction. Complete
wide pairs and emoji/Arabic clusters retain their metadata. A wide character
cut by source bounds or destination clipping becomes an empty cell; replacing
half an old destination pair also clears/damages its old partner. Self-copy
does nothing. Cursor and pen are unchanged.

Scratch is one maximum-width row, under48KiB, with no allocation per copy.
The destination plus any cleared outside wide partners are damaged; existing
renderer ink halos handle overhanging glyph repaint.

## What the application and hold still own

DECCRA is not a window compositor. It does **not**:

- erase the exposed source strip or resolve occlusion;
- relocate Kitty/sixel images or placements;
- recomposite translucent content against a changed destination background;
- skip Hibr's own frame construction or modify hold's diff/back buffers;
- make an unmodified intermediary or incapable client support rectangle copy.

Only copy genuinely reusable cell content. Repaint exposed/occluded strips,
update retained graphics through their existing placement protocol, and
recompute background-dependent/translucent cells. Use synchronized output
when the operation plus repairs form one logical frame. Preserve the normal
diff fallback for incapable, mixed or reconnecting clients and keep outline
drag as the default until solid mode is proven end-to-end.

Internal Hibr337 requests console emission, term-grid semantics, hold
forwarding/fallback and desktop frame elision. Blit does not modify Hibr code.

Read-only source audit at Hibr7dc5f74 (clean audited files) confirms why
route-level negotiation is required: mods/term/vt.c `tm_csiin` currently
ignores `$v`, while relayed `CSI c` is passed to the real terminal. A real
terminal's feature28 reply must not enable DECCRA inside hold before both
the emulator and forwarding path support it. This is source-checkout
evidence, not a claim about the installed desktop binary. It is recorded
in Hibr337; no Hibr files were changed.

## Verification and remaining acceptance

`ruby tools/tests/rectangle_copy_gate.rb` tests the actual production parser
and grid against an immutable-source oracle. It covers full cell metadata,
overlap, clipping, wide boundaries/damage, maximum512x160 grids, origin,
page/defaults, fragmented CSI and exact DA1/DECID replies. Terminal conformance
adds real PTY-to-API metadata and wide-pair cases. Standard renderer/GLES
regressions remain independent of a real desktop drag.

The fixture also times30 local144x38 cell copies on a232x71 grid. That is
primitive CPU cost only: no input, PTY parsing, hold, application redraw,
glyph rendering or physical presentation. It is not a frame-rate measurement.

After Hibr integration, measure actual through-hold bytes, application/server
CPU, physical presentation cadence and incremental-vs-clean correctness for
content-bearing30-step drags. Cover wallpaper, translucent/pixel windows,
overlaps, Arabic/wide/emoji text, joined panes, resize/reconnect and mixed
capable/incapable clients. Do not close120 or claim solid-motion speed from
the local copy fixture alone. See [ADR0175](adr/0175-complete-cell-rectangle-copy.md)
and the [verification ledger](verification-status.md).
