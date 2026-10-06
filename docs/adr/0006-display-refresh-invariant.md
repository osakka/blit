# ADR 0006: Display refresh is an output invariant

- Status: accepted
- Date: 2026-10-06

## Context

Blit presents one logical state engine to one or more physical displays. A
display has a finite refresh rate selected by its active mode. Rendering faster
than that rate cannot make the physical display show more frames; rendering
slower than it makes motion and pointer updates visibly uneven. A single
global timer is also incorrect when outputs have different modes or refresh
rates.

## Decision

Each output owns a refresh-aware presentation schedule. Its target cadence is
the maximum refresh rate of its active display mode, as reported by the
display backend. The scheduler presents at that cadence when work is pending
and synchronizes presentation to the backend's page-flip/vblank mechanism
where available.

The invariant is:

```text
target_refresh(output) = active_mode.max_refresh(output)
target_interval(output) = 1 second / target_refresh(output)
```

The scheduler must not intentionally select an interval slower than the
active mode's maximum refresh. A software loop may wake more often to service
input, network, PTY, and page-flip events, but that is not a presentation-rate
claim and must not cause busy-loop rendering when no output is due.

The physical output remains the authority. Blit must not advertise a frame
rate higher than the output can scan, and a missed deadline is reported in
diagnostics rather than hidden by claiming that the target cadence was met.
When outputs differ, each output keeps its own deadline and missed-deadline
counter; one slow output must not downgrade the cadence of another.

An explicit administrative policy may request a lower cap for power, thermal,
or bandwidth reasons. That policy is opt-in, visible in diagnostics, and is
the only permitted exception to the default no-degradation rule. It cannot
raise the target above the active mode maximum.

## Consequences

- Pointer and terminal motion are scheduled against the actual output rather
  than a hard-coded 16 ms timer.
- A 60 Hz display defaults to approximately 16.667 ms, a 120 Hz display to
  approximately 8.333 ms, and so on; integer timer resolution must not round
  a target down into a slower cadence.
- SDL, direct KMS, and future backends expose the same per-output timing
  semantics through the authoritative diagnostics model.
- CPU load is controlled by event-driven wakeups and damage tracking, not by
  lowering the required presentation cadence.

## Verification

The acceptance suite must prove all of the following:

1. A single output advertises its active mode refresh and derives the matching
   interval without truncating it into a slower cadence.
2. Mixed-refresh outputs retain independent intervals and counters.
3. Pointer-only damage is presented at the due output's cadence.
4. A deliberately delayed page flip increments `missed_deadlines` and does
   not claim a successful frame for the missed deadline.
5. No-damage periods do not produce presentation calls.
6. An explicit lower cap is visible as policy in diagnostics and never raises
   the target above the active mode maximum.

The deterministic VKMS fixture and a physical KMS host are both required
before this ADR is considered fully validated. SDL dummy-backend timing is
only a scheduler test; it is not physical refresh evidence.
