# Programs in windows

What to expect when you run a graphical program -- Firefox, a GTK or Qt
application, a Vulkan demo -- on the Hibr desktop. It opens as a Hibr
window through `blit-compositor`, Blit's own Wayland compositor
([ADR 0228](adr/0228-blit-is-the-compositor-for-programs.md)). Who owns
what between the program, the desktop and Blit is
[ADR 0229](adr/0229-programs-and-the-desktop-one-owner-per-concern.md); the
wire is [the API guide's "Program windows"](api.md). This page is the
promise: if what you see differs from it, that is a bug.

## Before you start

- **Install** `blit-compositor` (`sudo apt install blit-compositor`; Hibr
  suggests it). Hibr starts it with the desktop when **Control Panel >
  Windows > Programs in Windows** is on.
- **Use a terminal opened after that.** Hibr gives every terminal it opens
  from then on `WAYLAND_DISPLAY` (`blit-0`), and `DISPLAY` (`:0` or the
  next free one) when Xwayland is installed (below). A terminal opened
  before the switch was turned on does not have them, and cannot be given
  them (a shell's environment is its own): a program run there fails with
  "cannot open display". Open a new terminal.

## Running a program

- Type its name (`firefox`, `vkcube --wsi wayland`, `gnome-calculator`). A
  new Hibr window opens, titled by the program, placed and sized by the
  desktop. The first frame can show at the program's own size for an
  instant before the window's size reaches it.
- The terminal stays the program's parent, as for any command: its warnings
  print there, Ctrl-C ends it, and closing the terminal ends the program.
  Run it as `firefox &` (or from Hibr's menus) to keep the terminal free.

## X11 programs

*Since Blit 0.3.78 with Hibr 0.99.486 (ADR 0228 phase 2, milestone A).*
Programs that speak only X11 (xterm,
older applications; Steam is milestone B) run under Xwayland, which
`blit-compositor` starts with itself and manages: install `xwayland`
(`blit-compositor` recommends it; 23.1 or later, as Debian 13 has).
Without it, or with an older one, X11 programs fail with "cannot open
display" and the compositor's log says why.

An X program's window is a Hibr window like any other: its title and
application name, placed and sized by the desktop, keys and mouse, and
Hibr's close asks it to close. A program that draws its own frame (Steam)
says so, and Hibr's frame stays off, as for Firefox.

*From the next Blit (milestone C):*

- **Menus, dropdowns and tooltips** show over their window, where the
  program puts them, and take the keys and the mouse as the program
  expects: choose with a click or the keys, and a click elsewhere in the
  program's windows closes the menu. Switching to another window closes
  it in programs that close their menus when they lose the focus (Steam,
  Chromium and Qt programs do). A click on the desktop or in a Wayland
  program does not reach X, so a program that waits for a click of its
  own (some older toolkits) keeps its menu open until you come back to it.
- **Dialogs** belong to their window, as a Wayland program's do, and a
  window's minimum and maximum size hold when you resize it.
- **The program's own buttons**: full screen, maximize, minimize, and
  moving and resizing from its own title bar and edges (Steam's) work as
  for a Wayland program.
- **Pointer shapes**: the program's named cursors (text, hand, resize,
  ...) are Blit's own pointer for that name, as for a Wayland program; a
  cursor the program draws without a name is the display's pointer.

Copy and paste with X programs is milestone D.

## Using the window

| What | What happens |
| --- | --- |
| Keyboard | Goes to the focused window, with Blit's keyboard layout and key repeat (the keyboard settings in Blit's Control Panel pane). |
| Mouse | Clicks, scrolling, menus and popups land where they are drawn; a press on a background window focuses it first. A game can lock the pointer after a click; Hibr's release key gives it back. |
| Pointer shape | The program names it (text, hand, resize, ...) and Blit draws its own pointer for that name: the same cursors as everywhere else. A program's own cursor pictures are never drawn. Shapes show over the focused window; the text cursor shows everywhere. |
| Copy and paste | One clipboard with Hibr's terminals and applications, as text. Vault passwords paste in and are cleared again when the Vault's timer ends, and never enter the clipboard history. Middle-click paste (the primary selection) works between programs. |
| Moving and resizing | From Hibr's frame, or from the program's own title bar and edges if it draws them (Firefox, GTK applications); right click on such a title bar for the window menu. Maximize, full screen and minimize as for any Hibr window. |
| Closing | Hibr's close asks the program to close, so it can still ask to save. |
| Restart Desktop | Programs keep running; their windows come back as they were. |
| Quitting the desktop | Ends every program. |

## Not yet

- **X11 programs**: the X clipboard, Big Picture full screen and Proton
  games -- ADR 0228 phase 2's milestones D and E (above). A menu its
  program moves while it is open stays where it first showed; a program
  whose only window is an override-redirect one (some games' full-screen
  windows) does not show.
- **Programs' menus in Hibr's bar** (the global menu): Blit's side is in
  0.3.77, Hibr's to follow. Qt and KDE programs, and GTK 3 ones with the
  appmenu module, get it; Firefox and GTK 4 programs keep their own menus.
- **Drag and drop, HiDPI scaling, input methods, touch and tablets** are
  ADR 0229's later rows.

## When something is wrong

- **No window at all:** check `echo $WAYLAND_DISPLAY` (and `echo $DISPLAY`
  for an X program) in that terminal (see above) and that
  `blit-compositor` is running; for an X program, that `Xwayland -version`
  says 23.1 or later.
- **A window, but no keys or clicks:** this was a real bug for programs
  that draw their own frame in shared memory (Firefox), fixed in Hibr
  0.99.484. Note the program and what you did, and report it.
- The compositor logs to `~/.local/state/hibr/compositor.log`; Blit's
  input routes are in `GET /v1/terminal-sessions` under `frames.input`.

## How this is kept true

Each release is checked against this page with real programs on the
owner's machine -- vkcube, Firefox, a GTK and a Qt application: open, type,
click, copy and paste both ways, move and resize from both frames,
minimize and raise, Restart Desktop -- besides `compositor_gate`, which
checks every line of the wire with a stand-in program, and
`xwayland_gate`, which runs an X stand-in program under the compositor's
Xwayland (map, title and class, a drawing pixel for pixel, a resize,
focus, a key and a click, its menus with their grabs, a dialog and its
limits, full screen and maximize both ways, its own move and resize, a
named cursor, close).
