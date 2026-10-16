# ADR 0105: Explicit terminal graphics reset

- Status: accepted
- Date: 2026-10-17
- Scope: terminal-session graphics lifecycle and API recovery
- Related: [ADR 0085](0085-graphics-mutations-invalidate-every-presentation-backend.md)

## Context

Hibr can change a picture from pixel mode to Half Blocks, or restart a
desktop, while Blit still owns a Kitty image object from the earlier mode.
Kitty images are persistent objects: clearing terminal cells does not delete
them. If the producer does not emit its delete sequence, the old image remains
visible behind the new cell output.

The renderer and Kitty parser already handle a valid delete correctly. A
generic automatic delete on every terminal clear would violate the graphics
protocol and break clients that intentionally retain an image across cell
updates.

## Decision

Blit exposes:

```text
DELETE /v1/terminal-sessions/{session_id}/graphics
```

This is an explicit, idempotent lifecycle recovery operation. It removes all
retained graphics from the selected surface, marks the entire surface damaged,
and returns the updated terminal-session representation. It does not modify
cells, stop the PTY, or change Hibr source.

Mode-aware clients should still emit the protocol delete themselves. The API
operation exists for recovery and orchestration boundaries where the producer
has been restarted or cannot describe graphics it previously created.

## Verification

The live Hibr session showed one visible Kitty image while the desktop was
already configured for Half Blocks. A controlled temporary session proved that
Blit transitions `images=0->1` and `images=1->0` for a valid Kitty delete.
The graphics-reset gate verifies that the explicit API operation removes the
retained image and that the following screenshot is black where it was placed.
