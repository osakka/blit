#!/usr/bin/env python3
"""ADR 0225: physical input to a compositor's EIS socket, the frame
channel's `INPUT` request (docs/api.md, "Picture frames by descriptor").

Two modes, each run by its own shell gate on the mock display:

Every request names its image and every answer carries it
(`OK input <id>`, `ERROR INPUT <id> <code>`, 0 for a line with no readable
id); one channel carries one route per image.

local (frame_input_gate.sh): what needs no libeis, on any host.
  - `ERROR INPUT 7 ENOTSUP` where libei.so.1 cannot be loaded (skipped, and
    said so, where the system has libei);
  - against a stand-in libei (tools/tests/fixtures/libei_stub.c, which
    takes the connection and reports nothing): `EINVAL` for a malformed
    request (id 0), a version 2 channel, the old bare `INPUT on`/`off`,
    `INPUT on <id>` for an image with no route, an image deleted, a Kitty
    image or id 0; `ENOENT` for a missing path, a file that is not a
    socket, a link to a socket and a socket nobody listens on; `EACCES` for
    a socket another user owns (where one exists and the gate is not
    root); `OK input 7`, then `EBUSY` for the same image again and
    `OK input 12` for a second image on the same channel; `frames.input`
    routes going off, on, live and back with INPUT on/off and focus; one
    route on per session, on one channel and across two; and none left
    when the channel closes. `INPUT hold`: `EINVAL` for an image with no
    route, a missing or unknown mode; `OK input 7` for `confine` and
    `relative`, the route's `hold` saying so, `relative` by default.

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
  - the compositor dropping the connection: `ERROR INPUT 10 ENOENT` on the
    channel at once, the route ended (`INPUT on 10` is then EINVAL);
  - relative motion, summed exactly, while the session holds ?7701, the
    pointer hidden and still (hold `relative`, the default);
  - hold `confine` under ?7701: the pointer shown, moved by the motion and
    kept inside the picture's drawn rectangle (both far corners), the
    compositor sent the same point as absolute positions and no relative
    motion, the holder still sent its raw deltas, buttons sent; the picture
    no longer drawn, the lock is relative again (hidden, raw motion); after
    `?7701l` the pointer stays where it was and moves as an ordinary
    absolute pointer over the picture;
  - several routes, a compositor each: two images on one channel each get
    a route; `INPUT on B` releases what A held and stops A before B starts
    (checked in the stand-in's cross-route order, EI_STUB_ORDER); a late
    ENOENT on one route leaves the other working; deleting one image ends
    only its route; two channels of one session keep one route on.
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
    fail(why() if callable(why) else why)


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

    def hidden(self):
        """Whether Blit's pointer is hidden, as drawn."""
        return self.api('GET', '/v1/settings')['effective']['pointer_shown']['hidden']

    def pointer(self):
        p = self.api('GET', '/v1/diagnostics')['render']['pointer']
        return (p['x'], p['y'])

    def locked(self, session):
        return self.api('GET', '/v1/terminal-sessions/%s' % session)['mouse_reporting']['pointer_lock']

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


def route(info, image):
    """frames.input's route for an image, or None."""
    for r in info['routes']:
        if r['image'] == image:
            return r
    return None


