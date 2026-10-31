# ADR 0042: Correlated live evidence bundles

- Status: accepted

## Context

A black or unstable physical display can be caused by different layers:
Hibr's held-session lifecycle, terminal surface population, Blit's renderer,
the display scheduler, or physical evdev delivery. A single screenshot or a
single frame-rate number cannot distinguish those causes. The troubleshooting
process must inspect the real host while preserving the API as the one source
of truth.

## Decision

Use a bounded, timestamped evidence bundle for every live visual/input
investigation. `tools/diagnostics/capture-live.sh` records, in one directory,
the version, display and topology inventory, session and surface summaries,
runtime timing samples, render-path and input traces, and display screenshots.
The operator must correlate:

1. Hibr session state and exit code;
2. surface revision, dimensions, and non-empty-cell count;
3. render/present counts, output refresh interval, and missed deadlines;
4. raw evdev counters and filtered pointer/button trace events; and
5. screenshot pixels from the same bounded interval.

The bundle is diagnostic evidence, not a replacement for physical acceptance.
No physical input or visual claim is accepted when the corresponding event or
pixel evidence is absent. Hibr remains an external process and is never
modified by this workflow.

## Consequences

Launch-state failures, empty surfaces, compositor damage, scheduler stalls,
and absent physical input are distinguishable without guessing. The workflow
also makes the exact evidence available for review after the live session has
ended. It adds bounded operator artifacts but no second runtime state model or
new Hibr dependency.
