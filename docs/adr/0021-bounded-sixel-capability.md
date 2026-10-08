# ADR-0021: Advertise the bounded sixel capability

- Status: accepted
- Date: 2026-10-08
- Supersedes: the initial unadvertised-sixel phase of ADR-0005

## Context

ADR-0005 correctly made Kitty the first graphics protocol and initially kept
sixel unadvertised while its shared graphics-object path was being verified.
That implementation phase is complete: bounded direct-colour sixel decoding,
resource limits, placement, clipping, and the SDL3 and direct-KMS pixel gates
all pass. The runtime already reports `kitty` and `sixel` in the session
capability object, so leaving the older ADR wording in place creates two
sources of truth.

## Decision

Blit advertises `kitty` and `sixel` as supported terminal graphics protocols.
The advertised sixel contract is the bounded subset implemented by the
canonical graphics-object store: direct-colour raster data, colour selection,
repeated columns, carriage return, and sixel-row advance. Malformed input,
unsupported extensions, bounds violations, and resource-limit failures are
consumed without corrupting terminal cells and are exposed through graphics
diagnostics.

Kitty and sixel continue to share one image store, placement model, renderer
boundary, and lifecycle. Neither protocol is the high-rate emulator transport;
pixel-oriented games use the API-owned frame path defined by ADR-0013.

## Consequences

Clients may select sixel after discovering it in `terminal_capabilities`, but
must remain within the bounded grammar and inspect graphics diagnostics for
rejection. Expanding sixel grammar requires a parser regression and matching
SDL3/direct-KMS pixel evidence. ADR-0005 remains the historical record of the
Kitty-first sequencing decision.

## Evidence

The verification ledger records `PPM_DISPLAY_47_SIXEL_PASS`, `SDL_SIXEL_PASS`,
shared graphics-object assertions, and bounded rejection tests from the
TCC-built VKMS and SDL fixtures.
