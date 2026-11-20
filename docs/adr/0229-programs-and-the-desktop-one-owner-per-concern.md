# ADR 0229: Programs and the desktop -- one owner per concern

- Status: accepted
- Note: The 0.3.74 rows shipped with Hibr 0.99.482; later rows (drag and drop, input methods, viewports) shipped through 0.3.125.
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
| Cursor shape | the program names, Blit's pointer art draws (one source: OSC 22's names), Hibr relays | `WINDOW cursor <id> <name>` (wp_cursor_shape_v1); `none` hides | 0.3.76 |
| Cursor images (wl_pointer.set_cursor surfaces) | none: never drawn, the display's own pointer (`default`) | -- | decided (owner: the same cursors we already have) |
| The global menu (programs' menus in Hibr's bar) | the program serves them (D-Bus, `com.canonical.dbusmenu`), Hibr shows them | `WINDOW appmenu <id> <service> <path>` (org_kde_kwin_appmenu); `- -` none | 0.3.77 (owner confirmed, Hibr #500) |
| Drag and drop | Hibr | `CLIP` extended with a drag: a drop onto a Wayland program (`CLIP drop`, `CLIP dropped`) in 0.3.114, onto an X program (XDND) in 0.3.117; hover feedback and dragging out later | 0.3.114 (drop), 0.3.117 (X) |
| Activation: a program asks for its window's focus (xdg_activation's token; an X program's `_NET_ACTIVE_WINDOW`, or X's focus taken itself and put back) | program asks, Hibr decides (its focus-stealing setting) | `WINDOW activate <id> x-focus\|x-active\|xdg-activation`; granted with `WINDOW size ... activated` | 0.3.86 |
| Idle inhibit (video keeps the screen saver off) | Hibr | `WINDOW idle <id> on\|off` | later |
| Text input and input methods | Hibr (the desktop's fields); the engine itself is hibr-arabizi's, also run by Blit while a program window has the keys (ADR 0233, amended 2026-11-13) | zwp_text_input_v3 in blit-compositor for program windows | later |
| Displays: which there are, their places, names and refresh, the primary (programs' wl_outputs, X's RandR outputs and primary) | Hibr (its layout of Blit's displays) | `WINDOW layout <name> <x> <y> <w> <h> <mHz>`, `WINDOW layout end`, `WINDOW primary <name>`; a window's display by `WINDOW output` | 0.3.88 (Hibr #514) |
| Scale (HiDPI): viewporter, fractional scale | Blit's outputs | `WINDOW output` with a scale | later |
| Portals: colour scheme, settings, notifications, screenshots | Hibr (Blit for screenshots) | D-Bus, xdg-desktop-portal | colour scheme with phase 2 (ADR 0228) |
| Touch, tablets, shortcut inhibit | Blit | the input route | later |
| Launching, app ids and icons (.desktop) | Hibr | none: the app id names it | Hibr's |
| Terminals and `WAYLAND_DISPLAY` | Hibr | the environment at start | Hibr's |
| X programs (phase 2) | as above | the X display name on `WINDOW display <wayland-name> <x-display>`; otherwise none new: `_MOTIF_WM_HINTS` is `decor`, `_NET_WM_MOVERESIZE` is `move`/`resize`, the X selections are `CLIP`, `_NET_ACTIVE_WINDOW` and X's focus taken are `activate` | phase 2 |
| Keyboard grab for games (every key to the program) | the program asks, Hibr grants | keyboard-shortcuts-inhibit; the `INPUT keep` question decided with it | with phase 2's games |
| No screen saver mid-game or mid-video | Hibr | idle-inhibit: `WINDOW idle <id> on|off` | with phase 2's games |
| Gamepads | the program (Steam reads `/dev/input` itself) | none: a device permission on the machine | -- |

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
  `bottom-right`. The desktop runs the drag as from its own frame (Hibr's
  DT_DRAG and DT_SIZE, set at a press and driven by the drag reports),
  then sends `WINDOW size` as usual. It has the press: Blit reports every
  press, drag and release to the terminal as well as to a live route. It
  sends `INPUT off <id>` when the drag starts, which releases the
  program's button through the route's own release and keeps the drag's
  motion from the program, and `INPUT on <id>` after.
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
  - **Text, one type.** Hibr's clipboard is text and reads a program's
    selection eagerly, at copy, for its history (at most 1 MB kept). The
    compositor normalizes: a program's offer with any text type
    (`text/plain;charset=utf-8`, `text/plain`, `UTF8_STRING`, `STRING`,
    `TEXT`) reaches the desktop as `text/plain;charset=utf-8`, read from
    the program's best text type; an offer with no text type reaches it
    with no types, so the desktop knows the selection moved to something
    it does not hold (programs still paste it from one another through the
    compositor). The desktop's offers are advertised to programs with the
    text aliases, every read asked of the desktop as
    `text/plain;charset=utf-8`.
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

Drag and drop, idle inhibit, text input, scale, the portals
and touch are the "later" rows (activation came in 0.3.86): each has its owner and its wire's shape
above, and each is its own change with its own gate. Nothing in this
release half-implements one of them.

## Consequences

- A CSD program (GTK, Firefox, Chromium, Electron) is a whole window: no
  margins, moved and resized from its own frame, its menu the desktop's.
- Copy and paste between programs, terminals and the Vault is one
  clipboard, Hibr's, with one history and one policy for secrets.
- Phase 2's X window manager adds one word of wire (the X display name);
  every X property above maps to a line that exists.
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
- `compositor_gate`, 0.3.74's rows: `--csd` (decor client, geometry 10 px
  inside, tiled left and top reaching the program, move, menu and
  bottom-right resize from its own presses, one move with no press
  ignored); `--clip` (a copy offered as the one text type and read through
  a pipe, a stale or unknown read answered empty, the desktop's offer
  pasted through `CLIP read` with the five aliases shown, a secret offered
  and cleared both ways, a source's destruction clearing, the primary
  both ways) and, across a restart, the program's selection offered again
  and the old desktop's lapsed. ASan and UBSan clean.
- **Cursors are Blit's (2026-11-10).** The owner: "one source of truth --
  these should be the same cursors we have already". Blit already draws
  the pointer from its own art chosen by name (OSC 22,
  `blit_pointer_name_shape`), and Hibr 0.99.482 already turns `WINDOW
  cursor` into a window's OSC 22 shape. So programs only name shapes
  (Debian 13's GTK 3.24.49 binds `wp_cursor_shape_manager_v1` and names CSS
  shapes, verified in its gdkdevice-wayland.c), `none` hides (Hibr's X
  Screen already uses it), and cursor images are never drawn. The
  protocol needs wayland-protocols 1.32: the build's minimum rose
  (ADR 0230), and a host below it skips the compositor gate with the
  reason.

## Amended 2026-11-17 (0.3.114): a file drop onto a program window

The owner dropped a file from Files onto mpv (a Wayland program) and was
told "does not take files": the compositor cancelled every drag, and the
desktop had no way to hand a program a drop. Agreed with Hibr: the desktop
sends `CLIP drop <seq> <window> <x> <y> <types>` at the release (the window
and the point in its own pixels, from its hit test), and its frames module
answers the compositor's `CLIP read` from what it holds, as a paste is
answered. The compositor runs the program's side of a Wayland drag at that
point and says `CLIP dropped <seq> taken|refused`, so "does not take files"
is the program's own answer, not a guess.

Two things learned from real programs, both kept:
- **A motion at the drop point.** GTK accepts nothing on the enter alone;
  it decides on a motion. The compositor sends one at the drop point
  right after the enter, and again every 150 ms until the program answers
  or its second is up.
- **Reads before the drop.** Firefox reads `text/uri-list` on each motion,
  to decide whether to take it, and turns down a drop whose early read
  came back empty. Every `receive` from the enter on is forwarded as a
  `CLIP read`.

A drop is a copy: the files stay where they are. One at a time. Not yet:
hover feedback while the drag is still moving (`CLIP drag`), X programs
(XDND), and a program dragging out to the desktop.

Evidence:
- compositor_gate `COMPOSITOR_DROP_OK`, under ASan/UBSan too: a stand-in
  that reads on the enter and again at the drop gets the files; one that
  accepts nothing is left and refused after about 1 s; a drop on no window
  is refused at once; a drop naming no type is no line.
- A real GTK3 window (Python GObject, `drag_dest_add_uri_targets`) on the
  compositor received the dropped `file://` URI.
- Firefox ESR 153 read the drop twice, accepted the copy and finished
  (taken). It did not open the file in that test profile, which is its
  own handling, still to be seen on the owner's desktop.
- mpv 0.40 took the drop at the protocol level but did not open the file:
  every Wayland output of mpv needs `wp_viewporter`, which the compositor
  does not offer yet, so mpv falls back to its SDL output, which ignores
  drops. wp_viewporter is the next row.

## Amended 2026-11-17 (0.3.116): the drag ends once the data is there

On the owner's machine the first file dropped on mpv 0.40 was "taken" only
when the 5 s finish timeout passed, and the second drop ended mpv: SIGABRT on
its `data_device_handle_enter: Assertion '!s->dnd_offer->offer'`, reproduced
on the build host with the same mpv. mpv asks for the data at the drop and
looks at its pipe once, without waiting, before the desktop has written; its
loop then waits on the Wayland socket alone, so it reads only when another
event comes. None came until the next drop's enter, with the first offer
still held.

A drop read is now relayed: the desktop's `CLIP read` gets a pipe of the
compositor's, whose loop gathers what is written (at most 4 MiB) and passes
it on to the program's pipe, so the compositor knows when the program has
all it asked for. A read after the drop, all delivered, ends the drag with
`wl_data_device.leave` -- as wlroots and Mutter send after a drop; the offer
stays the program's to read, finish and destroy. That event is what wakes
mpv: it reads, finishes (`taken` at once) and plays the file. A drop that
ends otherwise (finish, timeout, a new drop) says its leave then, once.

`compositor_gate`'s drop part adds a stand-in that reads as mpv does
(`--drop-lazy`: one look at the drop, again only on an event; an enter while
an offer is held aborts it): two drops in a row, each taken at once and read.
0.3.115 fails it: taken after 5.00 s.

## Amended 2026-11-17 (0.3.117): drops onto X programs (XDND)

A drop onto an X program's window had been refused. The compositor is now
the drag's source by XDND (versions 3 to 5), from a window of its own; no
change to the desktop's side, which sends `CLIP drop` for any program
window and reads `CLIP dropped`.

- The target is the window's top-level, XdndAware (or its XdndProxy when
  the proxy names itself); not aware, refused at once, with the reason in
  the log.
- The drop's server time comes from a property change on the source
  window (its PropertyNotify), not CurrentTime: XdndSelection is owned at
  it and XdndPosition and XdndDrop carry it. No synchronous wait on the X
  server, which is itself one of the compositor's Wayland clients.
- XdndPosition (root coordinates: the window's position plus the drop
  point) is sent again only after the last one's XdndStatus, every 150 ms,
  until it accepts; 1 s with no acceptance is XdndLeave and `refused`.
- Its ConvertSelection of XdndSelection is answered by the X selection
  transfer of milestone D (INCR, the idle timeout), fed by the desktop's
  `CLIP read`; the keyboard rule of X pastes does not apply (the desktop
  holds the keyboard during its drag). Text aliases map to
  text/plain;charset=utf-8.
- XdndFinished is `taken` when the drop was read (and, at version 5, it says
  it accepted); the window destroyed or Xwayland gone mid-drop is
  `refused`, with nothing more sent.

Evidence: xwayland_gate's drop part (an XdndAware stand-in: enter at
version 5 with the types, position with a copy, drop, read, finish,
`taken`; a refusing one left and `refused` after 1.1 s; an unaware one
refused at once and said), plain and under ASan/UBSan, in the Debian 13
release environment; and a real GTK3 window under Xwayland
(`GDK_BACKEND=x11`, `drag_dest_add_uri_targets`) in a throwaway container
of that environment: two drops in a row, both taken, both lists received.

