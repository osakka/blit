#!/usr/bin/env python3
"""ADR 0226: a frame-channel picture counts as shown, and is never freed
for room, while a placement is held for its next frame (ADR 0202's `a=p`
that arrived before the picture) or while it holds an input route (ADR
0228, `INPUT sock` to a socket the gate listens on). Run by
graphics_memory_frames_gate.sh.

One session, a 1 MiB picture budget, set live:
  - frame picture 30 (192 KiB) lands, unshown, and gets a route
    (`INPUT ok 30`); it is the least recently shown picture of all;
  - Kitty picture 41 (256 KiB) arrives, unshown;
  - `a=p,i=20,C=1` is held for picture 20, whose frame then lands
    (192 KiB) and is placed;
  - shown Kitty pictures arrive until one is refused `ENOSPC`.
Kitty 41 is the one picture freed (`evicted-budget`); 30 keeps its pixels
and its route, and 20 its placement.
"""
import json
import os
import sys
import tempfile
import time
import urllib.request

sys.dont_write_bytecode = True  # importing the input gate leaves its tracked .pyc alone
sys.path.insert(0, os.path.dirname(os.path.abspath(__file__)))
import frame_input_gate as fig  # noqa: E402
import gate_env  # noqa: E402

fig.TAG = 'GRAPHICS_MEMORY_FRAMES'
check, wait_for = fig.check, fig.wait_for
MIB = 1024 * 1024


def apc(control, payload=''):
    return '\033_G%s;%s\033\\' % (control, payload)


def kitty_picture(control, width, height, rgba):
    import base64
    data = base64.b64encode(bytes(rgba) * (width * height)).decode()
    chunks = [data[i:i + 4096] for i in range(0, len(data), 4096)]
    out = []
    for index, chunk in enumerate(chunks):
        more = 1 if index + 1 < len(chunks) else 0
        head = ('%s,f=32,s=%d,v=%d,m=%d' % (control, width, height, more)) if index == 0 else 'm=%d' % more
        out.append(apc(head, chunk))
    return ''.join(out)


def put_settings(server, server_settings):
    current = server.api('GET', '/v1/settings')
    document = current['document'] or {}
    document['server'] = dict(document.get('server') or {}, **server_settings)
    request = urllib.request.Request(server.base + '/v1/settings', method='PUT',
                                     data=json.dumps({'document': document}).encode(),
                                     headers={'Content-Type': 'application/json',
                                              'If-Match': str(current['revision'])})
    with urllib.request.urlopen(request, timeout=10) as response:
        check(response.status == 200, 'settings PUT %d' % response.status)


def main():
    work = tempfile.mkdtemp(prefix='blit-memory-frames-')
    own = os.path.join(work, 'own-input')
    keep = [fig.listening_socket(own)]
    server = fig.Server(19663 + fig.OFFSET, work)
    try:
        put_settings(server, {'graphics_session_budget_mib': 1})
        a = fig.Session(server, work, 'a')
        ch = fig.Channel(a)

        def state():
            return server.api('GET', '/v1/terminal-sessions/%s' % a.id)

        def ids():
            return sorted(g['id'] for g in state()['graphics'])

        # The routed picture: unshown, and the oldest.
        ch.frame(30, 1, 256, 256)
        wait_for(lambda: a.graphics(30), 'frame picture 30 did not land')
        got = ch.ask('INPUT sock 30 %s\n' % own)
        check(got == b'INPUT ok 30\n', 'INPUT for 30 was answered %r' % got)
        # An unshown Kitty picture.
        a.write(kitty_picture('a=t,i=41,q=1', 256, 256, (200, 30, 30, 255)))
        wait_for(lambda: a.graphics(41), 'Kitty picture 41 did not land')
        # A placement held for picture 20's first frame, then the frame.
        a.write('\033[3;3H' + apc('a=p,i=20,C=1,q=2'))
        time.sleep(0.3)
        ch.frame(20, 1, 256, 256)
        wait_for(lambda: (a.graphics(20) or {}).get('placements'), 'held placement of 20 not applied')
        time.sleep(0.4)
        # Shown pictures until one is refused.
        refused = None
        for i in range(51, 60):
            a.write('\033[%d;%dH' % (i - 45, 20) + kitty_picture('a=T,i=%d,C=1,q=1' % i, 256, 256,
                                                                    (20, i, 200, 255)))
            time.sleep(0.5)
            if b'i=%d;E' % i in a.sent():
                refused = i
                break
        check(refused, 'no picture was refused: %s' % state()['graphics_memory'])
        check(b'\033_Gi=%d;ENOSPC\033\\' % refused in a.sent(), 'refusal %r is not ENOSPC' % a.sent()[-40:])
        freed = [e['detail'] for e in server.api('GET', '/v1/diagnostics/trace?kind=image_freed&limit=64')['events']
                 if 'evicted-budget' in e['detail']]
        check(len(freed) == 1 and ' i=41 ' in freed[0], 'freed for the budget: %s' % freed)
        now = ids()
        check(30 in now and 20 in now and 41 not in now, 'pictures left %s' % now)
        check(fig.route(server.input(a.id), 30) is not None, 'the route of 30 ended')
        check(a.graphics(20)['placements'], 'picture 20 lost its placement')
        memory = state()['graphics_memory']
        check(memory['freed']['budget'] == 1 and memory['refused']['budget'] == 1, 'reported %s' % memory)
        ch.close()
        print('%s_PASS route=kept(30) held_placement=kept(20) freed=kitty-41 refused=%d:ENOSPC backend=%s'
              % (fig.TAG, refused, gate_env.server_env().get('BLIT_KMS_COMPOSITOR', 'cpu')))
    finally:
        server.stop()
        for s in keep:
            s.close()


if __name__ == '__main__':
    main()