def state(server, session, image):
    r = route(server.input(session), image)
    return r['state'] if r else 'none'


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
            check(got == b'ERROR INPUT 7 ENOTSUP\n', 'INPUT without libei was answered %r' % got)
            info = server.input(a.id)
            check(info['routes'] == [] and info['last_error'] == 'ENOTSUP', 'frames.input %s' % info)
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
        check(v2.ask('INPUT eis %s 7\n' % own) == b'ERROR INPUT 0 EINVAL\n', 'a version 2 INPUT was not EINVAL')
        v2.close()

        # No readable id: answered with 0. A readable one is carried back.
        malformed = [
            ('INPUT eis %s\n' % own, 0), ('INPUT eis %s 0\n' % own, 0),
            ('INPUT eis %s 7 8\n' % own, 0), ('INPUT eis %s -7\n' % own, 0), ('INPUT eis %s 07\n' % own, 0),
            ('INPUT eis %s 99999999999\n' % own, 0), ('INPUT eis  %s 7\n' % own, 0),
            ('INPUT eis %s 7\r\n' % own, 0), ('INPUT eis %s 7\n\n' % own, 0), ('INPUT foo\n', 0),
            ('INPUT on\n', 0), ('INPUT off\n', 0), ('INPUT on 0\n', 0), ('INPUT on 7 7\n', 0),
            ('INPUT eis relative/eis 7\n', 7), ('INPUT eis /%s 7\n' % ('x' * 120), 7),
            ('INPUT on 7\n', 7), ('INPUT off 7', 7),
            ('INPUT hold 7 confine\n', 7), ('INPUT hold 7 relative', 7), ('INPUT hold\n', 0),
            ('INPUT hold 0 confine\n', 0), ('INPUT hold confine 7\n', 0), ('INPUT hold 07 confine\n', 0),
        ]
        for text, image in malformed:
            got = ch.ask(text)
            check(got == b'ERROR INPUT %d EINVAL\n' % image, '%r was answered %r' % (text, got))
        fd = os.memfd_create('blit-input-gate-fd')
        got = ch.ask('INPUT eis %s 7\n' % own, [fd])
        os.close(fd)
        check(got == b'ERROR INPUT 0 EINVAL\n', 'INPUT with a descriptor was answered %r' % got)

        # Unknown images: one deleted, and a Kitty image of the session.
        ch.frame(9, 1)
        wait_for(lambda: a.graphics(9), 'image 9 did not land')
        place(a, 9, 1, 1, 4, 1)   # d=I frees an image whose placements it removed
        wait_for(lambda: a.graphics(9)['placements'], 'image 9 was not placed')
        a.write('\033_Ga=d,d=I,i=9,q=2\033\\')
        wait_for(lambda: a.graphics(9) is None, 'image 9 was not deleted')
        got = ch.ask('INPUT eis %s 9\n' % own)
        check(got == b'ERROR INPUT 9 EINVAL\n', 'a deleted image was answered %r' % got)
        a.write('\033_Ga=T,i=11,f=24,s=1,v=1,q=2;AAAA\033\\')
        wait_for(lambda: a.graphics(11), 'Kitty image 11 did not land')
        got = ch.ask('INPUT eis %s 11\n' % own)
        check(got == b'ERROR INPUT 11 EINVAL\n', 'a Kitty image was answered %r' % got)

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
            check(got == b'ERROR INPUT 7 ENOENT\n', '%s was answered %r' % (why, got))
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
            check(got == b'ERROR INPUT 7 EACCES\n', "another user's socket %s was answered %r" % (others[0], got))
            check(server.input(a.id)['last_error'] == 'EACCES', 'EACCES not reported')
            results.append('eacces=EACCES(%s)' % others[0])

        # Accepted: an image whose frame has not come yet may be named, as a
        # placement may come first. The same image again is EBUSY; another
        # image on the same channel gets its own route.
        got = ch.ask('INPUT eis %s 7\n' % own)
        check(got == b'OK input 7\n', 'a well-formed INPUT was answered %r' % got)
        info = server.input(a.id)
        check([{k: r[k] for k in ('image', 'state', 'hold', 'region')} for r in info['routes']] ==
              [{'image': 7, 'state': 'off', 'hold': 'relative', 'region': ''}] and info['last_error'] == '' and
              info['routes'][0]['pointer']['miss'] == 'none-yet',
              'frames.input after OK %s' % info)
        got = ch.ask('INPUT eis %s 7\n' % own)
        check(got == b'ERROR INPUT 7 EBUSY\n', 'the same image twice was answered %r' % got)
        # The hold mode: answered, shown, and refused for an unknown mode.
        holds = []
        for text, answer, hold in (('INPUT hold 7 confine\n', b'OK input 7\n', 'confine'),
                                   ('INPUT hold 7 sideways\n', b'ERROR INPUT 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7\n', b'ERROR INPUT 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7 confine \n', b'ERROR INPUT 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7 relative', b'OK input 7\n', 'relative')):
            got = ch.ask(text)
            check(got == answer, '%r was answered %r' % (text, got))
            check(route(server.input(a.id), 7)['hold'] == hold,
                  'after %r the hold is %s' % (text, route(server.input(a.id), 7)))
            holds.append(got.split()[0].decode())
        got = ch.ask('INPUT eis %s 12\n' % own)
        check(got == b'OK input 12\n', 'a second image on the channel was answered %r' % got)
        check(sorted(r['image'] for r in server.input(a.id)['routes']) == [7, 12],
              'two routes on one channel: %s' % server.input(a.id))

        # on/off are not answered; live needs the focus.
        server.focus(b.id)
        ch.line('INPUT on 7\n')
        check(ch.reply(0.3) == b'', 'INPUT on was answered')
        wait_for(lambda: state(server, a.id, 7) == 'on', 'an unfocused route on is not "on"')
        server.focus(a.id)
        wait_for(lambda: state(server, a.id, 7) == 'live', 'the focused route on is not live')
        ch.line('INPUT off 7')   # the newline is optional
        wait_for(lambda: state(server, a.id, 7) == 'off', 'INPUT off did not end it')
        ch.line('INPUT on 7\n')
        wait_for(lambda: state(server, a.id, 7) == 'live', 'INPUT on again did not make it live')
        # One route on per session: 12 on turns 7 off ...
        ch.line('INPUT on 12\n')
        wait_for(lambda: state(server, a.id, 12) == 'live' and state(server, a.id, 7) == 'off',
                 lambda: 'INPUT on 12 did not turn 7 off: %s' % server.input(a.id))
        # ... and across the session's channels.
        ch2 = Channel(a)
        check(ch2.ask('INPUT eis %s 13\n' % own) == b'OK input 13\n', 'a second channel was refused')
        ch2.line('INPUT on 13\n')
        wait_for(lambda: state(server, a.id, 13) == 'live' and state(server, a.id, 12) == 'off',
                 lambda: 'the second channel did not turn the first off: %s' % server.input(a.id))
        ch.line('INPUT on 7\n')
        wait_for(lambda: state(server, a.id, 7) == 'live' and state(server, a.id, 13) == 'off',
                 lambda: 'the first channel did not turn the second off: %s' % server.input(a.id))
        check(server.input(a.id)['events'] == 0, 'the stand-in libei was sent events')
        ch2.close()
        wait_for(lambda: state(server, a.id, 13) == 'none' and state(server, a.id, 7) == 'live',
                 lambda: 'closing the second channel touched the first: %s' % server.input(a.id))
        ch.close()
        wait_for(lambda: server.input(a.id)['routes'] == [], 'the routes outlived their channel')
        results.append('ok=OK ebusy=same_image two_images=two_routes states=off,on,live,off,live,none '
                       'one_on=channel+session hold=%s' % ','.join(holds))
    finally:
        server.stop()
        for s in keep:
            s.close()
    print('FRAME_INPUT_PASS ' + ' '.join(results))


