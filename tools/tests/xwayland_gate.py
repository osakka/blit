#!/usr/bin/env python3
"""ADR 0228 phase 2, milestones A, C and D: X programs in Hibr windows, keys
and mouse, menus, dialogs and states, copy and paste, and focus, through Xwayland
with blit-compositor as its window manager.

    xwayland_gate.py <blit-compositor> <x_standin_client> [blit-server [wl_standin_client]]

The gate plays the desktop on a frame channel (compositor_gate's Desktop:
the version 5 hello answered with window=1 and no dmabuf, so Xwayland draws
by wl_shm) and Blit on the window's input socket, with blit-compositor's
Xwayland on (`Xwayland` on PATH; xwayland_gate.sh has checked it is there
and 23.1 or later) and the in-repo X stand-in client
(tools/tests/x_standin_client.c) on the compositor's DISPLAY. It checks, in
order:

  - `WINDOW display blit-0 :<n>`, Xwayland ready on :n, its sockets
    (/tmp/.X11-unix/X<n>, @/tmp/.X11-unix/X<n>) and /tmp/.X<n>-lock with
    the compositor's pid, and xwayland_shell_v1 hidden from a Wayland
    client that is not Xwayland;
  - the stand-in's window: `WINDOW new`, its title (_NET_WM_NAME) and app
    id (WM_CLASS's class), `decor server`, its `INPUT sock`, and a 320x200
    frame of its pattern, pixel for pixel;
  - `WINDOW size 1 400 300 activated`: the X client's ConfigureNotify at
    400x300, FocusIn, and a 400x300 frame of the pattern;
  - Blit's input on the window's socket (with blit-server's keymap, as
    Blit sends it, when a blit-server is given): a click at 10,12 reaching
    the X client as ButtonPress 1 at 10,12, a key (evdev 30) as KeyPress 38;
  - the space bar: 150 frames drawn, and the compositor's measure of its
    first 60 frames, commit to frame sent (printed);
  - Xwayland alive after its AccessX timeout has expired since the last
    key: run with `+accessx 1` (1 s instead of X's 120), the timeout updates
    every keyboard's indicators, which crashed Xwayland 24.1 when Blit's
    keymap had none (src/blit_xkb_keymap.h);
  - _MOTIF_WM_HINTS with no decorations (the D key): `WINDOW decor 1
    client`, and taken away, `WINDOW decor 1 server`;
  - milestone C (x_milestone_c): override-redirect menus as `WINDOW
    popup` at their X offset, a key and a choice through their grabs,
    dismissed by a press outside and by the window's deactivation (with
    `WINDOW close` for the popup noted; a menu its program moves is `WINDOW
    popup` again at its new offset); a menu's parent by
    WM_TRANSIENT_FOR where its origin is in another window, a submenu's by
    the menu under its origin where the pointer is in another window; a
    transient dialog `WINDOW new 4 1` with its WM_NORMAL_HINTS as `WINDOW
    limits`, and a press on window 1 under it in X space, with no move,
    reaching window 1; a base size as limits; _NET_WM_STATE full screen
    and maximize both ways, and maximized before the map; a parent set
    after the map; _NET_WM_MOVERESIZE move and resize with a press held,
    refused with none; WM_CHANGE_STATE as `WINDOW minimize`; a window
    asking for a place past the root kept inside it, a press at its far
    corner landing; named X cursors as `WINDOW cursor 1 xterm`, and
    `default` for a name Blit has no pointer for;
  - milestone D (x_milestone_d), the gate as Hibr's clipboard on the CLIP
    lines: an X program's CLIPBOARD and PRIMARY offered (`CLIP offer`) and
    read back byte for byte; the desktop's offers pasted by the X program
    byte for byte (its `CLIP read` answered); STRING both ways as Latin-1;
    INCR both ways (700001 bytes; to X also 0, 65535 and 65536); a secret
    offered with x-kde-passwordManagerHint both ways, in TARGETS, and gone
    from X when the desktop clears it; the owner changing mid-transfer in
    both directions (the transfer finishes with the old owner's bytes); an
    X requestor that never takes its INCR chunks, a desktop reader that
    never reads, and an X owner that never answers -- each ended at the
    compositor's deadline while it answers the desktop; the desktop
    silent before an X paste's answer (property None) and stalling
    mid-INCR (what came, then the end); two ownerships in quick
    succession by one window at one time making one offer, the newer's; a
    copy and a paste made after X's focus was taken away, the one not the
    clipboard's and the other refused; another X client than the focused
    one copying and pasting (its selection ending with it); with the Wayland
    stand-in (`--clip`), its CLIPBOARD and PRIMARY pasted by X and X's
    CLIPBOARD pasted by it; an X program giving its selection up is `CLIP
    clear`;
  - focus (x_focus_part): another X client taking X's focus itself, again
    at each loss, put back each time with one `WINDOW activate <id>
    x-focus` and a key still window 1's; the desktop's grant giving it the
    focus and the keys; a dialog's _NET_ACTIVE_WINDOW as `WINDOW activate
    <id> x-active`; a menu that takes X's focus inside its grab asking for
    nothing, and X's revert to the root when it goes put back;
  - the desktop's display layout (x_layout_part), after everything above
    ran without one as with a desktop older than 0.3.88: X's displays as
    the X stand-in reads them (B: RandR 1.5's monitors, the primary, the
    root, its windows' places) -- before, BLIT-1 at 0,0 and no primary;
    after `WINDOW primary` and two displays, a monitor per display at its
    place, named after it, the desktop's primary X's and changed by
    `WINDOW primary`, the root their extent; window 1 following its
    display; full screen at its display's origin; a toplevel kept inside
    its display (not only the root) with a press at its far corner
    landing; a display gone, its window to the primary;
  - `WINDOW close 1`: WM_DELETE_WINDOW, the client exits 0, `WINDOW gone 1`;
  - Xwayland killed while an X program owns PRIMARY and the desktop
    CLIPBOARD (x_restart_part): its offer cleared with its window, and
    after the restart the desktop's CLIPBOARD pasted by a new X client;
  - `WINDOW quit 0`: the compositor ends 0, Xwayland with it, the sockets
    and the lock removed.

`WINDOW cursor` lines may come at any point and are set aside (counted;
milestone C looks for its named one among them).
"""
import fcntl
import os
import random
import re
import select
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
import threading
import time
import urllib.request

sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
import compositor_gate as cg  # noqa: E402
import gate_env  # noqa: E402

ROOT = cg.ROOT
TOKEN = cg.TOKEN
BLIT_PICTURE = 1259388934
KEY_A, KEY_D, KEY_SPACE, BTN_LEFT = 30, 32, 57, 0x110
KEY_M, KEY_N, KEY_T, KEY_L, KEY_F, KEY_X, KEY_G, KEY_H, KEY_R, KEY_C = 50, 49, 20, 38, 33, 45, 34, 35, 19, 46
KEY_V = 47
KEY_O, KEY_S, KEY_I, KEY_P, KEY_J, KEY_U, KEY_ESC = 24, 31, 23, 25, 36, 22, 1


