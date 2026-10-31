# ADR 0040: Compose pointer motion over cached scene damage

- Status: retired in 0.2.22 (ADR 0184)

## Context

The software pointer is rendered after Blit copies the scene texture to the
output. It is not stored in that scene texture. The previous renderer treated
pointer motion plus a terminal damage rectangle as a reason to clear and
rebuild the entire scene, even when only a small Hibr cell region changed.
Wallpaper-heavy desktops therefore paid for a full-screen redraw on every
pointer move, causing avoidable latency and visible instability under load.

## Decision

When the output topology and graphics scene are unchanged, pointer motion does
not force `full_redraw`. A concurrent committed cell damage rectangle updates
the cached scene incrementally; Blit then copies the complete cached scene and
composes the current pointer before one presentation boundary. Graphics-scene
changes and topology changes continue to require a full scene rebuild.

## Consequences

Pointer-plus-terminal-damage frames retain untouched pixels while avoiding a
full wallpaper redraw. The atomic regression gates require both the
incremental-damage trace and non-black untouched samples. This changes only
Blit's renderer and does not modify Hibr.