class Receiver:
    """The compositor's side: tools/tests/eis_receiver.c on libeis, or, with
    `stub`, the recording stand-in libei, which writes the same lines from
    inside Blit (Blit then connects to a plain socket the gate holds) to
    `<socket>.log`, one log per socket as one per receiver."""

    def __init__(self, work, stub=False, name='eis-0'):
        self.path = os.path.join(work, name)
        self.log = self.path + '.log'
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
        elif self.process.poll() is None:
            self.process.terminate()
            self.process.wait(5)
        return open(self.log).read() if os.path.exists(self.log) else ''


def confined(server, a, ch, receiver, place, placement, point, origin):
    """Hold `confine` (ADR 0225, hold modes): under ?7701 the pointer stays
    shown, moves inside the picture and is sent as a position."""
    place(a, 7, 3, 5, 20, 5)
    at = wait_for(lambda: (lambda p: p if p['width'] == 160 else None)(placement()),
                  'placement 160x80 was not drawn')
    check(ch.ask('INPUT hold 7 confine\n') == b'OK input 7\n', 'INPUT hold confine was refused')
    check(route(server.input(a.id), 7)['hold'] == 'confine', 'hold %s' % server.input(a.id))
    point(at, 10, 10, 'absolute before the confined lock')

    def at_point(sx, sy):
        return (origin[0] + at['x'] + sx, origin[1] + at['y'] + sy)

    def abs_line(sx, sy):
        return 'abs %.2f %.2f' % (sx * REGION_W / at['width'], sy * REGION_H / at['height'])

    def deltas():
        return re.findall(rb'\x1b\[<35;(-?\d+);(-?\d+)M', a.sent())

    a.write('\033[?7701h')
    wait_for(lambda: server.locked(a.id), 'the confined lock was not engaged')
    time.sleep(0.3)
    check(not server.hidden(), 'the confined lock hid the pointer')
    check(server.pointer() == at_point(10, 10), 'the pointer moved when the lock engaged: %s' % (server.pointer(),))

    # Motion moves the pointer, and the compositor is sent the same point;
    # the holder is still sent its raw deltas.
    before = len(deltas())
    server.inject({'kind': 'pointer_relative', 'dx': 5, 'dy': 3})
    receiver.expect([abs_line(15, 13)], 'confined motion')
    check(server.pointer() == at_point(15, 13), 'the confined pointer is at %s' % (server.pointer(),))
    wait_for(lambda: deltas()[before:] == [(b'5', b'3')], lambda: 'the holder was sent %r' % deltas()[before:])
    # Clamped at the far corner, then the near one.
    server.inject({'kind': 'pointer_relative', 'dx': 10000, 'dy': 10000})
    receiver.expect([abs_line(at['width'] - 1, at['height'] - 1)], 'confined to the far corner')
    check(server.pointer() == at_point(at['width'] - 1, at['height'] - 1),
          'the pointer past the far corner is at %s' % (server.pointer(),))
    server.inject({'kind': 'pointer_relative', 'dx': 3, 'dy': 0})
    receiver.nothing('motion against the far edge')
    server.inject({'kind': 'pointer_relative', 'dx': -10000, 'dy': -10000})
    receiver.expect([abs_line(0, 0)], 'confined to the top-left corner')
    check(server.pointer() == at_point(0, 0), 'the pointer past the top-left is at %s' % (server.pointer(),))
    ptr = route(server.input(a.id), 7)['pointer']
    check(ptr['miss'] == '' and ptr['sent'] == [0.0, 0.0] and ptr['draw'][2:] == [at['width'], at['height']],
          'the diagnostic under confine: %s' % ptr)
    # Buttons still go to the compositor, and to the holder in the lock's form.
    server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
    server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
    receiver.expect(['button 272 press', 'button 272 release'], 'a click under confine')
    wait_for(lambda: b'\x1b[<0;0;0m' in a.sent(), 'the holder was not sent the click')
    check(not server.hidden(), 'the pointer was hidden under confine')

    # The picture no longer drawn: the lock is relative again, hidden and
    # raw, and the pointer does not move; drawn again, confined again.
    a.write('\033_Ga=d,d=i,i=7,q=2\033\\')
    wait_for(lambda: a.graphics(7) is not None and not a.graphics(7)['placements'],
             'the placement was not removed')
    wait_for(lambda: server.hidden(), 'an undrawn confined picture did not hide the pointer')
    server.inject({'kind': 'pointer_relative', 'dx': 4, 'dy': 2})
    receiver.expect(['rel 4 2'], 'relative motion with the picture not drawn')
    check(server.pointer() == at_point(0, 0), 'the pointer moved with the picture not drawn')
    place(a, 7, 3, 5, 20, 5)
    wait_for(lambda: (lambda p: p if p['width'] == 160 else None)(placement()), 'the picture was not drawn again')
    wait_for(lambda: not server.hidden(), 'the picture drawn again did not show the pointer')
    server.inject({'kind': 'pointer_relative', 'dx': 30, 'dy': 20})
    receiver.expect([abs_line(30, 20)], 'confined again')

    # Given back: the pointer stays where it was, and later motion is an
    # ordinary absolute pointer over the picture.
    a.write('\033[?7701l')
    wait_for(lambda: not server.locked(a.id), 'the confined lock was not given back')
    time.sleep(0.2)
    check(server.pointer() == at_point(30, 20), 'the pointer moved when the lock ended: %s' % (server.pointer(),))
    check(not server.hidden(), 'the pointer was hidden after the lock')
    receiver.nothing('the lock ending')
    server.inject({'kind': 'pointer_relative', 'dx': 20, 'dy': 10})
    receiver.expect([abs_line(50, 30)], 'ordinary motion after the confined lock')
    check(server.pointer() == at_point(50, 30), 'the pointer after the lock is at %s' % (server.pointer(),))
    check(ch.ask('INPUT hold 7 relative\n') == b'OK input 7\n', 'INPUT hold relative was refused')
    return 'confine=shown,clamped@0,0+%d,%d,absolute_only,raw_to_holder,buttons,undrawn=relative,released=absolute' % (
        at['width'] - 1, at['height'] - 1)


