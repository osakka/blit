#!/usr/bin/env python3
"""ADR 0228 phase 2, milestone A: one X program in a Hibr window, keys and
mouse, through Xwayland with blit-compositor as its window manager.

    xwayland_gate.py <blit-compositor> <x_standin_client> [blit-server]

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
  - `WINDOW close 1`: WM_DELETE_WINDOW, the client exits 0, `WINDOW gone 1`;
  - `WINDOW quit 0`: the compositor ends 0, Xwayland with it, the sockets
    and the lock removed.

`WINDOW cursor` lines (Xwayland's cursor image, the display's pointer until
milestone C) may come at any point and are only counted.
"""
import fcntl
import os
import re
import shutil
import signal
import socket
import struct
import subprocess
import sys
import tempfile
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
    WM_NORMAL_HINTS, _NET_WM_STATE both ways, _NET_WM_MOVERESIZE and a
    named X cursor. Returns what it saw, for the summary line."""
    why = lambda: '%s\n%s' % (printed(), log())

    def key(code):
        lines_to('key %%d %d press' % code, 'key %%d %d release' % code)

    seen = [len(printed())]

    def said(text, wait=10):
        """The X client says `text` after what it said last time."""
        line = 'XSTANDIN %s\n' % text
        wait_for(lambda: line in printed()[seen[0]:], lambda: 'the X client did not say %r:\n%s'
                 % (text, why()), wait)
        seen[0] = printed().index(line, seen[0]) + len(line)

    def click(x, y):
        lines_to('ptr %%d %d.00 %d.00' % (x, y), 'btn %%d %d press' % BTN_LEFT, 'btn %%d %d release' % BTN_LEFT)

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
    click(30 + 10, 40 + 12)
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

    # A menu past window 1's right edge (N), its WM_TRANSIENT_FOR window 1:
    # `WINDOW popup 4 1 410 10`. The desktop closes it when the window loses
    # its focus (Hibr's native_focus): `WINDOW close 4` (an X menu has no
    # popup_done: noted), then `WINDOW size` without `activated` -- X's
    # focus leaves the window, and the menu closes itself on its FocusOut.
    key(KEY_N)
    desk.lines(['WINDOW popup 4 1 410 10'], why=why)
    desk.frames_of(4, why=why)
    said('menu 410,10 100x60 grab 0 0')
    desk.send('WINDOW close 4')
    wait_for(lambda: 'popup 4: WINDOW close for an X menu' in log(), lambda: 'WINDOW close 4 not noted:\n' + why())
    desk.send('WINDOW size 1 400 300')
    said('menu-dismissed focus-out')
    desk.lines(['WINDOW gone 4'], why=why)
    desk.send('WINDOW size 1 400 300 activated')
    said('state focused')

    # A dialog (T): WM_TRANSIENT_FOR window 1, WM_NORMAL_HINTS minimum
    # 150x80 and maximum 300x200 -- `WINDOW new 5 1` and `WINDOW limits`;
    # `WINDOW close 5` is its WM_DELETE_WINDOW.
    key(KEY_T)
    desk.lines(['WINDOW new 5 1', 'WINDOW title 5 X stand-in dialog', 'WINDOW appid 5 blit.xstandin',
                'WINDOW limits 5 150 80 300 200', 'WINDOW decor 5 server'], why=why)
    said('dialog')
    # Window 1 and the dialog overlap in X space (the dialog at 50,50): the
    # pointer over window 1 there raises it in X's stacking (its
    # ConfigureNotify), so a press there is window 1's, as Blit's pointer
    # says. (The raise and an event in the same instant race inside
    # Xwayland, which takes input before requests; a person's press comes
    # after the pointer got there.)
    lines_to('ptr %d 60.00 60.00')
    said('configure 400x300')
    click(60, 60)
    said('button 1 60,60')
    desk.send('WINDOW close 5')
    desk.lines(['WINDOW gone 5'], why=why)
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
    said('moveresize 8 1')
    desk.lines(['WINDOW move 1'], why=why)
    lines_to('btn %%d %d release' % BTN_LEFT)
    key(KEY_H)
    lines_to('ptr %d 395.00 295.00', 'btn %%d %d press' % BTN_LEFT)
    said('moveresize 4 1')
    desk.lines(['WINDOW resize 1 bottom-right'], why=why)
    lines_to('btn %%d %d release' % BTN_LEFT)
    ignored = log().count('window 1: a move, resize or menu without a press on it is ignored')
    key(KEY_R)
    wait_for(lambda: log().count('window 1: a move, resize or menu without a press on it is ignored') > ignored,
             lambda: 'a _NET_WM_MOVERESIZE with no press was not refused:\n' + why())
    desk.quiet(0.5)

    # A named X cursor (C): `WINDOW cursor 1 xterm`, the name as the program
    # gave it (XFixes), which Blit's own table draws.
    key(KEY_C)
    said('cursor xterm')
    lines_to('ptr %d 20.00 20.00', 'ptr %d 21.00 20.00')
    wait_for(lambda: (desk.quiet(0.1), 'WINDOW cursor 1 xterm' in desk.cursors)[1],
             lambda: 'no WINDOW cursor 1 xterm (%s):\n%s' % (desk.cursors, why()))
    return ('menus=popup 2,3 at 30,40 (choice 10,12, dismissed by press), popup 4 at 410,10 transient '
            '(close noted, dismissed on focus-out) dialog=new 5 1 limits 150x80-300x200 hints=base 200x150 '
            'state=fullscreen,maximize on/off both ways moveresize=move,bottom-right,no-press refused '
            'cursor=xterm')


def x_part(compositor, standin, server, work):
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
    comp = cg.start([compositor, '--log', log], env, open(os.path.join(work, 'compositor.out'), 'w'))
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
        cenv = dict(env, DISPLAY=display)
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
                '(%d frames) accessx=1s->Xwayland-lived motif=decor-client,server %s '
                'close=WM_DELETE_WINDOW->exit 0->gone quit=Xwayland ended,socket+lock removed '
                'cursor-lines=%d' % (display, xpid, skipped, KEY_A + 8, '%dB' % len(keymap) if keymap else 'none',
                                     measure.group(1), measure.group(2), frames, milestone_c, len(desk.cursors)))
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
    work = tempfile.mkdtemp(prefix='blit-xwayland-gate-')
    try:
        result = x_part(compositor, standin, server, work)
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
