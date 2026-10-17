# ADR-0115: Bound evdev service turns for refresh fairness

- Status: accepted
- Date: 2026-10-17

## Context

Blit owns one event loop for PTY reads, evdev input, API clients, and KMS or
SDL presentation. The mouse, keyboard, and gamepad readers previously drained
each nonblocking evdev descriptor until `EAGAIN`. A continuously reporting
physical device could therefore keep the loop inside input service while PTY
output and presentation waited. The live login-manager reproducer showed the
exact signature: moving the physical mouse stopped the saver updates; when
movement stopped, queued scene changes resumed and exposed stale damage.

This is a different boundary from pointer composition. API-injected pointer
motion updates the pointer layer directly and cannot prove that the physical
evdev reader is fair to the producer and renderer.

## Decision

Each evdev device service turn processes at most
`BLIT_EVDEV_SERVICE_BUDGET` records, currently 32, then returns to the main
loop. Mouse, keyboard, and gamepad readers use the same bound. The next select
or service turn continues draining the descriptor, so records are not silently
dropped and a busy device cannot monopolize PTY, diagnostics, or presentation.

`/v1/diagnostics/runtime` exposes `performance.evdev_service_budget_yields`.
The counter increments when a device turn reaches the bound; it is evidence
that fairness was exercised, not a claim that every physical device is
healthy. The QEMU input soak and fairness fixture must exercise that counter.

## Consequences

- Physical input remains lossless at the evdev read boundary while service is
  spread across loop turns.
- Pointer motion can be coalesced by the existing refresh-bound renderer, but
  it no longer prevents PTY output or presentation from receiving service.
- A very busy device can accumulate kernel backlog; diagnostics make that
  condition observable instead of hiding it in an unbounded read loop.
- The public runtime schema gains one additive counter.

## Verification

The local source and package gates must pass. The QEMU synthetic evdev
fairness fixture must report a nonzero `evdev_service_budget_yields` counter
while its PTY/render workload remains serviced. Physical-host acceptance still
requires a post-deployment capture on the login manager while moving the real
mouse; this ADR does not claim that evidence in advance.