def multi(server, a, work, stub):
    """Several routes, a compositor each, as a desktop has one per window's
    picture over one channel. Returns what the PASS line says of it."""
    ra, rb, rc, rd = (Receiver(work, stub, name) for name in ('eis-a', 'eis-b', 'eis-c', 'eis-d'))
    receivers = (ra, rb, rc, rd)
    order = os.path.join(work, 'order.log')

    def key(code, *values):
        for v in values:
            server.inject({'kind': 'key', 'code': code, 'value': v})

    def ordered(wanted, why):
        """In the stand-in, Blit's lines across routes in the order sent."""
        if not stub:
            return
        lines = [l.rstrip('\n') for l in open(order) if not l.split(' ', 1)[1].startswith('#')]
        at = [i for i in range(len(lines) - len(wanted) + 1) if lines[i:i + len(wanted)] == wanted]
        check(at, '%s: %s not in order in %s' % (why, wanted, lines[-12:]))

    try:
        server.focus(a.id)
        ch = Channel(a)
        for image, column in ((20, 1), (21, 21)):
            ch.frame(image, 1)
            wait_for(lambda: a.graphics(image), 'image %d did not land' % image)
            place(a, image, 1, column, 16, 4)
        # Two images on one channel: a route each, no EBUSY.
        check(ch.ask('INPUT eis %s 20\n' % ra.path) == b'OK input 20\n', 'route 20 was refused')
        check(ch.ask('INPUT eis %s 21\n' % rb.path) == b'OK input 21\n', 'route 21 on the same channel was refused')
        ra.expect(['connect', BIND], 'route 20 bound its seat')
        rb.expect(['connect', BIND], 'route 21 bound its seat')
        # The same image twice on one channel is EBUSY.
        check(ch.ask('INPUT eis %s 20\n' % rb.path) == b'ERROR INPUT 20 EBUSY\n', 'image 20 twice was not EBUSY')
        rb.nothing('an EBUSY request')
        ch.line('INPUT on 20\n')
        ra.expect(['start'], 'INPUT on 20')
        rb.nothing('route 21 while 20 is on')

        # INPUT on 21: 20 releases what it held and stops before 21 starts.
        key(30, 1)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        ra.expect(['key 30 press', 'button 272 press'], 'held through route 20')
        rb.nothing('keys while 20 is on')
        ch.line('INPUT on 21\n')
        ra.expect(['key 30 release', 'button 272 release', 'stop'], 'release-all on another route on')
        rb.expect(['start'], 'INPUT on 21')
        ordered(['eis-a key 30 release', 'eis-a button 272 release', 'eis-a stop', 'eis-b start'],
                'route 20 released before route 21 started')
        key(30, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        key(31, 1, 0)
        rb.expect(['key 31 press', 'key 31 release'], 'keys to route 21 only')
        ra.nothing('the releases of what 20 already released, and keys after 21 on')
        info = server.input(a.id)
        check(route(info, 20)['state'] == 'off' and route(info, 21)['state'] == 'live',
              'frames.input after INPUT on 21 %s' % info)

        # A late ENOENT on route 20 leaves 21 working.
        ra.drop()
        got = ch.reply(5.0)
        check(got == b'ERROR INPUT 20 ENOENT\n', 'a dropped compositor on route 20 was answered %r' % got)
        wait_for(lambda: route(server.input(a.id), 20) is None, 'route 20 outlived its compositor')
        key(32, 1, 0)
        rb.expect(['key 32 press', 'key 32 release'], 'route 21 after route 20 was dropped')
        check(state(server, a.id, 21) == 'live', 'route 21 after the drop %s' % server.input(a.id))
        check(ch.ask('INPUT on 20\n') == b'ERROR INPUT 20 EINVAL\n', 'INPUT on 20 after its end was not EINVAL')

        # Deleting one image ends only its route: 22 live and holding a key,
        # its image deleted; 21 still works.
        ch.frame(22, 1)
        wait_for(lambda: a.graphics(22), 'image 22 did not land')
        place(a, 22, 6, 1, 16, 4)
        check(ch.ask('INPUT eis %s 22\n' % rc.path) == b'OK input 22\n', 'route 22 was refused')
        rc.expect(['connect', BIND], 'route 22 bound its seat')
        ch.line('INPUT on 22\n')
        rb.expect(['stop'], 'route 21 off for 22')
        rc.expect(['start'], 'INPUT on 22')
        key(33, 1)
        rc.expect(['key 33 press'], 'held through route 22')
        a.write('\033_Ga=d,d=I,i=22,q=2\033\\')
        rc.expect(['key 33 release', 'stop', 'disconnect'], 'release-all and end on image 22 deleted')
        key(33, 0)
        wait_for(lambda: route(server.input(a.id), 22) is None, 'route 22 outlived its image')
        check(state(server, a.id, 21) == 'off', 'route 21 after image 22 went %s' % server.input(a.id))
        check(ch.ask('INPUT on 22\n') == b'ERROR INPUT 22 EINVAL\n', 'INPUT on 22 after its image went was not EINVAL')
        ch.line('INPUT on 21\n')
        rb.expect(['start'], 'route 21 on again')
        key(34, 1, 0)
        rb.expect(['key 34 press', 'key 34 release'], 'route 21 after image 22 was deleted')
        rb.nothing('route 21 after image 22 was deleted')

        # Two channels of the session: one route on between them.
        ch2 = Channel(a)
        ch2.frame(23, 1)
        wait_for(lambda: a.graphics(23), 'image 23 did not land')
        check(ch2.ask('INPUT eis %s 23\n' % rd.path) == b'OK input 23\n', 'route 23 on a second channel was refused')
        rd.expect(['connect', BIND], 'route 23 bound its seat')
        key(35, 1)
        rb.expect(['key 35 press'], 'held through route 21')
        ch2.line('INPUT on 23\n')
        rb.expect(['key 35 release', 'stop'], 'release-all when the other channel turns its route on')
        rd.expect(['start'], 'INPUT on 23')
        ordered(['eis-b key 35 release', 'eis-b stop', 'eis-d start'],
                'route 21 released before route 23 on another channel started')
        key(35, 0)
        key(36, 1, 0)
        rd.expect(['key 36 press', 'key 36 release'], 'keys to the second channel only')
        rb.nothing('keys to the first channel while the second is on')
        ch.line('INPUT on 21\n')
        rd.expect(['stop'], 'route 23 off for 21')
        rb.expect(['start'], 'route 21 on again from the first channel')
        info = server.input(a.id)
        check(route(info, 21)['state'] == 'live' and route(info, 23)['state'] == 'off',
              'frames.input across two channels %s' % info)
        ch2.close()
        rd.expect(['disconnect'], 'the second channel closed')
        ch.close()
        rb.expect(['stop', 'disconnect'], 'the first channel closed, its live route stopped')
        wait_for(lambda: server.input(a.id)['routes'] == [], 'routes outlived their channels')
    finally:
        for r in receivers:
            r.stop()
    return ('routes=two_per_channel ebusy=same_image on_releases_other=%s late_enoent=one_route '
            'delete=one_route one_on=two_channels' % ('ordered' if stub else 'per_route'))


def eis(stub=False):
    work = tempfile.mkdtemp(prefix='blit-input-eis-gate-')
    receiver = Receiver(work, stub)
    extra = {}
    if stub:
        extra = {'LD_LIBRARY_PATH': os.path.join(ROOT, 'build/tests/libei-stub'),
                 'EI_STUB_LOG': '1', 'EI_STUB_ORDER': os.path.join(work, 'order.log'),
                 'EI_STUB_REGION': '%dx%d' % (REGION_W, REGION_H)}
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
        check(got == b'OK input 7\n', 'INPUT was answered %r' % got)
        receiver.expect(['connect', BIND], 'the route bound its seat')
        receiver.nothing('a route that starts off')
        ch.line('INPUT on 7\n')
        receiver.expect(['start'], 'INPUT on')
        check(state(server, a.id, 7) == 'live', 'frames.input %s' % server.input(a.id))

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
        ch.line('INPUT off 7\n')
        receiver.expect(['stop'], 'INPUT off')
        key(31, 1, 0)
        receiver.nothing('keys while off')
        ch.line('INPUT on 7\n')
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
        ch.line('INPUT off 7\n')
        receiver.expect(['key 32 release', 'button 272 release', 'stop'], 'release-all on INPUT off')
        key(32, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        receiver.nothing('the physical releases after INPUT off')
        ch.line('INPUT on 7\n')
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
        check(state(server, a.id, 7) == 'on', 'frames.input while away %s' % server.input(a.id))
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
            # The diagnostic says the same as what the compositor received.
            ptr = [r for r in server.input(a.id)['routes'] if r['image'] == 7][0]['pointer']
            check(ptr['miss'] == '' and ptr['draw'][2:] == [at['width'], at['height']] and
                  abs(ptr['sent'][0] - sx * REGION_W / at['width']) < 0.06 and
                  abs(ptr['sent'][1] - sy * REGION_H / at['height']) < 0.06,
                  'frames.input pointer diagnostic for %s: %s' % (why, ptr))

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
        miss = [r for r in server.input(a.id)['routes'] if r['image'] == 7][0]['pointer']['miss']
        check(miss == 'outside-picture', 'the diagnostic for a pointer outside the picture: %r' % miss)

        # Pointer lock: relative motion, the raw sum, and no absolute; the
        # pointer hidden and still (hold relative, the default).
        check(route(server.input(a.id), 7)['hold'] == 'relative', 'the default hold %s' % server.input(a.id))
        check(not server.hidden(), 'the pointer was hidden before the lock')
        a.write('\033[?7701h')
        wait_for(lambda: server.locked(a.id), 'the lock was not engaged')
        wait_for(lambda: server.hidden(), 'the relative lock did not hide the pointer')
        still = server.pointer()
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
        check(server.pointer() == still, 'the relative lock moved the pointer: %s -> %s' % (still, server.pointer()))
        a.write('\033[?7701l')
        wait_for(lambda: not server.locked(a.id), 'the lock was not given back')
        wait_for(lambda: not server.hidden(), 'the pointer was not shown after the lock')

        confine = confined(server, a, ch, receiver, place, placement, point, origin)

        # The channel closed: released, stopped, disconnected.
        key(36, 1)
        receiver.expect(['key 36 press'], 'held before the channel closes')
        events = server.input(a.id)['events']
        ch.close()
        receiver.expect(['key 36 release', 'stop', 'disconnect'], 'release-all on channel close')
        wait_for(lambda: server.input(a.id)['routes'] == [], 'the route outlived its channel')
        key(36, 0)

        # The image deleted, on a new channel and image.
        ch = Channel(a)
        ch.frame(8, 1)
        wait_for(lambda: a.graphics(8), 'image 8 did not land')
        place(a, 8, 1, 1, 8, 2)
        # As the bridge does: INPUT on as soon as OK input is read, before
        # the compositor has offered a seat; emulation starts when its
        # device is resumed.
        check(ch.ask('INPUT eis %s 8\n' % receiver.path) == b'OK input 8\n', 'the second route was refused')
        ch.line('INPUT on 8\n')
        receiver.expect(['connect', BIND, 'start'], 'INPUT on before the seat was bound')
        key(35, 1)
        receiver.expect(['key 35 press'], 'held before the image goes')
        # The route ends with its image: released, stopped, disconnected.
        a.write('\033_Ga=d,d=I,i=8,q=2\033\\')
        receiver.expect(['key 35 release', 'stop', 'disconnect'], 'release-all and end on image delete')
        key(35, 0, 1, 0)
        receiver.nothing('keys after the image was deleted')
        info = server.input(a.id)
        check(info['routes'] == [] and info['events'] > events, 'frames.input after the delete %s' % info)
        check(ch.ask('INPUT on 8\n') == b'ERROR INPUT 8 EINVAL\n', 'INPUT on after the image went was not EINVAL')
        ch.close()

        # The compositor goes away under a live route: the client is told
        # ERROR INPUT ENOENT at once, and the route ends.
        ch = Channel(a)
        ch.frame(10, 1)
        wait_for(lambda: a.graphics(10), 'image 10 did not land')
        place(a, 10, 1, 1, 8, 2)
        check(ch.ask('INPUT eis %s 10\n' % receiver.path) == b'OK input 10\n', 'the third route was refused')
        ch.line('INPUT on 10\n')
        receiver.expect(['connect', BIND, 'start'], 'the third route on')
        check(ch.reply(0.3) == b'', 'a live route was answered')
        receiver.drop()
        got = ch.reply(5.0)
        check(got == b'ERROR INPUT 10 ENOENT\n', 'a dropped compositor was answered %r' % got)
        info = wait_for(lambda: (lambda i: i if i['routes'] == [] else None)(server.input(a.id)),
                        'the route outlived its compositor')
        check(info['last_error'] == 'eis-disconnected', 'frames.input after the drop %s' % info)
        check(ch.ask('INPUT on 10\n') == b'ERROR INPUT 10 EINVAL\n', 'INPUT on after the route ended was not EINVAL')
        ch.close()

        several = multi(server, a, work, stub)
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
          'far_corner=region_far_corner outside=silent dropped=ERROR_INPUT_10_ENOENT '
          'relative=70,-30_in_%d,hidden,still %s events=%d frames=%d names=%s %s'
          % (TAG, REGION_W, REGION_H, ','.join(cases), len(moves), confine, sent, frames, names, several))


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
