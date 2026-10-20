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
   The ECMA-48 rule that C0 controls execute inside CSI is also covered,
   including CAN/SUB sequence cancellation. `CSI > u`, `CSI < u`,
   `CSI = ... u` and `CSI ? u` are the Kitty keyboard protocol's flag
   sequences, not cursor save/restore: they move no cursor, and `CSI ? u`
   reports active flags. Candidate585 implements flags1/2/8 with separate
   bounded main/alternate stacks and exact evdev key encoding; flags4/16 are
   not advertised ([ADR0169](adr/0169-kitty-keyboard-super-events.md)).
   SGR conceal (`8`/`28`) is retained in the canonical cell
   style and suppressed by both renderers while preserving the cell background.
   Synchronized output (`CSI ? 2026 h/l`) gates presentation of that session's
   output until the application releases the update batch; unrelated displays
   continue presenting normally. The synchronized surface retains its last
   complete frame and its damage until release. Its terminal state is reset by
   RIS and DEC soft reset. Colon SGR forms, underline colour (`58`/`59`), and
   OSC 10/11 background and foreground queries answer from validated
   `DT_INK`/`DT_FACE` (or `BLIT_TERMINAL_FG`/`BLIT_TERMINAL_BG`) session
   environment values, preserving Hibr theme negotiation without a shell
   wrapper. API-owned providers may carry the same pair as
   `terminal_default_foreground`/`terminal_default_background` on the
   revisioned cell update. That pair is also what both renderers paint an
   **unstyled** cell with, and what the cell snapshot reports for it, so the
   colour an application is told is the colour it is shown. OSC 10/11 are accepted as
   sets as well as queries, in either the `rgb:RRRR/GGGG/BBBB` or `#rrggbb`
   spelling, and a set repaints the whole surface.
   Physical keys carry xterm's modifier parameter: a held Shift, Alt or Ctrl
   turns an arrow, `Home`/`End` or `F1`-`F4` into `CSI 1;M <final>` and an
   editing or `F5`-`F12` key into `CSI n;M ~`, where M is 1 plus 1 for Shift,
   2 for Alt and 4 for Ctrl. Shift+Tab is `CSI Z`. Ctrl with `[ \ ] ^ _ ?`,
   space or the digits that spell them produces the corresponding C0 control
   rather than the printable character. Meta-sends-escape still applies to a
   literal character, but not to a control sequence, which carries Alt in its
   parameter instead. A key may be injected by evdev code through
   `POST /v1/input/inject`, which runs the same encoder as a physical device.
   Modifier state is what is physically held across every keyboard, so
   releasing one of two held Shift keys keeps Shift down and a device that
   disappears mid-hold does not leave one stuck; the kernel's own key and LED
   state is adopted when a keyboard is opened. CapsLock inverts Shift for
   letters only. NumLock selects between the keypad's digits and its cursor
   and editing block, and defaults to digits when no device LED says
   otherwise. The right Alt key is AltGr and does not prefix ESC the way the
   left one does -- but with only a US table there is no third level for it to
   select, so **keyboard layout is still US-only**: that, and a keymap source
   to replace the built-in table, are what remains of the keyboard work.
   Kitty graphics placements (source rectangle, cell offset, numbered
   placements, the `a i c p q x y z` delete targets) and the refusal of
   unimplemented keys are specified in
   [ADR 0060](adr/0060-kitty-placements-source-rectangles-and-delete-targets.md).
   Multi-chunk Kitty uploads keep the opening chunk's image id in the owning
   terminal session; conforming `q=2,m=1` and `q=2,m=0` continuations omit
   `i=` and are resolved to that current upload. See
   [ADR 0084](adr/0084-kitty-continuations-use-the-current-upload.md).
   Hibr also sends 4096 base64 bytes per Kitty chunk. The bounded APC buffer
   therefore carries the control header and the complete chunk before decode;
   the boundary is covered by the large-chunk graphics trace gate and
   [ADR 0096](adr/0096-kitty-apc-buffer-carries-the-full-hibr-chunk.md).
   Hibr wallpaper is Kitty `f=100` PNG data, not a request to synthesize
   half-block cells. Blit accumulates the complete bounded transmission,
   decodes PNG into RGBA graphics, and presents those pixels in the graphics
   scene. Half-block or block output is only a separately selected terminal
   rendering mode; it is never an automatic substitute for a rejected or
   incomplete Kitty image. The trace gate exercises a multi-chunk PNG and
   rejects neither its full chunks nor its decoded image.
   The `CSI >` modifyOtherKeys family and the Kitty keyboard flag prefixes are
   parsed and consumed without leaking their control parameters into cell
   attributes.
   SGR italic (`3`/`23`) is presented as a slant: the glyph's top edge moves
   right and its bottom edge left, which is deliberately not bold's
   double-strike, so the two attributes are distinguishable on screen.
   SGR blink (`5`/`6`) is retained in the canonical style and **presented
   steadily**, like the blinking cursor styles above. A blink phase would have
   to mark the blinking cells damaged about twice a second for as long as they
   exist, which contradicts the idle-frame contract that an otherwise quiet
   surface schedules no frames (`tools/tests/adapter_coalesce_gate`). A client
   that needs blink can read the attribute from the cell state.
   SGR italic (`3`/`23`), blink (`5`/`6`/`25`), and strike (`9`/`29`) are
   retained in the canonical style and exposed consistently through the API,
   SDL, and KMS paths.
   DA1 answers `CSI ? 62 ; 4 ; 22 c` (level 2, sixel, ANSI colour); the `4`
   is load-bearing, because Hibr treats a DA1 reply without it as a definite
   "no sixel" and falls back to half blocks (issue #87).
   Device status replies include DA1/DA2, XTVERSION, CPR, window size,
   DECRQSS, and XTGETTCAP; erase operations preserve the active background
   colour according to BCE semantics.
   DECRQSS answers every setting Blit owns the state for (issue #5): DECSTBM,
   DECSCUSR, SGR, DECSLRM, DECSCA, DECSCL, DECSLPP, DECSCPP, DECSNLS and
   DECSACE, and returns the invalid-request form only for a setting it does
   not. It used to answer three. DECSCL reports the level DA1 reports rather
   than a level Blit does not select; DECSCPP and DECSLPP agree with the
   `cols` and `lines` XTGETTCAP answers because both read the surface.
   The VT500 rectangular area operations are implemented (issue #2): DECFRA
   `$x`, DECERA `$z`, DECSERA `${`, DECCRA `$v`, DECCARA `$r`, DECRARA `$t`
   and DECSACE `*x`, with DECSCA `"q` providing the character protection
   DECSERA needs. The CSI parser records the intermediate byte and dispatches
   on the pair: before that the final byte alone decided, so `CSI 1;4$r`
   (DECCARA) was executed as `CSI 1;4r` (DECSTBM) and silently redefined the
   scroll region. The same collision through the private prefix is closed too
   -- `CSI ?2J` is DECSED and leaves protected cells, where it used to be
   plain ED.
   A rectangle is page-relative, not margin-relative, because these
   operations do not shift a line; origin mode is the one exception and makes
   coordinates relative to the margins in both axes. An inverted or empty
   rectangle is ignored rather than clamped, as DECSTBM and DECSLRM already
   are. DECCRA copies through a buffer so an overlapping copy is not smeared.
   DECCARA and DECRARA accept only the four VT500 attribute parameters and
   their clearing forms.
   Left/right margins are implemented (issue #3): DECLRMM (`CSI ?69h`) enables
   the model, `CSI Pl;Pr s` is DECSLRM while it is on and SCOSC while it is
   off, and insertion, deletion, scrolling, autowrap, CR, BS, HT and origin
   mode all work within the margins. One pair of accessors answers "which
   columns may this operation touch", and with DECLRMM off they return 0 and
   width-1 -- so every client that has not asked for margins behaves exactly
   as before, which is what the conformance suite at full width proves.
   A cursor outside the horizontal margins makes an insert or delete a no-op,
   the rule DEC states for the scroll margins; a two-cell glyph that would
   straddle the right margin wraps before it is placed.
   XTGETTCAP answers with real values for the capabilities Blit owns -- `TN`,
   `Co`/`colors`, `RGB`, `cols`, `lines`, and the booleans `Tc` and `bce` --
   and refuses the rest, one DCS reply per requested name (issue #4). It used
   to refuse everything, which pushed a client that trusted
   `TERM=xterm-kitty` onto its most conservative path. The arrow keys are
   deliberately refused rather than answered: `kcuu1` is `ESC O A` in
   application cursor mode and `ESC [ A` outside it, so any single value would
   be wrong half the time. `CSI 3 J` also releases retained
   main-screen scrollback instead of merely clearing visible cells.

## Sixel

A decoded sixel image is an ordinary graphics image, so the decoder's bound is
the graphics image bound: 4096 by 4096 pixels, which at four bytes a pixel is
exactly the 64 MiB graphics byte budget. The same numbers are what
XTSMGRAPHICS answers for item 2 and what `GET /v1/diagnostics` publishes under
`graphics`, because a client sizes its image to what it was told and an
advertised limit that disagrees with the enforced one is worse than no answer.
The canvas is allocated band by band as the stream reaches them, so an 80x40
picture does not pay for a full-size buffer.

Pixels outside the bound are clipped and only a payload that cannot be parsed
at all is rejected (issue #8). The bound was a literal 512 until then, which
is below any realistic picture -- a terminal-width image on a 1920-pixel
output was refused outright, and `!1920~`, the most natural thing an encoder
emits for a flat region, was refused twice over.

The colour registers belong to the session, not to one DCS string: a stream
may define its palette once and send several images (issue #10). The first
sixteen default to the VT340's documented colours and the rest to the same
256-colour palette the text path uses, so a register nobody defined reads as
the colour that index means everywhere else in this terminal; they used to all
be white, which made a partly-defaulted image render partly correct and partly
as white blocks. RIS and DECSTR drop the palette.

Both colour spaces are decoded. `Pu=1` is DEC's HLS, whose hue origin is blue
at zero degrees, so 120 is red and 240 is green; `Pu=2` is RGB in percent.
A component out of range clamps and a colour space Blit does not model leaves
the register as it was, because the picture staying visible beats every
alternative (issue #9).

The introducer's positional parameters are read. `P1` (pixel aspect) and `P3`
(horizontal grid size) are drawn at 1:1 and ignored; `P2` is the background
select, where 1 leaves an unset pixel transparent and 0 and 2 paint it with
the current background colour. A bare `DCS q` means `P2=0`, so the common
introducer asks for an opaque image -- which the always-transparent scratch
buffer could never produce (issue #11).

Placement follows the mode pair. Mode 80 is DECSDM: **set** is display mode,
which homes the image and leaves the cursor alone; **reset**, the power-up
default, is sixel scrolling, which places the image at the cursor, scrolls the
screen by the rows the image needs, and leaves the cursor at the start of the
line below it. Mode 8452 leaves the cursor to the right of the graphic on its
own last row instead. Both answer DECRQM. The polarity of mode 80 is worth
reading twice: it is DEC's and current xterm's, but xterm carried the inverse
for years, so a client written against old xterm asks for the opposite of what
it wants -- which is exactly why DECRQM has to answer. Blit also differs from
DEC on one point and says so here: in display mode DEC clips the image to the
graphics region, and Blit clips it to the screen.

Images placed by Kitty and sixel are page-anchored graphics: subsequent index,
scroll-region, and insert/delete-line operations move them with their owning
text rows and clip them at the active page boundary. A placement that leaves
the page is retained only as bounded image data and is no longer visible;
Blit does not claim graphics scrollback. API pixel frames are scene-owned and
remain at their absolute coordinates when terminal text scrolls. This
distinction is explicit in the graphics record rather than inferred from the
current pixel position (issue #62).

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
BLIT_PSF_FONT=assets/fonts/Blit-Hibr-Terminus16.psf \
SDL_VIDEODRIVER=dummy LD_LIBRARY_PATH=build/sdl3-kms \
  build/blit/blit-server 19195
```

`assets/fonts` is the authoritative font location: it is what the Debian
package installs, and `packaging/build-deb.sh` copies it *over* any font a
vendored bundle supplies. A font loaded from `build/` may be a different
artifact under the same name -- `tools/tests/font_identity_gate.rb` fails if
one is, and reports the loaded font's glyph and mapping counts, which
`GET /v1/fonts` also exposes as `active.glyphs` and `active.mappings`.

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

### Layering and slot lifetime (issues #84, #85)

Graphics are composed per surface in this order: images with
   `z < -1073741824` (under cell backgrounds), opaque background quads, images
   with `-1073741824 <= z < 0` (over backgrounds, under text), glyphs, cursor,
   images with `z >= 0`. A cell whose resolved background equals the terminal
   default draws no quad, so an image under the text shows through it; the
   default colour (OSC 11, an API terminal-default update, or black) is instead
   the floor of the surface. A cell
with an explicit colour, including explicit black on a non-black default, is
opaque. Hibr's wallpaper is `z = -1073741825` and depends on all of this. OSC
10/11 queries are always answered, from the session default when nothing was
configured. The direct KMS compositor applies the same bands and floor (issue #88,
`tools/tests/kms_compose_gate.c` drives `draw_view` against a memory buffer);
an image placed, moved or deleted redraws the whole view there.

A session has eight image slots shared by Kitty, sixel and API images. A
sixel has no id a client can place again, so one that scrolls off the page is
released immediately, and a full table reclaims the oldest complete image
with no placement before refusing with `image-store-full`.

Within each z band, the flattened draw list is ordered by Kitty image id and
then placement id. This ordering is explicit and independent of the internal
image-slot allocation order, so equal-z overlaps have one result on SDL and
direct KMS. Replacing a `(image id, placement id)` reference therefore changes
one retained scene record; the presentation boundary repaints the old and new
coverage before publishing the frame. See [ADR 0110](adr/0110-kitty-retained-scene-ordering-and-invalidation.md).

On direct KMS, a graphics-only mutation uses the union of the previous and
current retained-graphics bounds, quantized outward to complete cells. The
old rectangle is included so replacement and deletion reveal the cached scene;
the current rectangle is included so the new placement is composed. If either
scene cannot be bounded, the presenter deliberately falls back to a complete
view redraw. This keeps the atomic frame boundary while avoiding full-output
wallpaper work for small moving graphics. See [ADR 0111](adr/0111-bounded-retained-graphics-damage.md).
