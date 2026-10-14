# ADR 0068: the Hibr adapter translates input into Hibr's console vocabulary

- Status: accepted
- Date: 2026-10-14
- Scope: Hibr adapter input boundary

## Context

Blit's versioned input stream reports terminal `row` and `column` values using
the terminal protocol's one-based coordinates. Hibr's `console` provider API
uses zero-based row and column values for drawing, hit testing, and mouse
events. Forwarding the stream values unchanged therefore selected the cell one
row down and one column right of the pointer. The adapter's pixel-coordinate
fallback also divided both axes by eight even though its cells are 8 by 16
pixels, selecting a row approximately twice as far down.

Blit normalizes pointer buttons as left=0, middle=1, right=2. The adapter had
middle and right reversed and discarded pointer-wheel events. Its fixed pixel
fallback also became wrong whenever the API-selected font scale changed.

The stream identifies keyboard keys by Linux evdev code. Hibr's console API
expects names such as `escape` and `f10`. The adapter had no function-key
mapping, so it accepted and consumed F1 through F12 without delivering a key
to Hibr. On the physical host this made a pointer click miss a visible control
and made Hibr's F10 menu shortcut inert even though Blit's diagnostic trace
showed both inputs reaching the correct session.

Raw evdev code is also not printable text. Blit owns per-device keymaps and
modifier/lock state, but the stream did not publish the text it resolved. The
adapter consequently carried a second, partial US layout: configured layouts,
CapsLock and AltGr could not cross the boundary; shifted letters became names
such as `shift-a` instead of the `A` Hibr's native console returns; shifted
punctuation was similarly wrong.

This is a protocol-boundary mismatch, not a Hibr desktop or Blit input-routing
defect. Hibr is an external dependency and its source is out of scope.

## Options considered

1. Change Blit's stream coordinates to zero-based. This would change the
   public terminal-input contract for every client to accommodate one
   consumer and make the stream disagree with terminal mouse protocols.
2. Change Hibr's console API or desktop. This would fork an external
   dependency and create a parallel input contract.
3. Translate once in `console-blit`. The adapter already owns conversion from
   Blit's wire messages to Hibr's provider vocabulary, so the mismatch stays
   local and both public contracts remain intact.
4. Correct clicks or key names in the server for Hibr sessions. That would put
   client-specific policy in the display server and duplicate the adapter's
   transport role.

## Decision

`console-blit` is the sole translation boundary between Blit's input stream
and Hibr's console API.

- Stream `row` and `column` values are converted from one-based to zero-based
  exactly once and clamped at zero for defensive edge input.
- When a stream event lacks cell coordinates, the adapter derives them from
  the session's authoritative `terminal_capabilities.cell_pixels` and reports
  the same values through `console cellpx`. The 8 by 16 base size is only the
  compatibility default when an older server exposes no capability.
- Normalized buttons retain Blit's left/middle/right order, and vertical wheel
  deltas become Hibr's `wheelup`/`wheeldown` events at the pointer cell.
- Linux F1 through F12 codes are translated to Hibr's native `f1` through
  `f12` names. The non-contiguous Linux F11/F12 codes are handled explicitly.
- Keyboard presses and repeats carry optional resolved UTF-8 `text` from
  Blit's per-device keymap state. The adapter uses it for printable keys and
  uses the evdev-code mapping only for non-text keys. It does not maintain a
  second keyboard layout.
- Blit's stream schema, input routing, and terminal protocol coordinates do
  not change. Hibr source does not change.

## Consequences

Provider-backed Hibr controls receive the cell under the visible pointer, and
Hibr shortcuts such as F10 can use the same network and evdev dispatch path as
other keys. Middle/right clicks and the vertical wheel retain their normalized
meaning. Any future key-name addition belongs in this adapter mapping and must
be tested against Hibr's native console vocabulary. Font selection and scale
have one cell-pixel value in the session resource; input fallback, image
placement, and `console cellpx` consume that value rather than maintaining a
second geometry constant.

This decision does not by itself prove physical mouse hardware, Hibr widget
behavior, or an arbitrary keyboard layout. Those remain end-to-end acceptance
questions after the translation gate passes.

## Evidence

`tools/tests/adapter_input_gate.sh` compiles the shipped adapter into a harness
against the package-pinned Hibr headers, feeds it actual unmasked WebSocket
text frames, and asserts button, motion, pixel fallback, top-left clamping,
all three button names, vertical wheel directions, font-scaled fallback, F1
through F12, modifiers, resolved layout text, shifted punctuation, UTF-8, JSON
escapes, and an existing Escape mapping. The initial boundary fix failed
17 checks: all four pointer-coordinate checks and all twelve function keys
plus the modifier case. The added printable-text cases then exposed four more
failures in the duplicate adapter layout. With this decision implemented it
reports:

```text
ADAPTER_INPUT_PASS pointer_origin=zero buttons=3 wheel=2 function_keys=12 resolved_text=6
```

The production adapter is separately built with TCC against the exact Hibr
version in `packaging/HIBR_VERSION`; physical-host acceptance is recorded in
the verification ledger rather than inferred from this harness.
