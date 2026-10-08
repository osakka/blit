# ADR-0028: Use one owner for physical pointer input

## Status

Accepted

## Decision

When Blit has at least one raw evdev pointer open, raw evdev is the sole
physical pointer input path. SDL mouse button, motion, and wheel events are
discarded, and SDL's native cursor is hidden. Blit renders and routes the
authoritative software pointer itself. SDL mouse events remain available as a
fallback when no raw evdev pointer can be opened.

## Context

The KMSDRM SDL backend can poll the same evdev device that Blit opens directly.
Processing both paths duplicated clicks and produced competing pointer
positions/trails on the live Hibr desktop.

## Consequences

Each physical pointer event is dispatched once, with display-global coordinates
and Blit's normal input routing. The visible cursor has one renderer owner.
SDL-only environments without an accessible evdev pointer retain their
existing fallback behavior.
