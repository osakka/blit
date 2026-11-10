#!/usr/bin/env python3
"""ADR 0228 phase 1, milestone A: blit-compositor and the WINDOW wire.

    compositor_gate.py <blit-compositor> <wl_standin_client> [blit-server]

Part one plays the desktop: a fake frame-channel peer that answers the
version 4 hello with window=1, reads WINDOW lines and FRM1 frames, and sends
`WINDOW size` and `WINDOW close`, while blit-compositor serves the in-repo
stand-in client (tools/tests/wl_standin_client.c). It checks the draft in
docs/api.md, "Program windows": `WINDOW display` first, the new window and
its hints, no frame before the desktop's size, a frame of exactly each size
asked for (1:1, the stand-in's pattern pixel for pixel), `0 0` leaving the
size to the program, `WINDOW gone` after close, and the compositor ending
when the desktop's channel closes.

Part two, with a blit-server (on the mock display, gate_display.sh), checks
Blit's side: a version 4 hello is answered as version 3's plus ` window=0`,
each WINDOW line `ERROR WINDOW <id> ENOTSUP`, and blit-compositor reaching
Blit directly keeps running and shows nothing.
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

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19823')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT
SEALS = 0x0002 | 0x0004 | 0x0008  # F_SEAL_SHRINK | F_SEAL_GROW | F_SEAL_WRITE
XR24 = 0x34325258
F_GET_SEALS = 1034
TOKEN = 'compositor-gate-token'


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
        listener.settimeout(10)
        self.sock, _ = listener.accept()
        self.sock.settimeout(5)
        hello = self.sock.recv(128)
        check(hello[:8] == b'BLITFRM1', 'not a hello: %r' % hello[:16])
        version, length = struct.unpack_from('<II', hello, 8)
        check(version == 4, 'the hello says version %d, not 4' % version)
        check(hello[16:16 + length].decode() == TOKEN, 'the hello has the wrong token')
        self.sock.send(answer)
        # `INPUT sock <path> <id>`, one per toplevel: for Blit, which the
        # desktop relays it to; recorded, not part of the WINDOW sequence.
        self.input_socks = {}

    def next(self, wait=5.0):
        """('line', text) or ('frame', header fields, fd); None on timeout."""
        while True:
            got = self.next_any(wait)
            if got is None or got[0] != 'line' or not got[1].startswith('INPUT sock '):
                return got
            path, image = got[1].split(' ')[2:4]
            check(path.startswith('/') and path.endswith('-input') and int(image) not in self.input_socks,
                  'an INPUT sock line %r' % got[1])
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
        for fd in fds:
            os.close(fd)
        check(data.endswith(b'\n') and data.count(b'\n') == 1, 'not one line: %r' % data)
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
        wait_for(lambda: 'STANDIN enter BLIT-1 1920x1080@60000' in printed(),
                 lambda: 'the window did not enter the first output:\n' + printed())
        desk.send('WINDOW output 4 1280 720 30000')
        desk.send('WINDOW size 4 200 100 maximized')
        desk.frame(200, 100, image_id=4)
        for want in ('WINDOW maximize 4 on', 'WINDOW minimize 4', 'WINDOW maximize 4 off',
                     'WINDOW new 5 0', 'WINDOW title 5 Child', 'WINDOW decor 5 client',
                     'WINDOW parent 5 4'):
            desk.line(want)
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
                     'STANDIN child orphaned', 'STANDIN done draws=3 releases=3 frames=3'):
            check(want in output, 'the states stand-in did not print %r:\n%s' % (want, output))
        check(output.count('STANDIN child configure 50x40') == 2, 'the child was not configured again')
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return ('maximize+minimize+parent(5->4,->0)+hide->frame maximized output=BLIT-2:1280x720@30000,'
            'BLIT-1,BLIT-2:800x600@75000')


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
        check('STANDIN presentation presented=11 discarded=0 refresh=50000000' in output,
              'presentation feedback:\n' + output)
        check('STANDIN done draws=11 releases=11 frames=10' in output, 'counts:\n' + output)
    finally:
        if client.poll() is None:
            client.kill()
            client.wait()
    return 'shm 10 frames at 20 Hz in %d ms, presented=11 refresh=50ms' % ms


def input_part(server, compositor, standin, work):
    """Blit's input lines on the compositor's input socket as wl_seat
    events: the gate plays the desktop on the channel and Blit on the input
    socket (docs/api.md, "Input straight from Blit"), and a real blit-server
    serves the keymap (GET /v1/input/keymap, BLIT_SERVER_PORT)."""
    env = server_env(dict(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                          BLIT_SETTINGS_PATH=os.path.join(work, 'settings-input.json'),
                          BLIT_LOGIN_AUTOSTART='0', BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts')))
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
        keymap = urllib.request.urlopen(BASE + '/v1/input/keymap', timeout=10).read()
        cenv = dict(os.environ, BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
                    XDG_RUNTIME_DIR=runtime, BLIT_SERVER_PORT=str(PORT))
        cenv.pop('WAYLAND_DISPLAY', None)
        cenv.pop('BLIT_API_TOKEN', None)
        log = os.path.join(work, 'compositor-input.log')
        comp = start([compositor, '--log', log], cenv, open(os.path.join(work, 'compositor-input.out'), 'w'))
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0')
        out_path = os.path.join(work, 'standin-input.out')
        client = start([standin, '--popup', 'Input'], dict(cenv, WAYLAND_DISPLAY='blit-0'), open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Input', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.send('WINDOW size 1 200 100 activated')
        desk.frame(200, 100)
        desk.line('WINDOW popup 2 1 12 63')
        desk.frame(80, 60, image_id=2)
        check(desk.input_socks.get(1) == os.path.join(runtime, 'blit-0-input'),
              'INPUT sock for window 1: %s' % desk.input_socks)
        check(stat.S_IMODE(os.stat(desk.input_socks[1]).st_mode) == 0o600, 'the input socket is not 0600')
        check('STANDIN keymap format=1 size=%d xkb=1' % (len(keymap) + 1) in printed(),
              lambda: 'the client was not given Blit\'s keymap (%d bytes):\n%s' % (len(keymap), printed()))
        check('STANDIN repeat 25 600' in printed(), 'no repeat info')

        route = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
        route.connect(desk.input_socks[1])
        frame = [0]
        mark = [0]

        def lines_to(*lines):
            text = ''
            for line in lines:
                frame[0] += 1
                text += 'INPUT %s\nINPUT frame 1 %d\n' % (line, frame[0])
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
        # client is told of, from Blit's keymap.
        lines_to('key 1 42 press', 'key 1 30 press', 'key 1 30 release', 'key 1 42 release')
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
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')
        # The desktop takes the window's focus: the keyboard leaves it.
        desk.send('WINDOW size 1 200 100')
        desk.frame(200, 100)
        seen('keyboard leave main')
        lines_to('key 1 31 press')
        seen('keyboard enter main', 'modifiers 0 0 0 0', 'key 31 1')
        # Blit's route ends with the key down: it is released, the
        # keyboard leaves.
        route.close()
        seen('key 31 0', 'keyboard leave main')
        desk.send('WINDOW close 1')
        desk.line('WINDOW gone 1')
        check(client.wait(5) == 0, 'the input stand-in failed:\n' + printed())
        desk.sock.close()
        comp.wait(5)
        text = open(log).read()
        check('keymap: %d bytes from Blit' % len(keymap) in text, 'the keymap was not read:\n' + text)
        return ('keymap=%dB from Blit keys+shift modifiers ptr=enter/motion/popup/grab wheel=120 '
                'relative=7,-3+-2,5 focus=line,!activated,route-end' % len(keymap))
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
    env = dict(os.environ, BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    env.pop('WAYLAND_DISPLAY', None)
    env.pop('BLIT_SERVER_PORT', None)
    log = os.path.join(work, 'compositor-lock.log')
    comp = start([compositor, '--log', log], env, open(os.path.join(work, 'compositor-lock.out'), 'w'))
    client = None
    try:
        desk = Desktop(listener)
        desk.line('WINDOW display blit-0')
        out_path = os.path.join(work, 'standin-lock.out')
        client = start([standin, '--lock', 'Lock'], dict(env, WAYLAND_DISPLAY='blit-0'), open(out_path, 'w'))
        printed = lambda: open(out_path).read()
        count = lambda what: printed().splitlines().count('STANDIN ' + what)
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Lock', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
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
        desk.sock.close()
        comp.wait(5)
        return ('oneshot=pending(off ignored)->on:locked+rel,ptr-dropped->off:unlocked,defunct '
                'persistent=on/off/on,destroy->off confine=on+motion/off/on gone=off->gone')
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


def dmabuf_part(compositor, standin, work):
    """linux-dmabuf against a desktop that answers dmabuf=1 with a formats=
    list: each commit an FRM3 of the client's own plane (the same file, no
    copy), its buffer released only at Blit's RELEASE, given back when its
    window goes; ERROR ENOTSUP copies that frame as FRM1 and every later
    one. This host has no GPU: the stand-in's planes are memfds."""
    runtime = os.path.join(work, 'run-dmabuf')
    os.mkdir(runtime, 0o700)
    name = 'blit-compositor-gate-dmabuf-%d' % os.getpid()
    listener = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
    listener.bind('\0' + name)
    listener.listen(2)
    env = dict(os.environ, BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    env.pop('WAYLAND_DISPLAY', None)
    log = os.path.join(work, 'compositor-dmabuf.log')
    comp = start([compositor, '--log', log], env, open(os.path.join(work, 'compositor-dmabuf.out'), 'w'))
    clients = []
    try:
        desk = Desktop(listener, b'OK dmabuf=1 fence=0 formats=XR24/0,AR24/0 window=1\n')
        desk.line('WINDOW display blit-0')
        cenv = dict(env, WAYLAND_DISPLAY='blit-0')
        out_path = os.path.join(work, 'standin-dmabuf.out')
        client = start([standin, '--dmabuf', 'Dmabuf'], cenv, open(out_path, 'w'))
        clients.append(client)
        printed = lambda: open(out_path).read()
        for want in ('WINDOW new 1 0', 'WINDOW title 1 Dmabuf', 'WINDOW appid 1 blit.standin',
                     'WINDOW limits 1 64 48 0 0', 'WINDOW decor 1 server'):
            desk.line(want)
        desk.send('WINDOW size 1 64 32')
        seq1, file1 = desk.frm3(64, 32, 1)
        check('STANDIN dmabuf 64x32 file %d:%d' % file1 in printed(),
              lambda: 'the FRM3 plane is not the client\'s own file %s:\n%s' % (file1, printed()))
        desk.send('WINDOW size 1 80 40')
        seq2, file2 = desk.frm3(80, 40, 1)
        check(seq2 > seq1 and file2 != file1, 'the second FRM3: sequence %d file %s' % (seq2, file2))
        desk.nothing(0.3)
        check('STANDIN release' not in printed(), lambda: 'released before RELEASE:\n' + printed())
        desk.send('RELEASE 1 %d' % seq1)
        wait_for(lambda: 'STANDIN release 64x32' in printed(), 'RELEASE did not release the buffer')
        check('STANDIN release 80x40' not in printed(), 'the held 80x40 buffer was released too')

        # A window that goes while Blit holds its frame gives the buffer back.
        held_path = os.path.join(work, 'standin-held.out')
        held = start([standin, '--dmabuf', 'Held'], cenv, open(held_path, 'w'))
        clients.append(held)
        for want in ('WINDOW new 2 0', 'WINDOW title 2 Held', 'WINDOW appid 2 blit.standin',
                     'WINDOW limits 2 64 48 0 0', 'WINDOW decor 2 server'):
            desk.line(want)
        desk.send('WINDOW size 2 30 20')
        desk.frm3(30, 20, 2)
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')
        check(held.wait(5) == 0, 'the held stand-in failed: ' + open(held_path).read())
        check('STANDIN release 30x20' in open(held_path).read() and
              'STANDIN done draws=1 releases=1 frames=1' in open(held_path).read(),
              'the held buffer was not given back at WINDOW gone:\n' + open(held_path).read())

        # A dmabuf client drawing on every callback is one frame ahead of
        # Blit: its third frame waits for Blit's RELEASE of its first.
        paced_path = os.path.join(work, 'standin-paced.out')
        paced = start([standin, '--dmabuf', '--animate', '2', 'Paced'], cenv, open(paced_path, 'w'))
        clients.append(paced)
        for want in ('WINDOW new 3 0', 'WINDOW title 3 Paced', 'WINDOW appid 3 blit.standin',
                     'WINDOW limits 3 64 48 0 0', 'WINDOW decor 3 server'):
            desk.line(want)
        desk.send('WINDOW size 3 40 30')
        seq_a, _ = desk.frm3(40, 30, 3)
        seq_b, _ = desk.frm3(40, 30, 3)
        desk.nothing(0.4)
        desk.send('RELEASE 3 %d' % seq_a)
        seq_c, _ = desk.frm3(40, 30, 3)
        # RELEASE of the second takes the third: its callback and feedback
        # come, and the client, done animating, draws no more.
        desk.send('RELEASE 3 %d' % seq_b)
        desk.nothing(0.3)
        desk.send('WINDOW close 3')
        desk.line('WINDOW gone 3')
        check(paced.wait(5) == 0, 'the paced stand-in failed: ' + open(paced_path).read())
        text = open(paced_path).read()
        check('STANDIN done draws=3 releases=3 frames=3' in text and
              'STANDIN presentation presented=3 discarded=0 ' in text,
              'the paced dmabuf client:\n' + text)

        # ENOTSUP: the refused (newest held) frame again as FRM1, and every
        # later one copied.
        desk.send('ERROR 1 ENOTSUP')
        desk.frame(80, 40)
        wait_for(lambda: 'STANDIN release 80x40' in printed(), 'the refused buffer was not released')
        desk.send('WINDOW size 1 100 50')
        desk.frame(100, 50)
        wait_for(lambda: 'STANDIN release 100x50' in printed(), 'the copied buffer was not released')
        desk.send('RELEASE 1 %d' % seq2)  # stale: nothing
        desk.nothing(0.3)
        desk.send('WINDOW close 1')
        desk.line('WINDOW gone 1')
        check(client.wait(5) == 0, 'the dmabuf stand-in failed: ' + printed())
        output = printed()
        for want in ('STANDIN global zwp_linux_dmabuf_v1 4',
                     'STANDIN dmabuf feedback pairs=2 tranche=2 xr24-linear=1',
                     'STANDIN done draws=3 releases=3 frames=3'):
            check(want in output, 'the dmabuf stand-in did not print %r:\n%s' % (want, output))
        desk.sock.close()
        comp.wait(5)
        text = open(log).read()
        check('dmabuf 6 sent, 3 released, 1 refused, 2 copied' in text,
              'the log does not count the dmabuf frames:\n' + text)
        return ('FRM3=client-file,64x32+80x40 RELEASE->release gone->release paced=3rd-after-RELEASE-of-1st '
                'ENOTSUP->FRM1 80x40,100x50 feedback=2')
    finally:
        for p in clients + [comp]:
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
    env = dict(os.environ, BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=TOKEN,
               XDG_RUNTIME_DIR=runtime)
    env.pop('WAYLAND_DISPLAY', None)
    log = os.path.join(work, 'compositor.log')
    comp = start([compositor, '--log', log], env, open(os.path.join(work, 'compositor.out'), 'w'))
    client = None
    try:
        desk = Desktop(listener)
        got = desk.next()
        check(got and got[0] == 'line' and got[1].startswith('WINDOW display '),
              'the first line is %r, not WINDOW display' % (got,))
        display = got[1].split(' ', 2)[2]
        check(display == 'blit-0', 'the first free socket is blit-0, not %r' % display)
        check(os.path.exists(os.path.join(runtime, display)), 'no socket %s in XDG_RUNTIME_DIR' % display)

        client_out = os.path.join(work, 'standin.out')
        client = start([standin, '--popup'], dict(env, WAYLAND_DISPLAY=display), open(client_out, 'w'))
        desk.line('WINDOW new 1 0')
        desk.line('WINDOW title 1 Blit stand-in')
        desk.line('WINDOW appid 1 blit.standin')
        desk.line('WINDOW limits 1 64 48 0 0')
        desk.line('WINDOW decor 1 server')
        # The configure waits for the desktop's size: no frame before it.
        desk.nothing(0.5)

        desk.send('WINDOW size 1 320 200')
        first = desk.frame(320, 200)
        # The stand-in's popup: its own picture, at its offset in the
        # toplevel's surface pixels (anchor 10,20 30x40 bottom-left, gravity
        # bottom-right, offset 2,3), sized by its positioner, not the desktop.
        desk.line('WINDOW popup 2 1 12 63')
        desk.frame(80, 60, image_id=2)
        desk.send('WINDOW close 2')
        desk.line('WINDOW gone 2')

        # A second program, killed outright as crashing games are: its
        # window goes with no teardown from the client, and nothing else.
        second_out = os.path.join(work, 'standin-second.out')
        second = start([standin, 'Second'], dict(env, WAYLAND_DISPLAY=display), open(second_out, 'w'))
        for want in ('WINDOW new 3 0', 'WINDOW title 3 Second', 'WINDOW appid 3 blit.standin',
                     'WINDOW limits 3 64 48 0 0', 'WINDOW decor 3 server'):
            desk.line(want)
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
        check('STANDIN presentation presented=4 discarded=0 refresh=16666666' in output,
              'the stand-in\'s frames were not each presented at 60 Hz:\n%s' % output)
        for want in ('STANDIN decoration server',
                     'STANDIN configure 320x200\n', 'STANDIN configure 500x300 activated\n',
                     'STANDIN configure 0x0\n', 'STANDIN close',
                     'STANDIN popup configure 12,63 80x60', 'STANDIN popup done',
                     'STANDIN seat capabilities 3', 'STANDIN seat name seat0',
                     'STANDIN output geometry Blit blit-compositor', 'STANDIN output scale 1',
                     'STANDIN output name BLIT-1', 'STANDIN output mode 1920x1080\n',
                     'STANDIN capabilities 2 3 4\n', 'STANDIN enter BLIT-1 1920x1080@60000\n',
                     'STANDIN done draws=4 releases=4 frames=4'):
            check(want in output, 'the stand-in did not print %r:\n%s' % (want, output))
        check(sorted(desk.input_socks) == [1, 3], 'INPUT sock for the toplevels only: %s' % desk.input_socks)
        # The output is the display's, never a window's size.
        check('STANDIN output mode 320x200' not in output and 'STANDIN output mode 500x300' not in output,
              'a window size reached wl_output:\n%s' % output)
        desk.nothing(0.3)

        states = states_part(desk, standin, env, display, work)
        pacing = pacing_part(desk, standin, env, display, work)

        # The desktop goes: the compositor ends, status 1, and says why.
        desk.sock.close()
        try:
            status = comp.wait(5)
        except subprocess.TimeoutExpired:
            fail('blit-compositor kept running after its channel closed')
        text = open(log).read()
        check(status == 1 and 'the frame channel closed' in text,
              'blit-compositor exited %d after the channel closed:\n%s' % (status, text))
        check('19 frames sent, 0 failed' in text, 'the log does not count 19 frames:\n%s' % text)
        print('COMPOSITOR_DESKTOP_OK display=%s new+title+appid+limits+decor frames=320x200,500x300,'
              '0x0->256x160 popup=2@12,63+80x60+done->gone killed=3->gone seat+output pattern=exact close->gone standin=%s globals=%s'
              % (display, output.strip().splitlines()[-1].split(' ', 2)[2],
                 ','.join('%s:%s' % kv for kv in sorted(versions.items()))))
        print('COMPOSITOR_STATES_OK %s' % states)
        print('COMPOSITOR_PACING_OK %s' % pacing)
    finally:
        for p in (client, comp):
            if p and p.poll() is None:
                p.kill()
                p.wait()
        listener.close()


def server_env(extra):
    """blit-server's environment: ours, plus BLIT_GATE_SERVER_ENV's
    `NAME=value` words (the mock display, for a sanitized run that must not
    preload it into everything)."""
    env = dict(os.environ, **extra)
    for word in os.environ.get('BLIT_GATE_SERVER_ENV', '').split():
        k, _, v = word.partition('=')
        env[k] = v
    return env


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
    env = server_env(dict(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
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

        v3 = Peer(name, token, 3)
        v4 = Peer(name, token, 4)
        check(v3.hello == b'OK dmabuf=0\n', 'a version 3 hello was answered %r' % v3.hello)
        check(v4.hello == b'OK dmabuf=0 window=0\n', 'a version 4 hello was answered %r' % v4.hello)
        for line, want in ((b'WINDOW display blit-0\n', b'ERROR WINDOW 0 ENOTSUP\n'),
                           (b'WINDOW new 7 0\n', b'ERROR WINDOW 7 ENOTSUP\n'),
                           (b'WINDOW size 7 320 200', b'ERROR WINDOW 7 ENOTSUP\n'),
                           (b'WINDOW gone x\n', b'ERROR WINDOW 0 ENOTSUP\n')):
            got = v4.ask(line)
            check(got == want, '%r was answered %r, not %r' % (line, got, want))
        check(Peer(name, token, 5).hello.startswith(b'ERROR 0 EINVAL'), 'a version 5 hello was accepted')

        # blit-compositor reaching Blit directly: it runs, says why it shows
        # nothing, sends Blit no WINDOW line, and configures no client.
        runtime = os.path.join(work, 'run-blit')
        os.mkdir(runtime, 0o700)
        cenv = dict(os.environ, BLIT_FRAME_SOCKET='@' + name, BLIT_FRAME_TOKEN=token,
                    XDG_RUNTIME_DIR=runtime)
        log = os.path.join(work, 'compositor-blit.log')
        comp = start([compositor, '--log', log], cenv, subprocess.DEVNULL)
        wait_for(lambda: os.path.exists(os.path.join(runtime, 'blit-0')), 'no socket blit-0')
        client_out = os.path.join(work, 'standin-blit.out')
        client = start([standin], dict(cenv, WAYLAND_DISPLAY='blit-0'), open(client_out, 'w'))
        wait_for(lambda: 'committed, waiting' in open(client_out).read(), 'the stand-in did not start')
        time.sleep(0.5)
        check(comp.poll() is None, 'blit-compositor exited against Blit: %s' % open(log).read())
        check('STANDIN configure' not in open(client_out).read(),
              'a client was configured with no desktop: %s' % open(client_out).read())
        text = open(log).read()
        check('OK dmabuf=0 window=0' in text and 'without window=1' in text,
              'the log does not say why nothing is shown:\n%s' % text)
        frames = api('GET', '/v1/terminal-sessions/%s' % session).get('frames') or {}
        check(frames.get('refused', 0) == 0 and not frames.get('last_error'),
              'blit-compositor sent Blit something it refused: %s' % frames)
        api('DELETE', '/v1/terminal-sessions/%s' % session)
        print('COMPOSITOR_BLIT_OK hello_v3="OK dmabuf=0" hello_v4="OK dmabuf=0 window=0" '
              'WINDOW=ENOTSUPx4 v5=EINVAL compositor=runs,shows-nothing')
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
