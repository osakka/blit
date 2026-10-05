# Terminal engine vertical slice

The direct-KMS text frame has passed on the two connected outputs. The next
slice replaces its fixed diagnostic text with one canonical terminal grid.

## Ownership

The terminal engine owns:

- one cell grid per logical pane;
- cursor, saved cursor, scroll region, alternate screen, and modes;
- UTF-8 decoding and ANSI/ECMA-48 control sequences;
- cell attributes, palette, hyperlinks, and damage regions;
- PTY window size and output flow control.

The KMS renderer consumes a read-only frame snapshot. It never parses PTY
bytes. Hibr remains an ordinary PTY client and is attached only after the
shell conformance path is stable.

## First acceptance path

1. Open one PTY with the preferred logical cell size for each joined output.
2. Feed PTY output into the canonical grid.
3. Render the same grid to both current outputs without duplicating state.
4. Route keyboard input to the PTY and pointer reports to the focused pane.
5. Resize the grid and PTY together when the API changes font or placement.
6. Verify shell prompt, colors, cursor movement, clear-screen, scroll, and
   alternate-screen behavior before starting Hibr. G0/G1 DEC character sets,
   SO/SI shifts, DEC 1047/1048 alternate-screen/cursor state, autowrap, and
   newline mode, and 8-bit C1 controls are covered.
   OSC terminal titles are retained in the session API and tested for both
   BEL and ST termination.
   Evdev motion is committed
   at `SYN_REPORT`, so one physical input packet produces one pointer update;
   DEC origin-mode cursor motion is constrained to the active scroll region.
   Insert mode (`CSI 4 h/l`), legacy alternate-screen mode (`CSI ?47 h/l`),
   DEC soft reset (`CSI !p`), and cursor style selection (`DECSCUSR`,
   `CSI Ps SP q`) are covered by the conformance vectors. Cursor styles are
   carried through the surface API and consumed by both renderers; blinking
   styles retain their VT identity even when a backend presents them steadily.
   Kitty-compatible cursor restore (`CSI ? u`) and the ECMA-48 rule that C0
   controls execute inside CSI are also covered, including CAN/SUB sequence
   cancellation. SGR conceal (`8`/`28`) is retained in the canonical cell
   style and suppressed by both renderers while preserving the cell background.
   Synchronized output (`CSI ? 2026 h/l`) gates presentation until the
   application releases the update batch; its terminal state is reset by RIS
   and DEC soft reset.

The direct KMS test remains the hardware oracle; the terminal engine must not
introduce a second display or terminal implementation.

## Conformance gate

Run `tools/tests/terminal_conformance.rb` against a TCC-built server with the
dummy video backend. The runner currently asserts printable output, cursor
addressing, styles, erase, scrolling, scroll regions, alternate-screen
restoration, saved-cursor state, UTF-8 cells, truecolor SGR, and fragmented CSI input
through the public PTY/surface contract, plus cursor visibility and malformed
CSI recovery. A missing or failing vector blocks the
terminal gate; renderer screenshots do not substitute for these canonical-state
assertions. Broader remaining work is complete VT500 semantic coverage and
additional full Hibr redraw/input workloads; these are tracked separately from
the implemented conformance gate and are not claimed by the current package.

The runner deletes each session after its snapshot and is run twice against the
same server as a bounded-reclamation check. Completed sessions must not exhaust
the fixed session pool, and deleting a session must release its surface, pane
attachment, focus, and input-route target.

The application keypad state accepts both the DEC private mode (`CSI ? 66 h/l`)
and the legacy VT selectors (`ESC =` / `ESC >`). DEC screen alignment (`ESC # 8`)
is implemented as a full-grid `E` fill for diagnostics and terminal probes.

Supported terminal status queries include device attributes (`CSI c`), device
status (`CSI 5 n`), cursor position (`CSI 6 n`), and DEC mode reports
(`CSI ? Ps $ p` / `CSI Ps $ p`) for the modes Blit owns.
