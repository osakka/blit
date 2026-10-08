# ADR 0030: Coalesce terminal PTY writes before presentation

Status: Accepted

## Context

Hibr emits one logical desktop frame through a PTY, but a PTY is a byte stream
and Blit reads it in bounded chunks. A clear-and-redraw therefore arrived as
several parser updates. Refresh-driven rendering could present the cleared or
partially redrawn surface between those reads, producing blinking menus,
missing icons, and wallpaper-only frames even though the canonical surface
eventually contained the complete desktop.

## Decision

Blit reads up to 64 KiB per PTY service turn so a normal Hibr desktop flush,
which is emitted as one write, remains one parser update. When terminal parsing
sees a full-screen erase (the clear phase used by a desktop redraw), Blit also
holds presentation for 8 ms after the most recent PTY read. Further reads
extend the same quiet deadline. The canonical terminal grid and API revisions
continue advancing immediately; only the renderer is coalesced. Pointer-only
presentation is held by the same deadline so it cannot expose a partial
terminal frame. Ordinary streaming terminal output retains the normal refresh
cadence.

## Consequences

The clear/redraw frame is presented at the end of a bounded output burst,
preserving the display refresh schedule while removing clear/redraw tearing.
Interactive input adds at most the bounded quiet interval when it triggers a
full-screen redraw. This is a renderer scheduling rule, not a second terminal
buffer or a Hibr-specific parser, and it applies consistently to all PTY-backed
terminal clients. Explicit synchronized-output mode remains authoritative when
a client uses `CSI ? 2026 h/l`.

The 8 ms interval is intentionally below a 60 Hz refresh period and is
validated against the live Hibr desktop and the terminal conformance workload.
