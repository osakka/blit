# ADR 0012: Cache SDL glyphs and suppress unchanged terminal damage

- Status: accepted
- Date: 2026-10-07

## Context

Hibr wallpapers are emitted through the terminal surface. A wallpaper can
rewrite the same glyph and style over a large grid on every redraw. Blit was
marking those writes dirty even when the canonical cell was unchanged, then
rasterizing each glyph as several SDL rectangle operations. On the live
1920×2160 host, full redraws exceeded the 16 ms budget for a 59.99 Hz output.
Hibr also moves its logical cursor while the cursor is hidden. Treating those
movements as visual damage schedules frames even though no pixels can change.

## Decision

The SDL backend builds one monochrome glyph atlas per output from the canonical
loaded PSF font. Each glyph is presented as one colour-modulated textured draw;
the existing rectangle rasterizer remains a fallback if the atlas cannot be
created. The terminal state engine compares the complete cell state—codepoint,
colours, attributes, link, combining state, and wide-cell continuation—before
marking a printable write dirty. Clearing an already empty cell is also a
no-op for damage tracking.
Pure cursor movement is damage only while the cursor is visible; cell changes,
cursor visibility changes, and content-affecting control sequences still force
the required damage.

The atlas is an SDL presentation optimization only. The canonical terminal
grid, UTF-8 semantics, font mapping, direct-KMS rasterizer, and API surface
contract remain unchanged.

## Consequences

- Repeated Hibr wallpaper writes do not force a full redraw when their cell
  state is unchanged.
- Hidden cursor control traffic does not force presentation frames.
- SDL glyph work is reduced from per-pixel rectangle runs to one textured draw
  per glyph, while colour, bold, italic, combining, and wide-cell placement
  remain controlled by the existing cell renderer.
- A full initial redraw can still exceed the frame budget on a slow software
  renderer; the acceptance evidence must measure initial, damage, and
  pointer-only paths separately.
- Atlas and no-op damage behavior must be covered by TCC builds, terminal
  conformance, SDL pixel capture, hidden-cursor damage, and the relevant
  KMS/QEMU gates.

## Rejected alternatives

- Treating wallpaper as an exception in Hibr would duplicate desktop policy
  and violate the boundary that Blit consumes Hibr's PTY output.
- Lowering the refresh target would hide the renderer cost and violate the
  display-refresh invariant.
- Replacing the canonical PSF font with a renderer-specific font would risk
  reintroducing glyph and cell-metric divergence.
