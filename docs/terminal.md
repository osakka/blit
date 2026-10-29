# Terminal reference

What Blit's terminal emulator understands, taken from the code
(`src/blit_server.c`: parser `terminal_consume_chunk`, CSI
`terminal_apply_csi`, SGR `terminal_apply_sgr`, OSC `terminal_apply_osc`,
DCS `terminal_apply_string`, Kitty graphics `terminal_apply_kitty`). Where
this page and the code disagree, the code is right and this page is a bug.

Programs started in a session get `TERM=xterm-kitty` and
`COLORTERM=truecolor`. Blit ships no terminfo of its own: install
`kitty-terminfo` (or `ncurses-term`) for programs that look it up.

## At a glance

| Area | Supported |
| --- | --- |
| Colour | 16, 256 and 24-bit truecolour; underline colour (SGR 58/59) |
| Attributes | bold, dim, italic, underline, blink (drawn steady), reverse, hidden, strikethrough |
| Screens | main screen with 1,000 lines of scrollback; alternate screen (47, 1047, 1049) |
| Editing | insert/delete characters and lines, erase, repeat, scroll regions, left/right margins, VT400 rectangle operations including copy (DECCRA) |
| Mouse | X10/normal/button/any-motion tracking; legacy, SGR and SGR-pixel encodings; wheel and horizontal wheel |
| Keyboard | xterm modifier encoding, application cursor and keypad modes, Kitty keyboard protocol flags 1, 2 and 8 |
| Graphics | Kitty graphics protocol (direct transmission, RGB/RGBA/PNG, zlib, placements, in-place edits); Sixel |
| Text | Unicode 15.1 widths, combining marks (16 per cell), colour emoji with Emoji 18.0 sequences, Arabic shaping, Nerd Fonts, Blit icons |
| Other | hyperlinks (OSC 8), notifications (OSC 9, 777), clipboard write (OSC 52), pointer shapes (OSC 22), synchronized output (2026), bracketed paste, focus events, pointer lock (7701) |

## Control characters

| Code | Effect |
| --- | --- |
| BS, HT, CR | As usual; HT stops at the right margin. |
| LF, VT, FF | Line feed; also a carriage return when LNM (mode 20) is set. |
| SO, SI | Select G1 / G0. |
| CAN, SUB | Cancel a sequence in progress. |
| BEL, ENQ, NUL, DEL | Ignored: no bell, no answerback. |
| 8-bit C1 | IND, NEL, HTS, RI, DCS, SOS, CSI, ST, OSC, PM, APC (raw or UTF-8 encoded). SOS and PM are read and discarded. |

## Escape sequences

| Sequence | Effect |
| --- | --- |
| `ESC ( 0`, `ESC ) 0` | DEC Special Graphics (line drawing) into G0 / G1; any other final selects ASCII. |
| `ESC =`, `ESC >` | Application / normal keypad. |
| `ESC 7`, `ESC 8` | Save / restore cursor, attributes, character sets, origin and wrap mode (per screen). |
| `ESC D`, `ESC M`, `ESC E` | Index, reverse index, next line. |
| `ESC H` | Set a tab stop. |
| `ESC # 8` | Fill the screen with E (alignment test). |
| `ESC c` | Full reset, including keyboard flags, mouse modes, pointer lock, scrollback and the sixel palette. |
| `ESC Z` | Identify (answers as DA1). |

## CSI sequences

