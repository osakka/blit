# ADR 0070: pointer modifiers cross the Hibr provider boundary

- Status: accepted
- Date: 2026-10-15
- Scope: Blit input stream and Hibr adapter

## Context

Hibr's desktop window manager implements movement by matching a `press`
followed by `drag`, and Alt-move by matching `alt-press` followed by
`alt-drag`. Hibr's console contract includes the button in a drag report:
`mouse drag left row column`. Blit already tracked the shared keyboard
modifier state and applied it when encoding mouse reports directly to a
terminal, but the API input stream's pointer events omitted `shift`, `ctrl`,
and `alt`. The adapter also labeled every pointer motion as `motion`, even
while a button was held, and its first drag translation omitted the required
button field. The latter shifted Hibr's parser arguments, making the column
zero and turning a horizontal drag into an apparently vertical-only move.

## Decision

Blit includes the aggregate shared-pointer modifier state on pointer motion,
button, and wheel events. `console-blit` translates that state once into
Hibr's action vocabulary (`ctrl-alt-press`, `alt-drag`, and
`shift-wheelup`). A held drag includes the deterministic lowest-numbered
button (`drag left row column`); when multiple buttons are held, left wins,
then middle, then right. Missing fields remain false for compatibility with
older servers. Hibr source and desktop configuration remain unchanged.

## Consequences

The same modifier state now reaches Hibr whether the pointer originated in
evdev, SDL fallback, or API injection. Held-button motion is emitted as
`drag`, while unbuttoned motion remains `motion`. Window movement, resize
gestures, and modifier-aware application mouse callbacks can distinguish
modified input from an ordinary click. The adapter input gate covers button
identity, correctly shaped drag actions, and wheel modifier forms; live
acceptance must still verify the physical gestures and visible presentation
cadence.
