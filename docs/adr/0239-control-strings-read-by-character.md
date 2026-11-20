# ADR 0239: Control strings are read by character, C1 is one control raw or encoded, and CAN/SUB end a string

- Status: accepted
- Date: 2026-11-20
- Scope: the terminal parser in `src/blit_server.c` (`terminal_consume_chunk`),
  docs/terminal.md; Gitea #178, #179, #181

## Context

Three gaps in the parser, found by the documentation review and a reading of
the string state:

- Inside an OSC, DCS, APC, SOS or PM string, any raw byte 0x9C was taken as
  ST, even as the last byte of a UTF-8 character: 东 is E4 B8 9C, so a title
  "东京" was cut to the two bytes E4 B8 -- no longer UTF-8 -- and the rest
  printed as text (#181).
- The C1 controls were dispatched twice, once for the raw byte and once for
  the UTF-8 encoded scalar, and the lists had drifted: SS2 and SS3 worked only
  raw, and a UTF-8 encoded ST left its C2 in the string (#178).
- CAN and SUB cancelled a CSI but were kept as payload inside a string, where
  VT100 and ECMA-48 have them end it (#179).

## Decision

- **A string body is read a character at a time where it is not ASCII.** The
  fast path copies runs of plain ASCII data and stops at ESC, CAN, SUB, BEL
  (for an OSC) and any byte from 0x80. ST is U+009C at a character boundary,
  raw (9C) or encoded (C2 9C); a 9C inside a character is data; an incomplete
  character at the end of a read waits for the next one, as text does
  (`terminal_utf8_pending`).
- **One C1 dispatch.** A UTF-8 encoded C1 control (C2 80..C2 9F) the parser
  knows is read as its byte and consumes both: IND, NEL, HTS, RI, SS2, SS3,
  DCS, SOS, CSI, ST, OSC, PM, APC (`terminal_c1_known`). ST with no string
  open is read and dropped.
- **CAN and SUB end a control string** without acting on it, in the body and
  after an ESC inside it; what follows is text.
- An OSC title longer than its 127-byte field is cut at a character boundary.

## Consequences

- Titles, hyperlinks and clipboard text in any script arrive whole.
- A program that relied on a raw 9C inside a character ending a string (none
  is known) would now see the string continue to its real terminator.
- The fast path still carries Kitty and Sixel payloads (base64 and sixel
  data are ASCII) in one copy per run.

## Evidence

terminal_conformance: the 13 C1 controls each raw and UTF-8 encoded, with
the same screen, and each shown to act; 东 and Ĝ in a title, written whole
and a byte at a time across reads; a UTF-8 ST whole and split between its
two bytes; a raw 9C at a boundary still ends a string; CAN and SUB in an
OSC; the over-long title. The pre-change binary fails the new cases (and
spun on the first draft of the fast path, caught by the same cases). The
terminal and renderer suites, graphics_ingestion_gate and the Kitty and
Sixel gates pass.
