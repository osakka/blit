#!/usr/bin/env python3
"""ADR 0225: physical input to a compositor's EIS socket, the frame
channel's `INPUT` request (docs/api.md, "Picture frames by descriptor").

Two modes, each run by its own shell gate on the mock display:

local (frame_input_gate.sh): what needs no libeis, on any host.
  - `ERROR INPUT ENOTSUP` where libei.so.1 cannot be loaded (skipped, and
    said so, where the system has libei);
  - against a stand-in libei (tools/tests/fixtures/libei_stub.c, which
    takes the connection and reports nothing): `EINVAL` for a malformed
    request, a version 2 channel, `INPUT on` without a route, an image
    deleted, a Kitty image or id 0; `ENOENT` for a missing path, a file that is
    not a socket, a link to a socket and a socket nobody listens on;
    `EACCES` for a socket another user owns (where one exists and the gate
    is not root); `OK input`, then `EBUSY` for a second route; `frames.input`
    going off, on, live and back with INPUT on/off and focus, and to none
    when the channel closes.

eis (frame_input_eis_gate.sh): what reaches a compositor, against
  tools/tests/eis_receiver.c (libeis.so.1) and the system's libei.so.1;
  exits 77 where either is missing. Every case compares the receiver's
  canonical log:
  - key press and release in order, no repeats (value 2) sent;
  - nothing sent while off, while another session has focus, or while the
    console is given up (BLIT_TEST_VT_SIGNALS);
  - every key and button pressed through the route released, then
    emulation stopped, on INPUT off, on a focus change, on the console
    given up, on the channel closed and on the image deleted;
  - wheel notches as discrete scroll;
  - the absolute pointer over the image's drawn rectangle, scaled to the
    compositor's region (880x440, larger than the 320x160 frames), at two
    placements and after the placement is resized, the picture's far corner
    reaching the region's, and nothing outside it;
  - the compositor dropping the connection: `ERROR INPUT ENOENT` on the
    channel at once, the route ended (`INPUT on` is then EINVAL);
  - relative motion, summed exactly, while the session holds ?7701.
"""
import ctypes
import fcntl
import json
import os
import re
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
OFFSET = int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0'))
SEALS = fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
BINARY = os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server'))
FRAME_W, FRAME_H = 320, 160   # 2:1, so a c=4r placement of 8x16 cells draws it unletterboxed
# The compositor's pointer region, larger than the frames it sends (as when
# gamescope shrinks its stream): the pointer is scaled to this, not to them.
REGION_W, REGION_H = 880, 440
BIND = 'bind keyboard,pointer,pointer_absolute,button,scroll'
TAG = 'FRAME_INPUT'


def fail(why):
    print('%s_FAIL %s' % (TAG, why), file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


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
    fail(why)


class Server:
    def __init__(self, port, work, extra=None):
        self.base = 'http://127.0.0.1:%d' % port
        env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
                   BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
                   BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'), BLIT_EXPECT_DESKTOP='0',
                   BLIT_TEST_MODE='1', BLIT_TEST_VT_SIGNALS='1')
        env.update(extra or {})
        self.process = subprocess.Popen([BINARY, str(port)], env=env, stdout=subprocess.DEVNULL,
                                        stderr=open(os.path.join(work, 'server.log'), 'w'))
        wait_for(lambda: self.api('GET', '/v1/version'), 'the server on %d did not start' % port, 15)

    def api(self, method, path, body=None):
        data = json.dumps(body).encode() if body is not None else None
        request = urllib.request.Request(self.base + path, method=method, data=data,
                                         headers={'Content-Type': 'application/json'})
        with urllib.request.urlopen(request, timeout=10) as response:
            raw = response.read()
            return json.loads(raw) if raw else {}

    def inject(self, event):
        self.api('POST', '/v1/input/inject', event)

    def focus(self, session):
        self.api('PATCH', '/v1/terminal-sessions/%s' % session, {'focused': True})
        wait_for(lambda: self.api('GET', '/v1/terminal-sessions/%s' % session)['focused'],
                 'session %s did not take focus' % session)

    def input(self, session):
        return self.api('GET', '/v1/terminal-sessions/%s' % session)['frames']['input']

    def stop(self):
        self.process.terminate()
        try:
            self.process.wait(5)
        except subprocess.TimeoutExpired:
            self.process.kill()


