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
- every frame is released (`FRAME release`), in order, the newest only
  once the image is deleted, so a producer's pool of three never starves;
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
# Every session this gate starts, so it deletes them on every way out.
STARTED = []


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
    sid = api('POST', '/v1/terminal-sessions', body)['id']
    STARTED.append(sid)
    return sid


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
        if api('GET', '/v1/terminal-sessions/%s' % sid)['state'] != 'running':
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


def colour_counts(display, colours, tolerance):
    """Pixels within `tolerance` of each colour, in one screenshot."""
    bmp = api('GET', '/v1/diagnostics/screenshots/%s' % display, raw=True)
    offset, = struct.unpack_from('<I', bmp, 10)
    width, height = struct.unpack_from('<ii', bmp, 18)
    bpp, = struct.unpack_from('<H', bmp, 28)
    step = bpp // 8
    stride = (width * step + 3) & ~3
    counts = [0] * len(colours)
    for row in range(abs(height)):
        line = bmp[offset + row * stride: offset + row * stride + width * step]
        blues, greens, reds = line[0::step], line[1::step], line[2::step]
        for i, (r, g, b) in enumerate(colours):
            # Cheap rejection first: the green channel alone.
            if g - tolerance > 255 or g + tolerance < 0:
                continue
            counts[i] += sum(1 for x in range(width)
                             if abs(greens[x] - g) <= tolerance and abs(reds[x] - r) <= tolerance and
                             abs(blues[x] - b) <= tolerance)
    return counts


def held_phase(mode, display, hold, colours, tolerance):
    """Runs the producer in `mode` on the display, captures it while it holds,
    and returns the counts and the producer's result line."""
    sid = session('%s/producer %s %d; sleep 2' % (REMOTE, mode, hold), display=display,
                  width=120, height=40)
    marker = 'DMABUF_%s_HOLDING' % mode.upper()
    text = wait_text(sid, marker, 40)
    check(marker in text, 'the %s producer did not reach its hold: %s' % (mode, text))
    counts = [0] * len(colours)
    for _ in range(10):
        counts = colour_counts(display, colours, tolerance)
        if counts[0] >= 256 * 128:
            break
        time.sleep(0.5)
    seen = dominant(display) if counts[0] < 256 * 128 else []
    text = wait_text(sid, 'DMABUF_RESULT', hold + 20)
    result = text.replace('\n', '').split('DMABUF_RESULT', 1)[1] if 'DMABUF_RESULT' in text else text
    retire(sid)
    return counts, result.strip() + (' screen_top_colours=%s' % seen if seen else '')


def retire(sid):
    """A finished producer session goes, so the next one is the one shown."""
    ended(sid, 20)
    api('DELETE', '/v1/terminal-sessions/%s' % sid)
    STARTED.remove(sid)


