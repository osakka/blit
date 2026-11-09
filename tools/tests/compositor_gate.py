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

    def __init__(self, listener):
        listener.settimeout(10)
        self.sock, _ = listener.accept()
        self.sock.settimeout(5)
        hello = self.sock.recv(128)
        check(hello[:8] == b'BLITFRM1', 'not a hello: %r' % hello[:16])
        version, length = struct.unpack_from('<II', hello, 8)
        check(version == 4, 'the hello says version %d, not 4' % version)
        check(hello[16:16 + length].decode() == TOKEN, 'the hello has the wrong token')
        self.sock.send(b'OK dmabuf=0 window=1\n')

    def next(self, wait=5.0):
        """('line', text) or ('frame', header fields, fd); None on timeout."""
        self.sock.settimeout(wait)
        try:
            data, fds, _flags, _addr = socket.recv_fds(self.sock, 4096, 4)
        except socket.timeout:
            return None
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

    def nothing(self, wait=0.4):
        got = self.next(wait)
        check(got is None, 'unexpected %r' % (got[:2] if got else got,))

    def send(self, line):
        self.sock.send(line.encode() + b'\n')


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
                        'xdg_wm_base', 'zxdg_decoration_manager_v1'):
            check(global_ in versions, 'no global %s: %s' % (global_, versions))
        check(int(versions['wl_output']) == 4 and int(versions['wl_seat']) == 8,
              'wl_output or wl_seat at another version: %s' % versions)
        for want in ('STANDIN decoration server',
                     'STANDIN configure 320x200\n', 'STANDIN configure 500x300 activated\n',
                     'STANDIN configure 0x0\n', 'STANDIN close',
                     'STANDIN popup configure 12,63 80x60', 'STANDIN popup done',
                     'STANDIN seat capabilities 3', 'STANDIN seat name seat0',
                     'STANDIN output geometry Blit blit-compositor', 'STANDIN output scale 1',
                     'STANDIN output name BLIT-1', 'STANDIN output mode 1920x1080\n',
                     'STANDIN output mode 320x200\n', 'STANDIN output mode 500x300\n',
                     'STANDIN done draws=4 releases=4 frames=4'):
            check(want in output, 'the stand-in did not print %r:\n%s' % (want, output))
        desk.nothing(0.3)

        # The desktop goes: the compositor ends, status 1, and says why.
        desk.sock.close()
        try:
            status = comp.wait(5)
        except subprocess.TimeoutExpired:
            fail('blit-compositor kept running after its channel closed')
        text = open(log).read()
        check(status == 1 and 'the frame channel closed' in text,
              'blit-compositor exited %d after the channel closed:\n%s' % (status, text))
        check('5 frames sent, 0 failed' in text, 'the log does not count 5 frames:\n%s' % text)
        print('COMPOSITOR_DESKTOP_OK display=%s new+title+appid+limits+decor frames=320x200,500x300,'
              '0x0->256x160 popup=2@12,63+80x60+done->gone killed=3->gone seat+output pattern=exact close->gone standin=%s globals=%s'
              % (display, output.strip().splitlines()[-1].split(' ', 2)[2],
                 ','.join('%s:%s' % kv for kv in sorted(versions.items()))))
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
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'))
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
        if server:
            blit_part(server, compositor, standin, work)
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
