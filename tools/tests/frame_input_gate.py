#!/usr/bin/env python3
"""ADR 0228: physical input to blit-compositor's per-window input socket,
the frame channel's `INPUT` requests (docs/api.md, "Picture frames by
descriptor" and "Program windows", "Input straight from Blit").

Every request names its image and every answer carries it, the id the
third word as everywhere in version 5 of the channel (docs/api.md, "One
grammar, version 5": `INPUT sock <id> <path>`, `INPUT ok <id>`,
`INPUT error <id> <code>`, 0 for a line with no readable id); one channel
carries one route per image. The gate is the compositor's end: a plain
listening socket, and for the events a reader of the lines Blit writes.

local: the answers and the states, nothing read from the socket.
  - `EINVAL` for a malformed request (id 0), the version 4 shape
    `INPUT sock <path> <id>` (and a version 4 hello refused `ERROR 0
    EINVAL`), `INPUT eis`, retired in 0.3.75 and now an unknown verb
    (`INPUT error 0 EINVAL`, as `INPUT foo`), the old bare `INPUT on`/`off`,
    `INPUT on <id>` for an image with no route, an image deleted, a Kitty
    image or id 0; `ENOENT` for a missing path, a file that is not a
    socket, a link to a socket and a socket nobody listens on; `EACCES` for
    a socket another user owns (where one exists and the gate is not
    root); `INPUT ok 7`, then `EBUSY` for the same image again and
    `INPUT ok 12` for a second image on the same channel; `frames.input`
    routes going off, on, live and back with INPUT on/off and focus; one
    route on per session, on one channel and across two; and none left
    when the channel closes. `INPUT hold`: `EINVAL` for an image with no
    route, a missing or unknown mode; `INPUT ok 7` for `confine` and
    `relative`, the route's `hold` saying so, `relative` by default.

sock: what reaches the compositor, the lines read off the socket, every
  event followed by its `INPUT frame`:
  - the keymap (a sealed memfd, what GET /v1/input/keymap serves) once
    when the route first goes live, and not again;
  - key press and release in order, no repeats (value 2) sent;
  - nothing sent while off, while another session has focus, or while the
    console is given up (BLIT_TEST_VT_SIGNALS);
  - every key and button pressed through the route released on INPUT off,
    on a focus change, on the console given up, on the channel closed and
    on the image deleted (then the connection closed);
  - wheel notches, positive right and down;
  - the absolute pointer over the image's drawn rectangle, in the
    picture's own frame pixels (320x160), at two placements, scaled and
    not, and after the placement is resized, at both corners, and nothing
    outside it (`miss` `outside-picture`); the `frames.input` diagnostic
    saying what was sent;
  - `INPUT member`: its answers, the pointer over a popup mapped into the
    top-level's pixels, a negative offset, the member ending with its
    picture;
  - relative motion, summed exactly, while the session holds ?7701, the
    pointer hidden and still (hold `relative`, the default);
  - hold `confine` under ?7701: the pointer shown, moved by the motion and
    kept inside the picture's drawn rectangle (both far corners), the
    compositor sent the same point as absolute positions and no relative
    motion, the holder still sent its raw deltas, buttons sent; the picture
    no longer drawn, the lock is relative again (hidden, raw motion); after
    `?7701l` the pointer stays where it was and moves as an ordinary
    absolute pointer over the picture;
  - `INPUT on` before the compositor accepted the connection: the keymap
    and the keys wait in the socket;
  - the compositor closing its socket under a live route: `INPUT error 10
    ENOENT` on the channel at once, the route ended (`INPUT on 10` is then
    EINVAL), `last_error` `sock-disconnected`; a line it would not take:
    counted in `dropped`, ENOENT;
  - several routes, a socket each: two images on one channel each get a
    route; `INPUT on B` releases what A held before B gets anything (A's
    release lines stamped before B's next event); a late ENOENT on one
    route leaves the other working; deleting one image ends only its
    route; two channels of one session keep one route on.
"""
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

