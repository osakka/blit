#!/usr/bin/env python3
"""ADR 0202: picture frames by descriptor.

A session's program sends an image's pixels as a sealed memfd over Blit's
frame socket; the image is placed and deleted with Kitty escapes on its PTY.
HUD1 and STA1 (ADR 0203) serve the performance panel's Hibr section. The
channel speaks version 5 only (docs/api.md, "One grammar, version 5"): a
refused frame is `FRAME error <id> <code>`, and a hello of versions 1 to 4 is
refused `ERROR 0 EINVAL`. Run by frame_descriptor_gate.sh, which sets up the
mock display.

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

sys.dont_write_bytecode = True  # the helper below is imported from this directory
import gate_env  # noqa: E402

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19821')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT
SEALS = fcntl.F_SEAL_SHRINK | fcntl.F_SEAL_GROW | fcntl.F_SEAL_WRITE


def fail(why):
    print('FRAME_DESCRIPTOR_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def api(method, path, body=None, revision=None):
    data = json.dumps(body).encode() if body is not None else None
    headers = {'Content-Type': 'application/json'}
    if revision is not None:
        headers['If-Match'] = str(revision)
    request = urllib.request.Request(BASE + path, data=data, method=method, headers=headers)
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


def status(lines, count=None):
    """STA1: the lines, each ending in a newline, after their count."""
    count = len(lines) if count is None else count
    return b'STA1' + struct.pack('<I', count) + b''.join(line + b'\n' for line in lines)


def set_hud(hud):
    """The settings document with `hud` in it, written whole."""
    wait_for(lambda: api('GET', '/v1/settings')['storage']['state'] == 'clean', 'settings not clean')
    settings = api('GET', '/v1/settings')
    document = settings['document']
    document['hud'] = hud
    api('PUT', '/v1/settings', {'document': document}, settings['revision'])


class Client:
    def __init__(self, name, token, version=5):
        self.socket = socket.socket(socket.AF_UNIX, socket.SOCK_SEQPACKET)
        self.socket.connect('\0' + name)
        token = token.encode()
        self.socket.send(b'BLITFRM1' + struct.pack('<II', version, len(token)) + token)
        self.socket.settimeout(2.0)
        self.hello = self.socket.recv(64)

    def send(self, header, fd):
        socket.send_fds(self.socket, [header], [fd])
        os.close(fd)

    def say(self, message):
        self.socket.send(message)

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
    env = gate_env.server_env(BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'))
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

        # Version 5 only: an older hello gets the hello's own refusal.
        for version in (1, 2, 3, 4):
            old = Client(child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN'], version)
            check(old.hello == b'ERROR 0 EINVAL\n', 'a version %d hello was answered %r' % (version, old.hello))
        client = Client(child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN'])
        check(client.hello.startswith(b'OK dmabuf=') and client.hello.endswith(b' window=0\n'),
              'hello answered %r' % client.hello)
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
        check(client.reply().startswith(b'FRAME error 7 EINVAL\n'), 'an unsealed memfd was not refused')
        client.send(frame(7, 11, 16, 16), memfd(red[:100]))
        check(client.reply().startswith(b'FRAME error 7 EINVAL\n'), 'a short memfd was not refused')
        client.send(frame(7, 12, 5000, 16), memfd(red))
        check(client.reply().startswith(b'FRAME error 7 EFBIG\n'), 'an oversized frame was not refused')
        client.send(frame(9, 1, 16, 16, x=0, y=0, w=4, h=4), memfd(yellow))
        check(client.reply().startswith(b'FRAME error 9 ENOENT\n'), 'an edit of an unknown image was not refused')
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
        check(client.reply().startswith(b'FRAME error 8 EINVAL\n'), 'a frame into a Kitty image was not refused')

        # 6. After a delete that frees it, a frame gets ENOENT.
        run('\\033_Ga=d,d=I,i=7,q=2\\033\\\\')
        wait_for(lambda: image(session, 7)[0] is None, 'a=d,d=I did not free the descriptor image')
        client.send(frame(7, 20, 16, 16), memfd(red))
        check(client.reply().startswith(b'FRAME error 7 ENOENT\n'), 'a frame after its delete was not refused')

        # 7. The reverse order: an a=p read before its image's first frame
        #    is held while the session has a frame connection, and placed
        #    when the frame lands.
        run('\\033[6;1H\\033_Ga=p,i=11,q=2,C=1\\033\\\\')
        client.send(frame(11, 1, 16, 16), memfd(red))
        got = wait_for(lambda: (image(session, 11)[0] or {}).get('visible') and image(session, 11)[0],
                       'an a=p before its first frame was not placed when the frame landed')
        # ...and one whose frame never comes is refused after two seconds:
        # a frame that arrives later finds the image unplaced.
        rejected = image(session, 7)[1].get('graphics_diagnostics', {}).get('rejected', 0)
        run('\\033[8;1H\\033_Ga=p,i=12,q=2,C=1\\033\\\\')
        time.sleep(2.6)
        check(image(session, 7)[1].get('graphics_diagnostics', {}).get('rejected', 0) == rejected + 1,
              'a held placement was not refused after two seconds')
        client.send(frame(12, 1, 16, 16), memfd(red))
        late = wait_for(lambda: image(session, 12)[0], 'the late frame did not land')
        check(not late.get('visible'), 'an expired placement was applied: %s' % late)

        # 8. a=d,d=A frees every image, descriptor-made ones included, even
        #    one that was never placed -- how Hibr wipes a display's store
        #    after a restart (0.99.400) -- and their ids then get ENOENT.
        client.send(frame(13, 1, 16, 16), memfd(red))
        wait_for(lambda: image(session, 13)[0], 'an unplaced descriptor image was not kept')
        run('\\033_Ga=d,d=A,q=2\\033\\\\')
        wait_for(lambda: not api('GET', '/v1/terminal-sessions/%s' % session).get('graphics'),
                 'a=d,d=A left images: %s' % [g.get('id') for g in
                     api('GET', '/v1/terminal-sessions/%s' % session).get('graphics', [])])
        client.send(frame(13, 2, 16, 16), memfd(red))
        check(client.reply().startswith(b'FRAME error 13 ENOENT\n'), 'a frame after a=d,d=A was not refused')

        # 9. ADR 0203: HUD1 to every client, when the panel with its Hibr
        #    section is shown on the session's display; STA1 lines in the
        #    session and the panel, refused when malformed, gone five seconds
        #    after the last.
        name, token = child['BLIT_FRAME_SOCKET'][1:], child['BLIT_FRAME_TOKEN']
        v2 = Client(name, token)
        check(v2.hello.startswith(b'OK dmabuf='), 'a second hello was answered %r' % v2.hello)
        check(v2.reply(0.4) == b'' and client.reply(0.2) == b'', 'HUD1 was sent with the panel off')
        set_hud({'enabled': True, 'display_id': 'display-1', 'sections': ['blit', 'hibr']})
        check(v2.reply() == b'HUD1 on\n', 'the panel was turned on without HUD1 on')
        check(client.reply() == b'HUD1 on\n', 'the first client was not sent HUD1 on')
        late = Client(name, token)
        check(late.hello.startswith(b'OK dmabuf=') and late.reply() == b'HUD1 on\n',
              'a client joining a shown panel was not told after its OK')
        # A settings write that leaves the panel as it was tells no one:
        # a write resets to the defaults before applying the document.
        wait_for(lambda: api('GET', '/v1/settings')['storage']['state'] == 'clean', 'settings not clean')
        settings = api('GET', '/v1/settings')
        document = dict(settings['document'], effects={'enabled': True})
        api('PUT', '/v1/settings', {'document': document}, settings['revision'])
        check(v2.reply(0.6) == b'' and late.reply(0.2) == b'',
              'an unrelated settings write sent HUD1')
        # A video by descriptor: its display's panel says so, as the API does.
        client.send(frame(21, 1, 16, 16), memfd(red))
        wait_for(lambda: image(session, 21)[0], 'the video image did not land')
        run('\\033[1;1H\\033_Ga=p,i=21,q=2,C=1\\033\\\\')
        for n in range(30):
            client.send(frame(21, 2 + n, 16, 16), memfd(red if n % 2 else blue))
            time.sleep(0.08)
        recent = wait_for(lambda: (lambda r: r if r['descriptor_frames_hz'] >= 5 and
                                   r['why_hz']['video'] >= 3 else None)(
                              api('GET', '/v1/diagnostics')['render']['outputs'][0]['recent']),
                          'descriptor frames are not counted as the display\'s video', 2)
        video = [l for l in api('GET', '/v1/diagnostics')['hud']['panels'][0]['lines']
                 if l.startswith('video') or l.startswith('why')]
        check(any(l.startswith('why') and 'video ' in l for l in video) and
              any(l.startswith('video') and not l.startswith('video   0/s') for l in video),
              'the panel does not say the display is drawing a video: %s (%s)' % (video, recent))
        v2.say(status([b'fps=60 wakes=12', b'cpu=31%', b'slow=paint 4.2']))
        check(v2.reply(0.4) == b'', 'an accepted STA1 was answered')
        got = wait_for(lambda: api('GET', '/v1/terminal-sessions/%s' % session).get('status'),
                       'the STA1 lines are not in the session')
        check(got['lines'] == ['fps=60 wakes=12', 'cpu=31%', 'slow=paint 4.2'] and got['age_ms'] < 5000,
              'session status %s' % got)
        panel = wait_for(lambda: next((p for p in api('GET', '/v1/diagnostics')['hud']['panels']
                                       if 'cpu=31%' in p['lines']), None),
                         'the STA1 lines are not in the panel')
        at = panel['lines'].index('cpu=31%')
        check(panel['lines'][at - 2].startswith('hibr  ('), 'the hibr heading is missing: %s' % panel['lines'])
        text_y = panel['y'] + 7 + at * 16
        inked = sum(1 for x in range(panel['x'] + 7, panel['x'] + 7 + 8 * 7) for y in range(text_y, text_y + 16)
                    if screen_pixel(x, y) == (0xd8, 0xd8, 0xdc))
        check(inked > 20, 'the hibr area of the panel is empty on screen (%d text pixels)' % inked)
        refusals = [('nine lines', status([b'k=v'] * 9)),
                    ('a 65-byte line', status([b'x' * 65])),
                    ('count 3 with 2 lines', status([b'a=1', b'b=2'], count=3)),
                    ('count 1 with 2 lines', status([b'a=1', b'b=2'], count=1)),
                    ('no final newline', status([b'a=1'])[:-1]),
                    ('a control character', status([b'a=\x1b[1m'])),
                    ('count 0', status([], count=0))]
        for why, message in refusals:
            v2.say(message)
            check(v2.reply() == b'FRAME error 0 EINVAL\n', 'STA1 with %s was not refused' % why)
        socket.send_fds(v2.socket, [status([b'a=1'])], [memfd(b'x')])
        check(v2.reply() == b'FRAME error 0 EINVAL\n', 'STA1 with a descriptor was not refused')
        v2.say(status([b'x' * 64]))
        check(v2.reply(0.4) == b'', 'a 64-byte line was refused')
        time.sleep(5.4)
        check(api('GET', '/v1/terminal-sessions/%s' % session).get('status') is None,
              'STA1 lines older than five seconds were kept')
        wait_for(lambda: not any(l.startswith('hibr  (') for p in api('GET', '/v1/diagnostics')['hud']['panels']
                                 for l in p['lines']),
                 'the panel kept a status older than five seconds')
        set_hud({'enabled': True, 'display_id': 'display-1', 'sections': ['blit']})
        check(v2.reply() == b'HUD1 off\n' and late.reply() == b'HUD1 off\n',
              'dropping the hibr section did not send HUD1 off')
        set_hud({'enabled': True, 'display_id': 'display-1', 'sections': ['hibr']})
        check(v2.reply() == b'HUD1 on\n' and late.reply() == b'HUD1 on\n',
              'restoring the hibr section did not send HUD1 on')
        set_hud({'enabled': False})
        check(v2.reply() == b'HUD1 off\n' and late.reply() == b'HUD1 off\n',
              'turning the panel off did not send HUD1 off')
        check([client.reply() for _ in range(4)] == [b'HUD1 off\n', b'HUD1 on\n', b'HUD1 off\n', b''],
              'the first client was not told as the others were')

        frames = image(session, 7)[1]['frames']
        print('FRAME_DESCRIPTOR_PASS token=random hello=token_checked+v5_only(v1-v4=ERROR_0_EINVAL) '
              'before_placement=kept screen=drawn '
              'newest=wins stale=dropped edit=rectangle refused=FRAME_error:unsealed+short+efbig+unknown '
              'kitty_into_descriptor=refused frame_into_kitty=refused after_delete=enoent '
              'placement_before_frame=held_then_placed expired_placement=refused delete_all=frees_descriptor_images '
              'video_by_descriptor=panel+api hud1=every_client+on+off+after_ok+steady_on_unrelated_write sta1=session+panel+screen '
              'sta1_refused=9_lines+65_bytes+count_mismatch+no_newline+control+count_0+descriptor '
              'sta1_expiry=5s accepted=%d refused=%d stale=%d wrong_uid=not_tested_without_root'
              % (frames['accepted'], frames['refused'], frames['stale']))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == '__main__':
    main()
