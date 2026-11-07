#!/usr/bin/env python3
"""ADR 0223: frames by dmabuf, the parts that need no GPU import.

Version 3 of the frame channel (ADR 0202) adds FRM3, a frame whose pixels
are dmabuf planes the GPU compositor imports. A gate host has no GPU that
can import a dmabuf (the surfaceless llvmpipe has no
EGL_EXT_image_dma_buf_import), so this gate proves what holds without one:

- the hello's answer offers dmabuf only when GET /v1/diagnostics says the
  compositor can import, and says `OK\\n` to version 1 and 2 clients as before;
- a well-formed FRM3 that cannot be imported here is refused ENOTSUP, and
  counted as `unavailable`;
- a malformed FRM3 is EINVAL wherever it is sent (wrong size, plane count,
  descriptor count, unused plane fields, image id, a version 1 client), and
  an oversized one EFBIG;
- memfd frames keep working on a version 3 connection;
- the counts per session and in /v1/diagnostics.

Import, drawing, the fence and RELEASE are tools/tests/frame_dmabuf_live_gate.sh,
on a host with a GPU. Run by frame_dmabuf_gate.sh, which sets up the mock
display (BLIT_GATE_BACKEND=native for the CPU compositor, gles for the GPU
one).
"""
import fcntl
import json
import os
import socket
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request
import zlib

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19822')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT
SEALS = fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE


def fourcc(code):
    return struct.unpack('<I', code.encode())[0]


def fail(why):
    print('FRAME_DMABUF_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def api(method, path):
    request = urllib.request.Request(BASE + path, method=method)
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()
        return json.loads(raw) if raw else {}


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


def memfd(data, seal=True):
    fd = os.memfd_create('blit-gate-dmabuf', os.MFD_ALLOW_SEALING)
    os.write(fd, data)
    if seal:
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, SEALS)
    return fd


def frm1(image_id, sequence, width, height, fmt=24):
    stride = width * (3 if fmt == 24 else 4)
    return b'FRM1' + struct.pack('<IQIIIIIIII', image_id, sequence, width, height,
                                 fmt, stride, 0, 0, 0, 0)


def frm3(image_id, sequence, width, height, code='XR24', modifier=0, planes=1,
         offsets=(0, 0, 0, 0), pitches=None):
    pitches = pitches if pitches is not None else (width * 4, 0, 0, 0)
    body = struct.pack('<IQIIIIQ', image_id, sequence, width, height, fourcc(code),
                       planes, modifier)
    for i in range(4):
        body += struct.pack('<II', offsets[i], pitches[i])
    return b'FRM3' + body


class Client:
    def __init__(self, name, token, version):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.connect('\0' + name)
        token = token.encode()
        self.socket.send(b'BLITFRM1' + struct.pack('<II', version, len(token)) + token)
        self.socket.settimeout(2.0)
        self.hello = self.socket.recv(2048)

    def send(self, header, fds):
        socket.send_fds(self.socket, [header], fds)
        for fd in fds:
            os.close(fd)

    def reply(self, wait=1.0):
        self.socket.settimeout(wait)
        try:
            return self.socket.recv(2048)
        except (socket.timeout, BlockingIOError):
            return b''


def start_session(work):
    """A session that writes its environment, so the gate has its frame
    socket and token."""
    script = os.path.join(work, 'run.sh')
    with open(script, 'w') as out:
        out.write("env > %s/env.part && mv %s/env.part %s/env; printf '\\033[?25l'; "
                  "while :; do sleep 1; done\n" % (work, work, work))
    session = json.loads(urllib.request.urlopen(urllib.request.Request(
        BASE + '/v1/terminal-sessions', method='POST',
        data=json.dumps({'command': '/bin/sh', 'arguments': [script], 'display_id': 'display-1',
                         'surface': {'width': 40, 'height': 12}}).encode(),
        headers={'Content-Type': 'application/json'}), timeout=10).read())['id']
    wait_for(lambda: os.path.exists(os.path.join(work, 'env')), 'the session did not start')
    child = dict(line.rstrip('\n').split('=', 1) for line in open(os.path.join(work, 'env')) if '=' in line)
    return session, child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN']


