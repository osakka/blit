# ADR 0229: Programs and the desktop -- one owner per concern

- Status: proposed (the owner asked for it, 2026-11-10; the 0.3.74 rows
  are in that release, with Hibr 0.99.482)
- Date: 2026-11-10
- Scope: everything a program in a Hibr window (Wayland now, X through
  Xwayland in ADR 0228's phase 2) shares with the desktop: what each side
  owns, the wire between them, and which release carries each part

## Context

ADR 0228 made blit-compositor the Wayland server for programs, with Hibr
as the window manager. The interop between programs and the desktop grew
one live finding at a time: input routed by the wrong id, subsurfaces not
shown, then, with Firefox, a window whose shadow margins showed and whose
own title bar could not move it. The owner asked for the whole of it to be
drawn out once -- "comprehensive interop, no hackiness, one source of
truth" -- with the clipboard and the Vault's rules in it, rather than added
piecemeal.

## Decision

### The owners

Each concern has exactly one owner of its truth. The others ask it or
mirror it; none keeps a second copy.

- **Blit** owns pixels and input: pictures and their placement, input
  devices, the keymap and key repeat, outputs and their modes, the pointer.
- **Hibr** owns windows and the person's session: where a window is, its
  size, stacking and focus; the clipboard's content, its history and the
  Vault's policy for secrets; the cursor theme; notifications; the screen
  saver; launching programs; the colour scheme.
- **blit-compositor** translates. It speaks Wayland to programs and the
  frame channel to the desktop, and holds nothing either side owns: no
  clipboard bytes, no window positions, no keymap of its own.
- **The program** owns its content, and its own frame when it says so.

Every offset on the wire -- a popup's, a subsurface's, an input member's,
Blit's pointer -- is in the top-level's **surface pixels** (its buffer's),
and `WINDOW geometry` says where the window itself sits in those same
pixels. The desktop crops with a source rectangle and subtracts the
origin once; Blit maps the pointer through source rectangles already.
`WINDOW size` is the window geometry's size, as xdg_toplevel defines it.

### The map

| Concern | Owner of truth | Wire | Release |
|---|---|---|---|
| Lifecycle and identity: new, title, app id, parent, hide, gone | program asks, Hibr decides | `WINDOW new/title/appid/parent/hide/gone` | done (0.3.70-0.3.73) |
| Decorations | the program says, Hibr draws or not | `WINDOW decor <id> server\|client` | done |
| Window geometry (the window inside its buffer: CSD shadows) | program | `WINDOW geometry <id> <x> <y> <w> <h>` | 0.3.74 |
| Moving and resizing from the program's own frame | Hibr runs the drag | `WINDOW move <id>`, `WINDOW resize <id> <edges>` | 0.3.74 |
| The window menu (right click on a CSD title bar) | Hibr | `WINDOW menu <id> <x> <y>` | 0.3.74 |
| States: maximized, fullscreen, activated, tiled | Hibr | `WINDOW size ... [tiled-left] [tiled-right] [tiled-top] [tiled-bottom]` | tiled in 0.3.74 |
| Popups and subsurfaces | program places, Hibr draws | `WINDOW popup`, `WINDOW sub` | done (0.3.74) |
| Keys, pointer, relative motion, pointer lock | Blit | input route, `WINDOW lock` | done |
| Keymap and repeat | Blit | `INPUT keymap` with a memfd | done (0.3.73) |
| Clipboard (Ctrl-C/V) and primary selection (middle click) | Hibr | `CLIP offer/read/clear` | 0.3.74 |
| Secrets on the clipboard (the Vault) | Hibr | the `x-kde-passwordManagerHint` type on `CLIP offer`; `CLIP clear` | 0.3.74 |
| Cursor shape | the program asks, Hibr's theme draws | `WINDOW cursor <id> <name>` (wp_cursor_shape_v1) | next |
| Cursor images (wl_pointer.set_cursor surfaces) | program | a picture, as a subsurface | decided with the next |
| Drag and drop | Hibr | `CLIP` extended with a drag | later |
| Activation (xdg_activation: a launched program's window raised) | Hibr | `WINDOW activate <id>` | later |
| Idle inhibit (video keeps the screen saver off) | Hibr | `WINDOW idle <id> on\|off` | later |
| Text input and input methods | Hibr | zwp_text_input_v3 | later |
| Scale (HiDPI): viewporter, fractional scale | Blit's outputs | `WINDOW output` with a scale | later |
| Portals: colour scheme, settings, notifications, screenshots | Hibr (Blit for screenshots) | D-Bus, xdg-desktop-portal | colour scheme with phase 2 (ADR 0228) |
| Touch, tablets, shortcut inhibit | Blit | the input route | later |
| Launching, app ids and icons (.desktop) | Hibr | none: the app id names it | Hibr's |
| Terminals and `WAYLAND_DISPLAY` | Hibr | the environment at start | Hibr's |
| X programs (phase 2) | as above | none new: `_MOTIF_WM_HINTS` is `decor`, `_NET_WM_MOVERESIZE` is `move`/`resize`, the X selections are `CLIP` | phase 2 |

"Later" rows are named so they are built once, in this shape, when they
come; none of them is half-built now.

### The wire in 0.3.74

All lines follow version 5's grammar, `<WORD> <verb> <id> ...`.

- **`WINDOW geometry <id> <x> <y> <w> <h>`**, compositor to desktop: the
  window's rectangle in its surface pixels, from
  xdg_surface.set_window_geometry, sent at the commit that changes it and
  in the re-announcement. A window that never sets one is its whole
  buffer, and no line is sent.
- **`WINDOW move <id>`**, **`WINDOW resize <id> <edges>`**, compositor to
  desktop: the program asked to be moved or resized from its own frame
  (xdg_toplevel.move/resize). Sent only while a pointer button is down on
  that window -- the request's serial is checked against the button's --
  so no program can start a drag by itself. `<edges>` is one of `top`,
  `bottom`, `left`, `right`, `top-left`, `top-right`, `bottom-left`,
  `bottom-right`. The desktop runs the drag as from its own frame, then
  sends `WINDOW size` as usual. Turning the window's input route off while
  it drags releases the program's button through the route's own release.
- **`WINDOW menu <id> <x> <y>`**, compositor to desktop: the window menu
  asked for at x, y in the window's surface pixels.
- **Tiled states** on `WINDOW size`: `tiled-left`, `tiled-right`,
  `tiled-top`, `tiled-bottom` (xdg_toplevel v2), for a window the desktop
  has put against an edge; a CSD program then drops its shadow on that
  side.
- **The clipboard**, both ways, two selections, `clipboard` (Ctrl-C/V,
  wl_data_device) and `primary` (middle click, zwp_primary_selection_v1):
  - `CLIP offer <seq> <selection> <type> ...`: the sender's selection is
    now the offer `<seq>` (a number the sender counts up), in these media
    types. From the compositor: a program set the selection (with a serial
    of a key or button on its window, as Wayland requires). From the
    desktop: Hibr's clipboard changed (a copy in a terminal, the Vault).
  - `CLIP read <seq> <type>` with a pipe's write end passed with the line
    (SCM_RIGHTS): the other side wants the offer's bytes in that type; the
    owner writes them and closes. From the compositor: a program pasted
    from the desktop's offer. From the desktop: Hibr wants a program's
    selection (for history at copy, or at paste: Hibr's choice).
  - `CLIP clear <seq>`: the offer is withdrawn (the program's source went,
    Hibr's clipboard was cleared). The compositor then sets the program
    side's selection to nothing.
  - The compositor stores no bytes. It keeps the current offer of each
    selection -- a Wayland source or the desktop's `<seq>` -- and streams a
    read from the owner to the reader through the pipe. A program with the
    keyboard is offered the selection when it gets the keyboard and when
    the selection changes, as Wayland defines.
  - **Secrets.** An offer that includes the type
    `x-kde-passwordManagerHint` (whose bytes are `secret`) is a secret:
    the Vault's offers carry it, and a program's that carry it (password
    managers) are treated so by Hibr. A secret enters no history on either
    side, and when Hibr's timeout clears it, `CLIP clear` ends it on the
    program side too, so a password pasted into Firefox does not stay on
    any clipboard.

### What is not done here

Drag and drop, activation, idle inhibit, text input, scale, the portals
and touch are the "later" rows: each has its owner and its wire's shape
above, and each is its own change with its own gate. Nothing in this
release half-implements one of them.

## Consequences

- A CSD program (GTK, Firefox, Chromium, Electron) is a whole window: no
  margins, moved and resized from its own frame, its menu the desktop's.
- Copy and paste between programs, terminals and the Vault is one
  clipboard, Hibr's, with one history and one policy for secrets.
- Phase 2's X window manager adds no wire: every X property above maps to
  a line that exists.
- Two more line families (`WINDOW geometry/move/resize/menu`, `CLIP`) on
  the channel, both version 5, both agreed with Hibr before they ship.

## Evidence

- 2026-11-10, the mini PC, Blit 0.3.73: Firefox showed only its frame
  (its page is a subsurface: fixed in 0.3.74), and its title bar could not
  move it (`xdg_toplevel.move` was a no-op: "moving and resizing are the
  desktop's, through its own frame", and a CSD window has no desktop frame).
- `blit_frames_input.h`: Blit maps the pointer through a placement's
  source rectangle, so a window cropped to its geometry is hit where it is
  drawn.
- Hibr's Vault (programs/desktop/README.md): a copied password never
  enters the clipboard history and clears itself after a while; this ADR
  carries that rule to programs.