def fail(why):
    print('XWAYLAND_GATE_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


# compositor_gate's helpers (its Desktop among them) fail as this gate.
cg.fail = fail


def check(ok, why):
    if not ok:
        fail(why() if callable(why) else why)


wait_for = cg.wait_for


class Desk:
    """compositor_gate's Desktop, with `WINDOW cursor` lines set aside."""

    def __init__(self, listener):
        self.desk = cg.Desktop(listener)
        self.cursors = []

    def next(self, wait=10.0):
        while True:
            got = self.desk.next(wait)
            if got and got[0] == 'line' and got[1].startswith('WINDOW cursor '):
                self.cursors.append(got[1])
                continue
            return got

    def line(self, expected, wait=10.0):
        got = self.next(wait)
        check(got is not None, 'no line, expected %r' % expected)
        if got[0] != 'line':
            os.close(got[2] if got[0] == 'frame' else got[2][0])
        check(got[0] == 'line' and got[1] == expected, 'got %r, expected %r' % (got[:2], expected))

    def pattern_frame(self, width, height, wait=15.0):
        """Frames of window 1 until one is width x height and the stand-in's
        pattern pixel for pixel (Xwayland may show the window's background
        first): (frames before it, sequence)."""
        expected = cg.pattern(width, height)
        deadline = time.time() + wait
        before = 0
        last = 'none'
        while time.time() < deadline:
            got = self.next(max(0.1, deadline - time.time()))
            if got is None:
                break
            check(got[0] == 'frame', 'got %r while waiting for a %dx%d frame' % (got[:2], width, height))
            (image, sequence, w, h, fmt, stride, _x, _y, _rw, _rh), fd = got[1], got[2]
            try:
                check(image == 1, 'a frame for picture %d' % image)
                check(fmt == 24 and stride == w * 3, 'a frame of format %d stride %d' % (fmt, stride))
                if (w, h) == (width, height) and os.pread(fd, stride * h, 0) == expected:
                    return before, sequence
                last = '%dx%d %s' % (w, h, 'not the pattern' if (w, h) == (width, height) else '')
            finally:
                os.close(fd)
            before += 1
        fail('no %dx%d frame of the pattern within %.0f s (%d other frames, the last %s)'
             % (width, height, wait, before, last))

    def lines(self, wanted, wait=10.0, why=lambda: ''):
        """The lines `wanted`, in order, and no other line; frames between
        are closed and kept as (image, width, height) in the list returned."""
        frames = []
        deadline = time.time() + wait
        for want in wanted:
            while True:
                got = self.next(max(0.1, deadline - time.time()))
                check(got is not None, lambda: 'no %r (wanted %r):\n%s' % (want, wanted, why()))
                if got[0] == 'frame':
                    frames.append((got[1][0], got[1][2], got[1][3]))
                    os.close(got[2])
                    continue
                if got[0] != 'line':
                    os.close(got[2][0])
                    continue
                check(got[1] == want, lambda: 'got %r, expected %r:\n%s' % (got[1], want, why()))
                break
        return frames

    def frames_of(self, image, wait=10.0, why=lambda: ''):
        """Frames until one of picture `image`: its (width, height). A line
        meanwhile fails."""
        deadline = time.time() + wait
        while True:
            got = self.next(max(0.1, deadline - time.time()))
            check(got is not None, lambda: 'no frame of picture %d:\n%s' % (image, why()))
            check(got[0] == 'frame', lambda: 'got %r while waiting for a frame of %d:\n%s'
                  % (got[:2], image, why()))
            os.close(got[2])
            if got[1][0] == image:
                return got[1][2], got[1][3]

    def quiet(self, wait):
        """`wait` seconds with no line but cursor lines: frames closed."""
        deadline = time.time() + wait
        while time.time() < deadline:
            got = self.next(max(0.05, deadline - time.time()))
            if got is None:
                continue
            if got[0] == 'frame':
                os.close(got[2])
                continue
            fail('got %r, expected no line' % (got[:2],))

    def send(self, line):
        self.desk.send(line)

    @property
    def input_socks(self):
        return self.desk.input_socks


def wayland_globals(path):
    """The globals a plain Wayland client is shown at `path`: wl_display's
    get_registry and a sync, read off the wire."""
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.settimeout(5)
    sock.connect(path)
    sock.sendall(struct.pack('<III', 1, 12 << 16 | 1, 2) + struct.pack('<III', 1, 12 << 16 | 0, 3))
    data = b''
    names = []
    while True:
        chunk = sock.recv(65536)
        check(chunk, 'the compositor closed a registry probe')
        data += chunk
        while len(data) >= 8:
            obj, word = struct.unpack_from('<II', data)
            size, opcode = word >> 16, word & 0xffff
            if len(data) < size:
                break
            body, data = data[8:size], data[size:]
            if obj == 2 and opcode == 0:
                length = struct.unpack_from('<I', body, 4)[0]
                names.append(body[8:8 + length - 1].decode())
            elif obj == 3 and opcode == 0:
                sock.close()
                return names


def blit_keymap(server, work):
    """Blit's keymap and repeat, from a blit-server of the gate's own with
    one fabricated keyboard (as compositor_gate's input part), or None
    without a server."""
    if not server or not os.access(server, os.X_OK):
        return None, None
    nodes = os.path.join(work, 'nodes')
    os.mkdir(nodes)
    open(os.path.join(nodes, 'event1'), 'w').close()
    with open(os.path.join(nodes, 'event1.identity'), 'w') as out:
        out.write('bustype=3\nvendor=05ac\nproduct=0256\nversion=0100\nphys=usb-gate/event1\n'
                  'name=Keyboard A\nkinds=keyboard\ncapabilities=keys,repeat\nrepeat=250,33\n')
    env = gate_env.server_env(dict(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                                   BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'),
                                   BLIT_LOGIN_AUTOSTART='0', BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'),
                                   BLIT_EXPECT_DESKTOP='0', BLIT_TEST_MODE='1', BLIT_TEST_VT_SIGNALS='1',
                                   BLIT_INPUT_ROOT=nodes))
    blit = subprocess.Popen([server, str(cg.PORT)], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: cg.api('GET', '/v1/version'), 'the server did not start', 15)
        answer = urllib.request.urlopen(cg.BASE + '/v1/input/keymap', timeout=10)
        keymap = answer.read()
        repeat = (int(answer.headers['X-Blit-Repeat-Rate']), int(answer.headers['X-Blit-Repeat-Delay']))
        return keymap, repeat
    finally:
        blit.terminate()
        try:
            blit.wait(5)
        except subprocess.TimeoutExpired:
            blit.kill()
            blit.wait()


def x_milestone_c(desk, lines_to, printed, log):
    """Milestone C, on the stand-in's window 1 (400x300, activated, at 0,0
    in X space): its menus as popups, a dialog with its parent and limits,
    WM_NORMAL_HINTS, _NET_WM_STATE both ways, _NET_WM_MOVERESIZE,
    WM_CHANGE_STATE, a window kept inside the root, and named X cursors.
    Returns what it saw, for the summary line."""
    why = lambda: '%s\n%s' % (printed(), log())
    seen = [len(printed())]
    routes = []

    def said(text, wait=10):
        """The X client says `text` after what it said last time."""
        line = 'XSTANDIN %s\n' % text
        wait_for(lambda: line in printed()[seen[0]:], lambda: 'the X client did not say %r:\n%s'
                 % (text, why()), wait)
        seen[0] = printed().index(line, seen[0]) + len(line)

    def key(code):
        lines_to('key %%d %d press' % code, 'key %%d %d release' % code)

    def click(x, y):
        lines_to('ptr %%d %d.00 %d.00' % (x, y), 'btn %%d %d press' % BTN_LEFT, 'btn %%d %d release' % BTN_LEFT)

    def route(window):
        """Blit's input route to another window's socket: a function that
        sends it lines, as lines_to does window 1's."""
        # Its `INPUT sock` follows its announcement's hints.
        wait_for(lambda: (desk.quiet(0.1), window in desk.input_socks)[1],
                 lambda: 'no INPUT sock for window %d (%s)' % (window, desk.input_socks))
        sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        sock.connect(desk.input_socks[window])
        routes.append(sock)
        count = [0]

        def send(*lines):
            text = ''
            for line in lines:
                count[0] += 1
                text += 'INPUT %s\nINPUT frame %d %d\n' % (line % BLIT_PICTURE, BLIT_PICTURE, count[0])
            sock.sendall(text.encode())
        return send

    try:
        return x_milestone_c_steps(desk, lines_to, log, why, said, key, click, route)
    finally:
        for sock in routes:
            sock.close()


def x_milestone_c_steps(desk, lines_to, log, why, said, key, click, route):
    # A menu (M): override-redirect at 30,40 from window 1 in X space, which
    # grabs the pointer and the keyboard -- `WINDOW popup 2 1 30 40` and its
    # own frames. The desktop joins it to window 1's route (INPUT member), so
    # Blit sends the pointer over it in window 1's pixels: the compositor
    # gives it to the menu's surface, and Xwayland puts it at the menu's own
    # X position. A key reaches the menu by its grab; a press in it is a
    # choice at its own 10,12; the menu goes, and so does its picture.
    key(KEY_M)
    desk.lines(['WINDOW popup 2 1 30 40'], why=why)
    size = desk.frames_of(2, why=why)
    check(size == (120, 90), 'the menu\'s frame is %dx%d, not 120x90' % size)
    said('menu 30,40 120x90 grab 0 0')
    key(KEY_A)
    said('menu-key %d' % (KEY_A + 8))
    # The program moves its open menu (V, 20 right and 10 down): `WINDOW
    # popup 2` again at its new offset, and a press there is in it.
    key(KEY_V)
    said('menu-moved 50,50')
    desk.lines(['WINDOW popup 2 1 50 50'], why=why)
    click(50 + 10, 50 + 12)
    said('menu-choice 10,12')
    desk.lines(['WINDOW gone 2'], why=why)

    # Again (M), and a press on window 1 outside it: the grab gives the
    # press to the menu at its own -25,-35, and it dismisses itself.
    key(KEY_M)
    desk.lines(['WINDOW popup 3 1 30 40'], why=why)
    desk.frames_of(3, why=why)
    click(5, 5)
    said('menu-dismissed press -25,-35')
    desk.lines(['WINDOW gone 3'], why=why)

    # A dialog (T) at 300,250 from window 1, over it and past it:
    # WM_TRANSIENT_FOR window 1 and WM_NORMAL_HINTS minimum 150x80, maximum
    # 300x200 -- `WINDOW new 4 1` and `WINDOW limits`. X stacks it on top,
    # under Blit's pointer resting on window 1 at 310,260: the compositor
    # raises window 1 back over it (its ConfigureNotify), so a press there
    # with no move is window 1's.
    lines_to('ptr %d 310.00 260.00')
    key(KEY_T)
    desk.lines(['WINDOW new 4 1', 'WINDOW title 4 X stand-in dialog', 'WINDOW appid 4 blit.xstandin',
                'WINDOW limits 4 150 80 300 200', 'WINDOW decor 4 server'], why=why)
    said('dialog')
    said('configure 400x300')
    lines_to('btn %%d %d press' % BTN_LEFT, 'btn %%d %d release' % BTN_LEFT)
    said('button 1 310,260')

    # A menu (N) at 450,320: in the dialog and past window 1, with
    # WM_TRANSIENT_FOR window 1 -- its parent is 1 (by the origin it would
    # be the dialog). The desktop closes it when the window loses its focus
    # (Hibr's native_focus): `WINDOW close 5` (an X menu has no popup_done:
    # noted), then `WINDOW size` without `activated` -- X's focus leaves
    # the window, and the menu closes itself on its FocusOut.
    key(KEY_N)
    desk.lines(['WINDOW popup 5 1 450 320'], why=why)
    desk.frames_of(5, why=why)
    said('menu 450,320 100x60 grab 0 0')
    desk.send('WINDOW close 5')
    wait_for(lambda: 'popup 5: WINDOW close for an X menu' in log(), lambda: 'WINDOW close 5 not noted:\n' + why())
    desk.send('WINDOW size 1 400 300')
    said('menu-dismissed focus-out')
    desk.lines(['WINDOW gone 5'], why=why)
    desk.send('WINDOW size 1 400 300 activated')
    said('state focused')

    # A submenu: with Blit's pointer on the dialog (its own route), a menu
    # at 330,40 from window 1 (O) and a submenu of it (S) at 420,50, in the
    # menu and past window 1: the popup under its origin decides, window 1
    # (the pointer's window would be the dialog). Escape closes both.
    dialog = route(4)
    dialog('ptr %d 10.00 10.00')
    key(KEY_O)
    desk.lines(['WINDOW popup 6 1 330 40'], why=why)
    desk.frames_of(6, why=why)
    said('menu 330,40 120x90 grab 0 0')
    key(KEY_S)
    said('submenu')
    desk.lines(['WINDOW popup 7 1 420 50'], why=why)
    key(KEY_ESC)
    said('menu-dismissed escape')
    desk.lines(['WINDOW gone 7', 'WINDOW gone 6'], why=why)

    desk.send('WINDOW close 4')
    desk.lines(['WINDOW gone 4'], why=why)
    said('dialog-delete')

    # WM_NORMAL_HINTS changed on window 1 (L): a base size and no minimum is
    # a minimum of the base size.
    key(KEY_L)
    desk.lines(['WINDOW limits 1 200 150 0 0'], why=why)

    # _NET_WM_STATE (F, X): the program's toggles are `WINDOW fullscreen`
    # and `WINDOW maximize`; the desktop's `WINDOW size` states come back as
    # the property, and the next toggle is from them.
    key(KEY_F)
    desk.lines(['WINDOW fullscreen 1 on'], why=why)
    desk.send('WINDOW size 1 800 600 fullscreen activated')
    said('state fullscreen focused')
    key(KEY_F)
    desk.lines(['WINDOW fullscreen 1 off'], why=why)
    key(KEY_X)
    desk.lines(['WINDOW maximize 1 on'], why=why)
    desk.send('WINDOW size 1 1000 700 maximized activated')
    said('state maximized-vert maximized-horz focused')
    key(KEY_X)
    desk.lines(['WINDOW maximize 1 off'], why=why)
    desk.send('WINDOW size 1 400 300 activated')
    said('configure 400x300')

    # _NET_WM_MOVERESIZE (G, H): sent while the press that asked is held,
    # `WINDOW move 1` and `WINDOW resize 1 bottom-right`; with no press down
    # (R), ignored and logged.
    key(KEY_G)
    lines_to('ptr %d 100.00 50.00', 'btn %%d %d press' % BTN_LEFT)
    said('button 1 100,50')
    said('moveresize 8 1')
    desk.lines(['WINDOW move 1'], why=why)
    lines_to('btn %%d %d release' % BTN_LEFT)
    key(KEY_H)
    lines_to('ptr %d 395.00 295.00', 'btn %%d %d press' % BTN_LEFT)
    said('button 1 395,295')
    said('moveresize 4 1')
    desk.lines(['WINDOW resize 1 bottom-right'], why=why)
    lines_to('btn %%d %d release' % BTN_LEFT)
    ignored = log().count('window 1: a move, resize or menu without a press on it is ignored')
    key(KEY_R)
    wait_for(lambda: log().count('window 1: a move, resize or menu without a press on it is ignored') > ignored,
             lambda: 'a _NET_WM_MOVERESIZE with no press was not refused:\n' + why())
    desk.quiet(0.5)

    # WM_CHANGE_STATE IconicState (I): `WINDOW minimize 1`.
    key(KEY_I)
    said('iconify')
    desk.lines(['WINDOW minimize 1'], why=why)

    # A panel (P) asking for 1800,1000 with its 200x100, past the root's
    # 1920x1080 (X confines its pointer to the root): placed at 1720,980,
    # so a press at its far corner reaches it there. Maximized before its
    # map (_NET_WM_STATE): `WINDOW maximize 8 on` with its announcement.
    # Its WM_TRANSIENT_FOR set later (J): `WINDOW parent 8 1`.
    key(KEY_P)
    desk.lines(['WINDOW new 8 0', 'WINDOW title 8 X stand-in panel', 'WINDOW appid 8 blit.xstandin',
                'WINDOW decor 8 server', 'WINDOW maximize 8 on'], why=why)
    said('panel')
    check('X: window' in log() and 'placed at 1720,980, inside the root' in log(),
          lambda: 'the panel was not placed inside the root:\n' + why())
    panel = route(8)
    panel('ptr %d 190.00 90.00', 'btn %%d %d press' % BTN_LEFT, 'btn %%d %d release' % BTN_LEFT)
    said('panel-button 1 190,90')
    key(KEY_J)
    said('panel-transient')
    desk.lines(['WINDOW parent 8 1'], why=why)
    desk.send('WINDOW close 8')
    desk.lines(['WINDOW gone 8'], why=why)
    said('panel-delete')

    # Named X cursors (C, U): `WINDOW cursor 1 xterm`, the name as the
    # program gave it (XFixes), which Blit's own table draws; a name Blit
    # has no pointer for is `default`.
    for k, name, line in ((KEY_C, 'xterm', 'WINDOW cursor 1 xterm'),
                          (KEY_U, 'nosuchcursor', 'WINDOW cursor 1 default')):
        before = len(desk.cursors)
        key(k)
        said('cursor %s' % name)
        lines_to('ptr %d 20.00 20.00', 'ptr %d 21.00 20.00')
        wait_for(lambda: (desk.quiet(0.1), line in desk.cursors[before:])[1],
                 lambda: 'no %s (%s):\n%s' % (line, desk.cursors, why()))
    return ('menus=popup 2,3 at 30,40 (moved->50,50, choice 10,12, dismissed by press), popup 5 at 450,320 by '
            'WM_TRANSIENT_FOR (close noted, dismissed on focus-out), submenu 7 at 420,50 by its menu '
            'dialog=new 4 1 limits 150x80-300x200, press under it with no move->window 1 '
            'hints=base 200x150 state=fullscreen,maximize on/off both ways moveresize=move,bottom-right,'
            'no-press refused minimize=WM_CHANGE_STATE panel=1800,1000->1720,980 press 190,90 maximized-at-map '
            'parent-later cursor=xterm,unknown->default')


# ---- milestone D: the X selections ----

KEY_1, KEY_2, KEY_3, KEY_4, KEY_5, KEY_6, KEY_7, KEY_8, KEY_9, KEY_0, KEY_Q = 2, 3, 4, 5, 6, 7, 8, 9, 10, 11, 16
KEY_W, KEY_E, KEY_Y = 17, 18, 21
TEXT = 'text/plain;charset=utf-8'
SECRET = 'x-kde-passwordManagerHint'
IDLE_MS = 3000  # the compositor's BC_XSEL_IDLE_MS


def fnv64(data):
    h = 0xcbf29ce484222325
    for c in data:
        h = ((h ^ c) * 0x100000001b3) & 0xffffffffffffffff
    return h


def noise(n, seed):
    return random.Random(seed).randbytes(n)


class Clip:
    """The gate as Hibr's clipboard, and the X stand-in's files and lines."""

    def __init__(self, desk, printed, log, clip_dir):
        self.desk, self.printed, self.log, self.dir = desk, printed, log, clip_dir
        self.writers = []

    def why(self):
        return '%s\n%s' % (self.printed(), self.log())

    def mark(self):
        return len(self.printed())

    def saw(self, text, since, wait=10):
        """The X client said `text` since the mark."""
        line = 'XSTANDIN %s\n' % text
        wait_for(lambda: line in self.printed()[since:],
                 lambda: 'the X client did not say %r:\n%s' % (text, self.why()), wait)

    def put(self, name, data):
        with open(os.path.join(self.dir, name), 'wb') as f:
            f.write(data)

    def pasted(self):
        with open(os.path.join(self.dir, 'pasted'), 'rb') as f:
            return f.read()

    def match(self, pattern, wait=10):
        """The compositor's next line (frames closed) matches `pattern`."""
        deadline = time.time() + wait
        while True:
            got = self.desk.next(max(0.1, deadline - time.time()))
            check(got is not None, lambda: 'no line matching %r:\n%s' % (pattern, self.why()))
            if got[0] == 'frame':
                os.close(got[2])
                continue
            if got[0] == 'frm3':
                for fd in got[2]:
                    os.close(fd)
                continue
            m = re.fullmatch(pattern, got[1])
            check(m, lambda: 'got %r, expected %r:\n%s' % (got[1], pattern, self.why()))
            return m

    def offered(self, sel, secret=False):
        """An X program's selection offered to the desktop: its seq."""
        return int(self.match(r'CLIP offer (\d+) %s %s%s' % (sel, re.escape(TEXT), ' ' + SECRET if secret else ''))
                   .group(1))

    def read_all(self, r, wait=10, limit=None):
        out = bytearray()
        deadline = time.time() + wait
        while limit is None or len(out) < limit:
            left = deadline - time.time()
            check(left > 0 and select.select([r], [], [], left)[0],
                  lambda: 'a read stopped at %d bytes:\n%s' % (len(out), self.why()))
            chunk = os.read(r, 65536 if limit is None else min(65536, limit - len(out)))
            if not chunk:
                break
            out += chunk
        return bytes(out)

    def ask(self, seq, type_=TEXT):
        """`CLIP read` with a pipe: its read end."""
        r, w = os.pipe()
        socket.send_fds(self.desk.desk.sock, [b'CLIP read %d %s\n' % (seq, type_.encode())], [w])
        os.close(w)
        return r

    def read(self, seq, wait=10):
        r = self.ask(seq)
        try:
            return self.read_all(r, wait)
        finally:
            os.close(r)

    def answer(self, seq, data, between=None):
        """A program's paste of the desktop's offer `seq`: its `CLIP read`
        answered with `data` from a thread (a large one waits for the
        reader); `between` (an event pair) pauses it after 300000 bytes."""
        self.match(r'CLIP read %d %s' % (seq, re.escape(TEXT)))
        fd = self.desk.desk.clip_fds.pop(0)
        done = {'error': None}

        def write():
            try:
                view = memoryview(data)
                if between:
                    sent = 0
                    while sent < 300000:
                        sent += os.write(fd, view[sent:300000])
                    between[0].set()
                    between[1].wait(20)
                    view = view[300000:]
                while len(view):
                    view = view[os.write(fd, view):]
            except OSError as e:
                done['error'] = e
            finally:
                os.close(fd)
        thread = threading.Thread(target=write, daemon=True)
        thread.start()
        self.writers.append(thread)
        return thread, done

    def desk_offers(self, seq, sel, secret=False):
        """The desktop's offer: the window manager owns X's selection."""
        m = self.mark()
        self.desk.send('CLIP offer %d %s %s%s' % (seq, sel, TEXT, ' ' + SECRET if secret else ''))
        self.saw('owner %s other' % sel, m)

    def pastes(self, sel, target, data, since):
        """The X client pasted exactly `data`."""
        self.saw('pasted %s %s %d %016x%s' % (sel, target, len(data), fnv64(data),
                                              ' incr' if len(data) >= 65536 else ''), since, 20)
        check(self.pasted() == data, lambda: 'the X client\'s paste differs (%d bytes, wanted %d)'
              % (len(self.pasted()), len(data)))


def x_milestone_d(desk, lines_to, printed, log, clip_dir, wl_standin, env, work, standin, xenv):
    """Milestone D on the stand-in's window 1 (400x300, X's focus): the X
    selections onto CLIP both ways, CLIPBOARD and PRIMARY, Latin-1, INCR,
    a secret, owners changing mid-transfer, stalled readers and owners,
    and Wayland programs' copies and pastes. Returns its summary."""
    c = Clip(desk, printed, log, clip_dir)

    def key(code):
        lines_to('key %%d %d press' % code, 'key %%d %d release' % code)

    # X copies CLIPBOARD (1): `CLIP offer` of its text, read back byte for byte.
    small = 'X → Hibr: héllo wörld ✓'.encode()
    c.put('copy', small)
    m = c.mark()
    key(KEY_1)
    c.saw('copied clipboard %d' % len(small), m)
    seq = c.offered('clipboard')
    got = c.read(seq)
    check(got == small, lambda: 'the desktop read %r, not %r' % (got, small))
    check(c.read(seq, wait=10) == small, 'a second read differs')

    # The desktop offers CLIPBOARD: the window manager owns X's, and the X
    # client's paste (4) is a `CLIP read` the desktop answers.
    data = 'Hibr → X: ünïcode ✓ end'.encode()
    c.desk_offers(101, 'clipboard')
    m = c.mark()
    key(KEY_4)
    c.answer(101, data)
    c.pastes('clipboard', 'UTF8_STRING', data, m)

    # PRIMARY both ways (2, 5).
    middle = b'middle-click from X'
    c.put('copy-primary', middle)
    key(KEY_2)
    seq = c.offered('primary')
    check(c.read(seq) == middle, 'PRIMARY read back differs')
    c.desk_offers(102, 'primary')
    m = c.mark()
    key(KEY_5)
    c.answer(102, b'middle-click from Hibr')
    c.pastes('primary', 'UTF8_STRING', b'middle-click from Hibr', m)

    # Latin-1: STRING asked of the desktop's UTF-8 (6), '?' for what it has
    # not; an X program's STRING-only copy (Q) read as UTF-8.
    c.desk_offers(103, 'clipboard')
    m = c.mark()
    key(KEY_6)
    c.answer(103, 'héllo € wörld'.encode())
    c.pastes('clipboard', 'STRING', 'héllo ? wörld'.encode('latin-1'), m)
    c.put('copy-latin1', 'café crème'.encode('latin-1'))
    key(KEY_Q)
    seq = c.offered('clipboard')
    got = c.read(seq)
    check(got == 'café crème'.encode(), lambda: 'a STRING copy read as %r' % got)

    # Large, INCR both ways: an X program's 700001 bytes (its own 32 KiB
    # chunks) to the desktop; the desktop's to X empty, one byte under the
    # 64 KiB chunk, exactly it, and 700001 bytes.
    big = noise(700001, 1)
    c.put('copy', big)
    key(KEY_1)
    seq = c.offered('clipboard')
    got = c.read(seq, wait=20)
    check(got == big, lambda: 'an X program\'s 700001 bytes read as %d, differing' % len(got))
    sizes = []
    for n, offer in ((0, 104), (65535, 105), (65536, 106), (700001, 107)):
        data = noise(n, n)
        c.desk_offers(offer, 'clipboard')
        m = c.mark()
        key(KEY_4)
        c.answer(offer, data)
        c.pastes('clipboard', 'UTF8_STRING', data, m)
        sizes.append('%d%s' % (n, '(incr)' if n >= 65536 else ''))

    # A secret: an X program's (3) offered with the hint; the desktop's
    # listed in TARGETS with it (9), pasted, and gone from X when the
    # desktop clears it (the Vault's timer): no owner, nothing read.
    secret = b'hunter2 from X'
    c.put('copy', secret)
    key(KEY_3)
    seq = c.offered('clipboard', secret=True)
    check(c.read(seq) == secret, 'the X secret read back differs')
    c.desk_offers(110, 'clipboard', secret=True)
    m = c.mark()
    key(KEY_9)
    c.saw('targets clipboard TARGETS,TIMESTAMP,UTF8_STRING,%s,STRING,TEXT,%s' % (TEXT, SECRET), m)
    m = c.mark()
    key(KEY_4)
    c.answer(110, b'vault secret')
    c.pastes('clipboard', 'UTF8_STRING', b'vault secret', m)
    m = c.mark()
    desk.send('CLIP clear 110')
    c.saw('owner clipboard none', m)
    m = c.mark()
    key(KEY_4)
    c.saw('pasted clipboard UTF8_STRING none', m)
    desk.quiet(0.3)

    # The owner changes mid-transfer: the transfer is the old owner's to
    # finish. The desktop's 700001 bytes to X with a new desktop offer
    # after 300000 of them; then an X program's 500001 to the desktop, a
    # new offer after 200000 read.
    old = noise(700001, 7)
    c.desk_offers(111, 'clipboard')
    m = c.mark()
    key(KEY_4)
    pause = (threading.Event(), threading.Event())
    c.answer(111, old, between=pause)
    check(pause[0].wait(10), lambda: 'the first 300000 bytes were not taken:\n' + c.why())
    c.desk_offers(112, 'clipboard')
    pause[1].set()
    c.pastes('clipboard', 'UTF8_STRING', old, m)
    m = c.mark()
    key(KEY_4)
    c.answer(112, b'the newer offer')
    c.pastes('clipboard', 'UTF8_STRING', b'the newer offer', m)
    xold = noise(500001, 8)
    c.put('copy', xold)
    key(KEY_1)
    seq = c.offered('clipboard')
    r = c.ask(seq)
    try:
        first = c.read_all(r, limit=200000)
        m = c.mark()
        desk.send('CLIP offer 113 clipboard %s' % TEXT)
        c.saw('lost clipboard', m)
        got = first + c.read_all(r, wait=20)
    finally:
        os.close(r)
    check(got == xold, lambda: 'the X program\'s bytes read across its loss: %d, differing' % len(got))

    def responsive(why):
        """The compositor answers the desktop while a transfer stalls."""
        m = c.mark()
        t0 = time.time()
        desk.send('WINDOW size 1 401 300 activated')
        c.saw('configure 401x300', m)
        took = time.time() - t0
        check(took < IDLE_MS / 1000.0 - 0.5, '%s: the compositor took %.1f s to answer' % (why, took))
        m = c.mark()
        desk.send('WINDOW size 1 400 300 activated')
        c.saw('configure 400x300', m)
        return took

    # A requestor that never takes an INCR's chunks (7): the compositor
    # answers meanwhile, lets it go at the deadline, and closes the
    # desktop's pipe.
    count = log().count('the requestor took nothing for %d ms; let go' % IDLE_MS)
    c.desk_offers(114, 'clipboard')
    m = c.mark()
    key(KEY_7)
    writer, wrote = c.answer(114, noise(700001, 14))
    c.saw('pasted clipboard UTF8_STRING stalled', m)
    stall_a = responsive('an X requestor stalled')
    wait_for(lambda: log().count('the requestor took nothing for %d ms; let go' % IDLE_MS) > count,
             lambda: 'a stalled X requestor was not let go:\n' + c.why(), 10)
    writer.join(5)
    check(not writer.is_alive() and isinstance(wrote['error'], BrokenPipeError),
          lambda: 'the desktop\'s pipe to a stalled requestor stayed open (%r)' % (wrote['error'],))
    m = c.mark()
    key(KEY_4)
    c.answer(114, b'after the stall')
    c.pastes('clipboard', 'UTF8_STRING', b'after the stall', m)

    # A reader (the desktop) that never reads an X program's 700001 bytes:
    # let go at the deadline, the X program's INCR still finished.
    c.put('copy', noise(700001, 15))
    key(KEY_1)
    seq = c.offered('clipboard')
    count = log().count('the reader took nothing for %d ms; let go' % IDLE_MS)
    r = c.ask(seq)
    stall_b = responsive('a desktop reader stalled')
    wait_for(lambda: log().count('the reader took nothing for %d ms; let go' % IDLE_MS) > count,
             lambda: 'a stalled reader was not let go:\n' + c.why(), 10)
    os.close(r)

    # An X owner that answers TARGETS and never the data (0): the read ends
    # empty at the deadline.
    c.put('copy', b'never sent')
    key(KEY_0)
    seq = c.offered('clipboard')
    t0 = time.time()
    got = c.read(seq, wait=10)
    took = time.time() - t0
    check(got == b'' and took >= IDLE_MS / 1000.0 - 0.5 and 'gave nothing for %d ms; ended' % IDLE_MS in log(),
          lambda: 'a silent X owner: read %r after %.1f s:\n%s' % (got, took, c.why()))

    # The desktop's pipe for an X paste, held: never written (before the
    # answer, property None at the deadline), or 100000 bytes and then
    # nothing (mid-INCR: what came, then the end).
    def hold(seq, data):
        c.match(r'CLIP read %d %s' % (seq, re.escape(TEXT)))
        fd = desk.desk.clip_fds.pop(0)
        thread = threading.Thread(target=lambda: os.write(fd, data) if data else None, daemon=True)
        thread.start()
        return fd, thread
    count = log().count('its owner did not finish in %d ms; refused' % IDLE_MS)
    c.desk_offers(118, 'clipboard')
    m = c.mark()
    key(KEY_4)
    fd, _ = hold(118, b'')
    c.saw('pasted clipboard UTF8_STRING none', m)
    os.close(fd)
    check(log().count('its owner did not finish in %d ms; refused' % IDLE_MS) > count,
          lambda: 'a silent desktop was not refused at the deadline:\n' + c.why())
    count = log().count('its owner gave nothing for %d ms; ended short' % IDLE_MS)
    part = noise(100000, 19)
    c.desk_offers(119, 'clipboard')
    m = c.mark()
    key(KEY_4)
    fd, writer = hold(119, part)
    c.pastes('clipboard', 'UTF8_STRING', part, m)
    writer.join(5)
    os.close(fd)
    check(log().count('its owner gave nothing for %d ms; ended short' % IDLE_MS) > count,
          lambda: 'a stalled desktop was not ended short:\n' + c.why())

    # Two ownerships at once by one window at one time (W, secret and then
    # not): one offer, the newer's -- the older's TARGETS answer dropped.
    c.put('copy', b'twice')
    m = c.mark()
    key(KEY_W)
    c.saw('copied clipboard 5', m)
    seq = c.offered('clipboard')
    desk.quiet(0.5)
    check(c.read(seq) == b'twice', 'the newer of two quick ownerships read back differs')

    # The keyboard rule: a copy (E) and a paste (Y) the X program makes a
    # second after the desktop took X's focus from it -- its ownership is
    # not the clipboard's (no offer; logged), its read refused (None).
    ignored = "while no X window has the keyboard: not the clipboard's"
    refused = 'while no X window has the keyboard: refused'
    c.desk_offers(115, 'clipboard')
    count = log().count(ignored)
    m = c.mark()
    key(KEY_E)
    c.saw('key %d' % (KEY_E + 8), m)
    desk.send('WINDOW size 1 400 300')
    c.saw('focus-out', m)
    c.saw('copied clipboard 5', m)
    wait_for(lambda: log().count(ignored) > count, lambda: 'a copy without the keyboard was taken:\n' + c.why())
    desk.quiet(0.5)
    c.desk_offers(116, 'clipboard')
    m = c.mark()
    desk.send('WINDOW size 1 400 300 activated')
    c.saw('focus-in', m)
    count = log().count(refused)
    m = c.mark()
    key(KEY_Y)
    c.saw('key %d' % (KEY_Y + 8), m)
    desk.send('WINDOW size 1 400 300')
    c.saw('focus-out', m)
    c.saw('pasted clipboard UTF8_STRING none', m)
    check(log().count(refused) > count, lambda: 'a paste without the keyboard was not refused:\n' + c.why())
    desk.quiet(0.3)
    m = c.mark()
    desk.send('WINDOW size 1 400 300 activated')
    c.saw('focus-in', m)

    # Another X client than the one with X's focus, while window 1 has it
    # (as xclip run from an xterm, or Wine's clipboard thread): its copy is
    # the clipboard's, its paste answered; its selection ends with it.
    def other_client(name, mode):
        out_path = os.path.join(work, name)
        client = cg.start([standin, 'X other'], dict(xenv, XSTANDIN_CLIP_AT_START=mode), open(out_path, 'w'))
        oc = Clip(desk, lambda: open(out_path).read(), log, clip_dir)
        wid = int(oc.match(r'WINDOW new (\d+) 0', 20).group(1))
        for _ in range(3):
            oc.match(r'WINDOW (title|appid|decor) %d .*' % wid)
        return client, oc, wid
    c.put('copy', b'from another X client')
    other, oc, wid = other_client('xstandin-other.out', 'copy')
    try:
        seq = c.offered('clipboard')
        check(c.read(seq) == b'from another X client', 'another X client\'s copy read back differs')
        desk.send('WINDOW close %d' % wid)
        got = {c.match(r'CLIP clear %d|WINDOW gone %d' % (seq, wid)).group(0) for _ in range(2)}
        check(len(got) == 2, lambda: 'its selection did not end with it: %r' % got)
        check(other.wait(5) == 0, 'the other X client failed')
        c.desk_offers(117, 'clipboard')
        other, oc, wid = other_client('xstandin-other-2.out', 'paste')
        c.answer(117, b'to another X client')
        oc.pastes('clipboard', 'UTF8_STRING', b'to another X client', 0)
        desk.send('WINDOW close %d' % wid)
        c.match(r'WINDOW gone %d' % wid)
        check(other.wait(5) == 0, 'the second other X client failed')
    finally:
        if other.poll() is None:
            other.kill()
            other.wait()

    # Wayland programs: the stand-in with --clip copies (C, P) and X
    # pastes them (4, 5); X copies (1) and it pastes (V).
    cross = 'none (no wl_standin_client)'
    if wl_standin:
        out_path = os.path.join(work, 'wl-standin.out')
        client = cg.start([wl_standin, '--clip', 'Wayland clip'], dict(env, WAYLAND_DISPLAY='blit-0'),
                          open(out_path, 'w'))
        wl_printed = lambda: open(out_path).read()
        try:
            wid = int(c.match(r'WINDOW new (\d+) 0').group(1))
            for want in ('WINDOW title %d Wayland clip' % wid, 'WINDOW appid %d blit.standin' % wid,
                         'WINDOW limits %d 64 48 0 0' % wid, 'WINDOW decor %d server' % wid):
                c.match(re.escape(want))
            wait_for(lambda: (desk.quiet(0.1), wid in desk.input_socks)[1],
                     lambda: 'no INPUT sock for window %d' % wid)
            route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
            route.connect(desk.input_socks[wid])
            count = [0]

            def wl_key(code):
                text = ''
                for state in ('press', 'release'):
                    count[0] += 1
                    text += 'INPUT key %d %d %s\nINPUT frame %d %d\n' % (BLIT_PICTURE, code, state, BLIT_PICTURE,
                                                                           count[0])
                route.sendall(text.encode())
            try:
                m = c.mark()
                wl_key(46)  # C: copies the clipboard
                c.offered('clipboard')
                c.saw('owner clipboard other', m)
                m = c.mark()
                key(KEY_4)
                c.pastes('clipboard', 'UTF8_STRING', b'copied: ' + TEXT.encode(), m)
                m = c.mark()
                wl_key(25)  # P: copies the primary
                c.match(r'CLIP offer \d+ primary %s' % re.escape(TEXT))
                c.saw('owner primary other', m)
                m = c.mark()
                key(KEY_5)
                c.pastes('primary', 'UTF8_STRING', b'copied: UTF8_STRING', m)
                c.put('copy', b'from X to Wayland')
                key(KEY_1)
                xseq = c.offered('clipboard')
                wl_key(47)  # V: pastes the clipboard
                wait_for(lambda: 'STANDIN pasted clipboard from X to Wayland\n' in wl_printed(),
                         lambda: 'the Wayland program did not paste X\'s copy:\n%s\n%s' % (wl_printed(), c.why()))
            finally:
                route.close()
            # Its window closed: its primary source goes (`CLIP clear`).
            desk.send('WINDOW close %d' % wid)
            got = {c.match(r'CLIP clear \d+|WINDOW gone %d' % wid).group(0) for _ in range(2)}
            check(any(g.startswith('CLIP clear') for g in got), 'the Wayland primary was not cleared: %r' % got)
            check(client.wait(5) == 0, 'the Wayland clip stand-in failed:\n' + wl_printed())
        finally:
            if client.poll() is None:
                client.kill()
                client.wait()
        # X gives its selection up (8): `CLIP clear` of its offer.
        m = c.mark()
        key(KEY_8)
        c.saw('gave-up clipboard', m)
        c.match(r'CLIP clear %d' % xseq)
        cross = 'wl->X clipboard+primary, X->wl clipboard, gave-up->CLIP clear'
    for thread in c.writers:
        thread.join(5)
    return ('selections: X->desk clipboard,primary,latin1->utf8 desk->X utf8,primary,STRING->latin1 '
            'incr X->desk 700001, desk->X %s secret=hint both ways,TARGETS,cleared->none '
            'owner-change-mid-transfer=finished both ways stalls=requestor %.2f s,reader %.2f s answered,'
            'let go at %d ms; silent owner->empty; desktop silent->None, stalled mid-INCR->100000 then end '
            'two-quick-owners->one offer keyboard-rule=copy ignored,read refused with no X window focused; '
            'other X client=copy+paste+ends with it %s' % (','.join(sizes), stall_a, stall_b, IDLE_MS, cross))


# ---- focus: `WINDOW activate` (X's focus follows the desktop's) ----

KEY_K, KEY_Z = 37, 44


def x_focus_part(desk, lines_to, printed, log, standin, xenv, work):
    """X's focus follows the desktop's activated window, window 1: another X
    client that takes X's focus itself (three times, at each loss) is put
    back and asks once, `WINDOW activate <id> x-focus`, and a key reaches
    window 1; the desktop's grant gives it the focus and the keys; a
    dialog's _NET_ACTIVE_WINDOW is `WINDOW activate <id> x-active`; a menu
    that takes X's focus inside its own grab asks for nothing, and when it
    goes (X reverting the focus to the root) the focus is window 1's
    again. Returns its summary."""
    c = Clip(desk, printed, log, None)
    why = c.why

    def key(code):
        lines_to('key %%d %d press' % code, 'key %%d %d release' % code)

    def window_lines(pattern_new):
        wid = int(c.match(pattern_new, 20).group(1))
        return wid

    desk.send('WINDOW size 1 400 300 activated')
    m = c.mark()
    key(KEY_A)
    c.saw('key %d' % (KEY_A + 8), m)
    desk.quiet(0.3)

    # Another X program (Steam beside its game) takes X's focus a second
    # after its window shows, and again at each of its next two losses.
    thief_path = os.path.join(work, 'xstandin-thief.out')
    thief = cg.start([standin, 'X thief'], dict(xenv, XSTANDIN_FOCUS_AT_START='take'), open(thief_path, 'w'))
    tp = lambda: open(thief_path).read()
    try:
        m = c.mark()
        tid = window_lines(r'WINDOW new (\d+) 0')
        for _ in range(3):
            c.match(r'WINDOW (title|appid|decor) %d .*' % tid)
        c.match(r'WINDOW activate %d x-focus' % tid)
        wait_for(lambda: 'XSTANDIN took-focus 3\n' in tp(), lambda: 'the thief did not take the focus thrice:\n'
                 + tp() + why())
        # Window 1 lost the focus and has it back.
        c.saw('focus-out', m)
        wait_for(lambda: printed()[m:].rstrip().endswith('XSTANDIN focus-in'),
                 lambda: 'window 1 did not end with the focus:\n' + why())
        # Asked once: the second and third takes are within the quiet period.
        desk.quiet(1.2)
        check('asks for the focus again (x-focus) within 1000 ms: not said again' in log(),
              lambda: 'the repeated asks were not noted:\n' + log())
        # Put back three times (the thief takes again only at a loss, and
        # window 1 ended with the focus); logged once, the rest within
        # that second counted.
        check(log().count('its program took X\'s focus itself: put back') == 1,
              lambda: 'the putting back was not logged once:\n' + log())
        m = c.mark()
        key(KEY_A)
        c.saw('key %d' % (KEY_A + 8), m)
        check('XSTANDIN key' not in tp(), lambda: 'a key went to the thief:\n' + tp())

        # The desktop grants: the focus and the keys are the thief's.
        wait_for(lambda: (desk.quiet(0.1), tid in desk.input_socks)[1],
                 lambda: 'no INPUT sock for window %d' % tid)
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[tid])
        try:
            tmark = len(tp())
            m = c.mark()
            left = log().count('the focus left every window')
            desk.send('WINDOW size 1 400 300')
            desk.send('WINDOW size %d 320 200 activated' % tid)
            c.saw('focus-out', m)
            wait_for(lambda: 'XSTANDIN focus-in\n' in tp()[tmark:], lambda: 'the grant gave no focus:\n' + tp())
            route.sendall(('INPUT key %d %d press\nINPUT frame %d 1\nINPUT key %d %d release\nINPUT frame %d 2\n'
                           % (BLIT_PICTURE, KEY_A, BLIT_PICTURE, BLIT_PICTURE, KEY_A, BLIT_PICTURE)).encode())
            wait_for(lambda: 'XSTANDIN key %d\n' % (KEY_A + 8) in tp()[tmark:],
                     lambda: 'the key did not reach the granted window:\n' + tp())
            # The window manager's own focus changes are not put back.
            check(log().count('the focus left every window') == left,
                  lambda: 'the desktop\'s own activation was taken for a lost focus:\n' + log())
        finally:
            route.close()
        # And back to window 1: the thief, done fighting, lets it go.
        m = c.mark()
        tmark = len(tp())
        desk.send('WINDOW size %d 320 200' % tid)
        desk.send('WINDOW size 1 400 300 activated')
        c.saw('focus-in', m)
        wait_for(lambda: 'XSTANDIN focus-out\n' in tp()[tmark:], lambda: 'the thief kept the focus:\n' + tp())
        desk.quiet(0.5)
        check('took-focus 4' not in tp(), 'the thief took the focus a fourth time')

        # A dialog (T) asking to be active (K): `WINDOW activate <id> x-active`.
        m = c.mark()
        key(KEY_T)
        did = window_lines(r'WINDOW new (\d+) 1')
        for _ in range(4):
            c.match(r'WINDOW (title|appid|limits|decor) %d .*' % did)
        c.saw('dialog', m)
        key(KEY_K)
        c.saw('active-asked dialog', m)
        c.match(r'WINDOW activate %d x-active' % did)
        desk.send('WINDOW close %d' % did)
        c.match(r'WINDOW gone %d' % did)
        c.saw('dialog-delete', m)

        # A menu that takes X's focus inside its grab (Z): no ask, its keys
        # through the grab; when it goes X reverts the focus to the root,
        # and the window manager puts it back on window 1.
        menus = log().count('X menu 0x')
        left = log().count('the focus left every window')
        m = c.mark()
        key(KEY_Z)
        pid = int(c.match(r'WINDOW popup (\d+) 1 30 40').group(1))
        c.saw('menu 30,40 120x90 grab 0 0', m)
        c.saw('menu-focused', m)
        desk.quiet(0.5)
        check(log().count('X menu 0x') == menus, lambda: 'the menu\'s focus was taken for a steal:\n' + log())
        key(KEY_A)
        c.saw('menu-key %d' % (KEY_A + 8), m)
        m = c.mark()
        key(KEY_ESC)
        c.saw('menu-dismissed escape', m)
        c.match(r'WINDOW gone %d' % pid)
        c.saw('focus-in', m)
        check(log().count('the focus left every window') > left,
              lambda: 'the focus reverted to the root was not put back:\n' + log())
        m = c.mark()
        key(KEY_A)
        c.saw('key %d' % (KEY_A + 8), m)
        desk.quiet(0.3)

        desk.send('WINDOW close %d' % tid)
        c.match(r'WINDOW gone %d' % tid)
        check(thief.wait(5) == 0, 'the thief failed:\n' + tp())
    finally:
        if thief.poll() is None:
            thief.kill()
            thief.wait()
    return ('focus: thief=window %d took X focus x3->one WINDOW activate x-focus, put back each time, key->window 1; '
            'grant->focus+key to it; dialog %d _NET_ACTIVE_WINDOW->x-active; focused menu %d in its grab->no ask, '
            'reverted to root->put back' % (tid, did, pid))


