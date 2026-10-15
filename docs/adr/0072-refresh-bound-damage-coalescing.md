# ADR 0072: Coalesce pending damage at each output refresh boundary

- Status: accepted
- Date: 2026-10-15
- Scope: the SDL and direct-KMS presentation scheduler

## Context

The live host runs SDL3 through `opengles2` on a 60 Hz output. During a Hibr
window move, the desktop generated surface commits about every 5 ms. Because
the scheduler re-phased itself whenever a new event arrived after the
previous frame had completed, and because pointer and pixel damage could force
an output into the due mask, Blit composed a full 1920x2160 scene for producer
events instead of coalescing them to the output's refresh schedule. The result
was high render pressure and visibly uneven motion even though individual
render and present calls were below the nominal 16.667 ms budget.

The existing refresh invariant already makes the active display mode the
authority. The defect was that the event-driven wakeup path could bypass that
invariant. Input and network handling must remain responsive, but accepting an
event is not permission to present a second frame before the output can show
it.

## Options considered

1. Keep presenting immediately for every pointer or producer event. Rejected:
   it makes producer burst rate the visible cadence and can monopolize the
   render loop while moving a Hibr window.
2. Run a global fixed-rate timer. Rejected: outputs can have different modes,
   and one slow or absent output must not pace another.
3. Drop intermediate producer updates. Rejected: terminal and pixel state are
   authoritative latest-state snapshots; dropping state would make the image
   incorrect rather than merely coalesced.
4. Keep one independent deadline per output, retain pending damage, and
   present the newest complete state when that output is due. Accepted.

## Decision

Each output's existing refresh-aware deadline is authoritative for all pending
work: terminal damage, pointer damage, overlays, and API pixel frames. The
event loop still wakes immediately for input, PTY, HTTP, and page-flip events,
but `due_output_mask` no longer overrides an output deadline merely because
pointer or pixel work is pending.

Pointer damage is tracked as an output bitmask. Moving between outputs marks
both the old and new output; each bit remains set until that output presents,
so independent refresh phases cannot lose the cursor on a secondary display.
When a previously idle output receives work, its deadline is re-phased only if
the old deadline is already at least one period stale. A short idle gap keeps
the existing phase and therefore coalesces a burst instead of restarting the
cadence for every event.

The frame-rate gate drives terminal output faster than 60 Hz and requires both
that presentation reaches the 60 Hz target and that it does not escape above
125% of the active refresh during the bounded measurement window. This is a
composition-rate assertion, not a claim that a panel scans above its mode.

## Consequences

- A 60 Hz output presents pending motion at its refresh boundary, not at a
  5 ms Hibr producer cadence.
- Pointer latency can increase by at most the remaining time in the current
  refresh period, which is the required trade for stable motion and bounded
  render pressure.
- Outputs keep independent deadlines and pointer-damage ownership.
- Input dispatch and API acceptance remain event-driven and are not delayed
  until the next frame.
- The renderer still performs a full scene rebuild on live GLES2 where ADR
  0070 conservatively disables the scene cache; this decision bounds how often
  that rebuild can occur. The future GLES2 cache issue remains separate.

## Verification

- TCC build succeeds.
- `frame_rate_gate.rb`: 60.000 Hz, 30 frames in 500 ms, zero missed
  deadlines, and the producer-burst upper bound passes.
- Pointer damage atomicity, pointer-cache composition, full-rebuild
  composition, and motion-trail gates pass.
- The live host trace before the fix showed full-redraw entries about every
  5 ms during surface churn. The deployed candidate must be rechecked through
  `/v1/diagnostics/runtime` and `trace?kind=render_path` while moving a Hibr
  window; physical acceptance is not inferred from the dummy backend.
