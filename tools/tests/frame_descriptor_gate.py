#!/usr/bin/env python3
"""ADR 0202: picture frames by descriptor.

A session's program sends an image's pixels as a sealed memfd over Blit's
frame socket; the image is placed and deleted with Kitty escapes on its PTY.
Run by frame_descriptor_gate.sh, which sets up the mock display.

Not covered here: a peer whose uid may not send a session frames. That needs
a second uid, so root; the rule is frame_peer_allowed in src/blit_frames.h.
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
PORT = int(os.environ.get('BLIT_GATE_PORT', '19821'))
BASE = 'http://127.0.0.1:%d' % PORT
SEALS = fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE


def fail(why):
    print('FRAME_DESCRIPTOR_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def api(method, path, body=None):
    data = json.dumps(body).encode() if body is not None else None
    request = urllib.request.Request(BASE + path, data=data, method=method,
                                     headers={'Content-Type': 'application/json'})
    with urllib.request.urlopen(request, timeout=10) as response:
        raw = response.read()
        return json.loads(raw) if raw else {}


def solid(rgb, width, height):
    return bytes(rgb) * (width * height)


def memfd(pixels, seal=True):
    fd = os.memfd_create('blit-gate-frame', os.MFD_ALLOW_SEALING)
    os.write(fd, pixels)
    if seal:
        fcntl.fcntl(fd, fcntl.F_ADD_SEALS, SEALS)
    return fd


def frame(image_id, sequence, width, height, fmt=24, stride=None, x=0, y=0, w=0, h=0):
    stride = stride if stride is not None else (w or width) * (3 if fmt == 24 else 4)
    return b'FRM1' + struct.pack('<IQIIIIIIII', image_id, sequence, width, height,
                                 fmt, stride, x, y, w, h)


class Client:
    def __init__(self, name, token):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.connect('\0' + name)
        token = token.encode()
        self.socket.send(b'BLITFRM1' + struct.pack('<II', 1, len(token)) + token)
        self.socket.settimeout(2.0)
        self.hello = self.socket.recv(64)

    def send(self, header, fd):
        socket.send_fds(self.socket, [header], [fd])
        os.close(fd)

    def reply(self, wait=1.0):
        self.socket.settimeout(wait)
        try:
            return self.socket.recv(128)
        except (socket.timeout, BlockingIOError):
            return b''


def screen_pixel(x, y):
    """One pixel of display-1's screenshot (BMP, 24 or 32 bpp)."""
    request = urllib.request.Request(BASE + '/v1/diagnostics/screenshots/display-1')
    with urllib.request.urlopen(request, timeout=10) as response:
        bmp = response.read()
    offset, = struct.unpack_from('<I', bmp, 10)
    width, height = struct.unpack_from('<ii', bmp, 18)
    bpp, = struct.unpack_from('<H', bmp, 28)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    row = (abs(height) - 1 - y) if height > 0 else y
    at = offset + row * stride + x * step
    b, g, r = bmp[at], bmp[at + 1], bmp[at + 2]
    return (r, g, b)


def image(session, image_id):
    state = api('GET', '/v1/terminal-sessions/%s' % session)
    return next((g for g in state.get('graphics', []) if g.get('id') == image_id), None), state


def wait_for(predicate, why, seconds=5.0):
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


