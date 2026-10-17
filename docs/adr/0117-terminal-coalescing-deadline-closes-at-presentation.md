# ADR 0117: Terminal coalescing deadlines close at presentation

- Status: accepted
- Date: 2026-10-17
- Issue: [Blit #90](https://git.home.arpa/itdlabs/blit/issues/90)

## Evidence

The actual `service_session_output()` rearmed a terminal coalescing deadline
after it expired but before presentation. Repeated incoming PTY chunks could
therefore postpone the eligible frame again. The actual `render_wait_ms()`
returned zero when the output deadline had passed even though the quiet window
still prohibited rendering, causing a busy poll. Both cases fail deterministic
tests in `surface_damage_gate.c` before correction.

Direct KMS also consumed surface revisions without clearing the per-session
`terminal_frame_batch` flag, although SDL cleared it. After the first clear,
later output repeatedly entered the quiet-window logic.
The Hibr primary/join barrier had the same expired-deadline rearming defect,
reproduced with two member surfaces and one dirty member in the real helper.

## Decision

A zero quiet deadline means closed; a nonzero deadline means the window is
open, even after expiry. Arm it only when closed. Close it after a scheduled
render attempt. New chunks cannot postpone an existing deadline.

The event loop's wait is governed by the later of the output refresh deadline
and active coalescing/group deadlines. Other input, PTY, and API readiness can
still wake `select()`. Clear a session's batch flag when direct KMS consumes
its surface revision, matching the SDL boundary. DEC synchronized-output
state and graphics ownership are unchanged.
An open Hibr group deadline likewise remains fixed through expiry until the
main loop releases or times it out; incoming chunks cannot postpone it.

## Verification

The real pipe/PTY-consumer test verifies an expired window cannot rearm, and
the scheduler test verifies an active eight-millisecond quiet window produces
an eight-millisecond wait. Run TCC renderer/terminal gates and the physical
PTY/pointer workload; record actual page-flip submission cadence separately
from parser throughput or render-attempt counters.