def x_restart_part(desk, standin, env, clip_dir, log, work, xpid):
    """Xwayland killed while an X program owns PRIMARY and the desktop
    CLIPBOARD: its offer cleared with its window, and the next Xwayland's
    window manager owns CLIPBOARD again for the desktop. Returns the new
    Xwayland's pid."""
    def start(name):
        out_path = os.path.join(work, name)
        client = cg.start([standin], dict(env, XSTANDIN_CLIP_DIR=clip_dir), open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        c = Clip(desk, printed, log, clip_dir)
        wid = int(c.match(r'WINDOW new (\d+) 0', 20).group(1))
        for _ in range(3):
            c.match(r'WINDOW (title|appid|decor) %d .*' % wid)
        desk.send('WINDOW size %d 400 300 activated' % wid)
        wait_for(lambda: (desk.quiet(0.1), wid in desk.input_socks)[1], lambda: 'no INPUT sock for %d' % wid)
        wait_for(lambda: 'XSTANDIN focus-in' in printed(), lambda: 'no FocusIn:\n' + printed())
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[wid])
        count = [0]

        def key(code):
            text = ''
            for state in ('press', 'release'):
                count[0] += 1
                text += 'INPUT key %d %d %s\nINPUT frame %d %d\n' % (BLIT_PICTURE, code, state, BLIT_PICTURE,
                                                                       count[0])
            route.sendall(text.encode())
        return client, c, wid, route, key

    client, c, wid, route, key = start('xstandin-2.out')
    try:
        c.put('copy-primary', b'dies with Xwayland')
        key(KEY_2)
        pseq = c.offered('primary')
        c.desk_offers(120, 'clipboard')
        ready = log().count('X: Xwayland ready on')
        os.kill(xpid, signal.SIGKILL)
        got = {c.match(r'CLIP clear \d+|WINDOW gone %d' % wid, 15).group(0) for _ in range(2)}
        check(got == {'CLIP clear %d' % pseq, 'WINDOW gone %d' % wid},
              lambda: 'after Xwayland died: %r, wanted CLIP clear %d and WINDOW gone %d' % (got, pseq, wid))
        client.wait(10)
    finally:
        route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    wait_for(lambda: log().count('X: Xwayland ready on') > ready, lambda: 'Xwayland did not start again:\n' + log(),
             30)
    xpid = int(re.findall(r'X: Xwayland \S+ started on \S+, pid (\d+)', log())[-1])
    client, c, wid, route, key = start('xstandin-3.out')
    try:
        m = c.mark()
        key(KEY_4)
        c.answer(120, b'kept across Xwayland')
        c.pastes('clipboard', 'UTF8_STRING', b'kept across Xwayland', m)
        desk.send('WINDOW close %d' % wid)
        c.match(r'WINDOW gone %d' % wid)
        check(client.wait(5) == 0, 'the third X client failed')
    finally:
        route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return xpid, 'restart=X primary cleared+gone, desktop clipboard owned again and pasted'


