#!/usr/bin/env python3
"""ADR 0225: blit-gamescope's frame core against a real Blit (CPU compositor).

The gate binary runs as a session's command, as blit-gamescope does: it reads
the pane's size with CSI 14 t, says hello on the frame channel (version 3,
answered dmabuf=0 here), sends one FRM3 that is refused ENOTSUP (its buffer
must come back and FRM3 stop), then fake BGRx gamescope buffers as sealed
memfd FRM1 frames placed with a=p, and the INPUT request, which a Blit
without INPUT answers as a malformed frame (counted as ENOTSUP: frames
only). This script checks what it wrote and that the picture is on screen.
"""
import json
import os
import struct
import subprocess
import sys
import tempfile
import time
import urllib.request

ROOT = os.path.abspath(os.path.join(os.path.dirname(__file__), '..', '..'))
PORT = (int(os.environ.get('BLIT_GATE_PORT', '19822')) +
        int(os.environ.get('BLIT_GATE_PORT_OFFSET', '0')))
BASE = 'http://127.0.0.1:%d' % PORT


def fail(why):
    print('GAMESCOPE_BRIDGE_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def api(method, path, body=None):
    request = urllib.request.Request(BASE + path, method=method,
                                     data=json.dumps(body).encode() if body is not None else None,
                                     headers={'Content-Type': 'application/json'})
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
    fail(why() if callable(why) else why)


def screen_pixel(x, y):
    with urllib.request.urlopen(BASE + '/v1/diagnostics/screenshots/display-1', timeout=10) as response:
        bmp = response.read()
    offset, = struct.unpack_from('<I', bmp, 10)
    width, height = struct.unpack_from('<ii', bmp, 18)
    bpp, = struct.unpack_from('<H', bmp, 28)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    row = (abs(height) - 1 - y) if height > 0 else y
    at = offset + row * stride + x * step
    return (bmp[at + 2], bmp[at + 1], bmp[at])


def results(path):
    if not os.path.exists(path):
        return None
    lines = dict(l.rstrip('\n').split('=', 1) for l in open(path) if '=' in l)
    return lines if lines.get('DONE') == '1' else None


def main():
    binary = os.path.abspath(sys.argv[1])
    work = tempfile.mkdtemp(prefix='blit-gamescope-gate-')
    env = dict(os.environ, BLIT_PSF_FONT=os.path.join(ROOT, 'assets/fonts/Blit-Terminus16.psf'),
               BLIT_SETTINGS_PATH=os.path.join(work, 'settings.json'), BLIT_LOGIN_AUTOSTART='0',
               BLIT_FONT_ROOT=os.path.join(ROOT, 'assets/fonts'))
    server = subprocess.Popen([os.environ.get('BLIT_GATE_BINARY', os.path.join(ROOT, 'build/blit/blit-server')),
                               str(PORT)], env=env, stdout=subprocess.DEVNULL, stderr=subprocess.DEVNULL)
    try:
        wait_for(lambda: api('GET', '/v1/version'), 'the server did not start', 15)
        result = os.path.join(work, 'result')
        session = api('POST', '/v1/terminal-sessions',
                      {'command': binary, 'arguments': ['session', result], 'display_id': 'display-1',
                       'surface': {'width': 40, 'height': 12}})['id']
        got = wait_for(lambda: results(result),
                       lambda: 'the session did not finish: %r' %
                       (open(result).read() if os.path.exists(result) else 'no result'), 15)
        print('session: %s' % ' '.join('%s=%s' % kv for kv in sorted(got.items())))
        width, height = (int(v) for v in got['SIZE'].split('x'))
        check(width > 0 and height > 0, 'CSI 14 t was not answered: %s' % got['SIZE'])
        check(got['CONNECT'] == 'ok', 'the frame channel refused the hello: %s' % got['CONNECT'])
        check(got['DMABUF'] == '0', 'the CPU compositor offered dmabufs')
        check(got['FRM3_ERROR'] == 'ERROR 1 ENOTSUP', 'FRM3 answered %s' % got['FRM3_ERROR'])
        check(got['FRM3_REQUEUED'] == '1', 'the refused FRM3 buffer was not requeued')
        check(got['FRM3_OFF'] == '1', 'FRM3 still in use after ENOTSUP')
        check(got['FRM1_SENT'] == '3' and got['ERRORS'] == '1', 'memfd frames refused: %s' % got['LAST_ERROR'])
        check(got['INPUT_STATE'] == '3' and got['INPUT_ERROR'] == 'ENOTSUP',
              'INPUT on a Blit without it: state %s %s' % (got['INPUT_STATE'], got['INPUT_ERROR']))

        state = api('GET', '/v1/terminal-sessions/%s' % session)
        images = [g for g in state.get('graphics', []) if g.get('id') == 1]
        check(images and images[0].get('visible'), 'the picture is not placed: %s' % state.get('graphics'))
        cell_w, cell_h = width // 40, height // 12
        check((images[0]['placement_width'], images[0]['placement_height']) == (10 * cell_w, 4 * cell_h),
              'the placement does not fill its 10x4 cells: %s' % images[0])
        dmabuf = (state.get('frames') or {}).get('dmabuf') or {}
        check(dmabuf.get('accepted', 0) == 0, 'a dmabuf was accepted on the CPU compositor: %s' % dmabuf)
        # The 10x4 cells at the pane's top-left show the fake buffer's colour
        # (BGRx 30 a0 d0 is RGB d0 a0 30), out to the scaled corner.
        far = (10 * cell_w - 3, 4 * cell_h - 3)
        wait_for(lambda: screen_pixel(10, 10) == (0xd0, 0xa0, 0x30) and screen_pixel(*far) == (0xd0, 0xa0, 0x30),
                 lambda: 'the frame is not on screen: %s %s' % (screen_pixel(10, 10), screen_pixel(*far)))
        api('DELETE', '/v1/terminal-sessions/%s' % session)
        print('GAMESCOPE_BRIDGE_SESSION_OK size=%dx%d frm3=ENOTSUP+requeued memfd=drawn input=ENOTSUP'
              % (width, height))
    finally:
        server.terminate()
        try:
            server.wait(5)
        except subprocess.TimeoutExpired:
            server.kill()


if __name__ == '__main__':
    main()