| Sequence | Name |
| --- | --- |
| `CSI n @`, `CSI n P` | Insert / delete characters (within left/right margins) |
| `CSI n A` `B` `C` `D` | Cursor up / down / forward / back |
| `CSI n E`, `CSI n F` | Cursor next / previous line |
| `CSI n G`, `` CSI n ` `` | Column absolute |
| `CSI r;c H`, `CSI r;c f` | Cursor position (origin-relative under DECOM) |
| `CSI n I`, `CSI n Z` | Tab forward / back |
| `CSI n J`, `CSI ? n J` | Erase in display, 0–3 |
| `CSI n K`, `CSI ? n K` | Erase in line, 0–2 |
| `CSI n L`, `CSI n M` | Insert / delete lines (within margins) |
| `CSI n S`, `CSI n T` | Scroll up / down (within margins) |
| `CSI n X` | Erase characters |
| `CSI n a`, `CSI n e` | Column / row relative |
| `CSI n b` | Repeat the last character (or emoji) |
| `CSI n d` | Row absolute |
| `CSI n g` | Clear tab stop (0) / all (3) |
| `CSI 4 h/l`, `CSI 20 h/l` | Insert mode, new-line mode |
| `CSI ? n h/l` | DEC private modes (below) |
| `CSI … m` | SGR (below) |
| `CSI 5 n`, `CSI 6 n` | Status report, cursor position report |
| `CSI t;b r` | Scroll region |
| `CSI l;r s` | Left/right margins when DECLRMM is set; otherwise save cursor |
| `CSI u` | Restore cursor |
| `CSI 18 t` | Report the text area size in cells (the only window operation answered) |
| `CSI n SP q` | Cursor style 0–6 (blinking styles are drawn steady) |
| `CSI ! p` | Soft reset |
| `CSI n $ p`, `CSI ? n $ p` | Report a mode's state |
| `CSI n " q` | Character protection |
| `CSI … $ x`, `$ z`, `$ {`, `$ v`, `$ r`, `$ t`, `* x` | Fill, erase, selective erase, copy, change and reverse attributes in a rectangle, attribute change extent |
| `CSI c`, `CSI > c`, `CSI > q` | Device attributes (`?62;4;22;28c`), secondary attributes (`>1;10;0c`), version (`blit-term`) |
| `CSI ? 1;… S` | Sixel graphics attributes (256 registers, 4096×4096) |

### SGR

| Code | Effect |
| --- | --- |
| 0 | Reset |
| 1, 2, 3, 4, 5, 7, 8, 9 | Bold, dim, italic, underline, blink, reverse, hidden, strikethrough |
| 22, 23, 24, 25, 27, 28, 29 | Turn the above off |
| 4:0, 4:3 | No underline; curly underline (drawn straight) |
| 30–37, 40–47, 90–97, 100–107 | 16 colours |
| 38, 48, 58 with `;5;n` or `;2;r;g;b` (or colons) | 256 colours and truecolour for text, background and underline |
| 39, 49, 59 | Default text, background and underline colour |

### DEC private modes

| Mode | Meaning |
| --- | --- |
| 1 | Application cursor keys |
| 6 | Origin mode |
| 7 | Auto-wrap |
| 9, 1000, 1002, 1003 | Mouse tracking: X10, normal, button-event, any-event |
| 1004 | Focus in/out events |
| 1006, 1016 | SGR and SGR-pixel mouse encoding |
| 25 | Show cursor |
| 47, 1047, 1049 | Alternate screen (1049 also saves the cursor and clears) |
| 1048 | Save / restore cursor |
| 66 | Application keypad |
| 69 | Left/right margin mode |
| 80 | Sixel display mode |
| 8452 | Sixel leaves the cursor to the right of the image |
| 2004 | Bracketed paste |
| 2026 | Synchronized output: the pane is not redrawn until the update ends |
| 7701 | Blit pointer lock (below) |

## OSC

| OSC | Effect |
| --- | --- |
| 0, 1, 2 | Window title (up to 127 bytes) |
| 8 | Hyperlinks (up to 32 per session) |
| 9, 777 | Desktop notifications |
| 10, 11 | Set or query the default text and background colour |
| 22 | Pointer shape: set, push, pop, query; `none` hides it |
| 52 | Write the clipboard (up to 4 KiB, read through the API); reading is not offered |

## DCS

| Request | Effect |
| --- | --- |
| `DCS … q` | Sixel image |
| `DCS $ q` | Request a setting (DECRQSS): margins, cursor style, SGR, protection, conformance and others |
| `DCS + q` | Terminal capabilities (XTGETTCAP): name, colours, RGB, size, `Tc`, `bce` |

## Mouse, paste, focus and keyboard

- **Mouse:** button presses, releases, motion and the wheel (64/65, and
  66/67 for horizontal) with Shift, Alt and Ctrl. With no tracking mode the
  wheel scrolls back through history and a click focuses the pane. In
  pixel mode, motion is reported at most once a frame (16.7 ms).
- **Bracketed paste:** text pasted through the API in `paste` mode arrives
  wrapped in `CSI 200~ … CSI 201~`.