# ---- the desktop's display layout: X space ----

KEY_B = 48


def x_layout_part(desk, lines_to, printed, log):
    """The desktop's display layout in X (docs/api.md, "Program windows"),
    after milestones C and D and focus ran without one, on window 1: X's
    displays (RandR 1.5's monitors, its primary, the root) before the
    layout, one output (BLIT-1 at 0,0, no primary); `WINDOW primary` and a
    set of two displays (one of unknown refresh) -- both monitors at their
    places, named after them, the primary X's, the root their extent, and
    window 1 following its display that went to the primary; `WINDOW
    output` moving it to the other; `WINDOW primary` changing X's; the
    program's own full screen at its display's origin and the desktop's
    size, then back inside its display; a panel asking for a place past its
    display (not past the root) kept inside it, sized past its edge kept
    inside again, a press at its far corner landing, and moved to the other
    display with it; the displays swapped in place, window 1 moving with
    its display; a display gone, its window moving to the primary.
    Returns its summary."""
    c = Clip(desk, printed, log, None)

    def key(code):
        lines_to('key %%d %d press' % code, 'key %%d %d release' % code)

    def displays(want, wait=10):
        """The B key until the X program sees `want` (Xwayland takes a new
        layout a moment after the compositor): its monitor, primary, root
        and where lines, in order."""
        deadline = time.time() + wait
        while True:
            m = c.mark()
            key(KEY_B)
            c.saw('displays-said', m)
            got = [l[len('XSTANDIN '):] for l in printed()[m:].splitlines()
                   if l.startswith(('XSTANDIN monitor ', 'XSTANDIN primary ', 'XSTANDIN root ', 'XSTANDIN where '))]
            if want is None or got == want:
                return got
            check(time.time() < deadline, lambda: 'X\'s displays are %r, not %r:\n%s' % (got, want, c.why()))
            time.sleep(0.2)

    def send_set(*displays_):
        for line in displays_:
            desk.send('WINDOW layout %s' % line)
        desk.send('WINDOW layout end')

    # Before any layout: the outputs of before it, X's root the largest.
    before = displays(None)
    main = [l for l in before if l.startswith('where main ')]
    check(before[:3] == ['monitor BLIT-1 0,0 1920x1080', 'primary none', 'root 1920x1080'] and len(main) == 1 and
          main[0].endswith(' 400x300'), lambda: 'X\'s displays before the layout: %r\n%s' % (before, c.why()))
    mx, my = (int(v) for v in main[0].split()[2].split(','))

    # The primary and a set of two displays (DP-1's refresh unknown, 0):
    # window 1 was on BLIT-1, which goes; it follows to the primary.
    desk.send('WINDOW primary HDMI-A-1')
    send_set('DP-1 0 0 1920 1080 0', 'HDMI-A-1 1920 0 2560 1440 144000')
    wait_for(lambda: 'X: RandR primary HDMI-A-1' in log(), lambda: 'X\'s primary was not set:\n' + c.why(), 15)
    displays(['monitor HDMI-A-1 1920,0 2560x1440 primary', 'monitor DP-1 0,0 1920x1080', 'primary HDMI-A-1',
              'root 4480x1440', 'where main %d,%d 400x300' % (mx + 1920, my)])
    check('window 1: X window 0x' in log() and 'on display HDMI-A-1 at %d,%d' % (mx + 1920, my) in log(),
          lambda: 'window 1 did not follow its display to HDMI-A-1:\n' + log())

    # The desktop shows it on DP-1 (by its mode; DP-1's refresh unknown).
    desk.send('WINDOW output 1 1920 1080 60000')
    displays(['monitor HDMI-A-1 1920,0 2560x1440 primary', 'monitor DP-1 0,0 1920x1080', 'primary HDMI-A-1',
              'root 4480x1440', 'where main %d,%d 400x300' % (mx, my)])
    # X's primary is the desktop's.
    desk.send('WINDOW primary DP-1')
    wait_for(lambda: 'X: RandR primary DP-1' in log(), lambda: 'X\'s primary did not change:\n' + c.why())
    displays(['monitor DP-1 0,0 1920x1080 primary', 'monitor HDMI-A-1 1920,0 2560x1440', 'primary DP-1',
              'root 4480x1440', 'where main %d,%d 400x300' % (mx, my)])

    # The program's full screen (F), which the desktop puts on HDMI-A-1:
    # its size, then the display (Hibr's order). At HDMI-A-1's origin at
    # its size; back from full screen, inside HDMI-A-1.
    m = c.mark()
    key(KEY_F)
    c.match(r'WINDOW fullscreen 1 on')
    desk.send('WINDOW size 1 2560 1440 fullscreen activated')
    desk.send('WINDOW output 1 2560 1440 144000')
    c.saw('configure 2560x1440', m)
    c.saw('state fullscreen focused', m)
    displays(['monitor DP-1 0,0 1920x1080 primary', 'monitor HDMI-A-1 1920,0 2560x1440', 'primary DP-1',
              'root 4480x1440', 'where main 1920,0 2560x1440'])
    m = c.mark()
    key(KEY_F)
    c.match(r'WINDOW fullscreen 1 off')
    desk.send('WINDOW size 1 400 300 activated')
    c.saw('configure 400x300', m)
    displays(['monitor DP-1 0,0 1920x1080 primary', 'monitor HDMI-A-1 1920,0 2560x1440', 'primary DP-1',
              'root 4480x1440', 'where main 1920,0 400x300'])

    # A panel (P) asking for 1800,1000 with its 200x100: past DP-1, where
    # its centre is, but not past the root (4480x1440) -- kept inside DP-1
    # at 1720,980. The desktop's 300x150 reaches past DP-1 again: 1620,930.
    # A press at its far corner lands; on HDMI-A-1 it keeps its place in
    # its display, and a press there lands too.
    m = c.mark()
    key(KEY_P)
    pid = int(c.match(r'WINDOW new (\d+) 0', 20).group(1))
    for _ in range(4):
        c.match(r'WINDOW (title|appid|decor|maximize) %d .*' % pid)
    c.saw('panel', m)
    check('placed at 1720,980, inside display DP-1 (it asked for 1800,1000)' in log(),
          lambda: 'the panel was not kept inside DP-1:\n' + log())
    desk.send('WINDOW size %d 300 150' % pid)
    displays(['monitor DP-1 0,0 1920x1080 primary', 'monitor HDMI-A-1 1920,0 2560x1440', 'primary DP-1',
              'root 4480x1440', 'where main 1920,0 400x300', 'where panel 1620,930 300x150'])
    wait_for(lambda: (desk.quiet(0.1), pid in desk.input_socks)[1],
             lambda: 'no INPUT sock for the panel (%s)' % desk.input_socks)
    sock = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    sock.connect(desk.input_socks[pid])
    try:
        count = [0]

        def press(x, y):
            text = ''
            for line in ('ptr %d %d.00 %d.00' % (BLIT_PICTURE, x, y), 'btn %d %d press' % (BLIT_PICTURE, BTN_LEFT),
                         'btn %d %d release' % (BLIT_PICTURE, BTN_LEFT)):
                count[0] += 1
                text += 'INPUT %s\nINPUT frame %d %d\n' % (line, BLIT_PICTURE, count[0])
            sock.sendall(text.encode())

        m = c.mark()
        press(290, 140)
        c.saw('panel-button 1 290,140', m)
        desk.send('WINDOW output %d 2560 1440 144000' % pid)
        displays(['monitor DP-1 0,0 1920x1080 primary', 'monitor HDMI-A-1 1920,0 2560x1440', 'primary DP-1',
                  'root 4480x1440', 'where main 1920,0 400x300', 'where panel 3540,930 300x150'])
        m = c.mark()
        press(290, 140)
        c.saw('panel-button 1 290,140', m)
    finally:
        sock.close()
    m = c.mark()
    desk.send('WINDOW close %d' % pid)
    c.match(r'WINDOW gone %d' % pid)
    c.saw('panel-delete', m)

    # The displays swapped in place: window 1 moves with HDMI-A-1, keeping
    # its place in it.
    send_set('DP-1 2560 0 1920 1080 0', 'HDMI-A-1 0 0 2560 1440 144000')
    displays(['monitor DP-1 2560,0 1920x1080 primary', 'monitor HDMI-A-1 0,0 2560x1440', 'primary DP-1',
              'root 4480x1440', 'where main 0,0 400x300'])
    # HDMI-A-1 gone: window 1, on it, goes to the primary, DP-1, at its
    # place there; X has one display.
    send_set('DP-1 0 0 1920 1080 0')
    displays(['monitor DP-1 0,0 1920x1080 primary', 'primary DP-1', 'root 1920x1080', 'where main 0,0 400x300'])
    return ('layout: before=BLIT-1@0,0,no primary set=DP-1@0,0(0 mHz)+HDMI-A-1@1920,0 primary=HDMI-A-1->DP-1 '
            'root=4480x1440 window 1 followed->%d,%d output->DP-1 fullscreen->HDMI-A-1 origin 2560x1440 '
            'panel %d 1800,1000->1720,980 inside DP-1, 300x150->1620,930, ->HDMI-A-1 3540,930, far corner presses '
            'swapped=window 1 with HDMI-A-1->0,0 gone=HDMI-A-1->window 1 to DP-1' % (mx + 1920, my, pid))