class Session:
    """A raw-mode program that reports mouse motion in pixels (1003, 1016),
    copies what it is sent to a file, and writes to its terminal whatever
    the gate puts in its command file, so the gate can place, delete and
    lock without a second channel."""

    def __init__(self, server, work, name):
        self.server = server
        self.dir = os.path.join(work, name)
        os.mkdir(self.dir)
        script = os.path.join(self.dir, 'run.sh')
        with open(script, 'w') as out:
            out.write("stty raw -echo; printf '\\033[?1003h\\033[?1006h\\033[?1016h'; "
                      "env > {d}/env.part && mv {d}/env.part {d}/env; "
                      "( while :; do if [ -f {d}/cmd ]; then cat {d}/cmd; rm -f {d}/cmd; fi; "
                      "sleep 0.05; done ) & exec cat > {d}/input\n".format(d=self.dir))
        self.id = server.api('POST', '/v1/terminal-sessions',
                             {'command': '/bin/sh', 'arguments': [script], 'display_id': 'display-1',
                              'surface': {'width': 40, 'height': 12}})['id']
        wait_for(lambda: os.path.exists(os.path.join(self.dir, 'env')), 'session %s did not start' % name)
        child = dict(line.rstrip('\n').split('=', 1)
                     for line in open(os.path.join(self.dir, 'env')) if '=' in line)
        self.frame_socket = child['BLIT_FRAME_SOCKET'][1:]
        self.frame_token = child['BLIT_FRAME_TOKEN']

    def write(self, text):
        """Bytes to its terminal, as its program would write them."""
        cmd = os.path.join(self.dir, 'cmd')
        wait_for(lambda: not os.path.exists(cmd), 'a terminal command was not taken')
        with open(cmd + '.part', 'w') as out:
            out.write(text)
        os.rename(cmd + '.part', cmd)
        wait_for(lambda: not os.path.exists(cmd), 'a terminal command was not taken')

    def sent(self):
        path = os.path.join(self.dir, 'input')
        return open(path, 'rb').read() if os.path.exists(path) else b''

    def graphics(self, image_id):
        for g in self.server.api('GET', '/v1/terminal-sessions/%s' % self.id).get('graphics', []):
            if g.get('id') == image_id and g.get('complete'):
                return g
        return None


class Channel:
    def __init__(self, session, version=3):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.connect('\0' + session.frame_socket)
        token = session.frame_token.encode()
        self.socket.send(b'BLITFRM1' + struct.pack('<II', version, len(token)) + token)
        self.socket.settimeout(2.0)
        self.hello = self.socket.recv(2048)

    def line(self, text, fds=()):
        if fds:
            socket.send_fds(self.socket, [text.encode()], list(fds))
        else:
            self.socket.send(text.encode())

    def reply(self, wait=1.0):
        self.socket.settimeout(wait)
        try:
            return self.socket.recv(2048)
        except (socket.timeout, BlockingIOError):
            return b''

    def ask(self, text, fds=()):
        self.line(text, fds)
        return self.reply()

    def frame(self, image_id, sequence, width=FRAME_W, height=FRAME_H):
        fd = os.memfd_create('blit-input-gate', os.MFD_ALLOW_SEALING)
        os.write(fd, b'\x40' * (width * height * 3))
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, SEALS)
        header = b'FRM1' + struct.pack('<IQIIIIIIII', image_id, sequence, width, height, 24,
                                       width * 3, 0, 0, 0, 0)
        socket.send_fds(self.socket, [header], [fd])
        os.close(fd)

    def close(self):
        self.socket.close()


def listening_socket(path, listen=True):
    s = socket.socket(socket.AF_UNIX, socket.SOCK_STREAM)
    s.bind(path)
    if listen:
        s.listen(4)
    return s


def system_has(library):
    try:
        ctypes.CDLL(library)
        return True
    except OSError:
        return False


def place(session, image_id, row, column, columns, rows):
    session.write('\033[%d;%dH\033_Ga=p,i=%d,p=1,q=2,C=1,c=%d,r=%d\033\\'
                  % (row, column, image_id, columns, rows))


