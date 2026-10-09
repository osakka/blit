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
   newline mode, and 8-bit C1 controls (CSI/string dispatch plus IND, NEL,
   HTS, and RI) are covered.
   OSC terminal titles are retained in the session API and tested for both
   BEL and ST termination. OSC 8 hyperlinks, OSC 52 clipboard sets and OSC
   9/777 notifications are retained in the session API in Hibr's raw payload
   form; clipboard read queries are never answered. Cells expose the active
   OSC 8 URI as `link`.
   Evdev motion is committed
   at `SYN_REPORT`, so one physical input packet produces one pointer update;
   DEC focus reporting (`CSI ? 1004 h/l`) emits focus-in/focus-out reports
   when the single focused pane changes.
   DEC origin-mode cursor motion is constrained to the active scroll region.
   DEC 1049 alternate-screen entry saves and restores the main-screen cursor,
   pen, character-set, origin, and autowrap state; legacy 47/1047 switching
   retains its distinct buffer-switching semantics: 47 swaps persistent
   buffers, while 1047 clears the alternate buffer when it is left. RIS
   returns to the main buffer before resetting the terminal, and full-screen
   erases clear underline-colour state along with cell content.
   Insert mode (`CSI 4 h/l`), legacy alternate-screen mode (`CSI ?47 h/l`),
   DEC soft reset (`CSI !p`), and cursor style selection (`DECSCUSR`,
   `CSI Ps SP q`) are covered by the conformance vectors. Cursor styles are
   carried through the surface API and consumed by both renderers; blinking
   styles retain their VT identity even when a backend presents them steadily.
   VT500 C0 vertical-tab/form-feed line feeds and the legacy ESC Z DECID query
   are handled by the same parser and covered by the conformance gate.
   Kitty-compatible cursor restore (`CSI ? u`) and the ECMA-48 rule that C0
   controls execute inside CSI are also covered, including CAN/SUB sequence
   cancellation. SGR conceal (`8`/`28`) is retained in the canonical cell
   style and suppressed by both renderers while preserving the cell background.
   Synchronized output (`CSI ? 2026 h/l`) gates presentation of that session's
   output until the application releases the update batch; unrelated displays
   continue presenting normally. The synchronized surface retains its last
   complete frame and its damage until release. Its terminal state is reset by
   RIS and DEC soft reset. Colon SGR forms, underline colour (`58`/`59`), and
   OSC 10/11 background and foreground queries answer from validated
   `DT_INK`/`DT_FACE` (or `BLIT_TERMINAL_FG`/`BLIT_TERMINAL_BG`) session
   environment values, preserving Hibr theme negotiation without a shell
   wrapper.
   Hibr's modifyOtherKeys/cursor push-pop prefixes are parsed without leaking
   their control parameters into cell attributes.
   SGR italic (`3`/`23`), blink (`5`/`6`/`25`), and strike (`9`/`29`) are
   retained in the canonical style and exposed consistently through the API,
   SDL, and KMS paths.
   Device status replies include DA1/DA2, XTVERSION, CPR, window size,
   DECRQSS, and XTGETTCAP; erase operations preserve the active background
   colour according to BCE semantics. `CSI 3 J` also releases retained
   main-screen scrollback instead of merely clearing visible cells.

The direct KMS test remains the hardware oracle; the terminal engine must not
introduce a second display or terminal implementation.

Printable output follows DEC deferred-wrap semantics: writing the final
column leaves the cursor on that column with a pending-wrap flag, and only
the next printable glyph advances to the next line. Cursor reports and
intervening controls therefore observe the same state as Hibr's terminal
implementation; the transient flag is cleared by controls and is not part
of DECSC's saved cursor state.

Unicode cell width uses the generated Hibr-compatible Unicode 15.1 table:
combining marks, joiners, and variation selectors attach to the preceding
cell at width zero, while East Asian wide/full-width characters occupy two
cells. This is deliberately table-driven; broad guessed ranges are not
compatible with Hibr's wrapping contract.
Surface and scrollback snapshots serialize the complete UTF-8 cell sequence,
not only its base codepoint, so API consumers retain the same combining data
that the renderer receives.

Writing or erasing either half of a wide cell first invalidates the complete
wide glyph, including its continuation cell. This keeps the canonical grid
and both renderers free of orphaned wide-glyph halves.

Insert-line and delete-line operations preserve their scroll-region semantics
and reset the cursor column to zero, matching the Hibr terminal contract.

## Conformance gate

Run `tools/tests/terminal_conformance.rb` against a TCC-built server with the
dummy video backend and the bundled production font. Start the server with:

```sh
BLIT_PSF_FONT=build/blit-desktop-current/fonts/Lat15-Terminus16.psf \
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=build/sdl3-kms \
  build/blit/blit-server 19195
```

The runner currently asserts printable output, cursor
addressing, styles, erase, scrolling, scroll regions, alternate-screen
restoration, saved-cursor state, UTF-8 cells, truecolor SGR, and fragmented CSI input
through the public PTY/surface contract, plus cursor visibility, VT500
vertical/form-feed controls, legacy DECID, and malformed
CSI recovery. Malformed UTF-8 is represented as one U+FFFD replacement
cell per invalid leading byte, matching Hibr's xterm/VTE differential
contract. The Hibr differential set also covers deferred-wrap `EL`, wrapping
`REP`, DEC Special Graphics and SO/SI shifts, `CHT`, column-preserving `IL`
and `DL`, and overwriting or inserting through either half of a wide cell. A
missing or failing vector blocks the terminal gate; renderer screenshots do
not substitute for these canonical-state assertions. Broader remaining work
is complete VT500 semantic coverage and additional full Hibr redraw/input
workloads; these are tracked separately from the implemented conformance gate
and are not claimed by the current package.

The runner deletes each session after its snapshot and is run twice against the
same server as a bounded-reclamation check. Completed sessions must not exhaust
the fixed session pool, and deleting a session must release its surface, pane
attachment, focus, and input-route target.

The application keypad state accepts both the DEC private mode (`CSI ? 66 h/l`)
and the legacy VT selectors (`ESC =` / `ESC >`). DEC screen alignment (`ESC # 8`)
is implemented as a full-grid `E` fill for diagnostics and terminal probes.
   Legacy X10 mouse mode (`CSI ? 9 h/l`) and DEC private mode reports for
   alternate-screen modes (`47`, `1047`, `1049`) and mouse mode `9` are retained
   consistently with Hibr's terminal model.
   Deferred-wrap erase and `CSI S/T` scroll-up/scroll-down behavior are covered
   by the public conformance gate as well.

Supported terminal status queries include device attributes (`CSI c`), device
status (`CSI 5 n`), cursor position (`CSI 6 n`), and DEC mode reports
(`CSI ? Ps $ p` / `CSI Ps $ p`) for the modes Blit owns.
