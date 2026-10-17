# ADR 0114: Restore the KMS pointer background before every scene repaint

- Status: accepted
- Date: 2026-10-17
- Scope: direct-KMS software pointer composition and persistent scene cache

## Context

Direct KMS composes the software pointer into its persistent CPU scene and
saves the pixels underneath it so a later pointer move can restore the old
footprint. The previous implementation restored that saved background only
when the pointer coordinates changed.

That was incorrect when terminal or saver content changed elsewhere while the
pointer remained still. The repaint could leave the pointer drawn in the scene
cache. The next frame then saved those pointer pixels as the new background,
and the next pointer move restored a cursor-shaped trail at the old location.

## Decision

Before every non-full direct-KMS scene repaint, restore the saved pointer
background. This happens before surface-local terminal damage is applied and
is independent of whether pointer coordinates changed. Full redraws clear the
scene and invalidate the saved background as before.

The KMS composition gate now covers the exact sequence: save background, draw
pointer, repaint an unrelated cell region, restore the repaint buffer, and
assert that the pointer footprint is back to the original pixels.

## Consequences

An unrelated PTY/saver update cannot turn the software pointer into retained
scene content. Pointer motion can therefore restore only genuine underlying
pixels, eliminating this proven cursor-trail source without changing Hibr,
terminal parsing, input routing, or the API contract.

This does not by itself prove the complete Hibr desktop, graphics protocols,
or physical input. Those remain separate acceptance boundaries.

## Evidence

- `tools/tests/kms_compose_gate.sh` passes `KMS_COMPOSE_PASS` with the
  stationary-pointer repaint regression.
- The direct-KMS pointer background state is implemented in
  `src/blit_kms.c` by `prepare_repaint_buffer()`.
