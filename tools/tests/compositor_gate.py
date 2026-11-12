#!/usr/bin/env python3
"""ADR 0228 phase 1, milestone A: blit-compositor and the WINDOW wire.

    compositor_gate.py <blit-compositor> <wl_standin_client> [blit-server]

Part one plays the desktop: a fake frame-channel peer that answers the
version 5 hello with window=1, reads WINDOW lines and FRM1 frames, and sends
`WINDOW size` and `WINDOW close`, while blit-compositor serves the in-repo
stand-in client (tools/tests/wl_standin_client.c). It checks the draft in
docs/api.md, "Program windows": `WINDOW display` first, the new window and
its hints, no frame before the desktop's size, a frame of exactly each size
asked for (1:1, the stand-in's pattern pixel for pixel), `0 0` leaving the
size to the program, `WINDOW gone` after close, and the compositor ending
when the desktop's channel closes and no desktop comes back.

Last in part one, the desktop's display layout (layout_part): every part
before it runs without one, as a desktop older than 0.3.88 does (the
outputs of before: one per mode, at 0,0); then `WINDOW layout` and
`WINDOW primary` make one named wl_output per display at its place
(wl_output's geometry, xdg_output's logical position and name), a
repeated set changes nothing, and a display not in a set is removed.

The restart part ("Programs survive a desktop restart"): the channel lost
with programs running, which keep running and drawing; the same socket
served again; every window announced again with its newest frame; the
`WINDOW quit 0` ending; no desktop coming back; `--display`.

Part two, with a blit-server (on the mock display, gate_display.sh), checks
Blit's side: a version 5 hello is answered with ` window=0`, each WINDOW
line `WINDOW error <id> ENOTSUP`, a version 3 or 4 hello `ERROR 0 EINVAL`,
and blit-compositor reaching Blit directly keeps running and shows nothing.

Every line about a picture is version 5's `<WORD> <verb> <id> [arguments]`
(docs/api.md, "One grammar, version 5"): the compositor asks
`INPUT sock <id> <path>`, and takes `FRAME release` and `FRAME error`; the
version 4 forms (`RELEASE`, `ERROR <id>`) release nothing.
"""
import array
import fcntl
import json
import os
import shutil
import signal
import socket
import stat
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

# The frame channel's session, channel and placement helpers, shared with
# the INPUT gate (same directory).
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
sys.dont_write_bytecode = True
import frame_input_gate as fig  # noqa: E402
import gate_env  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19823')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT
SEALS = 0x0002 | 0x0004 | 0x0008  # F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE
XR24 = 0x34325258
F_GET_SEALS = 1034
TOKEN = 'compositor-gate-token'


def compositor_env(**more):
    """blit-compositor's environment (gate_env.process_env). Every
    compositor this gate starts runs with `--x11 off`, so `WINDOW display`
    says `-` for X whether or not the host has Xwayland
    (tools/tests/xwayland_gate.py is the gate with it on)."""
    return gate_env.process_env(**more)