def x_part(compositor, standin, server, work, wl_standin=None):
    keymap, repeat = blit_keymap(server, work)
    runtime = os.path.join(work, 'run')
    os.mkdir(runtime, 0o700)
    name = 'blit-xwayland-gate-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    # Xwayland as the compositor runs it, with its AccessX timeout at one
    # second (X's default is 120), so the gate sees it expire.
    xwayland = os.path.join(work, 'Xwayland')
    with open(xwayland, 'w') as f:
        f.write('#!/bin/sh\nexec %s "$@" +accessx 1\n' % shutil.which('Xwayland'))
    os.chmod(xwayland, 0o755)
    env = gate_env.process_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN, XDG_RUNTIME_DIR=runtime,
                               BLIT_COMPOSITOR_XWAYLAND=xwayland)
    for k in ('WAYLAND_DISPLAY', 'DISPLAY', 'XAUTHORITY'):
        env.pop(k, None)
    log = os.path.join(work, 'compositor.log')
    comp = cg.start([compositor, '--x11', 'on', '--log', log], env, open(os.path.join(work, 'compositor.out'), 'w'))
    client = None
    xpid = None
    try:
        desk = Desk(listener)
        got = desk.next()
        check(got and got[0] == 'line', 'no first line: %r' % (got,))
        m = re.fullmatch(r'WINDOW display blit-0 :(\d+)', got[1])
        check(m, lambda: 'the first line is %r, not WINDOW display blit-0 :<n> (X off?):\n%s'
              % (got[1], open(log).read()))
        number = int(m.group(1))
        display = ':%d' % number
        wait_for(lambda: 'X: Xwayland ready on %s' % display in open(log).read(),
                 lambda: 'Xwayland did not get ready on %s:\n%s' % (display, open(log).read()), 30)
        text = open(log).read()
        xpid = int(re.search(r'X: Xwayland \S+ started on %s, pid (\d+)' % display, text).group(1))
        lock = '/tmp/.X%d-lock' % number
        check(os.path.exists('/tmp/.X11-unix/X%d' % number), 'no /tmp/.X11-unix/X%d' % number)
        check(open(lock).read().strip() == str(comp.pid), '%s does not hold the compositor\'s pid' % lock)
        with open('/proc/net/unix') as table:
            check(any(line.rstrip().endswith('@/tmp/.X11-unix/X%d' % number) for line in table),
                  'no abstract socket @/tmp/.X11-unix/X%d' % number)
        seen = wayland_globals(os.path.join(runtime, 'blit-0'))
        check('wl_compositor' in seen and 'xwayland_shell_v1' not in seen,
              'a plain Wayland client is shown %s' % seen)

        out_path = os.path.join(work, 'xstandin.out')
        clip_dir = os.path.join(work, 'clip')
        os.mkdir(clip_dir)
        cenv = dict(env, DISPLAY=display, XSTANDIN_CLIP_DIR=clip_dir)
        client = cg.start([standin], cenv, open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Blit X stand-in', 'WINDOW appid 1 blit.xstandin',
                     'WINDOW decor 1 server'):
            desk.line(want, 20)
        skipped, _ = desk.pattern_frame(320, 200)
        check(desk.input_socks.get(1) == os.path.join(runtime, 'blit-0-input-1'),
              'INPUT sock for window 1: %s' % desk.input_socks)
        check('XSTANDIN mapped' in printed(), lambda: 'the stand-in was not mapped:\n' + printed())

        desk.send('WINDOW size 1 400 300 activated')
        wait_for(lambda: 'XSTANDIN configure 400x300' in printed(),
                 lambda: 'the X client was not configured to 400x300:\n' + printed())
        wait_for(lambda: 'XSTANDIN focus-in' in printed(), lambda: 'the X client got no FocusIn:\n' + printed())
        desk.pattern_frame(400, 300)

        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        if keymap:
            memfd = os.memfd_create('gate-keymap', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
            os.write(memfd, keymap + b'\0')
            fcntl.fcntl(memfd, 1033, 0x0f)  # F_ADD_SEALS: seal, shrink, grow, write
            socket.send_fds(route, [b'INPUT keymap %d 7 %d %d %d\n' % (BLIT_PICTURE, len(keymap) + 1, repeat[0],
                                                                         repeat[1])], [memfd])
            os.close(memfd)
            wait_for(lambda: 'keymap: %d bytes from Blit' % len(keymap) in open(log).read(),
                     lambda: 'the compositor did not take Blit\'s keymap:\n' + open(log).read())
        frame = [0]

        def lines_to(*lines):
            text = ''
            for line in lines:
                frame[0] += 1
                text += 'INPUT %s\nINPUT frame %d %d\n' % (line % BLIT_PICTURE, BLIT_PICTURE, frame[0])
            route.sendall(text.encode())

        lines_to('ptr %d 10.00 12.00', 'btn %%d %d press' % BTN_LEFT, 'btn %%d %d release' % BTN_LEFT)
        wait_for(lambda: 'XSTANDIN button 1 10,12' in printed(),
                 lambda: 'the click did not reach the X client as button 1 at 10,12:\n' + printed())
        lines_to('key %%d %d press' % KEY_A, 'key %%d %d release' % KEY_A)
        wait_for(lambda: 'XSTANDIN key %d' % (KEY_A + 8) in printed(),
                 lambda: 'the key did not reach the X client as keycode %d:\n%s' % (KEY_A + 8, printed()))

        # 150 drawings (Xwayland commits at most one a refresh, so some
        # coalesce): the compositor's measure of the window's first 60 frames.
        lines_to('key %%d %d press' % KEY_SPACE, 'key %%d %d release' % KEY_SPACE)
        frames = 0
        deadline = time.time() + 20
        measure = None
        while time.time() < deadline and measure is None:
            got = desk.next(0.5)
            if got and got[0] == 'frame':
                os.close(got[2])
                frames += 1
            elif got:
                fail('got %r while the X client drew' % (got[:2],))
            measure = re.search(r'window 1: X commit to frame sent over its first 60 frames: '
                                r'min (\d+) us, median (\d+) us', open(log).read())
        check(measure, lambda: 'no measure of 60 frames (%d frames seen):\n%s\n%s'
              % (frames, printed(), open(log).read()))
        wait_for(lambda: 'XSTANDIN animated 150' in printed(), lambda: 'the X client did not draw 150:\n' + printed())

        # Twice the AccessX timeout since the space bar's release, the last
        # key: Xwayland has updated every keyboard's indicators, and lives.
        deadline = time.time() + 2.0
        while time.time() < deadline:
            got = desk.next(0.2)
            if got and got[0] == 'frame':
                os.close(got[2])
            elif got:
                check(not got[1].startswith('WINDOW gone'), lambda: 'the window went after the AccessX '
                      'timeout:\n%s' % open(log).read())
        text = open(log).read()
        check(os.path.exists('/proc/%d' % xpid) and 'X: Xwayland was killed' not in text and
              'X: Xwayland exited' not in text,
              lambda: 'Xwayland did not outlive its AccessX timeout (the keymap\'s indicators):\n' + text)

        # _MOTIF_WM_HINTS "no decorations" (the D key; Steam sets it): the
        # program draws its own frame, `WINDOW decor 1 client`; taken away,
        # the desktop's again, `WINDOW decor 1 server`.
        for printed_word, decor in (('undecorated', 'client'), ('decorated', 'server')):
            lines_to('key %%d %d press' % KEY_D, 'key %%d %d release' % KEY_D)
            deadline = time.time() + 10
            while True:
                got = desk.next(max(0.1, deadline - time.time()))
                check(got is not None, lambda: 'no WINDOW decor 1 %s after _MOTIF_WM_HINTS:\n%s\n%s'
                      % (decor, printed(), open(log).read()))
                if got[0] == 'frame':
                    os.close(got[2])
                    continue
                if got[1].startswith('WINDOW cursor'):
                    continue
                check(got[1] == 'WINDOW decor 1 %s' % decor, 'got %r, expected WINDOW decor 1 %s' % (got[1], decor))
                break
            # The compositor can see the property before the client's own
            # line is out: waited for, not checked at once.
            wait_for(lambda: 'XSTANDIN %s' % printed_word in printed(),
                     lambda: 'the X client did not say %s:\n%s' % (printed_word, printed()))

        milestone_c = x_milestone_c(desk, lines_to, printed, lambda: open(log).read())
        milestone_d = x_milestone_d(desk, lines_to, printed, lambda: open(log).read(), clip_dir, wl_standin,
                                    env, work, standin, cenv)
        focus = x_focus_part(desk, lines_to, printed, lambda: open(log).read(), standin, cenv, work)
        layout = x_layout_part(desk, lines_to, printed, lambda: open(log).read())

        desk.send('WINDOW close 1')
        deadline = time.time() + 10
        while True:
            got = desk.next(max(0.1, deadline - time.time()))
            check(got is not None, lambda: 'no WINDOW gone 1 after WINDOW close 1:\n' + printed())
            if got[0] == 'frame':
                os.close(got[2])
                continue
            check(got[1] == 'WINDOW gone 1', 'got %r, expected WINDOW gone 1' % (got[1],))
            break
        try:
            status = client.wait(5)
        except subprocess.TimeoutExpired:
            fail('the X client did not exit after WM_DELETE_WINDOW:\n' + printed())
        check(status == 0 and 'XSTANDIN delete' in printed(),
              lambda: 'the X client exited %d:\n%s' % (status, printed()))
        check('window 1: WM_DELETE_WINDOW' in open(log).read(), 'the log does not say WM_DELETE_WINDOW')
        route.close()
        xpid, restart = x_restart_part(desk, standin, cenv, clip_dir, lambda: open(log).read(), work, xpid)

        desk.send('WINDOW quit 0')
        try:
            status = comp.wait(10)
        except subprocess.TimeoutExpired:
            fail('blit-compositor kept running after WINDOW quit 0')
        check(status == 0, lambda: 'blit-compositor ended %d:\n%s' % (status, open(log).read()))
        check(not os.path.exists('/proc/%d' % xpid) or
              open('/proc/%d/stat' % xpid).read().split(') ')[1][0] == 'Z',
              'Xwayland (pid %d) outlived the compositor' % xpid)
        check(not os.path.exists('/tmp/.X11-unix/X%d' % number) and not os.path.exists(lock),
              'the X socket or lock outlived the compositor')
        return ('display=%s xpid=%d shell=hidden new+title+appid+decor+INPUT-sock frames=320x200(after %d),'
                '400x300 configure+focus-in click=1@10,12 key=%d keymap=%s measure=min %s us median %s us '
                '(%d frames) accessx=1s->Xwayland-lived motif=decor-client,server %s %s '
                '%s %s close=WM_DELETE_WINDOW->exit 0->gone %s quit=Xwayland ended,socket+lock removed '
                'cursor-lines=%d' % (display, xpid, skipped, KEY_A + 8, '%dB' % len(keymap) if keymap else 'none',
                                     measure.group(1), measure.group(2), frames, milestone_c, milestone_d, focus,
                                     layout, restart,
                                     len(desk.cursors)))
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.send_signal(signal.SIGTERM)
                try:
                    p.wait(5)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        listener.close()


def main():
    compositor, standin = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    server = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
    wl_standin = os.path.abspath(sys.argv[4]) if len(sys.argv) > 4 and sys.argv[4] else None
    work = tempfile.mkdtemp(prefix='blit-xwayland-gate-')
    try:
        result = x_part(compositor, standin, server, work, wl_standin)
        print('XWAYLAND_X_OK %s' % result)
        m = re.search(r'measure=min (\d+) us median (\d+) us', result)
        print('XWAYLAND_MEASURE commit-to-frame-sent first 60 frames: min %s us, median %s us'
              % (m.group(1), m.group(2)))
    except SystemExit:
        for name in sorted(os.listdir(work)):
            if name.endswith(('.log', '.out')):
                print('--- %s\n%s' % (name, open(os.path.join(work, name)).read()), file=sys.stderr)
        raise
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