sys.dont_write_bytecode = True  # the helper below is imported from this directory
import gate_env  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
OFFSET = int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0'))
SEALS = fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE
BINARY = os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server'))
FRAME_W, FRAME_H = 320, 160   # 2:1, so a c=4r placement of 8x16 cells draws it unletterboxed
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
        env = gate_env.server_env(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
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
    def __init__(self, session, version=5):
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
    own = os.path.join(work, 'own-input')
    # A window's socket each: one route per socket (ADR 0229).
    own12 = os.path.join(work, 'own-input-12')
    own13 = os.path.join(work, 'own-input-13')
    keep = [listening_socket(own), listening_socket(own12), listening_socket(own13)]
    server = Server(19834 + OFFSET, work)
    try:
        a = Session(server, work, 'a')
        b = Session(server, work, 'b')
        ch = Channel(a)
        check(ch.hello.startswith(b'OK dmabuf='), 'version 5 hello answered %r' % ch.hello)

        # Version 5 only: a version 4 channel is refused at its hello, so
        # nothing it could ask is read.
        v4 = Channel(a, 4)
        check(v4.hello == b'ERROR 0 EINVAL\n', 'a version 4 hello was answered %r' % v4.hello)
        v4.close()

        # No readable id: answered with 0. A readable one is carried back.
        # The version 4 shape, the path before the id, has no readable id;
        # nor has `INPUT eis`, retired in 0.3.75 and now no verb at all.
        malformed = [
            ('INPUT sock %s 7\n' % own, 0), ('INPUT eis 7 %s\n' % own, 0), ('INPUT eis %s 7\n' % own, 0),
            ('INPUT sock\n', 0), ('INPUT sock 0 %s\n' % own, 0),
            ('INPUT sock -7 %s\n' % own, 0), ('INPUT sock 07 %s\n' % own, 0),
            ('INPUT sock 99999999999 %s\n' % own, 0), ('INPUT sock  7 %s\n' % own, 0),
            ('INPUT sock 7 %s\r\n' % own, 0), ('INPUT sock 7 %s\n\n' % own, 0), ('INPUT foo\n', 0),
            ('INPUT on\n', 0), ('INPUT off\n', 0), ('INPUT on 0\n', 0), ('INPUT on 7 7\n', 0),
            ('INPUT sock 7\n', 7), ('INPUT sock 7 \n', 7), ('INPUT sock 7 %s 8\n' % own, 7),
            ('INPUT sock 7 relative/input\n', 7), ('INPUT sock 7 /%s\n' % ('x' * 120), 7),
            ('INPUT on 7\n', 7), ('INPUT off 7', 7),
            ('INPUT hold 7 confine\n', 7), ('INPUT hold 7 relative', 7), ('INPUT hold\n', 0),
            ('INPUT hold 0 confine\n', 0), ('INPUT hold confine 7\n', 0), ('INPUT hold 07 confine\n', 0),
        ]
        for text, image in malformed:
            got = ch.ask(text)
            check(got == b'INPUT error %d EINVAL\n' % image, '%r was answered %r' % (text, got))
        fd = os.memfd_create('blit-input-gate-fd')
        got = ch.ask('INPUT sock 7 %s\n' % own, [fd])
        os.close(fd)
        check(got == b'INPUT error 0 EINVAL\n', 'INPUT with a descriptor was answered %r' % got)

        # Unknown images: one deleted, and a Kitty image of the session.
        ch.frame(9, 1)
        wait_for(lambda: a.graphics(9), 'image 9 did not land')
        place(a, 9, 1, 1, 4, 1)   # d=I frees an image whose placements it removed
        wait_for(lambda: a.graphics(9)['placements'], 'image 9 was not placed')
        a.write('\033_Ga=d,d=I,i=9,q=2\033\\')
        wait_for(lambda: a.graphics(9) is None, 'image 9 was not deleted')
        got = ch.ask('INPUT sock 9 %s\n' % own)
        check(got == b'INPUT error 9 EINVAL\n', 'a deleted image was answered %r' % got)
        a.write('\033_Ga=T,i=11,f=24,s=1,v=1,q=2;AAAA\033\\')
        wait_for(lambda: a.graphics(11), 'Kitty image 11 did not land')
        got = ch.ask('INPUT sock 11 %s\n' % own)
        check(got == b'INPUT error 11 EINVAL\n', 'a Kitty image was answered %r' % got)

        # No such socket.
        regular = os.path.join(work, 'regular')
        open(regular, 'w').close()
        link = os.path.join(work, 'link')
        os.symlink(own, link)
        dead = os.path.join(work, 'dead')
        keep.append(listening_socket(dead, listen=False))
        for path, why in ((os.path.join(work, 'missing'), 'missing'), (regular, 'a regular file'),
                          (link, 'a link to a socket'), (dead, 'a socket nobody listens on')):
            got = ch.ask('INPUT sock 7 %s\n' % path)
            check(got == b'INPUT error 7 ENOENT\n', '%s was answered %r' % (why, got))
        results.append('hello_v4=ERROR_0_EINVAL v4_shape=INPUT_error_0_EINVAL eis=INPUT_error_0_EINVAL '
                       'einval=%d enoent=4' % (len(malformed) + 4))

        # Another user's socket: refused before Blit connects.
        others = [p for p in ('/run/dbus/system_bus_socket', '/run/systemd/journal/stdout',
                              '/run/systemd/private', '/run/systemd/notify', '/run/udev/control')
                  if os.path.exists(p) and stat.S_ISSOCK(os.lstat(p).st_mode) and
                  os.lstat(p).st_uid != os.getuid()]
        if os.getuid() == 0 or not others:
            results.append('eacces=SKIPPED(no_socket_of_another_user)')
        else:
            got = ch.ask('INPUT sock 7 %s\n' % others[0])
            check(got == b'INPUT error 7 EACCES\n', "another user's socket %s was answered %r" % (others[0], got))
            check(server.input(a.id)['last_error'] == 'EACCES', 'EACCES not reported')
            results.append('eacces=EACCES(%s)' % others[0])

        # Accepted: an image whose frame has not come yet may be named, as a
        # placement may come first. The same image again is EBUSY; another
        # image on the same channel gets its own route.
        got = ch.ask('INPUT sock 7 %s\n' % own)
        check(got == b'INPUT ok 7\n', 'a well-formed INPUT was answered %r' % got)
        info = server.input(a.id)
        check([{k: r[k] for k in ('image', 'state', 'hold', 'members')} for r in info['routes']] ==
              [{'image': 7, 'state': 'off', 'hold': 'relative', 'members': []}] and info['last_error'] == '' and
              info['routes'][0]['pointer']['miss'] == 'none-yet' and
              'backend' not in info['routes'][0] and 'region' not in info['routes'][0],
              'frames.input after OK %s' % info)
        got = ch.ask('INPUT sock 7 %s\n' % own)
        check(got == b'INPUT error 7 EBUSY\n', 'the same image twice was answered %r' % got)
        # The hold mode: answered, shown, and refused for an unknown mode.
        holds = []
        for text, answer, hold in (('INPUT hold 7 confine\n', b'INPUT ok 7\n', 'confine'),
                                   ('INPUT hold 7 sideways\n', b'INPUT error 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7\n', b'INPUT error 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7 confine \n', b'INPUT error 7 EINVAL\n', 'confine'),
                                   ('INPUT hold 7 relative', b'INPUT ok 7\n', 'relative')):
            got = ch.ask(text)
            check(got == answer, '%r was answered %r' % (text, got))
            check(route(server.input(a.id), 7)['hold'] == hold,
                  'after %r the hold is %s' % (text, route(server.input(a.id), 7)))
            holds.append(got.split()[1].decode())
        got = ch.ask('INPUT sock 12 %s\n' % own12)
        check(got == b'INPUT ok 12\n', 'a second image on the channel was answered %r' % got)
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
        check(ch2.ask('INPUT sock 13 %s\n' % own13) == b'INPUT ok 13\n', 'a second channel was refused')
        ch2.line('INPUT on 13\n')
        wait_for(lambda: state(server, a.id, 13) == 'live' and state(server, a.id, 12) == 'off',
                 lambda: 'the second channel did not turn the first off: %s' % server.input(a.id))
        ch.line('INPUT on 7\n')
        wait_for(lambda: state(server, a.id, 7) == 'live' and state(server, a.id, 13) == 'off',
                 lambda: 'the first channel did not turn the second off: %s' % server.input(a.id))
        check(server.input(a.id)['events'] == 0, 'events were sent with no input')
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


class LineReceiver:
    """blit-compositor's end of an `INPUT sock` route: a stream socket whose
    connection carries `INPUT <kind> <id> ...` lines, each event followed by
    `INPUT frame <id> <us>`, and `INPUT keymap <id> <revision> <size>
    <rate> <delay>`, which is no event and has no frame, with a sealed
    memfd of the keymap passed with it."""

    def __init__(self, work, name='input-0'):
        self.path = os.path.join(work, name)
        self.name = name
        self.listener = listening_socket(self.path)
        self.listener.settimeout(5)
        self.conn = None
        self.buffer = b''
        self.events = []      # the lines, frames apart
        self.stamps = []      # (event, the monotonic us of its frame), every one read
        self.notices = []     # `keymap <id> <revision>`
        self.keymaps = []     # (text, rate, delay) of each keymap line
        self.fds = []         # passed and not yet matched to a line
        self.frames = 0
        self.total = 0        # every event line read
        self.eof = False

    def accept(self):
        self.conn, _ = self.listener.accept()

    def reaccept(self):
        """A new connection on the same socket, after the last one ended."""
        if self.conn is not None:
            self.conn.close()
        self.conn = None
        self.buffer = b''
        self.events, self.notices, self.keymaps, self.fds = [], [], [], []
        self.frames = self.total = 0
        self.eof = False
        self.accept()

    def connected(self, wait=0.3):
        """Whether Blit connected (an accept pending), taking it if so."""
        self.listener.settimeout(wait)
        try:
            self.accept()
            return True
        except (socket.timeout, BlockingIOError):
            return False
        finally:
            self.listener.settimeout(5)

    def read(self, wait):
        if self.eof:
            return False
        self.conn.settimeout(wait)
        try:
            data, fds, _, _ = socket.recv_fds(self.conn, 65536, 8)
        except socket.timeout:
            return False
        self.fds += fds
        if not data:
            self.eof = True
            return False
        self.buffer += data
        while b'\n' in self.buffer:
            line, self.buffer = self.buffer.split(b'\n', 1)
            words = line.decode().split(' ')
            check(len(words) >= 3 and words[0] == 'INPUT', 'not an input line: %r' % line)
            if words[1] == 'frame':
                check(len(words) == 4 and int(words[3]) > 0, 'a frame line %r' % line)
                self.frames += 1
                self.stamps[-1] = (self.stamps[-1][0], int(words[3]))
            elif words[1] == 'keymap':
                check(len(words) == 7 and int(words[3]) > 0 and self.total == self.frames,
                      'a keymap line %r' % line)
                check(self.fds, 'no memfd passed with %r' % line)
                fd = self.fds.pop(0)
                try:
                    seals = fcntl.fcntl(fd, 1034)  # F_GET_SEALS
                    check(seals & 0x0e == 0x0e, 'the keymap memfd is not sealed: %#x' % seals)
                    size = int(words[4])
                    check(os.fstat(fd).st_size == size, 'the keymap memfd holds %d bytes, the line says %d'
                          % (os.fstat(fd).st_size, size))
                    text = os.pread(fd, size, 0)
                finally:
                    os.close(fd)
                check(text.endswith(b'\0') and text.startswith(b'xkb_keymap'),
                      'the keymap memfd is not xkb text and a NUL: %r' % text[:40])
                self.keymaps.append((text[:-1].decode(), int(words[5]), int(words[6])))
                self.notices.append(' '.join(words[1:4]))
            else:
                check(self.total == self.frames, 'an event before the last one\'s frame: %r' % line)
                self.total += 1
                self.events.append(' '.join(words[1:]))
                self.stamps.append((' '.join(words[1:]), 0))
        return True

    def expect(self, wanted, why):
        deadline = time.time() + 5
        while len(self.events) < len(wanted) and time.time() < deadline and not self.eof:
            self.read(0.2)
        got, self.events = self.events[:len(wanted)], self.events[len(wanted):]
        check(got == wanted, '%s: %s got %s, expected %s' % (why, self.name, got, wanted))

    def nothing(self, why):
        self.read(0.4)
        check(self.events == [], '%s: %s got %s' % (why, self.name, self.events))

    def ended(self, why):
        """Blit closed the connection, with nothing more sent."""
        deadline = time.time() + 5
        while not self.eof and time.time() < deadline:
            self.read(0.2)
        check(self.eof and self.events == [], '%s: %s eof=%s, got %s' % (why, self.name, self.eof, self.events))

    def drain(self):
        self.read(0.3)
        self.events = []

    def stamp(self, event, last=True):
        """The frame stamp of the last (or first) `event` read."""
        found = [us for e, us in self.stamps if e == event]
        check(found, '%s was never sent %s' % (self.name, event))
        return found[-1] if last else found[0]

    def close(self):
        if self.conn is not None:
            self.conn.close()
        self.listener.close()


def confined(server, a, ch, receiver, placement, point, origin):
    """Hold `confine` (ADR 0225's hold modes, on the sock route): under
    ?7701 the pointer stays shown, moves inside the picture and is sent as
    a position."""
    place(a, 7, 3, 5, 20, 5)
    at = wait_for(lambda: (lambda p: p if p['width'] == 160 else None)(placement()),
                  'placement 160x80 was not drawn')
    check(ch.ask('INPUT hold 7 confine\n') == b'INPUT ok 7\n', 'INPUT hold confine was refused')
    check(route(server.input(a.id), 7)['hold'] == 'confine', 'hold %s' % server.input(a.id))
    point(at, 10, 10, 'absolute before the confined lock')

    def at_point(sx, sy):
        return (origin[0] + at['x'] + sx, origin[1] + at['y'] + sy)

    def ptr_line(sx, sy):
        return 'ptr 7 %.2f %.2f' % (sx * FRAME_W / at['width'], sy * FRAME_H / at['height'])

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
    receiver.expect([ptr_line(15, 13)], 'confined motion')
    check(server.pointer() == at_point(15, 13), 'the confined pointer is at %s' % (server.pointer(),))
    wait_for(lambda: deltas()[before:] == [(b'5', b'3')], lambda: 'the holder was sent %r' % deltas()[before:])
    # Clamped at the far corner, then the near one.
    server.inject({'kind': 'pointer_relative', 'dx': 10000, 'dy': 10000})
    receiver.expect([ptr_line(at['width'] - 1, at['height'] - 1)], 'confined to the far corner')
    check(server.pointer() == at_point(at['width'] - 1, at['height'] - 1),
          'the pointer past the far corner is at %s' % (server.pointer(),))
    server.inject({'kind': 'pointer_relative', 'dx': 3, 'dy': 0})
    receiver.nothing('motion against the far edge')
    server.inject({'kind': 'pointer_relative', 'dx': -10000, 'dy': -10000})
    receiver.expect([ptr_line(0, 0)], 'confined to the top-left corner')
    check(server.pointer() == at_point(0, 0), 'the pointer past the top-left is at %s' % (server.pointer(),))
    ptr = route(server.input(a.id), 7)['pointer']
    check(ptr['miss'] == '' and ptr['sent'] == [0.0, 0.0] and ptr['draw'][2:] == [at['width'], at['height']],
          'the diagnostic under confine: %s' % ptr)
    # Buttons still go to the compositor, and to the holder in the lock's form.
    server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
    server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
    receiver.expect(['btn 7 272 press', 'btn 7 272 release'], 'a click under confine')
    wait_for(lambda: b'\x1b[<0;0;0m' in a.sent(), 'the holder was not sent the click')
    check(not server.hidden(), 'the pointer was hidden under confine')

    # The picture no longer drawn: the lock is relative again, hidden and
    # raw, and the pointer does not move; drawn again, confined again.
    a.write('\033_Ga=d,d=i,i=7,q=2\033\\')
    wait_for(lambda: a.graphics(7) is not None and not a.graphics(7)['placements'],
             'the placement was not removed')
    wait_for(lambda: server.hidden(), 'an undrawn confined picture did not hide the pointer')
    server.inject({'kind': 'pointer_relative', 'dx': 4, 'dy': 2})
    receiver.expect(['rel 7 4 2'], 'relative motion with the picture not drawn')
    check(server.pointer() == at_point(0, 0), 'the pointer moved with the picture not drawn')
    place(a, 7, 3, 5, 20, 5)
    wait_for(lambda: (lambda p: p if p['width'] == 160 else None)(placement()), 'the picture was not drawn again')
    wait_for(lambda: not server.hidden(), 'the picture drawn again did not show the pointer')
    server.inject({'kind': 'pointer_relative', 'dx': 30, 'dy': 20})
    receiver.expect([ptr_line(30, 20)], 'confined again')

    # Given back: the pointer stays where it was, and later motion is an
    # ordinary absolute pointer over the picture.
    a.write('\033[?7701l')
    wait_for(lambda: not server.locked(a.id), 'the confined lock was not given back')
    time.sleep(0.2)
    check(server.pointer() == at_point(30, 20), 'the pointer moved when the lock ended: %s' % (server.pointer(),))
    check(not server.hidden(), 'the pointer was hidden after the lock')
    receiver.nothing('the lock ending')
    server.inject({'kind': 'pointer_relative', 'dx': 20, 'dy': 10})
    receiver.expect([ptr_line(50, 30)], 'ordinary motion after the confined lock')
    check(server.pointer() == at_point(50, 30), 'the pointer after the lock is at %s' % (server.pointer(),))
    check(ch.ask('INPUT hold 7 relative\n') == b'INPUT ok 7\n', 'INPUT hold relative was refused')
    return 'confine=shown,clamped@0,0+%d,%d,absolute_only,raw_to_holder,buttons,undrawn=relative,released=absolute' % (
        at['width'] - 1, at['height'] - 1)


def multi(server, a, work, b):
    """Several routes, a socket each, as a desktop has one per window's
    picture over one channel. Returns what the PASS line says of it."""
    ra, rb, rc, rd = (LineReceiver(work, name) for name in ('input-a', 'input-b', 'input-c', 'input-d'))
    receivers = (ra, rb, rc, rd)

    def key(code, *values):
        for v in values:
            server.inject({'kind': 'key', 'code': code, 'value': v})

    def live(image):
        wait_for(lambda: state(server, a.id, image) == 'live',
                 lambda: 'route %d is not live: %s' % (image, server.input(a.id)))

    try:
        server.focus(a.id)
        ch = Channel(a)
        for image, column in ((20, 1), (21, 21)):
            ch.frame(image, 1)
            wait_for(lambda: a.graphics(image), 'image %d did not land' % image)
            place(a, image, 1, column, 16, 4)
        # Two images on one channel: a route each, no EBUSY.
        check(ch.ask('INPUT sock 20 %s\n' % ra.path) == b'INPUT ok 20\n', 'route 20 was refused')
        check(ch.ask('INPUT sock 21 %s\n' % rb.path) == b'INPUT ok 21\n', 'route 21 on the same channel was refused')
        ra.accept()
        rb.accept()
        # The same image twice on one channel is EBUSY, before any connect.
        check(ch.ask('INPUT sock 20 %s\n' % rb.path) == b'INPUT error 20 EBUSY\n', 'image 20 twice was not EBUSY')
        check(not rb.connected(), 'an EBUSY request connected')
        ch.line('INPUT on 20\n')
        live(20)
        ra.read(0.3)
        check(len(ra.notices) == 1, 'route 20 live without its keymap: %s' % ra.notices)
        rb.nothing('route 21 while 20 is on')
        check(rb.notices == [], 'route 21 sent a keymap while off: %s' % rb.notices)

        # INPUT on 21: 20 releases what it held before 21 gets anything.
        key(30, 1)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        ra.expect(['key 20 30 press', 'btn 20 272 press'], 'held through route 20')
        rb.nothing('keys while 20 is on')
        ch.line('INPUT on 21\n')
        ra.expect(['key 20 30 release', 'btn 20 272 release'], 'release-all on another route on')
        live(21)
        rb.read(0.3)
        check(len(rb.notices) == 1, 'route 21 live without its keymap: %s' % rb.notices)
        rb.nothing('route 21 before its first key')
        key(30, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        key(31, 1, 0)
        rb.expect(['key 21 31 press', 'key 21 31 release'], 'keys to route 21 only')
        ra.nothing('the releases of what 20 already released, and keys after 21 on')
        check(ra.stamp('btn 20 272 release') <= rb.stamp('key 21 31 press', last=False),
              'route 20 released after route 21 was sent a key')
        info = server.input(a.id)
        check(route(info, 20)['state'] == 'off' and route(info, 21)['state'] == 'live',
              'frames.input after INPUT on 21 %s' % info)

        # A late ENOENT on route 20 leaves 21 working.
        ra.conn.close()
        got = ch.reply(5.0)
        check(got == b'INPUT error 20 ENOENT\n', 'a closed socket on route 20 was answered %r' % got)
        wait_for(lambda: route(server.input(a.id), 20) is None, 'route 20 outlived its compositor')
        key(32, 1, 0)
        rb.expect(['key 21 32 press', 'key 21 32 release'], 'route 21 after route 20 was dropped')
        check(state(server, a.id, 21) == 'live', 'route 21 after the drop %s' % server.input(a.id))
        check(ch.ask('INPUT on 20\n') == b'INPUT error 20 EINVAL\n', 'INPUT on 20 after its end was not EINVAL')

        # Deleting one image ends only its route: 22 live and holding a key,
        # its image deleted; 21 still works.
        ch.frame(22, 1)
        wait_for(lambda: a.graphics(22), 'image 22 did not land')
        place(a, 22, 6, 1, 16, 4)
        check(ch.ask('INPUT sock 22 %s\n' % rc.path) == b'INPUT ok 22\n', 'route 22 was refused')
        rc.accept()
        ch.line('INPUT on 22\n')
        live(22)
        check(state(server, a.id, 21) == 'off', 'route 21 still on with 22 on: %s' % server.input(a.id))
        key(33, 1)
        rc.expect(['key 22 33 press'], 'held through route 22')
        rb.nothing('route 21 while 22 is on')
        a.write('\033_Ga=d,d=I,i=22,q=2\033\\')
        rc.expect(['key 22 33 release'], 'release-all on image 22 deleted')
        rc.ended('route 22 ended with its image')
        key(33, 0)
        wait_for(lambda: route(server.input(a.id), 22) is None, 'route 22 outlived its image')
        check(state(server, a.id, 21) == 'off', 'route 21 after image 22 went %s' % server.input(a.id))
        check(ch.ask('INPUT on 22\n') == b'INPUT error 22 EINVAL\n', 'INPUT on 22 after its image went was not EINVAL')
        ch.line('INPUT on 21\n')
        live(21)
        key(34, 1, 0)
        rb.expect(['key 21 34 press', 'key 21 34 release'], 'route 21 after image 22 was deleted')
        rb.nothing('route 21 after image 22 was deleted')

        # Two channels of the session: one route on between them.
        ch2 = Channel(a)
        ch2.frame(23, 1)
        wait_for(lambda: a.graphics(23), 'image 23 did not land')
        check(ch2.ask('INPUT sock 23 %s\n' % rd.path) == b'INPUT ok 23\n', 'route 23 on a second channel was refused')
        rd.accept()
        key(35, 1)
        rb.expect(['key 21 35 press'], 'held through route 21')
        ch2.line('INPUT on 23\n')
        rb.expect(['key 21 35 release'], 'release-all when the other channel turns its route on')
        live(23)
        key(35, 0)
        key(36, 1, 0)
        rd.expect(['key 23 36 press', 'key 23 36 release'], 'keys to the second channel only')
        rb.nothing('keys to the first channel while the second is on')
        check(rb.stamp('key 21 35 release') <= rd.stamp('key 23 36 press', last=False),
              'route 21 released after route 23 was sent a key')
        ch.line('INPUT on 21\n')
        live(21)
        info = server.input(a.id)
        check(route(info, 21)['state'] == 'live' and route(info, 23)['state'] == 'off',
              'frames.input across two channels %s' % info)
        ch2.close()
        rd.ended('the second channel closed')
        key(37, 1)
        rb.expect(['key 21 37 press'], 'held before the first channel closes')
        ch.close()
        rb.expect(['key 21 37 release'], 'the first channel closed, its live route released')
        rb.ended('the first channel closed')
        key(37, 0)
        wait_for(lambda: server.input(a.id)['routes'] == [], 'routes outlived their channels')

        # One route per window socket (ADR 0229): route 27, on another
        # channel, to route 26's socket -- as a desktop asking again under a
        # new picture id, or on another display's channel -- ends 26: what
        # it held released first, its channel told ENOENT, last_error
        # `superseded`; 27 takes the keys on the same socket.
        ch3, ch4 = Channel(a), Channel(a)
        ch3.frame(26, 1)
        ch4.frame(27, 1)
        wait_for(lambda: a.graphics(26) and a.graphics(27), 'images 26 and 27 did not land')
        check(ch3.ask('INPUT sock 26 %s\n' % ra.path) == b'INPUT ok 26\n', 'route 26 was refused')
        ra.reaccept()
        ch3.line('INPUT on 26\n')
        live(26)
        key(38, 1)
        ra.expect(['key 26 38 press'], 'held through route 26')
        check(ch4.ask('INPUT sock 27 %s\n' % ra.path) == b'INPUT ok 27\n', 'route 27 to 26\'s socket was refused')
        ra.expect(['key 26 38 release'], 'release-all when route 26 is superseded')
        ra.ended('route 26 superseded')
        got = ch3.reply(5.0)
        check(got == b'INPUT error 26 ENOENT\n', 'the superseded route 26 was answered %r' % got)
        info = server.input(a.id)
        check(route(info, 26) is None and info.get('last_error') == 'superseded',
              'frames.input after route 26 was superseded %s' % info)
        ra.reaccept()
        ch4.line('INPUT on 27\n')
        live(27)
        key(38, 0)
        key(39, 1, 0)
        ra.expect(['key 27 39 press', 'key 27 39 release'], 'keys to route 27 on the same socket')

        # Per session: another session's route to the same socket -- a
        # desktop gives each display's session its own route to a window --
        # does not end 27; only the session with the keyboard delivers.
        chb = Channel(b)
        chb.frame(29, 1)
        wait_for(lambda: b.graphics(29), 'image 29 did not land')
        check(chb.ask('INPUT sock 29 %s\n' % ra.path) == b'INPUT ok 29\n',
              'route 29 from another session to 27\'s socket was refused')
        check(chb.reply(0.3) == b'' and ch4.reply(0.3) == b'',
              'a route in another session was superseded across sessions')
        check(route(server.input(a.id), 27) is not None and route(server.input(b.id), 29) is not None,
              'routes 27 and 29 do not both stand: %s %s' % (server.input(a.id), server.input(b.id)))
        key(40, 1, 0)
        ra.expect(['key 27 40 press', 'key 27 40 release'], 'keys to the focused session\'s route only')
        chb.close()

        # A route before its picture has come says `picture: absent`, then
        # `present` once it lands.
        check(ch4.ask('INPUT sock 28 %s\n' % rc.path) == b'INPUT ok 28\n', 'route 28 was refused')
        rc.reaccept()
        ch4.line('INPUT on 28\n')
        live(28)
        check(route(server.input(a.id), 28)['picture'] == 'absent',
              'route 28 before its picture: %s' % server.input(a.id))
        ch4.frame(28, 1)
        wait_for(lambda: route(server.input(a.id), 28)['picture'] == 'present',
                 lambda: 'route 28 after its picture landed: %s' % server.input(a.id))
        ch3.close()
        ch4.close()
        ra.ended('the fourth channel closed')
        rc.ended('the fourth channel closed')
        wait_for(lambda: server.input(a.id)['routes'] == [], 'routes outlived their channels')
    finally:
        for r in receivers:
            r.close()
    return ('routes=two_per_channel ebusy=same_image,no_connect on_releases_other=stamped_before '
            'late_enoent=one_route delete=one_route one_on=two_channels '
            'one_route_per_socket=per_session(superseded,released,ENOENT;other_session_kept) picture=absent->present')


def sock():
    """`INPUT sock`: every input case, read off the socket (see the top)."""
    work = tempfile.mkdtemp(prefix='blit-input-sock-gate-')
    server = Server(19837 + OFFSET, work)
    receivers = []
    try:
        a = Session(server, work, 'a')
        b = Session(server, work, 'b')
        server.focus(a.id)
        ch = Channel(a)
        ch.frame(7, 1)
        wait_for(lambda: a.graphics(7), 'image 7 did not land')
        place(a, 7, 3, 5, 20, 5)
        check(ch.ask('INPUT sock 7 %s\n' % os.path.join(work, 'missing')) == b'INPUT error 7 ENOENT\n',
              'a missing input socket was not ENOENT')
        check(ch.ask('INPUT sock 7 relative\n') == b'INPUT error 7 EINVAL\n', 'a relative path was not EINVAL')
        receiver = LineReceiver(work)
        receivers.append(receiver)
        got = ch.ask('INPUT sock 7 %s\n' % receiver.path)
        check(got == b'INPUT ok 7\n', 'INPUT sock was answered %r' % got)
        receiver.accept()
        check(ch.ask('INPUT sock 7 %s\n' % receiver.path) == b'INPUT error 7 EBUSY\n', 'a second route was not EBUSY')
        check(state(server, a.id, 7) == 'off', 'frames.input %s' % server.input(a.id))
        receiver.nothing('a route that starts off')
        check(receiver.notices == [], 'a keymap notice to a route that is off: %s' % receiver.notices)
        ch.line('INPUT on 7\n')
        wait_for(lambda: state(server, a.id, 7) == 'live', 'the route did not go live')
        # Live: told the keymap's revision once (ADR 0228), and not again.
        receiver.read(0.3)
        check(len(receiver.notices) == 1 and receiver.notices[0].startswith('keymap 7 '),
              'the keymap notice when the route went live: %s' % receiver.notices)
        # The keymap itself, one source with GET /v1/input/keymap: its text
        # and its repeat headers.
        with urllib.request.urlopen(server.base + '/v1/input/keymap', timeout=10) as response:
            served = response.read().decode()
            served_repeat = (int(response.headers['X-Blit-Repeat-Rate']),
                             int(response.headers['X-Blit-Repeat-Delay']))
        text, rate, delay = receiver.keymaps[0]
        check(text == served, 'the route\'s keymap (%d bytes) is not what GET /v1/input/keymap serves '
              '(%d bytes)' % (len(text), len(served)))
        check((rate, delay) == served_repeat, 'the route\'s repeat %s, the API\'s %s'
              % ((rate, delay), served_repeat))

        def key(code, *values):
            for v in values:
                server.inject({'kind': 'key', 'code': code, 'value': v})

        def live(image=7):
            # The channel and the API are two sockets: an injected event
            # only once the route is live.
            wait_for(lambda: state(server, a.id, image) == 'live',
                     lambda: 'route %d is not live: %s' % (image, server.input(a.id)))

        # Press and release, in order; the kernel's repeats are not sent.
        key(30, 1, 2, 2, 0)
        receiver.expect(['key 7 30 press', 'key 7 30 release'], 'a key with repeats')
        key(48, 1)
        key(46, 1)
        key(48, 0)
        key(46, 0)
        receiver.expect(['key 7 48 press', 'key 7 46 press', 'key 7 48 release', 'key 7 46 release'],
                        'overlapping keys')

        # Nothing while off, nor while another session has focus.
        ch.line('INPUT off 7\n')
        wait_for(lambda: state(server, a.id, 7) == 'off', 'INPUT off did not take')
        key(31, 1, 0)
        receiver.nothing('keys while off')
        ch.line('INPUT on 7\n')
        live()
        server.focus(b.id)
        key(31, 1, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        server.inject({'kind': 'pointer_wheel', 'delta': 1})
        receiver.nothing('input while unfocused')
        server.focus(a.id)
        live()

        # Everything held is released: INPUT off ...
        key(32, 1)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': True})
        receiver.expect(['key 7 32 press', 'btn 7 272 press'], 'a key and a button held')
        ch.line('INPUT off 7\n')
        receiver.expect(['key 7 32 release', 'btn 7 272 release'], 'release-all on INPUT off')
        key(32, 0)
        server.inject({'kind': 'pointer_button', 'button': 0, 'pressed': False})
        receiver.nothing('the physical releases after INPUT off')
        ch.line('INPUT on 7\n')
        live()
        # ... a focus change ...
        key(33, 1)
        server.inject({'kind': 'pointer_button', 'button': 2, 'pressed': True})
        receiver.expect(['key 7 33 press', 'btn 7 273 press'], 'held before the focus change')
        server.focus(b.id)
        receiver.expect(['key 7 33 release', 'btn 7 273 release'], 'release-all on focus change')
        key(33, 0)
        server.inject({'kind': 'pointer_button', 'button': 2, 'pressed': False})
        receiver.nothing('the physical releases after the focus change')
        server.focus(a.id)
        live()
        # ... and Blit giving up the console.
        key(34, 1)
        receiver.expect(['key 7 34 press'], 'held before the console goes')
        server.process.send_signal(signal.SIGUSR1)
        receiver.expect(['key 7 34 release'], 'release-all on console release')
        key(31, 1, 0)
        receiver.nothing('keys while the console is away')
        check(state(server, a.id, 7) == 'on', 'frames.input while away %s' % server.input(a.id))
        server.process.send_signal(signal.SIGUSR2)
        live()
        key(34, 0)
        receiver.nothing('the release of a key already released')

        # Wheel notches, positive right and down.
        server.inject({'kind': 'pointer_wheel', 'delta': 1})
        server.inject({'kind': 'pointer_wheel', 'delta': -1})
        receiver.expect(['wheel 7 0 -1', 'wheel 7 0 1'], 'wheel up then down')

        # The absolute pointer, in the picture's own pixels (its 320x160
        # frame), wherever and however large it is drawn. The surface's
        # origin on the display, from the session's own pixel reports.
        server.inject({'kind': 'pointer_motion', 'x': 301, 'y': 101})
        report = wait_for(lambda: re.findall(rb'\x1b\[<35;(\d+);(\d+)M', a.sent()),
                          'no pixel report: %r' % a.sent()[-200:])[-1]
        origin = (301 - (int(report[0]) - 1), 101 - (int(report[1]) - 1))
        receiver.drain()  # motion so far was outside the picture

        def placement():
            g = wait_for(lambda: a.graphics(7), 'image 7 is gone')
            p = [p for p in g['placements'] if p['id'] == 1]
            check(len(p) == 1, 'placement 1 of image 7: %s' % g['placements'])
            return p[0]

        def point(at, sx, sy, why):
            server.inject({'kind': 'pointer_motion', 'x': origin[0] + at['x'] + sx,
                           'y': origin[1] + at['y'] + sy})
            want = (sx * FRAME_W / at['width'], sy * FRAME_H / at['height'])
            receiver.expect(['ptr 7 %.2f %.2f' % want], why)
            # The diagnostic says the same as what the compositor received.
            ptr = route(server.input(a.id), 7)['pointer']
            check(ptr['miss'] == '' and ptr['over'] == 7 and ptr['draw'][2:] == [at['width'], at['height']] and
                  abs(ptr['sent'][0] - want[0]) < 0.06 and abs(ptr['sent'][1] - want[1]) < 0.06,
                  'frames.input pointer diagnostic for %s: %s' % (why, ptr))

        cases = []
        for row, column, columns, rows, sx, sy in ((3, 5, 20, 5, 40, 20), (1, 1, 40, 10, 100, 50)):
            place(a, 7, row, column, columns, rows)
            at = wait_for(lambda: (lambda p: p if p['width'] == columns * 8 else None)(placement()),
                          'placement %dx%d was not drawn' % (columns, rows))
            point(at, sx, sy, 'absolute at %d,%d %dx%d' % (at['x'], at['y'], at['width'], at['height']))
            point(at, 0, 0, 'the top-left of %dx%d' % (at['width'], at['height']))
            point(at, at['width'] - 1, at['height'] - 1, 'the far corner of %dx%d' % (at['width'], at['height']))
            cases.append('%dx%d@%d,%d' % (at['width'], at['height'], at['x'], at['y']))

        # INPUT member: popup 9 (a 160x80 frame) joins 7's route; the
        # pointer over it is its offset plus the point within it, in 7's
        # pixels; it ends with its picture.
        ch.frame(9, 1, 160, 80)
        wait_for(lambda: a.graphics(9), 'image 9 did not land')
        a.write('\033[4;8H\033_Ga=p,i=9,p=1,q=2,C=1,c=8,r=2,z=1\033\\')
        p9 = wait_for(lambda: [p for p in (a.graphics(9) or {}).get('placements', []) if p['id'] == 1],
                      'image 9 was not placed')[0]
        for line, want in (('INPUT member 9 8 12 63\n', b'INPUT error 9 EINVAL\n'),   # 8 has no route
                           ('INPUT member 7 7 12 63\n', b'INPUT error 7 EINVAL\n'),   # itself
                           ('INPUT member 9 7 12\n', b'INPUT error 9 EINVAL\n'),      # no y
                           ('INPUT member 9 7 1 x\n', b'INPUT error 9 EINVAL\n'),
                           ('INPUT member 9 7 40000 1\n', b'INPUT error 9 EINVAL\n'),
                           ('INPUT member x 7 1 1\n', b'INPUT error 0 EINVAL\n'),
                           ('INPUT member 9 7 12 63\n', b'INPUT ok 9\n')):
            got = ch.ask(line)
            check(got == want, '%r was answered %r, not %r' % (line, got, want))
        r = route(server.input(a.id), 7)
        check(r['members'] == [{'image': 9, 'x': 12, 'y': 63}], 'frames.input members %s' % r)
        receiver.drain()
        for sx, sy in ((0, 0), (p9['width'] - 1, p9['height'] - 1)):
            server.inject({'kind': 'pointer_motion', 'x': origin[0] + p9['x'] + sx,
                           'y': origin[1] + p9['y'] + sy})
            receiver.expect(['ptr 7 %.2f %.2f' % (12 + sx * 160 / p9['width'], 63 + sy * 80 / p9['height'])],
                            'the pointer at %d,%d of member 9' % (sx, sy))
        check(route(server.input(a.id), 7)['pointer']['over'] == 9, 'the pointer was not over 9')
        # Moved, past the top-level's left and top.
        check(ch.ask('INPUT member 9 7 -4 -6\n') == b'INPUT ok 9\n', 'a member moved')
        server.inject({'kind': 'pointer_motion', 'x': origin[0] + p9['x'], 'y': origin[1] + p9['y']})
        receiver.expect(['ptr 7 -4.00 -6.00'], 'a member at a negative offset')
        # Its picture deleted: the member ends, the point is 7's again.
        a.write('\033_Ga=d,d=I,i=9,q=2\033\\')
        wait_for(lambda: route(server.input(a.id), 7)['members'] == [], 'the member outlived its picture')
        server.inject({'kind': 'pointer_motion', 'x': origin[0] + p9['x'] + 1, 'y': origin[1] + p9['y']})
        receiver.expect(['ptr 7 %.2f %.2f' % ((p9['x'] + 1 - at['x']) * FRAME_W / at['width'],
                                              (p9['y'] - at['y']) * FRAME_H / at['height'])],
                        'the pointer where the member was')
        check(ch.ask('INPUT member 9 7 0 0\n') == b'INPUT error 9 EINVAL\n', 'a deleted member was taken')
        cases.append('member:9@12,63+-4,-6')

        # Resized in place: the same placement, smaller.
        place(a, 7, 1, 1, 8, 2)
        at = wait_for(lambda: (lambda p: p if p['width'] == 64 else None)(placement()), 'the resize was not drawn')
        point(at, 10, 5, 'absolute after the resize')
        point(at, at['width'] - 1, at['height'] - 1, 'the far corner after the resize')
        cases.append('resized:%dx%d' % (at['width'], at['height']))
        server.inject({'kind': 'pointer_motion', 'x': origin[0] + at['x'] + 200, 'y': origin[1] + at['y'] + 100})
        receiver.nothing('the pointer outside the picture')
        miss = route(server.input(a.id), 7)['pointer']['miss']
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
        deadline = time.time() + 5
        total, moves = [0, 0], 0
        while total != [70, -30] and time.time() < deadline:
            receiver.read(0.2)
            for e in receiver.events:
                w = e.split(' ')
                check(w[0] == 'rel' and w[1] == '7', 'under the lock: %s' % e)
                total[0] += int(w[2])
                total[1] += int(w[3])
                moves += 1
            receiver.events = []
        check(total == [70, -30], 'relative motion summed to %s' % total)
        check(server.pointer() == still, 'the relative lock moved the pointer: %s -> %s' % (still, server.pointer()))
        a.write('\033[?7701l')
        wait_for(lambda: not server.locked(a.id), 'the lock was not given back')
        wait_for(lambda: not server.hidden(), 'the pointer was not shown after the lock')
        receiver.drain()

        confine = confined(server, a, ch, receiver, placement, point, origin)
        notices = ','.join(receiver.notices)
        check(len(receiver.notices) == 1, 'keymap notices with no keyboard change: %s' % receiver.notices)

        # The channel closed: released, then the connection closed.
        key(36, 1)
        receiver.expect(['key 7 36 press'], 'held before the channel closes')
        events = server.input(a.id)['events']
        ch.close()
        receiver.expect(['key 7 36 release'], 'release-all on channel close')
        receiver.ended('the channel closed')
        wait_for(lambda: server.input(a.id)['routes'] == [], 'the route outlived its channel')
        key(36, 0)
        check(receiver.frames == receiver.total and receiver.total >= 40,
              'events and frames: %d events, %d frames' % (receiver.total, receiver.frames))
        frames = receiver.frames

        # The image deleted, on a new channel and image. As a desktop may:
        # INPUT on as soon as INPUT ok is read, before the compositor has
        # accepted the connection; what is written waits in the socket.
        ch = Channel(a)
        ch.frame(8, 1)
        wait_for(lambda: a.graphics(8), 'image 8 did not land')
        place(a, 8, 1, 1, 8, 2)
        r8 = LineReceiver(work, 'input-8')
        receivers.append(r8)
        check(ch.ask('INPUT sock 8 %s\n' % r8.path) == b'INPUT ok 8\n', 'the second route was refused')
        ch.line('INPUT on 8\n')
        live(8)
        key(35, 1)
        r8.accept()
        r8.expect(['key 8 35 press'], 'a key before the compositor accepted')
        check(len(r8.notices) == 1, 'the keymap before the compositor accepted: %s' % r8.notices)
        # The route ends with its image: released, then closed.
        a.write('\033_Ga=d,d=I,i=8,q=2\033\\')
        r8.expect(['key 8 35 release'], 'release-all on image delete')
        r8.ended('the route ended with its image')
        key(35, 0, 1, 0)
        info = server.input(a.id)
        check(info['routes'] == [] and info['events'] > events, 'frames.input after the delete %s' % info)
        check(ch.ask('INPUT on 8\n') == b'INPUT error 8 EINVAL\n', 'INPUT on after the image went was not EINVAL')
        ch.close()

        # The compositor goes away under a live route: the client is told
        # INPUT error ENOENT at once, and the route ends.
        ch = Channel(a)
        ch.frame(10, 1)
        wait_for(lambda: a.graphics(10), 'image 10 did not land')
        place(a, 10, 1, 1, 8, 2)
        r10 = LineReceiver(work, 'input-10')
        receivers.append(r10)
        check(ch.ask('INPUT sock 10 %s\n' % r10.path) == b'INPUT ok 10\n', 'the third route was refused')
        r10.accept()
        ch.line('INPUT on 10\n')
        live(10)
        check(ch.reply(0.3) == b'', 'a live route was answered')
        r10.conn.close()
        got = ch.reply(5.0)
        check(got == b'INPUT error 10 ENOENT\n', 'a closed input socket was answered %r' % got)
        info = wait_for(lambda: (lambda i: i if i['routes'] == [] else None)(server.input(a.id)),
                        'the route outlived its compositor')
        check(info['last_error'] == 'sock-disconnected', 'frames.input after the close %s' % info)
        check(ch.ask('INPUT on 10\n') == b'INPUT error 10 EINVAL\n', 'INPUT on after the route ended was not EINVAL')

        # A line the compositor will not take: written into a socket whose
        # reader shut down, it is counted, and the route ends.
        r11 = LineReceiver(work, 'input-11')
        receivers.append(r11)
        check(ch.ask('INPUT sock 10 %s\n' % r11.path) == b'INPUT ok 10\n', 'a route again')
        r11.accept()
        ch.line('INPUT on 10\n')
        live(10)
        r11.read(0.3)
        before = server.input(a.id)['dropped']
        r11.conn.shutdown(socket.SHUT_RD)
        key(38, 1)
        got = ch.reply(5.0)
        check(got == b'INPUT error 10 ENOENT\n', 'a socket that took no line was answered %r' % got)
        info = server.input(a.id)
        check(info['dropped'] == before + 1 and info['last_error'] == 'sock-disconnected' and
              route(info, 10) is None, 'frames.input after a line was not taken %s' % info)
        key(38, 0)
        ch.close()

        several = multi(server, a, work, b)
    finally:
        server.stop()
        for r in receivers:
            r.close()
    print('FRAME_INPUT_SOCK_PASS answers=OK,ENOENT,EINVAL,EBUSY keys=ordered repeats=dropped '
          'off+unfocused+console=silent release_all=off,focus,console,close,delete wheel=0,-1/0,1 '
          'ptr=frame-pixels absolute=%s outside=silent relative=70,-30_in_%d,hidden,still %s '
          'keymap=memfd,at-live,once,=api(%s) on_before_accept=queued closed=ENOENT '
          'not-taken=dropped+ENOENT frames=%d %s'
          % (','.join(cases), moves, confine, notices, frames, several))


if __name__ == '__main__':
    mode = sys.argv[1] if len(sys.argv) > 1 else 'local'
    if mode == 'sock':
        TAG = 'FRAME_INPUT_SOCK'
        sock()
    else:
        local()