def local():
    work = tempfile.mkdtemp(prefix='blit-input-gate-')
    results = []
    own = os.path.join(work, 'own-eis')
    keep = [listening_socket(own)]

    # Without libei: ENOTSUP, before any file is looked at.
    if system_has('libei.so.1'):
        results.append('enotsup=SKIPPED(system_has_libei.so.1)')
    else:
        plain = os.path.join(work, 'plain')
        os.mkdir(plain)
        server = Server(19834 + OFFSET, plain)
        try:
            a = Session(server, plain, 'session')
            ch = Channel(a)
            check(ch.hello.startswith(b'OK dmabuf='), 'version 3 hello answered %r' % ch.hello)
            got = ch.ask('INPUT eis %s 7\n' % own)
            check(got == b'ERROR INPUT ENOTSUP\n', 'INPUT without libei was answered %r' % got)
            info = server.input(a.id)
            check(info['state'] == 'none' and info['last_error'] == 'ENOTSUP', 'frames.input %s' % info)
            results.append('enotsup=ENOTSUP')
        finally:
            server.stop()

    # Against the stand-in libei.
    stub = os.path.join(ROOT, 'build/tests/libei-stub')
    server = Server(19834 + OFFSET, work, {'LD_LIBRARY_PATH': stub})
    try:
        a = Session(server, work, 'a')
        b = Session(server, work, 'b')
        ch = Channel(a)

        # A version 2 channel may not ask.
        v2 = Channel(a, 2)
        check(v2.hello == b'OK\n', 'version 2 hello %r' % v2.hello)
        check(v2.ask('INPUT eis %s 7\n' % own) == b'ERROR INPUT EINVAL\n', 'a version 2 INPUT was not EINVAL')
        v2.close()

        malformed = [
            'INPUT eis relative/eis 7\n', 'INPUT eis %s\n' % own, 'INPUT eis %s 0\n' % own,
            'INPUT eis %s 7 8\n' % own, 'INPUT eis %s -7\n' % own, 'INPUT eis %s 07\n' % own,
            'INPUT eis %s 99999999999\n' % own, 'INPUT eis  %s 7\n' % own, 'INPUT eis %s 7\r\n' % own,
            'INPUT eis %s 7\n\n' % own, 'INPUT foo\n', 'INPUT on\n', 'INPUT off\n',
            'INPUT eis /%s 7\n' % ('x' * 120),
        ]
        for text in malformed:
            got = ch.ask(text)
            check(got == b'ERROR INPUT EINVAL\n', '%r was answered %r' % (text, got))
        fd = os.memfd_create('blit-input-gate-fd')
        got = ch.ask('INPUT eis %s 7\n' % own, [fd])
        os.close(fd)
        check(got == b'ERROR INPUT EINVAL\n', 'INPUT with a descriptor was answered %r' % got)

        # Unknown images: one deleted, and a Kitty image of the session.
        ch.frame(9, 1)
        wait_for(lambda: a.graphics(9), 'image 9 did not land')
        place(a, 9, 1, 1, 4, 1)   # d=I frees an image whose placements it removed
        wait_for(lambda: a.graphics(9)['placements'], 'image 9 was not placed')
        a.write('\033_Ga=d,d=I,i=9,q=2\033\\')
        wait_for(lambda: a.graphics(9) is None, 'image 9 was not deleted')
        got = ch.ask('INPUT eis %s 9\n' % own)
        check(got == b'ERROR INPUT EINVAL\n', 'a deleted image was answered %r' % got)
        a.write('\033_Ga=T,i=11,f=24,s=1,v=1,q=2;AAAA\033\\')
        wait_for(lambda: a.graphics(11), 'Kitty image 11 did not land')
        got = ch.ask('INPUT eis %s 11\n' % own)
        check(got == b'ERROR INPUT EINVAL\n', 'a Kitty image was answered %r' % got)

        # No such socket.
        regular = os.path.join(work, 'regular')
        open(regular, 'w').close()
        link = os.path.join(work, 'link')
        os.symlink(own, link)
        dead = os.path.join(work, 'dead')
        keep.append(listening_socket(dead, listen=False))
        for path, why in ((os.path.join(work, 'missing'), 'missing'), (regular, 'a regular file'),
                          (link, 'a link to a socket'), (dead, 'a socket nobody listens on')):
            got = ch.ask('INPUT eis %s 7\n' % path)
            check(got == b'ERROR INPUT ENOENT\n', '%s was answered %r' % (why, got))
        results.append('einval=%d enoent=4' % (len(malformed) + 4))

        # Another user's socket: refused before Blit connects.
        others = [p for p in ('/run/dbus/system_bus_socket', '/run/systemd/journal/stdout',
                              '/run/systemd/private', '/run/systemd/notify', '/run/udev/control')
                  if os.path.exists(p) and stat.S_ISSOCK(os.lstat(p).st_mode) and
                  os.lstat(p).st_uid != os.getuid()]
        if os.getuid() == 0 or not others:
            results.append('eacces=SKIPPED(no_socket_of_another_user)')
        else:
            got = ch.ask('INPUT eis %s 7\n' % others[0])
            check(got == b'ERROR INPUT EACCES\n', "another user's socket %s was answered %r" % (others[0], got))
            check(server.input(a.id)['last_error'] == 'EACCES', 'EACCES not reported')
            results.append('eacces=EACCES(%s)' % others[0])

        # Accepted: an image whose frame has not come yet may be named, as a
        # placement may come first. Then a second route is EBUSY.
        got = ch.ask('INPUT eis %s 7\n' % own)
        check(got == b'OK input\n', 'a well-formed INPUT was answered %r' % got)
        info = server.input(a.id)
        check(info['state'] == 'off' and info['image'] == 7 and info['last_error'] == '',
              'frames.input after OK %s' % info)
        got = ch.ask('INPUT eis %s 7\n' % own)
        check(got == b'ERROR INPUT EBUSY\n', 'a second route was answered %r' % got)

        # on/off are not answered; live needs the focus.
        server.focus(b.id)
        ch.line('INPUT on\n')
        check(ch.reply(0.3) == b'', 'INPUT on was answered')
        wait_for(lambda: server.input(a.id)['state'] == 'on', 'an unfocused route on is not "on"')
        server.focus(a.id)
        wait_for(lambda: server.input(a.id)['state'] == 'live', 'the focused route on is not live')
        ch.line('INPUT off')   # the newline is optional
        wait_for(lambda: server.input(a.id)['state'] == 'off', 'INPUT off did not end it')
        ch.line('INPUT on\n')
        wait_for(lambda: server.input(a.id)['state'] == 'live', 'INPUT on again did not make it live')
        check(server.input(a.id)['events'] == 0, 'the stand-in libei was sent events')
        ch.close()
        wait_for(lambda: server.input(a.id)['state'] == 'none', 'the route outlived its channel')
        results.append('ok=OK ebusy=EBUSY states=off,on,live,off,live,none')
    finally:
        server.stop()
        for s in keep:
            s.close()
    print('FRAME_INPUT_PASS ' + ' '.join(results))


