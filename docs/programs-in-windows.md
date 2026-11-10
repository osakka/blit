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
  from then on `WAYLAND_DISPLAY` (`blit-0`). A terminal opened before the
  switch was turned on does not have it, and cannot be given it (a shell's
  environment is its own): a program run there falls back to X11 and fails
  with "cannot open display". Open a new terminal.

## Running a program

- Type its name (`firefox`, `vkcube --wsi wayland`, `gnome-calculator`). A
  new Hibr window opens, titled by the program, placed and sized by the
  desktop. The first frame can show at the program's own size for an
  instant before the window's size reaches it.
- The terminal stays the program's parent, as for any command: its warnings
  print there, Ctrl-C ends it, and closing the terminal ends the program.
  Run it as `firefox &` (or from Hibr's menus) to keep the terminal free.

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

- **X11 programs** (Steam, older applications) do not open: they need
  Xwayland, ADR 0228's phase 2. They fail with "cannot open display".
- **Programs' menus in Hibr's bar** (the global menu) are being built; Qt
  and KDE programs, and GTK 3 ones with the appmenu module, will get it.
  Firefox and GTK 4 programs keep their own menus.
- **Drag and drop, HiDPI scaling, input methods, touch and tablets** are
  ADR 0229's later rows.

## When something is wrong

- **No window at all:** check `echo $WAYLAND_DISPLAY` in that terminal (see
  above) and that `blit-compositor` is running.
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
checks every line of the wire with a stand-in program.