## Amended 2026-11-18 (0.3.118): hovering -- the program's answer before the drop

Agreed with Hibr (row 1 of Gitea #160). The desktop says its drag over a
program window as `CLIP drag <seq> <window> <x> <y> <type>+` (enter, then
motion; another window leaves the last), and `CLIP leave <seq>` when it
leaves program windows or ends; the compositor answers `CLIP hover <seq>
copy|none` when the program's answer changes, which the desktop shows on
its drag pointer. A Wayland program answers in several requests (accept,
then set_actions); the answer is said from an idle callback, once the batch
is in, so it never shows none-then-copy. An X program's is its XdndStatus;
a move while the last position is unanswered waits for that answer. A
`CLIP drop` over the hovered window drops there at once when the answer was
copy (as wlroots drops at the release when the source was accepted), with no
second enter. Hover has no clock: the desktop's lines drive it.

Row 3 (a program dragging out) is agreed too: `CLIP dragout o<n> <window>
<actions> <type>+`, the `o` keeping its sequence apart from the desktop's,
echoed in `CLIP drag|drop|read|dragend o<n>`.

Evidence: compositor_gate's drop part (hover copy over an accepting
stand-in, said once; moving says nothing new; the drop over it taken at
once with no second enter; none over a refusing one; leave with no answer;
one drag from window to window) with ASan/UBSan; xwayland_gate's drop part
(copy from XdndStatus, a move a new XdndPosition, the drop taken, none and
XdndLeave on leave) in the release environment; a real GTK3 window on the
compositor: hover copy once, motions quiet, the drop taken and the file
received.