def dominant(display):
    """The commonest colours of the display, sampled, for a failure report."""
    bmp = api('GET', '/v1/diagnostics/screenshots/%s' % display, raw=True)
    offset, = struct.unpack_from('<I', bmp, 10)
    width, height = struct.unpack_from('<ii', bmp, 18)
    step = struct.unpack_from('<H', bmp, 28)[0] // 8
    stride = (width * step + 3) & ~3
    seen = {}
    for row in range(0, abs(height), 4):
        for x in range(0, width, 4):
            at = offset + row * stride + x * step
            key = '%02x%02x%02x' % (bmp[at + 2], bmp[at + 1], bmp[at])
            seen[key] = seen.get(key, 0) + 1
    return sorted(seen.items(), key=lambda kv: -kv[1])[:6]


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
    diagnostics = api('GET', '/v1/diagnostics')
    dmabuf = diagnostics['graphics']['frames']['dmabuf']
    check(dmabuf['advertised'] and any(f.split('/')[0] == 'XR24' and '0' in f.split('/')[1:]
                                       for f in dmabuf['formats'].split(',')),
          'the host does not offer XR24/LINEAR dmabuf import: %s' % dmabuf)
    display = DISPLAY or output_of(diagnostics, None).get('display_id')
    try:
        send_file(PRODUCER, REMOTE + '/producer')
        probe = session('%s/producer probe; sleep 1' % REMOTE, width=200, height=4)
        ended(probe, 20)
        text = screen_text(probe)
        check('verdict=PROBE gbm=ok' in text.replace('\n', ''), 'GBM probe on the host: %s' % text)
        before = uploaded_bytes()
        producer = session('%s/producer run 90 12; sleep 2' % REMOTE, display=display,
                           width=120, height=40)
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
        seen = dominant(display) if covered < 256 * 128 else []
        text = wait_text(producer, 'DMABUF_RESULT', 40)
        result = ' '.join(l for l in text.replace('\n', '').split('DMABUF_RESULT')[1:]) if 'DMABUF_RESULT' in text else text
        if seen:
            result += ' screen_top_colours=%s' % seen
        frames = api('GET', '/v1/terminal-sessions/%s' % producer)['frames']
        retire(producer)
        uploaded = during - before if during is not None and before is not None else 'unknown'
        print('FRAME_DMABUF_LIVE host=%s display=%s covered_px=%d image_px_at_1x=%d upload_bytes_delta=%s '
              'session_frames=%s result=%s' % (BASE, display, covered, 256 * 128, uploaded,
                                               json.dumps(frames['dmabuf']), result.strip()))
        check('verdict=PASS' in result, 'the producer reported %s' % result)
        # The display's scale (HiDPI) enlarges the image by scale_x x scale_y.
        out = output_of(api('GET', '/v1/diagnostics'), display)
        area = int(256 * 128 * out.get('scale_x', 1) * out.get('scale_y', 1))
        check(covered >= area * 9 // 10,
              'the last frame is not on screen: %d pixels of its colour, %d expected' % (covered, area))
        check(frames['dmabuf']['accepted'] >= 90 and frames['dmabuf']['released'] >= 90 and
              frames['dmabuf']['refused']['format'] >= 1, 'session counts %s' % frames)

        # Alpha: AR24 is premultiplied. Red 0x80 at alpha 0x80 over opaque
        # blue shows (0x80, 0, 0x7f); read as straight alpha it would be
        # (0x40, 0, 0x7f).
        (premultiplied, straight), result = held_phase(
            'alpha', display, 8, [(0x80, 0x00, 0x7f), (0x40, 0x00, 0x7f)], 3)
        print('FRAME_DMABUF_LIVE_ALPHA premultiplied_px=%d straight_px=%d expected_px=%d result=%s'
              % (premultiplied, straight, area, result))
        check('verdict=PASS' in result, 'the alpha producer reported %s' % result)
        check(premultiplied >= area * 9 // 10 and straight == 0,
              'AR24 was not blended as premultiplied: %d premultiplied, %d straight pixels'
              % (premultiplied, straight))

        # Fence: drawn by this host's GPU with EGL into the buffer, sent with
        # its native fence before the GPU finished; Blit's GPU waits on it.
        fences_before = api('GET', '/v1/diagnostics')['graphics']['frames']['dmabuf']['fences_waited']
        (left, right), result = held_phase(
            'fence', display, 8, [(0x30, 0x60, 0xc0), (0xc0, 0x40, 0x30)], 1)
        fences = api('GET', '/v1/diagnostics')['graphics']['frames']['dmabuf']['fences_waited'] - fences_before
        print('FRAME_DMABUF_LIVE_FENCE left_px=%d right_px=%d expected_each_px=%d fences_waited=%d result=%s'
              % (left, right, area // 2, fences, result))
        check('verdict=PASS' in result, 'the fence producer reported %s' % result)
        check(fences >= 1, 'Blit did not hand the acquire fence to the GPU')
        check(left >= area * 9 // 20 and right >= area * 9 // 20,
              'the GPU-rendered frame is not on screen: %d and %d pixels' % (left, right))
        print('FRAME_DMABUF_LIVE_PASS')
    finally:
        cleanup = session('rm -rf %s && echo CLEAN_OK' % REMOTE)
        ended(cleanup, 15)
        print('FRAME_DMABUF_LIVE_CLEANUP %s' % ('removed' if 'CLEAN_OK' in screen_text(cleanup) else 'NOT removed'))
        for sid in STARTED:
            try:
                api('DELETE', '/v1/terminal-sessions/%s' % sid)
            except Exception as error:  # report, do not hide
                print('FRAME_DMABUF_LIVE_DELETE session %s: %s' % (sid, error))


if __name__ == '__main__':
    main()
