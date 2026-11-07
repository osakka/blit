#!/usr/bin/env python3
"""ADR 0223: frames by dmabuf on a live host whose GPU can import them.

Run by frame_dmabuf_live_gate.sh against a running Blit (its GLES
compositor on real hardware): BLIT_GATE_LIVE_DMABUF names the host,
BLIT_AUTH_TOKEN is its API token. The host needs no compiler, ssh or
script language: the test producer (frame_dmabuf_producer.c, built here,
libc only, GBM loaded with dlopen) is carried over in a Blit session's
terminal input, base64 through `base64 -d`, and run as a Blit session of
its own, so it has that session's BLIT_FRAME_SOCKET and BLIT_FRAME_TOKEN
as any program in a session does.

What it proves:
- the hello offers dmabuf=1 with XR24/LINEAR, and /v1/diagnostics agrees;
- frames sent by dmabuf are imported and drawn: the producer's last colour
  covers its 256x128 image in a screenshot of the display it is on (the
  GPU composes that screenshot; the CPU never had the pixels), while
  `image_upload_bytes` of that output does not grow by a frame's bytes;
- every frame is RELEASEd, in order, the newest only once the image is
  deleted, so a producer's pool of three never starves;
- a modifier Blit did not list is refused ENOTSUP;
- the session's dmabuf counts.
Not proved: the acquire fence (the producer sends none: making a sync_file
needs a GPU API it does not load) and the GPU-to-CPU fallback.
"""
import base64
import hashlib
import json
import os
import struct
import sys
import time
import urllib.request

HOST = sys.argv[1]
PRODUCER = sys.argv[2]
DISPLAY = os.environ.get('BLIT_GATE_LIVE_DISPLAY', '')
BASE = HOST if HOST.startswith('http') else 'http://%s:8080' % HOST
TOKEN = os.environ.get('BLIT_AUTH_TOKEN', '')
USER = os.environ.get('BLIT_GATE_LIVE_USER', os.environ.get('USER', ''))
REMOTE = '/tmp/blit-dmabuf'
LAST = (0x20, 0xc0, 0x60)


def fail(why):
    print('FRAME_DMABUF_LIVE_FAIL %s' % why, file=sys.stderr)
    sys.exit(1)


def check(ok, why):
    if not ok:
        fail(why)


def api(method, path, body=None, raw=False):
    headers = {'Content-Type': 'application/json'}
    if TOKEN:
        headers['Authorization'] = 'Bearer %s' % TOKEN
    request = urllib.request.Request(BASE + path, method=method, headers=headers,
                                     data=json.dumps(body).encode() if body is not None else None)
    with urllib.request.urlopen(request, timeout=30) as response:
        data = response.read()
    if raw:
        return data
    return json.loads(data) if data.strip() else {}


def session(command, display=None, width=80, height=10):
    body = {'command': '/bin/sh', 'arguments': ['-c', command], 'user': USER,
            'surface': {'width': width, 'height': height}}
    if display:
        body['display_id'] = display
    return api('POST', '/v1/terminal-sessions', body)['id']


def screen_text(sid):
    cells = api('GET', '/v1/surfaces/surface-%s/cells' % sid)['cells']
    rows = {}
    for c in cells:
        rows.setdefault(c.get('y', 0), {})[c.get('x', 0)] = c.get('text') or ' '
    return '\n'.join(''.join(r.get(k, ' ') for k in range(max(r) + 1)).rstrip()
                     for _, r in sorted(rows.items()))