## Amended 2026-11-18 (0.3.119): a program drags out

Row 3 of Gitea #160, as agreed with Hibr. A Wayland program's
`wl_data_device.start_drag`, held to the rule of moves (a press the
compositor delivered on that window, its serial), becomes `CLIP dragout
o<n> <window> <actions> <type>+`. The `o` keeps the program's drags apart
from the desktop's drop sequence on the wire (Hibr's catch: the two counts
can be equal). The desktop runs it: the compositor takes the program's
pointer focus away (a leave) and gives that window no pointer input until
`CLIP dragend`, as a compositor's drag grab does.
- Onto the desktop: the desktop reads `CLIP read o<n> <type>` from the
  program's source.
- Onto a program window (the same program's too): the desktop's `CLIP
  drag|drop o<n>` there run the target side of row 1, and the target's
  reads -- a Wayland target's receive, an X target's XdndSelection
  conversion -- are written by the source directly, not through the
  desktop.
- `CLIP dragend o<n> copy|move` sends the source `action`,
  `dnd_drop_performed` and `dnd_finished` (version 3); `none` sends
  `cancelled`. The source destroyed mid-drag is `CLIP clear o<n>`; the
  channel lost cancels.
- Not yet: a program's own drag icon (the desktop draws its own), X
  programs as drag sources, and moves that delete (the source decides on
  `move`, as GTK's drag-data-delete).

Evidence: compositor_gate's dragout part (a stand-in dragging out:
announced with copy,move and its types; its pointer left and no input
meanwhile; read by the desktop then `dragend move` performed/finished/move;
onto another program, hover and taken, the target fed by the source; `none`
cancelled) with ASan/UBSan; the wire unit test (o-seqs, dragend); a real
GTK3 drag source (drag_source_set, uri targets): its drag announced, the
desktop read its file URI, and `dragend copy` ended it in GTK as a
successful drag.

## Amended 2026-11-18 (0.3.120): a drag out only for a desktop that runs it

0.3.119 took the program's pointer from the start of its drag until the
desktop's `CLIP dragend`; every Hibr then released ignored `CLIP dragout`,
so a tab or text drag inside a Wayland program would leave its window
without the pointer. The desktop now says it runs drags with `drag=1` in its
hello answer (next to `window=1`); without it the compositor cancels the
drag at once, as before 0.3.119, and says so once. compositor_gate's
dragout part runs with drag=1; a second case without it shows the drag
cancelled and the window keeping its pointer -- 0.3.119 fails it (the drag
not cancelled). The mini PC was put back on 0.3.118 until this release.

## Amended 2026-11-19 (0.3.121): X programs drag out

Agreed with Hibr: no wire change, the same `CLIP dragout`. The compositor is
the X program's XDND target. It watches XdndSelection (XFixes); an X
program taking it while a button the compositor delivered is down on its
window arms a drop window: override-redirect, input-only, the root's size,
on top, XdndAware 5 -- the program's drag finds it wherever the pointer is
(its own windows' raising stays beneath). Nothing from it in 2 s, or no
position before its button comes up: disarmed, its drag X's own.
- Its XdndEnter (types, XdndTypeList past three) and first XdndPosition
  (action) become `CLIP dragout o<n>`; each position is answered
  XdndStatus not-yet-accepting, positions wanted. Its window then takes
  no pointer input; its releases are held.
- Reads: an X source in all but name (primary 2: x_sel_atom gives
  XdndSelection), converted at its latest position's time through the X
  paste transfer (INCR, the idle timeout). XDND lets the target read before
  the drop; GTK and the stand-in answer.
- `CLIP dragend copy|move`: XdndStatus accepting that action, one X round
  trip (the status and the Wayland release reach X by different roads --
  without it the release won and the program left), then the held release;
  its XdndDrop is answered XdndFinished with the action. `none`: XdndStatus
  refusing, the release; it leaves.
- Onto an X window: refused; being that window's source would take
  XdndSelection from the program dragging and end its drag.
- Its selection or window gone, another owner, Xwayland gone: `CLIP clear
  o<n>`.

Evidence: xwayland_gate's dragout part (an X stand-in that drags as a
toolkit does: owner at the press, a pointer grab, the XdndAware window under
the pointer): announced with copy and its types; the desktop's read served
from XdndSelection; the release held; dragend copy -> accepted, dropped,
finished copy; none -> refused, left; onto an X window refused, said) plain
and under ASan/UBSan in the release environment; a real GTK3 drag source
under Xwayland (GDK_BACKEND=x11) in a throwaway container: announced, read,
ended in GTK as a successful drag.