def main():
    work = tempfile.mkdtemp(prefix='blit-frame-gate-')
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS='ignore', BLIT_LOGIN_AUTOSTART='0')
    server = subprocess.Popen([os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server')),
                               str(PORT)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the server did not start', 15)
        # The session writes its environment, then prints whatever step it is
        # given: Kitty placements and deletions for the descriptor image.
        script = os.path.join(work, 'run.sh')
        with open(script, 'w') as out:
            out.write("env > %s/env; printf '\\033[?25l'; n=0; while :; do "
                      "if [ -e %s/go$n ]; then printf '%%b' \"$(cat %s/step$n)\"; : > %s/done$n; n=$((n+1)); fi; "
                      "sleep 0.05; done\n" % (work, work, work, work))
        session = api('POST', '/v1/terminal-sessions',
                      {'command': '/bin/sh', 'arguments': [script], 'display_id': 'display-1',
                       'surface': {'width': 40, 'height': 12}})['id']
        wait_for(lambda: os.path.exists(os.path.join(work, 'env')) and
                 'BLIT_FRAME_TOKEN' in open(os.path.join(work, 'env')).read(),
                 'the session was not given BLIT_FRAME_SOCKET and BLIT_FRAME_TOKEN')
        child = dict(line.rstrip('\n').split('=', 1) for line in open(os.path.join(work, 'env')) if '=' in line)
        check(child['BLIT_FRAME_SOCKET'] == '@blit-frames-%d' % PORT, 'socket name %s' % child['BLIT_FRAME_SOCKET'])
        check(len(child['BLIT_FRAME_TOKEN']) == 32 and child['BLIT_FRAME_TOKEN'] != child.get('BLIT_SESSION_TOKEN'),
              'the frame token is not a separate random secret')
        steps = [0]

        def run(text):
            n = steps[0]
            with open(os.path.join(work, 'step%d' % n), 'w') as out:
                out.write(text)
            open(os.path.join(work, 'go%d' % n), 'w').close()
            wait_for(lambda: os.path.exists(os.path.join(work, 'done%d' % n)), 'step %d did not run' % n)
            steps[0] += 1
            time.sleep(0.3)

        # A wrong token is refused and the connection closed.
        bad = Client(child['BLIT_FRAME_SOCKET'][1:], 'not-the-token')
        check(bad.hello.startswith(b'ERROR 0 EACCES'), 'a wrong token was answered %r' % bad.hello)

        client = Client(child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN'])
        check(client.hello == b'OK\n', 'hello answered %r' % client.hello)
        red, blue = solid((255, 0, 0), 16, 16), solid((0, 0, 255), 16, 16)

        # 1. A frame before its placement is kept, then drawn when placed.
        client.send(frame(7, 1, 16, 16), memfd(red))
        got = wait_for(lambda: (image(session, 7)[0] or {}).get('complete') and image(session, 7)[0],
                       'a frame before its placement was not kept')
        check(got['pixel_crc32'] == zlib.crc32(red), 'first frame pixels %s' % got)
        run('\\033[1;1H\\033_Ga=p,i=7,q=2,C=1\\033\\\\')
        check(image(session, 7)[0]['visible'], 'the placed descriptor image is not shown')

        # 2. Newer frames replace it; an older sequence is dropped.
        wait_for(lambda: screen_pixel(4, 4) == (255, 0, 0), 'the placed frame is not on screen: %s' % (screen_pixel(4, 4),))
        client.send(frame(7, 2, 16, 16), memfd(blue))
        wait_for(lambda: image(session, 7)[0]['pixel_crc32'] == zlib.crc32(blue), 'a newer frame did not land')
        wait_for(lambda: screen_pixel(4, 4) == (0, 0, 255), 'the newer frame is not on screen: %s' % (screen_pixel(4, 4),))
        client.send(frame(7, 1, 16, 16), memfd(solid((0, 255, 0), 16, 16)))
        time.sleep(0.4)
        got, state = image(session, 7)
        check(got['pixel_crc32'] == zlib.crc32(blue), 'an older sequence replaced a newer frame')
        check(state['frames']['stale'] >= 1, 'the stale frame was not counted: %s' % state['frames'])

        # 3. An edit writes only its rectangle.
        yellow = solid((255, 255, 0), 4, 4)
        client.send(frame(7, 3, 16, 16, x=2, y=3, w=4, h=4), memfd(yellow))
        expected = bytearray(blue)
        for row in range(4):
            at = ((3 + row) * 16 + 2) * 3
            expected[at:at + 12] = yellow[:12]
        wait_for(lambda: image(session, 7)[0]['pixel_crc32'] == zlib.crc32(bytes(expected)),
                 'an edit did not write its rectangle')
        check(image(session, 7)[0]['visible'], 'an edit lost the placement')

        # 4. Refusals, each with its code, the image untouched.
        before = image(session, 7)[0]['pixel_crc32']
        client.send(frame(7, 10, 16, 16), memfd(red, seal=False))
        check(client.reply().startswith(b'ERROR 7 EINVAL'), 'an unsealed memfd was not refused')
        client.send(frame(7, 11, 16, 16), memfd(red[:100]))
        check(client.reply().startswith(b'ERROR 7 EINVAL'), 'a short memfd was not refused')
        client.send(frame(7, 12, 5000, 16), memfd(red))
        check(client.reply().startswith(b'ERROR 7 EFBIG'), 'an oversized frame was not refused')
        client.send(frame(9, 1, 16, 16, x=0, y=0, w=4, h=4), memfd(yellow))
        check(client.reply().startswith(b'ERROR 9 ENOENT'), 'an edit of an unknown image was not refused')
        check(image(session, 7)[0]['pixel_crc32'] == before, 'a refused frame changed the image')

        # 5. A descriptor image takes no pixels from the PTY, and a Kitty
        #    image takes no frames.
        rejected = image(session, 7)[1].get('graphics_diagnostics', {}).get('rejected', 0)
        run('\\033_Ga=t,f=24,s=1,v=1,i=7,q=2;AAAA\\033\\\\')
        got, state = image(session, 7)
        check(got['pixel_crc32'] == before and
              state.get('graphics_diagnostics', {}).get('rejected', 0) == rejected + 1,
              'Kitty pixels into a descriptor image were not refused')
        run('\\033_Ga=t,f=24,s=1,v=1,i=8,q=2;AAAA\\033\\\\')
        client.send(frame(8, 1, 1, 1), memfd(b'\xff\xff\xff'))
        check(client.reply().startswith(b'ERROR 8 EINVAL'), 'a frame into a Kitty image was not refused')

        # 6. After a delete that frees it, a frame gets ENOENT.
        run('\\033_Ga=d,d=I,i=7,q=2\\033\\\\')
        wait_for(lambda: image(session, 7)[0] is None, 'a=d,d=I did not free the descriptor image')
        client.send(frame(7, 20, 16, 16), memfd(red))
        check(client.reply().startswith(b'ERROR 7 ENOENT'), 'a frame after its delete was not refused')

        frames = image(session, 7)[1]['frames']
        print('FRAME_DESCRIPTOR_PASS token=random hello=token_checked before_placement=kept screen=drawn '
              'newest=wins stale=dropped edit=rectangle refused=unsealed+short+efbig+unknown '
              'kitty_into_descriptor=refused frame_into_kitty=refused after_delete=enoent '
              'accepted=%d refused=%d stale=%d wrong_uid=not_tested_without_root'
              % (frames['accepted'], frames['refused'], frames['stale']))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == '__main__':
    main()
