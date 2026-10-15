# ADR 0070: pointer modifiers cross the Hibr provider boundary

- Status: accepted
- Date: 2026-10-15
- Scope: Blit input stream and Hibr adapter

## Context

Hibr's desktop window manager implements Alt-move by matching an `alt-press`
followed by `alt-drag`. Blit already tracked the shared keyboard modifier
state and applied it when encoding mouse reports directly to a terminal, but
the API input stream's pointer events omitted `shift`, `ctrl`, and `alt`.
The adapter therefore delivered an ordinary press/drag to Hibr. Normal
clicks worked while Alt-drag window movement did not.

## Decision

Blit includes the aggregate shared-pointer modifier state on pointer motion,
button, and wheel events. `console-blit` translates that state once into
Hibr's action vocabulary (`ctrl-alt-press`, `alt-drag`, and
`shift-wheelup`). Missing fields remain false for compatibility with older
servers. Hibr source and desktop configuration remain unchanged.

## Consequences

The same modifier state now reaches Hibr whether the pointer originated in
evdev, SDL fallback, or API injection. Window movement, resize gestures, and
modifier-aware application mouse callbacks can distinguish modified input
from an ordinary click. The adapter input gate covers button, drag, and wheel
modifier forms; live acceptance must still verify the physical Alt-drag
gesture.