def fail(why):
    print('COMPOSITOR_GATE_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why() if callable(why) else why)


def wait_for(predicate, why, seconds=8.0):
    deadline = time.time() + seconds
    while time.time() < deadline:
        try:
            value = predicate()
        except OSError:
            value = None
        if value:
            return value
        time.sleep(0.05)
    fail(why() if callable(why) else why)


def pattern(width, height):
    """The stand-in's picture as FRM1 format 24 (RGB) rows."""
    out = bytearray(width * height * 3)
    i = 0
    for y in range(height):
        g = y & 255
        yb = (y >> 8) & 7
        for x in range(width):
            out[i] = x & 255
            out[i + 1] = g
            out[i + 2] = 0x80 | ((x >> 8) & 7) << 3 | yb
            i += 3
    return bytes(out)


class Desktop:
    """The desktop's end of a frame channel: one accepted connection."""

    def __init__(self, listener, answer=b'OK dmabuf=0 window=1\n'):
        self.clip_fds = []
        listener.settimeout(10)
        self.sock, _ = listener.accept()
        self.sock.settimeout(5)
        hello = self.sock.recv(128)
        check(hello[:8] == b'BLITFRM1', 'not a hello: %r' % hello[:16])
        version, length = struct.unpack_from('<II', hello, 8)
        check(version == 5, 'the hello says version %d, not 5' % version)
        check(hello[16:16 + length].decode() == TOKEN, 'the hello has the wrong token')
        self.sock.send(answer)
        # `INPUT sock <id> <path>`, one per toplevel: for Blit, which the
        # desktop relays it to; recorded, not part of the WINDOW sequence.
        self.input_socks = {}

    def next(self, wait=5.0):
        """('line', text) or ('frame', header fields, fd); None on timeout."""
        while True:
            got = self.next_any(wait)
            if got is None or got[0] != 'line' or not got[1].startswith('INPUT sock '):
                return got
            image, path = got[1].split(' ')[2:4]
            # Each toplevel its own socket, <display>-input-<id>.
            check(path.startswith('/') and path.endswith('-input-%s' % image) and
                  int(image) not in self.input_socks, 'an INPUT sock line %r' % got[1])
            self.input_socks[int(image)] = path

    def next_any(self, wait):
        self.sock.settimeout(wait)
        try:
            data, fds, _flags, _addr = socket.recv_fds(self.sock, 4096, 4)
        except socket.timeout:
            return None
        if data[:4] == b'FRM3':
            check(len(data) == 72 and 1 <= len(fds) <= 4, 'FRM3 of %d bytes with %d fds' % (len(data), len(fds)))
            image, sequence, w, h, fourcc, planes, modifier = struct.unpack_from('<IQIIIIQ', data, 4)
            check(planes == len(fds), 'FRM3 of %d planes with %d fds' % (planes, len(fds)))
            layout = struct.unpack_from('<8I', data, 40)
            return ('frm3', (image, sequence, w, h, fourcc, planes, modifier, layout), fds)
        if data[:4] == b'FRM1':
            check(len(data) == 48 and len(fds) == 1, 'FRM1 of %d bytes with %d fds' % (len(data), len(fds)))
            fields = struct.unpack_from('<IQIIIIIIII', data, 4)
            return ('frame', fields, fds[0])
        check(data.endswith(b'\n') and data.count(b'\n') == 1, 'not one line: %r' % data)
        if data.startswith(b'CLIP read '):
            # The reader's pipe, passed with the line: the gate writes into it.
            check(len(fds) == 1, 'CLIP read with %d fds' % len(fds))
            self.clip_fds.append(fds[0])
        else:
            for fd in fds:
                os.close(fd)
        return ('line', data[:-1].decode())

    def line(self, expected, wait=5.0):
        got = self.next(wait)
        check(got is not None, 'no line, expected %r' % expected)
        check(got[0] == 'line', 'a frame, expected %r' % expected)
        check(got[1] == expected, 'got %r, expected %r' % (got[1], expected))
        return got[1]

    def frame(self, width, height, wait=5.0, image_id=1):
        got = self.next(wait)
        check(got is not None, 'no frame, expected %dx%d' % (width, height))
        check(got[0] == 'frame', 'got %r, expected a %dx%d frame' % (got, width, height))
        (image, sequence, w, h, fmt, stride, x, y, rw, rh), fd = got[1], got[2]
        try:
            check(image == image_id, 'the frame is for picture %d, not %d' % (image, image_id))
            check((w, h) == (width, height), 'a %dx%d frame, expected %dx%d' % (w, h, width, height))
            check(fmt == 24 and stride == width * 3 and (x, y, rw, rh) == (0, 0, 0, 0),
                  'header format %d stride %d rect %s' % (fmt, stride, (x, y, rw, rh)))
            seals = fcntl.fcntl(fd, F_GET_SEALS)
            check(seals & SEALS == SEALS, 'the memfd is not sealed: %#x' % seals)
            check(os.fstat(fd).st_size == stride * height, 'the memfd holds %d bytes' % os.fstat(fd).st_size)
            pixels = os.pread(fd, stride * height, 0)
            expected = pattern(width, height)
            if pixels != expected:
                at = next(i for i in range(len(expected)) if pixels[i] != expected[i]) // 3
                fail('the %dx%d frame differs from the pattern first at %d,%d: %s, expected %s'
                     % (width, height, at % width, at // width, tuple(pixels[at * 3:at * 3 + 3]),
                        tuple(expected[at * 3:at * 3 + 3])))
        finally:
            os.close(fd)
        return sequence

    def frm3(self, width, height, image_id, wait=5.0):
        """An FRM3 of the stand-in's pattern, XRGB8888 LINEAR, one plane:
        (sequence, (st_dev, st_ino) of its plane)."""
        got = self.next(wait)
        check(got is not None and got[0] == 'frm3', 'got %r, expected a %dx%d FRM3' % (got, width, height))
        (image, sequence, w, h, fourcc, planes, modifier, layout), fds = got[1], got[2]
        try:
            check(image == image_id and (w, h) == (width, height),
                  'FRM3 %d %dx%d, expected %d %dx%d' % (image, w, h, image_id, width, height))
            check(fourcc == XR24 and planes == 1 and modifier == 0 and layout[:2] == (0, width * 4)
                  and layout[2:] == (0,) * 6, 'FRM3 fourcc %#x planes %d modifier %#x layout %s'
                  % (fourcc, planes, modifier, layout))
            pixels = os.pread(fds[0], width * 4 * height, 0)
            rgb = pattern(width, height)
            expected = bytearray(width * height * 4)
            expected[0::4], expected[1::4], expected[2::4] = rgb[2::3], rgb[1::3], rgb[0::3]
            check(pixels == bytes(expected), 'the FRM3 plane is not the pattern')
            st = os.fstat(fds[0])
            return sequence, (st.st_dev, st.st_ino)
        finally:
            for fd in fds:
                os.close(fd)

    def nothing(self, wait=0.4):
        got = self.next(wait)
        check(got is None, 'unexpected %r' % (got[:2] if got else got,))

    def send(self, line):
        self.sock.send(line.encode() + b'\n')


def states_part(desk, standin, env, display, work):
    """maximize/minimize/parent/hide, the maximized state and WINDOW output
    (docs/api.md, "Program windows"), with `--states`: window 4 and its
    child 5."""
    out_path = os.path.join(work, 'standin-states.out')
    client = start([standin, '--states', 'States'], dict(env, WAYLAND_DISPLAY=display),
                   open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    try:
        for want in ('WINDOW new 4 0', 'WINDOW title 4 States', 'WINDOW appid 4 blit.standin',
                     'WINDOW limits 4 64 48 0 0', 'WINDOW decor 4 server'):
            desk.line(want)
        # Configured at its initial commit: its own size first.
        desk.frame(256, 160, image_id=4)
        wait_for(lambda: 'STANDIN enter BLIT-1 1920x1080@60000' in printed(),
                 lambda: 'the window did not enter the first output:\n' + printed())
        desk.send('WINDOW output 4 1280 720 30000')
        desk.send('WINDOW size 4 200 100 maximized')
        desk.frame(200, 100, image_id=4)
        for want in ('WINDOW maximize 4 on', 'WINDOW minimize 4', 'WINDOW maximize 4 off',
                     'WINDOW new 5 0', 'WINDOW title 5 Child', 'WINDOW decor 5 client',
                     'WINDOW parent 5 4'):
            desk.line(want)
        desk.frame(50, 40, image_id=5)
        desk.send('WINDOW size 5 50 40')
        desk.frame(50, 40, image_id=5)
        desk.line('WINDOW hide 5')
        desk.frame(50, 40, image_id=5)
        desk.line('WINDOW parent 5 0')
        desk.nothing(0.3)
        # A mode another window's output already has, then a new one: the
        # idle output takes it rather than a third global.
        wait_for(lambda: 'STANDIN enter BLIT-2 1280x720@30000' in printed(),
                 lambda: 'the window did not enter the 1280x720 output:\n' + printed())
        desk.send('WINDOW output 4 1920 1080 60000')
        wait_for(lambda: printed().count('STANDIN enter BLIT-1 1920x1080@60000') == 2,
                 lambda: 'the window did not go back to BLIT-1:\n' + printed())
        desk.send('WINDOW output 4 800 600 75000')
        wait_for(lambda: 'STANDIN enter BLIT-2 800x600@75000' in printed(),
                 lambda: 'the idle output did not take the new mode:\n' + printed())
        desk.send('WINDOW close 4')
        desk.line('WINDOW gone 5')
        desk.line('WINDOW gone 4')
        try:
            status = client.wait(5)
        except subprocess.TimeoutExpired:
            fail('the states stand-in did not exit after WINDOW close')
        output = printed()
        check(status == 0, 'the states stand-in exited %d: %s' % (status, output))
        moves = [l for l in output.splitlines() if l.startswith(('STANDIN enter', 'STANDIN leave',
                                                                  'STANDIN output BLIT-2'))]
        check(moves == ['STANDIN enter BLIT-1 1920x1080@60000', 'STANDIN leave BLIT-1',
                        'STANDIN output BLIT-2 mode 1280x720@30000',
                        'STANDIN enter BLIT-2 1280x720@30000', 'STANDIN leave BLIT-2',
                        'STANDIN enter BLIT-1 1920x1080@60000',
                        'STANDIN output BLIT-2 mode 800x600@75000', 'STANDIN leave BLIT-1',
                        'STANDIN enter BLIT-2 800x600@75000'],
              'the outputs entered and left:\n%s' % '\n'.join(moves))
        check('STANDIN child enter BLIT-1 1920x1080@60000' in output, 'the child is on no output')
        for want in ('STANDIN configure 200x100 maximized\n', 'STANDIN capabilities 2 3 4\n',
                     'STANDIN child configure 50x40\n', 'STANDIN child hidden',
                     'STANDIN child orphaned', 'STANDIN done draws=5 releases=5 frames=5'):
            check(want in output, 'the states stand-in did not print %r:\n%s' % (want, output))
        check(output.count('STANDIN child configure 50x40') == 2, 'the child was not configured again')
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('maximize+minimize+parent(5->4,->0)+hide->frame maximized output=BLIT-2:1280x720@30000,'
            'BLIT-1,BLIT-2:800x600@75000')


def appmenu_part(desk, standin, env, display, work, log):
    """The global menu (ADR 0229), with `--appmenu`, window 12: the
    program's D-Bus address for its menus reaches the desktop as `WINDOW
    appmenu <id> <service> <path>` (an address that is not D-Bus syntax
    ignored), and `- -` when it releases it."""
    out_path = os.path.join(work, 'standin-appmenu.out')
    client = start([standin, '--appmenu', 'Menus'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    try:
        for want in ('WINDOW new 12 0', 'WINDOW title 12 Menus', 'WINDOW appid 12 blit.standin',
                     'WINDOW limits 12 64 48 0 0', 'WINDOW decor 12 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=12)
        desk.send('WINDOW size 12 200 120')
        desk.frame(200, 120, image_id=12)
        desk.line('WINDOW appmenu 12 :1.42 /MenuBar/1')
        wait_for(lambda: 'STANDIN appmenu set' in printed(), lambda: 'no appmenu set:\n' + printed())
        desk.nothing(0.3)
        check('appmenu: an address that is not D-Bus syntax is ignored' in open(log).read(),
              'the address that is not D-Bus syntax was not noted')
        # A new size (the same one is no configure since 0.3.87): the
        # stand-in releases its address at its next configure.
        desk.send('WINDOW size 12 210 120')
        desk.frame(210, 120, image_id=12)
        desk.line('WINDOW appmenu 12 - -')
        desk.send('WINDOW close 12')
        desk.line('WINDOW gone 12')
        check(client.wait(5) == 0, 'the appmenu stand-in failed:\n' + printed())
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return 'address=:1.42,/MenuBar/1 not-dbus=ignored release=- -'


def layout_part(desk, standin, env, display, work, log):
    """The desktop's display layout (docs/api.md, "Program windows"), last:
    a layout is for the rest of the compositor's life. With `--xdg-output`,
    window 14: before any layout the outputs of before it (BLIT-1 at 0,0,
    as xdg_output says too); `WINDOW primary` and a set of two displays --
    the outputs of before removed, one named wl_output per display at its
    place (wl_output's geometry and xdg_output's logical position), the
    window on the primary; `WINDOW output` its display by mode; the same
    set again changing nothing; the displays moved and re-timed in place;
    a display gone removed (and destroyed later, its resource kept by the
    stand-in past that); two displays of one mode, where `WINDOW output`
    leaves the window where it is."""
    out_path = os.path.join(work, 'standin-layout.out')
    client = start([standin, '--xdg-output', 'Layout'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    why = lambda: '%s\n%s' % (printed(), open(log).read())

    def send_set(*displays):
        for line in displays:
            desk.send('WINDOW layout %s' % line)
        desk.send('WINDOW layout end')

    try:
        for want in ('WINDOW new 14 0', 'WINDOW title 14 Layout', 'WINDOW appid 14 blit.standin',
                     'WINDOW limits 14 64 48 0 0', 'WINDOW decor 14 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=14)
        # No layout yet: the outputs of before it, at 0,0.
        wait_for(lambda: 'STANDIN layout BLIT-1 at 0,0 mode 1920x1080@60000 logical 0,0 1920x1080 BLIT-1\n'
                 in printed() and 'STANDIN enter BLIT-1 1920x1080@60000' in printed(),
                 lambda: 'BLIT-1 at 0,0 before the layout:\n' + why())
        # The primary, then the set (one display's refresh unknown, 0).
        removed_at = time.time()
        desk.send('WINDOW primary HDMI-A-1')
        send_set('DP-1 0 0 1920 1080 60000', 'HDMI-A-1 1920 0 2560 1440 0')
        for want in ('STANDIN output removed BLIT-1', 'STANDIN output removed BLIT-2',
                     'STANDIN layout DP-1 at 0,0 mode 1920x1080@60000 logical 0,0 1920x1080 DP-1\n',
                     'STANDIN layout HDMI-A-1 at 1920,0 mode 2560x1440@0 logical 1920,0 2560x1440 HDMI-A-1\n',
                     'STANDIN enter HDMI-A-1 2560x1440@0'):
            wait_for(lambda: want in printed(), lambda: 'no %r:\n%s' % (want, why()))
        # The desktop's display for the window, by its mode.
        desk.send('WINDOW output 14 1920 1080 60000')
        wait_for(lambda: 'STANDIN enter DP-1 1920x1080@60000' in printed(),
                 lambda: 'the window did not enter DP-1:\n' + why())
        # The same set again (the desktop repeats it): nothing.
        said = len(printed())
        logged = len(open(log).read())
        desk.send('WINDOW primary HDMI-A-1')
        send_set('DP-1 0 0 1920 1080 60000', 'HDMI-A-1 1920 0 2560 1440 0')
        desk.nothing(0.5)
        check(printed()[said:] == '', lambda: 'a repeated layout changed something:\n' + printed()[said:])
        check('display ' not in open(log).read()[logged:] and 'layout:' not in open(log).read()[logged:],
              lambda: 'a repeated layout was applied:\n' + open(log).read()[logged:])
        # Moved and re-timed: told in place, no output added or removed.
        send_set('DP-1 2560 0 1920 1080 75000', 'HDMI-A-1 0 0 2560 1440 144000')
        for want in ('STANDIN layout DP-1 at 2560,0 mode 1920x1080@75000 logical 2560,0 1920x1080 DP-1\n',
                     'STANDIN layout HDMI-A-1 at 0,0 mode 2560x1440@144000 logical 0,0 2560x1440 HDMI-A-1\n'):
            wait_for(lambda: want in printed(), lambda: 'no %r:\n%s' % (want, why()))
        # HDMI-A-1 gone, DP-2 of DP-1's mode: `WINDOW output` of that mode
        # names two displays; the window stays on DP-1, where it is.
        send_set('DP-1 2560 0 1920 1080 75000', 'DP-2 4480 0 1920 1080 75000')
        for want in ('STANDIN output removed HDMI-A-1',
                     'STANDIN layout DP-2 at 4480,0 mode 1920x1080@75000 logical 4480,0 1920x1080 DP-2\n'):
            wait_for(lambda: want in printed(), lambda: 'no %r:\n%s' % (want, why()))
        desk.send('WINDOW output 14 1920 1080 75000')
        wait_for(lambda: 'window 14: 2 displays are 1920x1080 at 75000 mHz (WINDOW output names a mode, not a '
                 'display): on DP-1' in open(log).read(), lambda: 'the two displays of one mode:\n' + why())
        # Named (0.3.88's trailing <name>): the display of that name, of the
        # two of one mode.
        desk.send('WINDOW output 14 1920 1080 75000 DP-2')
        wait_for(lambda: 'window 14: display DP-2' in open(log).read(), lambda: 'the named display:\n' + why())
        wait_for(lambda: 'STANDIN enter DP-2 1920x1080@75000' in printed(), lambda: 'not on DP-2:\n' + why())
        for want in ('display BLIT-1: gone', 'display HDMI-A-1: gone',
                     'display HDMI-A-1: 2560x1440 at 1920,0, 0 mHz (new)', 'layout: 2 displays, 6400x1080'):
            check(want in open(log).read(), lambda: 'the log does not say %r:\n%s' % (want, open(log).read()))
        # The removed globals destroyed (BC_OUTPUT_LINGER_MS) while the
        # stand-in still holds them: its release at exit is no error.
        time.sleep(max(0.0, removed_at + 5.5 - time.time()))
        desk.send('WINDOW close 14')
        desk.line('WINDOW gone 14')
        check(client.wait(5) == 0, lambda: 'the layout stand-in failed:\n' + why())
        moves = [l for l in printed().splitlines() if l.startswith(('STANDIN enter', 'STANDIN leave'))]
        check(moves == ['STANDIN enter BLIT-1 1920x1080@60000', 'STANDIN leave BLIT-1',
                        'STANDIN enter HDMI-A-1 2560x1440@0', 'STANDIN leave HDMI-A-1',
                        'STANDIN enter DP-1 1920x1080@60000', 'STANDIN leave DP-1',
                        'STANDIN enter DP-2 1920x1080@75000'],
              lambda: 'the window entered and left:\n%s' % '\n'.join(moves))
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('before=BLIT-1@0,0 primary+set->BLIT-1,2 removed DP-1@0,0 HDMI-A-1@1920,0(0 mHz) window->HDMI-A-1 '
            'output->DP-1 repeat=nothing moved+retimed=in place gone=HDMI-A-1 same-mode=stays on DP-1 '
            'named=DP-2')


def activation_part(desk, standin, env, display, work, log):
    """Activation (xdg_activation_v1, ADR 0229), with `--activate`, window
    13: a token never issued asks for nothing; a token asked for with a key
    press's serial and the surface, then used, is `WINDOW activate 13
    xdg-activation`; used again, nothing; a window the desktop has
    activated asks for nothing."""
    out_path = os.path.join(work, 'standin-activate.out')
    client = start([standin, '--activate', 'Activate'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    route = None
    frame = [0]

    def key(code):
        text = b''
        for state in ('press', 'release'):
            frame[0] += 1
            text += b'INPUT key %d %d %s\nINPUT frame %d %d\n' % (BLIT_PICTURE, code, state.encode(),
                                                                   BLIT_PICTURE, frame[0])
        route.sendall(text)

    try:
        for want in ('WINDOW new 13 0', 'WINDOW title 13 Activate', 'WINDOW appid 13 blit.standin',
                     'WINDOW limits 13 64 48 0 0', 'WINDOW decor 13 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=13)
        desk.send('WINDOW size 13 200 120')
        desk.frame(200, 120, image_id=13)
        wait_for(lambda: 'STANDIN activation bad token' in printed(), lambda: 'no bad token:\n' + printed())
        desk.nothing(0.3)
        wait_for(lambda: 'activation: a token not issued here, used, or replaced by newer ones: nothing asked'
                 in open(log).read(), 'the bad token was not logged')
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[13])
        key(30)
        desk.line('WINDOW activate 13 xdg-activation')
        wait_for(lambda: 'STANDIN activation used 32' in printed(), lambda: 'no token used:\n' + printed())
        desk.nothing(0.3)
        check('activation: a token issued (window 13, the serial of its newest press)' in open(log).read(),
              'the token was not issued for the key press:\n' + open(log).read())
        # Activated by the desktop, it has nothing to ask for.
        desk.send('WINDOW size 13 200 120 activated')
        desk.frame(200, 120, image_id=13)
        used = printed().count('STANDIN activation used')
        key(30)
        wait_for(lambda: printed().count('STANDIN activation used') > used, lambda: 'no token used:\n' + printed())
        desk.nothing(0.3)
        desk.send('WINDOW close 13')
        desk.line('WINDOW gone 13')
        check(client.wait(5) == 0, 'the activation stand-in failed:\n' + printed())
    finally:
        if route: route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return 'bad-token=nothing(logged) key-press-token->WINDOW activate 13 xdg-activation reuse=nothing activated=nothing'


def cursor_part(desk, standin, env, display, work, log):
    """The pointer's shape (ADR 0229), with `--cursor`, window 11: shapes the
    program names reach the desktop as `WINDOW cursor <id> <name>` in Blit's
    OSC 22 vocabulary, once per change; no surface hides it (`none`); a
    cursor image of the program's own is never drawn (`default`)."""
    out_path = os.path.join(work, 'standin-cursor.out')
    client = start([standin, '--cursor', 'Cursor'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    route = None
    frame = [0]

    def lines(*events):
        text = b''
        for e in events:
            frame[0] += 1
            text += b'INPUT %s\nINPUT frame %d %d\n' % (e.encode() % BLIT_PICTURE, BLIT_PICTURE, frame[0])
        route.sendall(text)

    try:
        for want in ('WINDOW new 11 0', 'WINDOW title 11 Cursor', 'WINDOW appid 11 blit.standin',
                     'WINDOW limits 11 64 48 0 0', 'WINDOW decor 11 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=11)
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[11])
        lines('ptr %d 10.00 10.00')
        desk.line('WINDOW cursor 11 text')
        lines('ptr %d 20.00 20.00')  # the same shape again: no line
        desk.nothing(0.3)
        lines('btn %d 272 press')
        desk.line('WINDOW cursor 11 pointer')
        lines('btn %d 272 release', 'btn %d 273 press')
        desk.line('WINDOW cursor 11 none')
        lines('btn %d 273 release', 'btn %d 274 press')
        desk.line('WINDOW cursor 11 default')
        lines('btn %d 274 release')
        wait_for(lambda: 'STANDIN cursor image' in printed(), lambda: 'no cursor image:\n' + printed())
        desk.nothing(0.3)
        check("a program's own cursor image is shown as the display's pointer" in open(log).read(),
              'the cursor image was not noted in the log')
        desk.send('WINDOW close 11')
        desk.line('WINDOW gone 11')
        check(client.wait(5) == 0, 'the cursor stand-in failed:\n' + printed())
    finally:
        if route: route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return 'shapes=text,pointer(once per change) set_cursor(NULL)=none image=default(noted)'


def clip_part(desk, standin, env, display, work):
    """The clipboard (ADR 0229), with `--clip`, window 10, the gate as Hibr's
    clipboard: a program's copy offered to the desktop as one text type and
    read through a pipe; the desktop's offer pasted by the program as
    UTF8_STRING through a `CLIP read`; a secret offered and cleared both
    ways; a source destroyed clears; the primary selection the same."""
    out_path = os.path.join(work, 'standin-clip.out')
    client = start([standin, '--clip', 'Clip'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    route = None
    frame = [0]

    def key(code):
        frame[0] += 1
        route.sendall(b'INPUT key %d %d press\nINPUT frame %d %d\nINPUT key %d %d release\nINPUT frame %d %d\n'
                      % (BLIT_PICTURE, code, BLIT_PICTURE, frame[0], BLIT_PICTURE, code, BLIT_PICTURE,
                         frame[0] + 1))
        frame[0] += 1

    def seen(text):
        wait_for(lambda: text in printed(), lambda: 'the program did not print %r:\n%s' % (text, printed()))

    def desktop_reads(seq, type_):
        r, w = os.pipe()
        socket.send_fds(desk.sock, [b'CLIP read %d %s\n' % (seq, type_.encode())], [w])
        os.close(w)
        out = b''
        while True:
            chunk = os.read(r, 4096)
            if not chunk:
                break
            out += chunk
        os.close(r)
        return out

    def desktop_answers(text):
        fd = desk.clip_fds.pop(0)
        os.write(fd, text)
        os.close(fd)

    aliases = 'text/plain;charset=utf-8,UTF8_STRING,text/plain,STRING,TEXT'
    try:
        for want in ('WINDOW new 10 0', 'WINDOW title 10 Clip', 'WINDOW appid 10 blit.standin',
                     'WINDOW limits 10 64 48 0 0', 'WINDOW decor 10 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=10)
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[10])
        # A program copies: one text type to the desktop, read through a pipe.
        key(46)
        desk.line('CLIP offer 1 clipboard text/plain;charset=utf-8')
        seen('STANDIN selection clipboard text/plain;charset=utf-8,text/plain,x-standin/other')
        got = desktop_reads(1, 'text/plain;charset=utf-8')
        check(got == b'copied: text/plain;charset=utf-8', 'the desktop read %r' % got)
        check(desktop_reads(1, 'image/png') == b'', 'a type nobody offers was answered')
        check(desktop_reads(99, 'text/plain;charset=utf-8') == b'', 'a stale offer was answered')
        # The desktop copies: the program's source cancelled, the aliases offered,
        # its paste a CLIP read the desktop answers.
        desk.send('CLIP offer 7 clipboard text/plain;charset=utf-8')
        seen('STANDIN source cancelled clipboard')
        seen('STANDIN selection clipboard ' + aliases + '\n')
        key(47)
        desk.line('CLIP read 7 text/plain;charset=utf-8')
        desktop_answers(b'hello from hibr')
        seen('STANDIN pasted clipboard hello from hibr')
        # A secret (the Vault), offered and cleared.
        desk.send('CLIP offer 8 clipboard text/plain;charset=utf-8 x-kde-passwordManagerHint')
        seen('STANDIN selection clipboard ' + aliases + ',x-kde-passwordManagerHint')
        desk.send('CLIP clear 8')
        wait_for(lambda: printed().rstrip().endswith('STANDIN selection clipboard none') or
                 printed().splitlines()[-1] == 'STANDIN selection clipboard none',
                 lambda: 'the secret was not cleared:\n' + printed())
        # A program's secret, then its source destroyed: offered, then cleared.
        key(45)
        desk.line('CLIP offer 2 clipboard text/plain;charset=utf-8 x-kde-passwordManagerHint')
        key(32)
        desk.line('CLIP clear 2')
        # The primary selection, both ways.
        key(25)
        desk.line('CLIP offer 3 primary text/plain;charset=utf-8')
        desk.send('CLIP offer 9 primary text/plain;charset=utf-8')
        seen('STANDIN source cancelled primary')
        seen('STANDIN selection primary ' + aliases)
        key(24)
        desk.line('CLIP read 9 text/plain;charset=utf-8')
        desktop_answers(b'middle')
        seen('STANDIN pasted primary middle')
        desk.nothing(0.3)
        desk.send('WINDOW close 10')
        desk.line('WINDOW gone 10')
        check(client.wait(5) == 0, 'the clip stand-in failed:\n' + printed())
    finally:
        if route: route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('copy->CLIP offer text,read-by-pipe paste<-CLIP read aliases=5 secret=offer+clear-both-ways '
            'source-gone->clear primary=both-ways stale/unknown=empty')


def csd_part(desk, standin, env, display, work):
    """A program that draws its own frame (ADR 0229), with `--csd`, window 9:
    `WINDOW decor client`, its geometry inside its buffer, tiled states
    passed on, and move, menu and resize asked from its own frame with a
    press's serial -- one asked with no press comes to nothing."""
    out_path = os.path.join(work, 'standin-csd.out')
    client = start([standin, '--csd', 'Csd'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    route = None
    try:
        for want in ('WINDOW new 9 0', 'WINDOW title 9 Csd', 'WINDOW appid 9 blit.standin',
                     'WINDOW limits 9 64 48 0 0', 'WINDOW decor 9 client'):
            desk.line(want)
        desk.line('WINDOW geometry 9 10 10 236 140')
        desk.frame(256, 160, image_id=9)
        desk.send('WINDOW size 9 200 120 tiled-left tiled-top')
        desk.line('WINDOW geometry 9 10 10 180 100')
        desk.frame(200, 120, image_id=9)
        wait_for(lambda: 'STANDIN csd asked to move with no press' in printed(),
                 lambda: 'no unsolicited move:\n' + printed())
        desk.nothing(0.3)  # no WINDOW move: there was no press
        check('STANDIN configure 200x120 tiled-left tiled-top' in printed(),
              lambda: 'the tiled states did not reach the program:\n' + printed())
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[9])
        frame = [0]

        def lines(*events):
            text = b''
            for e in events:
                frame[0] += 1
                text += b'INPUT %s\nINPUT frame %d %d\n' % (e.encode() % BLIT_PICTURE, BLIT_PICTURE, frame[0])
            route.sendall(text)

        lines('ptr %d 50.00 50.00', 'btn %d 272 press')
        desk.line('WINDOW move 9')
        lines('btn %d 272 release', 'btn %d 273 press')
        desk.line('WINDOW menu 9 15 16')
        lines('btn %d 273 release', 'btn %d 274 press')
        desk.line('WINDOW resize 9 bottom-right')
        lines('btn %d 274 release')
        wait_for(lambda: printed().count('STANDIN csd asked with serial') == 3,
                 lambda: 'the frame did not ask three times:\n' + printed())
        desk.nothing(0.3)
        desk.send('WINDOW close 9')
        desk.line('WINDOW gone 9')
        check(client.wait(5) == 0, 'the csd stand-in failed:\n' + printed())
    finally:
        if route: route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('decor=client geometry=10,10+236x140->180x100 tiled=left,top '
            'move/menu@15,16/resize=bottom-right on a press only')


def sub_part(desk, standin, env, display, work):
    """Subsurfaces (docs/api.md, "Subsurfaces"), with `--sub`: window 7 and
    its subsurface, picture 8. Synchronized, the subsurface's frame waits
    for the parent's commit, then `WINDOW sub` and the frame; desynchronized,
    its frame and size come at once and its position at the parent's
    commit; the input region decides who takes the pointer; place_below
    makes it `below`; destroying the wl_subsurface ends picture 8."""
    out_path = os.path.join(work, 'standin-sub.out')
    client = start([standin, '--sub', 'Sub'], dict(env, WAYLAND_DISPLAY=display), open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    route = None
    try:
        for want in ('WINDOW new 7 0', 'WINDOW title 7 Sub', 'WINDOW appid 7 blit.standin',
                     'WINDOW limits 7 64 48 0 0', 'WINDOW decor 7 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=7)
        # 1: synchronized. The parent's frame, then nothing while the
        # Each step's nudge is a new size: the same one is no configure
        # (0.3.87), and the stand-in moves on at each configure.
        # subsurface's commit is only cached, then the line and its frame.
        desk.send('WINDOW size 7 200 120')
        desk.frame(200, 120, image_id=7)
        # (the stand-in waits 400 ms between the two commits)
        desk.nothing(0.25)
        desk.line('WINDOW sub 8 7 20 30 64 40 above')
        desk.frame(64, 40, image_id=8)
        wait_for(lambda: 'STANDIN sub step 1' in printed(), lambda: 'no sub step 1:\n' + printed())
        # 2: desynchronized. Its new size and frame at once, at the old
        # position; the new position only at the parent's commit.
        desk.send('WINDOW size 7 202 120')
        desk.frame(202, 120, image_id=7)
        desk.line('WINDOW sub 8 7 20 30 48 24 above')
        desk.frame(48, 24, image_id=8)
        desk.line('WINDOW sub 8 7 40 10 48 24 above')
        wait_for(lambda: 'STANDIN sub step 2' in printed(), lambda: 'no sub step 2:\n' + printed())
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[7])
        frame = [0]

        def pointer(x, y, *wanted):
            frame[0] += 1
            before = len(printed().splitlines())
            route.sendall(b'INPUT ptr %d %.2f %.2f\nINPUT frame %d %d\n'
                          % (BLIT_PICTURE, x, y, BLIT_PICTURE, frame[0]))
            want = ['STANDIN ' + w for w in wanted]
            wait_for(lambda: [l for l in printed().splitlines()[before:] if l in want] == want,
                     lambda: 'the pointer at %s,%s: expected %s:\n%s'
                     % (x, y, want, '\n'.join(printed().splitlines()[before:])))

        # 3: an empty input region: over the subsurface, the parent has it.
        desk.send('WINDOW size 7 200 120')
        desk.frame(200, 120, image_id=7)
        wait_for(lambda: 'STANDIN sub step 3' in printed(), lambda: 'no sub step 3:\n' + printed())
        desk.nothing(0.3)
        pointer(45, 15, 'pointer enter main 45.00 15.00')
        # 4: the whole surface again: the subsurface has it, in its pixels.
        desk.send('WINDOW size 7 202 120')
        desk.frame(202, 120, image_id=7)
        wait_for(lambda: 'STANDIN sub step 4' in printed(), lambda: 'no sub step 4:\n' + printed())
        pointer(46, 16, 'pointer leave main', 'pointer enter sub 6.00 6.00')
        # 5: below the parent: `below`, and the parent over it has the pointer.
        desk.send('WINDOW size 7 200 120')
        desk.frame(200, 120, image_id=7)
        desk.line('WINDOW sub 8 7 40 10 48 24 below')
        wait_for(lambda: 'STANDIN sub step 5' in printed(), lambda: 'no sub step 5:\n' + printed())
        pointer(47, 17, 'pointer leave sub', 'pointer enter main 47.00 17.00')
        # 6: the wl_subsurface destroyed: its picture gone at once.
        desk.send('WINDOW size 7 202 120')
        desk.frame(202, 120, image_id=7)
        desk.line('WINDOW gone 8')
        wait_for(lambda: 'STANDIN sub step 6' in printed(), lambda: 'no sub step 6:\n' + printed())
        desk.send('WINDOW close 7')
        desk.line('WINDOW gone 7')
        check(client.wait(5) == 0, 'the sub stand-in failed:\n' + printed())
        check('STANDIN drew sub 64x40' in printed() and 'STANDIN drew sub 48x24' in printed(),
              'the subsurface did not draw:\n' + printed())
    finally:
        if route: route.close()
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('sync=cached-until-parent desync=frame+size-at-once,position-at-parent '
            'input-region=empty->main,all->sub below->main destroy->gone')


def pacing_part(desk, standin, env, display, work):
    """A client that draws on every frame callback (milestone A's answered
    them at once) runs at its output's refresh: 10 more frames at 20 Hz
    take at least 9 periods, each presented with that refresh."""
    out_path = os.path.join(work, 'standin-animate.out')
    client = start([standin, '--animate', '10', 'Animate'], dict(env, WAYLAND_DISPLAY=display),
                   open(out_path, 'w'))
    printed = lambda: open(out_path).read()
    try:
        for want in ('WINDOW new 6 0', 'WINDOW title 6 Animate', 'WINDOW appid 6 blit.standin',
                     'WINDOW limits 6 64 48 0 0', 'WINDOW decor 6 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=6)
        desk.send('WINDOW output 6 640 480 20000')
        wait_for(lambda: 'STANDIN enter BLIT-2 640x480@20000' in printed(),
                 lambda: 'the window did not enter the 20 Hz output:\n' + printed())
        desk.send('WINDOW size 6 64 48')
        started = time.time()
        for _ in range(11):
            desk.frame(64, 48, image_id=6)
        took = time.time() - started
        wait_for(lambda: 'STANDIN animated 10 in' in printed(), 'the client did not animate')
        ms = int(printed().split('STANDIN animated 10 in ')[1].split()[0])
        check(ms >= 9 * 50 * 0.9 and took >= 9 * 0.05 * 0.9,
              'ten frames at 20 Hz took %d ms (the desktop saw %.3f s): not paced' % (ms, took))
        check(ms < 3000, 'ten frames at 20 Hz took %d ms' % ms)
        desk.send('WINDOW close 6')
        desk.line('WINDOW gone 6')
        check(client.wait(5) == 0, 'the animating stand-in failed: ' + printed())
        output = printed()
        check('STANDIN presentation presented=12 discarded=0 refresh=50000000' in output,
              'presentation feedback:\n' + output)
        check('STANDIN done draws=12 releases=12 frames=11' in output, 'counts:\n' + output)
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return 'shm 10 frames at 20 Hz in %d ms, presented=12 refresh=50ms' % ms


# The desktop's id for a window's picture in Blit, which Blit writes on the
# route's lines: deliberately not the compositor's window id (Hibr's are
# large numbers like 1259388934).
BLIT_PICTURE = 1259388934


def input_part(server, compositor, standin, work):
    """Blit's input lines on the compositor's input socket as wl_seat
    events: the gate plays the desktop on the channel and Blit on the input
    socket (docs/api.md, "Input straight from Blit"), sending a real
    blit-server's keymap on it as Blit does (`INPUT keymap` and a sealed
    memfd); the compositor has no BLIT_SERVER_PORT or token at all."""
    # Two fabricated keyboards (keymap_gate's sidecars): A, the first, and
    # B, given a Q that types a-umlaut, each with its own repeat.
    nodes = os.path.join(work, 'nodes')
    os.mkdir(nodes)
    for node, identity in (('event1', 'name=Keyboard A\nkinds=keyboard\ncapabilities=keys,repeat\nrepeat=250,33\n'),
                           ('event2', 'name=Keyboard B\nkinds=keyboard\ncapabilities=keys,repeat\nrepeat=500,50\n')):
        open(os.path.join(nodes, node), 'w').close()
        with open(os.path.join(nodes, node + '.identity'), 'w') as out:
            out.write('bustype=3\nvendor=05ac\nproduct=0256\nversion=0100\nphys=usb-gate/%s\n%s' % (node, identity))
    env = gate_env.server_env(dict(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                          BLIT_SETTINGS_PATH=os.path.join(work, 'settings-input.json'),
                          BLIT_LOGIN_AUTOSTART='0', BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'),
                          BLIT_EXPECT_DESKTOP='0', BLIT_TEST_MODE='1', BLIT_TEST_VT_SIGNALS='1',
                          BLIT_INPUT_ROOT=nodes))
    blit = subprocess.Popen([server, str(PORT)], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    runtime = os.path.join(work, 'run-input')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-input-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    comp = client = None
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the server did not start', 15)
        named = dict((d['name'], d['id']) for d in api('GET', '/v1/input/devices')['devices'])
        keyboards = (named['Keyboard A'], named['Keyboard B'])
        answer = urllib.request.urlopen(BASE + '/v1/input/keymap', timeout=10)
        keymap = answer.read()
        repeat = (int(answer.headers['X-Blit-Repeat-Rate']), int(answer.headers['X-Blit-Repeat-Delay']))
        check(answer.headers['X-Blit-Keymap-Device'] == keyboards[0], 'the default is not keyboard A')
        cenv = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
                    XDG_RUNTIME_DIR=runtime)
        for k in ('WAYLAND_DISPLAY', 'BLIT_SERVER_PORT', 'BLIT_API_TOKEN'):
            cenv.pop(k, None)
        log = os.path.join(work, 'compositor-input.log')
        comp = start([compositor, '--x11', 'off', '--log', log], cenv, open(os.path.join(work, 'compositor-input.out'), 'w'))
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        out_path = os.path.join(work, 'standin-input.out')
        client = start([standin, '--popup', '--gone-on-ctrl-d', 'Input'], dict(cenv, WAYLAND_DISPLAY='blit-0'),
                       open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Input', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.frame(256, 160)
        desk.send('WINDOW size 1 200 100 activated')
        desk.frame(200, 100)
        desk.line('WINDOW popup 2 1 12 63')
        desk.frame(80, 60, image_id=2)
        check(desk.input_socks.get(1) == os.path.join(runtime, 'blit-0-input-1'),
              'INPUT sock for window 1: %s' % desk.input_socks)
        check(stat.S_IMODE(os.stat(desk.input_socks[1]).st_mode) == 0o600, 'the input socket is not 0600')
        # No keymap until a route brings one: no_keymap, keys still go.
        check('STANDIN keymap format=0 size=0 xkb=0' in printed(),
              lambda: 'the client had a keymap before any route:\n' + printed())

        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        # Blit's keymap on the route, as Blit sends it: the line and a
        # sealed memfd of the text and a NUL.
        memfd = os.memfd_create('gate-keymap', os.MFD_CLOEXEC | os.MFD_ALLOW_SEALING)
        os.write(memfd, keymap + b'\0')
        fcntl.fcntl(memfd, 1033, 0x0f)  # F_ADD_SEALS: seal, shrink, grow, write
        socket.send_fds(route, [b'INPUT keymap %d 7 %d %d %d\n' % (BLIT_PICTURE, len(keymap) + 1, repeat[0],
                                                                     repeat[1])],
                        [memfd])
        os.close(memfd)
        wait_for(lambda: 'STANDIN keymap format=1 size=%d xkb=1' % (len(keymap) + 1) in printed(),
                 lambda: 'the client was not given Blit\'s keymap (%d bytes):\n%s' % (len(keymap), printed()))
        # Repeat is Blit's, keyboard A's: 250 ms, a 33 ms period.
        wait_for(lambda: 'STANDIN repeat 30 250' in printed(), lambda: 'no repeat info from Blit:\n' + printed())
        frame = [0]
        mark = [0]

        def lines_to(*lines):
            # Under Blit's id for the picture, as the desktop relayed it --
            # never the compositor's: the socket says which window.
            text = ''
            for line in lines:
                frame[0] += 1
                words = line.split(' ')
                words[1] = str(BLIT_PICTURE)
                text += 'INPUT %s\nINPUT frame %d %d\n' % (' '.join(words), BLIT_PICTURE, frame[0])
            route.sendall(text.encode())

        def seen(*wanted):
            """The client's next input prints are exactly these."""
            def events():
                return [l for l in printed().splitlines()[mark[0]:]
                        if l.split(' ')[1] in ('keyboard', 'key', 'modifiers', 'pointer', 'button',
                                               'axis', 'axis120', 'relative')]
            wait_for(lambda: len(events()) >= len(wanted),
                     lambda: 'the client saw %s, expected %s' % (events(), list(wanted)))
            got = events()[:len(wanted)]
            check(got == ['STANDIN ' + w for w in wanted], 'the client saw %s, expected %s' % (got, list(wanted)))
            # Past the last line matched.
            lines = printed().splitlines()
            count, i = 0, mark[0]
            while count < len(wanted):
                if lines[i] == 'STANDIN ' + wanted[count]:
                    count += 1
                i += 1
            mark[0] = i

        # Keys: the window takes the keyboard; Shift is a modifier the
        # client is told of -- Blit's own state's, on `INPUT modifiers` after
        # each key as Blit sends it (ADR 0231), told only when it changes.
        lines_to('key 1 42 press', 'modifiers 1 1 0 0 0', 'key 1 30 press', 'modifiers 1 1 0 0 0',
                 'key 1 30 release', 'modifiers 1 1 0 0 0', 'key 1 42 release', 'modifiers 1 0 0 0 0')
        seen('keyboard enter main', 'modifiers 0 0 0 0', 'key 42 1', 'modifiers 1 0 0 0', 'key 30 1',
             'key 30 0', 'key 42 0', 'modifiers 0 0 0 0')
        # The pointer: the toplevel, then the popup over it (at 12,63), a
        # press there keeping it while the pointer leaves it, the wheel.
        lines_to('ptr 1 10.50 20.25')
        seen('pointer enter main 10.50 20.25', 'pointer frame')
        lines_to('ptr 1 20.00 70.00')
        seen('pointer leave main', 'pointer frame', 'pointer enter popup 8.00 7.00', 'pointer frame')
        lines_to('btn 1 272 press', 'ptr 1 150.00 150.00', 'btn 1 272 release', 'ptr 1 30.00 30.00')
        seen('button 272 1', 'pointer frame', 'pointer motion 138.00 87.00', 'pointer frame',
             'button 272 0', 'pointer frame', 'pointer leave popup', 'pointer frame',
             'pointer enter main 30.00 30.00', 'pointer frame')
        lines_to('wheel 1 0 1')
        seen('axis source 0', 'axis120 0 120', 'axis 0 15.00', 'pointer frame')
        # Relative motion (Blit's relative hold) to the client with the pointer.
        lines_to('rel 1 7 -3', 'rel 1 -2 5')
        seen('relative 7.00 -3.00', 'pointer frame', 'relative -2.00 5.00', 'pointer frame')
        e2e = popup_end_to_end(desk, work, seen, keyboards, printed, mark)
        # Blit's route ended with its channel: the keyboard left with it.
        # Back by a key on the gate's route, so `!activated` below is what
        # takes it away.
        seen('keyboard leave main')
        lines_to('key 1 31 press', 'key 1 31 release')
        # Blit's Num Lock from the part above stays: a lock is Blit's.
        seen('keyboard enter main', 'modifiers 0 0 16 0', 'key 31 1', 'key 31 0')
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')
        # The desktop takes the window's focus: the keyboard leaves it.
        desk.send('WINDOW size 1 200 100')
        desk.frame(200, 100)
        seen('keyboard leave main')
        lines_to('key 1 31 press')
        seen('keyboard enter main', 'modifiers 0 0 16 0', 'key 31 1')
        # Blit's route ends with the key down: it is released, the
        # keyboard leaves.
        route.close()
        seen('key 31 0', 'keyboard leave main')
        # The window goes with keys still down while its client stays (an
        # X terminal whose shell exits on Ctrl-D, under Xwayland): the
        # stand-in destroys it on Ctrl+D. The keyboard leaves it as at any
        # focus change -- every key down released to the client, the
        # modifiers cleared, then the leave -- so no key stays pressed for
        # the client's next window (Xwayland repeated a stuck Ctrl-D into
        # every X window focused after one closed that way).
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        lines_to('key 1 29 press', 'modifiers 1 4 0 0 0', 'key 1 32 press', 'modifiers 1 4 0 0 0')
        seen('keyboard enter main', 'modifiers 0 0 16 0', 'key 29 1', 'modifiers 4 0 0 0', 'key 32 1',
             'key 29 0', 'key 32 0', 'modifiers 0 0 0 0', 'keyboard leave none')
        desk.line('WINDOW gone 1')
        check(client.wait(5) == 0 and 'STANDIN gone' in printed(), 'the input stand-in failed:\n' + printed())
        route.close()
        quit_compositor(desk, comp)
        text = open(log).read()
        check('keymap: %d bytes from Blit' % len(keymap) in text, 'the keymap was not installed:\n' + text)
        # Installed for the gate's route, then for the switch to Arabic and
        # back (ADR 0231 step 2): the keymap is the current keyboard's, one
        # for every device, so the real route's first, the repeat PATCH's
        # and each device's turn change only the repeat.
        installs, same = text.count('bytes from Blit, revision'), text.count(', unchanged')
        check(installs == 3 and same >= 4,
              '%d keymaps installed, %d unchanged:\n%s' % (installs, same, text))
        return ('keymap=%dB from Blit keys+shift modifiers ptr=enter/motion/popup/grab wheel=120 '
                'relative=7,-3+-2,5 focus=line,!activated,route-end,gone-with-keys-down->released %s'
                % (len(keymap), e2e))
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()
        blit.terminate()
        try:
            blit.wait(5)
        except subprocess.TimeoutExpired:
            blit.kill()


def popup_tree_part(compositor, standin, work):
    """Popups as Firefox draws a menu (docs/api.md, "Subsurfaces"), with
    `--popup-tree`: window 1, its popup 2 at 12,63, the popup's contents a
    subsurface of it (picture 3: `WINDOW sub 3 2 5 5 70 50 above`, the tree
    under a popup flattened onto the popup), and a popup of the popup -- a
    submenu -- (`WINDOW popup 4 2 60 10`) that grabs. The pointer over the
    contents reaches the subsurface and over the submenu the submenu, in
    the top-level's pixels on the top-level's route; a key goes to the
    grabbing submenu. Before 0.3.92 the contents were never shown
    ("a popup's, not shown in this release") and the menu opened empty."""
    runtime = os.path.join(work, 'run-tree')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-tree-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    comp = client = route = None
    log = os.path.join(work, 'compositor-tree.log')
    out_path = os.path.join(work, 'standin-tree.out')
    printed = lambda: open(out_path).read()
    try:
        cenv = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN, XDG_RUNTIME_DIR=runtime)
        for k in ('WAYLAND_DISPLAY', 'BLIT_SERVER_PORT', 'BLIT_API_TOKEN'):
            cenv.pop(k, None)
        comp = start([compositor, '--x11', 'off', '--log', log], cenv,
                     open(os.path.join(work, 'compositor-tree.out'), 'w'))
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        client = start([standin, '--popup-tree', 'Tree'], dict(cenv, WAYLAND_DISPLAY='blit-0'),
                       open(out_path, 'w'))
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Tree', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.frame(256, 160)
        desk.send('WINDOW size 1 200 100 activated')
        desk.frame(200, 100)
        # The popup, then its contents and the submenu, in whatever order the
        # client's commits bring them: every line and frame of each.
        lines, frames = [], {}
        deadline = time.time() + 8
        wanted = ('WINDOW popup 2 1 12 63', 'WINDOW sub 3 2 5 5 70 50 above', 'WINDOW popup 4 2 60 10')
        while time.time() < deadline and not (all(w in lines for w in wanted) and
                                               set(frames) >= {2, 3, 4}):
            got = desk.next(0.5)
            if got is None:
                continue
            if got[0] == 'line':
                lines.append(got[1])
            elif got[0] == 'frame':
                (image, _seq, w, h) = got[1][:4]
                frames[image] = (w, h)
                os.close(got[2])
        check(all(w in lines for w in wanted),
              lambda: 'the popup tree\'s lines: %s, expected %s\n%s' % (lines, wanted, open(log).read()))
        check(frames.get(2) == (80, 60) and frames.get(3) == (70, 50) and frames.get(4) == (40, 30),
              'the popup tree\'s frames: %s' % frames)
        check('not shown' not in open(log).read(), 'a popup\'s subsurface was not shown:\n' + open(log).read())
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        frame = [0]

        def send(*events):
            text = ''
            for event in events:
                frame[0] += 1
                text += 'INPUT %s\nINPUT frame %d %d\n' % (event % BLIT_PICTURE, BLIT_PICTURE, frame[0])
            route.sendall(text.encode())

        def saw(*wanted):
            want = ['STANDIN ' + w for w in wanted]
            wait_for(lambda: all(w in printed().splitlines() for w in want),
                     lambda: 'the tree client saw none of %s:\n%s' % (want, printed()))

        # Over the popup's contents: the subsurface, in its own pixels (the
        # popup at 12,63, its contents at 5,5 of it).
        send('ptr %d 20.00 70.00')
        saw('pointer enter popsub 3.00 2.00')
        # Over the submenu (60,10 of the popup): the submenu, over the menu.
        send('ptr %d 80.00 80.00')
        saw('pointer enter popup2 8.00 7.00')
        # A key: the grabbing submenu has the keyboard.
        send('key %d 30 press', 'key %d 30 release')
        saw('keyboard enter popup2')
        desk.send('WINDOW close 1')
    finally:
        if route: route.close()
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()
    return 'popup-contents=sub-of-popup(WINDOW sub 3 2) submenu=popup-of-popup(WINDOW popup 4 2) pointer=popsub,popup2 keys=grab'


def lock_part(compositor, standin, work):
    """zwp_pointer_constraints_v1 asked of the desktop (docs/api.md, "Pointer
    lock"), with `--lock`: a oneshot lock pending until `WINDOW lock 1 on`
    (an `off` before it, no gesture yet, leaves it pending), then locked,
    relative motion and no absolute motion, `off` -> unlocked and defunct
    (a second `on` does nothing, and its destroy says nothing); a
    persistent lock locked again after `off`, its destroy `WINDOW lock 1
    off`; a persistent confinement whose pointer moves; and the window going
    with it still there: `WINDOW lock 1 off`, then `WINDOW gone 1`. The gate
    plays the desktop and Blit's route on the input socket."""
    runtime = os.path.join(work, 'run-lock')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-lock-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    env = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('BLIT_SERVER_PORT', None)
    log = os.path.join(work, 'compositor-lock.log')
    comp = start([compositor, '--x11', 'off', '--log', log], env, open(os.path.join(work, 'compositor-lock.out'), 'w'))
    client = None
    try:
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        out_path = os.path.join(work, 'standin-lock.out')
        client = start([standin, '--lock', 'Lock'], dict(env, WAYLAND_DISPLAY='blit-0'), open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        count = lambda what: printed().splitlines().count('STANDIN ' + what)
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Lock', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.frame(256, 160)
        desk.send('WINDOW size 1 120 80 activated')
        desk.frame(120, 80)
        desk.line('WINDOW lock 1 relative')
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        frame = [0]

        def lines_to(*lines):
            text = ''
            for line in lines:
                frame[0] += 1
                text += 'INPUT %s\nINPUT frame 1 %d\n' % (line, frame[0])
            route.sendall(text.encode())

        def settle(what, n):
            wait_for(lambda: count(what) >= n, lambda: 'no STANDIN %s #%d:\n%s' % (what, n, printed()))

        lines_to('ptr 1 10.00 10.00')
        settle('pointer enter main 10.00 10.00', 1)
        # Pending: no gesture yet, the desktop says off; nothing activates.
        desk.send('WINDOW lock 1 off')
        lines_to('rel 1 3 4')
        settle('relative 3.00 4.00', 1)
        desk.nothing(0.3)
        check(count('locked') == 0 and count('unlocked') == 0,
              lambda: 'a pending lock was activated or deactivated:\n' + printed())
        # On: locked; relative motion, and an absolute position is dropped.
        desk.send('WINDOW lock 1 on')
        settle('locked', 1)
        lines_to('ptr 1 20.00 20.00', 'rel 1 5 -2')
        settle('relative 5.00 -2.00', 1)
        check(count('pointer motion 20.00 20.00') == 0, lambda: 'a locked pointer moved:\n' + printed())
        # Off: unlocked, and the oneshot is defunct: on again does nothing.
        desk.send('WINDOW lock 1 off')
        settle('unlocked', 1)
        desk.send('WINDOW lock 1 on')
        lines_to('ptr 1 30.00 30.00')
        settle('pointer motion 30.00 30.00', 1)
        check(count('locked') == 1, lambda: 'a defunct oneshot lock was locked again:\n' + printed())
        # Space: the defunct oneshot destroyed (nothing said), a persistent lock.
        lines_to('key 1 57 press', 'key 1 57 release')
        settle('lock persistent', 1)
        desk.line('WINDOW lock 1 relative')
        desk.send('WINDOW lock 1 on')
        settle('locked', 2)
        desk.send('WINDOW lock 1 off')
        settle('unlocked', 2)
        desk.send('WINDOW lock 1 on')
        settle('locked', 3)
        # Space: the persistent lock destroyed, `off`; a confinement asked for.
        lines_to('key 1 57 press', 'key 1 57 release')
        desk.line('WINDOW lock 1 off')
        desk.line('WINDOW lock 1 confine')
        settle('confine persistent', 1)
        desk.send('WINDOW lock 1 on')
        settle('confined', 1)
        lines_to('ptr 1 40.00 40.00')
        settle('pointer motion 40.00 40.00', 1)
        desk.send('WINDOW lock 1 off')
        settle('unconfined', 1)
        desk.send('WINDOW lock 1 on')
        settle('confined', 2)
        # The window goes with the confinement still there.
        desk.send('WINDOW close 1')
        desk.line('WINDOW lock 1 off')
        desk.line('WINDOW gone 1')
        desk.nothing(0.3)
        check(client.wait(5) == 0, 'the lock stand-in failed:\n' + printed())
        check(count('global zwp_pointer_constraints_v1 1') == 1, 'no pointer constraints 1:\n' + printed())
        tally = dict((w, count(w)) for w in ('locked', 'unlocked', 'confined', 'unconfined'))
        check(tally == {'locked': 3, 'unlocked': 2, 'confined': 2, 'unconfined': 1},
              'the constraint events: %s\n%s' % (tally, printed()))
        route.close()
        quit_compositor(desk, comp)
        return ('oneshot=pending(off ignored)->on:locked+rel,ptr-dropped->off:unlocked,defunct '
                'persistent=on/off/on,destroy->off confine=on+motion/off/on gone=off->gone')
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


class _Blit:
    """frame_input_gate's Session wants an object with api()."""
    api = staticmethod(lambda method, path, body=None: api(method, path, body))


def popup_end_to_end(desk, work, seen, keyboards, printed, mark):
    """A real Blit's route into the compositor's input socket, as a desktop
    sets it up (docs/api.md, "One input route per top-level"): the gate,
    as the desktop, shows windows 1 and 2 (the stand-in's toplevel and its
    popup) as pictures 41 and 42 of a Blit session -- the desktop's ids, as
    Hibr's are not the compositor's -- asks `INPUT sock` for 41 with window
    1's socket and `INPUT member 42 41 12 63`, and turns the route on; the
    pointer over picture 42 reaches the popup surface at its own pixels.

    Then the keyboard (milestone C): a repeat PATCH of keyboard A while the
    route is live reaches the client as repeat info (Blit's `INPUT keymap`
    with the same keymap); a key from keyboard B makes its repeat the
    client's before that key, one from A, A's again -- the keymap is the
    system's, one for both (ADR 0231), so it is not installed again. The
    modifiers are Blit's (`INPUT modifiers`): the keyboards' Num Lock, on
    until their LEDs say otherwise, reaches the client as a lock. The
    keymap comes only on Blit's route."""
    session = fig.Session(_Blit, work, 'e2e')
    api('PATCH', '/v1/terminal-sessions/%s' % session.id, {'focused': True})
    wait_for(lambda: api('GET', '/v1/terminal-sessions/%s' % session.id)['focused'], 'no focus')
    ch = fig.Channel(session)
    ch.frame(41, 1, 200, 100)
    ch.frame(42, 1, 80, 60)
    wait_for(lambda: session.graphics(41) and session.graphics(42), 'pictures 41 and 42 did not land')
    fig.place(session, 41, 3, 5, 20, 5)
    session.write('\033[4;8H\033_Ga=p,i=42,p=1,q=2,C=1,c=8,r=3,z=1\033\\')
    at = wait_for(lambda: [p for p in (session.graphics(42) or {}).get('placements', []) if p['id'] == 1],
                  'picture 42 was not placed')[0]
    check(ch.ask('INPUT sock 41 %s\n' % desk.input_socks[1]) == b'INPUT ok 41\n', 'Blit took no route')
    check(ch.ask('INPUT member 42 41 12 63\n') == b'INPUT ok 42\n', 'Blit took no member')
    ch.line('INPUT on 41\n')
    wait_for(lambda: [r for r in api('GET', '/v1/terminal-sessions/%s' % session.id)['frames']['input']['routes']
                      if r['image'] == 41 and r['state'] == 'live'], 'the route did not go live')
    # The session's surface origin on the display, from its pixel reports.
    api('POST', '/v1/input/inject', {'kind': 'pointer_motion', 'x': 301, 'y': 101})
    report = wait_for(lambda: __import__('re').findall(rb'\x1b\[<35;(\d+);(\d+)M', session.sent()),
                      'no pixel report')[-1]
    origin = (301 - (int(report[0]) - 1), 101 - (int(report[1]) - 1))
    sx, sy = 8, 8
    api('POST', '/v1/input/inject', {'kind': 'pointer_motion', 'x': origin[0] + at['x'] + sx,
                                     'y': origin[1] + at['y'] + sy})
    x, y = sx * 80 / at['width'], sy * 60 / at['height']
    # Blit's modifiers came with its keymap as the route went live: the
    # keyboards' Num Lock (Mod2), on until their LEDs say otherwise.
    seen('modifiers 0 0 16 0', 'pointer leave main', 'pointer frame',
         'pointer enter popup %.2f %.2f' % (x, y), 'pointer frame')
    api('POST', '/v1/input/inject', {'kind': 'pointer_motion', 'x': origin[0] + at['x'] + 2 * sx,
                                     'y': origin[1] + at['y'] + sy // 2})
    seen('pointer motion %.2f %.2f' % (2 * x, y / 2), 'pointer frame')

    # Repeat changed while the route is live: the same keymap, new repeat.
    api('PATCH', '/v1/input/devices/%s' % keyboards[0],
        {'configuration': {'repeat_delay_ms': 400, 'repeat_rate_hz': 20}})
    wait_for(lambda: 'STANDIN repeat 20 400' in printed(), lambda: 'no new repeat:\n' + printed())
    texts = [urllib.request.urlopen(BASE + '/v1/input/keymap?device=%s' % k, timeout=10).read()
             for k in keyboards]
    check(texts[0] == texts[1], 'the two keyboards have two keymaps')
    order = []
    for device, text, repeat in ((keyboards[1], texts[1], 'repeat 20 500'),
                                 (keyboards[0], texts[0], 'repeat 20 400')):
        before = len(printed().splitlines())
        for value in (1, 0):
            api('POST', '/v1/input/inject', {'kind': 'key', 'code': 16, 'value': value, 'device_id': device})
        wait_for(lambda: 'STANDIN key 16 0' in printed().splitlines()[before:],
                 lambda: 'no key from %s:\n%s' % (device, printed()))
        lines = printed().splitlines()[before:]
        want = ['STANDIN ' + repeat, 'STANDIN key 16 1']
        got = [l for l in lines if l in want]
        check(got == want, 'a key from %s: %s, expected %s in that order:\n%s'
              % (device, got, want, '\n'.join(lines)))
        check(not [l for l in lines if l.startswith('STANDIN keymap ')],
              'the one keymap was installed again for %s:\n%s' % (device, '\n'.join(lines)))
        # Num Lock (Mod2) locked, from Blit's state.
        check([l for l in printed().splitlines() if l == 'STANDIN modifiers 0 0 16 0'],
              'Blit\'s Num Lock did not reach the client:\n' + printed())
        order.append('%dB' % len(text))
    # ADR 0231 step 2: another keyboard made current reaches the client as
    # its keymap, and the first again as the first's.
    before = len(printed().splitlines())
    api('PUT', '/v1/input/keyboards', {'keyboards': [{'layout': 'us'}, {'layout': 'ara', 'variant': 'mac-phonetic'}],
                                       'current': 1})
    arabic = urllib.request.urlopen(BASE + '/v1/input/keymap', timeout=10).read()
    check(arabic != texts[0], 'the Arabic keyboard has the US keymap')
    wait_for(lambda: 'STANDIN keymap format=1 size=%d xkb=1' % (len(arabic) + 1) in printed().splitlines()[before:],
             lambda: 'the switch to Arabic did not reach the client:\n' + printed())
    before = len(printed().splitlines())
    api('PUT', '/v1/input/keyboards', {'current': 0})
    wait_for(lambda: 'STANDIN keymap format=1 size=%d xkb=1' % (len(texts[0]) + 1) in printed().splitlines()[before:],
             lambda: 'the switch back to US did not reach the client:\n' + printed())
    order.append('switch=ara:%dB,us' % len(arabic))
    mark[0] = len(printed().splitlines())
    ch.line('INPUT off 41\n')
    ch.close()
    api('DELETE', '/v1/terminal-sessions/%s' % session.id)
    return ('blit-member-e2e=popup@%.2f,%.2f repeat=blit:30/250,patch->20/400 '
            'keyboard-switch=B:%s+20/500,A:%s+20/400 before-the-key one-keymap numlock=blit %s'
            % (x, y, order[0], order[1], order[2]))


def dmabuf_part(compositor, standin, work):
    """linux-dmabuf against a desktop that answers dmabuf=1 with a formats=
    list: each commit an FRM3 of the client's own plane (the same file, no
    copy), its buffer released only at Blit's `FRAME release`, given back
    when its window goes; `FRAME error <id> ENOTSUP` copies that frame as
    FRM1 and every later one. This host has no GPU: the stand-in's planes
    are memfds."""
    runtime = os.path.join(work, 'run-dmabuf')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-dmabuf-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    env = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    env.pop('WAYLAND_DISPLAY', None)
    log = os.path.join(work, 'compositor-dmabuf.log')
    comp = start([compositor, '--x11', 'off', '--log', log], env, open(os.path.join(work, 'compositor-dmabuf.out'), 'w'))
    clients = []
    try:
        desk = Desktop(listener, b'OK dmabuf=1 fence=0 formats=XR24/0,AR24/0 window=1\n')
        desk.line('WINDOW display blit-0 -')
        cenv = dict(env, WAYLAND_DISPLAY='blit-0')
        out_path = os.path.join(work, 'standin-dmabuf.out')
        client = start([standin, '--dmabuf', 'Dmabuf'], cenv, open(out_path, 'w'))
        clients.append(client)
        printed = lambda: open(out_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Dmabuf', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        # Its own size first, at the initial commit's configure; Blit shows
        # and releases it, and the cases below start from there.
        seq0, _ = desk.frm3(256, 160, 1)
        desk.send('FRAME release 1 %d' % seq0)
        wait_for(lambda: 'STANDIN release 256x160' in printed(), 'FRAME release did not release the first buffer')
        desk.send('WINDOW size 1 64 32')
        seq1, file1 = desk.frm3(64, 32, 1)
        check('STANDIN dmabuf 64x32 file %d:%d' % file1 in printed(),
              lambda: 'the FRM3 plane is not the client\'s own file %s:\n%s' % (file1, printed()))
        desk.send('WINDOW size 1 80 40')
        seq2, file2 = desk.frm3(80, 40, 1)
        check(seq2 > seq1 and file2 != file1, 'the second FRM3: sequence %d file %s' % (seq2, file2))
        desk.nothing(0.3)
        check('STANDIN release 64x32' not in printed() and 'STANDIN release 80x40' not in printed(),
              lambda: 'released before FRAME release:\n' + printed())
        # The version 4 forms are no line of version 5: they release nothing.
        desk.send('RELEASE 1 %d' % seq1)
        desk.send('ERROR 1 ENOTSUP')
        desk.nothing(0.3)
        check('STANDIN release 64x32' not in printed() and 'STANDIN release 80x40' not in printed(),
              lambda: 'a version 4 line released a buffer:\n' + printed())
        desk.send('FRAME release 1 %d' % seq1)
        wait_for(lambda: 'STANDIN release 64x32' in printed(), 'FRAME release did not release the buffer')
        check('STANDIN release 80x40' not in printed(), 'the held 80x40 buffer was released too')

        # A window that goes while Blit holds its frame gives the buffer back.
        held_path = os.path.join(work, 'standin-held.out')
        held = start([standin, '--dmabuf', 'Held'], cenv, open(held_path, 'w'))
        clients.append(held)
        for want in ('WINDOW new 2 0', 'WINDOW title 2 Held', 'WINDOW appid 2 blit.standin',
                     'WINDOW limits 2 64 48 0 0', 'WINDOW decor 2 server'):
            desk.line(want)
        seq0, _ = desk.frm3(256, 160, 2)
        desk.send('FRAME release 2 %d' % seq0)
        wait_for(lambda: 'STANDIN release 256x160' in open(held_path).read(),
                 'FRAME release did not release the held client\'s first buffer')
        desk.send('WINDOW size 2 30 20')
        desk.frm3(30, 20, 2)
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')
        check(held.wait(5) == 0, 'the held stand-in failed: ' + open(held_path).read())
        # Every buffer back, the held 30x20 at WINDOW gone. (Whether its frame
        # callback is read before the client acts on close is the client's
        # race, not the compositor's: frames is not counted here.)
        check('STANDIN release 30x20' in open(held_path).read() and
              'STANDIN done draws=2 releases=2 frames=' in open(held_path).read(),
              'the held buffer was not given back at WINDOW gone:\n' + open(held_path).read())

        # A dmabuf client drawing on every callback is one frame ahead of
        # Blit: its third frame waits for Blit's FRAME release of its first.
        paced_path = os.path.join(work, 'standin-paced.out')
        paced = start([standin, '--dmabuf', '--animate', '2', 'Paced'], cenv, open(paced_path, 'w'))
        clients.append(paced)
        for want in ('WINDOW new 3 0', 'WINDOW title 3 Paced', 'WINDOW appid 3 blit.standin',
                     'WINDOW limits 3 64 48 0 0', 'WINDOW decor 3 server'):
            desk.line(want)
        seq0, _ = desk.frm3(256, 160, 3)
        desk.send('FRAME release 3 %d' % seq0)
        wait_for(lambda: 'STANDIN release 256x160' in open(paced_path).read(),
                 'FRAME release did not release the paced client\'s first buffer')
        desk.send('WINDOW size 3 40 30')
        seq_a, _ = desk.frm3(40, 30, 3)
        seq_b, _ = desk.frm3(40, 30, 3)
        desk.nothing(0.4)
        desk.send('FRAME release 3 %d' % seq_a)
        seq_c, _ = desk.frm3(40, 30, 3)
        # FRAME release of the second takes the third: its callback and feedback
        # come, and the client, done animating, draws no more.
        desk.send('FRAME release 3 %d' % seq_b)
        desk.nothing(0.3)
        desk.send('WINDOW close 3')
        desk.line('WINDOW gone 3')
        check(paced.wait(5) == 0, 'the paced stand-in failed: ' + open(paced_path).read())
        text = open(paced_path).read()
        check('STANDIN done draws=4 releases=4 frames=4' in text and
              'STANDIN presentation presented=4 discarded=0 ' in text,
              'the paced dmabuf client:\n' + text)

        # ENOTSUP: the refused (newest held) frame again as FRM1, and every
        # later one copied.
        desk.send('FRAME error 1 ENOTSUP')
        desk.frame(80, 40)
        wait_for(lambda: 'STANDIN release 80x40' in printed(), 'the refused buffer was not released')
        desk.send('WINDOW size 1 100 50')
        desk.frame(100, 50)
        wait_for(lambda: 'STANDIN release 100x50' in printed(), 'the copied buffer was not released')
        desk.send('FRAME release 1 %d' % seq2)  # stale: nothing
        desk.nothing(0.3)
        desk.send('WINDOW close 1')
        desk.line('WINDOW gone 1')
        check(client.wait(5) == 0, 'the dmabuf stand-in failed: ' + printed())
        output = printed()
        for want in ('STANDIN global zwp_linux_dmabuf_v1 4',
                     'STANDIN dmabuf feedback pairs=2 tranche=2 xr24-linear=1',
                     'STANDIN done draws=4 releases=4 frames=4'):
            check(want in output, 'the dmabuf stand-in did not print %r:\n%s' % (want, output))
        quit_compositor(desk, comp)
        text = open(log).read()
        check('dmabuf 9 sent, 6 released, 1 refused, 2 copied' in text,
              'the log does not count the dmabuf frames:\n' + text)
        check(text.count('an unknown line from the channel: RELEASE 1 ') == 1 and
              text.count('an unknown line from the channel: ERROR 1 ENOTSUP') == 1,
              'the version 4 lines were not logged as unknown:\n' + text)
        return ('FRM3=client-file,64x32+80x40 FRAME_release->release v4_RELEASE+ERROR=unknown,released_nothing '
                'gone->release paced=3rd-after-FRAME_release-of-1st '
                'ENOTSUP->FRM1 80x40,100x50 feedback=2')
    finally:
        for p in clients + [comp]:
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


def quit_compositor(desk, comp):
    """The person quits the desktop: `WINDOW quit 0`, and the compositor
    ends at once, status 0, with no reconnect."""
    desk.send('WINDOW quit 0')
    try:
        status = comp.wait(5)
    except subprocess.TimeoutExpired:
        fail('blit-compositor did not end at WINDOW quit 0')
    check(status == 0, 'blit-compositor exited %d at WINDOW quit 0' % status)
    desk.sock.close()


def listen(name):
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    return listener


def sub_restart_part(compositor, standin, work):
    """Subsurfaces survive a desktop restart: a top-level with a subsurface
    shown, the channel closed and served again; the re-announcement is the
    top-level's lines, then `WINDOW sub` with the same id, offset, size and
    side, then both newest frames (the subsurface's too)."""
    runtime = os.path.join(work, 'run-sub-restart')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-sub-restart-%d' % os.getpid()
    listener = listen(name)
    env = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    for k in ('WAYLAND_DISPLAY', 'BLIT_COMPOSITOR_RECONNECT_MS'):
        env.pop(k, None)
    log = os.path.join(work, 'compositor-sub-restart.log')
    comp = start([compositor, '--x11', 'off', '--log', log], env, open(os.path.join(work, 'compositor-sub-restart.out'), 'w'))
    client = None
    try:
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        out_path = os.path.join(work, 'standin-sub-restart.out')
        client = start([standin, '--sub', '--clip', 'Subbed'], dict(env, WAYLAND_DISPLAY='blit-0'),
                       open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        hints = ('WINDOW new 1 0', 'WINDOW title 1 Subbed', 'WINDOW appid 1 blit.standin',
                 'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server')
        for want in hints:
            desk.line(want)
        desk.frame(256, 160)
        desk.send('WINDOW size 1 200 120')
        desk.frame(200, 120)
        desk.line('WINDOW sub 2 1 20 30 64 40 above')
        desk.frame(64, 40, image_id=2)
        wait_for(lambda: 'STANDIN sub step 1' in printed(), lambda: 'no sub step 1:\n' + printed())
        # Its copy, and the desktop's offer of the primary (which lapses).
        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        route.sendall(b'INPUT key %d 46 press\nINPUT frame %d 1\n' % (BLIT_PICTURE, BLIT_PICTURE))
        desk.line('CLIP offer 1 clipboard text/plain;charset=utf-8')
        desk.send('CLIP offer 5 primary text/plain;charset=utf-8')
        wait_for(lambda: 'STANDIN selection primary text/plain;charset=utf-8' in printed(),
                 lambda: 'no primary from the desktop:\n' + printed())
        # Restart Desktop (the route kept: the program keeps the keyboard): the channel and its socket go, then come back.
        desk.sock.close()
        listener.close()
        time.sleep(0.5)
        listener = listen(name)
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        for want in hints:
            desk.line(want)
        desk.line('WINDOW sub 2 1 20 30 64 40 above')
        desk.frame(200, 120)
        desk.frame(64, 40, image_id=2)
        # The program's selection offered to the new desktop, the old
        # desktop's primary gone from the program.
        desk.line('CLIP offer 2 clipboard text/plain;charset=utf-8')
        desk.nothing(0.3)
        check(printed().splitlines().count('STANDIN selection primary none') >= 2,
              lambda: 'the lost desktop\'s primary was not withdrawn:\n' + printed())
        route.close()
        quit_compositor(desk, comp)
        client.wait(5)
        text = open(log).read()
        check('reconnect: 2 windows announced again, 2 frames sent again' in text,
              'the re-announcement in the log:\n' + text)
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()
    return ('reconnect=new+hints,WINDOW-sub-same-id,frames(top-level,sub),'
            'program-selection-offered-again,desktop-offer-lapsed')


def restart_part(compositor, standin, work):
    """docs/api.md, "Programs survive a desktop restart". The desktop picks
    the display (`--display`); a dmabuf program (window 1, drawing on every
    callback, so one frame ahead of Blit and stalled until a FRAME release) and a
    wl_shm one (window 2, drawing 60 frames on its callbacks, and asking for
    a pointer lock the desktop has not granted) are shown; the channel
    closes. Both keep drawing at the output's refresh with
    nothing shown, each dmabuf buffer given back at once but the newest; a
    third program starts meanwhile. The fake desktop serves the same socket
    again after its programs are done drawing (about a second): the
    compositor says hello, `WINDOW display`, announces windows 1, 2 and 3
    with their hints and input routes, then the newest frame of 1 (FRM3) and
    of 2 (FRM1) and of 3, each the program's pattern at its size, then the lock
    asked for again. The late program was configured at its initial commit
    (0x0: no desktop has sized it), so its own 256x160 is shown again too.
    No window is configured again until the desktop's new `WINDOW size`,
    which brings a frame of that size. `WINDOW quit 0` ends it at once, status 0, its programs
    gone. A second compositor, whose desktop never comes back (only a
    peer answering without window=1), ends after the bound, status 1. Names
    in use or invalid are refused, status 2."""
    runtime = os.path.join(work, 'run-restart')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-restart-%d' % os.getpid()
    display = 'gate-display'
    answer = b'OK dmabuf=1 fence=0 formats=XR24/0,AR24/0 window=1\n'
    listener = listen(name)
    env = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    for k in ('WAYLAND_DISPLAY', 'BLIT_SERVER_PORT', 'BLIT_COMPOSITOR_RECONNECT_MS'):
        env.pop(k, None)
    log = os.path.join(work, 'compositor-restart.log')
    comp = start([compositor, '--x11', 'off', '--display', display, '--log', log], env,
                 open(os.path.join(work, 'compositor-restart.out'), 'w'))
    clients = []
    comp2 = None
    try:
        desk = Desktop(listener, answer)
        desk.line('WINDOW display %s -' % display)
        check(os.path.exists(os.path.join(runtime, display)) and
              not [f for f in os.listdir(runtime) if '-input' in f],
              'no %s, or an input socket before any window, in XDG_RUNTIME_DIR: %s'
              % (display, os.listdir(runtime)))
        # A display in use, or one that is not a name, is refused before the
        # channel hears of it.
        refused = []
        for label, arg, why in (('taken', display, 'is in use'), ('slash', 'a/b', 'has a slash'),
                                ('long', 'x' * 32, 'is longer than 31 bytes'),
                                ('space', 'a b', 'has a space'), ('empty', '', 'is empty')):
            r = subprocess.run([compositor, '--x11', 'off', '--display', arg], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=10)
            check(r.returncode == 2 and why in r.stderr.decode(),
                  '--display %r: status %d, %r' % (arg, r.returncode, r.stderr))
            refused.append(label)
        # --x11 takes on or off and nothing else.
        for arg in ('maybe', 'ON', ''):
            r = subprocess.run([compositor, '--x11', arg], env=env, stdout=subprocess.PIPE,
                               stderr=subprocess.PIPE, timeout=10)
            check(r.returncode == 2 and b'--x11 on|off' in r.stderr,
                  '--x11 %r: status %d, %r' % (arg, r.returncode, r.stderr))
        refused.append('x11-value')
        check('X: X programs are off: turned off (--x11 off' in open(log).read(),
              lambda: 'the log does not say --x11 off turned X off:\n' + open(log).read())
        cenv = dict(env, WAYLAND_DISPLAY=display)

        held_path = os.path.join(work, 'standin-restart-held.out')
        clients.append(start([standin, '--dmabuf', '--animate', '60', 'Held'], cenv, open(held_path, 'w')))
        held = lambda: open(held_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Held', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        seq0, _ = desk.frm3(256, 160, 1)
        desk.send('FRAME release 1 %d' % seq0)
        wait_for(lambda: 'STANDIN release 256x160' in held(), 'FRAME release did not release the first buffer')
        desk.send('WINDOW size 1 64 32')
        desk.frm3(64, 32, 1)
        desk.frm3(64, 32, 1)
        desk.nothing(0.3)  # one frame ahead of Blit: no FRAME release, no third
        kept_path = os.path.join(work, 'standin-restart-kept.out')
        clients.append(start([standin, '--animate', '60', '--lock', 'Kept'], cenv,
                             open(kept_path, 'w')))
        kept = lambda: open(kept_path).read()
        for want in ('WINDOW new 2 0', 'WINDOW title 2 Kept', 'WINDOW appid 2 blit.standin',
                     'WINDOW limits 2 64 48 0 0', 'WINDOW decor 2 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=2)
        desk.send('WINDOW size 2 160 100')
        desk.frame(160, 100, image_id=2)
        desk.line('WINDOW lock 2 relative')
        check(sorted(desk.input_socks) == [1, 2], 'INPUT sock before: %s' % desk.input_socks)

        # The desktop goes (Restart Desktop): its channel and its socket.
        desk.sock.close()
        listener.close()
        lost = time.time()
        wait_for(lambda: 'STANDIN animated 60 in' in kept(),
                 lambda: 'the wl_shm program stopped drawing with the channel down:\n' + kept())
        wait_for(lambda: 'STANDIN animated 60 in' in held(),
                 lambda: 'the dmabuf program stopped drawing with the channel down:\n' + held())
        ms = int(kept().split('STANDIN animated 60 in ')[1].split()[0])
        check(ms >= 59 * 1000 / 60 * 0.85, 'sixty frame callbacks in %d ms: not at the refresh' % ms)
        wait_for(lambda: held().count('STANDIN release 64x32') >= 60,
                 lambda: 'the dmabuf buffers were not given back with the channel down:\n' + held())
        time.sleep(0.2)
        draws = held().count('STANDIN dmabuf 64x32')
        releases = held().count('STANDIN release 64x32')
        check(draws == 61 and releases == 60,
              'the dmabuf program drew %d and was given back %d with the channel down:\n%s'
              % (draws, releases, held()))
        check(comp.poll() is None, 'blit-compositor ended with the channel')
        check(os.path.exists(os.path.join(runtime, display)) and
              os.path.exists(os.path.join(runtime, display + '-input-1')) and
              os.path.exists(os.path.join(runtime, display + '-input-2')),
              'the Wayland or an input socket went with the channel: %s' % os.listdir(runtime))
        # A program started meanwhile: announced on the new channel.
        late_path = os.path.join(work, 'standin-restart-late.out')
        clients.append(start([standin, 'Late'], cenv, open(late_path, 'w')))
        late = lambda: open(late_path).read()
        wait_for(lambda: 'committed, waiting' in late(), 'the late program did not start')
        time.sleep(max(0.0, 1.0 - (time.time() - lost)))

        # The desktop again, on the same socket.
        listener = listen(name)
        desk = Desktop(listener, answer)
        back = time.time() - lost
        desk.line('WINDOW display %s -' % display)
        for image, title in ((1, 'Held'), (2, 'Kept'), (3, 'Late')):
            for want in ('WINDOW new %d 0' % image, 'WINDOW title %d %s' % (image, title),
                         'WINDOW appid %d blit.standin' % image, 'WINDOW limits %d 64 48 0 0' % image,
                         'WINDOW decor %d server' % image):
                desk.line(want)
        desk.frm3(64, 32, 1)
        desk.frame(160, 100, image_id=2)
        desk.frame(256, 160, image_id=3)  # Late's own size, configured 0x0
        desk.line('WINDOW lock 2 relative')
        desk.nothing(0.4)
        check(sorted(desk.input_socks) == [1, 2, 3], 'INPUT sock again: %s' % desk.input_socks)
        # Configured once at the initial commit (0x0; Late's while no desktop
        # was there) and once at the old desktop's size: nothing again until
        # the new desktop's WINDOW size.
        check(kept().count('STANDIN configure') == 2 and held().count('STANDIN configure') == 2 and
              late().count('STANDIN configure') == 1, 'a window was configured before its new WINDOW size')
        desk.send('WINDOW size 2 200 120 activated')
        desk.frame(200, 120, image_id=2)
        desk.send('WINDOW size 1 80 40')
        desk.frm3(80, 40, 1)
        desk.send('WINDOW size 3 100 80')
        desk.frame(100, 80, image_id=3)
        desk.nothing(0.3)

        # The person quits the desktop: at once, status 0, the programs gone.
        started = time.time()
        quit_compositor(desk, comp)
        took = time.time() - started
        for client in clients:
            try:
                client.wait(5)
            except subprocess.TimeoutExpired:
                fail('a program outlived WINDOW quit 0')
        check(not os.path.exists(os.path.join(runtime, display)), 'the Wayland socket outlived quit')
        check(not [f for f in os.listdir(runtime) if '-input-' in f],
              'an input socket outlived quit: %s' % os.listdir(runtime))
        text = open(log).read()
        check('reconnect: 3 windows announced again, 3 frames sent again' in text and
              'no desktop answered' not in text, 'the restart in the log:\n' + text)
        check(text.split('WINDOW quit 0')[1].count('reconnect:') == 0, 'a reconnect after quit:\n' + text)
        listener.close()

        # No desktop comes back: only a peer that answers without window=1.
        runtime2 = os.path.join(work, 'run-restart-gone')
        os.mkdir(runtime2, 0o700)
        name2 = name + '-gone'
        listener = listen(name2)
        env2 = dict(env, BLIT_FRAME_SOCKET='@' + name2, XDG_RUNTIME_DIR=runtime2,
                    BLIT_COMPOSITOR_RECONNECT_MS='1500')
        log2 = os.path.join(work, 'compositor-gone.log')
        comp2 = start([compositor, '--x11', 'off', '--log', log2], env2, open(os.path.join(work, 'compositor-gone.out'), 'w'))
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0 -')
        gone_path = os.path.join(work, 'standin-gone.out')
        gone = start([standin, 'Gone'], dict(env2, WAYLAND_DISPLAY='blit-0'), open(gone_path, 'w'))
        clients.append(gone)
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Gone', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.frame(256, 160)
        desk.send('WINDOW size 1 64 48')
        desk.frame(64, 48)
        desk.sock.close()
        listener.close()
        lost = time.time()
        listener = listen(name2)
        listener.settimeout(0.1)
        windowless = 0
        while comp2.poll() is None and time.time() - lost < 10:
            try:
                peer, _ = listener.accept()
            except socket.timeout:
                continue
            peer.settimeout(2)
            hello = peer.recv(128)
            if hello[:8] == b'BLITFRM1':
                # Each redial is a version 5 hello, as the first was.
                check(struct.unpack_from('<I', hello, 8)[0] == 5,
                      'a redial says version %d, not 5' % struct.unpack_from('<I', hello, 8)[0])
                peer.send(b'OK dmabuf=0 window=0\n')
                windowless += 1
            peer.close()
        took2 = time.time() - lost
        check(comp2.poll() == 1, 'with no desktop back, blit-compositor %s'
              % ('ended %d' % comp2.returncode if comp2.returncode is not None else 'kept running'))
        check(1.4 <= took2 <= 4.5, 'it ended %.2f s after the loss, the bound is 1.5 s' % took2)
        check(windowless >= 1, 'it never dialed again')
        # Ended with its compositor: an error, not a close (under the
        # sanitizers, the stand-in's leaks on that path abort it instead).
        check(gone.wait(5) != 0 and 'the connection ended before close' in open(gone_path).read(),
              'its program did not go with it:\n' + open(gone_path).read())
        text = open(log2).read()
        check('without window=1' in text and
              'no desktop answered with window=1 within 1500 ms of the frame channel\'s loss' in text,
              'the log does not say why it ended:\n' + text)
        listener.close()
        return ('display=%s refused=%s outage: shm 60 callbacks in %d ms, dmabuf 61 draws 60 given back, '
                'a program started; back after %.1f s: display+new 1,2,3+hints+INPUT sock, newest '
                'FRM3 64x32 and FRM1 160x100, lock asked again, no configure before WINDOW size, then 200x120,80x40,100x80; '
                'quit 0 in %.2f s; no desktop: status 1 after %.2f s (bound 1.5 s, %d window=0 answers)'
                % (display, ','.join(refused), ms, back, took, took2, windowless))
    finally:
        for p in clients + [comp, comp2]:
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


def start(argv, env, out):
    return subprocess.Popen(argv, env=env, stdout=out, stderr=subprocess.STDOUT)


def desktop_part(compositor, standin, work):
    runtime = os.path.join(work, 'run')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    # The reconnect bound shortened (for gates only): the ending below is
    # the one after it.
    env = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime, BLIT_COMPOSITOR_RECONNECT_MS='600')
    env.pop('WAYLAND_DISPLAY', None)
    log = os.path.join(work, 'compositor.log')
    comp = start([compositor, '--x11', 'off', '--log', log], env, open(os.path.join(work, 'compositor.out'), 'w'))
    client = None
    try:
        desk = Desktop(listener)
        got = desk.next()
        check(got and got[0] == 'line' and got[1].startswith('WINDOW display ') and
              len(got[1].split(' ')) == 4 and got[1].endswith(' -'),
              'the first line is %r, not WINDOW display <name> - (X off)' % (got,))
        display = got[1].split(' ')[2]
        check(display == 'blit-0', 'the first free socket is blit-0, not %r' % display)
        check(os.path.exists(os.path.join(runtime, display)), 'no socket %s in XDG_RUNTIME_DIR' % display)

        client_out = os.path.join(work, 'standin.out')
        client = start([standin, '--popup-move'], dict(env, WAYLAND_DISPLAY=display), open(client_out, 'w'))
        desk.line('WINDOW new 1 0')
        desk.line('WINDOW title 1 Blit stand-in')
        desk.line('WINDOW appid 1 blit.standin')
        desk.line('WINDOW limits 1 64 48 0 0')
        desk.line('WINDOW decor 1 server')
        # The initial commit is answered at once (xdg-shell; vkcube presents
        # after one roundtrip): 0x0 before the desktop's size, so the
        # client's own 256x160 is the first frame.
        desk.frame(256, 160)
        # The desktop's size, when it lands, is an ordinary reconfigure.
        desk.send('WINDOW size 1 320 200')
        first = desk.frame(320, 200)
        # The stand-in's popup: its own picture, at its offset in the
        # toplevel's surface pixels (anchor 10,20 30x40 bottom-left, gravity
        # bottom-right, offset 2,3), sized by its positioner, not the desktop.
        desk.line('WINDOW popup 2 1 12 63')
        desk.frame(80, 60, image_id=2)
        # Repositioned by its program (offset 12,13): `WINDOW popup 2` again
        # at its new offset, and its frame drawn there.
        desk.line('WINDOW popup 2 1 22 73')
        desk.frame(80, 60, image_id=2)
        wait_for(lambda: 'STANDIN popup repositioned 7' in open(client_out).read(),
                 'the popup was not told repositioned')
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')

        # A second program, killed outright as crashing games are: its
        # window goes with no teardown from the client, and nothing else.
        second_out = os.path.join(work, 'standin-second.out')
        second = start([standin, 'Second'], dict(env, WAYLAND_DISPLAY=display), open(second_out, 'w'))
        for want in ('WINDOW new 3 0', 'WINDOW title 3 Second', 'WINDOW appid 3 blit.standin',
                     'WINDOW limits 3 64 48 0 0', 'WINDOW decor 3 server'):
            desk.line(want)
        desk.frame(256, 160, image_id=3)
        desk.send('WINDOW size 3 100 80')
        desk.frame(100, 80, image_id=3)
        wait_for(lambda: 'STANDIN drew 100x80' in open(second_out).read(), 'the second program did not draw')
        second.kill()
        second.wait()
        desk.line('WINDOW gone 3')
        desk.nothing(0.3)
        desk.send('WINDOW size 1 500 300 activated')
        second = desk.frame(500, 300)
        check(second > first, 'the sequence did not rise: %d then %d' % (first, second))
        desk.send('WINDOW size 1 0 0')
        desk.frame(256, 160)
        desk.nothing(0.3)

        desk.send('WINDOW close 1')
        desk.line('WINDOW gone 1')
        try:
            status = client.wait(5)
        except subprocess.TimeoutExpired:
            fail('the stand-in did not exit after WINDOW close')
        output = open(client_out).read()
        check(status == 0, 'the stand-in exited %d: %s' % (status, output))
        # The versions are the ADR's, bounded by the build host's XML.
        versions = dict(l.split()[2:4] for l in output.splitlines() if l.startswith('STANDIN global '))
        for global_ in ('wl_compositor', 'wl_subcompositor', 'wl_shm', 'wl_output', 'wl_seat',
                        'xdg_wm_base', 'zxdg_decoration_manager_v1', 'wp_presentation',
                        'zwp_relative_pointer_manager_v1', 'zwp_pointer_constraints_v1'):
            check(global_ in versions, 'no global %s: %s' % (global_, versions))
        check(int(versions['wl_output']) == 4 and int(versions['wl_seat']) == 8,
              'wl_output or wl_seat at another version: %s' % versions)
        check('zwp_linux_dmabuf_v1' not in versions, 'linux-dmabuf offered without dmabuf=1: %s' % versions)
        # Pointer constraints, asked of the desktop with WINDOW lock.
        check(versions.get('zwp_pointer_constraints_v1') == '1', 'pointer constraints: %s' % versions)
        check(versions.get('wp_presentation') == '1', 'no wp_presentation 1: %s' % versions)
        # Six: the popup draws again after its program moves it.
        check('STANDIN presentation presented=6 discarded=0 refresh=16666666' in output,
              'the stand-in\'s frames were not each presented at 60 Hz:\n%s' % output)
        for want in ('STANDIN decoration server',
                     'STANDIN configure 320x200\n', 'STANDIN configure 500x300 activated\n',
                     'STANDIN configure 0x0\n', 'STANDIN close',
                     'STANDIN popup configure 12,63 80x60', 'STANDIN popup done',
                     'STANDIN seat capabilities 3', 'STANDIN seat name seat0',
                     'STANDIN output geometry Blit blit-compositor', 'STANDIN output scale 1',
                     'STANDIN output name BLIT-1', 'STANDIN output mode 1920x1080\n',
                     'STANDIN capabilities 2 3 4\n', 'STANDIN enter BLIT-1 1920x1080@60000\n'):
            check(want in output, 'the stand-in did not print %r:\n%s' % (want, output))
        # Six draws, each released; their frame callbacks answered, but the
        # popup's last one maybe not: callbacks are answered at the next
        # refresh, and the popup is closed as soon as its moved frame
        # arrives, so it can go first (its callback destroyed with it).
        # Every frame being presented is the check above (presented=6).
        check('STANDIN done draws=6 releases=6 frames=6' in output or
              'STANDIN done draws=6 releases=6 frames=5' in output,
              lambda: 'the stand-in\'s counts:\n' + output)
        check(sorted(desk.input_socks) == [1, 3], 'INPUT sock for the toplevels only: %s' % desk.input_socks)
        # The output is the display's, never a window's size.
        check('STANDIN output mode 320x200' not in output and 'STANDIN output mode 500x300' not in output,
              'a window size reached wl_output:\n%s' % output)
        desk.nothing(0.3)

        states = states_part(desk, standin, env, display, work)
        pacing = pacing_part(desk, standin, env, display, work)
        subs = sub_part(desk, standin, env, display, work)
        csd = csd_part(desk, standin, env, display, work)
        clip = clip_part(desk, standin, env, display, work)
        cursor = cursor_part(desk, standin, env, display, work, log)
        appmenu = appmenu_part(desk, standin, env, display, work, log)
        activation = activation_part(desk, standin, env, display, work, log)
        layout = layout_part(desk, standin, env, display, work, log)

        # The desktop goes and does not come back: the compositor ends after
        # the reconnect bound, status 1, and says why.
        listener.close()
        desk.sock.close()
        try:
            status = comp.wait(5)
        except subprocess.TimeoutExpired:
            fail('blit-compositor kept running after its channel closed')
        text = open(log).read()
        check(status == 1 and 'the frame channel closed' in text and
              'no desktop answered with window=1 within 600 ms' in text,
              'blit-compositor exited %d after the channel closed:\n%s' % (status, text))
        # 45: 40, the popup again after its move, the activation window's
        # three (its own size, the desktop's, and again activated), and the
        # layout window's one (its own size: the layout part sends no size).
        check('45 frames sent, 0 failed' in text, 'the log does not count 45 frames (40, the popup again after its '
              'move, the activation window\'s 3, the layout window\'s 1):\n%s' % text)
        print('COMPOSITOR_DESKTOP_OK display=%s new+title+appid+limits+decor frames=0x0->256x160,320x200,500x300,'
              '0x0->256x160 popup=2@12,63+80x60+done->gone killed=3->gone seat+output pattern=exact close->gone standin=%s globals=%s'
              % (display, output.strip().splitlines()[-1].split(' ', 2)[2],
                 ','.join('%s:%s' % kv for kv in sorted(versions.items()))))
        print('COMPOSITOR_STATES_OK %s' % states)
        print('COMPOSITOR_PACING_OK %s' % pacing)
        print('COMPOSITOR_SUB_OK %s' % subs)
        print('COMPOSITOR_CSD_OK %s' % csd)
        print('COMPOSITOR_CLIP_OK %s' % clip)
        print('COMPOSITOR_CURSOR_OK %s' % cursor)
        print('COMPOSITOR_APPMENU_OK %s' % appmenu)
        print('COMPOSITOR_ACTIVATION_OK %s' % activation)
        print('COMPOSITOR_LAYOUT_OK %s' % layout)
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


def api(method, path, body=None):
    request = urllib.request.Request(BASE + path, method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()
        return json.loads(raw) if raw else {}


class Peer:
    """A frame-channel client of a real Blit."""

    def __init__(self, name, token, version):
        self.sock = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.sock.connect('\0' + name)
        self.sock.send(b'BLITFRM1' + struct.pack('<II', version, len(token)) + token.encode())
        self.sock.settimeout(3)
        self.hello = self.sock.recv(2048)

    def ask(self, line):
        self.sock.send(line)
        return self.sock.recv(2048)


def blit_part(server, compositor, standin, work):
    env = gate_env.server_env(dict(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                          BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'),
                          BLIT_LOGIN_AUTOSTART='0', BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts')))
    blit = subprocess.Popen([server, str(PORT)], env=env, stdout=subprocess.DEVNULL,
                            stderr=subprocess.DEVNULL)
    comp = client = None
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the server did not start', 15)
        script = os.path.join(work, 'run.sh')
        with open(script, 'w') as out:
            out.write("env > %s/env.part && mv %s/env.part %s/env; printf '\\033[?25l'; "
                      "while :; do sleep 1; done\n" % (work, work, work))
        session = api('POST', '/v1/terminal-sessions',
                      {'command': '/bin/sh', 'arguments': [script], 'display_id': 'display-1',
                       'surface': {'width': 40, 'height': 12}})['id']
        wait_for(lambda: os.path.exists(os.path.join(work, 'env')), 'the session did not start')
        child = dict(l.rstrip('\n').split('=', 1) for l in open(os.path.join(work, 'env')) if '=' in l)
        name, token = child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN']

        for version in (3, 4):
            got = Peer(name, token, version).hello
            check(got == b'ERROR 0 EINVAL\n', 'a version %d hello was answered %r' % (version, got))
        v5 = Peer(name, token, 5)
        check(v5.hello == b'OK dmabuf=0 window=0\n', 'a version 5 hello was answered %r' % v5.hello)
        for line, want in ((b'WINDOW display blit-0 -\n', b'WINDOW error 0 ENOTSUP\n'),
                           (b'WINDOW new 7 0\n', b'WINDOW error 7 ENOTSUP\n'),
                           (b'WINDOW size 7 320 200', b'WINDOW error 7 ENOTSUP\n'),
                           (b'WINDOW gone x\n', b'WINDOW error 0 ENOTSUP\n')):
            got = v5.ask(line)
            check(got == want, '%r was answered %r, not %r' % (line, got, want))
        check(Peer(name, token, 6).hello == b'ERROR 0 EINVAL\n', 'a version 6 hello was accepted')

        # blit-compositor reaching Blit directly: it runs, says why it shows
        # nothing, and sends Blit no WINDOW line and no frame. The client
        # still gets xdg-shell's reply to its initial commit (0x0, it
        # picks) -- a client is never left waiting -- and nothing more.
        runtime = os.path.join(work, 'run-blit')
        os.mkdir(runtime, 0o700)
        cenv = compositor_env(BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=token,
                    XDG_RUNTIME_DIR=runtime)
        log = os.path.join(work, 'compositor-blit.log')
        comp = start([compositor, '--x11', 'off', '--log', log], cenv, subprocess.DEVNULL)
        wait_for(lambda: os.path.exists(os.path.join(runtime, 'blit-0')), 'no socket blit-0')
        client_out = os.path.join(work, 'standin-blit.out')
        client = start([standin], dict(cenv, WAYLAND_DISPLAY='blit-0'), open(client_out, 'w'))
        wait_for(lambda: 'committed, waiting' in open(client_out).read(), 'the stand-in did not start')
        time.sleep(0.5)
        check(comp.poll() is None, 'blit-compositor exited against Blit: %s' % open(log).read())
        printed = open(client_out).read()
        check(printed.count('STANDIN configure') == 1 and 'STANDIN configure 0x0\n' in printed,
              'the client was not given exactly the initial 0x0 configure: %s' % printed)
        text = open(log).read()
        check('OK dmabuf=0 window=0' in text and 'without window=1' in text,
              'the log does not say why nothing is shown:\n%s' % text)
        frames = api('GET', '/v1/terminal-sessions/%s' % session).get('frames') or {}
        check(frames.get('refused', 0) == 0 and not frames.get('last_error'),
              'blit-compositor sent Blit something it refused: %s' % frames)
        api('DELETE', '/v1/terminal-sessions/%s' % session)
        print('COMPOSITOR_BLIT_OK hello_v3_v4_v6="ERROR 0 EINVAL" hello_v5="OK dmabuf=0 window=0" '
              'WINDOW="WINDOW error <id> ENOTSUP"x4 compositor=runs,shows-nothing')
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.send_signal(signal.SIGTERM)
                try:
                    p.wait(3)
                except subprocess.TimeoutExpired:
                    p.kill()
                    p.wait()
        blit.terminate()
        try:
            blit.wait(5)
        except subprocess.TimeoutExpired:
            blit.kill()


def main():
    compositor, standin = os.path.abspath(sys.argv[1]), os.path.abspath(sys.argv[2])
    server = os.path.abspath(sys.argv[3]) if len(sys.argv) > 3 else None
    work = tempfile.mkdtemp(prefix='blit-compositor-gate-')
    try:
        desktop_part(compositor, standin, work)
        print('COMPOSITOR_DMABUF_OK %s' % dmabuf_part(compositor, standin, work))
        print('COMPOSITOR_LOCK_OK %s' % lock_part(compositor, standin, work))
        print('COMPOSITOR_POPUP_TREE_OK %s' % popup_tree_part(compositor, standin, work))
        print('COMPOSITOR_RESTART_OK %s' % restart_part(compositor, standin, work))
        print('COMPOSITOR_SUB_RESTART_OK %s' % sub_restart_part(compositor, standin, work))
        if server:
            blit_part(server, compositor, standin, work)
            # A server of its own, for its keymap.
            print('COMPOSITOR_INPUT_OK %s' % input_part(server, compositor, standin, work))
    except SystemExit:
        # A failure shows what each process said before the work goes.
        for name in sorted(os.listdir(work)):
            if name.endswith(('.log', '.out')):
                print('--- %s\n%s' % (name, open(os.path.join(work, name)).read()), file=sys.stderr)
        raise
    finally:
        shutil.rmtree(work, ignore_errors=True)


if __name__ == '__main__':
    main()
