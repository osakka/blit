# ADR-0027: Invalidate the cached scene on cursor visibility transitions

## Status

Accepted

## Decision

When a terminal changes DECTCEM cursor visibility (`CSI ?25 h/l`), Blit marks
the complete surface damaged. Ordinary cursor movement continues to use the
small old/new cursor damage rectangle.

## Context

The SDL renderer caches the composed terminal surface in a scene texture.
Cursor pixels are drawn into that cache after cell content. A live Hibr capture
showed the API reporting a hidden cursor and blank canonical cells while the
previous cursor glyph remained in the screenshot. Cell-only damage could not
prove that the cached cursor overlay had been removed.

## Consequences

Visibility transitions redraw one surface once, preserving wallpaper and cell
content correctness. High-frequency cursor movement does not trigger a full
surface redraw. The hidden-cursor gate now checks both presentation suppression
and screenshot pixels.