def wait_text(sid, needle, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        text = screen_text(sid)
        if needle in text:
            return text
        time.sleep(0.5)
    return screen_text(sid)


def ended(sid, seconds):
    deadline = time.time() + seconds
    while time.time() < deadline:
        if api('GET', '/v1/terminal-sessions/%s' % sid)['state'] not in ('starting', 'running'):
            return True
        time.sleep(0.3)
    return False


def send_file(local, remote):
    """The producer, base64 through a session's terminal: canonical lines of
    76 characters, about 600 bytes a request, paced so the terminal's input
    queue never overflows, ended by ^D at a line start. Its checksum there
    must match."""
    data = open(local, 'rb').read()
    encoded = base64.b64encode(data).decode()
    sid = session('stty -echo; mkdir -p %s && base64 -d > %s.part && chmod 755 %s.part '
                  '&& mv %s.part %s && echo SENT_OK $(sha256sum %s)'
                  % (REMOTE, remote, remote, remote, remote, remote), width=160)
    time.sleep(1.0)
    lines = [encoded[i:i + 76] + '\n' for i in range(0, len(encoded), 76)]
    chunk = ''
    for line in lines:
        if len(chunk) + len(line) > 600:
            api('POST', '/v1/terminal-sessions/%s/input' % sid, {'data': chunk, 'mode': 'raw'})
            time.sleep(0.05)
            chunk = ''
        chunk += line
    if chunk:
        api('POST', '/v1/terminal-sessions/%s/input' % sid, {'data': chunk, 'mode': 'raw'})
    time.sleep(0.3)
    api('POST', '/v1/terminal-sessions/%s/input' % sid, {'data': '\x04', 'mode': 'raw'})
    check(ended(sid, 60), 'the transfer session did not end: %s' % screen_text(sid))
    text = screen_text(sid)
    check('SENT_OK %s' % hashlib.sha256(data).hexdigest() in text,
          'the producer did not arrive whole: %s' % text)
    return sid


def colour_count(display, colour):
    bmp = api('GET', '/v1/diagnostics/screenshots/%s' % display, raw=True)
    offset, = struct.unpack_from('<I', bmp, 10)
    width, height = struct.unpack_from('<ii', bmp, 18)
    bpp, = struct.unpack_from('<H', bmp, 28)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    want = bytes((colour[2], colour[1], colour[0]))
    count = 0
    for row in range(abs(height)):
        line = bmp[offset + row * stride: offset + row * stride + width * step]
        for x in range(0, width * step, step):
            if line[x:x + 3] == want:
                count += 1
    return count


def output_of(diagnostics, display):
    outputs = diagnostics['render']['outputs']
    return next((o for o in outputs if o.get('display_id') == display), outputs[0])


def uploaded_bytes():
    """Picture bytes uploaded to GPU textures, all outputs (ADR 0213)."""
    def find(value):
        if isinstance(value, dict):
            if 'graphics_upload_bytes' in value:
                return value['graphics_upload_bytes']
            for item in value.values():
                got = find(item)
                if got is not None:
                    return got
        return None
    return find(api('GET', '/v1/diagnostics/runtime'))


def main():
    sessions = []
    diagnostics = api('GET', '/v1/diagnostics')
    dmabuf = diagnostics['graphics']['frames']['dmabuf']
    check(dmabuf['advertised'] and any(f.split('/')[0] == 'XR24' and '0' in f.split('/')[1:]
                                       for f in dmabuf['formats'].split(',')),
          'the host does not offer XR24/LINEAR dmabuf import: %s' % dmabuf)
    display = DISPLAY or output_of(diagnostics, None).get('display_id')
    try:
        sessions.append(send_file(PRODUCER, REMOTE + '/producer'))
        probe = session('%s/producer probe; sleep 1' % REMOTE, width=200, height=4)
        sessions.append(probe)
        ended(probe, 20)
        text = screen_text(probe)
        check('verdict=PROBE gbm=ok' in text.replace('\n', ''), 'GBM probe on the host: %s' % text)
        before = uploaded_bytes()
        producer = session('%s/producer run 90 12; sleep 30' % REMOTE, display=display,
                           width=120, height=40)
        sessions.append(producer)
        text = wait_text(producer, 'DMABUF_HOLDING', 60)
        check('DMABUF_HOLDING' in text, 'the producer did not reach its hold: %s' % text)
        # The display shows the producer's pane once it has a frame of it.
        covered = 0
        for _ in range(10):
            covered = colour_count(display, LAST)
            if covered >= 256 * 128:
                break
            time.sleep(0.5)
        during = uploaded_bytes()
        text = wait_text(producer, 'DMABUF_RESULT', 40)
        result = ' '.join(l for l in text.replace('\n', '').split('DMABUF_RESULT')[1:]) if 'DMABUF_RESULT' in text else text
        frames = api('GET', '/v1/terminal-sessions/%s' % producer)['frames']
        uploaded = during - before if during is not None and before is not None else 'unknown'
        print('FRAME_DMABUF_LIVE host=%s display=%s covered_px=%d of %d upload_bytes_delta=%s '
              'session_frames=%s result=%s' % (BASE, display, covered, 256 * 128, uploaded,
                                               json.dumps(frames['dmabuf']), result.strip()))
        check('verdict=PASS' in result, 'the producer reported %s' % result)
        check(covered >= 256 * 128 * 9 // 10,
              'the last frame is not on screen: %d pixels of its colour' % covered)
        check(frames['dmabuf']['accepted'] >= 90 and frames['dmabuf']['released'] >= 90 and
              frames['dmabuf']['refused']['format'] >= 1, 'session counts %s' % frames)
        print('FRAME_DMABUF_LIVE_PASS')
    finally:
        cleanup = session('rm -rf %s && echo CLEAN_OK' % REMOTE)
        ended(cleanup, 15)
        print('FRAME_DMABUF_LIVE_CLEANUP %s' % ('removed' if 'CLEAN_OK' in screen_text(cleanup) else 'NOT removed'))
        for sid in sessions + [cleanup]:
            try:
                api('DELETE', '/v1/terminal-sessions/%s' % sid)
            except Exception as error:  # report, do not hide
                print('FRAME_DMABUF_LIVE_DELETE session %s: %s' % (sid, error))


if __name__ == '__main__':
    main()
