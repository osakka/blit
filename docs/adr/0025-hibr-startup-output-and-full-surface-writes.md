# ADR 0025: Preserve Hibr startup diagnostics and full-surface writes

## Status

Accepted

## Context

Hibr desktop startup first paints a complete wallpaper and then paints the
menu bar and desktop controls. The Blit display adapter batches cell writes
before sending them to the surface API. Its historical hard queue limit was
smaller than a full 240×135 terminal surface; once the queue filled, later
writes were silently rejected. That made the top rows appear to be missing,
even though Hibr had issued them.

Hibr also reports startup timing through `DT_LOG`, using stderr only when its
log file cannot be opened. API-launched sessions have no guaranteed writable
login `HOME`, so the fallback could fail and lines such as `desktop: started
in ...` became terminal content. Those lines are diagnostics, not desktop UI.

## Decision

The adapter keeps a bounded pending queue sized for a complete ordinary
surface and flushes before reaching the hard bound. A failed flush remains a
reported adapter failure; cells are never silently discarded.

Blit supplies each API-launched Hibr child a unique `/tmp/blit-hibr-<id>.log`
as the default `DT_LOG`. An explicit API environment value still overrides
that default. This preserves Hibr's diagnostics without adding them to the
terminal surface and does not modify Hibr source or desktop policy.

## Consequences

- A full-screen wallpaper followed by a menu bar is rendered in order.
- Hibr startup/restart diagnostics are available in a per-session log rather
  than contaminating terminal content.
- The queue remains bounded; very large surfaces are sent in bounded batches.
- The QEMU Hibr acceptance fixture is the integration evidence for both
  behaviors. Physical visual confirmation remains a separate hardware gate.