## Amended 2026-11-19 (0.3.122): X onto X, relayed

0.3.121 refused an X program's drag onto an X window: being that window's
XDND source would take XdndSelection from the program dragging and end its
drag. It is relayed instead. The compositor runs the target side of row 1
toward the X window as before, but with the drop window -- the one the
dragging program talks to -- as the source in every message (XdndEnter's,
XdndPosition's and XdndDrop's first word), the program's own types (its
XdndTypeList set on the drop window) and its latest position's time, and
without owning XdndSelection. The window's XdndStatus and XdndFinished come
to the drop window and are its `CLIP hover` and `CLIP dropped`; its
ConvertSelection of XdndSelection goes to the program that owns it, which
answers it directly. The dragging program then ends as any X drag out, at
`CLIP dragend`. Hover feedback now works over X windows for X drags too.

Evidence: xwayland_gate's dragout part, onto a second X stand-in that
accepts: hover copy, taken, the target got the source's URI with no read
asked of the desktop, the source finished copy; plain and ASan/UBSan. Its
drop part also drops on a window moved to a second display (root 1953,44
for 33,44 on HDMI-A-1 at 1920,0). A real GTK3 X drag source onto a real
GTK3 X drop target, in a throwaway container: hover copy, taken, the file
received, the source's drag ended as a success.