- **Focus:** `CSI I` / `CSI O` when the focused pane changes (mode 1004).
- **Keyboard:** xterm encoding of modified keys (`CSI 1;5A` and so on),
  F1–F20, application cursor and keypad modes. The Kitty keyboard protocol
  is supported with flags 1 (disambiguate), 2 (report event types) and 8
  (report all keys as escape codes), with its push/pop/query stack per
  screen.
- **Pointer lock (mode 7701, [ADR 0198](adr/0198-pointer-lock.md)):** a game
  or emulator in the focused pane can capture the mouse and receive raw
  movement as `CSI < b;dx;dy M`. Blit releases it when the pane loses focus,
  the program exits or resets, or input goes unread for 2 seconds, and
  says so with `CSI ? 7701;2 $ y`.

## Kitty graphics

| Item | Supported |
| --- | --- |
| Actions | `a=t` transmit, `a=T` transmit and show, `a=p` place, `a=d` delete, `a=q` query, `a=f` with `r=1` edit the image in place ([ADR 0200](adr/0200-kitty-images-change-in-place.md)) |
| Transmission | Direct (`t=d`), in chunks (`m=1`) |
| Formats | `f=24` RGB, `f=32` RGBA, `f=100` PNG; `o=z` zlib compression |
| Placement | source rectangle `x y w h`, cell offset `X Y`, size in cells `c r`, layer `z`, placement id `p`, `C=1` to keep the cursor |
| Replacing | transmitting to an existing id replaces its pixels and keeps its placements |
| Delete | `a i z c p q x y`, and their capitals to free the image too |
| Replies | `OK`, `EINVAL`, `ENOENT`, `ENOSPC`; `q=1` hides OK, `q=2` hides everything |
| Limits | 8 images per session (shared with Sixel), 8 placements per image, 4096×4096 pixels, 64 MiB per image |

Images scroll with their text and are clipped at the edge of the pane.

## Sixel

Sixel images with HLS and RGB colour, 256 colour registers per session
(VT340 colours for the first 16), raster attributes, repeat, transparent
background (`P2=1`), display mode (80) and cursor placement (8452).

## Text

- Character widths follow Unicode 15.1, shared with Hibr.
- Combining marks, variation selectors and zero-width joiners attach to the
  previous cell (up to 16 per cell).
- Colour emoji: sessions created with `text_layout: "emoji"` get the full
  Emoji 18.0 sequence set (5,244 sequences: ZWJ families, flags, skin tones)
  laid out as two-cell clusters. The default `scalar` layout keeps the
  classic one-character-per-cell behaviour.
- Arabic letters are shaped with HarfBuzz and drawn from the Arabic
  companion fonts, with Arabic-Indic digits. Bidirectional ordering is the
  application's job; Blit sets `DT_BIDI=on` so Hibr does it.
- Invalid UTF-8 becomes U+FFFD.
- Glyphs come from the selected font, then the glyph stack: Nerd Fonts
  symbols, Unifont for scripts the font lacks, colour emoji, and Blit's own
  icons. See the [font reference](font-reference.md).

## Limits

| Item | Limit |
| --- | --- |
| Grid | 512 columns × 160 rows |
| Scrollback | 1,000 lines, main screen only |
| Escape string (OSC, DCS, APC) | 16 KiB |

## Known gaps

These are what the code does today, recorded so nobody relies on them.

- **Sixel images larger than 16 KiB of data are cut off**: the escape
  string buffer drops what does not fit. Small sixels work; large ones
  should use the Kitty protocol, which is chunked.
- **Bright colours 90–97 / 100–107 and palette entries 8–15 are computed
  darker than they should be.**
- SGR `4:1`, `4:2`, `4:4`, `4:5` (underline styles) also turn on another
  attribute; `38:2:CS:r:g:b` with a colour-space id is misread.
- `CSI 3 J` clears the screen as well as the scrollback.
- Line feeds and wrapping inside left/right margins scroll the full width.
- Only G0 and G1 exist; `ESC *` and `ESC +` act on them.
- `CSI = c` answers as DA1. Window operations other than 18 are ignored.
- Mode 9 reports like 1000. DECRQM answers 0 for 69, 1048 and 2026.
- DECRQSS for SGR reports attributes but not colours.
- Kitty: no file, temporary-file or shared-memory transmission, no
  animation, no Unicode placeholders; some failures send no reply.
- No bell, no OSC 4/104 palette changes, no OSC 52 read.