def mixed():
    """All or nothing: two displays, the GPU composing one and the CPU the
    other (BLIT_TEST_GLES_CPU_CONNECTOR, test mode). A picture on the CPU
    one would be accepted and never shown, so dmabufs are not offered at
    all and FRM3 is ENOTSUP -- the outputs are checked before EGL is."""
    work = tempfile.mkdtemp(prefix='blit-dmabuf-gate-mixed-')
    with open(os.environ['BLIT_DRM_MOCK_STATE'], 'w') as out:
        out.write('2 0 0 0\n1 1024 768\n2 1024 768\n')
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'), BLIT_TEST_MODE='1',
               BLIT_TEST_GLES_CPU_CONNECTOR='2')
    server = subprocess.Popen([os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server')),
                               str(PORT)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the mixed server did not start', 15)
        _, name, token = start_session(work)
        actives = wait_for(lambda: (lambda o: sorted(x['compositor']['active'] for x in o)
                                    if len(o) == 2 else None)(api('GET', '/v1/diagnostics')['render']['outputs']),
                           'two outputs did not come up')
        check(actives == ['cpu', 'gles'], 'compositors %s, expected one cpu and one gles' % actives)
        dmabuf = api('GET', '/v1/diagnostics')['graphics']['frames']['dmabuf']
        check(not dmabuf['advertised'] and dmabuf['unavailable'] == 'an output is composed by the CPU',
              'a GPU/CPU mix offers dmabufs: %s' % dmabuf)
        v3 = Client(name, token, 3)
        check(v3.hello == b'OK dmabuf=0\n', 'a mixed compositor answered %r' % v3.hello)
        v3.send(frm3(6, 1, 64, 64), [memfd(b'\0' * 64 * 64 * 4)])
        check(v3.reply().startswith(b'ERROR 6 ENOTSUP'), 'FRM3 under a GPU/CPU mix was not ENOTSUP')
        return 'mixed=cpu+gles:not_offered+ENOTSUP'
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()


def main():
    backend = os.environ.get('BLIT_GATE_BACKEND', 'native')
    mixed_result = 'mixed=gles_only'
    work = tempfile.mkdtemp(prefix='blit-dmabuf-gate-')
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'))
    server = subprocess.Popen([os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server')),
                               str(PORT)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the server did not start', 15)
        session, name, token = start_session(work)

        # What the compositor says it can import decides the hello's answer.
        outputs = wait_for(lambda: api('GET', '/v1/diagnostics')['render']['outputs'],
                           'no output in /v1/diagnostics')
        compositor = outputs[0]['compositor']['active']
        dmabuf = api('GET', '/v1/diagnostics')['graphics']['frames']['dmabuf']
        expected = {'native': 'cpu', 'gles': 'gles'}[backend]
        check(compositor == expected, 'compositor %s, expected %s' % (compositor, expected))
        if compositor == 'cpu':
            check(not dmabuf['advertised'] and dmabuf['unavailable'] == 'no GLES compositor',
                  'the CPU compositor offers dmabufs: %s' % dmabuf)
        check(not dmabuf['advertised'],
              'this host advertises dmabuf import (%s); the gate expects a GPU that cannot import, '
              'run frame_dmabuf_live_gate.sh here instead' % dmabuf)
        check(dmabuf['formats'] == '' and dmabuf['unavailable'] != '', 'diagnostics %s' % dmabuf)

        # Versions 1 and 2 are answered as before; version 3 is told no.
        check(Client(name, token, 1).hello == b'OK\n', 'a version 1 hello changed')
        check(Client(name, token, 2).hello == b'OK\n', 'a version 2 hello changed')
        v1 = Client(name, token, 1)
        v3 = Client(name, token, 3)
        check(v3.hello == b'OK dmabuf=0\n', 'a version 3 hello without import was answered %r' % v3.hello)
        bad = Client(name, token, 4)
        check(bad.hello.startswith(b'ERROR 0 EINVAL'), 'a version 4 hello was answered %r' % bad.hello)

        plane = lambda size=64 * 64 * 4: memfd(b'\0' * size)

        # A version 1 client cannot send FRM3.
        v1.send(frm3(5, 1, 64, 64), [plane()])
        check(v1.reply().startswith(b'ERROR 5 EINVAL'), 'FRM3 from a version 1 client was not refused')

        # Malformed FRM3: EINVAL, whatever the compositor.
        malformed = [
            ('71 bytes', frm3(5, 1, 64, 64)[:-1], [plane()]),
            ('no descriptor', frm3(5, 1, 64, 64), []),
            ('six descriptors', frm3(5, 1, 64, 64), [plane() for _ in range(6)]),
            ('zero planes', frm3(5, 1, 64, 64, planes=0), [plane()]),
            ('five planes', frm3(5, 1, 64, 64, planes=5), [plane() for _ in range(5)]),
            ('three descriptors for one plane', frm3(5, 1, 64, 64), [plane() for _ in range(3)]),
            ('an unused plane with a pitch', frm3(5, 1, 64, 64, pitches=(256, 256, 0, 0)), [plane()]),
            ('zero width', frm3(5, 1, 0, 64), [plane()]),
        ]
        for why, header, fds in malformed:
            v3.send(header, fds)
            got = v3.reply()
            check(got.startswith(b'ERROR 5 EINVAL'), 'FRM3 with %s was answered %r' % (why, got))
        v3.send(frm3(0, 1, 64, 64), [plane()])
        check(v3.reply().startswith(b'ERROR 0 EINVAL'), 'FRM3 for image id 0 was not refused')
        v3.send(frm3(5, 1, 5000, 64, pitches=(20000, 0, 0, 0)), [plane()])
        check(v3.reply().startswith(b'ERROR 5 EFBIG'), 'an oversized FRM3 was not refused EFBIG')

        # Well formed, but nothing here can import it: ENOTSUP, so the
        # producer falls back to memfd. With and without a fence.
        v3.send(frm3(5, 1, 64, 64), [plane()])
        check(v3.reply().startswith(b'ERROR 5 ENOTSUP'), 'an FRM3 this compositor cannot import was not ENOTSUP')
        v3.send(frm3(5, 2, 64, 64, code='AR24'), [plane(), memfd(b'')])
        check(v3.reply().startswith(b'ERROR 5 ENOTSUP'), 'an FRM3 with a fence was not ENOTSUP')
        v3.send(frm3(5, 3, 64, 64, code='NV12'), [plane()])
        check(v3.reply().startswith(b'ERROR 5 ENOTSUP'), 'an NV12 FRM3 was not ENOTSUP')
        check(v3.reply(0.3) == b'', 'a refused FRM3 was answered twice')

        # memfd frames still work on a version 3 connection, into the same id.
        red = bytes((255, 0, 0)) * (16 * 16)
        v3.send(frm1(5, 4, 16, 16), [memfd(red)])
        got = wait_for(lambda: next((g for g in api('GET', '/v1/terminal-sessions/%s' % session).get('graphics', [])
                                     if g.get('id') == 5 and g.get('complete')), None),
                       'a memfd frame on a version 3 connection did not land')
        check(got['pixel_crc32'] == zlib.crc32(red) and got['dmabuf'] is False and got['sequence'] == 4,
              'the memfd frame landed as %s' % got)
        check(v3.reply(0.3) == b'', 'an accepted memfd frame was answered')

        frames = api('GET', '/v1/terminal-sessions/%s' % session)['frames']
        d = frames['dmabuf']
        check(d['accepted'] == 0 and d['released'] == 0 and d['refused']['unavailable'] == 3 and
              d['refused']['invalid'] == 11 and d['refused']['format'] == 0 and d['refused']['import'] == 0,
              'session dmabuf counts %s' % d)
        dmabuf = api('GET', '/v1/diagnostics')['graphics']['frames']['dmabuf']
        mine = [s for s in dmabuf['sessions'] if s['session_id'] == session]
        check(len(mine) == 1 and mine[0]['refused'] == d['refused'] and dmabuf['refused'] >= 14 and
              dmabuf['accepted'] == 0, 'diagnostics dmabuf %s' % dmabuf)
        summary = ('FRAME_DMABUF_PASS compositor=%s advertised=false unavailable="%s" hello_v1_v2=OK '
                   'hello_v3="OK dmabuf=0" frm3_from_v1=EINVAL malformed=EINVALx%d id0=EINVAL oversized=EFBIG '
                   'unimportable=ENOTSUPx3 memfd_on_v3=drawn counts=session+diagnostics'
                   % (compositor, dmabuf['unavailable'], len(malformed)))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()
    if backend == 'gles':
        mixed_result = mixed()
    print('%s %s' % (summary, mixed_result))


if __name__ == '__main__':
    main()