## Amended 2026-11-19 (0.3.123): the drag's own icon

Agreed with Hibr: `WINDOW dragicon <id> o<n> <dx> <dy>`, its frames as any
picture's, a repeat for a new offset, `WINDOW gone <id>` at the end; the
desktop draws it at the pointer plus (dx, dy), above everything, with its
copy/none badge, its own glyph until the first frame. Hibr asked for frames
at most 256 px a side, scaled here, where the buffer is: it cannot scale a
dmabuf or memfd frame without copying it, and a large thumbnail is a
megabyte a frame for every display.
- Wayland programs: start_drag's icon surface gets a picture of its own
  (id, holds, kept copy); its position is the protocol's -- 0,0 at the hot
  spot, moved by attach's dx/dy (version < 5) or offset.
- Every buffer kind (the owner: no self-imposed limits): shared memory,
  scaled by an area average past the bound; a dmabuf within the bound as
  FRM3, no copy; a linear dmabuf past it read between its fences and
  scaled; a tiled one past it, which no CPU can read, as FRM3 at its size --
  Blit's GPU draws a picture at the size it is placed.
- X programs: their icon is their own override-redirect window of type
  _NET_WM_WINDOW_TYPE_DND (GTK, Qt). Such a window is never a popup; during
  an X program's drag out it is the drag's icon, placed from the pointer
  where it was when the drag was announced, and asked to draw again (an
  Expose) so its next frame comes.

Evidence: compositor_gate's dragout part -- a 300x200 shm icon at -10,-5
as a 256x170 frame, a 64x48 dmabuf as FRM3, a 300x200 linear dmabuf read
and scaled to 256x170, each gone at the end -- with ASan/UBSan;
xwayland_gate's dragout part -- the X stand-in's DND window the icon, 64x48
at -8,-6, gone at the end -- plain and ASan/UBSan; real GTK3: its Wayland
drag icon (32x32 frames) and, under Xwayland in a throwaway container, its
X drag window (32x32 frames, at the pointer).
