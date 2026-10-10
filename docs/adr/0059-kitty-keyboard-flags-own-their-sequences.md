# ADR 0059: `CSI > u`, `CSI < u` and `CSI ? u` belong to the Kitty keyboard protocol

- Status: accepted
- Date: 2026-10-10
- Scope: terminal sequence semantics, advertised terminal identity

## Context

`POST /v1/terminal-sessions` returns `"terminal_capabilities": {"term":
"xterm-kitty", ...}`. A client that believes that performs the Kitty keyboard
protocol's progressive-enhancement handshake, which is four sequences:

| sequence | Kitty's meaning |
| --- | --- |
| `CSI > flags u` | push the current flags, then set them to `flags` |
| `CSI < Ps u` | pop `Ps` entries from the stack |
| `CSI = flags ; mode u` | set, or set/reset selected bits, without the stack |
| `CSI ? u` | query, answered `CSI ? flags u` |

Blit implemented the first two as cursor save and restore, and the fourth as
cursor restore. Measured on 0.1.410, with the sequences emitted by the
session's child so this is the parser and not the line discipline:

```
move(10,20) only                               cursor=(22,10)
move(10,20); CSI>1u; move(2,2); CSI<u          cursor=(22,10)   should be (2,2)
move(10,20); move(2,2); CSI?u                  cursor=(4,2)     no reply sent
```

So a client performing the standard handshake has its cursor dragged back to
wherever it was when it enabled the protocol, and then waits for an answer to
`CSI ? u` that never comes.

There is no cursor save/restore at these sequences in either the DEC set or
Kitty's. Cursor save/restore is `ESC 7`/`ESC 8` (DECSC/DECRC) and `CSI s`/`CSI
u` (SCOSC/SCORC), and Blit implements all four correctly. The behaviour being
replaced was a Blit-local invention occupying the Kitty keyboard protocol's
exact sequences, which `docs/terminal-engine.md` then described as
"Kitty-compatible cursor restore (`CSI ? u`)" — the opposite of what Kitty
does with them.

## Options considered

1. **Keep the cursor behaviour and stop advertising `xterm-kitty`.** Honest
   about what the terminal does, and the cheapest change. Rejected because the
   advertisement is load-bearing in the other direction: the Kitty *graphics*
   protocol is implemented, is how a Hibr desktop is meant to deliver bitmaps,
   and is discovered through exactly this TERM. Dropping the identity to
   protect an invented sequence would trade a working feature for an unused
   one.

2. **Implement the flag stack: store what a client pushes, pop it, and report
   it back from `CSI ? u`.** This is what the issue asked for and it is the
   option to argue against most carefully, because it looks like the complete
   answer. It is worse than it looks. Blit honours no keyboard enhancement
   today — see the decision below for why that cannot change in this cycle —
   so a stack would accept a flag, report it back, and change nothing. A
   client would read its own flag echoed, conclude the terminal disambiguates
   escape codes, and then receive legacy encoding with no way to find out.
   That is a worse failure than the one being fixed: the current defect is
   visible the instant the cursor jumps, and this one is invisible. State that
   nothing honours and nothing can act on is a lie with extra steps.

3. **Honour the sequences' real meaning, report honestly what is in effect,
   and keep no state.** Chosen.

## Decision

These four sequences mean what the Kitty keyboard protocol says they mean, and
the cursor save/restore on them is removed. None of them moves the cursor.
Cursor save/restore keeps its four correct spellings.

Blit honours no keyboard enhancement flags. So:

- `CSI > flags u`, `CSI < Ps u` and `CSI = flags ; mode u` are accepted and
  consumed. They change no state.
- `CSI ? u` answers `CSI ? 0 u`: the query is implemented, and no enhancement
  is in effect.

Answering zero is the protocol's own mechanism working correctly, not a
refusal to participate. Progressive enhancement is a feedback loop: a client
sets flags and reads them back, and a terminal that reports fewer than were
set has said which it took. Reporting zero after a push is the truthful answer
"none", and the client falls back to legacy encoding immediately instead of
timing out, which is what it does today.

**Why no flag is honoured yet, stated so it is not mistaken for an
oversight.** Keyboard flags change how a key is encoded on the way to the
application. Blit has exactly one key encoder, `send_evdev_key`, and it is
reachable only from a physical evdev keyboard — the API input route writes
bytes a client already encoded, and for a Hibr desktop
[ADR 0057](0057-owned-surfaces-are-not-interactive-terminals.md) means
physical keys do not reach the PTY at all. The machine this was implemented on
has no `/dev/uinput`, so a change to that encoder could be written but not
gated, and an ungateable claim about key encoding is exactly the kind this
project does not make. Flag 1 (disambiguate escape codes) is the one every
client sets and is the natural next piece of work; the instrument it needs is
the synthetic uinput keyboard ADR 0057's evidence already uses, on a host that
has the device.

## Consequences

The Kitty keyboard handshake no longer corrupts the cursor, and a client that
queries gets an immediate truthful answer instead of a timeout.

A client that wants disambiguated keys still does not get them. It now learns
that from the reply rather than from silence followed by a cursor jump.

Anything that relied on `CSI > u` as cursor save loses it. An audit found
nothing that does: Hibr emits no `CSI > u` or `CSI < u` — its only `CSI >`
sequence is the `CSI > 1 ; 10 ; 0 c` secondary-DA reply in its own terminal
emulator, `mods/term/vt.c:827` — and the conformance suite's cursor coverage is
on SCOSC/SCORC and DECSC/DECRC, which are untouched.

Three session fields that existed only for the removed behaviour
(`terminal_cursor_saved`, `terminal_cursor_saved_x`, `terminal_cursor_saved_y`)
are deleted rather than left unwritten.

`docs/terminal-engine.md` stops calling the old behaviour Kitty-compatible, in
both places it said so.

## Evidence

`tools/tests/terminal_conformance.rb`:

- a push with cursor motion after it, then a pop, leaves the cursor where the
  motion put it — the exact measurement in the issue, inverted;
- `CSI ? u` replies `CSI ? 0 u`, read back off `/dev/tty` as exact bytes, both
  before and after `CSI > 5 u`, which is the assertion that the reply reports
  what is honoured rather than what was asked;
- `CSI < 3 u` and `CSI = 9 ; 3 u` leave the cursor alone and print nothing;
- SCOSC/SCORC and DECSC/DECRC keep their existing vectors unchanged, which is
  the assertion that cursor save/restore was not harmed by removing its
  squatter.

Against a build with the old behaviour the first vector fails with the cursor
at the push position rather than the motion position.