class Receiver:
    """The compositor's side: tools/tests/eis_receiver.c on libeis, or, with
    `stub`, the recording stand-in libei, which writes the same lines from
    inside Blit (Blit then connects to a plain socket the gate holds)."""

    def __init__(self, work, stub=False):
        self.path = os.path.join(work, 'eis-0')
        self.log = os.path.join(work, 'eis.log')
        self.seen = 0
        if stub:
            self.process = None
            self.held = listening_socket(self.path)
            return
        self.process = subprocess.Popen([os.path.join(ROOT, 'build/tests/eis_receiver'), self.path,
                                         self.log, str(REGION_W), str(REGION_H)])
        wait_for(lambda: os.path.exists(self.path), 'the EIS receiver did not start')

    def lines(self):
        if not os.path.exists(self.log):
            return []
        return [l.rstrip('\n') for l in open(self.log) if l.strip() and not l.startswith('#')]

    def expect(self, wanted, why):
        wait_for(lambda: len(self.lines()) >= self.seen + len(wanted),
                 '%s: wanted %s, got %s' % (why, wanted, self.lines()[self.seen:]))
        time.sleep(0.25)
        got = self.lines()[self.seen:]
        check(got == wanted, '%s: wanted %s, got %s' % (why, wanted, got))
        self.seen += len(got)

    def nothing(self, why):
        time.sleep(0.4)
        got = self.lines()[self.seen:]
        check(got == [], '%s: sent %s' % (why, got))

    def drop(self):
        """The compositor goes away: the receiver killed, or the stand-in's
        connections closed."""
        if self.process is not None:
            self.process.terminate()
            self.process.wait(5)
            return
        self.held.settimeout(0.5)
        try:
            while True:
                self.held.accept()[0].close()
        except (socket.timeout, BlockingIOError):
            pass

    def stop(self):
        if self.process is None:
            self.held.close()
        else:
            self.process.terminate()
            self.process.wait(5)
        return open(self.log).read()


def eis(stub=False):
    work = tempfile.mkdtemp(prefix='blit-input-eis-gate-')
    receiver = Receiver(work, stub)
    extra = {}
    if stub:
        extra = {'LD_LIBRARY_PATH': os.path.join(ROOT, 'build/tests/libei-stub'),
                 'EI_STUB_LOG': receiver.log, 'EI_STUB_REGION': '%dx%d' % (REGION_W, REGION_H)}
    server = Server((19836 if stub else 19835) + OFFSET, work, extra)
    try:
        a = Session(server, work, 'a')
        b = Session(server, work, 'b')
        server.focus(a.id)
        ch = Channel(a)
        ch.frame(7, 1)
        wait_for(lambda: a.graphics(7), 'image 7 did not land')
        place(a, 7, 3, 5, 20, 5)
        got = ch.ask('INPUT eis %s 7\n' % receiver.path)
        check(got == b'OK input\n', 'INPUT was answered %r' % got)
        receiver.expect(['connect', BIND], 'the route bound its seat')
        receiver.nothing('a route that starts off')
        ch.line('INPUT on\n')
        receiver.expect(['start'], 'INPUT on')
        check(server.input(a.id)['state'] == 'live', 'frames.input %s' % server.input(a.id))

        def key(code, *values):
            for v in values:
                server.inject({'kind': 'key', 'code': code, 'value': v})

        # Press and release, in order; the kernel's repeats are not sent.
        key(30, 1, 2, 2, 0)
        receiver.expect(['key 30 press', 'key 30 release'], 'a key with repeats')
        key(48, 1)
        key(46, 1)
        key(48, 0)
        key(46, 0)
        receiver.expect(['key 48 press', 'key 46 press', 'key 48 release', 'key 46 release'], 'overlapping keys')

        # Nothing while off, nor while another session has focus.
        ch.line('INPUT off\n')
        receiver.expect(['stop'], 'INPUT off')
        key(31, 1, 0)
        receiver.nothing('keys while off')
        ch.line('INPUT on\n')
        receiver.expect(['start'], 'INPUT on again')
        server.focus(b.id)
        receiver.expect(['stop'], 'focus to another session')
        key(31, 1, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        receiver.nothing('input while unfocused')
        server.focus(a.id)
        receiver.expect(['start'], 'focus back')

        # Everything held is released before emulation stops: INPUT off ...
        key(32, 1)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        receiver.expect(['key 32 press', 'button 272 press'], 'a key and a button held')
        ch.line('INPUT off\n')
        receiver.expect(['key 32 release', 'button 272 release', 'stop'], 'release-all on INPUT off')
        key(32, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        receiver.nothing('the physical releases after INPUT off')
        ch.line('INPUT on\n')
        receiver.expect(['start'], 'on after the off case')
        # ... a focus change ...
        key(33, 1)
        server.inject({'kind': 'pointer_button', 'button': 2, 'pressed': True})
        receiver.expect(['key 33 press', 'button 273 press'], 'held before the focus change')
        server.focus(b.id)
        receiver.expect(['key 33 release', 'button 273 release', 'stop'], 'release-all on focus change')
        key(33, 0)
        server.inject({'kind': 'pointer_button', 'button': 2, 'pressed': False})
        server.focus(a.id)
        receiver.expect(['start'], 'focus back after the focus case')
        # ... and Blit giving up the console.
        key(34, 1)
        receiver.expect(['key 34 press'], 'held before the console goes')
        server.process.send_signal(signal.SIGUSR1)
        receiver.expect(['key 34 release', 'stop'], 'release-all on console release')
        key(31, 1, 0)
        receiver.nothing('keys while the console is away')
        check(server.input(a.id)['state'] == 'on', 'frames.input while away %s' % server.input(a.id))
        server.process.send_signal(signal.SIGUSR2)
        receiver.expect(['start'], 'the console back')
        key(34, 0)
        receiver.nothing('the release of a key already released')

        # Wheel notches.
        server.inject({'kind': 'pointer_wheel', 'delta': 1})
        server.inject({'kind': 'pointer_wheel', 'delta': -1})
        receiver.expect(['scroll 0 -120', 'scroll 0 120'], 'wheel up then down')

        # The absolute pointer. The surface's origin on the display, from the
        # session's own pixel reports.
        # Session a is stacked first on the display, near its top-left.
        # One motion, so the newest report is the one for this point.
        server.inject({'kind': 'pointer_motion', 'x': 301, 'y': 101})
        report = wait_for(lambda: re.findall(rb'\x1b\[<35;(\d+);(\d+)M', a.sent()),
                          'no pixel report: %r' % a.sent()[-200:])[-1]
        origin = (301 - (int(report[0]) - 1), 101 - (int(report[1]) - 1))
        receiver.seen = len(receiver.lines())  # motion so far was outside the picture

        def placement():
            g = wait_for(lambda: a.graphics(7), 'image 7 is gone')
            p = [p for p in g['placements'] if p['id'] == 1]
            check(len(p) == 1, 'placement 1 of image 7: %s' % g['placements'])
            return p[0]

        def point(at, sx, sy, why):
            server.inject({'kind': 'pointer_motion', 'x': origin[0] + at['x'] + sx,
                           'y': origin[1] + at['y'] + sy})
            receiver.expect(['abs %.2f %.2f' % (sx * REGION_W / at['width'], sy * REGION_H / at['height'])], why)

        cases = []
        for row, column, columns, rows, sx, sy in ((3, 5, 20, 5, 40, 20), (1, 1, 40, 10, 100, 50)):
            place(a, 7, row, column, columns, rows)
            at = wait_for(lambda: (lambda p: p if p['width'] == columns * 8 else None)(placement()),
                          'placement %dx%d was not drawn' % (columns, rows))
            point(at, sx, sy, 'absolute at %d,%d %dx%d' % (at['x'], at['y'], at['width'], at['height']))
            point(at, 0, 0, 'the top-left of %dx%d' % (at['width'], at['height']))
            # The picture's far corner reaches the region's: its last pixel
            # maps into the region's last pixel's worth.
            far = ((at['width'] - 1) * REGION_W / at['width'], (at['height'] - 1) * REGION_H / at['height'])
            check(far[0] >= REGION_W - REGION_W / at['width'] and far[1] >= REGION_H - REGION_H / at['height'],
                  'far corner arithmetic %s' % (far,))
            point(at, at['width'] - 1, at['height'] - 1, 'the far corner of %dx%d' % (at['width'], at['height']))
            cases.append('%dx%d@%d,%d' % (at['width'], at['height'], at['x'], at['y']))
        # Resized in place: the same placement, smaller.
        place(a, 7, 1, 1, 8, 2)
        at = wait_for(lambda: (lambda p: p if p['width'] == 64 else None)(placement()), 'the resize was not drawn')
        point(at, 10, 5, 'absolute after the resize')
        cases.append('resized:%dx%d' % (at['width'], at['height']))
        server.inject({'kind': 'pointer_motion', 'x': origin[0] + at['x'] + 200, 'y': origin[1] + at['y'] + 100})
        receiver.nothing('the pointer outside the picture')

        # Pointer lock: relative motion, the raw sum, and no absolute.
        a.write('\033[?7701h')
        wait_for(lambda: server.api('GET', '/v1/terminal-sessions/%s' % a.id)['mouse_reporting']['pointer_lock'],
                 'the lock was not engaged')
        for _ in range(10):
            server.inject({'kind': 'pointer_relative', 'dx': 7, 'dy': -3})

        def summed():
            moves = [l for l in receiver.lines()[receiver.seen:]]
            sums = [0, 0]
            for l in moves:
                check(l.startswith('rel '), 'under lock: %s' % moves)
                dx, dy = l.split()[1:]
                sums[0] += int(dx)
                sums[1] += int(dy)
            return moves if sums == [70, -30] else None
        moves = wait_for(summed, 'relative motion did not sum to 70,-30: %s' % receiver.lines()[receiver.seen:])
        receiver.seen += len(moves)
        a.write('\033[?7701l')
        wait_for(lambda: not server.api('GET', '/v1/terminal-sessions/%s' % a.id)['mouse_reporting']['pointer_lock'],
                 'the lock was not given back')

        # The channel closed: released, stopped, disconnected.
        key(36, 1)
        receiver.expect(['key 36 press'], 'held before the channel closes')
        events = server.input(a.id)['events']
        ch.close()
        receiver.expect(['key 36 release', 'stop', 'disconnect'], 'release-all on channel close')
        wait_for(lambda: server.input(a.id)['state'] == 'none', 'the route outlived its channel')
        key(36, 0)

        # The image deleted, on a new channel and image.
        ch = Channel(a)
        ch.frame(8, 1)
        wait_for(lambda: a.graphics(8), 'image 8 did not land')
        place(a, 8, 1, 1, 8, 2)
        # As the bridge does: INPUT on as soon as OK input is read, before
        # the compositor has offered a seat; emulation starts when its
        # device is resumed.
        check(ch.ask('INPUT eis %s 8\n' % receiver.path) == b'OK input\n', 'the second route was refused')
        ch.line('INPUT on\n')
        receiver.expect(['connect', BIND, 'start'], 'INPUT on before the seat was bound')
        key(35, 1)
        receiver.expect(['key 35 press'], 'held before the image goes')
        a.write('\033_Ga=d,d=I,i=8,q=2\033\\')
        receiver.expect(['key 35 release', 'stop'], 'release-all on image delete')
        key(35, 0, 1, 0)
        receiver.nothing('keys after the image was deleted')
        info = server.input(a.id)
        check(info['state'] == 'on' and info['image'] == 8 and info['events'] > events,
              'frames.input after the delete %s' % info)
        ch.close()
        receiver.expect(['disconnect'], 'the second channel closed')

        # The compositor goes away under a live route: the client is told
        # ERROR INPUT ENOENT at once, and the route ends.
        ch = Channel(a)
        ch.frame(10, 1)
        wait_for(lambda: a.graphics(10), 'image 10 did not land')
        place(a, 10, 1, 1, 8, 2)
        check(ch.ask('INPUT eis %s 10\n' % receiver.path) == b'OK input\n', 'the third route was refused')
        ch.line('INPUT on\n')
        receiver.expect(['connect', BIND, 'start'], 'the third route on')
        check(ch.reply(0.3) == b'', 'a live route was answered')
        receiver.drop()
        got = ch.reply(5.0)
        check(got == b'ERROR INPUT ENOENT\n', 'a dropped compositor was answered %r' % got)
        info = wait_for(lambda: (lambda i: i if i['state'] == 'none' else None)(server.input(a.id)),
                        'the route outlived its compositor')
        check(info['last_error'] == 'eis-disconnected', 'frames.input after the drop %s' % info)
        check(ch.ask('INPUT on\n') == b'ERROR INPUT EINVAL\n', 'INPUT on after the route ended was not EINVAL')
        ch.close()
    finally:
        server.stop()
        log = receiver.stop()
    sent = len([l for l in log.splitlines() if l.split(' ')[0] in ('key', 'button', 'abs', 'rel', 'scroll')])
    frames = len([l for l in log.splitlines() if l == '# frame'])
    check(frames >= sent, '%d events in %d frames' % (sent, frames))
    names = re.search(r'# names=(\w+)', log).group(1)
    check('# libeis names' not in log, 'event numbers disagree with libeis: %s' % log)
    print('%s_PASS keys=ordered repeats=dropped off+unfocused+console=silent '
          'release_all=off,focus,console,close,delete scroll=discrete region=%dx%d absolute=%s '
          'far_corner=region_far_corner outside=silent dropped=ERROR_INPUT_ENOENT '
          'relative=70,-30_in_%d events=%d frames=%d names=%s'
          % (TAG, REGION_W, REGION_H, ','.join(cases), len(moves), sent, frames, names))


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'local'
    if mode == 'eis':
        TAG = 'FRAME_INPUT_EIS'
        eis()
    elif mode == 'stub':
        TAG = 'FRAME_INPUT_STUB'
        eis(stub=True)
    else:
        local()
